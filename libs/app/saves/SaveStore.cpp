#include "saves/SaveStore.h"

#include "detail/Guard.h"
#include "detail/SafeFile.h"

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
		constexpr std::size_t		  kHeaderSize = 40;
		constexpr std::size_t		  kCrcSize = 4;
		constexpr std::string_view	  kSaveSuffix = ".save";
		constexpr std::string_view	  kBackupSuffix = ".bak";
		constexpr std::string_view	  kCorruptSuffix = ".corrupt";

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
			if (envelopeVersion > kEnvelopeVersion) {
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
			std::filesystem::path corrupt;
		};

		SlotFiles filesFor(const std::filesystem::path& root, std::string_view slot) {
			const std::filesystem::path save = root / (std::string(slot) + std::string(kSaveSuffix));
			return {.save = save, .backup = detail::withSuffix(save, kBackupSuffix), .corrupt = detail::withSuffix(save, kCorruptSuffix)};
		}

		std::uint64_t writesIn(const detail::FileRead& file) {
			if (file.status != detail::FileRead::Status::Read) {
				return 0;
			}
			const Envelope envelope = parseWhole(file.bytes);
			return envelope.status == Envelope::Status::Intact ? envelope.writes : 0;
		}

		// The save it replaces copied over the backup when it's intact, or aside when it's damaged, then
		// the new save; each a safe write, so the save is whole throughout. Stops at the first failure.
		bool writeSave(const SlotFiles& files, int version, std::span<const std::byte> payload) {
			const detail::FileRead current = detail::readFile(files.save);
			const detail::FileRead backup = detail::readFile(files.backup);
			const std::uint64_t	   writes = std::max(writesIn(current), writesIn(backup)) + 1;
			if (current.status == detail::FileRead::Status::Read) {
				const bool intact = parseWhole(current.bytes).status != Envelope::Status::Damaged;
				if (!intact) {
					LOG_WARNING(
						Engine, "SaveStore: %s is damaged; copied aside, not kept as the backup", detail::pathText(files.save).c_str()
					);
				}
				if (!detail::writeFileSafely(intact ? files.backup : files.corrupt, current.bytes)) {
					return false;
				}
			}
			return detail::writeFileSafely(files.save, encode(version, writes, payload));
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
		return writeSave(filesFor(root, slot), format.version, bytes);
	}

	Expected<std::vector<std::byte>, SaveStore::LoadError> SaveStore::read(std::string_view slot) {
		if (!usable(slot, "read")) {
			return Unexpected(LoadError::Missing);
		}
		const SlotFiles		   files = filesFor(root, slot);
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
					if (validFileName(name)) {
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
				info.written = std::chrono::system_clock::time_point(
					std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::microseconds(envelope.written))
				);
			}
			infos.push_back(std::move(info));
		}
		return infos;
	}

	bool SaveStore::restoreBackup(std::string_view slot) {
		if (!usable(slot, "restoreBackup")) {
			return false;
		}
		const SlotFiles		   files = filesFor(root, slot);
		const std::string	   name = detail::pathText(files.save);
		const detail::FileRead backup = detail::readFile(files.backup);
		if (backup.status != detail::FileRead::Status::Read || parseWhole(backup.bytes).status == Envelope::Status::Damaged) {
			LOG_WARNING(Engine, "SaveStore: %s has no backup to restore, or a damaged one", name.c_str());
			return false;
		}

		const detail::FileRead current = detail::readFile(files.save);
		const bool			   currentIntact =
			current.status == detail::FileRead::Status::Read && parseWhole(current.bytes).status != Envelope::Status::Damaged;
		if (!currentIntact) {
			if (current.status != detail::FileRead::Status::Missing && !detail::moveAside(files.save, kCorruptSuffix)) {
				return false;
			}
			std::error_code error;
			std::filesystem::rename(files.backup, files.save, error);
			if (error) {
				LOG_ERROR(Engine, "SaveStore: can't restore %s's backup: %s", name.c_str(), error.message().c_str());
				return false;
			}
			LOG_INFO(Engine, "SaveStore: %s restored from its backup", name.c_str());
			return true;
		}

		if (!detail::writeFileSafely(files.save, backup.bytes)) {
			return false;
		}
		if (!detail::writeFileSafely(files.backup, current.bytes)) {
			LOG_ERROR(Engine, "SaveStore: %s is restored, but the save it replaced couldn't be kept as the backup", name.c_str());
		}
		LOG_INFO(Engine, "SaveStore: %s and its backup swapped", name.c_str());
		return true;
	}

	bool SaveStore::remove(std::string_view slot) {
		if (!usable(slot, "remove")) {
			return false;
		}
		const SlotFiles files = filesFor(root, slot);
		bool			removed = true;
		for (const std::filesystem::path& file :
			 {files.save,
			  files.backup,
			  files.corrupt,
			  detail::tempFor(files.save),
			  detail::tempFor(files.backup),
			  detail::tempFor(files.corrupt)}) {
			std::error_code error;
			std::filesystem::remove(file, error);
			if (error) {
				LOG_ERROR(Engine, "SaveStore: can't remove %s: %s", detail::pathText(file).c_str(), error.message().c_str());
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
		if (!validFileName(slot)) {
			LOG_ERROR(Engine, "SaveStore::%s: \"%.*s\" isn't a valid slot name", what, static_cast<int>(slot.size()), slot.data());
			return false;
		}
		return true;
	}

} // namespace app
