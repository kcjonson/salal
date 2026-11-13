#pragma once

// A mask a scene, its ui or a debug layer left set ends with its render, so nothing drawn after it,
// the ui over a world pass above all, is masked by it (docs/masks.md).

#include <primitives/Primitives.h>
#include <utils/Log.h>

namespace app::detail {

	inline void endMask(const char* owner, const char* what) {
		if (Renderer::Primitives::getMask().mode == Renderer::Primitives::MaskMode::Off) {
			return;
		}
		static bool warned = false;
		if (!warned) {
			warned = true;
			LOG_WARNING(Engine, "%s::%s left a mask set; turned off (warned once)", owner, what);
		}
		Renderer::Primitives::setMask({});
	}

} // namespace app::detail
