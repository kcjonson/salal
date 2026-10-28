#pragma once

// Each control's name as files spell it: foundation's enumerator names ("Space", "A", "LeftShift";
// "Left", "Button4"; "A", "DpadUp"; "LeftX", "RightTrigger"). They stay put across builds where the
// enum values, which follow GLFW's order, may not. What a player sees is Input::keyName's.

#include <input/InputTypes.h>

#include <optional>
#include <string_view>

namespace app {

	// Empty for a value out of range.
	std::string_view controlName(foundation::Key key);
	std::string_view controlName(foundation::MouseButton button);
	std::string_view controlName(foundation::GamepadButton button);
	std::string_view controlName(foundation::GamepadAxis axis);

	// The control a name names, in any case; nullopt for a name that isn't one. Defined for Key,
	// MouseButton, GamepadButton and GamepadAxis.
	template <typename Control>
	std::optional<Control> controlNamed(std::string_view name);
	template <>
	std::optional<foundation::Key> controlNamed<foundation::Key>(std::string_view name);
	template <>
	std::optional<foundation::MouseButton> controlNamed<foundation::MouseButton>(std::string_view name);
	template <>
	std::optional<foundation::GamepadButton> controlNamed<foundation::GamepadButton>(std::string_view name);
	template <>
	std::optional<foundation::GamepadAxis> controlNamed<foundation::GamepadAxis>(std::string_view name);

} // namespace app
