// Saves: the envelope byte for byte, backups and restoring them, upgrades across versions, a failed
// upgrade and a newer save leaving every file as it was, crashes between the temp file and the rename,
// the JSON pair, slots(), remove, and which slot names are refused.

#include "saves/SaveStore.h"
#include "detail/SafeFile.h"
#include "tests/TempDir.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace app {

	namespace {

		using LoadError = SaveStore::LoadError;
		using Upgrade = std::function<bool(std::vector<std::byte>&)>;
		using testing::bytesOf;
		using testing::filesIn;
		using testing::readBytes;
		using testing::TempDir;
		using testing::textOf;
		using testing::writeBytes;
		using testing::writeText;

		// Bit by bit, not by table, so it checks the store's rather than repeating it.
		std::uint32_t referenceCrc(std::span<const std::byte> bytes) {
			std::uint32_t crc = 0xFFFFFFFFU;
			for (const std::byte b : bytes) {
				crc ^= std::to_integer<std::uint32_t>(b);
				for (int bit = 0; bit < 8; ++bit) {
					crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
				}
			}
			return ~crc;
		}

		std::uint64_t littleEndian(const std::vector<std::byte>& file, std::size_t offset, std::size_t size) {
			std::uint64_t value = 0;
			for (std::size_t i = 0; i < size; ++i) {
				value |= std::to_integer<std::uint64_t>(file.at(offset + i)) << (8U * i);
			}
			return value;
		}

		int versionIn(const std::vector<std::byte>& file) {
			return static_cast<std::int32_t>(littleEndian(file, 12, 4));
		}

		std::uint64_t writesIn(const std::vector<std::byte>& file) {
			return littleEndian(file, 16, 8);
		}

		std::string text(const Expected<std::vector<std::byte>, LoadError>& read) {
			return read ? textOf(*read) : "error " + std::to_string(static_cast<int>(read.error()));
		}

		// An upgrade step that appends its mark and notes the version it started from.
		Upgrade appending(std::string mark, int from, std::vector<int>& calls) {
			return [mark = std::move(mark), from, &calls](std::vector<std::byte>& bytes) {
				calls.push_back(from);
				const std::vector<std::byte> added = bytesOf(mark);
				bytes.insert(bytes.end(), added.begin(), added.end());
				return true;
			};
		}

		// Sets a SafeFile fault for a test and clears it however the test ends.
		class Fault {
		  public:
			explicit Fault(std::string failing)
				: name(std::move(failing)) {
				detail::setFaultForTests([this](const std::filesystem::path& target) { return target.filename().string() == name; });
			}
			~Fault() { detail::setFaultForTests({}); }
			Fault(const Fault&) = delete;
			Fault& operator=(const Fault&) = delete;
			Fault(Fault&&) = delete;
			Fault& operator=(Fault&&) = delete;

		  private:
			std::string name;
		};

		// A temp file a write would have left, made old enough to be swept.
		void staleTemp(const std::filesystem::path& file, std::string_view text) {
			writeText(file, text);
			std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() - std::chrono::hours(1));
		}

		class SaveStoreTest : public ::testing::Test {
		  protected:
			SaveStore store(int version, std::vector<Upgrade> upgrades = {}) const {
				return {temp.path(), {.version = version, .upgrades = std::move(upgrades)}};
			}
			std::filesystem::path file(std::string_view name) const { return temp / name; }

			TempDir temp;
		};

	} // namespace

	TEST(SaveStoreCrcTest, TheReferenceCrcIsZlibs) {
		EXPECT_EQ(referenceCrc(bytesOf("123456789")), 0xCBF43926U);
	}

	TEST_F(SaveStoreTest, AWriteIsReadBackAndTheFileIsTheDocumentedEnvelope) {
		SaveStore  saves = store(3);
		const auto before = std::chrono::system_clock::now();
		ASSERT_TRUE(saves.write("slot1", bytesOf("hello")));
		const auto after = std::chrono::system_clock::now();
		EXPECT_EQ(text(saves.read("slot1")), "hello");
		EXPECT_EQ(filesIn(temp.path()), std::vector<std::string>{"slot1.save"});

		const std::vector<std::byte> envelope = readBytes(file("slot1.save"));
		ASSERT_EQ(envelope.size(), 40U + 5U + 4U);
		EXPECT_EQ(textOf(std::span(envelope).first(8)), "SALALSAV");
		EXPECT_EQ(littleEndian(envelope, 8, 4), 1U); // envelope version
		EXPECT_EQ(versionIn(envelope), 3);			 // the game's
		EXPECT_EQ(writesIn(envelope), 1U);
		const auto written = static_cast<std::int64_t>(littleEndian(envelope, 24, 8));
		const auto micros = [](std::chrono::system_clock::time_point t) {
			return std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
		};
		EXPECT_GE(written, micros(before));
		EXPECT_LE(written, micros(after));
		EXPECT_EQ(littleEndian(envelope, 32, 8), 5U);
		EXPECT_EQ(textOf(std::span(envelope).subspan(40, 5)), "hello");
		EXPECT_EQ(littleEndian(envelope, 45, 4), referenceCrc(std::span(envelope).first(45)));
	}

	TEST_F(SaveStoreTest, TheSaveAWriteReplacesIsKeptAsTheBackupAndTheCounterGoesOn) {
		SaveStore saves = store(1);
		ASSERT_TRUE(saves.write("slot", bytesOf("a")));
		const std::vector<std::byte> first = readBytes(file("slot.save"));
		ASSERT_TRUE(saves.write("slot", bytesOf("b")));
		EXPECT_EQ(readBytes(file("slot.save.bak")), first);
		const std::vector<std::byte> second = readBytes(file("slot.save"));
		ASSERT_TRUE(saves.write("slot", bytesOf("c")));
		EXPECT_EQ(readBytes(file("slot.save.bak")), second);
		EXPECT_EQ(text(saves.read("slot")), "c");
		EXPECT_EQ(writesIn(readBytes(file("slot.save"))), 3U);
		EXPECT_EQ(filesIn(temp.path()), (std::vector<std::string>{"slot.save", "slot.save.bak"}));
	}

	TEST_F(SaveStoreTest, RestoreBackupSwapsSoASecondCallUndoesIt) {
		SaveStore saves = store(1);
		ASSERT_TRUE(saves.write("slot", bytesOf("a")));
		ASSERT_TRUE(saves.write("slot", bytesOf("b")));
		ASSERT_TRUE(saves.restoreBackup("slot"));
		EXPECT_EQ(text(saves.read("slot")), "a");
		ASSERT_TRUE(saves.restoreBackup("slot"));
		EXPECT_EQ(text(saves.read("slot")), "b");
		ASSERT_TRUE(saves.restoreBackup("slot"));
		// The counter goes on from the higher of the two, so it never repeats.
		ASSERT_TRUE(saves.write("slot", bytesOf("c")));
		EXPECT_EQ(writesIn(readBytes(file("slot.save"))), 3U);
		EXPECT_EQ(writesIn(readBytes(file("slot.save.bak"))), 1U);
	}

	TEST_F(SaveStoreTest, RestoreBackupWithNoBackupOrADamagedOneChangesNothing) {
		SaveStore saves = store(1);
		EXPECT_FALSE(saves.restoreBackup("slot"));
		ASSERT_TRUE(saves.write("slot", bytesOf("a")));
		EXPECT_FALSE(saves.restoreBackup("slot"));
		EXPECT_EQ(text(saves.read("slot")), "a");

		ASSERT_TRUE(saves.write("slot", bytesOf("b")));
		std::vector<std::byte> backup = readBytes(file("slot.save.bak"));
		backup[40] ^= std::byte{1}; // the payload no longer matches its CRC
		writeBytes(file("slot.save.bak"), backup);
		const std::vector<std::byte> save = readBytes(file("slot.save"));
		EXPECT_FALSE(saves.restoreBackup("slot"));
		EXPECT_EQ(readBytes(file("slot.save")), save);
		EXPECT_EQ(readBytes(file("slot.save.bak")), backup);
	}

	TEST_F(SaveStoreTest, WithNoSaveOrADamagedOneTheBackupMovesIntoPlace) {
		SaveStore saves = store(1);
		ASSERT_TRUE(saves.write("slot", bytesOf("a")));
		ASSERT_TRUE(saves.write("slot", bytesOf("b")));
		std::filesystem::remove(file("slot.save"));
		EXPECT_EQ(saves.read("slot").error(), LoadError::Missing); // a backup alone isn't read
		ASSERT_TRUE(saves.restoreBackup("slot"));
		EXPECT_EQ(text(saves.read("slot")), "a");
		EXPECT_EQ(filesIn(temp.path()), std::vector<std::string>{"slot.save"});

		ASSERT_TRUE(saves.write("slot", bytesOf("c")));
		std::vector<std::byte> damaged = readBytes(file("slot.save"));
		damaged.resize(damaged.size() / 2);
		writeBytes(file("slot.save"), damaged);
		ASSERT_TRUE(saves.restoreBackup("slot"));
		EXPECT_EQ(text(saves.read("slot")), "a");
		EXPECT_EQ(readBytes(file("slot.save.corrupt")), damaged);
		EXPECT_EQ(filesIn(temp.path()), (std::vector<std::string>{"slot.save", "slot.save.corrupt"}));
	}

	TEST_F(SaveStoreTest, ASaveIsUpgradedAcrossThreeVersionsInOrderAndOnce) {
		ASSERT_TRUE(store(1).write("slot", bytesOf("start")));
		const std::vector<std::byte> original = readBytes(file("slot.save"));

		std::vector<int> calls;
		SaveStore		 saves = store(4, {appending("+2", 1, calls), appending("+3", 2, calls), appending("+4", 3, calls)});
		EXPECT_EQ(text(saves.read("slot")), "start+2+3+4");
		EXPECT_EQ(calls, (std::vector<int>{1, 2, 3}));

		// Written back at the build's version, with the pre-upgrade file as the backup.
		const std::vector<std::byte> upgraded = readBytes(file("slot.save"));
		EXPECT_EQ(versionIn(upgraded), 4);
		EXPECT_EQ(writesIn(upgraded), 2U);
		EXPECT_EQ(readBytes(file("slot.save.bak")), original);

		EXPECT_EQ(text(saves.read("slot")), "start+2+3+4");
		EXPECT_EQ(calls, (std::vector<int>{1, 2, 3}));
		ASSERT_TRUE(saves.restoreBackup("slot")); // and the original comes back, upgraded again when read
		EXPECT_EQ(text(saves.read("slot")), "start+2+3+4");
		EXPECT_EQ(calls, (std::vector<int>{1, 2, 3, 1, 2, 3}));
	}

	TEST_F(SaveStoreTest, ASaveFromTheMiddleRunsOnlyTheStepsAfterIt) {
		std::vector<int> calls;
		ASSERT_TRUE(store(3).write("slot", bytesOf("mid")));
		SaveStore saves = store(4, {appending("+2", 1, calls), appending("+3", 2, calls), appending("+4", 3, calls)});
		EXPECT_EQ(text(saves.read("slot")), "mid+4");
		EXPECT_EQ(calls, std::vector<int>{3});
	}

	TEST_F(SaveStoreTest, AFailedUpgradeLeavesEveryFileAsItWas) {
		ASSERT_TRUE(store(1).write("slot", bytesOf("a")));
		ASSERT_TRUE(store(1).write("slot", bytesOf("b")));
		const std::vector<std::byte>   save = readBytes(file("slot.save"));
		const std::vector<std::byte>   backup = readBytes(file("slot.save.bak"));
		const std::vector<std::string> listing = filesIn(temp.path());

		std::vector<int> calls;
		const auto		 refuses = [](std::vector<std::byte>& bytes) {
			  bytes.clear(); // what a step does to the bytes before it fails is never written
			  return false;
		};
		const auto throws = [](std::vector<std::byte>& /*bytes*/) -> bool {
			throw std::runtime_error("bad save");
		};
		const std::vector<std::vector<Upgrade>> failing{
			{appending("+2", 1, calls), refuses},
			{appending("+2", 1, calls), throws},
			{appending("+2", 1, calls)}, // no step from 2
			{appending("+2", 1, calls), Upgrade{}},
		};
		for (const std::vector<Upgrade>& upgrades : failing) {
			SaveStore saves = store(3, upgrades);
			EXPECT_EQ(saves.read("slot").error(), LoadError::UpgradeFailed);
			EXPECT_EQ(readBytes(file("slot.save")), save);
			EXPECT_EQ(readBytes(file("slot.save.bak")), backup);
			EXPECT_EQ(filesIn(temp.path()), listing);
		}
		EXPECT_EQ(calls, (std::vector<int>{1, 1, 1, 1}));
	}

	TEST_F(SaveStoreTest, ANewerSaveIsRefusedAndUntouched) {
		ASSERT_TRUE(store(5).write("slot", bytesOf("future")));
		ASSERT_TRUE(store(5).write("slot", bytesOf("future 2")));
		const std::vector<std::byte> save = readBytes(file("slot.save"));
		const std::vector<std::byte> backup = readBytes(file("slot.save.bak"));
		SaveStore					 saves = store(3);
		EXPECT_EQ(saves.read("slot").error(), LoadError::NewerThanBuild);
		EXPECT_EQ(saves.readJson("slot").error(), LoadError::NewerThanBuild);
		EXPECT_EQ(readBytes(file("slot.save")), save);
		EXPECT_EQ(readBytes(file("slot.save.bak")), backup);
		EXPECT_EQ(filesIn(temp.path()), (std::vector<std::string>{"slot.save", "slot.save.bak"}));
		const std::vector<SlotInfo> slots = saves.slots();
		ASSERT_EQ(slots.size(), 1U);
		EXPECT_EQ(slots[0].version, 5);
		EXPECT_TRUE(slots[0].intact);

		// A later envelope version: its first 16 bytes still say whose save it is.
		std::vector<std::byte> later = save;
		later[8] = std::byte{2};
		writeBytes(file("slot.save"), later);
		EXPECT_EQ(saves.read("slot").error(), LoadError::NewerThanBuild);
		EXPECT_EQ(readBytes(file("slot.save")), later);
		EXPECT_EQ(saves.slots()[0].version, 5);
	}

	TEST_F(SaveStoreTest, AWriteOverANewerBuildsSaveKeepsItUnderItsOwnName) {
		// A beta at format 5 saved; the stable build, at 3, autosaves twice over it.
		ASSERT_TRUE(store(5).write("slot", bytesOf("beta")));
		const std::vector<std::byte> beta = readBytes(file("slot.save"));
		SaveStore					 stable = store(3);
		EXPECT_EQ(stable.read("slot").error(), LoadError::NewerThanBuild);
		ASSERT_TRUE(stable.write("slot", bytesOf("stable 1")));
		ASSERT_TRUE(stable.write("slot", bytesOf("stable 2")));
		EXPECT_EQ(readBytes(file("slot.save.v5")), beta);
		EXPECT_EQ(text(stable.read("slot")), "stable 2");
		EXPECT_EQ(versionIn(readBytes(file("slot.save.bak"))), 3);
		EXPECT_EQ(filesIn(temp.path()), (std::vector<std::string>{"slot.save", "slot.save.bak", "slot.save.v5"}));
	}

	TEST_F(SaveStoreTest, AKeptNewerSaveIsReplacedOnlyByOneWrittenLater) {
		// A beta at format 5 writes twice, so its save is B and its backup A.
		SaveStore beta = store(5);
		ASSERT_TRUE(beta.write("slot", bytesOf("A")));
		ASSERT_TRUE(beta.write("slot", bytesOf("B")));
		const std::vector<std::byte> b = readBytes(file("slot.save"));
		SaveStore					 stable = store(3);
		ASSERT_TRUE(stable.write("slot", bytesOf("X"))); // B kept as .v5; the backup is still A
		EXPECT_EQ(readBytes(file("slot.save.v5")), b);
		ASSERT_TRUE(stable.write("slot", bytesOf("Y"))); // A is a newer build's backup, older than B
		EXPECT_EQ(readBytes(file("slot.save.v5")), b);
		EXPECT_EQ(versionIn(readBytes(file("slot.save.bak"))), 3);

		// The beta comes back and saves C, later than B; the stable build keeps C over B.
		std::vector<int> calls;
		SaveStore		 back =
			store(5, {appending("+2", 1, calls), appending("+3", 2, calls), appending("+4", 3, calls), appending("+5", 4, calls)});
		ASSERT_TRUE(back.write("slot", bytesOf("C")));
		const std::vector<std::byte> later = readBytes(file("slot.save"));
		ASSERT_TRUE(stable.write("slot", bytesOf("Z")));
		EXPECT_EQ(readBytes(file("slot.save.v5")), later);
	}

	TEST_F(SaveStoreTest, ANewerBuildsBackupIsKeptUnderItsOwnNameBeforeItsReplaced) {
		ASSERT_TRUE(store(7).write("other", bytesOf("future")));
		const std::vector<std::byte> future = readBytes(file("other.save"));
		SaveStore					 saves = store(3);
		ASSERT_TRUE(saves.write("slot", bytesOf("s1")));
		const std::vector<std::byte> first = readBytes(file("slot.save"));
		writeBytes(file("slot.save.bak"), future);
		ASSERT_TRUE(saves.write("slot", bytesOf("s2")));
		EXPECT_EQ(readBytes(file("slot.save.v7")), future);
		EXPECT_EQ(readBytes(file("slot.save.bak")), first);
	}

	TEST_F(SaveStoreTest, AnEnvelopeVersionFarPastThisOnesIsDamagedNotNewer) {
		SaveStore saves = store(1);
		ASSERT_TRUE(saves.write("slot", bytesOf("good")));
		const std::vector<std::byte> good = readBytes(file("slot.save"));
		ASSERT_TRUE(saves.write("slot", bytesOf("flipped")));
		std::vector<std::byte> flipped = readBytes(file("slot.save"));
		flipped[8] |= std::byte{0x40}; // envelope version 1 becomes 65
		writeBytes(file("slot.save"), flipped);
		EXPECT_EQ(saves.read("slot").error(), LoadError::Corrupt);
		EXPECT_FALSE(saves.slots().at(0).intact);
		ASSERT_TRUE(saves.write("slot", bytesOf("next")));
		EXPECT_EQ(readBytes(file("slot.save.bak")), good); // not pushed out by the damaged save
		EXPECT_EQ(readBytes(file("slot.save.corrupt")), flipped);
	}

	TEST_F(SaveStoreTest, ASaveThatCannotBeReadIsSetAsideBeforeItsReplaced) {
		SaveStore saves = store(1);
		std::filesystem::create_directories(file("slot.save") / "inside"); // nothing can read this as a save
		EXPECT_EQ(saves.read("slot").error(), LoadError::Corrupt);
		ASSERT_TRUE(saves.write("slot", bytesOf("new")));
		EXPECT_EQ(text(saves.read("slot")), "new");
		EXPECT_TRUE(std::filesystem::is_directory(file("slot.save.corrupt") / "inside"));
		EXPECT_FALSE(std::filesystem::exists(file("slot.save.bak")));

		// And restoreBackup over one renames it aside the same way.
		ASSERT_TRUE(saves.write("slot", bytesOf("newer")));
		std::filesystem::remove(file("slot.save"));
		std::filesystem::create_directories(file("slot.save") / "again");
		ASSERT_TRUE(saves.restoreBackup("slot"));
		EXPECT_EQ(text(saves.read("slot")), "new");
		EXPECT_TRUE(std::filesystem::is_directory(file("slot.save.corrupt.1") / "again"));
	}

	TEST_F(SaveStoreTest, AFileThatCannotBeReadIsLinkedAsideSoItsNameAlwaysHoldsASave) {
#ifdef _WIN32
		GTEST_SKIP() << "a file this process can't read but can link needs POSIX permissions";
#else
		if (geteuid() == 0) {
			GTEST_SKIP() << "root reads any file";
		}
		SaveStore saves = store(1);
		ASSERT_TRUE(saves.write("slot", bytesOf("locked")));
		const std::vector<std::byte> locked = readBytes(file("slot.save"));
		std::filesystem::permissions(file("slot.save"), std::filesystem::perms::none);
		EXPECT_EQ(saves.read("slot").error(), LoadError::Corrupt);
		ASSERT_TRUE(saves.write("slot", bytesOf("new")));
		std::filesystem::permissions(file("slot.save.corrupt"), std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
		EXPECT_EQ(readBytes(file("slot.save.corrupt")), locked); // the same file, linked before the rename
		EXPECT_EQ(text(saves.read("slot")), "new");
		EXPECT_FALSE(std::filesystem::exists(file("slot.save.bak")));
#endif
	}

	TEST_F(SaveStoreTest, RestoreBackupKeepsTheSaveItReplacesOnDiskWhenTheLastStepFails) {
		SaveStore saves = store(1);
		ASSERT_TRUE(saves.write("slot", bytesOf("older")));
		const std::vector<std::byte> older = readBytes(file("slot.save"));
		ASSERT_TRUE(saves.write("slot", bytesOf("NEWEST")));
		const std::vector<std::byte> newest = readBytes(file("slot.save"));
		{
			// The staged save's rename over the backup fails (a held file, a full disk). The save is the
			// backup's, which is what was asked for.
			const Fault fault("slot.save.bak");
			EXPECT_TRUE(saves.restoreBackup("slot"));
		}
		EXPECT_EQ(readBytes(file("slot.save")), older);
		EXPECT_EQ(readBytes(file("slot.save.swap")), newest);
		EXPECT_FALSE(std::filesystem::exists(file("slot.save.bak")));

		// A retry finishes the restore without undoing it; a restore after that swaps back.
		EXPECT_TRUE(saves.restoreBackup("slot"));
		EXPECT_EQ(text(saves.read("slot")), "older");
		EXPECT_EQ(readBytes(file("slot.save.bak")), newest);
		EXPECT_EQ(filesIn(temp.path()), (std::vector<std::string>{"slot.save", "slot.save.bak"}));
		ASSERT_TRUE(saves.restoreBackup("slot"));
		EXPECT_EQ(text(saves.read("slot")), "NEWEST");
	}

	TEST_F(SaveStoreTest, RestoreBackupAfterACrashBetweenItsRenamesFinishesItAndDoesNotSwapAgain) {
		SaveStore saves = store(1);
		ASSERT_TRUE(saves.write("slot", bytesOf("older")));
		ASSERT_TRUE(saves.write("slot", bytesOf("newest")));
		const std::vector<std::byte> newest = readBytes(file("slot.save"));
		// Staged, the backup renamed over the save, then the crash: the player asks again.
		std::filesystem::rename(file("slot.save"), file("slot.save.swap"));
		std::filesystem::rename(file("slot.save.bak"), file("slot.save"));
		EXPECT_TRUE(saves.restoreBackup("slot"));
		EXPECT_EQ(text(saves.read("slot")), "older");
		EXPECT_EQ(readBytes(file("slot.save.bak")), newest);
	}

	TEST_F(SaveStoreTest, RestoreBackupWhoseEarlierStepsFailChangesNothing) {
		SaveStore saves = store(1);
		ASSERT_TRUE(saves.write("slot", bytesOf("older")));
		ASSERT_TRUE(saves.write("slot", bytesOf("newest")));
		const std::vector<std::byte> save = readBytes(file("slot.save"));
		const std::vector<std::byte> backup = readBytes(file("slot.save.bak"));
		for (const char* failing : {"slot.save.swap", "slot.save"}) {
			const Fault fault(failing);
			EXPECT_FALSE(saves.restoreBackup("slot")) << failing;
			EXPECT_EQ(readBytes(file("slot.save")), save) << failing;
			EXPECT_EQ(readBytes(file("slot.save.bak")), backup) << failing;
			EXPECT_EQ(filesIn(temp.path()), (std::vector<std::string>{"slot.save", "slot.save.bak"})) << failing;
		}
	}

	TEST_F(SaveStoreTest, ASwapFileACrashLeftIsFinishedOrDroppedOrKept) {
		SaveStore saves = store(1);
		ASSERT_TRUE(saves.write("slot", bytesOf("older")));
		ASSERT_TRUE(saves.write("slot", bytesOf("newest")));
		const std::vector<std::byte> save = readBytes(file("slot.save"));
		const std::vector<std::byte> backup = readBytes(file("slot.save.bak"));

		// Staged, then the crash: the swap file is a copy of the save, and goes.
		writeBytes(file("slot.save.swap"), save);
		EXPECT_EQ(text(saves.read("slot")), "newest");
		EXPECT_EQ(filesIn(temp.path()), (std::vector<std::string>{"slot.save", "slot.save.bak"}));

		// The backup renamed over the save, then the crash: the swap file becomes the backup.
		std::filesystem::rename(file("slot.save.bak"), file("slot.save"));
		writeBytes(file("slot.save.swap"), save);
		ASSERT_TRUE(saves.write("slot", bytesOf("after")));
		EXPECT_EQ(readBytes(file("slot.save.bak")), backup); // the restored save, kept as the write's backup
		EXPECT_FALSE(std::filesystem::exists(file("slot.save.swap")));

		// One that matches nothing is kept, and remove() takes it.
		writeText(file("slot.save.swap"), "who knows");
		EXPECT_EQ(text(saves.read("slot")), "after");
		EXPECT_TRUE(std::filesystem::exists(file("slot.save.swap")));
		EXPECT_TRUE(saves.remove("slot"));
		EXPECT_TRUE(filesIn(temp.path()).empty());
	}

	TEST_F(SaveStoreTest, AHeaderTimeTheClockCannotHoldIsZero) {
		SaveStore saves = store(1);
		ASSERT_TRUE(saves.write("slot", bytesOf("x")));
		const std::vector<std::byte> good = readBytes(file("slot.save"));
		const auto					 timeIn = [&](std::uint64_t microseconds) {
			  std::vector<std::byte> patched = good;
			  for (std::size_t i = 0; i < 8; ++i) {
				  patched[24 + i] = static_cast<std::byte>((microseconds >> (8U * i)) & 0xFFU);
			  }
			  writeBytes(file("slot.save"), patched);
			  const std::vector<SlotInfo> slots = saves.slots();
			  EXPECT_TRUE(slots.at(0).intact); // slots() reads headers alone
			  return slots.at(0).written;
		};
		using std::chrono::system_clock;
		EXPECT_EQ(timeIn(static_cast<std::uint64_t>(-1)), system_clock::time_point{}); // before 1970: never written

		// The largest i64: past what the clock holds where its ticks are finer than microseconds (Windows,
		// Linux), the time itself where they're microseconds (macOS); never an overflow.
		constexpr std::int64_t kLargest = std::numeric_limits<std::int64_t>::max();
		const auto			   latest = std::chrono::duration_cast<std::chrono::microseconds>(system_clock::duration::max()).count();
		const system_clock::time_point largest = timeIn(static_cast<std::uint64_t>(kLargest));
		if (latest < kLargest) {
			EXPECT_EQ(largest, system_clock::time_point{});
		} else {
			EXPECT_EQ(std::chrono::duration_cast<std::chrono::microseconds>(largest.time_since_epoch()).count(), kLargest);
		}
	}

	TEST_F(SaveStoreTest, ACrashBetweenTheTempFileAndTheRenameLeavesTheLastGoodSave) {
		SaveStore saves = store(1);
		ASSERT_TRUE(saves.write("slot", bytesOf("good")));
		const std::vector<std::byte> good = readBytes(file("slot.save"));

		// Died writing the temp file: a partial one beside an untouched save.
		staleTemp(file("slot.save.4242.0.tmp"), "SALALSAV\x01");
		EXPECT_EQ(text(saves.read("slot")), "good");

		// Died after the backup was copied and the new save flushed, before its rename.
		ASSERT_TRUE(store(1).write("other", bytesOf("newer")));
		std::filesystem::rename(file("other.save"), file("slot.save.4242.1.tmp"));
		std::filesystem::last_write_time(
			file("slot.save.4242.1.tmp"), std::filesystem::file_time_type::clock::now() - std::chrono::hours(1)
		);
		writeBytes(file("slot.save.bak"), good);
		EXPECT_EQ(text(saves.read("slot")), "good");
		const std::vector<SlotInfo> slots = saves.slots();
		ASSERT_EQ(slots.size(), 1U);
		EXPECT_EQ(slots[0].name, "slot");

		// The next write goes through as if nothing had happened.
		ASSERT_TRUE(saves.write("slot", bytesOf("next")));
		EXPECT_EQ(text(saves.read("slot")), "next");
		EXPECT_EQ(readBytes(file("slot.save.bak")), good);
		EXPECT_EQ(filesIn(temp.path()), (std::vector<std::string>{"slot.save", "slot.save.bak"}));
	}

	TEST_F(SaveStoreTest, AnInterruptedWriteIsCorruptAndItsBackupBringsBackTheLastGoodSave) {
		SaveStore saves = store(1);
		ASSERT_TRUE(saves.write("slot", bytesOf("good")));
		ASSERT_TRUE(saves.write("slot", bytesOf("lost")));
		std::vector<std::byte> cut = readBytes(file("slot.save"));
		cut.resize(30); // a disk that kept half the write
		writeBytes(file("slot.save"), cut);
		EXPECT_EQ(saves.read("slot").error(), LoadError::Corrupt);
		EXPECT_FALSE(saves.slots().at(0).intact);
		ASSERT_TRUE(saves.restoreBackup("slot"));
		EXPECT_EQ(text(saves.read("slot")), "good");
	}

	TEST_F(SaveStoreTest, AWriteOverADamagedSaveCopiesItAsideAndKeepsTheGoodBackup) {
		SaveStore saves = store(1);
		ASSERT_TRUE(saves.write("slot", bytesOf("good")));
		const std::vector<std::byte> good = readBytes(file("slot.save"));
		ASSERT_TRUE(saves.write("slot", bytesOf("flipped")));
		std::vector<std::byte> damaged = readBytes(file("slot.save"));
		damaged[42] ^= std::byte{0x20};
		writeBytes(file("slot.save"), damaged);
		EXPECT_EQ(saves.read("slot").error(), LoadError::Corrupt);

		ASSERT_TRUE(saves.write("slot", bytesOf("new")));
		EXPECT_EQ(text(saves.read("slot")), "new");
		EXPECT_EQ(readBytes(file("slot.save.bak")), good);
		EXPECT_EQ(readBytes(file("slot.save.corrupt")), damaged);
		EXPECT_EQ(writesIn(readBytes(file("slot.save"))), 2U); // after the backup's 1; the damaged one's can't be read
	}

	TEST_F(SaveStoreTest, JsonGoesThroughAsItsText) {
		SaveStore			 saves = store(2);
		const nlohmann::json game = nlohmann::json::parse(R"({"seed": 12345, "ore": [3, 1], "name": "Kevin"})");
		ASSERT_TRUE(saves.writeJson("run", game));
		EXPECT_EQ(saves.readJson("run").value_or(nullptr), game);
		EXPECT_EQ(text(saves.read("run")), game.dump());

		ASSERT_TRUE(saves.write("binary", bytesOf(std::string("\x00\x01\xFF", 3))));
		EXPECT_EQ(saves.readJson("binary").error(), LoadError::Corrupt);
		EXPECT_EQ(saves.readJson("none").error(), LoadError::Missing);

		EXPECT_FALSE(saves.writeJson("bad", nlohmann::json(std::string("\xFF\xFE")))); // not UTF-8
		EXPECT_FALSE(std::filesystem::exists(file("bad.save")));
	}

	TEST_F(SaveStoreTest, SlotsListEverySlotWithASaveOrABackupByName) {
		SaveStore  saves = store(2);
		const auto before = std::chrono::system_clock::now();
		ASSERT_TRUE(saves.write("b", bytesOf("1")));
		ASSERT_TRUE(saves.write("a", bytesOf("1")));
		ASSERT_TRUE(saves.write("a", bytesOf("2")));
		ASSERT_TRUE(saves.write("c.2", bytesOf("1")));
		ASSERT_TRUE(saves.write("d", bytesOf("1")));
		ASSERT_TRUE(saves.write("d", bytesOf("2")));
		std::filesystem::remove(file("d.save")); // a backup alone
		ASSERT_TRUE(saves.write("e", bytesOf("1")));
		writeText(file("e.save"), "SALALSAV"); // damaged
		const auto after = std::chrono::system_clock::now();
		for (const char* stray : {"notes.txt", "f.save.tmp", ".save", "bad name!.save", "g.save.corrupt", "h.sav"}) {
			writeText(file(stray), "x");
		}

		const std::vector<SlotInfo> slots = saves.slots();
		std::vector<std::string>	names;
		for (const SlotInfo& slot : slots) {
			names.push_back(slot.name);
		}
		EXPECT_EQ(names, (std::vector<std::string>{"a", "b", "c.2", "d", "e"}));
		EXPECT_EQ(slots[0].version, 2);
		EXPECT_EQ(slots[0].writes, 2U);
		EXPECT_TRUE(slots[0].intact);
		EXPECT_GE(slots[0].written, std::chrono::time_point_cast<std::chrono::microseconds>(before));
		EXPECT_LE(slots[0].written, after);
		EXPECT_EQ(slots[1].writes, 1U);
		EXPECT_TRUE(slots[2].intact);
		for (const SlotInfo& slot : {slots[3], slots[4]}) {
			EXPECT_FALSE(slot.intact) << slot.name;
			EXPECT_EQ(slot.version, 0) << slot.name;
			EXPECT_EQ(slot.writes, 0U) << slot.name;
			EXPECT_EQ(slot.written, std::chrono::system_clock::time_point{}) << slot.name;
		}

		EXPECT_TRUE(SaveStore(temp.path() / "never-written", {}).slots().empty());
	}

	TEST_F(SaveStoreTest, RemoveTakesEveryFileOfTheSlotAndNoOther) {
		SaveStore saves = store(1);
		ASSERT_TRUE(saves.write("slot", bytesOf("a")));
		ASSERT_TRUE(saves.write("slot", bytesOf("b")));
		ASSERT_TRUE(saves.write("keep", bytesOf("k")));
		for (const char* leftover :
			 {"slot.save.77.3.tmp",
			  "slot.save.bak.77.4.tmp",
			  "slot.save.swap",
			  "slot.save.corrupt",
			  "slot.save.corrupt.1",
			  "slot.save.corrupt.1.77.5.tmp",
			  "slot.save.v9",
			  "keep.save.v9"}) {
			writeText(file(leftover), "x");
		}
		writeText(file("slot.savex"), "not the slot's");
		writeText(file("slot.save.notes"), "not the slot's");
		EXPECT_TRUE(saves.remove("slot"));
		EXPECT_EQ(filesIn(temp.path()), (std::vector<std::string>{"keep.save", "keep.save.v9", "slot.save.notes", "slot.savex"}));
		EXPECT_EQ(saves.read("slot").error(), LoadError::Missing);
		EXPECT_FALSE(saves.restoreBackup("slot"));
		EXPECT_TRUE(saves.remove("slot")); // nothing left to remove is still gone
		EXPECT_EQ(text(saves.read("keep")), "k");
	}

	TEST_F(SaveStoreTest, ASlotNameThatIsNotAFileNameIsRefusedEverywhere) {
		SaveStore saves = store(1);
		for (const std::string& name : std::vector<std::string>{
				 "",
				 "../escape",
				 "a/b",
				 "a\\b",
				 "C:x",
				 "..",
				 ".hidden",
				 "end.",
				 " lead",
				 "CON",
				 "lpt1.x",
				 "nul .x",
				 "Kevin",
				 "slotA",
				 std::string(65, 's'),
				 std::string("nul\0x", 5)
			 }) {
			EXPECT_FALSE(saves.write(name, bytesOf("x"))) << name;
			EXPECT_FALSE(saves.writeJson(name, 1)) << name;
			EXPECT_EQ(saves.read(name).error(), LoadError::Missing) << name;
			EXPECT_EQ(saves.readJson(name).error(), LoadError::Missing) << name;
			EXPECT_FALSE(saves.restoreBackup(name)) << name;
			EXPECT_FALSE(saves.remove(name)) << name;
		}
		EXPECT_TRUE(filesIn(temp.path()).empty());
		EXPECT_FALSE(std::filesystem::exists(temp.path().parent_path() / "escape.save"));
		EXPECT_TRUE(saves.write(std::string(64, 's'), bytesOf("x")));
	}

	TEST_F(SaveStoreTest, SlotsThatDifferOnlyInCaseCannotExist) {
		SaveStore saves = store(1);
		EXPECT_TRUE(saves.write("kevin", bytesOf("kevin's")));
		EXPECT_FALSE(saves.write("Kevin", bytesOf("Kevin's")));
		EXPECT_EQ(saves.read("Kevin").error(), LoadError::Missing);
		EXPECT_EQ(text(saves.read("kevin")), "kevin's");
		writeText(file("Upper.save"), "copied from elsewhere");
		ASSERT_EQ(saves.slots().size(), 1U);
		EXPECT_EQ(saves.slots()[0].name, "kevin");
	}

	TEST_F(SaveStoreTest, AStoreWithNoDirectoryWritesNothingAndReadsNothing) {
		SaveStore saves;
		EXPECT_FALSE(saves.write("slot", bytesOf("x")));
		EXPECT_EQ(saves.read("slot").error(), LoadError::Missing);
		EXPECT_TRUE(saves.slots().empty());
		EXPECT_FALSE(saves.remove("slot"));
		EXPECT_TRUE(saves.directory().empty());
	}

	TEST_F(SaveStoreTest, TwoStoresOnOneDirectoryShareItLastWriteWins) {
		SaveStore first = store(1);
		SaveStore second = store(1);
		ASSERT_TRUE(first.write("slot", bytesOf("first")));
		ASSERT_TRUE(second.write("slot", bytesOf("second")));
		EXPECT_EQ(text(first.read("slot")), "second");
		EXPECT_EQ(writesIn(readBytes(file("slot.save"))), 2U);
		ASSERT_TRUE(first.restoreBackup("slot"));
		EXPECT_EQ(text(second.read("slot")), "first");
	}

	TEST_F(SaveStoreTest, AFormatVersionBelowOneIsOne) {
		ASSERT_TRUE(store(0).write("slot", bytesOf("x")));
		EXPECT_EQ(versionIn(readBytes(file("slot.save"))), 1);
		EXPECT_EQ(text(store(1).read("slot")), "x");
	}

	TEST(ExpectedTest, AValueOrAnErrorWithStdExpectedsNames) {
		Expected<std::string, LoadError> value(std::string("save"));
		EXPECT_TRUE(value.has_value());
		EXPECT_TRUE(static_cast<bool>(value));
		EXPECT_EQ(*value, "save");
		EXPECT_EQ(value->size(), 4U);
		EXPECT_EQ(value.value_or("fallback"), "save");

		const Expected<std::string, LoadError> error = Unexpected(LoadError::Corrupt);
		EXPECT_FALSE(error.has_value());
		EXPECT_FALSE(static_cast<bool>(error));
		EXPECT_EQ(error.error(), LoadError::Corrupt);
		EXPECT_EQ(error.value_or("fallback"), "fallback");
		EXPECT_THROW(static_cast<void>(error.value()), BadExpectedAccess);
		EXPECT_THROW(static_cast<void>(*error), BadExpectedAccess);

		std::string moved = std::move(value).value();
		EXPECT_EQ(moved, "save");
	}

} // namespace app
