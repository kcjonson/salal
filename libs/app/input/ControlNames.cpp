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

		template <typename Control>
		struct Named {
			Control			 control{};
			std::string_view name;
		};

		// Each entry is checked to sit at its enumerator's value, so an enumerator added mid-enum with its
		// name put anywhere else fails the build instead of moving every saved binding after it.
		constexpr std::array<Named<Key>, kCount<Key>>					  kKeys{{
			{.control = Key::Unknown, .name = "Unknown"},
			{.control = Key::Space, .name = "Space"},
			{.control = Key::Apostrophe, .name = "Apostrophe"},
			{.control = Key::Comma, .name = "Comma"},
			{.control = Key::Minus, .name = "Minus"},
			{.control = Key::Period, .name = "Period"},
			{.control = Key::Slash, .name = "Slash"},
			{.control = Key::Num0, .name = "Num0"},
			{.control = Key::Num1, .name = "Num1"},
			{.control = Key::Num2, .name = "Num2"},
			{.control = Key::Num3, .name = "Num3"},
			{.control = Key::Num4, .name = "Num4"},
			{.control = Key::Num5, .name = "Num5"},
			{.control = Key::Num6, .name = "Num6"},
			{.control = Key::Num7, .name = "Num7"},
			{.control = Key::Num8, .name = "Num8"},
			{.control = Key::Num9, .name = "Num9"},
			{.control = Key::Semicolon, .name = "Semicolon"},
			{.control = Key::Equal, .name = "Equal"},
			{.control = Key::A, .name = "A"},
			{.control = Key::B, .name = "B"},
			{.control = Key::C, .name = "C"},
			{.control = Key::D, .name = "D"},
			{.control = Key::E, .name = "E"},
			{.control = Key::F, .name = "F"},
			{.control = Key::G, .name = "G"},
			{.control = Key::H, .name = "H"},
			{.control = Key::I, .name = "I"},
			{.control = Key::J, .name = "J"},
			{.control = Key::K, .name = "K"},
			{.control = Key::L, .name = "L"},
			{.control = Key::M, .name = "M"},
			{.control = Key::N, .name = "N"},
			{.control = Key::O, .name = "O"},
			{.control = Key::P, .name = "P"},
			{.control = Key::Q, .name = "Q"},
			{.control = Key::R, .name = "R"},
			{.control = Key::S, .name = "S"},
			{.control = Key::T, .name = "T"},
			{.control = Key::U, .name = "U"},
			{.control = Key::V, .name = "V"},
			{.control = Key::W, .name = "W"},
			{.control = Key::X, .name = "X"},
			{.control = Key::Y, .name = "Y"},
			{.control = Key::Z, .name = "Z"},
			{.control = Key::LeftBracket, .name = "LeftBracket"},
			{.control = Key::Backslash, .name = "Backslash"},
			{.control = Key::RightBracket, .name = "RightBracket"},
			{.control = Key::GraveAccent, .name = "GraveAccent"},
			{.control = Key::World1, .name = "World1"},
			{.control = Key::World2, .name = "World2"},
			{.control = Key::Escape, .name = "Escape"},
			{.control = Key::Enter, .name = "Enter"},
			{.control = Key::Tab, .name = "Tab"},
			{.control = Key::Backspace, .name = "Backspace"},
			{.control = Key::Insert, .name = "Insert"},
			{.control = Key::Delete, .name = "Delete"},
			{.control = Key::Right, .name = "Right"},
			{.control = Key::Left, .name = "Left"},
			{.control = Key::Down, .name = "Down"},
			{.control = Key::Up, .name = "Up"},
			{.control = Key::PageUp, .name = "PageUp"},
			{.control = Key::PageDown, .name = "PageDown"},
			{.control = Key::Home, .name = "Home"},
			{.control = Key::End, .name = "End"},
			{.control = Key::CapsLock, .name = "CapsLock"},
			{.control = Key::ScrollLock, .name = "ScrollLock"},
			{.control = Key::NumLock, .name = "NumLock"},
			{.control = Key::PrintScreen, .name = "PrintScreen"},
			{.control = Key::Pause, .name = "Pause"},
			{.control = Key::F1, .name = "F1"},
			{.control = Key::F2, .name = "F2"},
			{.control = Key::F3, .name = "F3"},
			{.control = Key::F4, .name = "F4"},
			{.control = Key::F5, .name = "F5"},
			{.control = Key::F6, .name = "F6"},
			{.control = Key::F7, .name = "F7"},
			{.control = Key::F8, .name = "F8"},
			{.control = Key::F9, .name = "F9"},
			{.control = Key::F10, .name = "F10"},
			{.control = Key::F11, .name = "F11"},
			{.control = Key::F12, .name = "F12"},
			{.control = Key::F13, .name = "F13"},
			{.control = Key::F14, .name = "F14"},
			{.control = Key::F15, .name = "F15"},
			{.control = Key::F16, .name = "F16"},
			{.control = Key::F17, .name = "F17"},
			{.control = Key::F18, .name = "F18"},
			{.control = Key::F19, .name = "F19"},
			{.control = Key::F20, .name = "F20"},
			{.control = Key::F21, .name = "F21"},
			{.control = Key::F22, .name = "F22"},
			{.control = Key::F23, .name = "F23"},
			{.control = Key::F24, .name = "F24"},
			{.control = Key::F25, .name = "F25"},
			{.control = Key::Kp0, .name = "Kp0"},
			{.control = Key::Kp1, .name = "Kp1"},
			{.control = Key::Kp2, .name = "Kp2"},
			{.control = Key::Kp3, .name = "Kp3"},
			{.control = Key::Kp4, .name = "Kp4"},
			{.control = Key::Kp5, .name = "Kp5"},
			{.control = Key::Kp6, .name = "Kp6"},
			{.control = Key::Kp7, .name = "Kp7"},
			{.control = Key::Kp8, .name = "Kp8"},
			{.control = Key::Kp9, .name = "Kp9"},
			{.control = Key::KpDecimal, .name = "KpDecimal"},
			{.control = Key::KpDivide, .name = "KpDivide"},
			{.control = Key::KpMultiply, .name = "KpMultiply"},
			{.control = Key::KpSubtract, .name = "KpSubtract"},
			{.control = Key::KpAdd, .name = "KpAdd"},
			{.control = Key::KpEnter, .name = "KpEnter"},
			{.control = Key::KpEqual, .name = "KpEqual"},
			{.control = Key::LeftShift, .name = "LeftShift"},
			{.control = Key::LeftControl, .name = "LeftControl"},
			{.control = Key::LeftAlt, .name = "LeftAlt"},
			{.control = Key::LeftSuper, .name = "LeftSuper"},
			{.control = Key::RightShift, .name = "RightShift"},
			{.control = Key::RightControl, .name = "RightControl"},
			{.control = Key::RightAlt, .name = "RightAlt"},
			{.control = Key::RightSuper, .name = "RightSuper"},
			{.control = Key::Menu, .name = "Menu"},
		}};
		constexpr std::array<Named<MouseButton>, kCount<MouseButton>>	  kMouseButtons{{
			{.control = MouseButton::Left, .name = "Left"},
			{.control = MouseButton::Right, .name = "Right"},
			{.control = MouseButton::Middle, .name = "Middle"},
			{.control = MouseButton::Button4, .name = "Button4"},
			{.control = MouseButton::Button5, .name = "Button5"},
			{.control = MouseButton::Button6, .name = "Button6"},
			{.control = MouseButton::Button7, .name = "Button7"},
			{.control = MouseButton::Button8, .name = "Button8"},
		}};
		constexpr std::array<Named<GamepadButton>, kCount<GamepadButton>> kButtons{{
			{.control = GamepadButton::A, .name = "A"},
			{.control = GamepadButton::B, .name = "B"},
			{.control = GamepadButton::X, .name = "X"},
			{.control = GamepadButton::Y, .name = "Y"},
			{.control = GamepadButton::LeftBumper, .name = "LeftBumper"},
			{.control = GamepadButton::RightBumper, .name = "RightBumper"},
			{.control = GamepadButton::Back, .name = "Back"},
			{.control = GamepadButton::Start, .name = "Start"},
			{.control = GamepadButton::Guide, .name = "Guide"},
			{.control = GamepadButton::LeftThumb, .name = "LeftThumb"},
			{.control = GamepadButton::RightThumb, .name = "RightThumb"},
			{.control = GamepadButton::DpadUp, .name = "DpadUp"},
			{.control = GamepadButton::DpadRight, .name = "DpadRight"},
			{.control = GamepadButton::DpadDown, .name = "DpadDown"},
			{.control = GamepadButton::DpadLeft, .name = "DpadLeft"},
		}};
		constexpr std::array<Named<GamepadAxis>, kCount<GamepadAxis>>	  kAxes{{
			{.control = GamepadAxis::LeftX, .name = "LeftX"},
			{.control = GamepadAxis::LeftY, .name = "LeftY"},
			{.control = GamepadAxis::RightX, .name = "RightX"},
			{.control = GamepadAxis::RightY, .name = "RightY"},
			{.control = GamepadAxis::LeftTrigger, .name = "LeftTrigger"},
			{.control = GamepadAxis::RightTrigger, .name = "RightTrigger"},
		}};

		template <typename Control, std::size_t N>
		constexpr bool inOrder(const std::array<Named<Control>, N>& names) {
			for (std::size_t i = 0; i < N; ++i) {
				if (static_cast<std::size_t>(names[i].control) != i || names[i].name.empty()) {
					return false;
				}
			}
			return true;
		}
		static_assert(inOrder(kKeys), "a name for every Key, at its value");
		static_assert(inOrder(kMouseButtons), "a name for every MouseButton, at its value");
		static_assert(inOrder(kButtons), "a name for every GamepadButton, at its value");
		static_assert(inOrder(kAxes), "a name for every GamepadAxis, at its value");

		template <typename Control, std::size_t N>
		std::string_view nameIn(const std::array<Named<Control>, N>& names, Control control) {
			const auto index = static_cast<std::size_t>(control);
			return index < names.size() ? names[index].name : std::string_view();
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
		std::optional<Control> namedIn(const std::array<Named<Control>, N>& names, std::string_view name) {
			for (const Named<Control>& named : names) {
				if (sameIgnoringCase(named.name, name)) {
					return named.control;
				}
			}
			return std::nullopt;
		}

	} // namespace

	std::string_view controlName(Key key) {
		return nameIn(kKeys, key);
	}

	std::string_view controlName(MouseButton button) {
		return nameIn(kMouseButtons, button);
	}

	std::string_view controlName(GamepadButton button) {
		return nameIn(kButtons, button);
	}

	std::string_view controlName(GamepadAxis axis) {
		return nameIn(kAxes, axis);
	}

	template <>
	std::optional<Key> controlNamed<Key>(std::string_view name) {
		return namedIn(kKeys, name);
	}

	template <>
	std::optional<MouseButton> controlNamed<MouseButton>(std::string_view name) {
		return namedIn(kMouseButtons, name);
	}

	template <>
	std::optional<GamepadButton> controlNamed<GamepadButton>(std::string_view name) {
		return namedIn(kButtons, name);
	}

	template <>
	std::optional<GamepadAxis> controlNamed<GamepadAxis>(std::string_view name) {
		return namedIn(kAxes, name);
	}

} // namespace app
