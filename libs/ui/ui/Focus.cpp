#include "ui/Focus.h"

namespace ui {

	namespace {
		using foundation::Binding;

		Binding key(foundation::Key key) {
			return {.source = Binding::Source::Key, .control = static_cast<std::uint16_t>(key)};
		}

		Binding pad(foundation::GamepadButton button) {
			return {.source = Binding::Source::GamepadButton, .control = static_cast<std::uint16_t>(button)};
		}

		// Half of the stick, read as a button past half travel.
		Binding stick(foundation::GamepadAxis axis, float scale) {
			return {.source = Binding::Source::GamepadAxis, .control = static_cast<std::uint16_t>(axis), .scale = scale, .threshold = 0.5F};
		}
	} // namespace

	foundation::ActionMap defaultNavigation() {
		using foundation::GamepadAxis;
		using foundation::GamepadButton;
		using foundation::Key;
		foundation::ActionMap map;
		map.bind(actions::kActivate, key(Key::Enter));
		map.bind(actions::kActivate, key(Key::KpEnter));
		map.bind(actions::kActivate, key(Key::Space));
		map.bind(actions::kActivate, pad(GamepadButton::A));
		map.bind(actions::kCancel, key(Key::Escape));
		map.bind(actions::kCancel, pad(GamepadButton::B));
		map.bind(actions::kUp, key(Key::Up));
		map.bind(actions::kUp, pad(GamepadButton::DpadUp));
		map.bind(actions::kUp, stick(GamepadAxis::LeftY, -1.0F));
		map.bind(actions::kDown, key(Key::Down));
		map.bind(actions::kDown, pad(GamepadButton::DpadDown));
		map.bind(actions::kDown, stick(GamepadAxis::LeftY, 1.0F));
		map.bind(actions::kLeft, key(Key::Left));
		map.bind(actions::kLeft, pad(GamepadButton::DpadLeft));
		map.bind(actions::kLeft, stick(GamepadAxis::LeftX, -1.0F));
		map.bind(actions::kRight, key(Key::Right));
		map.bind(actions::kRight, pad(GamepadButton::DpadRight));
		map.bind(actions::kRight, stick(GamepadAxis::LeftX, 1.0F));
		map.bind(actions::kNext, key(Key::Tab));
		map.bind(actions::kNext, pad(GamepadButton::RightBumper));
		map.bind(actions::kPrevious, pad(GamepadButton::LeftBumper));
		return map;
	}

} // namespace ui
