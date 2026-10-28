#pragma once

// The frame's input: what dispatch, update and the ui read. A fixed tick reads its own snapshot,
// from TickContext. The App feeds both from one InputAccumulator, which the platform's events go
// into as they arrive.

#include <input/InputAccumulator.h>
#include <input/InputTypes.h>

#include <span>

namespace app {

	class App;

	class Input {
	  public:
		// This frame's state.
		const foundation::InputSnapshot& frame() const { return frameState; }
		// This frame's events, in order.
		std::span<const foundation::InputEvent> events() const { return accumulator.frameEvents(); }

	  private:
		friend class App;

		foundation::InputAccumulator accumulator;
		foundation::InputSnapshot	 frameState{};
	};

} // namespace app
