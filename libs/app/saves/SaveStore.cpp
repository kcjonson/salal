#include "saves/SaveStore.h"

#include "detail/Guard.h"
#include "detail/SafeFile.h"
#include "storage/UserDirectories.h"

#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

namespace app {

	namespace {

		using LoadError = SaveStore::LoadError;

		constexpr std::array<char, 8> kMagic{'S', 'A', 'L', 'A', 'L', 'S', 'A', 'V'};
		constexpr std::uint32_t		  kEnvelopeVersion = 1;
		constexpr std::uint32_t		  kNewestPlausibleEnvelope = 8; // past it, the header is damaged, not from a later salal
		constexpr std::size_t		  kHeaderSize = 40;
		constexpr std::size_t		  kCrcSize = 4;
		constexpr std::string_view	  kSaveSuffix = ".save";
		constexpr std::string_view	  kBackupSuffix = ".bak";
		constexpr std::string_view	  kCorruptSuffix = ".corrupt";
		constexpr std::string_view	  kSwapSuffix = ".swap";

		// --- CRC-32, the reflected 0x04C11DB7 polynomial zlib, PNG and zip use ---

		constexpr std::array<std::uint32_t, 256> kCrcTable = [] {
			std::array<std::uint32_t, 256> table{};
			for (std::uint32_t i = 0; i < table.size(); ++i) {
				std::uint32_t value = i;
				for (int bit = 0; bit < 8; ++bit) {
					value = (value & 1U) != 0 ? 0xEDB88320U ^ (value >> 1U) : value >> 1U;
				}
				table[i] = value;
			}
			return table;
		}();

		std::uint32_t crc32(std::span<const std::byte> bytes) {
			std::uint32_t crc = 0xFFFFFFFFU;
			for (const std::byte b : bytes) {
				crc = kCrcTable[(crc ^ std::to_integer<std::uint32_t>(b)) & 0xFFU] ^ (crc >> 8U);
			}
			return crc ^ 0xFFFFFFFFU;
		}

		// --- The envelope ---

		template <typename T>
		void put(std::vector<std::byte>& out, T value) {
			const auto bits = static_cast<std::uint64_t>(value);
			for (std::size_t i = 0; i < sizeof(T); ++i) {
				out.push_back(static_cast<std::byte>((bits >> (8U * i)) & 0xFFU));
			}
		}

		template <typename T>
		T get(std::span<const std::byte> in, std::size_t offset) {
			std::uint64_t bits = 0;
			for (std::size_t i = 0; i < sizeof(T); ++i) {
				bits |= std::to_integer<std::uint64_t>(in[offset + i]) << (8U * i);
			}
			return static_cast<T>(bits);
		}

		std::int64_t microsecondsNow() {
			const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
			return std::chrono::duration_cast<std::chrono::microseconds>(sinceEpoch).count();
		}

		std::vector<std::byte> encode(int version, std::uint64_t writes, std::span<const std::byte> payload) {
			std::vector<std::byte> file;
			file.reserve(kHeaderSize + payload.size() + kCrcSize);
			for (const char c : kMagic) {
				file.push_back(static_cast<std::byte>(c));
			}
			put<std::uint32_t>(file, kEnvelopeVersion);
			put<std::int32_t>(file, version);
			put<std::uint64_t>(file, writes);
			put<std::int64_t>(file, microsecondsNow());
			put<std::uint64_t>(file, payload.size());
			file.insert(file.end(), payload.begin(), payload.end());
			put<std::uint32_t>(file, crc32(file));
			return file;
		}

		struct Envelope {
			enum class Status : std::uint8_t { Intact, Damaged, Newer };
			Status					   status = Status::Damaged;
			int						   version = 0;
			std::uint64_t			   writes = 0;
			std::int64_t			   written = 0;
			std::span<const std::byte> payload; // only when the whole file was given
		};

