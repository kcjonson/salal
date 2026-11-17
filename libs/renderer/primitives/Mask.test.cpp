// CSS masks on drawRect (docs/masks.md): what reaches the backend, headless on the null backend, and
// what the pixels hold, drawn through Primitives into a RenderTarget with a stencil buffer on a hidden
// GL 3.3 core context and read back (skipped where no context can be made).

#include "primitives/Primitives.h"

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "backend/gl/GLBackend.h"
#include "primitives/BatchRenderer.h"
#include "random/Rng.h"
#include "resources/RenderTarget.h"
#include "vector/Types.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace Renderer {

	using Primitives::MaskComposite;
	using Primitives::MaskImage;
	using Primitives::MaskLayer;

	namespace {
		// The four corners of an axis-aligned square, as a polygon outline.
		std::array<Foundation::Vec2, 4> square(float left, float top, float right, float bottom) {
			return {{{left, top}, {right, top}, {right, bottom}, {left, bottom}}};
		}

		// A width x height rectangle from the origin, for uploadInstancedMesh.
		renderer::TessellatedMesh rectangleMesh(float width, float height) {
			renderer::TessellatedMesh mesh;
			mesh.vertices = {{0.0F, 0.0F}, {width, 0.0F}, {width, height}, {0.0F, height}};
			mesh.indices = {0, 1, 2, 0, 2, 3};
			return mesh;
		}
	} // namespace

	// --- On the null backend ---

	class MaskBatchTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			auto owned = std::make_unique<backend::NullBackend>();
			gpu = owned.get();
			Primitives::init(std::move(owned), base_shader::source());
			Primitives::setCoordinateSystem(nullptr);
			Primitives::setViewport(64, 64);
			Primitives::beginFrame();
			gpu->clearRecord();
		}

		void TearDown() override { Primitives::shutdown(); }

		static void rect(float x, std::span<const MaskLayer> mask, int32_t key = 0) {
			Primitives::drawRect({.bounds = {x, 0.0F, 4.0F, 4.0F}, .style = {.fill = Foundation::Color::red()}, .mask = mask, .key = key});
		}

		// The draws that write colour: the masked and unmasked runs, not the mask builds.
		std::vector<backend::NullBackend::RecordedDraw> colourDraws() const {
			std::vector<backend::NullBackend::RecordedDraw> drawn;
			for (const backend::NullBackend::RecordedDraw& draw : gpu->draws()) {
				if (draw.call.writes != backend::ChannelWrites::None) {
					drawn.push_back(draw);
				}
			}
			return drawn;
		}

		// The batch's upload, as the last flush wrote it.
		std::vector<BatchInstance> uploaded() const {
			const backend::GeometryDesc*   geometry = gpu->geometry(gpu->draws().front().call.geometry);
			const std::span<const uint8_t> bytes = gpu->bufferContents(geometry->vertices);
			std::vector<BatchInstance>	   quads(bytes.size() / sizeof(BatchInstance));
			std::memcpy(quads.data(), bytes.data(), quads.size() * sizeof(BatchInstance));
			return quads;
		}

		backend::NullBackend* gpu = nullptr;
	};

	TEST_F(MaskBatchTest, EqualMasksInternOnceAndShareOneBuild) {
		const std::array<Foundation::Vec2, 4> first = square(0.0F, 0.0F, 8.0F, 8.0F);
		const std::array<Foundation::Vec2, 4> copy = first; // equal content, other memory
		const std::array<MaskLayer, 1>		  firstMask = {{{.image = MaskImage::polygon(first)}}};
		const std::array<MaskLayer, 1>		  copyMask = {{{.image = MaskImage::polygon(copy)}}};
		rect(0.0F, firstMask);
		rect(10.0F, copyMask);
		Primitives::endFrame();

		EXPECT_EQ(gpu->stencilClears().size(), 1U);
		const std::vector<backend::NullBackend::RecordedDraw> drawn = colourDraws();
		ASSERT_EQ(drawn.size(), 1U); // both rects in one run
		EXPECT_EQ(drawn[0].call.instanceCount, 2U);
		EXPECT_TRUE(drawn[0].call.stencil.test);

		// A different mask builds again.
		Primitives::beginFrame();
		gpu->clearRecord();
		const std::array<Foundation::Vec2, 4> other = square(0.0F, 0.0F, 9.0F, 8.0F);
		const std::array<MaskLayer, 1>		  otherMask = {{{.image = MaskImage::polygon(other)}}};
		rect(0.0F, firstMask);
		rect(10.0F, otherMask);
		Primitives::endFrame();
		EXPECT_EQ(gpu->stencilClears().size(), 2U);
		EXPECT_EQ(colourDraws().size(), 2U);
	}

	TEST_F(MaskBatchTest, AMaskCutsARunAndKeysKeepTheirOrder) {
		const std::array<MaskLayer, 1> mask = {{{.image = MaskImage::rect({0.0F, 0.0F, 32.0F, 32.0F})}}};
		rect(20.0F, mask, 2);
		rect(10.0F, {}, 1);
		rect(30.0F, mask, 3);
		Primitives::endFrame();

		const std::vector<backend::NullBackend::RecordedDraw> drawn = colourDraws();
		ASSERT_EQ(drawn.size(), 2U);
		EXPECT_FALSE(drawn[0].call.stencil.test);
		EXPECT_TRUE(drawn[1].call.stencil.test);
		const std::vector<BatchInstance> quads = uploaded();
		ASSERT_GE(quads.size(), 3U);
		EXPECT_EQ(quads[0].corners[0].x, 10.0F); // key 1
		EXPECT_EQ(quads[1].corners[0].x, 20.0F); // key 2
		EXPECT_EQ(quads[2].corners[0].x, 30.0F); // key 3
		EXPECT_EQ(drawn[1].call.firstInstance, 1U);
		EXPECT_EQ(drawn[1].call.instanceCount, 2U);
		// One build, before the masked run: an equal mask on both sides of the key sort.
		ASSERT_EQ(gpu->stencilClears().size(), 1U);
	}

	TEST_F(MaskBatchTest, LayersAreCopiedAtTheCallAndDrawnUnclippedWithNoColour) {
		std::array<Foundation::Vec2, 4> outline = square(0.0F, 0.0F, 6.0F, 6.0F);
		const std::array<MaskLayer, 1>	mask = {{{.image = MaskImage::polygon(outline)}}};
		Primitives::pushClip({0.0F, 0.0F, 2.0F, 2.0F});
		rect(0.0F, mask);
		Primitives::popClip();
		outline = square(40.0F, 40.0F, 50.0F, 50.0F); // after the call: the mask already holds the first outline
		Primitives::endFrame();

		const std::vector<BatchInstance> quads = uploaded();
		bool							 sawLayer = false;
		for (const backend::NullBackend::RecordedDraw& draw : gpu->draws()) {
			if (draw.call.writes != backend::ChannelWrites::None) {
				continue;
			}
			sawLayer = true;
			for (uint32_t index = draw.call.firstInstance; index < draw.call.firstInstance + draw.call.instanceCount; ++index) {
				EXPECT_EQ(quads[index].clip, Foundation::Vec4(0.0F));
				for (const Foundation::Vec2& corner : quads[index].corners) {
					EXPECT_LE(corner.x, 6.0F);
					EXPECT_LE(corner.y, 6.0F);
				}
			}
		}
		EXPECT_TRUE(sawLayer);
	}

	TEST_F(MaskBatchTest, AMaskedDrawOnATargetWithNoStencilDrawsNothing) {
		const std::array<MaskLayer, 1> mask = {{{.image = MaskImage::rect({0.0F, 0.0F, 32.0F, 32.0F})}}};
		gpu->setWindowStencil(false);
		rect(0.0F, mask);
		rect(10.0F, {});
		Primitives::endFrame();
		const std::vector<backend::NullBackend::RecordedDraw> drawn = colourDraws();
		ASSERT_EQ(drawn.size(), 1U);
		EXPECT_FALSE(drawn[0].call.stencil.test);
		EXPECT_TRUE(gpu->stencilClears().empty());

		// A render target made without one, decided at the flush against the target bound then.
		gpu->setWindowStencil(true);
		RenderTarget plain = RenderTarget::create(8, 8);
		ASSERT_TRUE(plain.isValid());
		Primitives::beginFrame();
		gpu->clearRecord();
		rect(0.0F, mask);
		plain.begin();
		Primitives::flush();
		plain.end();
		EXPECT_TRUE(colourDraws().empty());
	}

	TEST_F(MaskBatchTest, AMeshReleasedBeforeTheFlushLivesUntilItsMaskIsBuilt) {
		auto mesh = std::make_unique<InstancedMeshHandle>(Primitives::uploadInstancedMesh(rectangleMesh(4.0F, 4.0F), 1));
		ASSERT_TRUE(mesh->isValid());
		const size_t				   liveBefore = gpu->live().geometries;
		const std::array<MaskLayer, 1> mask = {{{.image = MaskImage::mesh(*mesh, {})}}};
		rect(0.0F, mask);
		mesh.reset();
		EXPECT_EQ(gpu->live().geometries, liveBefore);
		Primitives::endFrame();
		bool drewMesh = false;
		for (const backend::NullBackend::RecordedDraw& draw : gpu->draws()) {
			drewMesh = drewMesh || (draw.call.writes == backend::ChannelWrites::None && draw.call.indexCount == 6U);
		}
		EXPECT_TRUE(drewMesh);
		EXPECT_EQ(gpu->live().geometries, liveBefore - 1);
	}

	// The stencil-only draws of the last frame: the mask builds.
	std::vector<backend::NullBackend::RecordedDraw> buildDraws(const backend::NullBackend& gpu) {
		std::vector<backend::NullBackend::RecordedDraw> built;
		for (const backend::NullBackend::RecordedDraw& draw : gpu.draws()) {
			if (draw.call.writes == backend::ChannelWrites::None) {
				built.push_back(draw);
			}
		}
		return built;
	}

	TEST_F(MaskBatchTest, ARectMinusAUnionThatHoldsItsDrawsIsBuiltInvertedWithNoCoverPass) {
		const Foundation::Rect				  view{0.0F, 0.0F, 64.0F, 64.0F};
		const std::array<Foundation::Vec2, 4> first = square(4.0F, 4.0F, 20.0F, 20.0F);
		const std::array<Foundation::Vec2, 4> second = square(16.0F, 16.0F, 40.0F, 40.0F);
		const std::array<MaskLayer, 3>		  mask = {{
			   {.image = MaskImage::rect(view), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::polygon(first)},
			   {.image = MaskImage::polygon(second)},
		   }};
		Primitives::drawRect({.bounds = view, .style = {.fill = Foundation::Color::red()}, .mask = mask});
		Primitives::endFrame();

		ASSERT_EQ(gpu->stencilClears().size(), 1U);
		const std::vector<backend::NullBackend::RecordedDraw> built = buildDraws(*gpu);
		ASSERT_EQ(built.size(), 2U); // the union, a polygon a call; the rect draws nothing
		for (const backend::NullBackend::RecordedDraw& draw : built) {
			EXPECT_EQ(draw.call.stencil.pass, backend::StencilOperation::Replace);
			EXPECT_EQ(draw.call.stencil.writeMask, kMaskAccumulatorBit); // never the scratch bit
		}
		std::vector<backend::NullBackend::RecordedDraw> drawn = colourDraws();
		ASSERT_EQ(drawn.size(), 1U);
		EXPECT_EQ(drawn[0].call.stencil.reference, 0U); // where the union is not
		EXPECT_EQ(drawn[0].call.stencil.readMask, kMaskAccumulatorBit);

		// A draw under the same mask past the rect: the rect no longer holds every draw, so the mask
		// is built the general way, the rect into the scratch bit and two cover passes.
		Primitives::beginFrame();
		gpu->clearRecord();
		Primitives::drawRect({.bounds = view, .style = {.fill = Foundation::Color::red()}, .mask = mask});
		Primitives::drawRect({.bounds = {70.0F, 0.0F, 4.0F, 4.0F}, .style = {.fill = Foundation::Color::red()}, .mask = mask});
		Primitives::endFrame();
		ASSERT_EQ(gpu->stencilClears().size(), 1U);
		const std::vector<backend::NullBackend::RecordedDraw> general = buildDraws(*gpu);
		ASSERT_EQ(general.size(), 5U);
		EXPECT_EQ(general[2].call.stencil.writeMask, kMaskScratchBit);
		EXPECT_EQ(general[3].call.instanceCount, 1U);
		EXPECT_EQ(general[4].call.instanceCount, 1U);
		drawn = colourDraws();
		ASSERT_EQ(drawn.size(), 1U);
		EXPECT_EQ(drawn[0].call.stencil.reference, kMaskAccumulatorBit);
	}

	TEST_F(MaskBatchTest, ARectHoldsADrawOnlyWhenItIsAnAxisAlignedBoxRoundIt) {
		const Foundation::Rect		   view{0.0F, 0.0F, 32.0F, 32.0F};
		const std::array<MaskLayer, 2> mask = {{
			{.image = MaskImage::rect(view), .composite = MaskComposite::Subtract},
			{.image = MaskImage::rect({8.0F, 8.0F, 8.0F, 8.0F})},
		}};
		const auto					   coverPasses = [this] {
			for (const backend::NullBackend::RecordedDraw& draw : buildDraws(*gpu)) {
				if (draw.call.stencil.writeMask == kMaskScratchBit) {
					return true;
				}
			}
			return false;
		};

		// Scaled and moved, the rect stays a box and holds the draw under the same transform.
		Primitives::PushTransform(glm::scale(glm::translate(Foundation::Mat4(1.0F), {3.25F, 2.5F, 0.0F}), {0.75F, 0.75F, 1.0F}));
		Primitives::drawRect({.bounds = view, .style = {.fill = Foundation::Color::red()}, .mask = mask});
		Primitives::PopTransform();
		Primitives::endFrame();
		EXPECT_FALSE(coverPasses());

		// Turned, it is no longer an axis-aligned box.
		Primitives::beginFrame();
		gpu->clearRecord();
		Primitives::PushTransform(glm::rotate(Foundation::Mat4(1.0F), 0.3F, {0.0F, 0.0F, 1.0F}));
		Primitives::drawRect({.bounds = view, .style = {.fill = Foundation::Color::red()}, .mask = mask});
		Primitives::PopTransform();
		Primitives::endFrame();
		EXPECT_TRUE(coverPasses());

		// A masked rect's outside border grows its quad past the rect.
		Primitives::beginFrame();
		gpu->clearRecord();
		Primitives::drawRect(
			{.bounds = view,
			 .style =
				 {.fill = Foundation::Color::red(),
				  .border = Foundation::BorderStyle{.width = 2.0F, .position = Foundation::BorderPosition::Outside}},
			 .mask = mask}
		);
		Primitives::endFrame();
		EXPECT_TRUE(coverPasses());
	}

	TEST_F(MaskBatchTest, ARectWithTheDrawsOwnFloatBoundsHoldsItToTheBit) {
		// The game's cover: a view off the pixel grid, the rect layer and the masked rect from the same
		// bounds. Both make their corners the same way, so the layer holds the quad exactly. The first
		// once failed: the quad's right edge, from its centre, came out 1494.71179 against 1494.71167.
		foundation::Rng				  random(97);
		std::vector<Foundation::Rect> views = {{586.1632F, 586.1632F, 908.5485F, 908.5485F}};
		while (views.size() < 500) {
			const float x = (random.nextFloat() * 4000.0F) - 1000.0F;
			const float y = (random.nextFloat() * 4000.0F) - 1000.0F;
			views.push_back({x, y, 1.0F + (random.nextFloat() * 3000.0F), 1.0F + (random.nextFloat() * 3000.0F)});
		}
		for (const Foundation::Rect& view : views) {
			const std::array<Foundation::Vec2, 3> sight = {{
				{view.x + (view.width * 0.25F), view.y + (view.height * 0.25F)},
				{view.x + (view.width * 0.75F), view.y + (view.height * 0.5F)},
				{view.x + (view.width * 0.5F), view.y + (view.height * 0.75F)},
			}};
			const std::array<MaskLayer, 2>		  mask = {{
				   {.image = MaskImage::rect(view), .composite = MaskComposite::Subtract},
				   {.image = MaskImage::polygon(sight)},
			   }};
			Primitives::drawRect({.bounds = view, .style = {.fill = Foundation::Color::red()}, .mask = mask});
			Primitives::endFrame();
			const std::vector<backend::NullBackend::RecordedDraw> built = buildDraws(*gpu);
			ASSERT_EQ(built.size(), 1U) << std::setprecision(9) << "view " << view.x << ", " << view.y << ", " << view.width << ", "
										<< view.height << ": built the general way";
			EXPECT_EQ(built[0].call.stencil.writeMask, kMaskAccumulatorBit);
			Primitives::beginFrame();
			gpu->clearRecord();
		}
	}

	// --- Pixels on GL ---

	class MaskDrawTest : public ::testing::Test {
	  protected:
		static constexpr int kSize = 32;

		void SetUp() override {
			if (glfwInit() != GLFW_TRUE) {
				GTEST_SKIP() << "GLFW initialization failed (no display available in CI)";
			}
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
			glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
			window = glfwCreateWindow(kSize, kSize, "offscreen", nullptr, nullptr);
			if (window == nullptr) {
				glfwTerminate();
				GTEST_SKIP() << "Could not create GLFW window (no OpenGL context available)";
			}
			glfwMakeContextCurrent(window);
			std::unique_ptr<backend::gl::GLBackend> created = backend::gl::GLBackend::create();
			if (created == nullptr) {
				glfwDestroyWindow(window);
				window = nullptr;
				glfwTerminate();
				GTEST_SKIP() << "GLEW initialization failed";
			}
			Primitives::init(std::move(created), base_shader::source());
			Primitives::setCoordinateSystem(nullptr);
			Primitives::setViewport(kSize, kSize);
			target = RenderTarget::create(kSize, kSize, TextureFormat::Rgba8, RenderTarget::Attachments::ColourAndStencil);
			ASSERT_TRUE(target.isValid());
		}

		void TearDown() override {
			if (window != nullptr) {
				target = RenderTarget();
				Primitives::shutdown();
				glfwDestroyWindow(window);
				glfwTerminate();
			}
		}

		// One frame into the target, its colour cleared to opaque blue, then `draw`, then read back,
		// with the frame's draw calls, which tell the builds apart: a Subtract built the general way
		// draws its layer and two cover passes, one that holds the draws draws nothing.
		template <typename Draw>
		void render(Draw draw) {
			target.begin();
			Primitives::setViewport(kSize, kSize);
			Primitives::clear(Foundation::Color(0.0F, 0.0F, 1.0F, 1.0F));
			Primitives::beginFrameStats();
			Primitives::beginFrame();
			draw();
			Primitives::endFrame();
			drawCalls = Primitives::getStats().drawCalls;
			target.end();
			pixels.assign(static_cast<size_t>(kSize) * kSize * 4, 0);
			target.readPixels(pixels);
		}

		// The pixel at (x, y), y down; readPixels rows run bottom up.
		std::array<int, 4> pixel(int x, int y) const {
			const size_t index = ((static_cast<size_t>(kSize - 1 - y) * kSize) + static_cast<size_t>(x)) * 4;
			return {pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
		}

		static void expectNear(const std::array<int, 4>& actual, const std::array<int, 4>& expected, int tolerance = 1) {
			for (size_t channel = 0; channel < 4; ++channel) {
				EXPECT_LE(std::abs(actual[channel] - expected[channel]), tolerance)
					<< "channel " << channel << ": " << actual[0] << "," << actual[1] << "," << actual[2] << "," << actual[3];
			}
		}

		// The whole target in `color` under `mask`; past its edges, so the rect's anti-aliased edge falls outside.
		static void fill(const Foundation::Color& color, std::span<const MaskLayer> mask) {
			Primitives::drawRect({.bounds = kView, .style = {.fill = color}, .mask = mask});
		}

		// `scene` drawn twice, then compared byte for byte. The second time, it calls
		// asGeneralBuild with each of its masks, which draws a transparent rect under the mask far
		// past the target, so no layer holds every draw under it and the mask is built the general
		// way, with cover passes for every Subtract, where the first may be built inverted. The first
		// render's draw calls are left in firstDrawCalls.
		template <typename Scene>
		void expectSameAsGeneralBuild(Scene scene) {
			render([&] { scene(false); });
			firstDrawCalls = drawCalls;
			const std::vector<uint8_t> built = pixels;
			render([&] { scene(true); });
			ASSERT_EQ(built.size(), pixels.size());
			for (size_t index = 0; index < pixels.size(); index += 4) {
				if (std::memcmp(&built[index], &pixels[index], 4) != 0) {
					const auto offset = static_cast<int>(index / 4);
					ADD_FAILURE() << "pixel (" << offset % kSize << ", " << kSize - 1 - (offset / kSize) << "): " << int{built[index]}
								  << "," << int{built[index + 1]} << "," << int{built[index + 2]} << "," << int{built[index + 3]}
								  << " against the general build's " << int{pixels[index]} << "," << int{pixels[index + 1]} << ","
								  << int{pixels[index + 2]} << "," << int{pixels[index + 3]};
					return;
				}
			}
		}

		static void asGeneralBuild(bool general, std::span<const MaskLayer> mask) {
			if (general) {
				Primitives::drawRect({.bounds = {1000.0F, 1000.0F, 4.0F, 4.0F}, .style = {.fill = {0.0F, 0.0F, 0.0F, 0.0F}}, .mask = mask});
			}
		}

		static constexpr Foundation::Rect	kView{-4.0F, -4.0F, kSize + 8.0F, kSize + 8.0F};
		static constexpr std::array<int, 4> kBlue = {0, 0, 255, 255};
		static constexpr std::array<int, 4> kRed = {255, 0, 0, 255};
		static constexpr std::array<int, 4> kGreen = {0, 255, 0, 255};

		GLFWwindow*			 window = nullptr;
		RenderTarget		 target;
		std::vector<uint8_t> pixels;
		uint32_t			 drawCalls = 0;
		uint32_t			 firstDrawCalls = 0;
	};

	TEST_F(MaskDrawTest, ARectMinusAPolygon) {
		const std::array<Foundation::Vec2, 4> sight = square(8.0F, 8.0F, 24.0F, 24.0F);
		const std::array<MaskLayer, 2>		  mask = {{
			   {.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::polygon(sight)},
		   }};
		render([&] { fill(Foundation::Color::red(), mask); });
		expectNear(pixel(2, 2), kRed);
		expectNear(pixel(30, 30), kRed);
		expectNear(pixel(7, 16), kRed);
		expectNear(pixel(8, 8), kBlue);
		expectNear(pixel(16, 16), kBlue);
		expectNear(pixel(23, 23), kBlue);
		expectNear(pixel(24, 16), kRed);
	}

	TEST_F(MaskDrawTest, ARectMinusAMeshPlusAPolygon) {
		// The unseen cover: the view minus what has been seen (a mesh in world space) and a shape pending.
		InstancedMeshHandle					  seen = Primitives::uploadInstancedMesh(rectangleMesh(8.0F, kSize), 1);
		const std::array<Foundation::Vec2, 4> pending = square(24.0F, 0.0F, 32.0F, 32.0F);
		ASSERT_TRUE(seen.isValid());
		// World equals screen: the camera at the viewport's centre, one pixel a metre.
		const Primitives::WorldCamera  camera{.position = {16.0F, 16.0F}};
		const std::array<MaskLayer, 3> mask = {{
			{.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			{.image = MaskImage::mesh(seen, camera)},
			{.image = MaskImage::polygon(pending)},
		}};
		render([&] { fill(Foundation::Color::red(), mask); });
		expectNear(pixel(4, 16), kBlue);
		expectNear(pixel(7, 2), kBlue);
		expectNear(pixel(8, 2), kRed);
		expectNear(pixel(16, 16), kRed);
		expectNear(pixel(23, 30), kRed);
		expectNear(pixel(24, 30), kBlue);
	}

	TEST_F(MaskDrawTest, TheSameLayersReorderedGiveTheOtherRegion) {
		const std::array<Foundation::Vec2, 4> outline = square(8.0F, 8.0F, 24.0F, 24.0F);
		const Foundation::Rect				  leftHalf{0.0F, 0.0F, 16.0F, 32.0F};
		const std::array<MaskLayer, 2>		  halfMinusSquare = {{
			   {.image = MaskImage::rect(leftHalf), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::polygon(outline)},
		   }};
		const std::array<MaskLayer, 2>		  squareMinusHalf = {{
			   {.image = MaskImage::polygon(outline), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::rect(leftHalf)},
		   }};
		render([&] { fill(Foundation::Color::red(), halfMinusSquare); });
		expectNear(pixel(4, 16), kRed); // the half, outside the square
		expectNear(pixel(12, 4), kRed);
		expectNear(pixel(12, 16), kBlue); // both
		expectNear(pixel(20, 16), kBlue); // the square alone
		render([&] { fill(Foundation::Color::red(), squareMinusHalf); });
		expectNear(pixel(4, 16), kBlue);
		expectNear(pixel(12, 4), kBlue);
		expectNear(pixel(12, 16), kBlue);
		expectNear(pixel(20, 16), kRed);
		expectNear(pixel(28, 16), kBlue);
	}

	TEST_F(MaskDrawTest, TheBottomLayersCompositeIsIgnored) {
		const std::array<Foundation::Vec2, 4> outline = square(8.0F, 8.0F, 24.0F, 24.0F);
		const std::array<MaskLayer, 1>		  alone = {{{.image = MaskImage::polygon(outline), .composite = MaskComposite::Subtract}}};
		render([&] { fill(Foundation::Color::red(), alone); });
		expectNear(pixel(16, 16), kRed);
		expectNear(pixel(4, 4), kBlue);
	}

	TEST_F(MaskDrawTest, AConcavePolygonCoversItsOutlineAndNotItsNotch) {
		const std::array<Foundation::Vec2, 6> ell = {
			{{4.0F, 4.0F}, {28.0F, 4.0F}, {28.0F, 12.0F}, {12.0F, 12.0F}, {12.0F, 28.0F}, {4.0F, 28.0F}}
		};
		const std::array<MaskLayer, 1> mask = {{{.image = MaskImage::polygon(ell)}}};
		render([&] { fill(Foundation::Color::red(), mask); });
		expectNear(pixel(24, 8), kRed);
		expectNear(pixel(8, 24), kRed);
		expectNear(pixel(20, 20), kBlue); // the notch
		expectNear(pixel(2, 2), kBlue);
	}

	TEST_F(MaskDrawTest, TwoMasksInOneFrameEachMaskTheirOwnDraw) {
		const std::array<MaskLayer, 1> left = {{{.image = MaskImage::rect({0.0F, 0.0F, 16.0F, 32.0F})}}};
		const std::array<MaskLayer, 1> right = {{{.image = MaskImage::rect({16.0F, 0.0F, 16.0F, 32.0F})}}};
		render([&] {
			fill(Foundation::Color::red(), left);
			fill(Foundation::Color::green(), right);
			fill(Foundation::Color::red(), left); // the first mask again, after the second
		});
		expectNear(pixel(4, 16), kRed);
		expectNear(pixel(28, 16), kGreen);
	}

	TEST_F(MaskDrawTest, AMaskedDrawUnderAClipIsClippedAndItsMaskIsNot) {
		const std::array<MaskLayer, 1> topHalf = {{{.image = MaskImage::rect({0.0F, 0.0F, 32.0F, 16.0F})}}};
		render([&] {
			Primitives::pushClip({0.0F, 0.0F, 16.0F, 32.0F});
			fill(Foundation::Color::red(), topHalf);
			Primitives::popClip();
			fill(Foundation::Color::green(), topHalf); // the same mask, unclipped
		});
		expectNear(pixel(4, 4), kGreen);
		expectNear(pixel(28, 4), kGreen);
		expectNear(pixel(4, 28), kBlue);

		render([&] {
			Primitives::pushClip({0.0F, 0.0F, 16.0F, 32.0F});
			fill(Foundation::Color::red(), topHalf);
			Primitives::popClip();
		});
		expectNear(pixel(4, 4), kRed);
		expectNear(pixel(28, 4), kBlue); // the draw's clip
		expectNear(pixel(4, 28), kBlue); // the mask
	}

	TEST_F(MaskDrawTest, AMeshReleasedBeforeTheFlushStillMasks) {
		render([&] {
			InstancedMeshHandle			   seen = Primitives::uploadInstancedMesh(rectangleMesh(kSize, 8.0F), 1);
			const std::array<MaskLayer, 1> mask = {{{.image = MaskImage::mesh(seen, {.position = {16.0F, 16.0F}})}}};
			fill(Foundation::Color::red(), mask);
			Primitives::releaseInstancedMesh(seen);
		});
		expectNear(pixel(16, 4), kRed);
		expectNear(pixel(16, 12), kBlue);
	}

	TEST_F(MaskDrawTest, AFlushLeavesTheFixedStateAndAGamesLeftoverStencilChangesNothing) {
		const std::array<MaskLayer, 1> left = {{{.image = MaskImage::rect({0.0F, 0.0F, 16.0F, 32.0F})}}};
		render([&] {
			// A game's own GL before the renderer's: stencil bits set everywhere, a test left on.
			glClearStencil(0xFF);
			glClear(GL_STENCIL_BUFFER_BIT);
			glEnable(GL_STENCIL_TEST);
			glStencilFunc(GL_NEVER, 0, 0xFF);
			glStencilMask(0x00);
			Primitives::invalidateState();
			fill(Foundation::Color::red(), left);
			Primitives::flush();
			EXPECT_FALSE(glIsEnabled(GL_STENCIL_TEST));
			std::array<GLboolean, 4> colourWrites{};
			glGetBooleanv(GL_COLOR_WRITEMASK, colourWrites.data());
			EXPECT_EQ(colourWrites, (std::array<GLboolean, 4>{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE}));
			GLint stencilWrites = 0;
			glGetIntegerv(GL_STENCIL_WRITEMASK, &stencilWrites);
			EXPECT_EQ(stencilWrites & 0xFF, 0xFF);
			Primitives::drawRect({.bounds = {24.0F, 24.0F, 8.0F, 8.0F}, .style = {.fill = Foundation::Color::green()}});
		});
		expectNear(pixel(4, 4), kRed);
		expectNear(pixel(20, 4), kBlue);
		expectNear(pixel(28, 28), kGreen);
		EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
	}

	// --- A mask built inverted against the general build ---

	TEST_F(MaskDrawTest, ARectMinusOverlappingLayersAndAMeshIsTheGeneralBuild) {
		InstancedMeshHandle					  seen = Primitives::uploadInstancedMesh(rectangleMesh(9.5F, 20.0F), 1);
		const std::array<Foundation::Vec2, 4> first = square(6.5F, 5.0F, 20.0F, 19.25F);
		const std::array<Foundation::Vec2, 3> second = {{{14.0F, 12.0F}, {31.0F, 18.5F}, {17.0F, 30.0F}}};
		ASSERT_TRUE(seen.isValid());
		const std::array<MaskLayer, 4> mask = {{
			{.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			{.image = MaskImage::mesh(seen, {.position = {16.0F, 16.0F}})},
			{.image = MaskImage::polygon(first)},
			{.image = MaskImage::polygon(second)},
		}};
		expectSameAsGeneralBuild([&](bool general) {
			fill(Foundation::Color::red(), mask);
			asGeneralBuild(general, mask);
		});
		expectNear(pixel(2, 30), kRed);
		expectNear(pixel(10, 10), kBlue);
		expectNear(pixel(18, 20), kBlue);
	}

	TEST_F(MaskDrawTest, PartlyCoveredMaskedRectsWithTheirOwnEdgesAreTheGeneralBuild) {
		// The rect is each draw's own bounds, or the box round two, edges off the pixel grid, so the
		// draws' anti-aliased edges and rounded corners fall inside the target, on the rect's edges.
		const Foundation::Rect				  bounds{5.25F, 6.75F, 20.5F, 17.25F};
		const Foundation::Rect				  pair{3.5F, 3.5F, 25.0F, 25.0F};
		const std::array<Foundation::Vec2, 4> crossing = square(10.0F, 2.0F, 18.0F, 30.0F);
		const std::array<MaskLayer, 2>		  own = {{
			   {.image = MaskImage::rect(bounds), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::polygon(crossing)},
		   }};
		const std::array<MaskLayer, 2>		  round = {{
			   {.image = MaskImage::rect(pair), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::polygon(crossing)},
		   }};
		expectSameAsGeneralBuild([&](bool general) {
			Primitives::drawRect(
				{.bounds = bounds,
				 .style = {.fill = Foundation::Color::red(), .border = Foundation::BorderStyle{.width = 0.0F, .cornerRadius = 4.5F}},
				 .mask = own}
			);
			asGeneralBuild(general, own);
		});
		expectSameAsGeneralBuild([&](bool general) {
			Primitives::drawRect({.bounds = {3.5F, 3.5F, 12.0F, 12.0F}, .style = {.fill = Foundation::Color::green()}, .mask = round});
			Primitives::drawRect({.bounds = {16.5F, 16.5F, 12.0F, 12.0F}, .style = {.fill = Foundation::Color::red()}, .mask = round});
			asGeneralBuild(general, round);
		});
	}

	TEST_F(MaskDrawTest, ClippedMaskedRectsAreTheGeneralBuild) {
		const std::array<Foundation::Vec2, 4> sight = square(4.0F, 9.0F, 26.0F, 21.0F);
		const std::array<MaskLayer, 2>		  mask = {{
			   {.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::polygon(sight)},
		   }};
		expectSameAsGeneralBuild([&](bool general) {
			Primitives::pushClip({7.5F, 0.0F, 13.0F, 32.0F});
			fill(Foundation::Color::red(), mask);
			asGeneralBuild(general, mask);
			Primitives::popClip();
		});
		expectNear(pixel(10, 4), kRed);
		expectNear(pixel(10, 16), kBlue);
		expectNear(pixel(4, 4), kBlue); // the clip
	}

	TEST_F(MaskDrawTest, ARectThatDoesNotHoldTheDrawIsBuiltTheGeneralWay) {
		const std::array<Foundation::Vec2, 4> sight = square(12.0F, 12.0F, 30.0F, 30.0F);
		const std::array<MaskLayer, 2>		  mask = {{
			   {.image = MaskImage::rect({8.0F, 8.0F, 16.0F, 16.0F}), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::polygon(sight)},
		   }};
		render([&] { fill(Foundation::Color::red(), mask); });
		EXPECT_EQ(drawCalls, 5U);		// the rect into the scratch bit, the polygon, two cover passes and the fill
		expectNear(pixel(4, 4), kBlue); // outside the rect
		expectNear(pixel(10, 10), kRed);
		expectNear(pixel(16, 16), kBlue); // under the sight
		expectNear(pixel(28, 28), kBlue);
	}

	TEST_F(MaskDrawTest, ARectMinusAnEmptyUnionIsTheGeneralBuild) {
		const std::array<Foundation::Vec2, 2> nothing = {{{4.0F, 4.0F}, {20.0F, 20.0F}}}; // no polygon: no triangles
		const std::array<MaskLayer, 2>		  mask = {{
			   {.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::polygon(nothing)},
		   }};
		expectSameAsGeneralBuild([&](bool general) {
			fill(Foundation::Color::red(), mask);
			asGeneralBuild(general, mask);
		});
		expectNear(pixel(0, 0), kRed);
		expectNear(pixel(12, 12), kRed);
		expectNear(pixel(31, 31), kRed);
	}

	TEST_F(MaskDrawTest, TwoCoversInOneFrameAreTheGeneralBuild) {
		// Worldsim's: a translucent fill over the view minus the sights, then an opaque one over the
		// view minus what was seen and what is pending.
		InstancedMeshHandle					  seen = Primitives::uploadInstancedMesh(rectangleMesh(14.0F, 14.0F), 1);
		const std::array<Foundation::Vec2, 4> sight = square(2.0F, 2.0F, 9.5F, 11.0F);
		const std::array<Foundation::Vec2, 3> pending = {{{20.0F, 18.0F}, {31.0F, 31.0F}, {18.0F, 29.0F}}};
		ASSERT_TRUE(seen.isValid());
		const std::array<MaskLayer, 2> dim = {{
			{.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			{.image = MaskImage::polygon(sight)},
		}};
		const std::array<MaskLayer, 3> unseen = {{
			{.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			{.image = MaskImage::mesh(seen, {.position = {16.0F, 16.0F}})},
			{.image = MaskImage::polygon(pending)},
		}};
		expectSameAsGeneralBuild([&](bool general) {
			fill(Foundation::Color(0.0F, 0.0F, 0.0F, 0.6F), dim);
			fill(Foundation::Color::green(), unseen);
			asGeneralBuild(general, dim);
			asGeneralBuild(general, unseen);
		});
		expectNear(pixel(4, 4), kBlue);				   // in sight
		expectNear(pixel(12, 4), {0, 0, 102, 255}, 2); // seen, dimmed
		expectNear(pixel(28, 4), kGreen);			   // never seen
	}

	TEST_F(MaskDrawTest, ARectUnderAScaleAndMoveIsTheGeneralBuild) {
		const Foundation::Rect				  view{0.0F, 0.0F, 40.0F, 40.0F};
		const std::array<Foundation::Vec2, 4> sight = square(10.0F, 7.0F, 27.0F, 33.0F);
		const std::array<MaskLayer, 2>		  mask = {{
			   {.image = MaskImage::rect(view), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::polygon(sight)},
		   }};
		expectSameAsGeneralBuild([&](bool general) {
			Primitives::PushTransform(glm::scale(glm::translate(Foundation::Mat4(1.0F), {3.25F, 2.5F, 0.0F}), {0.7F, 0.7F, 1.0F}));
			Primitives::drawRect({.bounds = view, .style = {.fill = Foundation::Color::red()}, .mask = mask});
			asGeneralBuild(general, mask);
			Primitives::PopTransform();
		});
	}

	TEST_F(MaskDrawTest, LayersOverAnInvertedBuildAreTheGeneralBuild) {
		// An Add over the inverted union, and a Subtract by a layer that does not hold the draw.
		const std::array<Foundation::Vec2, 4> under = square(4.0F, 4.0F, 20.0F, 20.0F);
		const std::array<Foundation::Vec2, 4> over = square(14.0F, 14.0F, 28.0F, 28.0F);
		const std::array<MaskLayer, 3>		  added = {{
			   {.image = MaskImage::polygon(over)},
			   {.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::polygon(under)},
		   }};
		const std::array<MaskLayer, 3>		  subtracted = {{
			   {.image = MaskImage::polygon(over), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::polygon(under)},
		   }};
		expectSameAsGeneralBuild([&](bool general) {
			fill(Foundation::Color::red(), added);
			asGeneralBuild(general, added);
		});
		expectNear(pixel(8, 8), kBlue);	 // the union alone
		expectNear(pixel(16, 16), kRed); // the added layer
		expectNear(pixel(2, 30), kRed);
		expectNear(pixel(24, 24), kRed);
		expectSameAsGeneralBuild([&](bool general) {
			fill(Foundation::Color::red(), subtracted);
			asGeneralBuild(general, subtracted);
		});
		expectNear(pixel(16, 16), kRed);  // the layer where the union is: outside the rect minus the union
		expectNear(pixel(24, 24), kBlue); // the layer alone
		expectNear(pixel(2, 30), kBlue);
	}

	TEST_F(MaskDrawTest, AMeshLayerUnderOrOverAnInvertedBuildIsTheGeneralBuild) {
		// World equals screen: the mesh covers x 0 to 8, the polygon overlaps it.
		InstancedMeshHandle					  seen = Primitives::uploadInstancedMesh(rectangleMesh(8.0F, kSize), 1);
		const std::array<Foundation::Vec2, 4> sight = square(4.0F, 4.0F, 28.0F, 28.0F);
		ASSERT_TRUE(seen.isValid());
		const Primitives::WorldCamera  camera{.position = {16.0F, 16.0F}};
		const std::array<MaskLayer, 3> meshAtTheBottom = {{
			{.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			{.image = MaskImage::polygon(sight)},
			{.image = MaskImage::mesh(seen, camera)},
		}};
		const std::array<MaskLayer, 3> meshOnTop = {{
			{.image = MaskImage::mesh(seen, camera)},
			{.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			{.image = MaskImage::polygon(sight)},
		}};
		expectSameAsGeneralBuild([&](bool general) {
			fill(Foundation::Color::red(), meshAtTheBottom);
			asGeneralBuild(general, meshAtTheBottom);
		});
		EXPECT_EQ(firstDrawCalls, 3U);	// the mesh, the polygon and the fill
		expectNear(pixel(2, 2), kBlue); // the mesh
		expectNear(pixel(6, 16), kBlue);
		expectNear(pixel(16, 16), kBlue); // the polygon
		expectNear(pixel(30, 30), kRed);
		expectSameAsGeneralBuild([&](bool general) {
			fill(Foundation::Color::red(), meshOnTop);
			asGeneralBuild(general, meshOnTop);
		});
		EXPECT_EQ(firstDrawCalls, 3U); // the polygon, the mesh over the inverted union and the fill
		expectNear(pixel(2, 2), kRed);
		expectNear(pixel(6, 16), kRed); // the mesh where the polygon is
		expectNear(pixel(16, 16), kBlue);
		expectNear(pixel(30, 30), kRed);
	}

	TEST_F(MaskDrawTest, TwoRectsThatHoldTheDrawSubtractedInARowGiveTheUnion) {
		const std::array<Foundation::Vec2, 4> first = square(4.0F, 4.0F, 14.0F, 14.0F);
		const std::array<Foundation::Vec2, 4> second = square(10.0F, 18.0F, 28.0F, 26.0F);
		const std::array<MaskLayer, 4>		  mask = {{
			   {.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::polygon(first)},
			   {.image = MaskImage::polygon(second)},
		   }};
		expectSameAsGeneralBuild([&](bool general) {
			fill(Foundation::Color::red(), mask);
			asGeneralBuild(general, mask);
		});
		EXPECT_EQ(firstDrawCalls, 3U); // the two polygons and the fill
		expectNear(pixel(8, 8), kRed);
		expectNear(pixel(20, 22), kRed);
		expectNear(pixel(2, 2), kBlue);
		expectNear(pixel(20, 8), kBlue);
		expectNear(pixel(30, 30), kBlue);
	}

	TEST_F(MaskDrawTest, AnInvertedAndAPlainMaskInAlternatingKeyOrderEachMaskTheirOwnDraws) {
		// A, the view minus a vertical band, built inverted; B, a horizontal band, not. Drawn A, B, A
		// by key, so A is built again after B, and each run reads its own build's polarity.
		const std::array<Foundation::Vec2, 4> vertical = square(12.0F, 0.0F, 20.0F, 32.0F);
		const std::array<Foundation::Vec2, 4> horizontal = square(0.0F, 12.0F, 32.0F, 20.0F);
		const std::array<MaskLayer, 2>		  inverted = {{
			   {.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			   {.image = MaskImage::polygon(vertical)},
		   }};
		const std::array<MaskLayer, 1>		  plain = {{{.image = MaskImage::polygon(horizontal)}}};
		render([&] {
			Primitives::drawRect(
				{.bounds = {-4.0F, -4.0F, 40.0F, 14.0F}, .style = {.fill = Foundation::Color::red()}, .mask = inverted, .key = 1}
			);
			Primitives::drawRect({.bounds = kView, .style = {.fill = Foundation::Color::green()}, .mask = plain, .key = 2});
			Primitives::drawRect(
				{.bounds = {-4.0F, 22.0F, 40.0F, 14.0F}, .style = {.fill = Foundation::Color::white()}, .mask = inverted, .key = 3}
			);
		});
		EXPECT_EQ(drawCalls, 6U); // each build one polygon, then its run
		expectNear(pixel(4, 4), kRed);
		expectNear(pixel(16, 4), kBlue); // A's band
		expectNear(pixel(4, 16), kGreen);
		expectNear(pixel(16, 16), kGreen);
		expectNear(pixel(4, 28), {255, 255, 255, 255});
		expectNear(pixel(16, 28), kBlue);
	}

	TEST_F(MaskDrawTest, FloatRectsOffThePixelGridAreBuiltInvertedAndAreTheGeneralBuild) {
		// The rect layer and the masked rect from the same float bounds, as the game's cover is.
		foundation::Rng random(97);
		for (int attempt = 0; attempt < 6; ++attempt) {
			const Foundation::Rect view{
				(random.nextFloat() * 10.0F) - 5.0F,
				(random.nextFloat() * 10.0F) - 5.0F,
				16.0F + (random.nextFloat() * 20.0F),
				16.0F + (random.nextFloat() * 20.0F)
			};
			const std::array<Foundation::Vec2, 3> sight = {{
				{view.x + (view.width * 0.2F), view.y + (view.height * 0.3F)},
				{view.x + (view.width * 0.8F), view.y + (view.height * 0.45F)},
				{view.x + (view.width * 0.4F), view.y + (view.height * 0.85F)},
			}};
			const std::array<MaskLayer, 2>		  mask = {{
				   {.image = MaskImage::rect(view), .composite = MaskComposite::Subtract},
				   {.image = MaskImage::polygon(sight)},
			   }};
			expectSameAsGeneralBuild([&](bool general) {
				Primitives::drawRect({.bounds = view, .style = {.fill = Foundation::Color::red()}, .mask = mask});
				asGeneralBuild(general, mask);
			});
			EXPECT_EQ(firstDrawCalls, 2U) << "attempt " << attempt; // the polygon and the fill
		}
	}

} // namespace Renderer
