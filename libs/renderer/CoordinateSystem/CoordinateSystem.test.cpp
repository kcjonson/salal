// CoordinateSystem is fed window sizes by the app, so these run with no window.

#include "CoordinateSystem/CoordinateSystem.h"
#include <gtest/gtest.h>

using namespace Renderer;

namespace {
	constexpr Viewport kWindow{800, 600, 800, 600};
	constexpr Viewport kRetinaWindow{800, 600, 1600, 1200};
} // namespace

// ----------------------------------------------------------------------------
// Viewport Tests
// ----------------------------------------------------------------------------

TEST(ViewportTest, PixelRatioIsFramebufferOverLogical) {
	EXPECT_FLOAT_EQ(kWindow.pixelRatio(), 1.0F);
	EXPECT_FLOAT_EQ(kRetinaWindow.pixelRatio(), 2.0F);
	EXPECT_FLOAT_EQ((Viewport{1000, 500, 1500, 750}).pixelRatio(), 1.5F);
}

TEST(ViewportTest, UnsizedViewportHasRatioOne) {
	EXPECT_FLOAT_EQ(Viewport{}.pixelRatio(), 1.0F);
}

// ----------------------------------------------------------------------------
// Viewport Round Trip
// ----------------------------------------------------------------------------

TEST(CoordinateSystemTest, SetViewportIsReadBack) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kRetinaWindow);

	const Viewport& v = coordSys.viewport();
	EXPECT_EQ(v.logicalWidth, 800);
	EXPECT_EQ(v.logicalHeight, 600);
	EXPECT_EQ(v.framebufferWidth, 1600);
	EXPECT_EQ(v.framebufferHeight, 1200);
}

TEST(CoordinateSystemTest, LastViewportWins) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kWindow);
	coordSys.setViewport({1024, 768, 1024, 768});

	glm::vec2 size = coordSys.getWindowSize();
	EXPECT_EQ(size.x, 1024.0F);
	EXPECT_EQ(size.y, 768.0F);
}

// ----------------------------------------------------------------------------
// Window Size Tests
// ----------------------------------------------------------------------------

TEST(CoordinateSystemTest, GetWindowSize) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kWindow);

	glm::vec2 size = coordSys.getWindowSize();
	EXPECT_EQ(size.x, 800.0F);
	EXPECT_EQ(size.y, 600.0F);
}

TEST(CoordinateSystemTest, GetWindowSizeIsLogicalNotFramebuffer) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kRetinaWindow);

	glm::vec2 size = coordSys.getWindowSize();
	EXPECT_EQ(size.x, 800.0F);
	EXPECT_EQ(size.y, 600.0F);
}

TEST(CoordinateSystemTest, GetWindowSizeWithoutViewport) {
	CoordinateSystem coordSys;
	// Should return default fallback values
	glm::vec2 size = coordSys.getWindowSize();
	EXPECT_EQ(size.x, 1920.0F); // Default fallback
	EXPECT_EQ(size.y, 1080.0F);
}

// ----------------------------------------------------------------------------
// Percentage Helper Tests
// ----------------------------------------------------------------------------

TEST(CoordinateSystemTest, PercentWidth) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kWindow); // 800x600 window

	EXPECT_FLOAT_EQ(coordSys.PercentWidth(50.0F), 400.0F);	// 50% of 800
	EXPECT_FLOAT_EQ(coordSys.PercentWidth(100.0F), 800.0F); // 100% of 800
	EXPECT_FLOAT_EQ(coordSys.PercentWidth(25.0F), 200.0F);	// 25% of 800
	EXPECT_FLOAT_EQ(coordSys.PercentWidth(0.0F), 0.0F);		// 0% of 800
}

TEST(CoordinateSystemTest, PercentHeight) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kWindow); // 800x600 window

	EXPECT_FLOAT_EQ(coordSys.PercentHeight(50.0F), 300.0F);	 // 50% of 600
	EXPECT_FLOAT_EQ(coordSys.PercentHeight(100.0F), 600.0F); // 100% of 600
	EXPECT_FLOAT_EQ(coordSys.PercentHeight(25.0F), 150.0F);	 // 25% of 600
	EXPECT_FLOAT_EQ(coordSys.PercentHeight(0.0F), 0.0F);	 // 0% of 600
}

TEST(CoordinateSystemTest, PercentSize) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kWindow); // 800x600 window

	glm::vec2 size = coordSys.PercentSize(50.0F, 75.0F);
	EXPECT_FLOAT_EQ(size.x, 400.0F); // 50% of 800
	EXPECT_FLOAT_EQ(size.y, 450.0F); // 75% of 600
}

TEST(CoordinateSystemTest, PercentPosition) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kWindow); // 800x600 window

	glm::vec2 pos = coordSys.PercentPosition(25.0F, 50.0F);
	EXPECT_FLOAT_EQ(pos.x, 200.0F); // 25% of 800
	EXPECT_FLOAT_EQ(pos.y, 300.0F); // 50% of 600
}

// ----------------------------------------------------------------------------
// Pixel Ratio Tests
// ----------------------------------------------------------------------------

TEST(CoordinateSystemTest, GetPixelRatio) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kWindow);
	EXPECT_FLOAT_EQ(coordSys.getPixelRatio(), 1.0F);

	coordSys.setViewport(kRetinaWindow);
	EXPECT_FLOAT_EQ(coordSys.getPixelRatio(), 2.0F);
}

