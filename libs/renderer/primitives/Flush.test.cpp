// What a flush hands the backend, end to end through Primitives on the null
// backend: the buffers it uploads, the draw calls it submits and the uniforms they
// read. Headless; no GL anywhere.

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "generated/OffsetHookShader.h"
#include "primitives/BatchRenderer.h"
#include "primitives/Primitives.h"
#include "vector/Types.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <vector>

namespace Renderer {

	using Primitives::BlendMode;

	class FlushTest : public ::testing::Test {
	  protected:
		static constexpr int   kWidth = 320;
		static constexpr int   kHeight = 200;
		static constexpr float kTolerance = 1e-4F;

		void SetUp() override {
			auto owned = std::make_unique<backend::NullBackend>();
			gpu = owned.get();
			Primitives::init(std::move(owned), base_shader::source());
			Primitives::setCoordinateSystem(nullptr);
			Primitives::setViewport(kWidth, kHeight);
			gpu->clearRecord();
		}

		void TearDown() override { Primitives::shutdown(); }

		template <typename T>
		std::vector<T> contents(BufferHandle buffer) const {
			const std::span<const uint8_t> bytes = gpu->bufferContents(buffer);
			std::vector<T>				   out(bytes.size() / sizeof(T));
			std::memcpy(out.data(), bytes.data(), out.size() * sizeof(T));
			return out;
		}

		// What the draw's geometry holds now: the last flush's upload.
		std::vector<BatchVertex> vertices(const backend::NullBackend::RecordedDraw& draw) const {
			return contents<BatchVertex>(gpu->geometry(draw.call.geometry)->vertices);
		}
		std::vector<uint32_t> indices(const backend::NullBackend::RecordedDraw& draw) const {
			return contents<uint32_t>(gpu->geometry(draw.call.geometry)->indices);
		}

		static void rect(float x, BlendMode blend = BlendMode::Alpha, int32_t key = 0) {
			Primitives::drawRect(
				{.bounds = {x, 0.0F, 10.0F, 10.0F}, .style = {.fill = Foundation::Color::red()}, .key = key, .blend = blend}
			);
		}

		static void glyph(float x) {
			const FontRenderer::GlyphQuad quad{
				.position = {x, 0.0F},
				.size = {8.0F, 8.0F},
				.uvMin = {0.0F, 0.0F},
				.uvMax = {0.1F, 0.1F},
				.color = {1.0F, 1.0F, 1.0F, 1.0F},
				.runOrigin = {x, 8.0F}
			};
			Primitives::drawGlyphs(&quad, 1);
		}

		backend::NullBackend* gpu = nullptr;
	};

	TEST_F(FlushTest, InitMakesThePipelineAndTheStreamGeometry) {
		const backend::NullBackend::Counts live = gpu->live();
		EXPECT_EQ(live.pipelines, 1U);
		EXPECT_EQ(live.buffers, 2U);
		EXPECT_EQ(live.geometries, 1U);

		rect(0.0F);
		Primitives::flush();
		ASSERT_EQ(gpu->draws().size(), 1U);
		const PipelineHandle pipeline = gpu->draws()[0].call.pipeline;
		ASSERT_NE(gpu->pipeline(pipeline), nullptr);
		EXPECT_EQ(gpu->pipeline(pipeline)->vertex, base_shader::source().vertex);
		EXPECT_EQ(gpu->pipeline(pipeline)->fragment, base_shader::source().fragment);
		EXPECT_TRUE(gpu->pipeline(pipeline)->hookBlocks.empty());
		EXPECT_EQ(gpu->geometry(gpu->draws()[0].call.geometry)->layout, backend::VertexLayout::Batched);
	}

	TEST_F(FlushTest, AnEmptyFlushSubmitsNothing) {
		Primitives::beginFrame();
		Primitives::endFrame();
		EXPECT_TRUE(gpu->draws().empty());
		EXPECT_TRUE(gpu->uniformWrites().empty());
		EXPECT_EQ(gpu->frames(), 1);
	}

	TEST_F(FlushTest, TheKeySortReachesTheIndexBuffer) {
		Primitives::beginFrame();
		rect(0.0F, BlendMode::Alpha, 1);  // vertices 0-3
		rect(20.0F, BlendMode::Alpha, 0); // vertices 4-7
		rect(40.0F, BlendMode::Alpha, 0); // vertices 8-11
		Primitives::endFrame();

		ASSERT_EQ(gpu->draws().size(), 1U);
		const backend::DrawCall& call = gpu->draws()[0].call;
		EXPECT_EQ(call.indexOffset, 0U);
		EXPECT_EQ(call.indexCount, 18U);
		EXPECT_EQ(call.instanceCount, 0U);

		// Key 0 in submission order, then key 1; the vertices stay as added.
		const std::vector<uint32_t> emitted = indices(gpu->draws()[0]);
		ASSERT_EQ(emitted.size(), 18U);
		EXPECT_EQ(emitted[0], 4U);
		EXPECT_EQ(emitted[6], 8U);
		EXPECT_EQ(emitted[12], 0U);
		const std::vector<BatchVertex> uploaded = vertices(gpu->draws()[0]);
		ASSERT_EQ(uploaded.size(), 12U);
		EXPECT_EQ(uploaded[4].position.x, 20.0F);
	}

	TEST_F(FlushTest, ACirclesBorderDrawsAtItsKeyStraightAfterItsFill) {
		Primitives::beginFrame();
		Primitives::drawCircle(
			{.center = {100.0F, 100.0F},
			 .radius = 20.0F,
			 .style =
				 {.fill = Foundation::Color::red(), .border = Foundation::BorderStyle{.color = Foundation::Color::white(), .width = 2.0F}},
			 .key = 2}
		);
		rect(0.0F, BlendMode::Alpha, 1);  // submitted after the circle, below its key
		rect(20.0F, BlendMode::Alpha, 2); // submitted after the circle, at its key
		Primitives::endFrame();

		// Vertices as added: the fill's quad 0-3, the border's 4-7, the key 1 rect 8-11, the key 2
		// rect 12-15.
		ASSERT_EQ(gpu->draws().size(), 1U);
		ASSERT_EQ(vertices(gpu->draws()[0]).size(), 16U);
		const std::vector<uint32_t> emitted = indices(gpu->draws()[0]);
		ASSERT_EQ(emitted.size(), 24U);

		// The draw groups in emit order, each by the vertices its indices reach.
		const auto allWithin = [&](size_t from, uint32_t low) {
			return std::ranges::all_of(std::span(emitted).subspan(from, 6), [&](uint32_t index) {
				return index >= low && index < low + 4;
			});
		};
		EXPECT_TRUE(allWithin(0, 8));	// the key 1 rect
		EXPECT_TRUE(allWithin(6, 0));	// the fill, key 2
		EXPECT_TRUE(allWithin(12, 4));	// the border, key 2, over the fill
		EXPECT_TRUE(allWithin(18, 12)); // the key 2 rect, after the border at the same key
	}

