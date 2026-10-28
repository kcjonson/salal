#include "ActionMap.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace foundation {

	namespace {

		template <typename Enum>
		constexpr std::size_t indexOf(Enum value) {
			return static_cast<std::size_t>(value);
		}

		bool isPad(const DeviceId& device) {
			return device.kind == DeviceKind::Gamepad && device.index < kMaxRealGamepads + kMaxVirtualGamepads;
		}

		// What's past the deadzone, rescaled to run from 0 at its edge to 1.
		float pastDeadzone(float magnitude, float deadzone) {
			const float edge = std::max(deadzone, 0.0F);
			if (magnitude <= edge || edge >= 1.0F) {
				return 0.0F;
			}
			return std::min(1.0F, (magnitude - edge) / (1.0F - edge));
		}

	} // namespace

	void ActionMap::bind(std::string_view action, Binding binding) {
		auto found = actions.find(action);
		if (found == actions.end()) {
			found = actions.emplace(std::string(action), std::vector<Binding>{}).first;
		}
		found->second.push_back(binding);
	}

	void ActionMap::setStickDeadzone(GamepadAxis x, GamepadAxis y, float radius) {
		for (Stick& stick : sticks) {
			if (stick.x == x && stick.y == y) {
				stick.radius = radius;
				return;
			}
		}
		sticks.push_back({.x = x, .y = y, .radius = radius});
	}

	ActionState ActionMap::evaluate(std::string_view action, const InputSnapshot& input, std::span<const DeviceId> devices) const {
		const auto found = actions.find(action);
		if (found == actions.end()) {
			return {};
		}

		float	 value = 0.0F;
		bool	 down = false;
		bool	 wasDown = false;
		bool	 heldThroughout = false;
		unsigned transitions = 0;
		for (const DeviceId& device : devices) {
			float deviceValue = 0.0F;
			for (const Binding& binding : found->second) {
				const std::optional<ActionState> state = read(binding, device, input);
				if (!state) {
					continue;
				}
				const ButtonState& button = state->button;
				down = down || button.down;
				wasDown = wasDown || (button.down != (button.transitions % 2 == 1));
				heldThroughout = heldThroughout || (button.down && button.transitions == 0);
				transitions += button.transitions;
				deviceValue += state->value;
			}
			deviceValue = std::clamp(deviceValue, -1.0F, 1.0F);
			if (std::abs(deviceValue) > std::abs(value)) {
				value = deviceValue;
			}
		}

		unsigned combined = 0;
		if (!heldThroughout) {
			combined = std::min(transitions, unsigned{std::numeric_limits<std::uint8_t>::max()});
			if ((combined % 2 == 1) != (wasDown != down)) {
				--combined;
			}
		}
		return {.button = {.down = down, .transitions = static_cast<std::uint8_t>(combined)}, .value = value};
	}

	std::optional<ActionState> ActionMap::read(const Binding& binding, const DeviceId& device, const InputSnapshot& input) const {
		const auto buttonContribution = [&binding](const ButtonState& button) {
			return ActionState{.button = button, .value = button.down ? binding.scale : 0.0F};
		};
		switch (binding.source) {
			case Binding::Source::Key:
				if (device.kind != DeviceKind::Keyboard || binding.control >= indexOf(Key::Count)) {
					return std::nullopt;
				}
				return buttonContribution(input.keys[binding.control]);
			case Binding::Source::MouseButton:
				if (device.kind != DeviceKind::Mouse || binding.control >= indexOf(MouseButton::Count)) {
					return std::nullopt;
				}
				return buttonContribution(input.mouse[binding.control]);
			case Binding::Source::GamepadButton:
				if (!isPad(device) || binding.control >= indexOf(GamepadButton::Count)) {
					return std::nullopt;
				}
				return buttonContribution(input.pads[device.index].buttons[binding.control]);
			case Binding::Source::GamepadAxis: {
				if (!isPad(device) || binding.control >= indexOf(GamepadAxis::Count)) {
					return std::nullopt;
				}
				const float axis = axisValue(input.pads[device.index], static_cast<GamepadAxis>(binding.control), binding.deadzone);
				const float scaled = binding.scale * axis;
				return ActionState{.button = {.down = scaled > binding.threshold}, .value = scaled};
			}
		}
		return std::nullopt;
	}

	float ActionMap::axisValue(const GamepadState& pad, GamepadAxis axis, float deadzone) const {
		float value = pad.axes[indexOf(axis)];
		for (const Stick& stick : sticks) {
			if (axis == stick.x || axis == stick.y) {
				const float magnitude = std::hypot(pad.axes[indexOf(stick.x)], pad.axes[indexOf(stick.y)]);
				value = magnitude > 0.0F ? value * pastDeadzone(magnitude, stick.radius) / magnitude : 0.0F;
				break;
			}
		}
		const float rescaled = pastDeadzone(std::abs(value), deadzone);
		return value < 0.0F ? -rescaled : rescaled;
	}

} // namespace foundation
