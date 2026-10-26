// The null backend and the slot table under every backend, headless.

#include "backend/NullBackend.h"
#include "backend/SlotTable.h"

#include <gtest/gtest.h>

#include <array>
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
		EXPECT_TRUE(gpu.createGeometry({.layout = VertexLayout::Uber, .vertices = vertices, .indices = indices}).isValid());
		EXPECT_FALSE(gpu.createGeometry({.layout = VertexLayout::InstancedMesh, .vertices = vertices, .indices = indices}).isValid());
		gpu.destroyBuffer(indices);
		EXPECT_FALSE(gpu.createGeometry({.layout = VertexLayout::Uber, .vertices = vertices, .indices = indices}).isValid());
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

} // namespace Renderer::backend