	TEST_F(FlushTest, KeysSortAsSignedIntegersAndTiesKeepSubmissionOrder) {
		constexpr int32_t kLowest = std::numeric_limits<int32_t>::min();
		constexpr int32_t kHighest = std::numeric_limits<int32_t>::max();
		Primitives::beginFrame();
		rect(0.0F, BlendMode::Alpha, 0);		 // vertices 0-3
		rect(10.0F, BlendMode::Alpha, kHighest); // 4-7
		rect(20.0F, BlendMode::Alpha, -5);		 // 8-11
		glyph(30.0F);							 // 12-15, key 0 after the rect at 0
		rect(40.0F, BlendMode::Alpha, 3);		 // 16-19
		rect(50.0F, BlendMode::Alpha, -5);		 // 20-23
		rect(60.0F, BlendMode::Alpha, kLowest);	 // 24-27
		Primitives::endFrame();

		std::vector<uint32_t> emitted;
		for (const backend::NullBackend::RecordedDraw& draw : gpu->draws()) {
			const std::vector<uint32_t> all = indices(draw);
			emitted.insert(emitted.end(), all.begin() + draw.call.indexOffset, all.begin() + draw.call.indexOffset + draw.call.indexCount);
		}
		ASSERT_EQ(emitted.size(), 42U);
		std::vector<uint32_t> firstVertices;
		for (size_t quad = 0; quad < 7; ++quad) {
			firstVertices.push_back(emitted[quad * 6]);
		}
		// The lowest key, both -5s as submitted, the rect then the glyph at 0, 3, the highest.
		EXPECT_EQ(firstVertices, (std::vector<uint32_t>{24, 8, 20, 0, 12, 16, 4}));
	}

	TEST_F(FlushTest, AClipIsMappedThroughTheTransformItIsPushedUnder) {
		const auto pushedUnder = [&](const Foundation::Mat4& transform, const Foundation::Rect& clip) {
			gpu->clearRecord();
			Primitives::beginFrame();
			Primitives::PushTransform(transform);
			Primitives::pushClip(clip);
			rect(0.0F);
			Primitives::popClip();
			Primitives::PopTransform();
			Primitives::endFrame();
			EXPECT_EQ(gpu->draws().size(), 1U);
			return gpu->draws().empty() ? Foundation::Vec4(-1.0F) : vertices(gpu->draws()[0])[0].clipBounds;
		};
		const Foundation::Rect clip{10.0F, 20.0F, 100.0F, 50.0F};
		const Foundation::Mat4 identity(1.0F);
		EXPECT_EQ(pushedUnder(identity, clip), Foundation::Vec4(10.0F, 20.0F, 110.0F, 70.0F));
		EXPECT_EQ(
			pushedUnder(glm::translate(identity, Foundation::Vec3(30.0F, 40.0F, 0.0F)), clip),
			Foundation::Vec4(40.0F, 60.0F, 140.0F, 110.0F)
		);
		EXPECT_EQ(
			pushedUnder(glm::scale(identity, Foundation::Vec3(2.0F, 0.5F, 1.0F)), clip), Foundation::Vec4(20.0F, 10.0F, 220.0F, 35.0F)
		);
		// A mirror turns the rect round; its bounds are the same box.
		EXPECT_EQ(
			pushedUnder(glm::scale(identity, Foundation::Vec3(-1.0F, 1.0F, 1.0F)), clip), Foundation::Vec4(-110.0F, 20.0F, -10.0F, 70.0F)
		);
		// A quarter turn takes (x, y) to (-y, x): the box around the turned rect.
		const Foundation::Vec4 turned =
			pushedUnder(glm::rotate(identity, std::numbers::pi_v<float> * 0.5F, Foundation::Vec3(0.0F, 0.0F, 1.0F)), clip);
		EXPECT_NEAR(turned.x, -70.0F, kTolerance);
		EXPECT_NEAR(turned.y, 10.0F, kTolerance);
		EXPECT_NEAR(turned.z, -20.0F, kTolerance);
		EXPECT_NEAR(turned.w, 110.0F, kTolerance);
	}

	TEST_F(FlushTest, AClipStaysWhereItWasPushedWhateverIsPushedOverIt) {
		const Foundation::Mat4 offset = glm::translate(Foundation::Mat4(1.0F), Foundation::Vec3(30.0F, 0.0F, 0.0F));
		Primitives::beginFrame();
		Primitives::PushTransform(offset);
		Primitives::pushClip(Foundation::Rect{0.0F, 0.0F, 100.0F, 100.0F}); // 30 to 130 on screen
		Primitives::PushTransform(offset);									// a content offset inside the clip
		rect(0.0F);
		Primitives::pushClip(Foundation::Rect{50.0F, 0.0F, 100.0F, 50.0F}); // 110 to 210 under both offsets, cut at 130
		rect(60.0F);
		Primitives::popClip();
		Primitives::PopTransform();
		Primitives::popClip();
		Primitives::PopTransform();
		Primitives::endFrame();

		ASSERT_EQ(gpu->draws().size(), 1U);
		const std::vector<BatchVertex> uploaded = vertices(gpu->draws()[0]);
		ASSERT_EQ(uploaded.size(), 8U);
		EXPECT_EQ(uploaded[0].position.x, 60.0F);
		EXPECT_EQ(uploaded[0].clipBounds, Foundation::Vec4(30.0F, 0.0F, 130.0F, 100.0F));
		EXPECT_EQ(uploaded[4].clipBounds, Foundation::Vec4(110.0F, 0.0F, 130.0F, 50.0F));
	}

	TEST_F(FlushTest, ANegativeExtentIsEmptyUnderAMirrorToo) {
		Primitives::beginFrame();
		Primitives::PushTransform(glm::scale(Foundation::Mat4(1.0F), Foundation::Vec3(-1.0F, -1.0F, 1.0F)));
		Primitives::pushClip(Foundation::Rect{-10.0F, -10.0F, -50.0F, 50.0F});
		rect(-5.0F);
		Primitives::popClip();
		Primitives::pushClip(Foundation::Rect{-10.0F, -10.0F, 50.0F, -50.0F});
		rect(-5.0F);
		Primitives::popClip();
		Primitives::PopTransform();
		Primitives::endFrame();
		EXPECT_TRUE(gpu->draws().empty());
	}

	TEST_F(FlushTest, ClipsThatShareAnEdgeShareNoPixel) {
		// Half-open, a clip's right and bottom edges are outside it, so the next clip over
		// starts where it ends and the two meet in nothing.
		Primitives::beginFrame();
		Primitives::pushClip(Foundation::Rect{0.0F, 0.0F, 50.0F, 50.0F});
		Primitives::pushClip(Foundation::Rect{50.0F, 0.0F, 50.0F, 50.0F});
		rect(45.0F);
		Primitives::popClip();
		Primitives::pushClip(Foundation::Rect{0.0F, 50.0F, 50.0F, 50.0F});
		rect(45.0F);
		Primitives::popClip();
		Primitives::popClip();
		Primitives::endFrame();
		EXPECT_TRUE(gpu->draws().empty());
	}

