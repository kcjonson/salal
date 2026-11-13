#pragma once

#include "math/Types.h"

namespace Renderer::Primitives { // NOLINT(readability-identifier-naming)

	// The view an instanced or baked world draw is projected with: world meters
	// to screen pixels through position, zoom and pixelsPerMeter, centred on the
	// viewport.
	struct WorldCamera {
		Foundation::Vec2 position{0.0F, 0.0F};
		float			 zoom = 1.0F;
		float			 pixelsPerMeter = 1.0F;

		bool operator==(const WorldCamera&) const = default;
	};

} // namespace Renderer::Primitives
