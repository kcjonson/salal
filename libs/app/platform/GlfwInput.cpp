#include "platform/GlfwInput.h"

#include <GLFW/glfw3.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace app::glfw {

	namespace {

		using foundation::GamepadAxis;
		using foundation::GamepadButton;
		using foundation::InputEvent;
		using foundation::Key;

		constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count);

		// GLFW's code for each Key, in Key's order, which is GLFW's.
		constexpr std::array<int, kKeyCount> kGlfwKeys = {
			GLFW_KEY_UNKNOWN,
			GLFW_KEY_SPACE,
			GLFW_KEY_APOSTROPHE,
			GLFW_KEY_COMMA,
			GLFW_KEY_MINUS,
			GLFW_KEY_PERIOD,
			GLFW_KEY_SLASH,
			GLFW_KEY_0,
			GLFW_KEY_1,
			GLFW_KEY_2,
			GLFW_KEY_3,
			GLFW_KEY_4,
			GLFW_KEY_5,
			GLFW_KEY_6,
			GLFW_KEY_7,
			GLFW_KEY_8,
			GLFW_KEY_9,
			GLFW_KEY_SEMICOLON,
			GLFW_KEY_EQUAL,
			GLFW_KEY_A,
			GLFW_KEY_B,
			GLFW_KEY_C,
			GLFW_KEY_D,
			GLFW_KEY_E,
			GLFW_KEY_F,
			GLFW_KEY_G,
			GLFW_KEY_H,
			GLFW_KEY_I,
			GLFW_KEY_J,
			GLFW_KEY_K,
			GLFW_KEY_L,
			GLFW_KEY_M,
			GLFW_KEY_N,
			GLFW_KEY_O,
			GLFW_KEY_P,
			GLFW_KEY_Q,
			GLFW_KEY_R,
			GLFW_KEY_S,
			GLFW_KEY_T,
			GLFW_KEY_U,
			GLFW_KEY_V,
			GLFW_KEY_W,
			GLFW_KEY_X,
			GLFW_KEY_Y,
			GLFW_KEY_Z,
			GLFW_KEY_LEFT_BRACKET,
			GLFW_KEY_BACKSLASH,
			GLFW_KEY_RIGHT_BRACKET,
			GLFW_KEY_GRAVE_ACCENT,
			GLFW_KEY_WORLD_1,
			GLFW_KEY_WORLD_2,
			GLFW_KEY_ESCAPE,
			GLFW_KEY_ENTER,
			GLFW_KEY_TAB,
			GLFW_KEY_BACKSPACE,
			GLFW_KEY_INSERT,
			GLFW_KEY_DELETE,
			GLFW_KEY_RIGHT,
			GLFW_KEY_LEFT,
			GLFW_KEY_DOWN,
			GLFW_KEY_UP,
			GLFW_KEY_PAGE_UP,
			GLFW_KEY_PAGE_DOWN,
			GLFW_KEY_HOME,
			GLFW_KEY_END,
			GLFW_KEY_CAPS_LOCK,
			GLFW_KEY_SCROLL_LOCK,
			GLFW_KEY_NUM_LOCK,
			GLFW_KEY_PRINT_SCREEN,
			GLFW_KEY_PAUSE,
			GLFW_KEY_F1,
			GLFW_KEY_F2,
			GLFW_KEY_F3,
			GLFW_KEY_F4,
			GLFW_KEY_F5,
			GLFW_KEY_F6,
			GLFW_KEY_F7,
			GLFW_KEY_F8,
			GLFW_KEY_F9,
			GLFW_KEY_F10,
			GLFW_KEY_F11,
			GLFW_KEY_F12,
			GLFW_KEY_F13,
			GLFW_KEY_F14,
			GLFW_KEY_F15,
			GLFW_KEY_F16,
			GLFW_KEY_F17,
			GLFW_KEY_F18,
			GLFW_KEY_F19,
			GLFW_KEY_F20,
			GLFW_KEY_F21,
			GLFW_KEY_F22,
			GLFW_KEY_F23,
			GLFW_KEY_F24,
			GLFW_KEY_F25,
			GLFW_KEY_KP_0,
			GLFW_KEY_KP_1,
			GLFW_KEY_KP_2,
			GLFW_KEY_KP_3,
			GLFW_KEY_KP_4,
			GLFW_KEY_KP_5,
			GLFW_KEY_KP_6,
			GLFW_KEY_KP_7,
			GLFW_KEY_KP_8,
			GLFW_KEY_KP_9,
			GLFW_KEY_KP_DECIMAL,
			GLFW_KEY_KP_DIVIDE,
			GLFW_KEY_KP_MULTIPLY,
			GLFW_KEY_KP_SUBTRACT,
			GLFW_KEY_KP_ADD,
			GLFW_KEY_KP_ENTER,
			GLFW_KEY_KP_EQUAL,
			GLFW_KEY_LEFT_SHIFT,
			GLFW_KEY_LEFT_CONTROL,
			GLFW_KEY_LEFT_ALT,
			GLFW_KEY_LEFT_SUPER,
			GLFW_KEY_RIGHT_SHIFT,
			GLFW_KEY_RIGHT_CONTROL,
			GLFW_KEY_RIGHT_ALT,
			GLFW_KEY_RIGHT_SUPER,
			GLFW_KEY_MENU,
		};

		// A table one entry short, or out of GLFW's order, fails here: GLFW's codes rise in its order.
		constexpr bool tableInGlfwOrder() {
			for (std::size_t i = 2; i < kGlfwKeys.size(); ++i) {
				if (kGlfwKeys[i] <= kGlfwKeys[i - 1]) {
					return false;
				}
			}
			return kGlfwKeys[1] == GLFW_KEY_SPACE && kGlfwKeys.back() == GLFW_KEY_LAST;
		}
		static_assert(tableInGlfwOrder(), "kGlfwKeys must list GLFW's key tokens in Key's order, one per Key");

		constexpr std::array<Key, GLFW_KEY_LAST + 1> kKeysByGlfw = [] {
			std::array<Key, GLFW_KEY_LAST + 1> keys{};
			for (std::size_t i = 1; i < kGlfwKeys.size(); ++i) {
				keys[static_cast<std::size_t>(kGlfwKeys[i])] = static_cast<Key>(i);
			}
			return keys;
		}();

		static_assert(
			GLFW_MOD_SHIFT == foundation::ModShift && GLFW_MOD_CONTROL == foundation::ModControl && GLFW_MOD_ALT == foundation::ModAlt &&
			GLFW_MOD_SUPER == foundation::ModSuper
		);
		constexpr int kModMask = GLFW_MOD_SHIFT | GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER;

		static_assert(GLFW_MOUSE_BUTTON_LAST + 1 == static_cast<int>(foundation::MouseButton::Count));
		static_assert(GLFW_GAMEPAD_BUTTON_LAST + 1 == static_cast<int>(GamepadButton::Count));
		static_assert(GLFW_GAMEPAD_BUTTON_DPAD_LEFT == static_cast<int>(GamepadButton::DpadLeft));
		static_assert(GLFW_GAMEPAD_AXIS_LAST + 1 == static_cast<int>(GamepadAxis::Count));
		static_assert(GLFW_GAMEPAD_AXIS_LEFT_TRIGGER == static_cast<int>(GamepadAxis::LeftTrigger));

		constexpr foundation::DeviceId kKeyboard{.kind = foundation::DeviceKind::Keyboard};
		constexpr foundation::DeviceId kMouse{.kind = foundation::DeviceKind::Mouse};

		std::uint8_t modsOf(int mods) {
			return static_cast<std::uint8_t>(mods & kModMask);
		}

		glm::vec2 pointOf(double x, double y) {
			return {static_cast<float>(x), static_cast<float>(y)};
		}

	} // namespace

	Key toKey(int glfwKey) {
		if (glfwKey < 0 || glfwKey > GLFW_KEY_LAST) {
			return Key::Unknown;
		}
		return kKeysByGlfw[static_cast<std::size_t>(glfwKey)];
	}

	int toGlfwKey(Key key) {
		const auto index = static_cast<std::size_t>(key);
		return index < kGlfwKeys.size() ? kGlfwKeys[index] : GLFW_KEY_UNKNOWN;
	}

	InputEvent keyEvent(int glfwKey, int action, int mods, double time) {
		return {
			.type = action == GLFW_RELEASE ? InputEvent::Type::KeyUp : InputEvent::Type::KeyDown,
			.device = kKeyboard,
			.time = time,
			.mods = modsOf(mods),
			.repeat = action == GLFW_REPEAT,
			.key = toKey(glfwKey),
		};
	}

	InputEvent charEvent(unsigned int codepoint, double time) {
		return {.type = InputEvent::Type::Char, .device = kKeyboard, .time = time, .codepoint = static_cast<char32_t>(codepoint)};
	}

	InputEvent cursorPosEvent(double x, double y, double time) {
		return {.type = InputEvent::Type::PointerMove, .device = kMouse, .time = time, .position = pointOf(x, y)};
	}

	InputEvent cursorEnterEvent(int entered, double time) {
		return {
			.type = entered == GLFW_TRUE ? InputEvent::Type::PointerEnter : InputEvent::Type::PointerLeave, .device = kMouse, .time = time
		};
	}

	InputEvent mouseButtonEvent(int button, int action, int mods, glm::vec2 cursor, double time) {
		return {
			.type = action == GLFW_PRESS ? InputEvent::Type::PointerDown : InputEvent::Type::PointerUp,
			.device = kMouse,
			.time = time,
			.mods = modsOf(mods),
			.mouseButton = static_cast<foundation::MouseButton>(button),
			.position = cursor,
		};
	}

	InputEvent scrollEvent(double x, double y, glm::vec2 cursor, double time) {
		return {.type = InputEvent::Type::Scroll, .device = kMouse, .time = time, .position = cursor, .scroll = pointOf(x, y)};
	}

	GamepadSample gamepadSample(std::span<const unsigned char, 15> buttons, std::span<const float, 6> axes, TriggersSeen& seen) {
		GamepadSample sample;
		for (std::size_t i = 0; i < sample.buttons.size(); ++i) {
			sample.buttons[i] = buttons[i] == GLFW_PRESS;
		}
		for (std::size_t i = 0; i < sample.axes.size(); ++i) {
			sample.axes[i] = axes[i];
		}
		const auto remap = [&sample](GamepadAxis trigger, bool& rested) {
			float& value = sample.axes[static_cast<std::size_t>(trigger)];
			rested = rested || value < 0.0F;
			value = rested ? (value + 1.0F) * 0.5F : 0.0F;
		};
		remap(GamepadAxis::LeftTrigger, seen.left);
		remap(GamepadAxis::RightTrigger, seen.right);
		return sample;
	}

} // namespace app::glfw