	TEST_F(FlushTest, ACirclesFillAndBorderAreAnArcQuadEach) {
		const Foundation::Color fill(0.2F, 0.4F, 0.6F, 0.8F);
		const Foundation::Color edge(1.0F, 1.0F, 1.0F, 0.5F);
		Primitives::beginFrame();
		Primitives::pushClip(Foundation::Rect{10.0F, 20.0F, 200.0F, 100.0F});
		Primitives::drawCircle(
			{.center = {100.0F, 60.0F},
			 .radius = 20.0F,
			 .style = {.fill = fill, .border = Foundation::BorderStyle{.color = edge, .width = 4.0F}}}
		);
		Primitives::popClip();
		Primitives::endFrame();

		ASSERT_EQ(gpu->draws().size(), 1U);
		const std::vector<BatchVertex> uploaded = vertices(gpu->draws()[0]);
		ASSERT_EQ(uploaded.size(), 8U);
		EXPECT_EQ(indices(gpu->draws()[0]), (std::vector<uint32_t>{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7}));

		const float							  pi = std::numbers::pi_v<float>;
		const std::array<Foundation::Vec2, 4> corners = {{{-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F}}};
		// Each quad reaches a pixel past its outer radius: the fill's 20, the centred
		// border's 22. Its SDF coordinates are the corner's offset from the centre, turned.
		const auto expectArc = [&](size_t first, float innerRadius, float outerRadius, const Foundation::Color& color) {
			const float reach = outerRadius + 1.0F;
			for (size_t k = 0; k < 4; ++k) {
				const BatchVertex& vertex = uploaded[first + k];
				EXPECT_NEAR(vertex.position.x, 100.0F + (corners[k].x * reach), kTolerance) << first + k;
				EXPECT_NEAR(vertex.position.y, 60.0F + (corners[k].y * reach), kTolerance) << first + k;
				EXPECT_NEAR(glm::length(vertex.texCoord), reach * std::numbers::sqrt2_v<float>, kTolerance) << first + k;
				EXPECT_EQ(vertex.color, color.toVec4());
				EXPECT_EQ(vertex.data1, Foundation::Vec4(0.0F));
				EXPECT_EQ(vertex.data2, Foundation::Vec4(innerRadius, outerRadius, pi, kRenderModeArc));
				EXPECT_EQ(vertex.clipBounds, Foundation::Vec4(10.0F, 20.0F, 210.0F, 120.0F));
			}
		};
		expectArc(0, 0.0F, 20.0F, fill);
		expectArc(4, 18.0F, 22.0F, edge);
	}

	TEST_F(FlushTest, ACirclesBorderRadiiFollowItsPosition) {
		// The border alone (the fill is transparent, so it adds nothing), radius 20, width 4.
		const auto ring = [&](const Foundation::BorderStyle& border) {
			gpu->clearRecord();
			Primitives::beginFrame();
			Primitives::drawCircle(
				{.center = {50.0F, 50.0F}, .radius = 20.0F, .style = {.fill = {1.0F, 1.0F, 1.0F, 0.0F}, .border = border}}
			);
			Primitives::endFrame();
			EXPECT_EQ(gpu->draws().size(), 1U);
			const std::vector<BatchVertex> uploaded = gpu->draws().empty() ? std::vector<BatchVertex>{} : vertices(gpu->draws()[0]);
			EXPECT_EQ(uploaded.size(), 4U);
			return uploaded.empty() ? Foundation::Vec2(-1.0F) : Foundation::Vec2(uploaded[0].data2.x, uploaded[0].data2.y);
		};
		using Foundation::BorderPosition;
		EXPECT_EQ(ring({.width = 4.0F}), Foundation::Vec2(18.0F, 22.0F)); // centred by default, as the line segments were
		EXPECT_EQ(ring({.width = 4.0F, .position = BorderPosition::Center}), Foundation::Vec2(18.0F, 22.0F));
		EXPECT_EQ(ring({.width = 4.0F, .position = BorderPosition::Inside}), Foundation::Vec2(16.0F, 20.0F));
		EXPECT_EQ(ring({.width = 4.0F, .position = BorderPosition::Outside}), Foundation::Vec2(20.0F, 24.0F));
		// Wider than the radius: a disk, its outer edge still where the position puts it.
		EXPECT_EQ(ring({.width = 30.0F, .position = BorderPosition::Inside}), Foundation::Vec2(0.0F, 20.0F));
		EXPECT_EQ(ring({.width = 50.0F}), Foundation::Vec2(0.0F, 45.0F));
	}

	TEST_F(FlushTest, ACircleWithNoRadiusNoWidthOrNoFillAddsNothing) {
		const Foundation::BorderStyle border{.width = 2.0F};
		Primitives::beginFrame();
		Primitives::drawCircle({.center = {50.0F, 50.0F}, .radius = 0.0F, .style = {.border = border}});
		Primitives::drawCircle({.center = {50.0F, 50.0F}, .radius = -5.0F, .style = {.border = border}});
		Primitives::drawCircle(
			{.center = {50.0F, 50.0F},
			 .radius = 5.0F,
			 .style = {.fill = {1.0F, 1.0F, 1.0F, 0.0F}, .border = Foundation::BorderStyle{.width = 0.0F}}}
		);
		Primitives::endFrame();
		EXPECT_TRUE(gpu->draws().empty());
	}

	TEST_F(FlushTest, DrawCircleAndDrawArcAgreeOnADiskUnderEveryTransform) {
		// Bit for bit, both quads, and in the additive mode when asked: the same disk.
		const Foundation::Mat4 identity(1.0F);
		const Foundation::Mat4 turned =
			glm::scale(glm::rotate(glm::translate(identity, {100.3F, 50.7F, 0.0F}), 0.37F, {0.0F, 0.0F, 1.0F}), {1.7F, 1.7F, 1.0F});
		const Foundation::Mat4	mirrored = glm::scale(glm::translate(identity, {200.0F, 0.0F, 0.0F}), {-1.0F, 0.5F, 1.0F});
		const Foundation::Color color(0.9F, 0.8F, 0.1F, 0.6F);
		for (const Foundation::Mat4* transform : {&identity, &turned, &mirrored}) {
			for (const BlendMode blend : {BlendMode::Alpha, BlendMode::Additive}) {
				gpu->clearRecord();
				Primitives::beginFrame();
				Primitives::PushTransform(*transform);
				Primitives::drawCircle({.center = {30.0F, 40.0F}, .radius = 12.5F, .style = {.fill = color}, .blend = blend});
				Primitives::drawArc({.center = {30.0F, 40.0F}, .outerRadius = 12.5F, .color = color, .blend = blend});
				Primitives::PopTransform();
				Primitives::endFrame();

				ASSERT_EQ(gpu->draws().size(), 1U);
				const std::vector<BatchVertex> uploaded = vertices(gpu->draws()[0]);
				ASSERT_EQ(uploaded.size(), 8U);
				EXPECT_EQ(std::memcmp(&uploaded[0], &uploaded[4], 4 * sizeof(BatchVertex)), 0);
				EXPECT_EQ(uploaded[0].data2.w, blend == BlendMode::Additive ? kRenderModeArc + kAdditiveModeOffset : kRenderModeArc);
			}
		}
	}

