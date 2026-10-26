#include "App.h"
#include "Ui.h"

namespace app {
	int layerDepth() {
		return ui::layerDepth() + 1;
	}
} // namespace app
