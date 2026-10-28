#include "InputAccumulator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace foundation {

	namespace {

		template <typename Enum>
		constexpr std::size_t indexOf(Enum value) {
			return static_cast<std::size_t>(value);
		}

		// state is either reader; the two always agree on which pads are connected.
		bool accepted(const InputEvent& event, const InputSnapshot& state) {
			using Type = InputEvent::Type;
			const auto connectedPad = [&] {
				return isPadSlot(event.device) && state.pads[event.device.index].connected;
			};
			switch (event.type) {
				case Type::KeyDown:
				case Type::KeyUp:
					return event.key != Key::Unknown && indexOf(event.key) < indexOf(Key::Count);
				case Type::PointerDown:
				case Type::PointerUp:
					return indexOf(event.mouseButton) < indexOf(MouseButton::Count);
				case Type::ButtonDown:
				case Type::ButtonUp:
					return connectedPad() && indexOf(event.button) < indexOf(GamepadButton::Count);
				case Type::Axis:
					return connectedPad() && indexOf(event.axis) < indexOf(GamepadAxis::Count) && std::isfinite(event.value);
				case Type::DeviceConnected:
				case Type::DeviceDisconnected:
					return event.device.kind != DeviceKind::Gamepad || isPadSlot(event.device);
				case Type::Char:
				case Type::PointerMove:
				case Type::Scroll:
				case Type::PointerEnter:
				case Type::PointerLeave:
				case Type::FocusLost:
				case Type::FocusGained:
					return true;
			}
			return false;
		}

		bool carriesPosition(InputEvent::Type type) {
			using Type = InputEvent::Type;
			return type == Type::PointerMove || type == Type::PointerDown || type == Type::PointerUp || type == Type::Scroll;
		}

		void setDown(ButtonState& state, bool down) {
			if (state.down == down) {
				return;
			}
			state.down = down;
			if (state.transitions < std::numeric_limits<std::uint8_t>::max()) {
				++state.transitions;
			}
		}

		void releaseAll(std::span<ButtonState> states) {
			for (ButtonState& state : states) {
				setDown(state, false);
			}
		}

		std::uint8_t heldMods(const InputSnapshot& snapshot) {
			const auto held = [&snapshot](Key left, Key right) {
				return snapshot.keys[indexOf(left)].down || snapshot.keys[indexOf(right)].down;
			};
			std::uint8_t mods = 0;
			if (held(Key::LeftShift, Key::RightShift)) {
				mods |= ModShift;
			}
			if (held(Key::LeftControl, Key::RightControl)) {
				mods |= ModControl;
			}
			if (held(Key::LeftAlt, Key::RightAlt)) {
				mods |= ModAlt;
			}
			if (held(Key::LeftSuper, Key::RightSuper)) {
				mods |= ModSuper;
			}
			return mods;
		}

		void apply(InputSnapshot& reader, const InputEvent& event, bool pointerKnown) {
			using Type = InputEvent::Type;
			if (carriesPosition(event.type)) {
				if (pointerKnown) {
					reader.pointerDelta += event.position - reader.pointer;
				}
				reader.pointer = event.position;
			}

			switch (event.type) {
				case Type::KeyDown:
					if (!event.repeat) {
						setDown(reader.keys[indexOf(event.key)], true);
					}
					reader.mods = heldMods(reader);
					break;
				case Type::KeyUp:
					setDown(reader.keys[indexOf(event.key)], false);
					reader.mods = heldMods(reader);
					break;
				case Type::PointerDown:
					setDown(reader.mouse[indexOf(event.mouseButton)], true);
					break;
				case Type::PointerUp:
					setDown(reader.mouse[indexOf(event.mouseButton)], false);
					break;
				case Type::Scroll:
					reader.scroll += event.scroll;
					break;
				case Type::PointerEnter:
					reader.pointerInWindow = true;
					break;
				case Type::PointerLeave:
					reader.pointerInWindow = false;
					break;
				case Type::ButtonDown:
				case Type::ButtonUp:
					setDown(reader.pads[event.device.index].buttons[indexOf(event.button)], event.type == Type::ButtonDown);
					break;
				case Type::Axis:
					reader.pads[event.device.index].axes[indexOf(event.axis)] = std::clamp(event.value, -1.0F, 1.0F);
					break;
				case Type::DeviceConnected:
					if (isPadSlot(event.device)) {
						reader.pads[event.device.index].connected = true;
					}
					break;
				case Type::DeviceDisconnected:
					if (isPadSlot(event.device)) {
						GamepadState& pad = reader.pads[event.device.index];
						releaseAll(pad.buttons);
						pad.axes.fill(0.0F);
						pad.connected = false;
					}
					break;
				case Type::FocusLost:
					releaseAll(reader.keys);
					releaseAll(reader.mouse);
					reader.mods = 0;
					break;
				case Type::Char:
				case Type::PointerMove:
				case Type::FocusGained:
					break;
			}
		}

		void startNextRead(InputSnapshot& reader) {
			for (ButtonState& state : reader.keys) {
				state.transitions = 0;
			}
			for (ButtonState& state : reader.mouse) {
				state.transitions = 0;
			}
			for (GamepadState& pad : reader.pads) {
				for (ButtonState& state : pad.buttons) {
					state.transitions = 0;
				}
				pad.startAxes = pad.axes;
			}
			reader.pointerDelta = {};
			reader.scroll = {};
		}

	} // namespace

	void InputAccumulator::push(const InputEvent& event) {
		if (!accepted(event, tickReader)) {
			return;
		}
		apply(tickReader, event, pointerKnown);
		apply(frameReader, event, pointerKnown);
		pointerKnown = pointerKnown || carriesPosition(event.type);
		pendingEvents.push_back(event);
	}

	InputSnapshot InputAccumulator::consumeTick() {
		InputSnapshot snapshot = tickReader;
		startNextRead(tickReader);
		return snapshot;
	}

	InputSnapshot InputAccumulator::consumeFrame() {
		InputSnapshot snapshot = frameReader;
		startNextRead(frameReader);
		consumedEvents.swap(pendingEvents);
		pendingEvents.clear();
		return snapshot;
	}

} // namespace foundation