	TEST_F(FlushTest, ACirclesQuadsCarryTheTransformAndItsSdfStaysLocal) {
		// The corners go through the transform; the SDF's coordinates and radii stay in the
		// circle's own units, so the disk becomes the ellipse the transform makes of it. The
		// quad's pixel of padding is a local pixel, 1 / sqrt(|det|): 2 here.
		const Foundation::Mat4 stretched = glm::scale(
			glm::rotate(glm::translate(Foundation::Mat4(1.0F), {80.0F, 60.0F, 0.0F}), 0.5F, {0.0F, 0.0F, 1.0F}), {2.0F, 0.125F, 1.0F}
		);
		Primitives::beginFrame();
		Primitives::PushTransform(stretched);
		Primitives::drawCircle(
			{.center = {10.0F, -5.0F},
			 .radius = 8.0F,
			 .style = {
				 .fill = Foundation::Color::red(),
				 .border = Foundation::BorderStyle{.width = 2.0F, .position = Foundation::BorderPosition::Outside}
			 }}
		);
		Primitives::PopTransform();
		Primitives::endFrame();

		ASSERT_EQ(gpu->draws().size(), 1U);
		const std::vector<BatchVertex> uploaded = vertices(gpu->draws()[0]);
		ASSERT_EQ(uploaded.size(), 8U);
		const std::array<Foundation::Vec2, 4> corners = {{{-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F}}};
		for (size_t k = 0; k < 8; ++k) {
			const float			   reach = (k < 4 ? 8.0F : 10.0F) + 2.0F;
			const Foundation::Vec4 local(10.0F + (corners[k % 4].x * reach), -5.0F + (corners[k % 4].y * reach), 0.0F, 1.0F);
			const Foundation::Vec4 expected = stretched * local;
			EXPECT_NEAR(uploaded[k].position.x, expected.x, 1e-3F) << k;
			EXPECT_NEAR(uploaded[k].position.y, expected.y, 1e-3F) << k;
			EXPECT_NEAR(glm::length(uploaded[k].texCoord), reach * std::numbers::sqrt2_v<float>, kTolerance) << k;
		}
		EXPECT_EQ(uploaded[0].data2.y, 8.0F);
		EXPECT_EQ(uploaded[4].data2.x, 8.0F);
		EXPECT_EQ(uploaded[4].data2.y, 10.0F);
	}

	TEST_F(FlushTest, AlphaAndAdditiveShareOneDrawCall) {
		Primitives::beginFrame();
		rect(0.0F, BlendMode::Alpha);
		rect(20.0F, BlendMode::Additive);
		Primitives::drawArc({.center = {50.0F, 50.0F}, .outerRadius = 8.0F, .blend = BlendMode::Additive});
		rect(40.0F, BlendMode::Alpha);
		Primitives::endFrame();

		ASSERT_EQ(gpu->draws().size(), 1U);
		EXPECT_EQ(gpu->draws()[0].call.blend, backend::BlendState::Alpha);
		EXPECT_EQ(gpu->draws()[0].call.indexCount, 24U);
		// The mode rides on the vertices instead.
		const std::vector<BatchVertex> uploaded = vertices(gpu->draws()[0]);
		ASSERT_EQ(uploaded.size(), 16U);
		EXPECT_EQ(uploaded[0].data2.w, 1.0F);
		EXPECT_EQ(uploaded[4].data2.w, 5.0F);
		EXPECT_EQ(uploaded[8].data2.w, kRenderModeArc + kAdditiveModeOffset);
	}

	TEST_F(FlushTest, AtlasChangesSplitIntoDrawCallsOverOneUpload) {
		const TextureHandle atlas = Primitives::createTexture({.width = 4, .height = 4, .format = TextureFormat::Rgb8});
		const TextureHandle image = Primitives::createTexture({.width = 4, .height = 4});
		Primitives::setFontAtlas(atlas);

		Primitives::beginFrame();
		glyph(0.0F);
		rect(10.0F); // a shape takes whatever is bound
		Primitives::drawImage({.texture = image, .bounds = {0.0F, 20.0F, 4.0F, 4.0F}});
		glyph(20.0F);
		Primitives::endFrame();

		const std::vector<backend::NullBackend::RecordedDraw>& draws = gpu->draws();
		ASSERT_EQ(draws.size(), 3U);
		EXPECT_EQ(draws[0].call.texture, atlas);
		EXPECT_EQ(draws[0].call.indexOffset, 0U);
		EXPECT_EQ(draws[0].call.indexCount, 12U);
		EXPECT_EQ(draws[1].call.texture, image);
		EXPECT_EQ(draws[1].call.indexOffset, 12U);
		EXPECT_EQ(draws[1].call.indexCount, 6U);
		EXPECT_EQ(draws[2].call.texture, atlas);
		EXPECT_EQ(draws[2].call.indexOffset, 18U);
		EXPECT_EQ(draws[2].call.indexCount, 6U);
		// One submission, one upload, one set of uniforms
		for (const backend::NullBackend::RecordedDraw& draw : draws) {
			EXPECT_EQ(draw.submission, 0);
			EXPECT_EQ(draw.call.pipeline, draws[0].call.pipeline);
			EXPECT_EQ(draw.call.geometry, draws[0].call.geometry);
		}
		EXPECT_EQ(gpu->uniformWrites().size(), 1U);
		EXPECT_EQ(Primitives::getStats().drawCalls, 3U);
	}

	TEST_F(FlushTest, ClipBoundsRideOnTheVerticesAndTheUniformsCarryTheFlip) {
		Primitives::beginFrame();
		Primitives::pushClip(Foundation::Rect{10.0F, 20.0F, 100.0F, 50.0F});
		rect(0.0F);
		Primitives::pushClip(Foundation::Rect{50.0F, 0.0F, 100.0F, 100.0F});
		rect(60.0F);
		Primitives::popClip();
		Primitives::popClip();
		rect(200.0F);
		Primitives::endFrame();

		ASSERT_EQ(gpu->draws().size(), 1U);
		const std::vector<BatchVertex> uploaded = vertices(gpu->draws()[0]);
		ASSERT_EQ(uploaded.size(), 12U);
		for (int i = 0; i < 4; ++i) {
			EXPECT_EQ(uploaded[i].clipBounds, Foundation::Vec4(10.0F, 20.0F, 110.0F, 70.0F));
			EXPECT_EQ(uploaded[4 + i].clipBounds, Foundation::Vec4(50.0F, 20.0F, 110.0F, 70.0F)); // nested: the intersection
			EXPECT_EQ(uploaded[8 + i].clipBounds, Foundation::Vec4(0.0F));
		}

		const backend::DrawUniforms& uniforms = gpu->draws()[0].uniforms;
		EXPECT_EQ(uniforms.instanced, 0);
		EXPECT_EQ(uniforms.viewportHeight, static_cast<float>(kHeight));
		EXPECT_EQ(uniforms.pixelRatio, 1.0F);
		EXPECT_EQ(uniforms.projection, glm::ortho(0.0F, static_cast<float>(kWidth), static_cast<float>(kHeight), 0.0F, -1.0F, 1.0F));
		EXPECT_EQ(uniforms.transform, Foundation::Mat4(1.0F));
	}

