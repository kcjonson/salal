// The null backend and the slot table under every backend, headless.

#include "backend/NullBackend.h"
#include "backend/SlotTable.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace Renderer::backend {

	TEST(SlotTableTest, HandlesFindTheirValueUntilErased) {
		SlotTable<BufferHandle, std::string> table;
		const BufferHandle					 a = table.insert("a");
		const BufferHandle					 b = table.insert("b");
		ASSERT_TRUE(a.isValid());
		EXPECT_NE(a, b);
		EXPECT_EQ(*table.find(a), "a");
		EXPECT_EQ(*table.find(b), "b");
		EXPECT_EQ(table.size(), 2U);

		EXPECT_TRUE(table.erase(a));
		EXPECT_EQ(table.find(a), nullptr);
		EXPECT_FALSE(table.erase(a)); // twice is harmless
		EXPECT_EQ(table.size(), 1U);
	}

	TEST(SlotTableTest, AReusedSlotGetsANewGeneration) {
		SlotTable<BufferHandle, int> table;
		const BufferHandle			 stale = table.insert(1);
		table.erase(stale);
		const BufferHandle fresh = table.insert(2);
		EXPECT_EQ(fresh.index(), stale.index());
		EXPECT_NE(fresh.generation(), stale.generation());
		EXPECT_EQ(table.find(stale), nullptr);
		EXPECT_EQ(*table.find(fresh), 2);
	}

	TEST(SlotTableTest, InvalidAndOutOfRangeHandlesFindNothing) {
		SlotTable<BufferHandle, int> table;
		EXPECT_EQ(table.find(BufferHandle{}), nullptr);
		EXPECT_EQ(table.find(BufferHandle::make(999, 0)), nullptr);
		EXPECT_FALSE(table.erase(BufferHandle{}));
	}

	TEST(NullBackendTest, BuffersKeepWhatIsWritten) {
		NullBackend					 gpu;
		const std::array<uint8_t, 4> initial = {1, 2, 3, 4};
		const BufferHandle			 dynamic = gpu.createBuffer(BufferUsage::Dynamic, 4, initial.data());
		const std::array<uint8_t, 2> patch = {9, 9};
		gpu.updateBuffer(dynamic, 1, 2, patch.data());
		const std::span<const uint8_t> written = gpu.bufferContents(dynamic);
		EXPECT_EQ(std::vector<uint8_t>(written.begin(), written.end()), (std::vector<uint8_t>{1, 9, 9, 4}));
		gpu.updateBuffer(dynamic, 3, 2, patch.data()); // overruns: refused
		EXPECT_EQ(gpu.bufferContents(dynamic)[3], 4);

		// A stream write at 0 starts over at the size written.
		const BufferHandle			 stream = gpu.createBuffer(BufferUsage::Stream, 0, nullptr);
		const std::array<uint8_t, 6> batch = {5, 6, 7, 8, 9, 10};
		gpu.updateBuffer(stream, 0, batch.size(), batch.data());
		EXPECT_EQ(gpu.bufferContents(stream).size(), 6U);
		gpu.updateBuffer(stream, 0, 2, batch.data());
		EXPECT_EQ(gpu.bufferContents(stream).size(), 2U);

		gpu.destroyBuffer(stream);
		EXPECT_TRUE(gpu.bufferContents(stream).empty());
	}

	TEST(NullBackendTest, GeometryNeedsItsBuffers) {
		NullBackend		   gpu;
		const BufferHandle vertices = gpu.createBuffer(BufferUsage::Static, 16, nullptr);
		const BufferHandle indices = gpu.createBuffer(BufferUsage::Static, 6, nullptr);
		EXPECT_TRUE(gpu.createGeometry({.layout = VertexLayout::Batched, .vertices = vertices, .indices = indices}).isValid());
		EXPECT_FALSE(gpu.createGeometry({.layout = VertexLayout::InstancedMesh, .vertices = vertices, .indices = indices}).isValid());
		gpu.destroyBuffer(indices);
		EXPECT_FALSE(gpu.createGeometry({.layout = VertexLayout::Batched, .vertices = vertices, .indices = indices}).isValid());
	}

	TEST(NullBackendTest, DrawsRecordTheirCallAndTheUniformsInEffect) {
		NullBackend	   gpu;
		DrawUniforms   first;
		DrawUniforms   second;
		const DrawCall call{.indexOffset = 6, .indexCount = 12};
		first.instanced = 1;
		second.instanced = 2;
		gpu.setUniforms(first);
		gpu.draw({&call, 1});
		gpu.setUniforms(second);
		const std::array<DrawCall, 2> calls = {call, call};
		gpu.draw(calls);

		ASSERT_EQ(gpu.draws().size(), 3U);
		EXPECT_EQ(gpu.draws()[0].uniforms.instanced, 1);
		EXPECT_EQ(gpu.draws()[0].call.indexOffset, 6U);
		EXPECT_EQ(gpu.draws()[0].submission, 0);
		EXPECT_EQ(gpu.draws()[2].uniforms.instanced, 2);
		EXPECT_EQ(gpu.draws()[2].submission, 1);
		EXPECT_EQ(gpu.uniformWrites().size(), 2U);

		gpu.clearRecord();
		EXPECT_TRUE(gpu.draws().empty());
		EXPECT_TRUE(gpu.uniformWrites().empty());
	}

	TEST(NullBackendTest, TargetsNeedATexture) {
		NullBackend			gpu;
		const TextureHandle colour = gpu.createTexture({.width = 2, .height = 2});
		EXPECT_TRUE(gpu.createRenderTarget(colour).isValid());
		EXPECT_FALSE(gpu.createRenderTarget(TextureHandle{}).isValid());
		EXPECT_FALSE(gpu.createTexture({.width = 0, .height = 2}).isValid());
	}

	TEST(NullBackendTest, AnEarlierBackendsHandlesAreStaleInTheNext) {
		TextureHandle fromA;
		BufferHandle  bufferA;
		{
			NullBackend a;
			fromA = a.createTexture({.width = 2, .height = 2});
			bufferA = a.createBuffer(BufferUsage::Static, 4, nullptr);
		}
		NullBackend			b;
		const TextureHandle fromB = b.createTexture({.width = 4, .height = 4});
		const BufferHandle	bufferB = b.createBuffer(BufferUsage::Static, 8, nullptr);
		EXPECT_EQ(fromB.index(), fromA.index()); // the same slot, a new generation
		EXPECT_NE(fromB, fromA);
		EXPECT_NE(bufferB, bufferA);
		EXPECT_EQ(b.texture(fromA), nullptr);
		EXPECT_TRUE(b.bufferContents(bufferA).empty());

		// Destroying A's handles in B touches nothing of B's.
		b.destroyTexture(fromA);
		b.destroyBuffer(bufferA);
		ASSERT_NE(b.texture(fromB), nullptr);
		EXPECT_EQ(b.texture(fromB)->width, 4);
		EXPECT_EQ(b.bufferContents(bufferB).size(), 8U);
		EXPECT_EQ(b.live().textures, 1U);
		EXPECT_EQ(b.live().buffers, 1U);
	}

	TEST(NullBackendTest, ATimestampIsReadyOnceWritten) {
		NullBackend		  gpu;
		const QueryHandle query = gpu.createTimestampQuery();
		EXPECT_FALSE(gpu.timestampReady(query));
		gpu.writeTimestamp(query);
		EXPECT_TRUE(gpu.timestampReady(query));
		EXPECT_FALSE(gpu.timestampReady(QueryHandle{}));
	}

	TEST(NullBackendTest, ReadPixelsReadsZerosWhereGLWouldRead) {
		NullBackend gpu;
		gpu.setWindowSize(2, 1);
		std::vector<uint8_t> window(12, 7);
		gpu.readPixels({}, window); // the window's 2 x 1, the rest untouched
		EXPECT_EQ(window, (std::vector<uint8_t>{0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7}));

		const TextureHandle		 colour = gpu.createTexture({.width = 1, .height = 1});
		const RenderTargetHandle target = gpu.createRenderTarget(colour);
		std::vector<uint8_t>	 shortSpan(3, 7);
		gpu.readPixels(target, shortSpan);
		EXPECT_EQ(shortSpan, std::vector<uint8_t>(3, 7));

		gpu.destroyRenderTarget(target);
		std::vector<uint8_t> stale(4, 7);
		gpu.readPixels(target, stale);
		EXPECT_EQ(stale, std::vector<uint8_t>(4, 7));
	}

	TEST(NullBackendTest, HookUniformsRideWithTheDrawsIssuedAfterThem) {
		NullBackend gpu;
		const auto	bytes = [](uint8_t value) {
			 return std::vector<std::byte>(4, std::byte{value});
		};
		const std::vector<std::byte> a = bytes(1);
		const std::vector<std::byte> b = bytes(2);
		const std::vector<std::byte> c = bytes(3);
		const DrawCall				 call{.indexCount = 3};
		const auto					 asSpan = [](const std::vector<std::byte>& v) {
			  return std::span<const std::byte>(v);
		};
		const auto same = [](std::span<const std::byte> x, const std::vector<std::byte>& y) {
			return std::vector<std::byte>(x.begin(), x.end()) == y;
		};

		gpu.setHookUniforms(2, a);
		gpu.draw({&call, 1});
		gpu.setHookUniforms(1, b);
		gpu.draw({&call, 1});
		gpu.setHookUniforms(2, c);
		gpu.draw({&call, 1});

		ASSERT_EQ(gpu.draws().size(), 3U);
		EXPECT_TRUE(same(gpu.draws()[0].hookBlock(2), a));
		EXPECT_TRUE(gpu.draws()[0].hookBlock(1).empty());
		EXPECT_TRUE(same(gpu.draws()[1].hookBlock(1), b));
		EXPECT_TRUE(same(gpu.draws()[1].hookBlock(2), a)); // a draw reads the last write at each point
		EXPECT_TRUE(same(gpu.draws()[2].hookBlock(2), c));
		EXPECT_TRUE(same(gpu.draws()[2].hookBlock(1), b));
		ASSERT_EQ(gpu.draws()[2].hookUniforms.size(), 2U);
		EXPECT_EQ(gpu.draws()[2].hookUniforms[0].binding, 1U); // by binding point
		EXPECT_EQ(gpu.draws()[2].hookUniforms[1].binding, 2U);

		ASSERT_EQ(gpu.hookUniformWrites().size(), 3U);
		EXPECT_EQ(gpu.hookUniformWrites()[1].binding, 1U);
		EXPECT_TRUE(same(asSpan(gpu.hookUniformWrites()[2].bytes), c));
	}

	TEST(NullBackendTest, APipelineKeepsItsSourceAndHookBlocks) {
		NullBackend			   gpu;
		const HookUniformBlock blocks[] = {{.hook = "sway", .block = "SwayUniforms", .binding = 1, .size = 32}};
		const PipelineHandle   pipeline = gpu.createPipeline({.vertex = "vertex", .fragment = "fragment", .hookBlocks = blocks});
		const NullBackend::PipelineRecord* record = gpu.pipeline(pipeline);
		ASSERT_NE(record, nullptr);
		EXPECT_EQ(record->vertex, "vertex");
		EXPECT_EQ(record->fragment, "fragment");
		ASSERT_EQ(record->hookBlocks.size(), 1U);
		EXPECT_EQ(record->hookBlocks[0].binding, 1U);
		EXPECT_EQ(record->hookBlocks[0].size, 32U);
	}

} // namespace Renderer::backend
