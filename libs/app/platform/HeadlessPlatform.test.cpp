#include "platform/HeadlessPlatform.h"
#include "tests/PlatformLog.h"

#include <backend/NullBackend.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace app {

	namespace {

		using foundation::GamepadAxis;
		using foundation::GamepadButton;
		using foundation::InputEvent;
		using testing::PlatformLog;

		DeviceInfo pad(std::uint8_t index, std::string name, std::string guid) {
			return {.id = padId(index), .name = std::move(name), .modelGuid = std::move(guid)};
		}

		GamepadSample holding(GamepadButton button) {
			GamepadSample sample;
			sample.buttons[static_cast<std::size_t>(button)] = true;
			return sample;
		}

	} // namespace

	TEST(HeadlessPlatformTest, StartsFocusedAtItsSizeOnANullBackend) {
		HeadlessPlatform platform(640, 480);
		EXPECT_TRUE(platform.focused());
		EXPECT_FALSE(platform.closeRequested());
		EXPECT_EQ(platform.viewport().logicalWidth, 640);
		EXPECT_EQ(platform.viewport().framebufferHeight, 480);
		EXPECT_EQ(platform.viewport().pixelRatio(), 1.0F);
		EXPECT_EQ(platform.clock().now(), 0.0);
		const std::unique_ptr<Renderer::backend::Backend> backend = platform.createBackend();
		EXPECT_NE(dynamic_cast<Renderer::backend::NullBackend*>(backend.get()), nullptr);
	}

	TEST(HeadlessPlatformTest, WhatIsQueuedArrivesInTheNextPollInOrder) {
		HeadlessPlatform platform(640, 480);
		platform.clock().advance(0.5);
		platform.push({.type = InputEvent::Type::KeyDown, .time = 9.0, .key = foundation::Key::A});
		platform.resize(320, 200);
		platform.clock().advance(0.25);
		platform.setFocused(false);

		EXPECT_EQ(platform.viewport().logicalWidth, 640);
		EXPECT_TRUE(platform.focused());

		PlatformLog log;
		platform.pollEvents(log);
		const std::vector<InputEvent> events = log.inputs();
		ASSERT_EQ(events.size(), 2U);
		EXPECT_EQ(events[0].type, InputEvent::Type::KeyDown);
		EXPECT_EQ(events[0].time, 0.5); // stamped when pushed, as a callback would be
		EXPECT_EQ(events[1].type, InputEvent::Type::FocusLost);
		EXPECT_EQ(events[1].time, 0.75);
		ASSERT_EQ(log.resizes.size(), 1U);
		EXPECT_EQ(log.resizes[0].logicalWidth, 320);
		EXPECT_EQ(platform.viewport().framebufferHeight, 200);
		EXPECT_FALSE(platform.focused());

		PlatformLog again;
		platform.pollEvents(again);
		EXPECT_TRUE(again.entries.empty());
		EXPECT_TRUE(again.resizes.empty());
	}

	TEST(HeadlessPlatformTest, FocusThatDoesNotChangeSendsNothing) {
		HeadlessPlatform platform(640, 480);
		platform.setFocused(true);
		platform.setFocused(false);
		platform.setFocused(false);
		platform.setFocused(true);
		platform.setFocused(true);
		PlatformLog log;
		platform.pollEvents(log);
		const std::vector<InputEvent> events = log.inputs();
		ASSERT_EQ(events.size(), 2U);
		EXPECT_EQ(events[0].type, InputEvent::Type::FocusLost);
		EXPECT_EQ(events[1].type, InputEvent::Type::FocusGained);
		EXPECT_TRUE(platform.focused());
	}

	// A pad's connection arrives in queue order; its state is sampled after the queue, as GLFW polls
	// pads after its callbacks, and diffed from rest, so what it holds as it arrives is reported.
	TEST(HeadlessPlatformTest, AGamepadConnectsInQueueOrderAndIsSampledAfterTheQueue) {
		HeadlessPlatform platform(640, 480);
		GamepadSample	 state = holding(GamepadButton::Start);
		state.axes[static_cast<std::size_t>(GamepadAxis::LeftTrigger)] = 0.5F;
		platform.setGamepad(3, state);
		platform.clock().advance(0.25);
		platform.connectGamepad(pad(3, "Pad Three", "03000000"));
		platform.clock().advance(0.25);
		platform.push({.type = InputEvent::Type::KeyDown, .key = foundation::Key::A});

		PlatformLog log;
		platform.pollEvents(log);
		const std::vector<std::string> expected{"connected 3 Pad Three 03000000", "type 0", "ButtonDown 3 Start", "Axis 3 LeftTrigger 0.5"};
		EXPECT_EQ(log.text(), expected);
		EXPECT_EQ(log.entries[0].time, 0.25);
		EXPECT_EQ(log.entries[2].time, 0.5);

		// Sampled again at each poll, reporting only what changed.
		PlatformLog quiet;
		platform.pollEvents(quiet);
		EXPECT_TRUE(quiet.entries.empty());

		state.buttons[static_cast<std::size_t>(GamepadButton::Start)] = false;
		platform.setGamepad(3, state);
		PlatformLog letGo;
		platform.pollEvents(letGo);
		EXPECT_EQ(letGo.text(), std::vector<std::string>{"ButtonUp 3 Start"});
	}

	TEST(HeadlessPlatformTest, AButtonHeldThroughAReconnectIsReportedAgain) {
		HeadlessPlatform platform(640, 480);
		platform.setGamepad(0, holding(GamepadButton::A));
		platform.connectGamepad(pad(0, "Pad", "0300"));
		PlatformLog first;
		platform.pollEvents(first);
		EXPECT_EQ(first.text(), (std::vector<std::string>{"connected 0 Pad 0300", "ButtonDown 0 A"}));

		platform.disconnectGamepad(0);
		PlatformLog gone;
		platform.pollEvents(gone);
		EXPECT_EQ(gone.text(), std::vector<std::string>{"disconnected 0"});

		// Not sampled while away.
		platform.setGamepad(0, holding(GamepadButton::B));
		PlatformLog away;
		platform.pollEvents(away);
		EXPECT_TRUE(away.entries.empty());

		platform.setGamepad(0, holding(GamepadButton::A));
		platform.connectGamepad(pad(0, "Pad", "0300"));
		PlatformLog back;
		platform.pollEvents(back);
		EXPECT_EQ(back.text(), (std::vector<std::string>{"connected 0 Pad 0300", "ButtonDown 0 A"}));
	}

	TEST(HeadlessPlatformTest, KeyLabelsAreTheOnesSet) {
		HeadlessPlatform platform(640, 480);
		EXPECT_EQ(platform.keyLabel(foundation::Key::Q), std::nullopt);
		platform.setKeyLabel(foundation::Key::Q, "a");
		EXPECT_EQ(platform.keyLabel(foundation::Key::Q), "a");
		EXPECT_EQ(platform.keyLabel(foundation::Key::W), std::nullopt);
	}

	TEST(HeadlessPlatformTest, TheClipboardIsAString) {
		HeadlessPlatform platform(640, 480);
		EXPECT_EQ(platform.clipboardText(), "");
		platform.setClipboardText("copied text");
		EXPECT_EQ(platform.clipboardText(), "copied text");
	}

	TEST(HeadlessPlatformTest, FullscreenResizesAtOnceWithItsEventInTheNextPollAndWindowedComesBack) {
		HeadlessPlatform platform(640, 480);
		platform.addMonitor(1920, 1080);
		platform.addMonitor(2560, 1440);
		EXPECT_EQ(platform.fullscreenMonitor(), std::nullopt);

		platform.setFullscreen(1);
		EXPECT_EQ(platform.fullscreenMonitor(), 1);
		EXPECT_EQ(platform.viewport().logicalWidth, 2560);
		EXPECT_EQ(platform.viewport().framebufferHeight, 1440);
		PlatformLog log;
		platform.pollEvents(log);
		ASSERT_EQ(log.resizes.size(), 1U);
		EXPECT_EQ(log.resizes[0].logicalWidth, 2560);

		// A windowed size set while fullscreen is where it returns to.
		platform.setWindowSize(800, 600);
		EXPECT_EQ(platform.viewport().logicalWidth, 2560);
		platform.setFullscreen(1); // already there: nothing
		platform.setFullscreen(std::nullopt);
		EXPECT_EQ(platform.fullscreenMonitor(), std::nullopt);
		EXPECT_EQ(platform.viewport().logicalWidth, 800);
		EXPECT_EQ(platform.viewport().logicalHeight, 600);
		PlatformLog windowed;
		platform.pollEvents(windowed);
		ASSERT_EQ(windowed.resizes.size(), 1U);
		EXPECT_EQ(windowed.resizes[0].logicalHeight, 600);

		// A monitor that isn't there leaves it windowed, and so do sizes below 1.
		platform.setFullscreen(2);
		platform.setFullscreen(-1);
		platform.setWindowSize(0, 600);
		EXPECT_EQ(platform.fullscreenMonitor(), std::nullopt);
		EXPECT_EQ(platform.viewport().logicalWidth, 800);
		PlatformLog nothing;
		platform.pollEvents(nothing);
		EXPECT_TRUE(nothing.resizes.empty());
	}

	TEST(HeadlessPlatformTest, FullscreenMovesToAnotherMonitorAndAMissingOneChangesNothing) {
		HeadlessPlatform platform(640, 480);
		platform.addMonitor(1920, 1080);
		platform.addMonitor(2560, 1440);
		platform.setFullscreen(0);
		platform.setFullscreen(1);
		EXPECT_EQ(platform.fullscreenMonitor(), 1);
		EXPECT_EQ(platform.viewport().logicalWidth, 2560);
		PlatformLog log;
		platform.pollEvents(log);
		ASSERT_EQ(log.resizes.size(), 2U);
		EXPECT_EQ(log.resizes[0].logicalWidth, 1920);
		EXPECT_EQ(log.resizes[1].logicalWidth, 2560);

		platform.setFullscreen(7); // fullscreen stays where it is
		EXPECT_EQ(platform.fullscreenMonitor(), 1);
		EXPECT_EQ(platform.viewport().logicalWidth, 2560);
		PlatformLog nothing;
		platform.pollEvents(nothing);
		EXPECT_TRUE(nothing.resizes.empty());

		platform.setFullscreen(std::nullopt);
		EXPECT_EQ(platform.viewport().logicalWidth, 640); // the windowed size from before monitor 0
	}

	TEST(HeadlessPlatformTest, GoingFullscreenRemembersTheWindowedSizeAndVsyncIsKept) {
		HeadlessPlatform platform(640, 480);
		platform.addMonitor(1920, 1080);
		platform.setWindowSize(1024, 768);
		platform.setFullscreen(0);
		platform.setFullscreen(std::nullopt);
		EXPECT_EQ(platform.viewport().logicalWidth, 1024);
		EXPECT_EQ(platform.viewport().logicalHeight, 768);

		EXPECT_TRUE(platform.vsync());
		platform.setVsync(false);
		EXPECT_FALSE(platform.vsync());
	}

} // namespace app