		// The header's fields, given at least its first 40 bytes and the file's size; with the whole file,
		// the CRC is checked too and the payload found. A newer envelope gives only the game's version.
		Envelope parse(std::span<const std::byte> head, std::uintmax_t fileSize, bool wholeFile) {
			Envelope envelope;
			if (head.size() < 16 || !std::equal(kMagic.begin(), kMagic.end(), head.begin(), [](char c, std::byte b) {
					return static_cast<std::byte>(c) == b;
				})) {
				return envelope;
			}
			const auto envelopeVersion = get<std::uint32_t>(head, 8);
			envelope.version = get<std::int32_t>(head, 12);
			// A later salal counts up from 1; a value far past that is a flipped bit, which reads as damaged.
			if (envelopeVersion > kEnvelopeVersion && envelopeVersion <= kNewestPlausibleEnvelope) {
				envelope.status = Envelope::Status::Newer;
				return envelope;
			}
			envelope.version = 0;
			if (envelopeVersion != kEnvelopeVersion || head.size() < kHeaderSize || fileSize < kHeaderSize + kCrcSize) {
				return envelope;
			}
			const auto length = get<std::uint64_t>(head, 32);
			if (length != fileSize - kHeaderSize - kCrcSize) {
				return envelope;
			}
			if (wholeFile) {
				const std::size_t crcAt = head.size() - kCrcSize;
				if (crc32(head.first(crcAt)) != get<std::uint32_t>(head, crcAt)) {
					return envelope;
				}
				envelope.payload = head.subspan(kHeaderSize, static_cast<std::size_t>(length));
			}
			const auto version = get<std::int32_t>(head, 12);
			if (version < 1) {
				return envelope;
			}
			envelope.status = Envelope::Status::Intact;
			envelope.version = version;
			envelope.writes = get<std::uint64_t>(head, 16);
			envelope.written = get<std::int64_t>(head, 24);
			return envelope;
		}

		Envelope parseWhole(std::span<const std::byte> file) {
			return parse(file, file.size(), true);
		}

		// The header alone, for slots(), which needn't read every save whole.
		Envelope parseHeader(const std::filesystem::path& file) {
			std::error_code		 error;
			const std::uintmax_t size = std::filesystem::file_size(file, error);
			std::ifstream		 in(file, std::ios::binary);
			if (error || !in) {
				return {};
			}
			std::array<char, kHeaderSize> head{};
			in.read(head.data(), head.size());
			return parse(std::as_bytes(std::span(head)).first(static_cast<std::size_t>(in.gcount())), size, false);
		}

		// --- A slot's files ---

		struct SlotFiles {
			std::filesystem::path save;
			std::filesystem::path backup;
			std::filesystem::path swap; // the save restoreBackup is replacing, until it's the backup
		};

		SlotFiles filesFor(const std::filesystem::path& root, std::string_view slot) {
			const std::filesystem::path save = root / (std::string(slot) + std::string(kSaveSuffix));
			return {.save = save, .backup = detail::withSuffix(save, kBackupSuffix), .swap = detail::withSuffix(save, kSwapSuffix)};
		}

		bool allDigits(std::string_view text) {
			return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
		}

		// Whether the file `name` is one of the slot's whose save is `saveName`: the save, its backup, its
		// staged swap, its copies set aside (.corrupt, .corrupt.N, .vN) and their temp files.
		bool slotFile(std::string_view name, std::string_view saveName) {
			if (const std::optional<std::string_view> target = detail::tempTargetOf(name)) {
				name = *target;
			}
			if (!name.starts_with(saveName)) {
				return false;
			}
			std::string_view rest = name.substr(saveName.size());
			if (rest.empty() || rest == kBackupSuffix || rest == kSwapSuffix) {
				return true;
			}
			if (rest.starts_with(kCorruptSuffix)) {
				rest.remove_prefix(kCorruptSuffix.size());
				return rest.empty() || (rest.front() == '.' && allDigits(rest.substr(1)));
			}
			if (rest.starts_with(".v")) {
				rest.remove_prefix(2);
				if (rest.starts_with('-')) {
					rest.remove_prefix(1);
				}
				return allDigits(rest);
			}
			return false;
		}

