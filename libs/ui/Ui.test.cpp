#include "Ui.h"

#include <gtest/gtest.h>

TEST(UiPlaceholder, SitsAtItsLayer) {
	EXPECT_EQ(ui::layerDepth(), 2);
}
