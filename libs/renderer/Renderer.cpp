#include "Renderer.h"

#include <utils/Log.h>

namespace renderer {
	int layerDepth() {
		// The call is the point: it keeps salal_renderer linked against salal_foundation until real code does.
		foundation::Logger::initialize();
		return 1;
	}
} // namespace renderer
