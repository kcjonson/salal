#include "Ui.h"
#include "Renderer.h"

namespace ui {
	int layerDepth() {
		return renderer::layerDepth() + 1;
	}
} // namespace ui
