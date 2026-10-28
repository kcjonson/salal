#pragma once

// Actions. A binding names a device kind and a control; a map holds bindings per action and is
// evaluated against a snapshot for one set of devices, so split keyboard is two maps over the
// keyboard and co-op is one map per claimed pad.
//
// How evaluate combines bindings:
// - A key, mouse button or pad button contributes scale while down. An axis contributes its value
//   after the stick's radial deadzone, then the binding's own deadzone, times scale; both
//   deadzones rescale what's left to start from 0, and its button is down while that value is
//   past threshold, so scale = -1 reads the negative half.
// - Values sum within a device (two keys to one axis cancel), clamped to [-1, 1]; across devices
//   the largest magnitude wins, the first on a tie, so two pads pushing together don't double.
// - The button is down while any binding is. Its transitions are the bindings' summed, one fewer
//   when that disagrees with how the action's own down state changed, and none while one binding
//   was held throughout. An axis carries no transitions: a snapshot has no history of it.
// - An unbound action, an empty device span, or a control out of range contributes nothing.

#include "InputTypes.h"

#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace foundation {

	struct Binding {
		enum class Source : std::uint8_t { Key, MouseButton, GamepadButton, GamepadAxis };
		Source		  source = Source::Key;
		std::uint16_t control = 0;		// the enum value
		float		  scale = 1.0F;		// -1 makes a key the negative half of an axis (two keys to one axis)
		float		  threshold = 0.5F; // an axis read as a button: down while its value is past this
		float		  deadzone = 0.0F;	// per axis; sticks take a radial deadzone over both axes in the map
	};

	// The button's transitions follow the reader the snapshot came from.
	struct ActionState {
		ButtonState button;
		float		value = 0.0F;
	};

	class ActionMap {
	  public:
		void bind(std::string_view action, Binding binding);
		// Replaces the radius for a pair already set.
		void		setStickDeadzone(GamepadAxis x, GamepadAxis y, float radius);
		ActionState evaluate(std::string_view action, const InputSnapshot& input, std::span<const DeviceId> devices) const;

	  private:
		struct Stick {
			GamepadAxis x = GamepadAxis::LeftX;
			GamepadAxis y = GamepadAxis::LeftY;
			float		radius = 0.0F;
		};

		// One binding on one device; nullopt when the binding doesn't read that device.
		std::optional<ActionState> read(const Binding& binding, const DeviceId& device, const InputSnapshot& input) const;
		float					   axisValue(const GamepadState& pad, GamepadAxis axis, float deadzone) const;

		std::map<std::string, std::vector<Binding>, std::less<>> actions{};
		std::vector<Stick>										 sticks{};
	};

} // namespace foundation
