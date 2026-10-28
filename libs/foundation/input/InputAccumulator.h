#pragma once

// Folds input events into state for two readers, the fixed tick and the frame. Each reader has
// its own transition counts, pointer delta and scroll, so consuming one leaves the other alone.
//
// What push does with what it's given:
// - Transitions count changes of state: a repeat KeyDown, a down for a key already down and an up
//   for one already up change nothing.
// - FocusLost releases every held key and mouse button, since the platform may never send their
//   ups. Pads are polled, not focus-bound, and keep their state.
// - DeviceDisconnected releases the pad's held buttons and zeroes its axes.
// - mods is rebuilt from the held modifier keys, not copied from the event, so it agrees with keys
//   and misses a modifier held since before the window had focus; ui that needs the modifiers at
//   the moment of an event reads event.mods.
// - The first known pointer position adds no delta; after that every position-carrying event adds
//   its move to pointerDelta. Axis values clamp to [-1, 1].
// - Dropped, from state and from frameEvents: an event naming Key::Unknown or a Count value, a
//   non-finite axis value, a button or axis event for a pad that isn't a connected gamepad in
//   pads[], and a connection event for a gamepad past pads[]. Everything else is recorded, Char
//   included. Key and pointer events go to the one keyboard and mouse whatever their device says.
//
// What that asks of the producers:
// - Pad input before DeviceConnected is dropped, so the device layer sends DeviceConnected for pads
//   present at startup (GLFW has no callback for them), the dev server sends one before a virtual
//   pad's first input, and the device layer resets its poll diff to all-released on disconnect so
//   a button held through a reconnect sends its ButtonDown.
// - pendingEvents is cleared only by consumeFrame, which the app calls every frame, paused or not.

#include "InputTypes.h"

#include <span>
#include <vector>

namespace foundation {

	class InputAccumulator {
	  public:
		void push(const InputEvent& event);

		// What a fixed tick reads.
		InputSnapshot consumeTick();
		// What dispatch, update and the ui read. Also moves the events pushed since the last
		// consumeFrame into frameEvents.
		InputSnapshot consumeFrame();
		// The events the last consumeFrame covered, in push order, until the next consumeFrame.
		std::span<const InputEvent> frameEvents() const { return consumedEvents; }

	  private:
		InputSnapshot			tickReader{};
		InputSnapshot			frameReader{};
		bool					pointerKnown = false;
		std::vector<InputEvent> pendingEvents{};
		std::vector<InputEvent> consumedEvents{};
	};

} // namespace foundation
