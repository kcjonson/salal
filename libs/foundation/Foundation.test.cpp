#include "Foundation.h"

#include <gtest/gtest.h>

TEST(FoundationPlaceholder, SitsAtItsLayer) {
	EXPECT_EQ(foundation::layerDepth(), 0);
}
