#include "InputTypes.h"

#include <gtest/gtest.h>

using namespace foundation;

namespace {

	template <typename Enum>
	constexpr int valueOf(Enum value) {
		return static_cast<int>(value);
	}

	// Consumers build keys from these runs (worldsim's keyFromName does Key::A + (c - 'a')).
	static_assert(valueOf(Key::Z) - valueOf(Key::A) == 25);
	static_assert(valueOf(Key::Num9) - valueOf(Key::Num0) == 9);
	static_assert(valueOf(Key::F25) - valueOf(Key::F1) == 24);

	template <typename Enum>
	constexpr bool follows(Enum before, Enum after) {
		return valueOf(after) == valueOf(before) + 1;
	}

	// GLFW's order across every group boundary, and where GLFW's navigation keys differ from the
	// order worldsim's enum had.
	static_assert(follows(Key::Unknown, Key::Space));
	static_assert(follows(Key::Slash, Key::Num0));
	static_assert(follows(Key::Num9, Key::Semicolon));
	static_assert(follows(Key::Equal, Key::A));
	static_assert(follows(Key::Z, Key::LeftBracket));
	static_assert(follows(Key::World2, Key::Escape));
	static_assert(follows(Key::Delete, Key::Right));
	static_assert(follows(Key::Up, Key::PageUp));
	static_assert(follows(Key::End, Key::CapsLock));
	static_assert(follows(Key::Pause, Key::F1));
	static_assert(follows(Key::F25, Key::Kp0));
	static_assert(follows(Key::KpEqual, Key::LeftShift));
	static_assert(follows(Key::RightSuper, Key::Menu));
	static_assert(follows(Key::Menu, Key::Count));

	// A GLFW button number casts straight to MouseButton.
	static_assert(valueOf(MouseButton::Left) == 0);
	static_assert(valueOf(MouseButton::Right) == 1);
	static_assert(valueOf(MouseButton::Middle) == 2);
	static_assert(valueOf(MouseButton::Button4) == 3);
	static_assert(valueOf(MouseButton::Button5) == 4);
	static_assert(valueOf(MouseButton::Button6) == 5);
	static_assert(valueOf(MouseButton::Button7) == 6);
	static_assert(valueOf(MouseButton::Button8) == 7);

	static_assert(isPadSlot({.kind = DeviceKind::Gamepad, .index = 0}));
	static_assert(isPadSlot({.kind = DeviceKind::Gamepad, .index = kMaxRealGamepads + kMaxVirtualGamepads - 1}));
	static_assert(!isPadSlot({.kind = DeviceKind::Gamepad, .index = kMaxRealGamepads + kMaxVirtualGamepads}));
	static_assert(!isPadSlot({.kind = DeviceKind::Keyboard, .index = 0}));

	TEST(InputTypes, KeyIsDenseFromUnknownToCount) {
		EXPECT_EQ(valueOf(Key::Unknown), 0);
		EXPECT_EQ(valueOf(Key::Space), 1);
		// GLFW 3.4 names 120 keys besides GLFW_KEY_UNKNOWN.
		EXPECT_EQ(valueOf(Key::Menu), 120);
		EXPECT_EQ(valueOf(Key::Count), 121);
	}

	TEST(InputTypes, ControlCounts) {
		EXPECT_EQ(valueOf(MouseButton::Count), 8);
		EXPECT_EQ(valueOf(GamepadButton::Count), 15);
		EXPECT_EQ(valueOf(GamepadAxis::Count), 6);
		EXPECT_EQ(InputSnapshot{}.pads.size(), 20U);
	}

	TEST(ButtonState, PressedAndReleasedFromTransitions) {
		struct Case {
			bool		 down;
			std::uint8_t transitions;
			bool		 pressed;
			bool		 released;
		};
		const Case cases[] = {
			{.down = false, .transitions = 0, .pressed = false, .released = false},
			{.down = true, .transitions = 0, .pressed = false, .released = false},
			{.down = true, .transitions = 1, .pressed = true, .released = false},
			{.down = false, .transitions = 1, .pressed = false, .released = true},
			{.down = false, .transitions = 2, .pressed = true, .released = true},
			{.down = true, .transitions = 2, .pressed = true, .released = true},
			{.down = true, .transitions = 3, .pressed = true, .released = true},
		};
		for (const Case& c : cases) {
			const ButtonState state{.down = c.down, .transitions = c.transitions};
			EXPECT_EQ(state.pressed(), c.pressed) << "down " << c.down << ", transitions " << int{c.transitions};
			EXPECT_EQ(state.released(), c.released) << "down " << c.down << ", transitions " << int{c.transitions};
		}
	}

} // namespace
