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

		// What's past the deadzone, rescaled to run from 0 at its edge to 1. A NaN anywhere reads 0.
		float pastDeadzone(float magnitude, float deadzone) {
			const float edge = std::max(deadzone, 0.0F);
			if (!(magnitude > edge) || edge >= 1.0F) {
				return 0.0F;
			}
			return std::min(1.0F, (magnitude - edge) / (1.0F - edge));
		}

		// Folds the bindings' button states into the action's, which is down while any binding is. The
		// snapshot doesn't say in what order the bindings changed, so the transitions reported are the
		// most some order of those changes gives. Only the last of the bindings that start down to let go
		// can release the action, and only the first of those that end down to make its last press can
		// press it, so each further one hides a transition.
		class ButtonFold {
		  public:
			void add(const ButtonState& button) {
				down = down || button.down;
				if (button.transitions == 0) {
					heldThroughout = heldThroughout || button.down;
					return;
				}
				transitions += button.transitions;
				startDown += button.down != (button.transitions % 2 == 1) ? 1U : 0U;
				endDown += button.down ? 1U : 0U;
			}

			ButtonState result() const {
				constexpr unsigned kMax = std::numeric_limits<std::uint8_t>::max();
				unsigned		   count = 0;
				if (!heldThroughout && transitions > 0) {
					count = transitions - (startDown > 1 ? startDown - 1 : 0) - (endDown > 1 ? endDown - 1 : 0);
				}
				// Saturating keeps the count's parity, which says whether the action's down state changed.
				if (count > kMax) {
					count = count % 2 == kMax % 2 ? kMax : kMax - 1;
				}
				return {.down = down, .transitions = static_cast<std::uint8_t>(count)};
			}

		  private:
			bool	 down = false;
			bool	 heldThroughout = false;
			unsigned transitions = 0; // of the bindings that moved
			unsigned startDown = 0;	  // bindings that moved and started down
			unsigned endDown = 0;	  // bindings that moved and ended down
		};

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
			if ((stick.x == x && stick.y == y) || (stick.x == y && stick.y == x)) {
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

		float	   value = 0.0F;
		ButtonFold button;
		for (const DeviceId& device : devices) {
			float deviceValue = 0.0F;
			for (const Binding& binding : found->second) {
				const std::optional<ActionState> state = read(binding, device, input);
				if (!state) {
					continue;
				}
				deviceValue += state->value;
				button.add(state->button);
			}
			deviceValue = std::clamp(deviceValue, -1.0F, 1.0F);
			if (std::abs(deviceValue) > std::abs(value)) {
				value = deviceValue;
			}
		}
		return {.button = button.result(), .value = value};
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
				if (!isPadSlot(device) || binding.control >= indexOf(GamepadButton::Count)) {
					return std::nullopt;
				}
				return buttonContribution(input.pads[device.index].buttons[binding.control]);
			case Binding::Source::GamepadAxis: {
				if (!isPadSlot(device) || binding.control >= indexOf(GamepadAxis::Count)) {
					return std::nullopt;
				}
				const GamepadState& pad = input.pads[device.index];
				const auto			axis = static_cast<GamepadAxis>(binding.control);
				const float			scaled = binding.scale * axisValue(pad.axes, axis, binding.deadzone);
				const bool			down = scaled > binding.threshold;
				const bool			wasDown = binding.scale * axisValue(pad.startAxes, axis, binding.deadzone) > binding.threshold;
				return ActionState{.button = {.down = down, .transitions = std::uint8_t{down != wasDown}}, .value = scaled};
			}
		}
		return std::nullopt;
	}

	float ActionMap::axisValue(const Axes& axes, GamepadAxis axis, float deadzone) const {
		float value = axes[indexOf(axis)];
		for (const Stick& stick : sticks) {
			if (axis == stick.x || axis == stick.y) {
				const float x = axes[indexOf(stick.x)];
				const float y = axes[indexOf(stick.y)];
				// Not std::hypot, which isn't correctly rounded and differs between standard libraries.
				const float magnitude = std::sqrt(x * x + y * y);
				value = magnitude > 0.0F ? value * pastDeadzone(magnitude, stick.radius) / magnitude : 0.0F;
				break;
			}
		}
		const float rescaled = pastDeadzone(std::abs(value), deadzone);
		return value < 0.0F ? -rescaled : rescaled;
	}

} // namespace foundation
