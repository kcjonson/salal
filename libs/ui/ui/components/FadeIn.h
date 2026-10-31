#pragma once

// An overlay's entrance: its opacity from 0 to its declared one over motion.duration.fast, on
// motion.easing.enter, through the animator, so reduced motion shows it at once.

#include "ui/Component.h"

namespace ui {

	[[nodiscard]] Tween<float> fadeIn(Component& component);

} // namespace ui
