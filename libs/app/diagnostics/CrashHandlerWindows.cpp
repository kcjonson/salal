#include "diagnostics/CrashHandler.h"

#include <utils/LogFile.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dbghelp.h> // needs windows.h first

namespace app::detail {

	namespace {

		constexpr DWORD kCxxException = 0xE06D7363;	  // what MSVC's throw raises
		constexpr DWORD kFatalAppExit = 0x40000015;	  // STATUS_FATAL_APP_EXIT, the dump's code for an abort
		constexpr DWORD kHeapCorruption = 0xC0000374; // STATUS_HEAP_CORRUPTION
		constexpr UINT	kAbortExitCode = 3;			  // abort()'s
		constexpr DWORD kReportWaitMs = 60 * 1000;
		constexpr int	kMaxFrames = 64;

		struct State {
			std::wstring				 report;
			std::wstring				 dump;
			std::string					 dumpText;
			std::string					 header;
			LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
			_crt_signal_t				 previousAbort = SIG_DFL;
			unsigned int				 previousAbortBehavior = 0;
			_purecall_handler			 previousPurecall = nullptr;
			_invalid_parameter_handler	 previousInvalidParameter = nullptr;
			std::terminate_handler		 previousTerminate = nullptr;
			HANDLE						 requested = nullptr; // the crashed thread to the writer
			HANDLE						 done = nullptr;	  // and back
			HANDLE						 writer = nullptr;
			DWORD						 writerId = 0;
			std::atomic<bool>			 stopping{false};
			HANDLE						 reportFile = INVALID_HANDLE_VALUE;
			// The crash being reported, set before the writer is woken.
			EXCEPTION_POINTERS* pointers = nullptr;
			DWORD				crashedThread = 0;
			const char*			what = "";
			std::string			detail;
		};

		State*			   g_state = nullptr;	 // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
		std::atomic<DWORD> g_reportingThread{0}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

		// stderr, the log file and the report.
		void out(std::string_view text) {
			foundation::writeNative(reinterpret_cast<foundation::NativeFile>(GetStdHandle(STD_ERROR_HANDLE)), text);
			foundation::writeNative(foundation::logFileHandle(), text);
			if (g_state->reportFile == INVALID_HANDLE_VALUE) {
				g_state->reportFile = CreateFileW(
					g_state->report.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
				);
			}
			if (g_state->reportFile != INVALID_HANDLE_VALUE) {
				foundation::writeNative(reinterpret_cast<foundation::NativeFile>(g_state->reportFile), text);
			}
		}

		void outf(const char* format, ...) {
			char	text[1024];
			va_list args;
			va_start(args, format);
			const int length = std::vsnprintf(text, sizeof(text), format, args);
			va_end(args);
			if (length > 0) {
				out(std::string_view(text, std::min(static_cast<std::size_t>(length), sizeof(text) - 1)));
			}
		}

		const char* exceptionName(DWORD code) {
			switch (code) {
				case EXCEPTION_ACCESS_VIOLATION:
					return "EXCEPTION_ACCESS_VIOLATION";
				case EXCEPTION_STACK_OVERFLOW:
					return "EXCEPTION_STACK_OVERFLOW";
				case EXCEPTION_ILLEGAL_INSTRUCTION:
					return "EXCEPTION_ILLEGAL_INSTRUCTION";
				case EXCEPTION_PRIV_INSTRUCTION:
					return "EXCEPTION_PRIV_INSTRUCTION";
				case EXCEPTION_INT_DIVIDE_BY_ZERO:
					return "EXCEPTION_INT_DIVIDE_BY_ZERO";
				case EXCEPTION_INT_OVERFLOW:
					return "EXCEPTION_INT_OVERFLOW";
				case EXCEPTION_FLT_DIVIDE_BY_ZERO:
					return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
				case EXCEPTION_FLT_INVALID_OPERATION:
					return "EXCEPTION_FLT_INVALID_OPERATION";
				case EXCEPTION_DATATYPE_MISALIGNMENT:
					return "EXCEPTION_DATATYPE_MISALIGNMENT";
				case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
					return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
				case EXCEPTION_IN_PAGE_ERROR:
					return "EXCEPTION_IN_PAGE_ERROR";
				case EXCEPTION_BREAKPOINT:
					return "EXCEPTION_BREAKPOINT";
				case EXCEPTION_NONCONTINUABLE_EXCEPTION:
					return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
				case kHeapCorruption:
					return "STATUS_HEAP_CORRUPTION";
				case kCxxException:
					return "an uncaught C++ exception";
				default:
					return "an unhandled exception";
			}
		}

