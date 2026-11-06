#include "LogFile.h"
#include "Log.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

using namespace foundation;

namespace {

	class LogFileTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			Logger::initialize();
			root = std::filesystem::temp_directory_path() / "salal-tests" /
				   ("LogFileTest." + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + "-" +
					std::to_string(std::random_device{}()));
			std::filesystem::create_directories(root);
		}
		void TearDown() override {
			closeLogFile();
			std::error_code error;
			std::filesystem::remove_all(root, error);
		}

		static std::string read(const std::filesystem::path& file) {
			std::ifstream in(file, std::ios::binary);
			return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
		}

		static void error(const char* format, int value) { Logger::log(LogCategory::Game, LogLevel::Error, "game.cpp", 7, format, value); }

		std::filesystem::path root;
	};

	TEST_F(LogFileTest, TheFileGetsWhatTheConsoleShowsWithoutItsColours) {
		const std::filesystem::path log = root / "run.log";
		ASSERT_TRUE(openLogFile(log, 1024 * 1024, "sandbox Debug, pid 1"));
		EXPECT_EQ(logFilePath(), log);
		Logger::setLevel(LogCategory::Renderer, LogLevel::Warning);
		Logger::log(LogCategory::Renderer, LogLevel::Info, "r.cpp", 1, "below the console's level");
		error("shown %d", 3);
		closeLogFile();
		EXPECT_TRUE(logFilePath().empty());

		const std::string text = read(log);
		EXPECT_TRUE(text.starts_with("sandbox Debug, pid 1\n"));
		EXPECT_NE(text.find("[Game][ERROR] shown 3 (game.cpp:7)\n"), std::string::npos);
		EXPECT_EQ(text.find("below the console's level"), std::string::npos);
		EXPECT_EQ(text.find('\033'), std::string::npos);
	}

	TEST_F(LogFileTest, LinesLoggedBeforeTheFileOpensAreWrittenIntoIt) {
		// Whatever earlier tests left waiting goes into a file of its own first.
		ASSERT_TRUE(openLogFile(root / "drain.log", 1024 * 1024, "drain"));
		closeLogFile();
		error("before the file %d", 1);
		ASSERT_TRUE(openLogFile(root / "run.log", 1024 * 1024, "header"));
		error("after the file %d", 2);
		closeLogFile();

		const std::string text = read(root / "run.log");
		const std::size_t header = text.find("header\n");
		const std::size_t before = text.find("before the file 1");
		const std::size_t after = text.find("after the file 2");
		ASSERT_NE(before, std::string::npos);
		ASSERT_NE(after, std::string::npos);
		EXPECT_EQ(header, 0U);
		EXPECT_LT(before, after);
	}

	TEST_F(LogFileTest, AFileRollsIntoOnePreviousSegmentEachUnderTheHeader) {
		const std::filesystem::path log = root / "run.log";
		const std::filesystem::path previous = root / "run.1.log";
		ASSERT_TRUE(openLogFile(log, 300, "header"));
		for (int i = 0; i < 20; ++i) {
			error("line %02d with some padding to fill it", i);
		}
		closeLogFile();

		const std::string current = read(log);
		const std::string older = read(previous);
		EXPECT_TRUE(current.starts_with("header\n"));
		EXPECT_TRUE(older.starts_with("header\n"));
		EXPECT_NE(current.find("line 19"), std::string::npos);
		EXPECT_EQ(current.find("line 00"), std::string::npos);
		EXPECT_EQ(older.find("line 00"), std::string::npos); // rolled out with the segment before
		EXPECT_LE(current.size(), 300U + 100U);
		EXPECT_FALSE(std::filesystem::exists(root / "run.1.1.log"));
	}

	TEST_F(LogFileTest, ItsHandleTakesRawWritesWhileItIsOpen) {
		EXPECT_EQ(logFileHandle(), kNoFile);
		const std::filesystem::path log = root / "run.log";
		ASSERT_TRUE(openLogFile(log, 1024 * 1024, "header"));
		ASSERT_NE(logFileHandle(), kNoFile);
		EXPECT_TRUE(writeNative(logFileHandle(), "a raw line\n"));
		closeLogFile();
		EXPECT_EQ(logFileHandle(), kNoFile);
		EXPECT_FALSE(writeNative(kNoFile, "nowhere"));
		EXPECT_NE(read(log).find("a raw line\n"), std::string::npos);
	}

	TEST_F(LogFileTest, AnOpenLogFileIsInUseAndAClosedOneIsNot) {
		const std::filesystem::path log = root / "run.log";
		ASSERT_TRUE(openLogFile(log, 1024 * 1024, "header"));
		EXPECT_TRUE(logFileInUse(log));
		closeLogFile();
		EXPECT_FALSE(logFileInUse(log));
		EXPECT_FALSE(logFileInUse(root / "missing.log"));
	}

} // namespace
