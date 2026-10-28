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
// - An axis binding's button went down or came up, once, when its value read through startAxes and
//   through axes sit on either side of threshold: the snapshot holds where the axis was at the
//   reader's last read and where it is now, not the path between.
// - The button is down while any binding is. Its transitions are the most some order of the
//   bindings' own changes would give: none while a binding was held throughout; otherwise their
//   sum, less one for each binding past the first that moved and started down, and one for each
//   past the first that moved and ended down. A lone binding passes through exactly.
// - An unbound action, an empty device span, or a control out of range contributes nothing, and a
//   NaN deadzone reads 0.

#include "InputTypes.h"

#include <array>
#include <cstddef>
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
		float		  threshold = 0.5F; // an axis read as a button: down while value * scale is past this, so -1 reads the negative half
		float		  deadzone = 0.0F;	// per axis; sticks take a radial deadzone over both axes in the map
	};

	// A radial deadzone over a stick's two axes.
	struct StickDeadzone {
		GamepadAxis x = GamepadAxis::LeftX;
		GamepadAxis y = GamepadAxis::LeftY;
		float		radius = 0.0F;
	};

	// The button's transitions follow the reader the snapshot came from, axis bindings' included.
	struct ActionState {
		ButtonState button;
		float		value = 0.0F;
	};

	class ActionMap {
	  public:
		void bind(std::string_view action, Binding binding);
		// Removes the action's bindings. It stays listed, with none, so a map written out (to the
		// settings, say) records that the player cleared it.
		void unbind(std::string_view action);
		// Replaces the radius for a pair already set, in either order.
		void		setStickDeadzone(GamepadAxis x, GamepadAxis y, float radius);
		ActionState evaluate(std::string_view action, const InputSnapshot& input, std::span<const DeviceId> devices) const;

		// What the map holds, for writing it out and reading it back. The actions bound, and unbound
		// since, in name order, as views that last while the map does and isn't assigned to.
		std::vector<std::string_view> actions() const;
		// The action's bindings in the order bound; none for an action never bound.
		std::span<const Binding> bindings(std::string_view action) const;
		// In the order first set.
		std::span<const StickDeadzone> stickDeadzones() const { return sticks; }

	  private:
		// A pad's axes or its startAxes.
		using Axes = std::array<float, static_cast<std::size_t>(GamepadAxis::Count)>;

		// One binding on one device; nullopt when the binding doesn't read that device.
		std::optional<ActionState> read(const Binding& binding, const DeviceId& device, const InputSnapshot& input) const;
		// One of the axes, through the deadzones.
		float axisValue(const Axes& axes, GamepadAxis axis, float deadzone) const;

		std::map<std::string, std::vector<Binding>, std::less<>> byAction{};
		std::vector<StickDeadzone>								 sticks{};
	};

} // namespace foundation
