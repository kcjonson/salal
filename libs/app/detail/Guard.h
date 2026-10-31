#pragma once

// Every call the App makes into game code (scenes, debug layers, result callbacks, the dispatch) goes
// through guard(): what it throws is logged as "<owner>::<what>" and the frame goes on, as
// worldsim's loop does for update and render.

#include <utils/Log.h>

#include <exception>
#include <utility>

namespace app::detail {

	// True when `call` returned normally.
	template <typename F>
	bool guard(const char* owner, const char* what, F&& call) {
		try {
			std::forward<F>(call)();
			return true;
		} catch (const std::exception& e) {
			LOG_ERROR(Engine, "Exception in %s::%s: %s", owner, what, e.what());
		} catch (...) {
			LOG_ERROR(Engine, "Unknown exception in %s::%s", owner, what);
		}
		return false;
	}

} // namespace app::detail
