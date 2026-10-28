// The GLFW platform's relay: what arrives outside a poll is held in order for the next one, a held
// resize goes first and only the latest, and inside a poll everything passes straight through.

#include "platform/EventRelay.h"
#include "tests/PlatformLog.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace app {

	namespace {

		using foundation::GamepadButton;
		using foundation::InputEvent;
		using testing::PlatformLog;

		const DeviceInfo kPad{.id = padId(2), .name = "Pad", .modelGuid = "0302"};

	} // namespace

	TEST(EventRelayTest, WhatArrivesOutsideAPollWaitsForTheNextInOrder) {
		EventRelay relay;
		relay.deviceConnected(kPad, 1.0);
		relay.framebufferResized({.logicalWidth = 640, .logicalHeight = 480, .framebufferWidth = 640, .framebufferHeight = 480});
		relay.inputEvent({.type = InputEvent::Type::ButtonDown, .device = padId(2), .time = 2.0, .button = GamepadButton::Y});
		relay.framebufferResized({.logicalWidth = 800, .logicalHeight = 600, .framebufferWidth = 800, .framebufferHeight = 600});
		relay.deviceDisconnected(padId(2), 3.0);

		PlatformLog log;
		relay.attach(log);
		EXPECT_EQ(log.text(), (std::vector<std::string>{"connected 2 Pad 0302", "ButtonDown 2 Y", "disconnected 2"}));
		EXPECT_EQ(log.entries[0].time, 1.0);
		EXPECT_EQ(log.entries[1].time, 2.0);
		EXPECT_EQ(log.entries[2].time, 3.0);
		ASSERT_EQ(log.resizes.size(), 1U);
		EXPECT_EQ(log.resizes[0].logicalWidth, 800);

		// Attached, everything passes straight through.
		relay.inputEvent({.type = InputEvent::Type::ButtonUp, .device = padId(2), .button = GamepadButton::Y});
		relay.framebufferResized({.logicalWidth = 320, .logicalHeight = 200, .framebufferWidth = 320, .framebufferHeight = 200});
		EXPECT_EQ(log.text().back(), "ButtonUp 2 Y");
		ASSERT_EQ(log.resizes.size(), 2U);
		EXPECT_EQ(log.resizes[1].logicalWidth, 320);
	}

	TEST(EventRelayTest, DetachedItHoldsAgainAndWhatWasDeliveredIsNotDeliveredTwice) {
		EventRelay	relay;
		PlatformLog first;
		relay.inputEvent({.type = InputEvent::Type::FocusLost});
		relay.attach(first);
		relay.detach();
		relay.inputEvent({.type = InputEvent::Type::FocusGained});
		EXPECT_EQ(first.inputs().size(), 1U);

		PlatformLog second;
		relay.attach(second);
		ASSERT_EQ(second.inputs().size(), 1U);
		EXPECT_EQ(second.inputs()[0].type, InputEvent::Type::FocusGained);
		EXPECT_TRUE(second.resizes.empty());
	}

} // namespace app
