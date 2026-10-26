#include "Ui.h"

#include <vector/MeshBounds.h>

namespace ui {
	int layerDepth() {
		// The call is the point: it keeps salal_ui linked against salal_renderer until real code does.
		const Foundation::Rect bounds = renderer::computeBounds(renderer::TessellatedMesh{});
		return bounds.width == 0.0F ? 2 : -1;
	}
} // namespace ui
