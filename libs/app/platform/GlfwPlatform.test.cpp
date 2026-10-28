// The GLFW platform needs a display and a GL 3.3 core context, so this skips where neither can be
// made (headless CI). It makes no GL call: createBackend isn't asked for.

#include "platform/Platform.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>

namespace app {

	TEST(GlfwPlatformTest, ASecondPlatformIsRefusedAndTheFirstLivesOn) {
		std::unique_ptr<Platform> first = createGlfwPlatform({.title = "salal platform test", .sizePercent = 0.1F, .vsync = false});
		if (first == nullptr) {
			GTEST_SKIP() << "No window with a GL 3.3 core context here";
		}
		EXPECT_EQ(createGlfwPlatform({.title = "second", .sizePercent = 0.1F}), nullptr);

		// The refusal left GLFW and the first window alone.
		EXPECT_FALSE(first->closeRequested());
		EXPECT_GT(first->viewport().logicalWidth, 0);
		EXPECT_GT(first->clock().now(), 0.0);

		first.reset();
		EXPECT_NE(createGlfwPlatform({.title = "after", .sizePercent = 0.1F, .vsync = false}), nullptr);
	}

	// The layout labels the keys that print, whatever the layout; Escape prints nothing, and Unknown
	// has no GLFW code to ask about.
	TEST(GlfwPlatformTest, KeyLabelsAreForKeysThatPrint) {
		std::unique_ptr<Platform> platform = createGlfwPlatform({.title = "salal key label test", .sizePercent = 0.1F, .vsync = false});
		if (platform == nullptr) {
			GTEST_SKIP() << "No window with a GL 3.3 core context here";
		}
		EXPECT_TRUE(platform->keyLabel(foundation::Key::A).has_value());
		EXPECT_EQ(platform->keyLabel(foundation::Key::Escape), std::nullopt);
		EXPECT_EQ(platform->keyLabel(foundation::Key::Unknown), std::nullopt);
	}

} // namespace app
