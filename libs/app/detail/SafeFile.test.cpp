// The safe write the settings file and saves share: through a temp file renamed over the target, its
// directory made, a stray temp file from a crash replaced; and reads that tell a missing file from one
// that can't be read.

#include "detail/SafeFile.h"
#include "tests/TempDir.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace app::detail {

	namespace {

		using testing::bytesOf;
		using testing::filesIn;
		using testing::readText;
		using testing::TempDir;
		using testing::writeText;

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

	TEST(SafeFileTest, AStrayTempFileFromACrashIsReplacedAndTheTargetWasNeverTouched) {
		const TempDir temp;
		const auto	  file = temp / "settings.json";
		writeText(file, "good");
		writeText(tempFor(file), "half writ"); // a write that died before its rename
		EXPECT_EQ(readText(file), "good");
		ASSERT_TRUE(writeFileSafely(file, bytesOf("new")));
		EXPECT_EQ(readText(file), "new");
		EXPECT_FALSE(std::filesystem::exists(tempFor(file)));
	}

	TEST(SafeFileTest, AWriteThatCannotHappenIsFalseAndLeavesNoTempFile) {
		const TempDir temp;
		const auto	  file = temp / "taken";
		std::filesystem::create_directories(file / "inside"); // a non-empty directory can't be replaced by a file
		EXPECT_FALSE(writeFileSafely(file, bytesOf("x")));
		EXPECT_TRUE(std::filesystem::is_directory(file));
		EXPECT_FALSE(std::filesystem::exists(tempFor(file)));
	}

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

	TEST(SafeFileTest, MoveAsideRenamesOverWhatIsThere) {
		const TempDir temp;
		const auto	  file = temp / "settings.json";
		writeText(file, "bad");
		writeText(withSuffix(file, ".corrupt"), "older bad");
		EXPECT_TRUE(moveAside(file, ".corrupt"));
		EXPECT_EQ(readText(temp / "settings.json.corrupt"), "bad");
		EXPECT_EQ(filesIn(temp.path()), std::vector<std::string>{"settings.json.corrupt"});
		EXPECT_FALSE(moveAside(file, ".corrupt"));
	}

} // namespace app::detail
