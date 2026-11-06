#include "LogFile.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace foundation {

	namespace {

		constexpr std::size_t kMaxEarlyBytes = 64 * 1024;

		struct State {
			std::mutex			  mutex;
			std::filesystem::path path;
			std::string			  header;
			std::uint64_t		  rollBytes = 0;
			std::uint64_t		  written = 0;
			bool				  bodyWritten = false; // a line since the header, so a roll can't loop on one long line
			NativeFile			  file = kNoFile;
			std::string			  early; // lines logged while no file was open
		};

		// Never destroyed, so a static destructor that logs at exit still finds it.
		State& state() {
			static State* const s = new State; // NOLINT(cppcoreguidelines-owning-memory)
			return *s;
		}

		std::atomic<NativeFile> g_handle{kNoFile}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

		NativeFile openForAppend(const std::filesystem::path& path) {
#ifdef _WIN32
			// No FILE_SHARE_DELETE, so no other process can remove it while it's open.
			HANDLE file =
				CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			return file == INVALID_HANDLE_VALUE ? kNoFile : reinterpret_cast<NativeFile>(file);
#else
			const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
			if (file >= 0) {
				::flock(file, LOCK_EX | LOCK_NB);
			}
			return file;
#endif
		}

		void closeNative(NativeFile file) {
#ifdef _WIN32
			CloseHandle(reinterpret_cast<HANDLE>(file));
#else
			::close(static_cast<int>(file));
#endif
		}

		std::filesystem::path previousSegment(const std::filesystem::path& path) {
			std::filesystem::path name = path.stem();
			name += ".1";
			name += path.extension();
			return path.parent_path() / name;
		}

		void writeHeader(State& s) {
			const std::string line = s.header + "\n";
			if (writeNative(s.file, line)) {
				s.written += line.size();
			}
			s.bodyWritten = false;
		}

		// The size bound holds even when the rename fails: the file starts over either way.
		void roll(State& s) {
			g_handle.store(kNoFile);
			closeNative(s.file);
			std::error_code error;
			std::filesystem::rename(s.path, previousSegment(s.path), error);
			if (error) {
				std::filesystem::remove(s.path, error);
			}
			s.file = openForAppend(s.path);
			s.written = 0;
			g_handle.store(s.file);
			writeHeader(s);
		}

		void put(State& s, std::string_view text) {
			if (s.bodyWritten && s.written + text.size() > s.rollBytes) {
				roll(s);
			}
			if (writeNative(s.file, text)) {
				s.written += text.size();
				s.bodyWritten = true;
			}
		}

		void closeLocked(State& s) {
			if (s.file == kNoFile) {
				return;
			}
			g_handle.store(kNoFile);
			closeNative(s.file);
			s.file = kNoFile;
			s.path.clear();
		}

	} // namespace

	bool openLogFile(const std::filesystem::path& path, std::uint64_t rollBytes, std::string header) {
		State&				   s = state();
		const std::scoped_lock lock(s.mutex);
		closeLocked(s);
		const NativeFile file = openForAppend(path);
		if (file == kNoFile) {
			return false;
		}
		std::error_code		 error;
		const std::uintmax_t existing = std::filesystem::file_size(path, error);
		s.path = path;
		s.header = std::move(header);
		s.rollBytes = rollBytes;
		s.file = file;
		s.written = error ? 0 : existing;
		g_handle.store(file);
		writeHeader(s);
		const std::string early = std::exchange(s.early, {});
		if (!early.empty()) {
			put(s, early);
		}
		return true;
	}

	void closeLogFile() {
		State&				   s = state();
		const std::scoped_lock lock(s.mutex);
		closeLocked(s);
	}

	std::filesystem::path logFilePath() {
		State&				   s = state();
		const std::scoped_lock lock(s.mutex);
		return s.path;
	}

	NativeFile logFileHandle() {
		return g_handle.load();
	}

	bool logFileInUse(const std::filesystem::path& path) {
#ifdef _WIN32
		// The writer shares no delete access, so asking for it is refused while the file is open.
		HANDLE file = CreateFileW(
			path.c_str(),
			DELETE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr
		);
		if (file == INVALID_HANDLE_VALUE) {
			return GetLastError() == ERROR_SHARING_VIOLATION;
		}
		CloseHandle(file);
		return false;
#else
		const int file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
		if (file < 0) {
			return false;
		}
		const bool held = ::flock(file, LOCK_EX | LOCK_NB) != 0 && errno == EWOULDBLOCK;
		::close(file); // and with it the lock, if this took it
		return held;
#endif
	}

	bool writeNative(NativeFile file, std::string_view text) {
		if (file == kNoFile) {
			return false;
		}
		while (!text.empty()) {
#ifdef _WIN32
			DWORD	   written = 0;
			const auto chunk = static_cast<DWORD>(std::min<std::size_t>(text.size(), std::size_t{1} << 30U));
			if (WriteFile(reinterpret_cast<HANDLE>(file), text.data(), chunk, &written, nullptr) == 0 || written == 0) {
				return false;
			}
#else
			const ssize_t written = ::write(static_cast<int>(file), text.data(), text.size());
			if (written < 0 && errno == EINTR) {
				continue;
			}
			if (written <= 0) {
				return false;
			}
#endif
			text.remove_prefix(static_cast<std::size_t>(written));
		}
		return true;
	}

	namespace detail {
		void writeLogLine(std::string_view line) {
			State&				   s = state();
			const std::scoped_lock lock(s.mutex);
			if (s.file != kNoFile) {
				put(s, line);
			} else if (s.early.size() + line.size() <= kMaxEarlyBytes) {
				s.early.append(line);
			}
		}
	} // namespace detail

} // namespace foundation
