#pragma once

#include <input/InputTypes.h>

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

} // namespace app
