// The poll diff both platforms run: connections, the events a sample turns into, and the reset on
// disconnect that makes a button held through a reconnect report again.

#include "platform/Gamepads.h"
#include "tests/PlatformLog.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace app {

	namespace {

		using foundation::GamepadAxis;
		using foundation::GamepadButton;
		using testing::PlatformLog;

		DeviceInfo pad(std::uint8_t index) {
			return {.id = padId(index), .name = "Pad", .modelGuid = "0300"};
		}

		void set(GamepadSample& sample, GamepadButton button, bool down) {
			sample.buttons[static_cast<std::size_t>(button)] = down;
		}

		void set(GamepadSample& sample, GamepadAxis axis, float value) {
			sample.axes[static_cast<std::size_t>(axis)] = value;
		}

	} // namespace

	TEST(GamepadTrackerTest, ASampleReportsWhatChangedButtonsFirstInOrder) {
		GamepadTracker tracker;
		PlatformLog	   log;
		tracker.connect(pad(2), 1.0, log);
		EXPECT_TRUE(tracker.connected(2));

		GamepadSample state;
		set(state, GamepadAxis::RightX, -0.25F);
		set(state, GamepadButton::DpadLeft, true);
		set(state, GamepadButton::A, true);
		set(state, GamepadAxis::LeftY, 1.0F);
		tracker.sample(2, state, 2.0, log);

		const std::vector<std::string> expected{
			"connected 2 Pad 0300", "ButtonDown 2 A", "ButtonDown 2 DpadLeft", "Axis 2 LeftY 1", "Axis 2 RightX -0.25"
		};
		EXPECT_EQ(log.text(), expected);
		EXPECT_EQ(log.entries[0].time, 1.0);
		for (std::size_t i = 1; i < log.entries.size(); ++i) {
			EXPECT_EQ(log.entries[i].time, 2.0);
			EXPECT_EQ(log.entries[i].event.device, padId(2));
		}

		// Release and move: only those.
		set(state, GamepadButton::A, false);
		set(state, GamepadAxis::LeftY, 0.5F);
		PlatformLog second;
		tracker.sample(2, state, 3.0, second);
		EXPECT_EQ(second.text(), (std::vector<std::string>{"ButtonUp 2 A", "Axis 2 LeftY 0.5"}));
	}

	TEST(GamepadTrackerTest, ASampleWithNothingChangedReportsNothing) {
		GamepadTracker tracker;
		PlatformLog	   log;
		tracker.connect(pad(0), 0.0, log);
		tracker.sample(0, {}, 0.0, log); // a pad at rest is where the diff starts
		GamepadSample state;
		set(state, GamepadButton::X, true);
		set(state, GamepadAxis::RightTrigger, 0.75F);
		tracker.sample(0, state, 0.0, log);

		PlatformLog quiet;
		tracker.sample(0, state, 1.0, quiet);
		tracker.sample(0, state, 2.0, quiet);
		EXPECT_TRUE(quiet.entries.empty());
	}

	TEST(GamepadTrackerTest, DisconnectReportsAndResetsToRest) {
		GamepadTracker tracker;
		PlatformLog	   log;
		tracker.connect(pad(5), 0.0, log);
		GamepadSample held;
		set(held, GamepadButton::RightBumper, true);
		set(held, GamepadAxis::LeftTrigger, 1.0F);
		tracker.sample(5, held, 0.0, log);

		// The disconnection alone: releasing what the pad held is the accumulator's.
		PlatformLog gone;
		tracker.disconnect(5, 1.0, gone);
		EXPECT_EQ(gone.text(), std::vector<std::string>{"disconnected 5"});
		EXPECT_EQ(gone.entries[0].time, 1.0);
		EXPECT_FALSE(tracker.connected(5));

		// Away, nothing is sampled; back, the same held state is reported from rest.
		PlatformLog back;
		tracker.sample(5, held, 2.0, back);
		tracker.connect(pad(5), 3.0, back);
		tracker.sample(5, held, 3.0, back);
		EXPECT_EQ(back.text(), (std::vector<std::string>{"connected 5 Pad 0300", "ButtonDown 5 RightBumper", "Axis 5 LeftTrigger 1"}));
	}

	TEST(GamepadTrackerTest, WhatIsNotARealConnectedPadIsIgnored) {
		GamepadTracker tracker;
		PlatformLog	   log;
		tracker.sample(1, {.buttons = {true}}, 0.0, log);
		tracker.disconnect(1, 0.0, log);
		tracker.connect(pad(foundation::kMaxRealGamepads), 0.0, log); // a virtual pad's slot
		tracker.connect({.id = {.kind = foundation::DeviceKind::Keyboard}}, 0.0, log);
		EXPECT_TRUE(log.entries.empty());
		EXPECT_FALSE(tracker.connected(foundation::kMaxRealGamepads));

		tracker.connect(pad(1), 0.0, log);
		tracker.connect(pad(1), 1.0, log);
		EXPECT_EQ(log.text(), std::vector<std::string>{"connected 1 Pad 0300"});
	}

} // namespace app