		void writeDump() {
			HANDLE file = CreateFileW(g_state->dump.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) {
				outf("Minidump: %s can't be created (error %lu)\n", g_state->dumpText.c_str(), GetLastError());
				return;
			}
			MINIDUMP_EXCEPTION_INFORMATION exception{
				.ThreadId = g_state->crashedThread, .ExceptionPointers = g_state->pointers, .ClientPointers = FALSE
			};
			const auto type =
				static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
			const BOOL	written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &exception, nullptr, nullptr);
			const DWORD error = GetLastError();
			CloseHandle(file);
			if (written != FALSE) {
				outf("Minidump: %s\n", g_state->dumpText.c_str());
			} else {
				DeleteFileW(g_state->dump.c_str());
				outf("Minidump: not written (error 0x%08lX)\n", error);
			}
		}

		void writeFrame(HANDLE process, int index, DWORD64 address, bool symbols) {
			// A return address is past its call; one byte back is the call's line.
			const DWORD64 lookup = index == 0 ? address : address - 1;
			char		  module[MAX_PATH] = "?";
			HMODULE		  base = nullptr;
			if (GetModuleHandleExA(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCSTR>(lookup),
					&base
				) != FALSE) {
				char path[MAX_PATH];
				if (GetModuleFileNameA(base, path, MAX_PATH) != 0) {
					const char* slash = std::strrchr(path, '\\');
					strncpy_s(module, slash != nullptr ? slash + 1 : path, _TRUNCATE);
				}
			}

			constexpr DWORD			  kMaxName = 512;
			alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + kMaxName] = {};
			auto*					  symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = kMaxName;
			DWORD64 displacement = 0;
			if (symbols && SymFromAddr(process, lookup, &displacement, symbol) != FALSE) {
				IMAGEHLP_LINE64 line{};
				line.SizeOfStruct = sizeof(line);
				DWORD column = 0;
				if (SymGetLineFromAddr64(process, lookup, &column, &line) != FALSE) {
					outf("  %2d  %s!%s+0x%llx  %s:%lu\n", index, module, symbol->Name, displacement, line.FileName, line.LineNumber);
				} else {
					outf("  %2d  %s!%s+0x%llx\n", index, module, symbol->Name, displacement);
				}
			} else {
				outf("  %2d  %s+0x%llx\n", index, module, base != nullptr ? address - reinterpret_cast<DWORD64>(base) : address);
			}
		}

		void writeStack() {
			HANDLE process = GetCurrentProcess();
			SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
			const bool	 symbols = SymInitialize(process, nullptr, TRUE) != FALSE;
			HANDLE		 thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_state->crashedThread);
			CONTEXT		 context = *g_state->pointers->ContextRecord;
			STACKFRAME64 frame{};
#if defined(_M_X64)
			constexpr DWORD kMachine = IMAGE_FILE_MACHINE_AMD64;
			frame.AddrPC.Offset = context.Rip;
			frame.AddrFrame.Offset = context.Rbp;
			frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_ARM64)
			constexpr DWORD kMachine = IMAGE_FILE_MACHINE_ARM64;
			frame.AddrPC.Offset = context.Pc;
			frame.AddrFrame.Offset = context.Fp;
			frame.AddrStack.Offset = context.Sp;
#endif
			frame.AddrPC.Mode = AddrModeFlat;
			frame.AddrFrame.Mode = AddrModeFlat;
			frame.AddrStack.Mode = AddrModeFlat;
			out("Stack:\n");
