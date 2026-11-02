#pragma once

// Reports a crash and lets it end the process. A fault, an abort, a pure virtual call, a CRT function
// given an invalid parameter, or std::terminate (an uncaught exception, with its what()) writes a report,
// what happened then the stack, to stderr, the log file and the report file, with a minidump beside it on
// Windows. The process then ends as it would have without it: on Windows with the exception's code (3,
// abort's, for the rest), elsewhere through the signal's default action. An attached debugger gets the
// crash first.
//
// Windows: an unhandled-exception filter, SIGABRT with abort's own fast fail turned off, and the
// pure-call and invalid-parameter handlers. A thread started at install writes the dump and walks the
// stack with DbgHelp while the crashed thread waits, so a stack overflow, with no stack left to do that
// on, is reported too. A C++ exception nothing caught is dumped where it was thrown, then handed back to
// the CRT, whose terminate adds its what(). macOS and Linux: sigaction for SIGSEGV, SIGBUS, SIGILL,
// SIGFPE and SIGABRT on an alternate signal stack, the backtrace written with async-signal-safe calls;
// frame names where dladdr finds them, module and offset otherwise (addr2line or atos turns those into
// lines).
//
// What it can't report: a fast fail (a /GS cookie, __fastfail) ends the process before any handler in it
// runs, so only Windows Error Reporting sees one. Only the installing thread has the alternate stack, so
// a stack overflow on another thread on POSIX ends unreported; macOS's own crash report still has it.

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace app {

	struct CrashFiles {
		std::filesystem::path report; // written when a crash happens
		std::filesystem::path dump;	  // Windows: the minidump
		std::string			  header; // the run, the report's second line
	};

	class CrashHandler {
	  public:
		// One at a time: null, logged, while another is installed.
		static std::unique_ptr<CrashHandler> install(const CrashFiles& files);
		// Puts back the handlers it replaced.
		~CrashHandler();

		CrashHandler(const CrashHandler&) = delete;
		CrashHandler& operator=(const CrashHandler&) = delete;
		CrashHandler(CrashHandler&&) = delete;
		CrashHandler& operator=(CrashHandler&&) = delete;

	  private:
		CrashHandler() = default;
	};

	// Whether every handler the crash handler sets is the one in place now.
	bool crashHandlerInstalled();

	// Crashes on purpose, to check a crash handler: "segv" writes through a null pointer, "abort" aborts,
	// "throw" lets an exception escape a noexcept function, "overflow" recurses until the stack runs out.
	// False for any other kind; for these it doesn't return.
	bool crashOnPurpose(std::string_view kind);

	namespace detail {
		// Each platform's half (CrashHandlerWindows.cpp, CrashHandlerPosix.cpp).
		bool installCrashHandler(const CrashFiles& files);
		void uninstallCrashHandler();
		bool crashHandlerInPlace();
		// The line std::terminate reports: the exception in flight, if any, and its what().
		std::string terminateDetail();
	} // namespace detail

} // namespace app
