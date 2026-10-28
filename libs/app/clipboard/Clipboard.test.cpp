// The App's clipboard is the platform's: on the headless one, a string both sides see.

#include "clipboard/Clipboard.h"
#include "tests/AppHarness.h"

#include <gtest/gtest.h>

namespace app::testing {

	using ClipboardTest = AppTest;

	TEST_F(ClipboardTest, TheAppsClipboardIsThePlatforms) {
		start();
		EXPECT_EQ(app->clipboard().getText(), "");
		app->clipboard().setText("copied from the game");
		EXPECT_EQ(platform->clipboardText(), "copied from the game");
		platform->setClipboardText("pasted from elsewhere");
		EXPECT_EQ(app->clipboard().getText(), "pasted from elsewhere");
	}

} // namespace app::testing
