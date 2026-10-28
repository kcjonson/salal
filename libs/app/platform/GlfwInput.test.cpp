// What each GLFW callback and the gamepad poll turn into, through the translation functions, with no
// window. GLFW's codes are written out as numbers, which also pins the values the key table assumes.

#include "platform/GlfwInput.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <set>

namespace app {

	namespace {

		using foundation::GamepadAxis;
		using foundation::GamepadButton;
		using foundation::InputEvent;
		using foundation::Key;
		using foundation::MouseButton;

		constexpr int kRelease = 0; // GLFW_RELEASE
		constexpr int kPress = 1;	// GLFW_PRESS
		constexpr int kRepeat = 2;	// GLFW_REPEAT

		constexpr foundation::DeviceId kKeyboard{.kind = foundation::DeviceKind::Keyboard};
		constexpr foundation::DeviceId kMouse{.kind = foundation::DeviceKind::Mouse};

	} // namespace

	TEST(GlfwInputTest, EveryKeyRoundTripsThroughItsOwnGlfwCode) {
		std::set<int> codes;
		for (std::size_t i = 1; i < static_cast<std::size_t>(Key::Count); ++i) {
			const auto key = static_cast<Key>(i);
			const int  code = glfw::toGlfwKey(key);
			EXPECT_GE(code, 32) << "Key " << i;
			EXPECT_EQ(glfw::toKey(code), key) << "Key " << i;
			codes.insert(code);
		}
		EXPECT_EQ(codes.size(), static_cast<std::size_t>(Key::Count) - 1);
	}

	TEST(GlfwInputTest, KeyCodesAreGlfws) {
		EXPECT_EQ(glfw::toGlfwKey(Key::Space), 32);
		EXPECT_EQ(glfw::toGlfwKey(Key::Apostrophe), 39);
		EXPECT_EQ(glfw::toGlfwKey(Key::Num0), 48);
		EXPECT_EQ(glfw::toGlfwKey(Key::A), 65);
		EXPECT_EQ(glfw::toGlfwKey(Key::Z), 90);
		EXPECT_EQ(glfw::toGlfwKey(Key::GraveAccent), 96);
		EXPECT_EQ(glfw::toGlfwKey(Key::World1), 161);
		EXPECT_EQ(glfw::toGlfwKey(Key::Escape), 256);
		EXPECT_EQ(glfw::toGlfwKey(Key::Pause), 284);
		EXPECT_EQ(glfw::toGlfwKey(Key::F1), 290);
		EXPECT_EQ(glfw::toGlfwKey(Key::F25), 314);
		EXPECT_EQ(glfw::toGlfwKey(Key::Kp0), 320);
		EXPECT_EQ(glfw::toGlfwKey(Key::KpEqual), 336);
		EXPECT_EQ(glfw::toGlfwKey(Key::LeftShift), 340);
		EXPECT_EQ(glfw::toGlfwKey(Key::Menu), 348);
	}

	TEST(GlfwInputTest, UnknownAndUndefinedCodesAreUnknown) {
		EXPECT_EQ(glfw::toGlfwKey(Key::Unknown), -1);
		EXPECT_EQ(glfw::toGlfwKey(Key::Count), -1);
		EXPECT_EQ(glfw::toKey(-1), Key::Unknown);
		EXPECT_EQ(glfw::toKey(0), Key::Unknown);
		EXPECT_EQ(glfw::toKey(33), Key::Unknown);  // between SPACE and APOSTROPHE
		EXPECT_EQ(glfw::toKey(270), Key::Unknown); // between END and CAPS_LOCK
		EXPECT_EQ(glfw::toKey(349), Key::Unknown); // past GLFW_KEY_LAST
	}

	TEST(GlfwInputTest, KeyCallbacksAreKeyDownsRepeatsAndKeyUps) {
		const InputEvent down = glfw::keyEvent(87, kPress, 0x01 | 0x02, 1.5); // W with Shift and Control
		EXPECT_EQ(down.type, InputEvent::Type::KeyDown);
		EXPECT_EQ(down.device, kKeyboard);
		EXPECT_EQ(down.time, 1.5);
		EXPECT_EQ(down.key, Key::W);
		EXPECT_FALSE(down.repeat);
		EXPECT_EQ(down.mods, foundation::ModShift | foundation::ModControl);

		const InputEvent repeat = glfw::keyEvent(87, kRepeat, 0, 1.6);
		EXPECT_EQ(repeat.type, InputEvent::Type::KeyDown);
		EXPECT_TRUE(repeat.repeat);

		// GLFW's Caps Lock (0x10) and Num Lock (0x20) bits have no Mod.
		const InputEvent up = glfw::keyEvent(87, kRelease, 0x3F, 1.7);
		EXPECT_EQ(up.type, InputEvent::Type::KeyUp);
		EXPECT_FALSE(up.repeat);
		EXPECT_EQ(up.mods, foundation::ModShift | foundation::ModControl | foundation::ModAlt | foundation::ModSuper);

		EXPECT_EQ(glfw::keyEvent(-1, kPress, 0, 0.0).key, Key::Unknown);
	}

