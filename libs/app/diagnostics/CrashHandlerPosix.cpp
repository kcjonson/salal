#include "diagnostics/CrashHandler.h"

#include <utils/LogFile.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>

#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

namespace app::detail {

	namespace {

		// Everything the handler calls once a signal arrives is async-signal-safe: write, open, sigaction,
		// raise, and backtrace, which is primed at install so its first call doesn't load the unwinder.
		constexpr std::array<int, 5> kSignals{SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
		constexpr int				 kMaxFrames = 64;
		constexpr std::size_t		 kAltStackBytes = 64 * 1024;

		struct State {
			std::string						report;
			std::string						header;
			std::unique_ptr<char[]>			altStack; // NOLINT(cppcoreguidelines-avoid-c-arrays)
			stack_t							previousStack{};
			bool							stackSet = false;
			std::array<struct sigaction, 5> previous{};
			std::terminate_handler			previousTerminate = nullptr;
			int								reportFile = -1;
			std::array<char, 1024>			detail{}; // terminate's line, for the SIGABRT that follows
		};

		State*			  g_state = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
		std::atomic<bool> g_reporting{false}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

		void outputs(std::array<int, 3>& files) {
			if (g_state->reportFile < 0) {
				g_state->reportFile = ::open(g_state->report.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
			}
			files = {STDERR_FILENO, static_cast<int>(foundation::logFileHandle()), g_state->reportFile};
		}

		void out(const char* text) {
			std::array<int, 3> files{};
			outputs(files);
			for (int file : files) {
				if (file >= 0) {
					foundation::writeNative(file, std::string_view(text, std::strlen(text)));
				}
			}
		}

		// "0x" and sixteen hex digits, into `buffer`.
		const char* hex(std::uintptr_t value, std::array<char, 19>& buffer) {
			constexpr const char* kDigits = "0123456789abcdef";
			buffer[0] = '0';
			buffer[1] = 'x';
			for (int i = 0; i < 16; ++i) {
				buffer[17 - i] = kDigits[(value >> (4 * i)) & 0xF];
			}
			buffer[18] = '\0';
			return buffer.data();
		}

		const char* signalName(int signal) {
			switch (signal) {
				case SIGSEGV:
					return "SIGSEGV (segmentation fault)";
				case SIGBUS:
					return "SIGBUS (bus error)";
				case SIGILL:
					return "SIGILL (illegal instruction)";
				case SIGFPE:
					return "SIGFPE (arithmetic exception)";
				case SIGABRT:
					return "SIGABRT (abort)";
				default:
					return "a signal";
			}
		}

		// The default action, once this handler returns: the signal is blocked until then.
		void raiseDefault(int signal) {
			struct sigaction action{};
			action.sa_handler = SIG_DFL;
			sigemptyset(&action.sa_mask);
			sigaction(signal, &action, nullptr);
			raise(signal);
		}

		void onSignal(int signal, siginfo_t* info, void* /*context*/) {
			if (g_reporting.exchange(true)) {
				raiseDefault(signal); // a crash in the report, or on another thread meanwhile
				return;
			}
			out("\n=== Crash: ");
			out(signalName(signal));
			if (signal != SIGABRT && info != nullptr) {
				std::array<char, 19> address{};
				out(" at address ");
				out(hex(reinterpret_cast<std::uintptr_t>(info->si_addr), address));
			}
			out("\n");
			out(g_state->header.c_str());
			out("\n");
			if (g_state->detail[0] != '\0') {
				out(g_state->detail.data());
				out("\n");
			}
			out("Stack:\n");
			std::array<void*, kMaxFrames> frames{};
			const int					  count = backtrace(frames.data(), kMaxFrames);
			std::array<int, 3>			  files{};
			outputs(files);
			for (int file : files) {
				if (file >= 0) {
					backtrace_symbols_fd(frames.data(), count, file);
				}
			}
			raiseDefault(signal);
		}

		[[noreturn]] void onTerminate() {
			const std::string detail = terminateDetail();
			const std::size_t length = std::min(detail.size(), g_state->detail.size() - 1);
			std::memcpy(g_state->detail.data(), detail.data(), length);
			g_state->detail[length] = '\0';
			std::abort(); // the SIGABRT report carries the line
		}

	} // namespace

	bool installCrashHandler(const CrashFiles& files) {
		auto* state = new State; // NOLINT(cppcoreguidelines-owning-memory)
		state->report = files.report.string();
		state->header = files.header;
		g_state = state;

		std::array<void*, 1> prime{};
		backtrace(prime.data(), 1);

		const std::size_t size = std::max(static_cast<std::size_t>(SIGSTKSZ), kAltStackBytes);
		state->altStack = std::make_unique<char[]>(size); // NOLINT(cppcoreguidelines-avoid-c-arrays)
		stack_t stack{};
		stack.ss_sp = state->altStack.get();
		stack.ss_size = size;
		state->stackSet = sigaltstack(&stack, &state->previousStack) == 0;

		struct sigaction action{};
		action.sa_sigaction = onSignal;
		action.sa_flags = SA_SIGINFO | SA_ONSTACK;
		sigemptyset(&action.sa_mask);
		for (std::size_t i = 0; i < kSignals.size(); ++i) {
			sigaction(kSignals[i], &action, &state->previous[i]);
		}
		state->previousTerminate = std::set_terminate(onTerminate);
		return true;
	}

	void uninstallCrashHandler() {
		State* state = g_state;
		if (state == nullptr) {
			return;
		}
		std::set_terminate(state->previousTerminate);
		for (std::size_t i = 0; i < kSignals.size(); ++i) {
			sigaction(kSignals[i], &state->previous[i], nullptr);
		}
		if (state->stackSet) {
			sigaltstack(&state->previousStack, nullptr);
		}
		if (state->reportFile >= 0) {
			::close(state->reportFile);
		}
		g_state = nullptr;
		g_reporting.store(false);
		delete state; // NOLINT(cppcoreguidelines-owning-memory)
	}

	bool crashHandlerInPlace() {
		for (int signal : kSignals) {
			struct sigaction current{};
			if (sigaction(signal, nullptr, &current) != 0 || (current.sa_flags & SA_SIGINFO) == 0 || current.sa_sigaction != onSignal) {
				return false;
			}
		}
		return std::get_terminate() == onTerminate;
	}

} // namespace app::detail
