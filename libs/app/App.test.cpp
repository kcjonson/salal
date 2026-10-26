#include "App.h"

#include <gtest/gtest.h>

TEST(AppPlaceholder, SitsAtItsLayer) {
	EXPECT_EQ(app::layerDepth(), 3);
}
