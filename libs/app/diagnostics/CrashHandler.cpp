#include "diagnostics/CrashHandler.h"

#include <utils/Log.h>

#include <cstdlib>
#include <exception>
#include <stdexcept>

namespace app {

	namespace {

		bool g_installed = false; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

		[[noreturn]] void throwOnPurpose() {
			throw std::runtime_error("thrown on purpose");
		}

		// A call, not a throw, which compilers warn about in a noexcept function.
		void throwThroughNoexcept() noexcept {
			throwOnPurpose();
		}

		volatile bool g_recurse = true; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

		// Each frame keeps a page live, so it can't become a loop, and the volatile flag hides that it never stops.
		int recurse(int depth) {	   // NOLINT(misc-no-recursion)
			volatile char frame[4096]; // NOLINT(cppcoreguidelines-avoid-c-arrays)
			frame[0] = static_cast<char>(depth);
			return g_recurse ? recurse(depth + 1) + frame[0] : frame[0];
		}

		int* volatile g_nowhere = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

	} // namespace

	std::unique_ptr<CrashHandler> CrashHandler::install(const CrashFiles& files) {
		if (g_installed) {
			LOG_ERROR(Engine, "CrashHandler::install: one is installed already");
			return nullptr;
		}
		if (!detail::installCrashHandler(files)) {
			LOG_ERROR(Engine, "CrashHandler::install: the handlers couldn't be installed");
			return nullptr;
		}
		g_installed = true;
		return std::unique_ptr<CrashHandler>(new CrashHandler);
	}

	CrashHandler::~CrashHandler() {
		detail::uninstallCrashHandler();
		g_installed = false;
	}

	bool crashHandlerInstalled() {
		return g_installed && detail::crashHandlerInPlace();
	}

	bool crashOnPurpose(std::string_view kind) {
		if (kind == "segv") {
			*g_nowhere = 1;
		} else if (kind == "abort") {
			std::abort();
		} else if (kind == "throw") {
			throwThroughNoexcept();
		} else if (kind == "overflow") {
			recurse(0);
		} else {
			return false;
		}
		return true;
	}

	namespace detail {
		std::string terminateDetail() {
			const std::exception_ptr exception = std::current_exception();
			if (!exception) {
				return "std::terminate with no exception in flight";
			}
			try {
				std::rethrow_exception(exception);
			} catch (const std::exception& e) {
				return std::string("uncaught exception: ") + e.what();
			} catch (...) {
				return "uncaught exception of a type not derived from std::exception";
			}
		}
	} // namespace detail

} // namespace app
