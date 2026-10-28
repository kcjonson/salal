#pragma once

// Files that are never seen half written, for the settings file and saves: the bytes go to a temp
// file beside the target, are flushed through to the disk, and the temp file is renamed over the
// target, which replaces it atomically on every platform salal runs on. A crash at any point leaves
// the target as it was, or as written, plus at most a stray temp file that the next write replaces.
// Not for two writers of one file at once: they share the temp file.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace app::detail {

	// <target>.tmp, beside the target.
	std::filesystem::path tempFor(const std::filesystem::path& target);

	// Creates the target's directory when it's missing. False, logged, when any step fails, and the temp
	// file is removed then.
	bool writeFileSafely(const std::filesystem::path& target, std::span<const std::byte> bytes);

	struct FileRead {
		enum class Status : std::uint8_t { Read, Missing, Unreadable };
		Status				   status = Status::Missing;
		std::vector<std::byte> bytes;
	};

	// Missing only when nothing is at the path; a directory there, or a file that can't be opened or
	// read, is Unreadable.
	FileRead readFile(const std::filesystem::path& file);

	// Renames `file` to <file><suffix>, replacing what's there. False, logged, when the rename fails.
	bool moveAside(const std::filesystem::path& file, std::string_view suffix);

	// <file><suffix>.
	std::filesystem::path withSuffix(const std::filesystem::path& file, std::string_view suffix);

	// The path as UTF-8, for a log line.
	std::string pathText(const std::filesystem::path& path);

} // namespace app::detail
