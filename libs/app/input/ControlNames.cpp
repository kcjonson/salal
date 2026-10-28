#include "input/ControlNames.h"

#include <array>
#include <cstddef>

namespace app {

	namespace {

		using foundation::GamepadAxis;
		using foundation::GamepadButton;
		using foundation::Key;
		using foundation::MouseButton;

		template <typename Control>
		constexpr std::size_t kCount = static_cast<std::size_t>(Control::Count);

		// In the enums' order, read by position.
		constexpr std::array<std::string_view, kCount<Key>> kKeyNames{
			"Unknown",	   "Space",
			"Apostrophe",  "Comma",
			"Minus",	   "Period",
			"Slash",	   "Num0",
			"Num1",		   "Num2",
			"Num3",		   "Num4",
			"Num5",		   "Num6",
			"Num7",		   "Num8",
			"Num9",		   "Semicolon",
			"Equal",	   "A",
			"B",		   "C",
			"D",		   "E",
			"F",		   "G",
			"H",		   "I",
			"J",		   "K",
			"L",		   "M",
			"N",		   "O",
			"P",		   "Q",
			"R",		   "S",
			"T",		   "U",
			"V",		   "W",
			"X",		   "Y",
			"Z",		   "LeftBracket",
			"Backslash",   "RightBracket",
			"GraveAccent", "World1",
			"World2",	   "Escape",
			"Enter",	   "Tab",
			"Backspace",   "Insert",
			"Delete",	   "Right",
			"Left",		   "Down",
			"Up",		   "PageUp",
			"PageDown",	   "Home",
			"End",		   "CapsLock",
			"ScrollLock",  "NumLock",
			"PrintScreen", "Pause",
			"F1",		   "F2",
			"F3",		   "F4",
			"F5",		   "F6",
			"F7",		   "F8",
			"F9",		   "F10",
			"F11",		   "F12",
			"F13",		   "F14",
			"F15",		   "F16",
			"F17",		   "F18",
			"F19",		   "F20",
			"F21",		   "F22",
			"F23",		   "F24",
			"F25",		   "Kp0",
			"Kp1",		   "Kp2",
			"Kp3",		   "Kp4",
			"Kp5",		   "Kp6",
			"Kp7",		   "Kp8",
			"Kp9",		   "KpDecimal",
			"KpDivide",	   "KpMultiply",
			"KpSubtract",  "KpAdd",
			"KpEnter",	   "KpEqual",
			"LeftShift",   "LeftControl",
			"LeftAlt",	   "LeftSuper",
			"RightShift",  "RightControl",
			"RightAlt",	   "RightSuper",
			"Menu",
		};
		constexpr std::array<std::string_view, kCount<MouseButton>> kMouseNames{
			"Left",
			"Right",
			"Middle",
			"Button4",
			"Button5",
			"Button6",
			"Button7",
			"Button8",
		};
		constexpr std::array<std::string_view, kCount<GamepadButton>> kButtonNames{
			"A",
			"B",
			"X",
			"Y",
			"LeftBumper",
			"RightBumper",
			"Back",
			"Start",
			"Guide",
			"LeftThumb",
			"RightThumb",
			"DpadUp",
			"DpadRight",
			"DpadDown",
			"DpadLeft",
		};
		constexpr std::array<std::string_view, kCount<GamepadAxis>> kAxisNames{
			"LeftX",
			"LeftY",
			"RightX",
			"RightY",
			"LeftTrigger",
			"RightTrigger",
		};

		// A name left out (an enum value added before Count) would leave an empty one, which every
		// lookup would then skip.
		template <std::size_t N>
		constexpr bool everyNameSet(const std::array<std::string_view, N>& names) {
			for (const std::string_view name : names) {
				if (name.empty()) {
					return false;
				}
			}
			return true;
		}
		static_assert(everyNameSet(kKeyNames), "a name for every Key, in its order");
		static_assert(everyNameSet(kMouseNames), "a name for every MouseButton, in its order");
		static_assert(everyNameSet(kButtonNames), "a name for every GamepadButton, in its order");
		static_assert(everyNameSet(kAxisNames), "a name for every GamepadAxis, in its order");

		template <typename Control, std::size_t N>
		std::string_view nameIn(const std::array<std::string_view, N>& names, Control control) {
			const auto index = static_cast<std::size_t>(control);
			return index < names.size() ? names[index] : std::string_view();
		}

		bool sameIgnoringCase(std::string_view a, std::string_view b) {
			const auto lower = [](char c) {
				return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
			};
			if (a.size() != b.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a.size(); ++i) {
				if (lower(a[i]) != lower(b[i])) {
					return false;
				}
			}
			return true;
		}

		template <typename Control, std::size_t N>
		std::optional<Control> namedIn(const std::array<std::string_view, N>& names, std::string_view name) {
			for (std::size_t i = 0; i < names.size(); ++i) {
				if (sameIgnoringCase(names[i], name)) {
					return static_cast<Control>(i);
				}
			}
			return std::nullopt;
		}

	} // namespace

	std::string_view controlName(Key key) {
		return nameIn(kKeyNames, key);
	}

	std::string_view controlName(MouseButton button) {
		return nameIn(kMouseNames, button);
	}

	std::string_view controlName(GamepadButton button) {
		return nameIn(kButtonNames, button);
	}

	std::string_view controlName(GamepadAxis axis) {
		return nameIn(kAxisNames, axis);
	}

	template <>
	std::optional<Key> controlNamed<Key>(std::string_view name) {
		return namedIn<Key>(kKeyNames, name);
	}

	template <>
	std::optional<MouseButton> controlNamed<MouseButton>(std::string_view name) {
		return namedIn<MouseButton>(kMouseNames, name);
	}

	template <>
	std::optional<GamepadButton> controlNamed<GamepadButton>(std::string_view name) {
		return namedIn<GamepadButton>(kButtonNames, name);
	}

	template <>
	std::optional<GamepadAxis> controlNamed<GamepadAxis>(std::string_view name) {
		return namedIn<GamepadAxis>(kAxisNames, name);
	}

} // namespace app