		// A save this build mustn't push out: a later format version, or a later envelope.
		bool newerThan(const Envelope& envelope, int build) {
			return envelope.status == Envelope::Status::Newer || (envelope.status == Envelope::Status::Intact && envelope.version > build);
		}

		// Where a newer build's save is kept: <slot>.save.v<its version>, which no write of this build touches.
		std::filesystem::path keptNewer(const SlotFiles& files, const Envelope& envelope) {
			return detail::withSuffix(files.save, ".v" + std::to_string(envelope.version));
		}

		// Whether a newer build's save, its header `candidate`, takes the place of the copy kept at `kept`:
		// when there's none, or a damaged one, or the candidate was written later. The write counter says
		// which is later; where either's can't be read (a later envelope), the slot's save, written after
		// its backup, is taken as the later.
		bool replacesKept(const std::filesystem::path& kept, const Envelope& candidate, bool fromSave) {
			std::error_code error;
			if (!std::filesystem::exists(kept, error)) {
				return true;
			}
			const Envelope existing = parseHeader(kept);
			if (existing.status == Envelope::Status::Damaged) {
				return true;
			}
			if (existing.status == Envelope::Status::Intact && candidate.status == Envelope::Status::Intact) {
				return candidate.writes > existing.writes;
			}
			return fromSave;
		}

		// A save that can't be read, kept aside as <slot>.save.corrupt without reading it: hard-linked, so
		// its name holds a save until the replacement's rename, and renamed where it can't be linked (a
		// directory, a filesystem without links).
		bool setAsideUnreadable(const std::filesystem::path& save) {
			const std::filesystem::path aside = detail::freeName(save, kCorruptSuffix);
			std::error_code				error;
			std::filesystem::create_hard_link(save, aside, error);
			if (!error) {
				LOG_WARNING(
					Engine,
					"SaveStore: %s can't be read; linked aside to %s",
					detail::pathText(save).c_str(),
					detail::pathText(aside).c_str()
				);
				return true;
			}
			return detail::moveAside(save, kCorruptSuffix).has_value();
		}

		enum class Recovered : std::uint8_t { Nothing, Finished, Dropped, Kept };

		// What an interrupted restoreBackup left, put right. Its swap file holds the save it was replacing:
		// with no backup the backup's rename into place had happened, so the restore is finished by the
		// swap file becoming the backup; matching the save, the restore hadn't started replacing anything,
		// so it goes. Anything else is kept and logged, for remove() or a person to deal with.
		Recovered recover(const SlotFiles& files) {
			const detail::FileRead swap = detail::readFile(files.swap);
			if (swap.status == detail::FileRead::Status::Missing) {
				return Recovered::Nothing;
			}
			const std::string name = detail::pathText(files.swap);
			std::error_code	  error;
			if (swap.status == detail::FileRead::Status::Read && !std::filesystem::exists(files.backup, error) && !error) {
				if (detail::renameReplacing(files.swap, files.backup)) {
					LOG_WARNING(Engine, "SaveStore: finished an interrupted restore; %s is the backup now", name.c_str());
					return Recovered::Finished;
				}
				return Recovered::Kept;
			}
			const detail::FileRead save = detail::readFile(files.save);
			if (swap.status == detail::FileRead::Status::Read && save.status == detail::FileRead::Status::Read &&
				save.bytes == swap.bytes) {
				std::filesystem::remove(files.swap, error);
				return Recovered::Dropped;
			}
			LOG_WARNING(Engine, "SaveStore: %s, left by an interrupted restore, matches nothing in its slot; kept", name.c_str());
			return Recovered::Kept;
		}

