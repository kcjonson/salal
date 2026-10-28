#pragma once

// An ActionMap's bindings through a settings section and back, so what a rebinding screen chose
// outlasts the game. foundation has no JSON; this is app's half. In the section:
//
//   "actions": { "jump": [ {"key": "Space", "scale": 1, "threshold": 0.5, "deadzone": 0}, {"button": "A", ...} ], ... }
//   "sticks":  [ {"x": "LeftX", "y": "LeftY", "radius": 0.2}, ... ]
//
// A binding names its control under its source's field, "key", "mouse", "button" (a pad's) or "axis",
// by controlName. Scale, threshold and deadzone are always written, so a later change to Binding's
// defaults doesn't change what a saved binding means.

#include <input/ActionMap.h>

#include <nlohmann/json.hpp>

namespace app {

	// Writes each of the map's actions over its entry in "actions" (an action the player cleared, which
	// unbind leaves listed, as []) and each stick deadzone over the entry for its pair in "sticks".
	// Entries the map doesn't have (a newer build's actions) and the section's other keys are kept. A
	// "actions" or "sticks" of the wrong type is replaced, logged; a section that isn't an object
	// becomes one.
	void writeBindings(const foundation::ActionMap& map, nlohmann::json& section);

	// Each action in "actions" replaces the map's bindings for it ([] clears it), and each stick in
	// "sticks" sets its deadzone. Actions the section doesn't name keep the map's bindings, so a build
	// that adds an action gets its default. What this build can't read is skipped, logged: a binding with
	// no known control or a field of the wrong type (the rest of its action still binds), an action
	// whose entry isn't a list (it keeps the map's bindings), a malformed stick.
	void readBindings(const nlohmann::json& section, foundation::ActionMap& map);

} // namespace app
