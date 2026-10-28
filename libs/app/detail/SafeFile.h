#pragma once

// Files that are never seen half written, for the settings file and saves. The bytes go to a temp file
// of their own beside the target, created exclusively and named for this write alone, are flushed
// through to the disk (F_FULLFSYNC on Apple, where fsync leaves them in the drive's cache), and the temp
// file is renamed over the target, which replaces it atomically. A crash at any point leaves the target
// as it was, or as written, plus at most a stray temp file that a later write of the target sweeps up.
//
// On Windows the rename is a POSIX-semantics one, which replaces a target another process has open
// (a scanner, an indexer, a sync client), with MoveFileExW where the filesystem can't do it, and a rename
// refused because a file is held is tried again for about a second. On POSIX the directory is flushed
// after the rename, and after making it, so the rename itself survives a power cut.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace app::detail {

	// The name of the file a temp file named `name` was written for, <target> in <target>.<pid>.<n>.tmp;
	// nullopt for a name that isn't a temp file's.
	std::optional<std::string_view> tempTargetOf(std::string_view name);

	// Creates the target's directory when it's missing. False, logged, when any step fails, and the temp
	// file is removed then. Temp files of the target left more than ten minutes ago are removed.
	bool writeFileSafely(const std::filesystem::path& target, std::span<const std::byte> bytes);

	// `from` renamed over `to`, replacing it atomically, as writeFileSafely's last step does. False,
	// logged, when it can't be, with both files as they were.
	bool renameReplacing(const std::filesystem::path& from, const std::filesystem::path& to);

	struct FileRead {
		enum class Status : std::uint8_t { Read, Missing, Unreadable };
		Status				   status = Status::Missing;
		std::vector<std::byte> bytes;
	};

	// Missing only when nothing is at the path; a directory there, or a file that can't be opened or
	// read, is Unreadable.
	FileRead readFile(const std::filesystem::path& file);

	// The first of <file><suffix>, <file><suffix>.1, <file><suffix>.2 and on that nothing is at.
	std::filesystem::path freeName(const std::filesystem::path& file, std::string_view suffix);

	// Renames `file` to freeName(file, suffix), so an earlier copy set aside is never replaced. Where it
	// went, or nullopt, logged, when the rename fails.
	std::optional<std::filesystem::path> moveAside(const std::filesystem::path& file, std::string_view suffix);

	// <file><suffix>.
	std::filesystem::path withSuffix(const std::filesystem::path& file, std::string_view suffix);

	// The path as UTF-8, for a log line.
	std::string pathText(const std::filesystem::path& path);

	// For tests: a write or rename onto a target this returns true for fails before it touches anything,
	// as a full disk or a held file would. Empty, so nothing fails, unless a test sets it.
	void setFaultForTests(std::function<bool(const std::filesystem::path& target)> fault);

} // namespace app::detail
