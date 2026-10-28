#include "platform/HeadlessPlatform.h"

#include <backend/NullBackend.h>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace app {

	namespace {

		using foundation::InputEvent;

		// What a poll reported, in order.
		class Listener final : public PlatformEvents {
		  public:
			void inputEvent(const InputEvent& event) override { events.push_back(event); }
			void framebufferResized(const Renderer::Viewport& viewport) override { resizes.push_back(viewport); }

			std::vector<InputEvent>			events;
			std::vector<Renderer::Viewport> resizes;
		};

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

		Listener listener;
		platform.pollEvents(listener);
		ASSERT_EQ(listener.events.size(), 2U);
		EXPECT_EQ(listener.events[0].type, InputEvent::Type::KeyDown);
		EXPECT_EQ(listener.events[0].time, 0.5); // stamped when pushed, as a callback would be
		EXPECT_EQ(listener.events[1].type, InputEvent::Type::FocusLost);
		EXPECT_EQ(listener.events[1].time, 0.75);
		ASSERT_EQ(listener.resizes.size(), 1U);
		EXPECT_EQ(listener.resizes[0].logicalWidth, 320);
		EXPECT_EQ(platform.viewport().framebufferHeight, 200);
		EXPECT_FALSE(platform.focused());

		Listener again;
		platform.pollEvents(again);
		EXPECT_TRUE(again.events.empty());
		EXPECT_TRUE(again.resizes.empty());
	}

	TEST(HeadlessPlatformTest, FocusThatDoesNotChangeSendsNothing) {
		HeadlessPlatform platform(640, 480);
		platform.setFocused(true);
		platform.setFocused(false);
		platform.setFocused(false);
		platform.setFocused(true);
		platform.setFocused(true);
		Listener listener;
		platform.pollEvents(listener);
		ASSERT_EQ(listener.events.size(), 2U);
		EXPECT_EQ(listener.events[0].type, InputEvent::Type::FocusLost);
		EXPECT_EQ(listener.events[1].type, InputEvent::Type::FocusGained);
		EXPECT_TRUE(platform.focused());
	}

} // namespace app
