#pragma once

// An overlay's entrance: its fade from 0 to 1 over motion.duration.fast, on motion.easing.enter,
// through the animator, so reduced motion shows it at once. Its style, opacity and all, is left alone.

#include "ui/Component.h"

namespace ui {

	[[nodiscard]] Tween<float> fadeIn(Component& component);

} // namespace ui
