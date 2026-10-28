// The safe write the settings file and saves share: through a temp file of its own renamed over the
// target, its directory made, stale temp files swept up and live ones left alone, a held target replaced
// on Windows; and reads that tell a missing file from one that can't be read.

#include "detail/SafeFile.h"
#include "tests/TempDir.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace app::detail {

	namespace {

		using testing::bytesOf;
		using testing::filesIn;
		using testing::readText;
		using testing::TempDir;
		using testing::writeText;

		// Clears the fault a test set, however the test ends.
		struct FaultReset {
			FaultReset() = default;
			FaultReset(const FaultReset&) = delete;
			FaultReset& operator=(const FaultReset&) = delete;
			FaultReset(FaultReset&&) = delete;
			FaultReset& operator=(FaultReset&&) = delete;
			~FaultReset() { setFaultForTests({}); }
		};

	} // namespace

	TEST(SafeFileTest, AWriteReplacesTheFileAndLeavesNoTempFile) {
		const TempDir temp;
		const auto	  file = temp / "settings.json";
		ASSERT_TRUE(writeFileSafely(file, bytesOf("first")));
		ASSERT_TRUE(writeFileSafely(file, bytesOf("second, longer")));
		EXPECT_EQ(readText(file), "second, longer");
		EXPECT_EQ(filesIn(temp.path()), std::vector<std::string>{"settings.json"});
	}

	TEST(SafeFileTest, AWriteMakesItsDirectory) {
		const TempDir temp;
		const auto	  file = temp.path() / "a" / "b" / "save";
		ASSERT_TRUE(writeFileSafely(file, bytesOf("x")));
		EXPECT_EQ(readText(file), "x");
	}

	TEST(SafeFileTest, TempFilesAreNamedForTheirTargetAndOneWrite) {
		EXPECT_EQ(tempTargetOf("slot.save.123.0.tmp"), "slot.save");
		EXPECT_EQ(tempTargetOf("slot.save.9.42.tmp"), "slot.save");
		EXPECT_EQ(tempTargetOf("slot.save.bak.123.0.tmp"), "slot.save.bak");
		EXPECT_EQ(tempTargetOf("slot.save.tmp"), std::nullopt);
		EXPECT_EQ(tempTargetOf("slot.save.123.tmp"), std::nullopt);
		EXPECT_EQ(tempTargetOf("slot.save.1a.0.tmp"), std::nullopt);
		EXPECT_EQ(tempTargetOf("slot.save.123.0.tmp.x"), std::nullopt);
		EXPECT_EQ(tempTargetOf(".1.2.tmp"), std::nullopt);
		EXPECT_EQ(tempTargetOf("settings.json"), std::nullopt);
	}

	TEST(SafeFileTest, AStaleTempFileFromACrashIsSweptAndALiveOneIsLeftAlone) {
		const TempDir temp;
		const auto	  file = temp / "settings.json";
		writeText(file, "good");
		writeText(temp / "settings.json.4242.0.tmp", "half writ"); // a write that died before its rename
		writeText(temp / "settings.json.4243.7.tmp", "being written");
		std::filesystem::last_write_time(
			temp / "settings.json.4242.0.tmp", std::filesystem::file_time_type::clock::now() - std::chrono::hours(1)
		);
		EXPECT_EQ(readText(file), "good");

		ASSERT_TRUE(writeFileSafely(file, bytesOf("new")));
		EXPECT_EQ(readText(file), "new");
		EXPECT_EQ(filesIn(temp.path()), (std::vector<std::string>{"settings.json", "settings.json.4243.7.tmp"}));
	}

	TEST(SafeFileTest, AWriteThatCannotHappenIsFalseAndLeavesNoTempFile) {
		const TempDir temp;
		const auto	  file = temp / "taken";
		std::filesystem::create_directories(file / "inside"); // a directory can't be replaced by a file
		EXPECT_FALSE(writeFileSafely(file, bytesOf("x")));
		EXPECT_TRUE(std::filesystem::is_directory(file));
		EXPECT_EQ(filesIn(temp.path()), std::vector<std::string>{"taken"});
	}

	TEST(SafeFileTest, AFaultSetByATestFailsTheWriteOrRenameOfItsTargetAlone) {
		const TempDir	 temp;
		const FaultReset reset;
		writeText(temp / "a", "old a");
		setFaultForTests([&](const std::filesystem::path& target) { return target.filename() == "a"; });
		EXPECT_FALSE(writeFileSafely(temp / "a", bytesOf("new")));
		EXPECT_TRUE(writeFileSafely(temp / "b", bytesOf("b")));
		writeText(temp / "c", "c");
		EXPECT_FALSE(renameReplacing(temp / "c", temp / "a"));
		EXPECT_EQ(readText(temp / "a"), "old a");
		EXPECT_EQ(filesIn(temp.path()), (std::vector<std::string>{"a", "b", "c"}));
	}