		// The save it replaces copied over the backup when it's one of this build's, kept under its own name
		// when it's a newer build's (unless a copy written later is there already), or copied aside when
		// it's damaged (linked aside when it can't be read), then the new save; each a safe write, so the
		// save is whole throughout. A newer build's backup is renamed to its own name the same way before
		// it's replaced. The counter goes on from the higher of the save's and the backup's, the backup's read
		// from its header alone. Stops at the first failure.
		bool writeSave(const SlotFiles& files, int build, std::span<const std::byte> payload) {
			const detail::FileRead current = detail::readFile(files.save);
			const Envelope		   backup = parseHeader(files.backup);
			std::uint64_t		   writes = backup.status == Envelope::Status::Intact ? backup.writes : 0;
			if (current.status == detail::FileRead::Status::Unreadable) {
				if (!setAsideUnreadable(files.save)) {
					return false;
				}
			} else if (current.status == detail::FileRead::Status::Read) {
				const Envelope envelope = parseWhole(current.bytes);
				writes = std::max(writes, envelope.status == Envelope::Status::Intact ? envelope.writes : 0);
				std::filesystem::path keep = files.backup;
				if (envelope.status == Envelope::Status::Damaged) {
					LOG_WARNING(
						Engine, "SaveStore: %s is damaged; copied aside, not kept as the backup", detail::pathText(files.save).c_str()
					);
					keep = detail::freeName(files.save, kCorruptSuffix);
				} else if (newerThan(envelope, build)) {
					const std::filesystem::path kept = keptNewer(files, envelope);
					const std::string			saveName = detail::pathText(files.save);
					const std::string			keptName = detail::pathText(kept);
					if (replacesKept(kept, envelope, true)) {
						keep = kept;
						LOG_WARNING(Engine, "SaveStore: %s is a newer build's; kept as %s", saveName.c_str(), keptName.c_str());
					} else {
						keep.clear();
						LOG_WARNING(
							Engine, "SaveStore: %s is a newer build's, older than %s; not kept", saveName.c_str(), keptName.c_str()
						);
					}
				} else if (newerThan(backup, build)) {
					const std::filesystem::path kept = keptNewer(files, backup);
					if (replacesKept(kept, backup, false) && !detail::renameReplacing(files.backup, kept)) {
						return false;
					}
				}
				if (!keep.empty() && !detail::writeFileSafely(keep, current.bytes)) {
					return false;
				}
			}
			return detail::writeFileSafely(files.save, encode(build, writes + 1, payload));
		}