	TEST_F(FlushTest, ClipsThatMissEachOtherCullWhatIsDrawnUnderThemUntilPopped) {
		const auto clip = [](float x, float width) {
			Primitives::pushClip(Foundation::Rect{x, 0.0F, width, 50.0F});
		};
		Primitives::beginFrame();
		clip(0.0F, 50.0F);
		clip(100.0F, 50.0F); // misses the first: the empty clip
		rect(0.0F);
		clip(0.0F, 320.0F); // over the empty clip, still empty
		rect(110.0F);
		Primitives::popClip();
		rect(120.0F);
		Primitives::popClip();
		rect(10.0F); // in the first clip again
		Primitives::popClip();
		rect(200.0F);
		Primitives::endFrame();

		ASSERT_EQ(gpu->draws().size(), 1U);
		const std::vector<BatchVertex> uploaded = vertices(gpu->draws()[0]);
		ASSERT_EQ(uploaded.size(), 8U);
		EXPECT_EQ(uploaded[0].position.x, 10.0F);
		EXPECT_EQ(uploaded[0].clipBounds, Foundation::Vec4(0.0F, 0.0F, 50.0F, 50.0F));
		EXPECT_EQ(uploaded[4].position.x, 200.0F);
		EXPECT_EQ(uploaded[4].clipBounds, Foundation::Vec4(0.0F));
	}

	TEST_F(FlushTest, ANanClipIsEmptyInsideAClipAsAtTheTop) {
		const float nan = std::numeric_limits<float>::quiet_NaN();
		Primitives::beginFrame();
		Primitives::pushClip(Foundation::Rect{nan, 0.0F, 50.0F, 50.0F});
		rect(0.0F);
		Primitives::popClip();
		Primitives::pushClip(Foundation::Rect{0.0F, 0.0F, 100.0F, 100.0F});
		Primitives::pushClip(Foundation::Rect{0.0F, 0.0F, nan, 50.0F});
		rect(10.0F);
		Primitives::pushClip(Foundation::Rect{0.0F, 0.0F, 50.0F, 50.0F}); // over the empty clip, still empty
		rect(20.0F);
		Primitives::popClip();
		Primitives::popClip();
		Primitives::popClip();
		Primitives::endFrame();
		EXPECT_TRUE(gpu->draws().empty());
	}

	TEST_F(FlushTest, AClipOrTransformLeftPushedIsDroppedAtTheNextFrame) {
		// A frame whose render threw between its pushes and their pops.
		Primitives::beginFrame();
		Primitives::PushTransform(glm::translate(Foundation::Mat4(1.0F), Foundation::Vec3(30.0F, 0.0F, 0.0F)));
		Primitives::pushClip(Foundation::Rect{0.0F, 0.0F, 50.0F, 50.0F});
		Primitives::pushClip(Foundation::Rect{100.0F, 0.0F, 50.0F, 50.0F}); // the empty clip
		Primitives::endFrame();

		Primitives::beginFrame();
		rect(10.0F);
		Primitives::endFrame();
		ASSERT_EQ(gpu->draws().size(), 1U);
		const std::vector<BatchVertex> uploaded = vertices(gpu->draws()[0]);
		ASSERT_EQ(uploaded.size(), 4U);
		EXPECT_EQ(uploaded[0].position.x, 10.0F);
		EXPECT_EQ(uploaded[0].clipBounds, Foundation::Vec4(0.0F));
		EXPECT_EQ(Primitives::getCurrentTransform(), Foundation::Mat4(1.0F));
	}

	TEST_F(FlushTest, AClipWithNoAreaCullsEveryKindOfDraw) {
		const TextureHandle					  image = Primitives::createTexture({.width = 1, .height = 1});
		const std::array<Foundation::Vec2, 3> triangle = {{{0.0F, 0.0F}, {10.0F, 0.0F}, {0.0F, 10.0F}}};
		const std::array<uint16_t, 3>		  triangleIndices = {0, 1, 2};
		Primitives::beginFrame();
		Primitives::pushClip(Foundation::Rect{20.0F, 20.0F, 0.0F, 40.0F});
		rect(0.0F);
		Primitives::drawRect({.bounds = {0.0F, 0.0F, 10.0F, 10.0F}, .style = {.boxShadow = Foundation::BoxShadow{}}});
		glyph(0.0F);
		Primitives::drawLine({.start = {0.0F, 0.0F}, .end = {10.0F, 10.0F}});
		Primitives::drawTriangles(
			{.vertices = triangle.data(),
			 .indices = triangleIndices.data(),
			 .vertexCount = triangle.size(),
			 .indexCount = triangleIndices.size(),
			 .color = Foundation::Color::white(),
			 .antiAlias = true}
		);
		Primitives::drawCircle({.center = {10.0F, 10.0F}, .radius = 5.0F, .style = {.border = Foundation::BorderStyle{}}});
		Primitives::drawPolygon({.vertices = triangle, .style = {.border = Foundation::LineStyle{}}});
		Primitives::drawPolyline({.points = triangle, .width = 2.0F});
		Primitives::drawArc({.center = {10.0F, 10.0F}, .outerRadius = 5.0F});
		Primitives::drawImage({.texture = image, .bounds = {0.0F, 0.0F, 4.0F, 4.0F}});
		Primitives::popClip();
		Primitives::endFrame();

		EXPECT_TRUE(gpu->draws().empty());
		Primitives::destroyTexture(image);
	}

	TEST_F(FlushTest, EachFlushIsASubmissionThatStartsTheBuffersOver) {
		Primitives::beginFrame();
		rect(0.0F);
		rect(20.0F);
		Primitives::flush();
		rect(40.0F);
		Primitives::endFrame();

		ASSERT_EQ(gpu->draws().size(), 2U);
		EXPECT_EQ(gpu->draws()[0].submission, 0);
		EXPECT_EQ(gpu->draws()[1].submission, 1);
		EXPECT_EQ(gpu->draws()[1].call.indexCount, 6U);
		const std::vector<BatchVertex> uploaded = vertices(gpu->draws()[1]);
		ASSERT_EQ(uploaded.size(), 4U); // the stream buffer holds the second flush only
		EXPECT_EQ(uploaded[0].position.x, 40.0F);
		EXPECT_EQ(gpu->uniformWrites().size(), 2U);
	}

