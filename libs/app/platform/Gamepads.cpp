#include "platform/Gamepads.h"

namespace app {

	namespace {
		constexpr foundation::DeviceId padId(std::uint8_t index) {
			return {.kind = foundation::DeviceKind::Gamepad, .index = index};
		}
	} // namespace

	void GamepadTracker::connect(const DeviceInfo& pad, double time, PlatformEvents& events) {
		if (pad.id.kind != foundation::DeviceKind::Gamepad || pad.id.index >= pads.size() || pads[pad.id.index].connected) {
			return;
		}
		pads[pad.id.index].connected = true;
		events.deviceConnected(pad, time);
	}

	void GamepadTracker::disconnect(std::uint8_t index, double time, PlatformEvents& events) {
		if (!connected(index)) {
			return;
		}
		pads[index] = {};
		events.deviceDisconnected(padId(index), time);
	}

	void GamepadTracker::sample(std::uint8_t index, const GamepadSample& now, double time, PlatformEvents& events) {
		if (!connected(index)) {
			return;
		}
		GamepadSample& last = pads[index].last;
		for (std::size_t button = 0; button < now.buttons.size(); ++button) {
			if (now.buttons[button] != last.buttons[button]) {
				events.inputEvent(
					{.type = now.buttons[button] ? foundation::InputEvent::Type::ButtonDown : foundation::InputEvent::Type::ButtonUp,
					 .device = padId(index),
					 .time = time,
					 .button = static_cast<foundation::GamepadButton>(button)}
				);
			}
		}
		for (std::size_t axis = 0; axis < now.axes.size(); ++axis) {
			if (now.axes[axis] != last.axes[axis]) {
				events.inputEvent(
					{.type = foundation::InputEvent::Type::Axis,
					 .device = padId(index),
					 .time = time,
					 .axis = static_cast<foundation::GamepadAxis>(axis),
					 .value = now.axes[axis]}
				);
			}
		}
		last = now;
	}

	bool GamepadTracker::connected(std::uint8_t index) const {
		return index < pads.size() && pads[index].connected;
	}

} // namespace app
