// Masks (docs/masks.md): what reaches the backend, headless on the null backend, and what the
// pixels hold, drawn through Primitives into a RenderTarget with a mask on a hidden GL 3.3 core
// context and read back (skipped where no context can be made).

#include "primitives/Primitives.h"

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "backend/gl/GLBackend.h"
#include "resources/RenderTarget.h"
#include "vector/Types.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace Renderer {

	using Primitives::Mask;
	using Primitives::MaskCompare;
	using Primitives::MaskMode;

	namespace {
		constexpr Mask write(uint8_t value) {
			return {.mode = MaskMode::Write, .value = value};
		}

		constexpr Mask test(uint8_t value, MaskCompare compare = MaskCompare::Equal) {
			return {.mode = MaskMode::Test, .value = value, .compare = compare};
		}

		void rect(float x, float y, float width, float height, const Foundation::Color& color, int32_t key = 0) {
			Primitives::drawRect({.bounds = {x, y, width, height}, .style = {.fill = color}, .key = key});
		}

		// A width x height rectangle from the origin, for drawInstanced.
		renderer::TessellatedMesh square(float width, float height) {
			renderer::TessellatedMesh mesh;
			mesh.vertices = {{0.0F, 0.0F}, {width, 0.0F}, {width, height}, {0.0F, height}};
			mesh.indices = {0, 1, 2, 0, 2, 3};
			return mesh;
		}
	} // namespace

	// --- On the null backend ---

	class MaskFlushTest : public ::testing::Test {
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

		backend::NullBackend* gpu = nullptr;
	};

	TEST_F(MaskFlushTest, AChangeFlushesWhatWasAddedUnderTheMaskBeforeIt) {
		rect(0.0F, 0.0F, 4.0F, 4.0F, Foundation::Color::red());
		Primitives::setMask(write(1));
		ASSERT_EQ(gpu->draws().size(), 1U);
		EXPECT_EQ(gpu->draws()[0].call.mask, Mask{});

		rect(0.0F, 0.0F, 4.0F, 4.0F, Foundation::Color::red());
		Primitives::setMask(write(1)); // no change, no flush
		EXPECT_EQ(gpu->draws().size(), 1U);
		Primitives::setMask(test(1));
		ASSERT_EQ(gpu->draws().size(), 2U);
		EXPECT_EQ(gpu->draws()[1].call.mask, write(1));

		rect(0.0F, 0.0F, 4.0F, 4.0F, Foundation::Color::red());
		Primitives::setMask({});
		ASSERT_EQ(gpu->draws().size(), 3U);
		EXPECT_EQ(gpu->draws()[2].call.mask, test(1));
		EXPECT_EQ(Primitives::getMask(), Mask{});
	}

	TEST_F(MaskFlushTest, AChangeIsAnOrderBoundaryWhateverTheKeys) {
		Primitives::setMask(write(1));
		rect(0.0F, 0.0F, 4.0F, 4.0F, Foundation::Color::red(), 100);
		Primitives::setMask(test(1));
		rect(10.0F, 0.0F, 4.0F, 4.0F, Foundation::Color::red(), -100);
		rect(20.0F, 0.0F, 4.0F, 4.0F, Foundation::Color::red(), -200);
		Primitives::endFrame();

		ASSERT_EQ(gpu->draws().size(), 2U);
		EXPECT_EQ(gpu->draws()[0].call.mask, write(1));
		EXPECT_EQ(gpu->draws()[1].call.mask, test(1));
		// Within the test mask the keys still sort: the -200 rect first.
		const backend::GeometryDesc*   geometry = gpu->geometry(gpu->draws()[1].call.geometry);
		const std::span<const uint8_t> bytes = gpu->bufferContents(geometry->vertices);
		ASSERT_EQ(bytes.size(), 2 * sizeof(BatchInstance));
		BatchInstance first{};
		std::memcpy(&first, bytes.data(), sizeof(first));
		EXPECT_EQ(first.corners[0].x, 20.0F);
	}

	TEST_F(MaskFlushTest, InstancedAndBakedDrawsCarryTheMaskInForce) {
		InstancedMeshHandle mesh = Primitives::uploadInstancedMesh(square(1.0F, 1.0F), 4);
		ASSERT_TRUE(mesh.isValid());
		const InstanceData instance({0.0F, 0.0F}, 0.0F, 1.0F, Foundation::Vec4(1.0F));
		Primitives::setMask(write(2));
		Primitives::drawInstanced(mesh, &instance, 1, {});
		ASSERT_EQ(gpu->draws().size(), 1U);
		EXPECT_EQ(gpu->draws()[0].call.mask, write(2));

		Primitives::setMask(test(2, MaskCompare::NotEqual));
		Primitives::beginBakedPass({});
		EXPECT_EQ(gpu->foreignMask(), test(2, MaskCompare::NotEqual));
		Primitives::setMask({}); // refused inside the pass
		EXPECT_EQ(Primitives::getMask(), test(2, MaskCompare::NotEqual));
		Primitives::endBakedPass();
		Primitives::setMask({});
		EXPECT_EQ(Primitives::getMask(), Mask{});
	}

	TEST_F(MaskFlushTest, ClearFlushesOnlyDrawsAddedUnderAMask) {
		rect(0.0F, 0.0F, 4.0F, 4.0F, Foundation::Color::red());
		Primitives::clearMask(3);
		EXPECT_TRUE(gpu->draws().empty());
		Primitives::setMask(write(1));
		rect(0.0F, 0.0F, 4.0F, 4.0F, Foundation::Color::red());
		Primitives::clearMask(0);
		ASSERT_EQ(gpu->draws().size(), 2U);
		EXPECT_EQ(gpu->draws()[1].call.mask, write(1));
		EXPECT_EQ(gpu->maskClears(), (std::vector<uint8_t>{3, 0}));
	}

	TEST_F(MaskFlushTest, ATargetWithNoMaskRefusesOne) {
		RenderTarget plain = RenderTarget::create(8, 8);
		ASSERT_TRUE(plain.isValid());
		plain.begin();
		Primitives::setMask(write(1));
		EXPECT_EQ(Primitives::getMask(), Mask{});
		Primitives::clearMask(1);
		EXPECT_TRUE(gpu->maskClears().empty());
		plain.end();

		gpu->setWindowMask(false);
		Primitives::setMask(write(1));
		EXPECT_EQ(Primitives::getMask(), Mask{});
	}

	TEST_F(MaskFlushTest, ATargetBeginsWithNoMaskAndEndsWithTheOneItWasBegunIn) {
		RenderTarget masked = RenderTarget::create(8, 8, TextureFormat::Rgba8, RenderTarget::Attachments::ColourAndMask);
		ASSERT_TRUE(masked.isValid());
		Primitives::setMask(test(1));
		rect(0.0F, 0.0F, 4.0F, 4.0F, Foundation::Color::red());
		masked.begin();
		EXPECT_EQ(Primitives::getMask(), Mask{});
		ASSERT_EQ(gpu->draws().size(), 1U); // the window's masked rect, before the target was bound
		EXPECT_EQ(gpu->draws()[0].call.mask, test(1));

		Primitives::setMask(write(5)); // the target has a mask of its own
		EXPECT_EQ(Primitives::getMask(), write(5));
		rect(0.0F, 0.0F, 4.0F, 4.0F, Foundation::Color::red());
		masked.end();
		ASSERT_EQ(gpu->draws().size(), 2U); // flushed into the target, under its mask
		EXPECT_EQ(gpu->draws()[1].call.mask, write(5));
		EXPECT_EQ(Primitives::getMask(), test(1));
		Primitives::setMask({});
	}

	TEST_F(MaskFlushTest, BeginFrameTurnsAMaskLeftSetOff) {
		Primitives::setMask(write(1));
		Primitives::beginFrame();
		EXPECT_EQ(Primitives::getMask(), Mask{});
	}

	TEST_F(MaskFlushTest, AClipNeitherOpensNorClosesAMask) {
		Primitives::setMask(write(1));
		Primitives::pushClip({0.0F, 0.0F, 8.0F, 8.0F});
		EXPECT_EQ(Primitives::getMask(), write(1));
		Primitives::popClip();
		EXPECT_EQ(Primitives::getMask(), write(1));
		Primitives::setMask({});
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
			std::unique_ptr<backend::gl::GLBackend> gl = backend::gl::GLBackend::create();
			if (gl == nullptr) {
				glfwDestroyWindow(window);
				window = nullptr;
				glfwTerminate();
				GTEST_SKIP() << "GLEW initialization failed";
			}
			Primitives::init(std::move(gl), base_shader::source());
			Primitives::setCoordinateSystem(nullptr);
			Primitives::setViewport(kSize, kSize);
			target = RenderTarget::create(kSize, kSize, TextureFormat::Rgba8, RenderTarget::Attachments::ColourAndMask);
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

		// One frame into the target: colour cleared to opaque blue, the mask to 0, then `draw`.
		template <typename Draw>
		void render(Draw draw) {
			target.begin();
			Primitives::setViewport(kSize, kSize);
			Primitives::clear(Foundation::Color(0.0F, 0.0F, 1.0F, 1.0F));
			Primitives::clearMask(0);
			Primitives::beginFrame();
			draw();
			Primitives::endFrame();
			target.end();
			pixels.assign(static_cast<size_t>(kSize) * kSize * 4, 0);
			target.readPixels(pixels);
		}

		// The pixel at (x, y), y down; readPixels rows run bottom up.
		std::array<int, 4> pixel(int x, int y) const {
			const size_t i = ((static_cast<size_t>(kSize - 1 - y) * kSize) + static_cast<size_t>(x)) * 4;
			return {pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]};
		}

		static void expectNear(const std::array<int, 4>& actual, const std::array<int, 4>& expected, int tolerance = 1) {
			for (size_t c = 0; c < 4; ++c) {
				EXPECT_LE(std::abs(actual[c] - expected[c]), tolerance)
					<< "channel " << c << ": " << actual[0] << "," << actual[1] << "," << actual[2] << "," << actual[3];
			}
		}

		// The whole target in `color`, where the mask lets it land; past its edges, so the rect's
		// anti-aliased edge falls outside.
		static void fill(const Foundation::Color& color) { rect(-4.0F, -4.0F, kSize + 8.0F, kSize + 8.0F, color); }

		// Two triangles over [x0, x1) x [y0, y1), aliased, as a game draws a mask's shape.
		static void quad(float x0, float y0, float x1, float y1) {
			const std::array<Foundation::Vec2, 4>	 corners = {{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}};
			static constexpr std::array<uint16_t, 6> kIndices = {0, 1, 2, 0, 2, 3};
			Primitives::drawTriangles(
				{.vertices = corners.data(),
				 .indices = kIndices.data(),
				 .vertexCount = corners.size(),
				 .indexCount = kIndices.size(),
				 .color = Foundation::Color::white()}
			);
		}

		static constexpr std::array<int, 4> kBlue = {0, 0, 255, 255};
		static constexpr std::array<int, 4> kRed = {255, 0, 0, 255};
		static constexpr std::array<int, 4> kGreen = {0, 255, 0, 255};

		GLFWwindow*			 window = nullptr;
		RenderTarget		 target;
		std::vector<uint8_t> pixels;
	};

	TEST_F(MaskDrawTest, BatchedTrianglesWriteTheMaskAndNoColour) {
		render([] {
			Primitives::setMask(write(1));
			quad(0.0F, 0.0F, 16.0F, 32.0F);
			Primitives::setMask({});
		});
		expectNear(pixel(4, 4), kBlue);
		expectNear(pixel(28, 4), kBlue);

		render([] {
			Primitives::setMask(write(1));
			quad(0.0F, 0.0F, 16.0F, 32.0F);
			Primitives::setMask(test(1));
			fill(Foundation::Color::red());
			Primitives::setMask(test(0));
			fill(Foundation::Color::green());
			Primitives::setMask({});
		});
		expectNear(pixel(0, 0), kRed);
		expectNear(pixel(15, 31), kRed);
		expectNear(pixel(16, 0), kGreen);
		expectNear(pixel(31, 31), kGreen);
	}

	TEST_F(MaskDrawTest, AnInstancedMeshWritesTheMask) {
		InstancedMeshHandle mesh = Primitives::uploadInstancedMesh(square(32.0F, 8.0F), 4);
		ASSERT_TRUE(mesh.isValid());
		render([&] {
			// World equals screen here: the camera at the viewport's centre, one pixel a metre.
			const InstanceData instance({0.0F, 24.0F}, 0.0F, 1.0F, Foundation::Vec4(1.0F));
			Primitives::setMask(write(1));
			Primitives::drawInstanced(mesh, &instance, 1, {.position = {16.0F, 16.0F}});
			Primitives::setMask(test(1));
			fill(Foundation::Color::red());
			Primitives::setMask({});
		});
		expectNear(pixel(4, 23), kBlue);
		expectNear(pixel(4, 24), kRed);
		expectNear(pixel(28, 31), kRed);
	}

	TEST_F(MaskDrawTest, OverlappingWritesReplaceAndEachValueFillsItsOwnArea) {
		// The player-visibility cover: 1 is seen, 2 in sight, 0 never seen.
		render([] {
			Primitives::setMask(write(1));
			quad(0.0F, 0.0F, 20.0F, 20.0F);
			Primitives::setMask(write(2));
			quad(12.0F, 12.0F, 32.0F, 32.0F);
			Primitives::setMask(test(0));
			fill(Foundation::Color::red());
			Primitives::setMask(test(1));
			fill(Foundation::Color(0.0F, 0.0F, 0.0F, 0.5F));
			Primitives::setMask({});
		});
		expectNear(pixel(28, 2), kRed);			   // 0
		expectNear(pixel(2, 28), kRed);			   // 0
		expectNear(pixel(2, 2), {0, 0, 128, 255}); // 1: half black over blue
		expectNear(pixel(16, 16), kBlue);		   // 2, over 1
		expectNear(pixel(28, 28), kBlue);		   // 2
	}

	TEST_F(MaskDrawTest, OrderedComparesReadTheStoredMaskAgainstTheValue) {
		const auto areas = [] {
			Primitives::setMask(write(1));
			quad(0.0F, 0.0F, 16.0F, 32.0F);
			Primitives::setMask(write(2));
			quad(16.0F, 0.0F, 32.0F, 16.0F);
		};
		render([&] {
			areas();
			Primitives::setMask(test(2, MaskCompare::Less));
			fill(Foundation::Color::red());
			Primitives::setMask({});
		});
		expectNear(pixel(4, 4), kRed);	 // 1 < 2
		expectNear(pixel(28, 28), kRed); // 0 < 2
		expectNear(pixel(28, 4), kBlue); // 2

		render([&] {
			areas();
			Primitives::setMask(test(0, MaskCompare::Greater));
			fill(Foundation::Color::red());
			Primitives::setMask({});
		});
		expectNear(pixel(4, 4), kRed);
		expectNear(pixel(28, 4), kRed);
		expectNear(pixel(28, 28), kBlue);
	}

	TEST_F(MaskDrawTest, NothingIsMaskedOnceItIsOffAndTheGlStateIsTheFixedState) {
		render([] {
			Primitives::setMask(write(1));
			quad(0.0F, 0.0F, 16.0F, 32.0F);
			Primitives::setMask(test(1));
			fill(Foundation::Color::red());
			Primitives::flush();
			// A game's own GL between the renderer's submissions finds no mask on.
			EXPECT_FALSE(glIsEnabled(GL_STENCIL_TEST));
			std::array<GLboolean, 4> colourWrites{};
			glGetBooleanv(GL_COLOR_WRITEMASK, colourWrites.data());
			EXPECT_EQ(colourWrites, (std::array<GLboolean, 4>{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE}));
			GLint stencilWrites = 0;
			glGetIntegerv(GL_STENCIL_WRITEMASK, &stencilWrites);
			EXPECT_EQ(stencilWrites & 0xFF, 0xFF);
			// One that leaves a stencil test on, and says so, doesn't mask the renderer's draws.
			glEnable(GL_STENCIL_TEST);
			glStencilFunc(GL_NEVER, 0, 0xFF);
			Primitives::invalidateState();
			Primitives::setMask({});
			rect(24.0F, 0.0F, 8.0F, 8.0F, Foundation::Color::green());
		});
		expectNear(pixel(4, 4), kRed);
		expectNear(pixel(20, 20), kBlue);
		expectNear(pixel(28, 4), kGreen);
		EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
	}

	TEST_F(MaskDrawTest, AClipCutsWhatAMaskWriteWritesAndWhatATestDraws) {
		render([] {
			Primitives::setMask(write(1));
			Primitives::pushClip({0.0F, 0.0F, 8.0F, 32.0F});
			quad(0.0F, 0.0F, 32.0F, 32.0F); // writes only inside the clip
			Primitives::popClip();
			Primitives::setMask(test(1));
			Primitives::pushClip({0.0F, 0.0F, 32.0F, 16.0F});
			fill(Foundation::Color::red());
			Primitives::popClip();
			Primitives::setMask({});
		});
		expectNear(pixel(4, 4), kRed);
		expectNear(pixel(4, 24), kBlue); // in the mask, outside the test's clip
		expectNear(pixel(12, 4), kBlue); // outside the write's clip
	}

	TEST_F(MaskDrawTest, ATargetBegunInsideAMaskDrawsUnmaskedAndTheMaskHoldsAfter) {
		RenderTarget inner = RenderTarget::create(kSize, kSize, TextureFormat::Rgba8, RenderTarget::Attachments::ColourAndMask);
		ASSERT_TRUE(inner.isValid());
		std::vector<uint8_t> innerPixels(static_cast<size_t>(kSize) * kSize * 4, 0);
		render([&] {
			Primitives::setMask(write(1));
			quad(0.0F, 0.0F, 16.0F, 32.0F);
			Primitives::setMask(test(1));
			inner.begin();
			Primitives::clear(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F));
			fill(Foundation::Color::green());
			Primitives::flush(); // a target's end() flushes nothing of its own
			inner.end();
			fill(Foundation::Color::red());
			Primitives::setMask({});
		});
		inner.readPixels(innerPixels);
		EXPECT_EQ(innerPixels[(31 * kSize + 28) * 4 + 1], 255); // green everywhere in the inner target
		expectNear(pixel(4, 4), kRed);
		expectNear(pixel(28, 4), kBlue);
	}

} // namespace Renderer
