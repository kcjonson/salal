#pragma once

// Saves: the game's bytes in named slots, written so a crash never costs the last good save, with the
// one before it kept as a backup, and brought up to the build's format by the game's own upgrade steps
// when read. What a save holds is the game's; app never looks inside.
//
// A slot is a file, <slot>.save, in the store's directory, with <slot>.save.bak beside it. Slot names
// are file names in lower case (validSlotName, in storage/UserDirectories.h), so two slots can't be
// one file on a filesystem that folds case. Each file is an envelope around the game's bytes,
// little-endian:
//
//   offset  size  field
//        0     8  magic, the ASCII "SALALSAV"
//        8     4  envelope version, u32, 1
//       12     4  the game's format version, i32, 1 or more
//       16     8  write counter, u64: the slot's writes, counting from 1
//       24     8  time written, i64, microseconds since 1970-01-01 UTC
//       32     8  payload length n, u64
//       40     n  payload, the game's bytes
//     40+n     4  CRC-32 (zlib's) of everything before it
//
// A later envelope version (2 to 8) keeps the first 16 bytes where they are, so an older build can still
// say which game version a save is, and reports it NewerThanBuild; past 8 the header is damaged.
//
// A write goes to a temp file, flushed to the disk, and is renamed over the save after the save it
// replaces has been copied (the same way) over the backup, so at every moment the save is whole: the
// last good one, or the new one. What isn't one of this build's good saves never goes over the backup:
// a damaged save (a disk that lost a write) is copied aside to <slot>.save.corrupt (.corrupt.1 and on
// after that), one that can't be read is hard-linked there (renamed where it can't be linked), and a
// newer build's save is kept as <slot>.save.v<its version>, which only a copy of that version written
// later (a higher write counter) replaces, so an older build's autosaves don't push it out.
// restoreBackup stages the save it replaces as <slot>.save.swap, and the next call on the slot
// finishes or drops what an interrupted restore left.
//
// Not thread-safe. Two stores on one directory (two processes, say) don't tear each other's files,
// since every write is its own temp file renamed into place, but don't coordinate either: the last
// write wins.

#include "saves/Expected.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace app {

	struct SaveFormat {
		int version = 1; // the game's format version, not salal's; 1 or more
		std::vector<std::function<bool(std::vector<std::byte>&)>>
			upgrades; // upgrades[i] takes version i+1 to i+2; the game writes each step
	};

	struct SlotInfo {
		std::string							  name;
		int									  version = 0; // the save's format version; past SaveFormat::version for a newer build's
		std::uint64_t						  writes = 0;  // the write counter
		std::chrono::system_clock::time_point written{};   // zero for a time past what the clock holds
		// The save's envelope reads and its length is right (its CRC is read() and restoreBackup()'s to
		// check). False for a damaged save, and for a slot with only a backup, whose other fields are 0;
		// restoreBackup may bring either back. A newer envelope gives its version alone.
		bool intact = false;
	};

	class SaveStore {
	  public:
		enum class LoadError : std::uint8_t {
			Missing,		// no save in the slot (a backup alone isn't read), or a slot name validSlotName refuses
			Corrupt,		// the file can't be read, its envelope or CRC is wrong, or (readJson) the payload isn't JSON
			NewerThanBuild, // a newer format version than the build's, or a newer envelope; not read, not touched
			UpgradeFailed	// an upgrade step returned false or threw, or the format has none for a version
		};

		// A store with no directory: writes fail and reads are Missing, logged.
		SaveStore() = default;
		// The directory is made by the first write. A format version below 1 is taken as 1, logged.
		SaveStore(std::filesystem::path directory, SaveFormat format);

		// The bytes as the slot's save, at the format's version, the save it replaces kept as the backup
		// when it's one of this build's (set aside as above when it isn't). The write counter goes on from
		// the higher of the save's and the backup's. False, logged, for a slot name validSlotName refuses or
		// a write that fails, and the save is as it was.
		bool write(std::string_view slot, std::span<const std::byte> bytes);

		// The slot's bytes at the format's version. A save at an older version goes through each upgrade
		// step in order and, once they all succeed, is written back at the build's version, so the upgrade
		// runs once and the pre-upgrade file is the backup from then (if that write fails, logged, the
		// bytes are still returned and the next read upgrades again). A failed upgrade leaves every file as
		// it was. A save newer than the build isn't read and isn't touched.
		Expected<std::vector<std::byte>, LoadError> read(std::string_view slot);

		// write and read for a game that saves JSON, as its UTF-8 text. writeJson is false, logged, for JSON
		// that can't be written as text (a string that isn't UTF-8).
		bool								writeJson(std::string_view slot, const nlohmann::json& value);
		Expected<nlohmann::json, LoadError> readJson(std::string_view slot);

		// Every slot with a save or a backup, by name, byte order.
		std::vector<SlotInfo> slots() const;

		// The backup becomes the save and the save it replaces the backup, so a second call undoes the
		// first. The save being replaced is staged as <slot>.save.swap, then the backup renamed over the
		// save, then the swap file over the backup, so the replaced save is on disk under some name
		// throughout. True once the save is the backup's: when only the last rename fails, logged, the
		// swap file stays for the next call on the slot to make the backup, and a call that finds a restore
		// interrupted between the renames (a crash, or that failure) finishes it and is true without
		// swapping again, so a retry doesn't undo it. When an earlier step fails, false and nothing changed.
		// With no save the backup is renamed into place, leaving none; a damaged save is copied aside and
		// one that can't be read linked aside, and the backup renamed straight over it. False, logged, with
		// nothing changed, when there's no backup or the backup is damaged.
		bool restoreBackup(std::string_view slot);

		// Every file of the slot: the save, the backup, a staged swap, the copies set aside (.corrupt,
		// .corrupt.N, .vN) and temp files. True when none is left, a slot that had none included; false,
		// logged, for a slot name validSlotName refuses or a file that won't go.
		bool remove(std::string_view slot);

		// Empty for a store with no directory.
		const std::filesystem::path& directory() const { return root; }

	  private:
		// A directory, and a slot name validSlotName takes; logged when not.
		bool usable(std::string_view slot, const char* what) const;

		std::filesystem::path root;
		SaveFormat			  format;
	};

} // namespace app
