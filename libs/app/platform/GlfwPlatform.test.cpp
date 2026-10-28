// The GLFW platform needs a display and a GL 3.3 core context, so this skips where neither can be
// made (headless CI). It makes no GL call: createBackend isn't asked for.

#include "platform/Platform.h"

#include <gtest/gtest.h>

#include <memory>

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

} // namespace app
