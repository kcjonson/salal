#pragma once

// The process's log on disk. Logger writes every line the console shows to it, without the console's
// colours, a line at a time with no buffering, so a crash loses nothing logged before it. Lines logged
// while no file is open are kept, up to 64 KB, and written into the next one opened, so a game's
// startup (the window, the GL context) is in the file too.
//
// A file that would pass its roll size is renamed to <stem>.1<ext>, replacing the one before, and
// started again under its header, so one run keeps its last roll size to twice that. While it's open
// another process can't remove it: Windows refuses the delete, and on POSIX it holds an flock that
// logFileInUse checks.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace foundation {

	// An open file's OS handle as a number: a HANDLE on Windows, a descriptor elsewhere.
	using NativeFile = std::intptr_t;
	inline constexpr NativeFile kNoFile = -1;

	// Opens `path` for appending, creating it, and writes `header` (a line) at its top and at the top of
	// each segment after a roll, then the lines kept from before. False, with nothing open, when the file
	// can't be opened. A file already open is closed first.
	bool openLogFile(const std::filesystem::path& path, std::uint64_t rollBytes, std::string header);
	void closeLogFile();
	// The open log file's path; empty when none is.
	std::filesystem::path logFilePath();
	// The open log file's handle, for a crash handler to write to without taking the log's lock; kNoFile
	// when none is open. Valid until the next roll or closeLogFile.
	NativeFile logFileHandle();

	// Whether a process (this one included) has `path` open as its log file now.
	bool logFileInUse(const std::filesystem::path& path);

	// All of `text` to `file`, retrying short writes; false when a write fails. Async-signal-safe on POSIX.
	bool writeNative(NativeFile file, std::string_view text);

	namespace detail {
		// One finished line, newline included, from Logger::log.
		void writeLogLine(std::string_view line);
	} // namespace detail

} // namespace foundation
