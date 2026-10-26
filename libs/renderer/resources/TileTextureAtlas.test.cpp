#include "resources/TileTextureAtlas.h"

#include "backend/NullBackend.h"
#include "backend/gl/GLBackend.h"
#include "primitives/Primitives.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

using namespace Renderer;

// ============================================================================
// AtlasRegion Tests (no GL context required)
// ============================================================================

TEST(AtlasRegionTest, DefaultConstructor) {
	AtlasRegion region;
	EXPECT_EQ(region.x, 0);
	EXPECT_EQ(region.y, 0);
	EXPECT_EQ(region.width, 0);
	EXPECT_EQ(region.height, 0);
	EXPECT_FALSE(region.valid);
}

// ============================================================================
// TileTextureAtlas Tests
// The atlas owns a GL texture, so these run against a hidden GL 3.3 core
// context and skip where none can be created (headless CI).
// ============================================================================

class TileTextureAtlasTest : public ::testing::Test {
  protected:
	void SetUp() override {
		if (glfwInit() != GLFW_TRUE) {
			GTEST_SKIP() << "GLFW initialization failed (no display available)";
		}
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
		window = glfwCreateWindow(32, 32, "tile-atlas-test", nullptr, nullptr);
		if (window == nullptr) {
			glfwTerminate();
			GTEST_SKIP() << "Could not create GLFW window (no OpenGL context available)";
		}
		glfwMakeContextCurrent(window);
		// The GL backend initialises GLEW on this context
		std::unique_ptr<backend::gl::GLBackend> gl = backend::gl::GLBackend::create();
		if (gl == nullptr) {
			glfwDestroyWindow(window);
			window = nullptr;
			glfwTerminate();
			GTEST_SKIP() << "GLEW initialization failed";
		}
		Primitives::init(std::move(gl));
	}

	void TearDown() override {
		if (window != nullptr) {
			Primitives::shutdown();
			glfwDestroyWindow(window);
			glfwTerminate();
		}
	}

	GLFWwindow* window = nullptr;
};

TEST(TileTextureAtlasNullTest, UploadsGoToTheBackendAndTheTextureGoesWithTheAtlas) {
	auto						owned = std::make_unique<backend::NullBackend>();
	const backend::NullBackend& gpu = *owned;
	Primitives::init(std::move(owned));
	{
		TileTextureAtlas atlas(64);
		ASSERT_TRUE(atlas.texture().isValid());
		ASSERT_NE(gpu.texture(atlas.texture()), nullptr);
		EXPECT_EQ(gpu.texture(atlas.texture())->width, 64);
		EXPECT_EQ(gpu.texture(atlas.texture())->format, TextureFormat::Rgba8);

		const AtlasRegion		   region = atlas.allocate(8, 4);
		const std::vector<uint8_t> pixels(8 * 4 * 4, 7);
		EXPECT_TRUE(atlas.upload(region, pixels.data()));
		ASSERT_EQ(gpu.textureUpdates().size(), 1U);
		EXPECT_EQ(gpu.textureUpdates()[0].texture, atlas.texture());
		EXPECT_EQ(gpu.textureUpdates()[0].width, 8);
		EXPECT_EQ(gpu.textureUpdates()[0].height, 4);
		EXPECT_EQ(gpu.live().textures, 1U);
	}
	EXPECT_EQ(gpu.live().textures, 0U);
	Primitives::shutdown();
}

TEST_F(TileTextureAtlasTest, ConstructorDefaultSize) {
	TileTextureAtlas atlas(1024);
	EXPECT_EQ(atlas.size(), 1024);
	EXPECT_TRUE(atlas.texture().isValid());
}

TEST_F(TileTextureAtlasTest, AllocationLogic) {
	// allocate() shelf-packs: left to right, then a new row
	TileTextureAtlas atlas(256);

	// First allocation should succeed (top-left corner)
	AtlasRegion r1 = atlas.allocate(64, 64);
	EXPECT_TRUE(r1.valid);
	EXPECT_EQ(r1.x, 0);
	EXPECT_EQ(r1.y, 0);
	EXPECT_EQ(r1.width, 64);
	EXPECT_EQ(r1.height, 64);

	// Second allocation should be to the right
	AtlasRegion r2 = atlas.allocate(64, 64);
	EXPECT_TRUE(r2.valid);
	EXPECT_EQ(r2.x, 64);
	EXPECT_EQ(r2.y, 0);

	// Third allocation fills first row
	AtlasRegion r3 = atlas.allocate(64, 64);
	EXPECT_TRUE(r3.valid);
	EXPECT_EQ(r3.x, 128);
	EXPECT_EQ(r3.y, 0);

	// Fourth allocation fills first row
	AtlasRegion r4 = atlas.allocate(64, 64);
	EXPECT_TRUE(r4.valid);
	EXPECT_EQ(r4.x, 192);
	EXPECT_EQ(r4.y, 0);

	// Fifth allocation should start new row
	AtlasRegion r5 = atlas.allocate(64, 64);
	EXPECT_TRUE(r5.valid);
	EXPECT_EQ(r5.x, 0);
	EXPECT_EQ(r5.y, 64);
}

