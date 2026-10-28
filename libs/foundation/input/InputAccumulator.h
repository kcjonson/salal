#pragma once

// Folds input events into state for two readers, the fixed tick and the frame. Each reader has
// its own transition counts, pointer delta and scroll, so consuming one leaves the other alone.
//
// What push does with what it's given:
// - Transitions count changes of state: a repeat KeyDown, a down for a key already down and an up
//   for one already up change nothing.
// - FocusLost releases every held key and mouse button, since the platform may never send their
//   ups. Pads are polled, not focus-bound, and keep their state.
// - DeviceDisconnected releases the pad's held buttons and zeroes its axes; button and axis
//   events for a pad that isn't connected change nothing.
// - mods is rebuilt from the held modifier keys, not copied from the event, so it agrees with keys.
// - The first known pointer position adds no delta; after that every position-carrying event adds
//   its move to pointerDelta. Axis values clamp to [-1, 1].
// - An event naming Key::Unknown or a Count value, a button or axis event whose device isn't a
//   gamepad in pads[], or a connection event for a gamepad past pads[] is dropped, from state and
//   from frameEvents. Everything else is recorded, Char included. Key and pointer events go to the
//   one keyboard and mouse whatever their device says.

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
