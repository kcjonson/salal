#include "Renderer.h"
#include "Foundation.h"

namespace renderer {
	int layerDepth() {
		return foundation::layerDepth() + 1;
	}
} // namespace renderer
