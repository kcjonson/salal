// The GLFW platform needs a display and a GL 3.3 core context, so this skips where neither can be
// made (headless CI). It makes no GL call: createBackend isn't asked for.

#include "platform/Platform.h"

#include <utils/Log.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>

namespace app {

	namespace {
#ifdef DEVELOPMENT_BUILD
		void countErrors(void* context, const foundation::LogRecord& record) {
			if (record.level == foundation::LogLevel::Error) {
				++*static_cast<int*>(context);
			}
		}
#endif
	} // namespace

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

	// The context is at least the version the config asks for; 4.1 is the most any desktop is sure to
	// give (macOS caps there).
	TEST(GlfwPlatformTest, TheContextIsTheVersionAskedFor) {
		std::unique_ptr<Platform> platform =
			createGlfwPlatform({.title = "salal gl version test", .sizePercent = 0.1F, .vsync = false, .glVersion = {4, 1}});
		if (platform == nullptr) {
			GTEST_SKIP() << "No window with a GL 4.1 core context here";
		}
		GLFWwindow* const context = glfwGetCurrentContext();
		ASSERT_NE(context, nullptr);
		const int major = glfwGetWindowAttrib(context, GLFW_CONTEXT_VERSION_MAJOR);
		const int minor = glfwGetWindowAttrib(context, GLFW_CONTEXT_VERSION_MINOR);
		EXPECT_TRUE(major > 4 || (major == 4 && minor >= 1)) << major << "." << minor;
		EXPECT_EQ(glfwGetWindowAttrib(context, GLFW_OPENGL_PROFILE), GLFW_OPENGL_CORE_PROFILE);
	}

	// The layout labels the keys that print, whatever the layout; Escape prints nothing, and Unknown
	// has no GLFW code to ask about. World 1 is missing from most keyboards, and asking about it raises
	// no GLFW error. The clipboard isn't tried here: it's the user's.
	TEST(GlfwPlatformTest, KeyLabelsAreForKeysThatPrint) {
		std::unique_ptr<Platform> platform = createGlfwPlatform({.title = "salal key label test", .sizePercent = 0.1F, .vsync = false});
		if (platform == nullptr) {
			GTEST_SKIP() << "No window with a GL 3.3 core context here";
		}
		EXPECT_TRUE(platform->keyLabel(foundation::Key::A).has_value());
		EXPECT_EQ(platform->keyLabel(foundation::Key::Escape), std::nullopt);
		EXPECT_EQ(platform->keyLabel(foundation::Key::Unknown), std::nullopt);

		[[maybe_unused]] int errors = 0;
#ifdef DEVELOPMENT_BUILD
		foundation::Logger::setSink(countErrors, &errors);
#endif
		platform->keyLabel(foundation::Key::World1);
		platform->keyLabel(foundation::Key::World2);
#ifdef DEVELOPMENT_BUILD
		foundation::Logger::setSink(nullptr, nullptr);
		EXPECT_EQ(errors, 0);
#endif
	}

	// The minimum is framebuffer pixels whatever the display's pixel ratio, and outranks sizePercent.
	TEST(GlfwPlatformTest, AWindowOpensAtItsMinimumSizeOrMoreAndASmallerSizeIsHeldToIt) {
		std::unique_ptr<Platform> platform = createGlfwPlatform(
			{.title = "salal minimum size test", .sizePercent = 0.05F, .vsync = false, .minWidth = 640, .minHeight = 360}
		);
		if (platform == nullptr) {
			GTEST_SKIP() << "No window with a GL 3.3 core context here";
		}
		EXPECT_GE(platform->viewport().framebufferWidth, 640);
		EXPECT_GE(platform->viewport().framebufferHeight, 360);
		platform->setWindowSize(100, 100);
		EXPECT_GE(platform->viewport().framebufferWidth, 640);
		EXPECT_GE(platform->viewport().framebufferHeight, 360);
	}

} // namespace app