	TEST_F(FlushTest, InstancedDrawsCarryTheirCameraAndInstances) {
		renderer::TessellatedMesh triangle;
		triangle.vertices = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};
		triangle.indices = {0, 1, 2};
		const backend::NullBackend::Counts before = gpu->live();
		{
			InstancedMeshHandle mesh = Primitives::uploadInstancedMesh(triangle, 8);
			ASSERT_TRUE(mesh.isValid());
			EXPECT_EQ(gpu->live().buffers, before.buffers + 3);
			EXPECT_EQ(gpu->live().geometries, before.geometries + 1);

			const InstanceData instances[2] = {
				InstanceData({1.0F, 2.0F}, 0.0F, 1.0F, Foundation::Vec4(1.0F)),
				InstanceData({3.0F, 4.0F}, 0.5F, 2.0F, Foundation::Vec4(0.5F))
			};
			Primitives::drawInstanced(mesh, instances, 2, {.position = {5.0F, 6.0F}, .zoom = 2.0F, .pixelsPerMeter = 16.0F});

			ASSERT_EQ(gpu->draws().size(), 1U);
			const backend::NullBackend::RecordedDraw& draw = gpu->draws()[0];
			EXPECT_EQ(draw.call.instanceCount, 2U);
			EXPECT_EQ(draw.call.indexOffset, 0U);
			EXPECT_EQ(draw.call.indexCount, 3U);
			EXPECT_FALSE(draw.call.texture.isValid());
			const backend::GeometryDesc* geometry = gpu->geometry(draw.call.geometry);
			ASSERT_NE(geometry, nullptr);
			EXPECT_EQ(geometry->layout, backend::VertexLayout::InstancedMesh);
			const std::vector<InstanceData> uploaded = contents<InstanceData>(geometry->instances);
			ASSERT_EQ(uploaded.size(), 8U); // the instance buffer's whole capacity
			EXPECT_EQ(uploaded[1].worldPosition, Foundation::Vec2(3.0F, 4.0F));
			EXPECT_EQ(uploaded[1].scale, 2.0F);
			const std::vector<uint16_t> meshIndices = contents<uint16_t>(geometry->indices);
			EXPECT_EQ(meshIndices, (std::vector<uint16_t>{0, 1, 2}));

			EXPECT_EQ(draw.uniforms.instanced, 1);
			EXPECT_EQ(draw.uniforms.cameraPosition, Foundation::Vec2(5.0F, 6.0F));
			EXPECT_EQ(draw.uniforms.cameraZoom, 2.0F);
			EXPECT_EQ(draw.uniforms.pixelsPerMeter, 16.0F);
			EXPECT_EQ(draw.uniforms.viewportSize, Foundation::Vec2(static_cast<float>(kWidth), static_cast<float>(kHeight)));
			EXPECT_EQ(draw.uniforms.bakedAlpha, 1.0F);
		}
		EXPECT_EQ(gpu->live().buffers, before.buffers);
		EXPECT_EQ(gpu->live().geometries, before.geometries);
	}

	TEST_F(FlushTest, TheBakedPassBracketsTheCallersDrawsAndRestoresAlpha) {
		Primitives::setBakedAlpha(0.25F); // outside a pass: kept for the next draw, nothing submitted
		EXPECT_TRUE(gpu->uniformWrites().empty());

		Primitives::beginBakedPass({.position = {1.0F, 2.0F}, .zoom = 1.5F, .pixelsPerMeter = 8.0F});
		ASSERT_EQ(gpu->uniformWrites().size(), 1U);
		EXPECT_EQ(gpu->uniformWrites()[0].instanced, 2);
		EXPECT_EQ(gpu->uniformWrites()[0].bakedAlpha, 1.0F);
		EXPECT_EQ(gpu->uniformWrites()[0].cameraZoom, 1.5F);
		EXPECT_EQ(
			gpu->uniformWrites()[0].projection, glm::ortho(0.0F, static_cast<float>(kWidth), static_cast<float>(kHeight), 0.0F, -1.0F, 1.0F)
		);
		EXPECT_EQ(gpu->foreignDrawsBegun(), 1);
		EXPECT_TRUE(gpu->foreignPipeline().isValid());

		Primitives::setBakedAlpha(0.5F); // inside: at once, for the caller's next draw
		ASSERT_EQ(gpu->uniformWrites().size(), 2U);
		EXPECT_EQ(gpu->uniformWrites()[1].bakedAlpha, 0.5F);
		EXPECT_EQ(gpu->uniformWrites()[1].instanced, 2);

		Primitives::endBakedPass();
		EXPECT_EQ(gpu->foreignDrawsEnded(), 1);
		EXPECT_TRUE(gpu->draws().empty()); // the pass's draws are the caller's own

		// The next batch draw reads alpha 1 again.
		rect(0.0F);
		Primitives::flush();
		ASSERT_EQ(gpu->draws().size(), 1U);
		EXPECT_EQ(gpu->draws()[0].uniforms.bakedAlpha, 1.0F);
		EXPECT_EQ(gpu->draws()[0].uniforms.instanced, 0);
	}

	TEST_F(FlushTest, FrameAndStateCallsReachTheBackend) {
		Primitives::beginFrame();
		Primitives::invalidateState();
		Primitives::endFrame();
		EXPECT_EQ(gpu->frames(), 1);
		EXPECT_EQ(gpu->invalidations(), 1);

		int width = 0;
		int height = 0;
		Primitives::readBackbuffer({}, width, height);
		EXPECT_EQ(width, kWidth);
		EXPECT_EQ(height, kHeight);
		EXPECT_EQ(gpu->windowWidth(), kWidth);
		std::vector<uint8_t> pixels(static_cast<size_t>(width * height * 4), 9);
		Primitives::readBackbuffer(pixels, width, height);
		EXPECT_EQ(pixels[0], 0);
	}

	namespace {

		const std::array<Foundation::Vec2, 4> kSquare = {{{10.0F, 10.0F}, {30.0F, 10.0F}, {30.0F, 30.0F}, {10.0F, 30.0F}}};
		const std::array<uint16_t, 6>		  kSquareIndices = {0, 1, 2, 0, 2, 3};

		size_t countAlpha(const std::vector<BatchVertex>& vertices, float alpha) {
			return static_cast<size_t>(std::ranges::count_if(vertices, [alpha](const BatchVertex& v) { return v.color.a == alpha; }));
		}

	} // namespace

	TEST_F(FlushTest, APolygonsFillCarriesItsFeatherRingInItsGroup) {
		Primitives::beginFrame();
		Primitives::drawPolygon({.vertices = kSquare, .style = {.fill = {0.2F, 0.4F, 0.6F, 0.8F}}});
		Primitives::endFrame();

		ASSERT_EQ(gpu->draws().size(), 1U);
		const std::vector<BatchVertex> uploaded = vertices(gpu->draws()[0]);
		ASSERT_EQ(uploaded.size(), 8U);
		EXPECT_EQ(countAlpha(uploaded, 0.8F), 4U);
		EXPECT_EQ(countAlpha(uploaded, 0.0F), 4U);
		// Every outer vertex a pixel out from both edges at its corner.
		for (const BatchVertex& vertex : uploaded) {
			if (vertex.color.a == 0.0F) {
				const bool onX = std::abs(vertex.position.x - 9.0F) < 1e-5F || std::abs(vertex.position.x - 31.0F) < 1e-5F;
				const bool onY = std::abs(vertex.position.y - 9.0F) < 1e-5F || std::abs(vertex.position.y - 31.0F) < 1e-5F;
				EXPECT_TRUE(onX && onY) << vertex.position.x << ", " << vertex.position.y;
			}
		}
		EXPECT_EQ(gpu->draws()[0].call.indexCount, 6U + (4U * 6U));

		// Drawn again, from the cache: the same ring.
		gpu->clearRecord();
		Primitives::beginFrame();
		Primitives::drawPolygon({.vertices = kSquare, .style = {.fill = {0.2F, 0.4F, 0.6F, 0.8F}}});
		Primitives::drawPolygon({.vertices = kSquare, .style = {.fill = {0.2F, 0.4F, 0.6F, 0.8F}}});
		Primitives::endFrame();
		ASSERT_EQ(gpu->draws().size(), 1U);
		EXPECT_EQ(countAlpha(vertices(gpu->draws()[0]), 0.0F), 8U);
		EXPECT_EQ(gpu->draws()[0].call.indexCount, 2U * (6U + (4U * 6U)));
	}

	TEST_F(FlushTest, APolylineDropsTheSamePointsAtEveryZoomAndNoneFurtherOffThanAQuarterPixel) {
		// Drawn at 1x and at 2x, a closed three-point line and a line whose second point turns
		// its butt cap upload the same pieces; under scale(4, 0.25) a point 0.24 units off the
		// line, 0.96 pixels across the stretch, stays.
		const auto pieceVertices =
			[&](const std::vector<Foundation::Vec2>& points, float width, bool closed, const Foundation::Mat4& transform) {
				gpu->clearRecord();
				Primitives::beginFrame();
				Primitives::PushTransform(transform);
				Primitives::drawPolyline({.points = points, .width = width, .closed = closed});
				Primitives::PopTransform();
				Primitives::endFrame();
				return gpu->draws().empty() ? size_t{0} : vertices(gpu->draws()[0]).size();
			};
		const Foundation::Mat4				one(1.0F);
		const Foundation::Mat4				two = glm::scale(one, Foundation::Vec3(2.0F, 2.0F, 1.0F));
		const std::vector<Foundation::Vec2> flat = {{10.0F, 10.0F}, {20.0F, 10.2F}, {30.0F, 10.0F}};
		EXPECT_EQ(pieceVertices(flat, 10.0F, true, one), pieceVertices(flat, 10.0F, true, two));
		const std::vector<Foundation::Vec2> hooked = {{10.0F, 50.0F}, {11.0F, 50.2F}, {110.0F, 50.2F}};
		EXPECT_EQ(pieceVertices(hooked, 40.0F, false, one), pieceVertices(hooked, 40.0F, false, two));
		const std::vector<Foundation::Vec2> bent = {{10.0F, 10.0F}, {10.24F, 20.0F}, {10.0F, 30.0F}};
		const Foundation::Mat4				stretched = glm::scale(one, Foundation::Vec3(4.0F, 0.25F, 1.0F));
		const std::vector<Foundation::Vec2> straight = {{10.0F, 10.0F}, {10.0F, 30.0F}};
		EXPECT_GT(pieceVertices(bent, 2.0F, false, stretched), pieceVertices(straight, 2.0F, false, stretched));
	}

	TEST_F(FlushTest, AnOutlineThatRepeatsItsFirstPointStillFeathersInEitherWinding) {
		// Tessellated with the repeat, the fan held a zero-area triangle whose edge ran the
		// same way as its neighbour's in one winding, and the fill lost its ring.
		const std::vector<Foundation::Vec2> closed = {{10.0F, 10.0F}, {10.0F, 30.0F}, {30.0F, 30.0F}, {30.0F, 10.0F}, {10.0F, 10.0F}};
		const std::vector<Foundation::Vec2> reversed(closed.rbegin(), closed.rend());
		for (const std::vector<Foundation::Vec2>* outline : {&closed, &reversed}) {
			gpu->clearRecord();
			Primitives::beginFrame();
			Primitives::drawPolygon({.vertices = *outline, .style = {.fill = {0.2F, 0.4F, 0.6F, 0.8F}}});
			Primitives::endFrame();
			ASSERT_EQ(gpu->draws().size(), 1U);
			const std::vector<BatchVertex> uploaded = vertices(gpu->draws()[0]);
			EXPECT_EQ(countAlpha(uploaded, 0.0F), 4U);
			EXPECT_EQ(gpu->draws()[0].call.indexCount, 6U + (4U * 6U));
		}
	}

	TEST_F(FlushTest, DrawTrianglesIsAliasedUnlessItsAskedToAntiAlias) {
		Primitives::beginFrame();
		Primitives::drawTriangles(
			{.vertices = kSquare.data(),
			 .indices = kSquareIndices.data(),
			 .vertexCount = 4,
			 .indexCount = 6,
			 .color = Foundation::Color::white()}
		);
		Primitives::endFrame();
		ASSERT_EQ(gpu->draws().size(), 1U);
		// Exactly the mesh: its vertices, its indices, a flat zero-size box.
		const std::vector<BatchVertex> aliased = vertices(gpu->draws()[0]);
		ASSERT_EQ(aliased.size(), 4U);
		for (size_t i = 0; i < 4; ++i) {
			EXPECT_EQ(aliased[i].position, kSquare[i]);
			EXPECT_EQ(aliased[i].color, Foundation::Vec4(1.0F));
			EXPECT_EQ(aliased[i].data2, Foundation::Vec4(0.0F, 0.0F, 0.0F, 1.0F));
		}
		EXPECT_EQ(indices(gpu->draws()[0]), (std::vector<uint32_t>{0, 1, 2, 0, 2, 3}));

		gpu->clearRecord();
		Primitives::beginFrame();
		Primitives::drawTriangles(
			{.vertices = kSquare.data(),
			 .indices = kSquareIndices.data(),
			 .vertexCount = 4,
			 .indexCount = 6,
			 .color = Foundation::Color::white(),
			 .antiAlias = true}
		);
		Primitives::endFrame();
		ASSERT_EQ(gpu->draws().size(), 1U);
		const std::vector<BatchVertex> feathered = vertices(gpu->draws()[0]);
		ASSERT_EQ(feathered.size(), 8U);
		EXPECT_EQ(feathered[4].position, Foundation::Vec2(9.0F, 9.0F));
		EXPECT_EQ(feathered[4].color, Foundation::Vec4(1.0F, 1.0F, 1.0F, 0.0F));
		EXPECT_EQ(gpu->draws()[0].call.indexCount, 30U);
	}

	TEST_F(FlushTest, AMeshWhoseBoundaryPinchesStaysAliasedWhenAskedToAntiAlias) {
		const std::array<Foundation::Vec2, 5> bowtie = {{{0.0F, 0.0F}, {10.0F, 0.0F}, {5.0F, 5.0F}, {0.0F, 10.0F}, {10.0F, 10.0F}}};
		const std::array<uint16_t, 6>		  pinched = {0, 1, 2, 2, 4, 3};
		Primitives::beginFrame();
		Primitives::drawTriangles(
			{.vertices = bowtie.data(),
			 .indices = pinched.data(),
			 .vertexCount = 5,
			 .indexCount = 6,
			 .color = Foundation::Color::white(),
			 .antiAlias = true}
		);
		Primitives::endFrame();
		ASSERT_EQ(gpu->draws().size(), 1U);
		EXPECT_EQ(vertices(gpu->draws()[0]).size(), 5U);
		EXPECT_EQ(gpu->draws()[0].call.indexCount, 6U);
	}

	TEST_F(FlushTest, APolylinesPiecesShareTheirCutsBitForBitThroughTheTransform) {
		// Under a rotation and scale the cut's two ends, transformed once per piece, land
		// on the same floats in both pieces.
		const std::array<Foundation::Vec2, 3> elbow = {{{0.0F, 0.0F}, {40.0F, 0.0F}, {40.0F, 40.0F}}};
		Primitives::beginFrame();
		Primitives::PushTransform(
			glm::scale(
				glm::rotate(glm::translate(Foundation::Mat4(1.0F), {100.3F, 50.7F, 0.0F}), 0.37F, {0.0F, 0.0F, 1.0F}), {1.7F, 1.7F, 1.0F}
			)
		);
		Primitives::drawPolyline({.points = elbow, .width = 9.0F, .color = {1.0F, 1.0F, 1.0F, 0.5F}, .join = Primitives::LineJoin::Miter});
		Primitives::PopTransform();
		Primitives::endFrame();

		ASSERT_EQ(gpu->draws().size(), 1U);
		const std::vector<BatchVertex> uploaded = vertices(gpu->draws()[0]);
		ASSERT_EQ(uploaded.size(), 8U); // two pieces of four corners
		const auto shared = [&](size_t a, size_t b) {
			return std::memcmp(&uploaded[a].position, &uploaded[b].position, sizeof(Foundation::Vec2)) == 0;
		};
		// The first piece ends outer, inner; the second starts inner, outer.
		EXPECT_TRUE(shared(0, 7));
		EXPECT_TRUE(shared(1, 6));
	}

	// The same through the base shader with the test hook, tests/hooks/offset.vert.glsl.
	class FlushHookTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			auto owned = std::make_unique<backend::NullBackend>();
			gpu = owned.get();
			Primitives::init(std::move(owned), offset_hook_shader::source());
			Primitives::setCoordinateSystem(nullptr);
			Primitives::setViewport(32, 32);
			gpu->clearRecord();

			renderer::TessellatedMesh triangle;
			triangle.vertices = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};
			triangle.indices = {0, 1, 2};
			mesh = Primitives::uploadInstancedMesh(triangle, 1);
		}

		void TearDown() override {
			mesh = InstancedMeshHandle();
			Primitives::shutdown();
		}

		void drawTriangle() const {
			const InstanceData instance({1.0F, 2.0F}, 0.0F, 1.0F, Foundation::Vec4(1.0F));
			Primitives::drawInstanced(mesh, &instance, 1, {});
		}

		static std::vector<std::byte> bytes(const std::array<float, 4>& values) {
			const std::span<const std::byte> raw = std::as_bytes(std::span(values));
			return {raw.begin(), raw.end()};
		}

		static std::vector<std::byte> bytes(std::span<const std::byte> raw) { return {raw.begin(), raw.end()}; }

		backend::NullBackend* gpu = nullptr;
		InstancedMeshHandle	  mesh;
	};

	TEST_F(FlushHookTest, ThePipelineDeclaresTheHooksBlock) {
		Primitives::drawRect({.bounds = {0.0F, 0.0F, 4.0F, 4.0F}, .style = {.fill = Foundation::Color::red()}});
		Primitives::flush();
		ASSERT_EQ(gpu->draws().size(), 1U);
		const backend::NullBackend::PipelineRecord* pipeline = gpu->pipeline(gpu->draws()[0].call.pipeline);
		ASSERT_NE(pipeline, nullptr);
		EXPECT_EQ(pipeline->vertex, offset_hook_shader::source().vertex);
		ASSERT_EQ(pipeline->hookBlocks.size(), 1U);
		EXPECT_STREQ(pipeline->hookBlocks[0].hook, "offset");
		EXPECT_STREQ(pipeline->hookBlocks[0].block, "OffsetUniforms");
		EXPECT_EQ(pipeline->hookBlocks[0].binding, 1U);
		EXPECT_EQ(pipeline->hookBlocks[0].size, 16U);
	}

	TEST_F(FlushHookTest, HookUniformsReachTheBackendInOrderWithTheDraws) {
		const std::array<float, 4> first = {1.0F, 2.0F, 3.0F, 4.0F};
		const std::array<float, 4> second = {5.0F, 6.0F, 7.0F, 8.0F};

		drawTriangle(); // the block as it starts: nothing written
		Primitives::setHookUniforms(offset_hook_shader::kOffset, std::as_bytes(std::span(first)));
		drawTriangle();
		Primitives::setHookUniforms("offset", std::as_bytes(std::span(second)));
		drawTriangle();

		ASSERT_EQ(gpu->draws().size(), 3U);
		EXPECT_TRUE(gpu->draws()[0].hookBlock(1).empty());
		EXPECT_EQ(bytes(gpu->draws()[1].hookBlock(1)), bytes(first));
		EXPECT_EQ(bytes(gpu->draws()[2].hookBlock(1)), bytes(second));
		ASSERT_EQ(gpu->hookUniformWrites().size(), 2U);
		EXPECT_EQ(gpu->hookUniformWrites()[0].binding, 1U);
		EXPECT_EQ(gpu->hookUniformWrites()[1].bytes, bytes(second));
	}

	TEST_F(FlushHookTest, AnUnknownHookOrBlockOrTheWrongSizeWritesNothing) {
		const std::array<float, 4>		 values = {1.0F, 2.0F, 3.0F, 4.0F};
		const std::span<const std::byte> all = std::as_bytes(std::span(values));
		Primitives::setHookUniforms(offset_hook_shader::kOffset, all.first(8));
		Primitives::setHookUniforms("offsets", all);
		Primitives::setHookUniforms({.hook = "offset", .block = "OffsetUniforms", .binding = 2, .size = 16}, all);
		EXPECT_TRUE(gpu->hookUniformWrites().empty());

		Primitives::shutdown();
		Primitives::setHookUniforms(offset_hook_shader::kOffset, all); // after shutdown: nothing to write to
		auto				  owned = std::make_unique<backend::NullBackend>();
		backend::NullBackend* other = owned.get();
		Primitives::init(std::move(owned), base_shader::source());
		Primitives::setHookUniforms(offset_hook_shader::kOffset, all); // a base shader without the hook
		EXPECT_TRUE(other->hookUniformWrites().empty());
	}

	TEST(FlushNoBackendTest, InitWithoutABackendLeavesTheRendererOff) {
		Primitives::init(std::unique_ptr<backend::Backend>{}, base_shader::source());
		Primitives::beginFrame();
		Primitives::drawRect({.bounds = {0.0F, 0.0F, 1.0F, 1.0F}});
		Primitives::endFrame();
		EXPECT_FALSE(Primitives::createTexture({.width = 1, .height = 1}).isValid());
		EXPECT_EQ(backend::active(), nullptr);
	}

	TEST(BatchRendererLifetimeTest, ItsObjectsGoWithIt) {
		backend::NullBackend gpu;
		{
			BatchRenderer batch(gpu, base_shader::source());
			EXPECT_EQ(gpu.live().pipelines, 1U);
			EXPECT_EQ(gpu.live().buffers, 2U);
			EXPECT_EQ(gpu.live().geometries, 1U);
		}
		EXPECT_EQ(gpu.live().pipelines, 0U);
		EXPECT_EQ(gpu.live().buffers, 0U);
		EXPECT_EQ(gpu.live().geometries, 0U);
	}

} // namespace Renderer