#ifdef _WIN32
	TEST(SafeFileTest, OnWindowsATargetAnotherProcessHasOpenIsStillReplaced) {
		const TempDir temp;
		const auto	  file = temp / "slot.save";
		writeText(file, "old");
		// A scanner or sync client reading it, sharing everything.
		const HANDLE reader = CreateFileW(
			file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr
		);
		ASSERT_NE(reader, INVALID_HANDLE_VALUE);
		EXPECT_TRUE(writeFileSafely(file, bytesOf("new")));
		CloseHandle(reader);
		EXPECT_EQ(readText(file), "new");
	}

	TEST(SafeFileTest, OnWindowsAReplaceRefusedWhileAFileIsHeldIsTriedAgain) {
		const TempDir temp;
		const auto	  file = temp / "slot.save";
		writeText(file, "old");
		// Held with no sharing at all, then let go a little later, as an indexer does.
		const HANDLE holder = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		ASSERT_NE(holder, INVALID_HANDLE_VALUE);
		std::thread release([holder] {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			CloseHandle(holder);
		});
		const bool	written = writeFileSafely(file, bytesOf("new"));
		release.join();
		EXPECT_TRUE(written);
		EXPECT_EQ(readText(file), "new");
	}
#endif

	TEST(SafeFileTest, ReadsTellMissingFromUnreadable) {
		const TempDir temp;
		EXPECT_EQ(readFile(temp / "nothing").status, FileRead::Status::Missing);
		EXPECT_EQ(readFile(temp.path()).status, FileRead::Status::Unreadable);

		writeText(temp / "empty", "");
		const FileRead empty = readFile(temp / "empty");
		EXPECT_EQ(empty.status, FileRead::Status::Read);
		EXPECT_TRUE(empty.bytes.empty());

		const std::string large(200000, 'z'); // more than one read's chunk
		writeText(temp / "large", large);
		const FileRead read = readFile(temp / "large");
		EXPECT_EQ(read.status, FileRead::Status::Read);
		EXPECT_EQ(read.bytes, bytesOf(large));
	}

	TEST(SafeFileTest, MoveAsideNeverReplacesAnEarlierCopy) {
		const TempDir temp;
		const auto	  file = temp / "settings.json";
		writeText(withSuffix(file, ".corrupt"), "first bad");
		writeText(file, "second bad");
		EXPECT_EQ(moveAside(file, ".corrupt"), temp / "settings.json.corrupt.1");
		writeText(file, "third bad");
		EXPECT_EQ(moveAside(file, ".corrupt"), temp / "settings.json.corrupt.2");
		EXPECT_EQ(readText(temp / "settings.json.corrupt"), "first bad");
		EXPECT_EQ(readText(temp / "settings.json.corrupt.1"), "second bad");
		EXPECT_EQ(readText(temp / "settings.json.corrupt.2"), "third bad");
		EXPECT_EQ(moveAside(file, ".corrupt"), std::nullopt); // nothing there to move
	}

} // namespace app::detail
