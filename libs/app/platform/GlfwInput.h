#pragma once

// What the GLFW platform's callbacks and gamepad poll turn into, as functions of GLFW's plain values,
// so the translation is tested without a window. No GLFW header here: the ints are GLFW's key,
// action, button and modifier codes, which GlfwInput.cpp maps with GLFW's own constants.

#include "platform/Gamepads.h"

#include <input/InputTypes.h>

#include <glm/vec2.hpp>

#include <span>

namespace app::glfw {

	// GLFW's key code as a Key; Key::Unknown for GLFW_KEY_UNKNOWN and any code GLFW doesn't define.
	foundation::Key toKey(int glfwKey);
	// A Key's GLFW code; GLFW_KEY_UNKNOWN (-1) for Key::Unknown and anything out of range.
	int toGlfwKey(foundation::Key key);

	// The keyboard's: GLFW_PRESS is a KeyDown, GLFW_REPEAT a KeyDown with repeat, GLFW_RELEASE a KeyUp.
	// mods keeps Shift, Control, Alt and Super, dropping GLFW's lock bits.
	foundation::InputEvent keyEvent(int glfwKey, int action, int mods, double time);
	// GLFW's char callback carries no mods, so the event's are 0.
	foundation::InputEvent charEvent(unsigned int codepoint, double time);

	// The mouse's, in window coordinates. GLFW's button and scroll callbacks carry no position, so the
	// platform passes the cursor's last known one; scroll carries no mods either.
	foundation::InputEvent cursorPosEvent(double x, double y, double time);
	foundation::InputEvent cursorEnterEvent(int entered, double time);
	foundation::InputEvent mouseButtonEvent(int button, int action, int mods, glm::vec2 cursor, double time);
	foundation::InputEvent scrollEvent(double x, double y, glm::vec2 cursor, double time);

	// Whether each of a pad's triggers has read below 0 since it connected. GLFW leaves an axis the
	// pad's mapping lacks at 0, which would read half pulled, and a mapped trigger rests at -1. The
	// platform keeps one per pad and clears it on disconnect.
	struct TriggersSeen {
		bool left = false;
		bool right = false;
	};

	// GLFWgamepadstate's arrays as a sample: a button is down at GLFW_PRESS, and a trigger moves from
	// GLFW's -1 to 1 onto 0 to 1 once `seen` says it has been below 0, reading 0 until then (so one
	// held as the pad connects reads 0 until it's let go).
	GamepadSample gamepadSample(std::span<const unsigned char, 15> buttons, std::span<const float, 6> axes, TriggersSeen& seen);

} // namespace app::glfw
