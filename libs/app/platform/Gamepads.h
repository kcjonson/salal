#pragma once

// Gamepads for a platform that polls them, as GLFW makes every platform do: which are connected,
// and what changed since each one's last sample, reported as events. The GLFW platform and the
// headless one share it, so the headless tests cover the diff the native platform runs.

#include "input/DeviceInfo.h"
#include "platform/Platform.h"

#include <input/InputTypes.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace app {

	// A pad's state at one sample, in salal's ranges: sticks -1 to 1, triggers 0 (released) to 1. The
	// default is a pad at rest.
	struct GamepadSample {
		std::array<bool, static_cast<std::size_t>(foundation::GamepadButton::Count)> buttons{};
		std::array<float, static_cast<std::size_t>(foundation::GamepadAxis::Count)>	 axes{};
	};

	class GamepadTracker {
	  public:
		// pad.id names the pad; one already connected, or that isn't a real pad (pads[0..15]), is
		// ignored. Its first sample is diffed from rest, so what it holds as it arrives is reported.
		void connect(const DeviceInfo& pad, double time, PlatformEvents& events);
		// Back to rest, so a button held through a reconnect is reported again. One not connected is
		// ignored.
		void disconnect(std::uint8_t index, double time, PlatformEvents& events);
		// A ButtonDown or ButtonUp for each button that changed since the pad's last sample, in button
		// order, then an Axis for each axis that changed, in axis order. Nothing for a pad not connected.
		void sample(std::uint8_t index, const GamepadSample& now, double time, PlatformEvents& events);

		bool connected(std::uint8_t index) const;

	  private:
		struct Pad {
			bool		  connected = false;
			GamepadSample last{};
		};

		std::array<Pad, foundation::kMaxRealGamepads> pads{};
	};

} // namespace app