TEST(CoordinateSystemTest, PixelRatioWithoutViewport) {
	CoordinateSystem coordSys;
	EXPECT_FLOAT_EQ(coordSys.getPixelRatio(), 1.0F);
}

TEST(CoordinateSystemTest, PixelRatioFollowsResize) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kWindow);
	EXPECT_FLOAT_EQ(coordSys.getPixelRatio(), 1.0F);

	// Dragged onto a 2x display: same logical size, doubled framebuffer
	coordSys.setViewport(kRetinaWindow);
	EXPECT_FLOAT_EQ(coordSys.getPixelRatio(), 2.0F);
}

// ----------------------------------------------------------------------------
// Coordinate Conversion Tests
// ----------------------------------------------------------------------------

TEST(CoordinateSystemTest, WindowToFramebufferConversion) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kRetinaWindow);

	glm::vec2 windowCoords(100.0F, 200.0F);
	glm::vec2 framebufferCoords = coordSys.WindowToFramebuffer(windowCoords);

	EXPECT_FLOAT_EQ(framebufferCoords.x, 200.0F);
	EXPECT_FLOAT_EQ(framebufferCoords.y, 400.0F);
}

TEST(CoordinateSystemTest, FramebufferToWindowConversion) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kRetinaWindow);

	glm::vec2 framebufferCoords(200.0F, 400.0F);
	glm::vec2 windowCoords = coordSys.FramebufferToWindow(framebufferCoords);

	EXPECT_FLOAT_EQ(windowCoords.x, 100.0F);
	EXPECT_FLOAT_EQ(windowCoords.y, 200.0F);
}

TEST(CoordinateSystemTest, RoundTripConversion) {
	CoordinateSystem coordSys;
	coordSys.setViewport({1000, 500, 1500, 750});

	glm::vec2 original(123.0F, 456.0F);
	glm::vec2 framebuffer = coordSys.WindowToFramebuffer(original);
	glm::vec2 roundtrip = coordSys.FramebufferToWindow(framebuffer);

	EXPECT_NEAR(roundtrip.x, original.x, 0.001F);
	EXPECT_NEAR(roundtrip.y, original.y, 0.001F);
}

// ----------------------------------------------------------------------------
// Projection Matrix Tests
// ----------------------------------------------------------------------------

TEST(CoordinateSystemTest, CreateScreenSpaceProjection) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kWindow);

	glm::mat4 projection = coordSys.CreateScreenSpaceProjection();

	// Verify it's not identity matrix
	EXPECT_NE(projection, glm::mat4(1.0F));

	// Screen space should have Y increasing downward
	// Top-left corner (0,0) in screen space should map to (-1,1) in clip space
	glm::vec4 topLeft = projection * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F);
	EXPECT_NEAR(topLeft.x / topLeft.w, -1.0F, 0.01F);
	EXPECT_NEAR(topLeft.y / topLeft.w, 1.0F, 0.01F);

	// Bottom-right corner (800,600) should map to (1,-1) in clip space
	glm::vec4 bottomRight = projection * glm::vec4(800.0F, 600.0F, 0.0F, 1.0F);
	EXPECT_NEAR(bottomRight.x / bottomRight.w, 1.0F, 0.01F);
	EXPECT_NEAR(bottomRight.y / bottomRight.w, -1.0F, 0.01F);
}

TEST(CoordinateSystemTest, ScreenSpaceProjectionUsesLogicalPixels) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kRetinaWindow);

	// (800,600) logical is still the bottom-right corner on a 2x display
	glm::mat4 projection = coordSys.CreateScreenSpaceProjection();
	glm::vec4 bottomRight = projection * glm::vec4(800.0F, 600.0F, 0.0F, 1.0F);
	EXPECT_NEAR(bottomRight.x / bottomRight.w, 1.0F, 0.01F);
	EXPECT_NEAR(bottomRight.y / bottomRight.w, -1.0F, 0.01F);
}

TEST(CoordinateSystemTest, CreateWorldSpaceProjection) {
	CoordinateSystem coordSys;
	coordSys.setViewport(kWindow);

	glm::mat4 projection = coordSys.CreateWorldSpaceProjection();

	// Verify it's not identity matrix
	EXPECT_NE(projection, glm::mat4(1.0F));

	// World space should have (0,0) at center, Y increasing upward
	// Center (0,0) should map to (0,0) in clip space
	glm::vec4 center = projection * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F);
	EXPECT_NEAR(center.x / center.w, 0.0F, 0.01F);
	EXPECT_NEAR(center.y / center.w, 0.0F, 0.01F);

	// Right edge (400,0) should map to (1,0) in clip space (half of 800)
	glm::vec4 rightEdge = projection * glm::vec4(400.0F, 0.0F, 0.0F, 1.0F);
	EXPECT_NEAR(rightEdge.x / rightEdge.w, 1.0F, 0.01F);
	EXPECT_NEAR(rightEdge.y / rightEdge.w, 0.0F, 0.01F);
}

// ----------------------------------------------------------------------------
// Edge Cases
// ----------------------------------------------------------------------------

TEST(CoordinateSystemTest, PercentHelpersWithoutViewport) {
	CoordinateSystem coordSys;
	// Not sized - should use fallback values

	// Should use default 1920x1080
	EXPECT_FLOAT_EQ(coordSys.PercentWidth(50.0F), 960.0F);	// 50% of 1920
	EXPECT_FLOAT_EQ(coordSys.PercentHeight(50.0F), 540.0F); // 50% of 1080
}