#if defined(_M_X64) || defined(_M_ARM64)
			for (int index = 0; index < kMaxFrames; ++index) {
				if (StackWalk64(
						kMachine,
						process,
						thread != nullptr ? thread : GetCurrentThread(),
						&frame,
						&context,
						nullptr,
						SymFunctionTableAccess64,
						SymGetModuleBase64,
						nullptr
					) == FALSE ||
					frame.AddrPC.Offset == 0) {
					break;
				}
				writeFrame(process, index, frame.AddrPC.Offset, symbols);
			}
#else
			out("  not walked on this architecture\n");
#endif
			if (thread != nullptr) {
				CloseHandle(thread);
			}
			if (symbols) {
				SymCleanup(process);
			}
		}

		void writeReport() {
			const EXCEPTION_RECORD& record = *g_state->pointers->ExceptionRecord;
			outf("\n=== Crash: %s (0x%08lX), thread %lu\n", g_state->what, record.ExceptionCode, g_state->crashedThread);
			out(g_state->header + "\n");
			if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
				const ULONG_PTR kind = record.ExceptionInformation[0];
				const char*		action = kind == 0 ? "reading" : kind == 1 ? "writing" : "executing";
				outf("%s address 0x%016llX\n", action, static_cast<unsigned long long>(record.ExceptionInformation[1]));
			}
			if (!g_state->detail.empty()) {
				out(g_state->detail + "\n");
			}
			writeDump();
			writeStack();
		}

		DWORD WINAPI writeReports(void* /*unused*/) {
			WaitForSingleObject(g_state->requested, INFINITE);
			if (!g_state->stopping.load()) {
				writeReport();
				SetEvent(g_state->done);
			}
			return 0;
		}

		[[noreturn]] void end(UINT code) {
			TerminateProcess(GetCurrentProcess(), code);
			ExitProcess(code);
		}

		void breakIfDebugged() {
			if (IsDebuggerPresent() != FALSE) {
				__debugbreak();
			}
		}

		// From the crashed thread: the writer reports while this one waits, so a thread with no stack left
		// still gets one. A second crash elsewhere waits for the first's report to end the process.
		void report(EXCEPTION_POINTERS* pointers, const char* what, std::string detail = {}) {
			const DWORD self = GetCurrentThreadId();
			DWORD		none = 0;
			if (!g_reportingThread.compare_exchange_strong(none, self)) {
				if (none == self) {
					return;
				}
				Sleep(INFINITE);
			}
			if (self == g_state->writerId) {
				end(pointers->ExceptionRecord->ExceptionCode); // the writer itself crashed
			}
			g_state->pointers = pointers;
			g_state->crashedThread = self;
			g_state->what = what;
			g_state->detail = std::move(detail);
			SetEvent(g_state->requested);
			WaitForSingleObject(g_state->done, kReportWaitMs);
		}

		// A report from a handler that has no exception, from here.
		void reportHere(const char* what, std::string detail = {}) {
			CONTEXT context{};
			RtlCaptureContext(&context);
			EXCEPTION_RECORD record{};
			record.ExceptionCode = kFatalAppExit;
#if defined(_M_X64)
			record.ExceptionAddress = reinterpret_cast<void*>(context.Rip);
#elif defined(_M_ARM64)
			record.ExceptionAddress = reinterpret_cast<void*>(context.Pc);
#endif
			EXCEPTION_POINTERS pointers{.ExceptionRecord = &record, .ContextRecord = &context};
			report(&pointers, what, std::move(detail));
		}

		LONG WINAPI onException(EXCEPTION_POINTERS* pointers) {
			const DWORD code = pointers->ExceptionRecord->ExceptionCode;
			report(pointers, exceptionName(code));
			if (code == kCxxException && g_state->previousFilter != nullptr) {
				// The CRT's filter makes the exception current and calls terminate, which adds its what().
				return g_state->previousFilter(pointers);
			}
			end(code);
		}

		void __cdecl onAbort(int /*signal*/) {
			breakIfDebugged();
			reportHere("abort");
			end(kAbortExitCode);
		}

		void __cdecl onPurecall() {
			breakIfDebugged();
			reportHere("a pure virtual call");
			end(kAbortExitCode);
		}

		std::string narrow(const wchar_t* text) {
			if (text == nullptr) {
				return "?";
			}
			const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			if (size <= 1) {
				return "?";
			}
			std::string result(static_cast<std::size_t>(size) - 1, '\0');
			WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
			return result;
		}

		// Release builds of the CRT pass nulls for all but the last.
		void __cdecl onInvalidParameter(
			const wchar_t* expression,
			const wchar_t* function,
			const wchar_t* file,
			unsigned int   line,
			uintptr_t /*reserved*/
		) {
			breakIfDebugged();
			std::string detail;
			if (expression != nullptr) {
				detail = narrow(expression) + " in " + narrow(function) + " (" + narrow(file) + ":" + std::to_string(line) + ")";
			}
			reportHere("a CRT function given an invalid parameter", std::move(detail));
			end(kAbortExitCode);
		}

		[[noreturn]] void onTerminate() {
			std::string detail = terminateDetail();
			if (g_reportingThread.load() == GetCurrentThreadId()) {
				out(detail + "\n"); // the C++ exception's report is written; this adds its what()
				end(kAbortExitCode);
			}
			breakIfDebugged();
			reportHere("std::terminate", std::move(detail));
			end(kAbortExitCode);
		}

		std::string utf8(const std::filesystem::path& path) {
			const std::u8string text = path.u8string();
			return {text.begin(), text.end()};
		}

	} // namespace

	bool installCrashHandler(const CrashFiles& files) {
		auto* state = new State; // NOLINT(cppcoreguidelines-owning-memory)
		state->report = files.report.wstring();
		state->dump = files.dump.wstring();
		state->dumpText = utf8(files.dump);
		state->header = files.header;
		state->requested = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		state->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		g_state = state;
		state->writer = CreateThread(nullptr, 0, writeReports, nullptr, 0, &state->writerId);
		if (state->requested == nullptr || state->done == nullptr || state->writer == nullptr) {
			for (HANDLE handle : {state->requested, state->done, state->writer}) {
				if (handle != nullptr) {
					CloseHandle(handle);
				}
			}
			g_state = nullptr;
			delete state; // NOLINT(cppcoreguidelines-owning-memory)
			return false;
		}

		// Room for the filter on a thread whose stack has overflowed: it only wakes the writer and waits.
		ULONG guarantee = 64 * 1024;
		SetThreadStackGuarantee(&guarantee);

		state->previousFilter = SetUnhandledExceptionFilter(onException);
		state->previousAbort = std::signal(SIGABRT, onAbort);
		state->previousAbortBehavior = _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		state->previousPurecall = _set_purecall_handler(onPurecall);
		state->previousInvalidParameter = _set_invalid_parameter_handler(onInvalidParameter);
		state->previousTerminate = std::set_terminate(onTerminate);
		return true;
	}

	void uninstallCrashHandler() {
		State* state = g_state;
		if (state == nullptr) {
			return;
		}
		std::set_terminate(state->previousTerminate);
		_set_invalid_parameter_handler(state->previousInvalidParameter);
		_set_purecall_handler(state->previousPurecall);
		_set_abort_behavior(state->previousAbortBehavior, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		std::signal(SIGABRT, state->previousAbort);
		SetUnhandledExceptionFilter(state->previousFilter);

		state->stopping.store(true);
		SetEvent(state->requested);
		WaitForSingleObject(state->writer, INFINITE);
		CloseHandle(state->writer);
		CloseHandle(state->requested);
		CloseHandle(state->done);
		if (state->reportFile != INVALID_HANDLE_VALUE) {
			CloseHandle(state->reportFile);
		}
		g_state = nullptr;
		g_reportingThread.store(0);
		delete state; // NOLINT(cppcoreguidelines-owning-memory)
	}

	bool crashHandlerInPlace() {
		const LPTOP_LEVEL_EXCEPTION_FILTER filter = SetUnhandledExceptionFilter(nullptr);
		SetUnhandledExceptionFilter(filter);
		return filter == onException && std::signal(SIGABRT, SIG_GET) == onAbort && _get_purecall_handler() == onPurecall &&
			   _get_invalid_parameter_handler() == onInvalidParameter && std::get_terminate() == onTerminate;
	}

} // namespace app::detail
