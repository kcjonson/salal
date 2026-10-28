#pragma once

#include <input/InputTypes.h>

#include <cstdint>
#include <string>

namespace app {

	// A device as Input lists it. A gamepad's name and model GUID come from the platform (or the dev
	// server, for a virtual pad) when it connects, since its InputEvent carries neither; the keyboard
	// and the mouse have no GUID. The GUID names a model, not a unit: two identical pads share one.
	struct DeviceInfo {
		foundation::DeviceId id;
		std::string			 name;
		std::string			 modelGuid;
	};

	// The one keyboard and the one mouse.
	inline constexpr foundation::DeviceId kKeyboard{.kind = foundation::DeviceKind::Keyboard};
	inline constexpr foundation::DeviceId kMouse{.kind = foundation::DeviceKind::Mouse};

	// The gamepad in pads[index].
	constexpr foundation::DeviceId padId(std::uint8_t index) {
		return {.kind = foundation::DeviceKind::Gamepad, .index = index};
	}

} // namespace app