		// A header's time, or zero for one past what the clock can hold.
		std::chrono::system_clock::time_point timeWritten(std::int64_t microseconds) {
			constexpr auto kLatest =
				std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::duration::max()).count();
			if (microseconds < 0 || microseconds > kLatest) {
				return {};
			}
			return std::chrono::system_clock::time_point(
				std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::microseconds(microseconds))
			);
		}

		std::string_view loadErrorName(LoadError error) {
			switch (error) {
				case LoadError::Missing:
					return "missing";
				case LoadError::Corrupt:
					return "corrupt";
				case LoadError::NewerThanBuild:
					return "newer than this build";
				case LoadError::UpgradeFailed:
					return "an upgrade failed";
			}
			return "?";
		}

	} // namespace

	SaveStore::SaveStore(std::filesystem::path directory, SaveFormat saveFormat)
		: root(std::move(directory)),
		  format(std::move(saveFormat)) {
		if (format.version < 1) {
			LOG_ERROR(Engine, "SaveStore: a format version of %d; versions start at 1, so it's 1", format.version);
			format.version = 1;
		}
	}

	bool SaveStore::write(std::string_view slot, std::span<const std::byte> bytes) {
		if (!usable(slot, "write")) {
			return false;
		}
		const SlotFiles files = filesFor(root, slot);
		recover(files);
		return writeSave(files, format.version, bytes);
	}

	Expected<std::vector<std::byte>, SaveStore::LoadError> SaveStore::read(std::string_view slot) {
		if (!usable(slot, "read")) {
			return Unexpected(LoadError::Missing);
		}
		const SlotFiles files = filesFor(root, slot);
		recover(files);
		const std::string	   name = detail::pathText(files.save);
		const detail::FileRead file = detail::readFile(files.save);
		const auto			   fail = [&](LoadError error) {
			LOG_WARNING(
				Engine, "SaveStore: %s: %.*s", name.c_str(), static_cast<int>(loadErrorName(error).size()), loadErrorName(error).data()
			);
			return Unexpected(error);
		};
		if (file.status == detail::FileRead::Status::Missing) {
			return Unexpected(LoadError::Missing);
		}
		if (file.status == detail::FileRead::Status::Unreadable) {
			return fail(LoadError::Corrupt);
		}
		const Envelope envelope = parseWhole(file.bytes);
		if (envelope.status == Envelope::Status::Damaged) {
			return fail(LoadError::Corrupt);
		}
		if (envelope.status == Envelope::Status::Newer || envelope.version > format.version) {
			return fail(LoadError::NewerThanBuild);
		}

		std::vector<std::byte> bytes(envelope.payload.begin(), envelope.payload.end());
		if (envelope.version == format.version) {
			return bytes;
		}
		for (int from = envelope.version; from < format.version; ++from) {
			const auto step = static_cast<std::size_t>(from - 1);
			if (step >= format.upgrades.size() || !format.upgrades[step]) {
				LOG_ERROR(Engine, "SaveStore: the format has no upgrade from version %d", from);
				return fail(LoadError::UpgradeFailed);
			}
			bool upgraded = false;
			if (!detail::guard("SaveStore", "upgrade", [&] { upgraded = format.upgrades[step](bytes); }) || !upgraded) {
				LOG_ERROR(Engine, "SaveStore: the upgrade from version %d to %d failed", from, from + 1);
				return fail(LoadError::UpgradeFailed);
			}
		}
		LOG_INFO(Engine, "SaveStore: %s upgraded from version %d to %d", name.c_str(), envelope.version, format.version);
		if (!writeSave(files, format.version, bytes)) {
			LOG_ERROR(Engine, "SaveStore: the upgraded %s couldn't be written back; the next read upgrades it again", name.c_str());
		}
		return bytes;
	}

	bool SaveStore::writeJson(std::string_view slot, const nlohmann::json& value) {
		std::string text;
		try {
			text = value.dump();
		} catch (const nlohmann::json::exception& e) {
			LOG_ERROR(Engine, "SaveStore: can't write %.*s as JSON: %s", static_cast<int>(slot.size()), slot.data(), e.what());
			return false;
		}
		return write(slot, std::as_bytes(std::span(text)));
	}

	Expected<nlohmann::json, SaveStore::LoadError> SaveStore::readJson(std::string_view slot) {
		Expected<std::vector<std::byte>, LoadError> bytes = read(slot);
		if (!bytes) {
			return Unexpected(bytes.error());
		}
		const auto*	   text = reinterpret_cast<const char*>(bytes->data());
		nlohmann::json value = nlohmann::json::parse(text, text + bytes->size(), nullptr, false);
		if (value.is_discarded()) {
			LOG_WARNING(Engine, "SaveStore: %.*s isn't JSON", static_cast<int>(slot.size()), slot.data());
			return Unexpected(LoadError::Corrupt);
		}
		return value;
	}

	std::vector<SlotInfo> SaveStore::slots() const {
		if (root.empty()) {
			return {};
		}
		std::set<std::string> names;
		std::error_code		  error;
		for (std::filesystem::directory_iterator it(root, error); !error && it != std::filesystem::directory_iterator();
			 it.increment(error)) {
			const std::string file = detail::pathText(it->path().filename());
			for (const std::string suffix : {std::string(kSaveSuffix), std::string(kSaveSuffix) + std::string(kBackupSuffix)}) {
				if (file.size() > suffix.size() && file.ends_with(suffix)) {
					const std::string name = file.substr(0, file.size() - suffix.size());
					if (validSlotName(name)) {
						names.insert(name);
					}
				}
			}
		}

		std::vector<SlotInfo> infos;
		infos.reserve(names.size());
		for (const std::string& name : names) {
			SlotInfo	   info{.name = name};
			const Envelope envelope = parseHeader(filesFor(root, name).save);
			if (envelope.status != Envelope::Status::Damaged) {
				info.intact = true;
				info.version = envelope.version;
				info.writes = envelope.writes;
				info.written = timeWritten(envelope.written);
			}
			infos.push_back(std::move(info));
		}
		return infos;
	}

	bool SaveStore::restoreBackup(std::string_view slot) {
		if (!usable(slot, "restoreBackup")) {
			return false;
		}
		const SlotFiles	  files = filesFor(root, slot);
		const std::string name = detail::pathText(files.save);
		if (recover(files) == Recovered::Finished) {
			// A restore this call repeats, already done: swapping again would undo it.
			LOG_INFO(Engine, "SaveStore: %s was restored from its backup by an earlier call; finished", name.c_str());
			return true;
		}
		const detail::FileRead backup = detail::readFile(files.backup);
		if (backup.status != detail::FileRead::Status::Read || parseWhole(backup.bytes).status == Envelope::Status::Damaged) {
			LOG_WARNING(Engine, "SaveStore: %s has no backup to restore, or a damaged one", name.c_str());
			return false;
		}

		const detail::FileRead current = detail::readFile(files.save);
		const bool			   damaged =
			current.status == detail::FileRead::Status::Read && parseWhole(current.bytes).status == Envelope::Status::Damaged;
		if (current.status != detail::FileRead::Status::Read || damaged) {
			// Nothing worth keeping as the backup: a damaged save is copied aside, one that can't be read is
			// linked aside, and the backup renamed straight over it, so there's always a save.
			if (damaged && !detail::writeFileSafely(detail::freeName(files.save, kCorruptSuffix), current.bytes)) {
				return false;
			}
			if (current.status == detail::FileRead::Status::Unreadable && !setAsideUnreadable(files.save)) {
				return false;
			}
			if (!detail::renameReplacing(files.backup, files.save)) {
				return false;
			}
			LOG_INFO(Engine, "SaveStore: %s restored from its backup", name.c_str());
			return true;
		}

		// The save being replaced is staged under a third name first, so it's on disk under some name at
		// every moment; recover() finishes or undoes a restore a crash interrupted.
		if (!detail::writeFileSafely(files.swap, current.bytes)) {
			return false;
		}
		if (!detail::renameReplacing(files.backup, files.save)) {
			std::error_code ignored;
			std::filesystem::remove(files.swap, ignored); // the save is as it was, and the staged copy a duplicate
			return false;
		}
		if (!detail::renameReplacing(files.swap, files.backup)) {
			// The save is the backup's, which is what was asked; the next call on the slot makes the swap
			// file the backup.
			LOG_ERROR(
				Engine,
				"SaveStore: %s is restored, but the save it replaced is still %s; the next call on the slot finishes it",
				name.c_str(),
				detail::pathText(files.swap).c_str()
			);
			return true;
		}
		LOG_INFO(Engine, "SaveStore: %s and its backup swapped", name.c_str());
		return true;
	}

	bool SaveStore::remove(std::string_view slot) {
		if (!usable(slot, "remove")) {
			return false;
		}
		const std::string saveName = detail::pathText(filesFor(root, slot).save.filename());
		bool			  removed = true;
		std::error_code	  error;
		for (std::filesystem::directory_iterator it(root, error); !error && it != std::filesystem::directory_iterator();
			 it.increment(error)) {
			if (!slotFile(detail::pathText(it->path().filename()), saveName)) {
				continue;
			}
			std::error_code failed;
			std::filesystem::remove(it->path(), failed);
			if (failed) {
				LOG_ERROR(Engine, "SaveStore: can't remove %s: %s", detail::pathText(it->path()).c_str(), failed.message().c_str());
				removed = false;
			}
		}
		return removed;
	}

	bool SaveStore::usable(std::string_view slot, const char* what) const {
		if (root.empty()) {
			LOG_ERROR(Engine, "SaveStore::%s: the store has no directory", what);
			return false;
		}
		if (!validSlotName(slot)) {
			LOG_ERROR(
				Engine,
				"SaveStore::%s: \"%.*s\" isn't a valid slot name (lowercase file name)",
				what,
				static_cast<int>(slot.size()),
				slot.data()
			);
			return false;
		}
		return true;
	}

} // namespace app