	TEST(GlfwInputTest, CharCallbackIsACharWithNoMods) {
		const InputEvent typed = glfw::charEvent(0x00E9, 2.0); // e acute
		EXPECT_EQ(typed.type, InputEvent::Type::Char);
		EXPECT_EQ(typed.device, kKeyboard);
		EXPECT_EQ(typed.codepoint, U'é');
		EXPECT_EQ(typed.mods, 0);
		EXPECT_EQ(typed.time, 2.0);
	}

	TEST(GlfwInputTest, CursorCallbacksAreMovesEntersAndLeaves) {
		const InputEvent move = glfw::cursorPosEvent(10.5, 20.25, 3.0);
		EXPECT_EQ(move.type, InputEvent::Type::PointerMove);
		EXPECT_EQ(move.device, kMouse);
		EXPECT_EQ(move.position, glm::vec2(10.5F, 20.25F));
		EXPECT_EQ(move.time, 3.0);

		EXPECT_EQ(glfw::cursorEnterEvent(1, 0.0).type, InputEvent::Type::PointerEnter);
		EXPECT_EQ(glfw::cursorEnterEvent(0, 0.0).type, InputEvent::Type::PointerLeave);
		EXPECT_EQ(glfw::cursorEnterEvent(1, 0.0).device, kMouse);
	}

	// GLFW's button and scroll callbacks carry no position; the platform passes the cursor's.
	TEST(GlfwInputTest, ButtonAndScrollCallbacksCarryTheCursor) {
		const InputEvent down = glfw::mouseButtonEvent(3, kPress, 0x04, {7.0F, 9.0F}, 4.0); // GLFW_MOUSE_BUTTON_4, Alt
		EXPECT_EQ(down.type, InputEvent::Type::PointerDown);
		EXPECT_EQ(down.device, kMouse);
		EXPECT_EQ(down.mouseButton, MouseButton::Button4);
		EXPECT_EQ(down.mods, foundation::ModAlt);
		EXPECT_EQ(down.position, glm::vec2(7.0F, 9.0F));
		EXPECT_EQ(down.time, 4.0);

		const InputEvent up = glfw::mouseButtonEvent(0, kRelease, 0, {8.0F, 9.0F}, 4.5);
		EXPECT_EQ(up.type, InputEvent::Type::PointerUp);
		EXPECT_EQ(up.mouseButton, MouseButton::Left);
		EXPECT_EQ(up.position, glm::vec2(8.0F, 9.0F));

		const InputEvent scroll = glfw::scrollEvent(-1.0, 2.5, {3.0F, 4.0F}, 5.0);
		EXPECT_EQ(scroll.type, InputEvent::Type::Scroll);
		EXPECT_EQ(scroll.device, kMouse);
		EXPECT_EQ(scroll.scroll, glm::vec2(-1.0F, 2.5F));
		EXPECT_EQ(scroll.position, glm::vec2(3.0F, 4.0F));
		EXPECT_EQ(scroll.mods, 0);
	}

	TEST(GlfwInputTest, AGamepadStateBecomesASampleWithTriggersFromZero) {
		std::array<unsigned char, 15> buttons{};
		buttons[0] = kPress;  // A
		buttons[14] = kPress; // DpadLeft
		buttons[7] = kRelease;
		const std::array<float, 6> axes{-1.0F, 0.5F, 1.0F, -0.25F, -1.0F, 0.0F};

		const GamepadSample sample = glfw::gamepadSample(buttons, axes);
		EXPECT_TRUE(sample.buttons[static_cast<std::size_t>(GamepadButton::A)]);
		EXPECT_TRUE(sample.buttons[static_cast<std::size_t>(GamepadButton::DpadLeft)]);
		EXPECT_FALSE(sample.buttons[static_cast<std::size_t>(GamepadButton::Start)]);
		EXPECT_FALSE(sample.buttons[static_cast<std::size_t>(GamepadButton::B)]);
		EXPECT_EQ(sample.axes[static_cast<std::size_t>(GamepadAxis::LeftX)], -1.0F);
		EXPECT_EQ(sample.axes[static_cast<std::size_t>(GamepadAxis::LeftY)], 0.5F);
		EXPECT_EQ(sample.axes[static_cast<std::size_t>(GamepadAxis::RightX)], 1.0F);
		EXPECT_EQ(sample.axes[static_cast<std::size_t>(GamepadAxis::RightY)], -0.25F);
		// A released trigger reads 0, half pulled 0.5, fully 1.
		EXPECT_EQ(sample.axes[static_cast<std::size_t>(GamepadAxis::LeftTrigger)], 0.0F);
		EXPECT_EQ(sample.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)], 0.5F);
		EXPECT_EQ(glfw::gamepadSample(buttons, std::array<float, 6>{0, 0, 0, 0, 1.0F, 1.0F}).axes[5], 1.0F);
	}

} // namespace app
