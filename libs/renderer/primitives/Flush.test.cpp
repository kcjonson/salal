// What a flush hands the backend, end to end through Primitives on the null
// backend: the buffers it uploads, the draw calls it submits and the uniforms they
// read. Headless; no GL anywhere.

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "generated/OffsetHookShader.h"
#include "primitives/BatchRenderer.h"
#include "primitives/Primitives.h"
#include "vector/Types.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace Renderer {

	using Primitives::BlendMode;

	class FlushTest : public ::testing::Test {
	  protected:
		static constexpr int kWidth = 320;
		static constexpr int kHeight = 200;

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

		static void rect(float x, BlendMode blend = BlendMode::Alpha, int zIndex = 0) {
			Primitives::drawRect(
				{.bounds = {x, 0.0F, 10.0F, 10.0F}, .style = {.fill = Foundation::Color::red()}, .zIndex = zIndex, .blend = blend}
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

	TEST_F(FlushTest, TheZSortReachesTheIndexBuffer) {
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

		// z 0 in submission order, then z 1; the vertices stay as added.
		const std::vector<uint32_t> emitted = indices(gpu->draws()[0]);
		ASSERT_EQ(emitted.size(), 18U);
		EXPECT_EQ(emitted[0], 4U);
		EXPECT_EQ(emitted[6], 8U);
		EXPECT_EQ(emitted[12], 0U);
		const std::vector<BatchVertex> uploaded = vertices(gpu->draws()[0]);
		ASSERT_EQ(uploaded.size(), 12U);
		EXPECT_EQ(uploaded[4].position.x, 20.0F);
	}

	TEST_F(FlushTest, ACirclesBorderDrawsAtItsZStraightAfterItsFill) {
		Primitives::beginFrame();
		Primitives::drawCircle(
			{.center = {100.0F, 100.0F},
			 .radius = 20.0F,
			 .style =
				 {.fill = Foundation::Color::red(), .border = Foundation::BorderStyle{.color = Foundation::Color::white(), .width = 2.0F}},
			 .zIndex = 2}
		);
		rect(0.0F, BlendMode::Alpha, 1); // submitted after the circle, below its z
		Primitives::endFrame();

		// Vertices as added: the fill's fan 0-64, the border's 64 quads 65-320, the rect 321-324.
		constexpr uint32_t kFanEnd = 65;
		constexpr uint32_t kBorderEnd = kFanEnd + (64 * 4);
		ASSERT_EQ(gpu->draws().size(), 1U);
		ASSERT_EQ(vertices(gpu->draws()[0]).size(), kBorderEnd + 4);
		const std::vector<uint32_t> emitted = indices(gpu->draws()[0]);
		ASSERT_EQ(emitted.size(), 6U + (64U * 3U) + (64U * 6U));

		// Each run of the emit order, by the vertices it reaches.
		const auto allWithin = [&](size_t from, size_t count, uint32_t low, uint32_t high) {
			return std::ranges::all_of(std::span(emitted).subspan(from, count), [&](uint32_t index) {
				return index >= low && index < high;
			});
		};
		EXPECT_TRUE(allWithin(0, 6, kBorderEnd, kBorderEnd + 4)); // the rect, z 1
		EXPECT_TRUE(allWithin(6, 192, 0, kFanEnd));				  // the fill, z 2
		EXPECT_TRUE(allWithin(198, 384, kFanEnd, kBorderEnd));	  // the border, z 2, over the fill
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
		Primitives::pushClip({.shape = Foundation::ClipRect{.bounds = Foundation::Rect{10.0F, 20.0F, 100.0F, 50.0F}}});
		rect(0.0F);
		Primitives::pushClip({.shape = Foundation::ClipRect{.bounds = Foundation::Rect{50.0F, 0.0F, 100.0F, 100.0F}}});
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
