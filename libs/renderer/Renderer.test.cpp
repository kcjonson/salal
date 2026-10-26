#include "Renderer.h"

#include <gtest/gtest.h>

TEST(RendererPlaceholder, SitsAtItsLayer) {
	EXPECT_EQ(renderer::layerDepth(), 1);
}