TEST_F(TileTextureAtlasTest, AllocationOverflow) {
	TileTextureAtlas atlas(128);

	// Fill the atlas (128x128 = 4 tiles of 64x64)
	AtlasRegion r1 = atlas.allocate(64, 64);
	AtlasRegion r2 = atlas.allocate(64, 64);
	AtlasRegion r3 = atlas.allocate(64, 64);
	AtlasRegion r4 = atlas.allocate(64, 64);

	EXPECT_TRUE(r1.valid);
	EXPECT_TRUE(r2.valid);
	EXPECT_TRUE(r3.valid);
	EXPECT_TRUE(r4.valid);

	// Fifth allocation should fail (no space)
	AtlasRegion r5 = atlas.allocate(64, 64);
	EXPECT_FALSE(r5.valid);
}

TEST_F(TileTextureAtlasTest, AllocationTooLarge) {
	TileTextureAtlas atlas(128);

	// Allocation larger than atlas should fail
	AtlasRegion r = atlas.allocate(256, 256);
	EXPECT_FALSE(r.valid);
}

TEST_F(TileTextureAtlasTest, VariableSizeAllocation) {
	TileTextureAtlas atlas(256);

	// First row: 128-wide item
	AtlasRegion r1 = atlas.allocate(128, 64);
	EXPECT_TRUE(r1.valid);
	EXPECT_EQ(r1.x, 0);
	EXPECT_EQ(r1.y, 0);
	EXPECT_EQ(r1.height, 64);

	// Second item in first row
	AtlasRegion r2 = atlas.allocate(64, 32);
	EXPECT_TRUE(r2.valid);
	EXPECT_EQ(r2.x, 128);
	EXPECT_EQ(r2.y, 0);

	// Row height is determined by tallest item (64)
	// Third item should go to second row since it won't fit
	AtlasRegion r3 = atlas.allocate(128, 128);
	EXPECT_TRUE(r3.valid);
	EXPECT_EQ(r3.x, 0);
	EXPECT_EQ(r3.y, 64); // Below first row (height 64)
}

TEST_F(TileTextureAtlasTest, MoveConstructor) {
	TileTextureAtlas atlas1(512);
	atlas1.allocate(64, 64);

	const TextureHandle texture = atlas1.texture();
	TileTextureAtlas	atlas2(std::move(atlas1));
	EXPECT_EQ(atlas2.size(), 512);
	EXPECT_EQ(atlas2.texture(), texture);
	// Original should be in moved-from state
	EXPECT_EQ(atlas1.size(), 0);
	EXPECT_FALSE(atlas1.texture().isValid());
}

TEST_F(TileTextureAtlasTest, MoveAssignment) {
	TileTextureAtlas atlas1(512);
	atlas1.allocate(64, 64);

	TileTextureAtlas atlas2(256);
	atlas2 = std::move(atlas1);

	EXPECT_EQ(atlas2.size(), 512);
	EXPECT_EQ(atlas1.size(), 0);
}

TEST_F(TileTextureAtlasTest, UploadInvalidRegion) {
	TileTextureAtlas atlas(256);

	// Create invalid region
	AtlasRegion invalid;
	invalid.valid = false;

	// Upload should return false for invalid region
	std::vector<uint8_t> data(64 * 64 * 4, 255);
	EXPECT_FALSE(atlas.upload(invalid, data.data()));
}

TEST_F(TileTextureAtlasTest, UploadNullData) {
	TileTextureAtlas atlas(256);
	AtlasRegion		 region = atlas.allocate(64, 64);
	EXPECT_TRUE(region.valid);

	// Upload should return false for null data
	EXPECT_FALSE(atlas.upload(region, nullptr));
}
