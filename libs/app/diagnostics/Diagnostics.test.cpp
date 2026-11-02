// The diagnostics folder: run names, the default folder, every prune rule, and what an App's start opens.

#include "diagnostics/Diagnostics.h"

#include "diagnostics/CrashHandler.h"
#include "tests/AppHarness.h"
#include "tests/TempDir.h"

#include <utils/LogFile.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace app::testing {

	namespace {

		// The n-th run, later n later.
		std::string run(int n) {
			return "20261102-14" + std::string(n < 10 ? "0" : "") + std::to_string(n) + "00-" + std::to_string(1000 + n);
		}

		void touch(const std::filesystem::path& file, std::chrono::hours age = std::chrono::hours(0)) {
			writeText(file, "x");
			std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() - age);
		}

		class DiagnosticsTest : public ::testing::Test {
		  protected:
			void SetUp() override {
				std::filesystem::create_directories(logs);
				std::filesystem::create_directories(crashes);
			}

			TempDir				  temp;
			std::filesystem::path logs = temp / "logs";
			std::filesystem::path crashes = temp / "crashes";
		};

	} // namespace

	TEST(DiagnosticsNames, ARunIsNamedByWhenItStartedAndItsProcess) {
		std::tm local{};
		local.tm_year = 2026 - 1900;
		local.tm_mon = 10;
		local.tm_mday = 2;
		local.tm_hour = 14;
		local.tm_min = 3;
		local.tm_sec = 11;
		local.tm_isdst = -1;
		const auto started = std::chrono::system_clock::from_time_t(std::mktime(&local));
		EXPECT_EQ(runName(started, 4321), "20261102-140311-4321");

		EXPECT_EQ(runOf("20261102-140311-4321.log"), "20261102-140311-4321");
		EXPECT_EQ(runOf("20261102-140311-4321.1.log"), "20261102-140311-4321");
		EXPECT_EQ(runOf("20261102-140311-4321.txt"), "20261102-140311-4321");
		EXPECT_EQ(runOf("20261102-140311-4321.dmp"), "20261102-140311-4321");
		EXPECT_EQ(runOf("notes.txt"), std::nullopt);
		EXPECT_EQ(runOf("20261102-140311-.log"), std::nullopt);
		EXPECT_EQ(runOf("20261102-140311-4321.log.bak"), std::nullopt);
		EXPECT_EQ(runOf("2026110-1403111-4321.log"), std::nullopt);
	}

	TEST(DiagnosticsNames, TheDefaultFolderIsTheApplicationsUnderTemp) {
		EXPECT_EQ(diagnosticsDirectory("sandbox"), std::filesystem::temp_directory_path() / "sandbox");
		EXPECT_TRUE(diagnosticsDirectory("").empty());
		EXPECT_TRUE(diagnosticsDirectory("a/b").empty());
	}

	TEST_F(DiagnosticsTest, OnlyTheNewestRunsAreKept) {
		for (int n = 0; n < 25; ++n) {
			touch(logs / (run(n) + ".log"));
		}
		touch(logs / (run(24) + ".1.log"));
		touch(logs / "notes.txt");
		pruneDiagnostics(temp.path(), {.runs = 20});

		const std::vector<std::string> left = filesIn(logs);
		EXPECT_EQ(left.size(), 22U); // 20 runs, the newest's second segment, and the notes
		EXPECT_FALSE(std::filesystem::exists(logs / (run(4) + ".log")));
		EXPECT_TRUE(std::filesystem::exists(logs / (run(5) + ".log")));
		EXPECT_TRUE(std::filesystem::exists(logs / (run(24) + ".1.log")));
		EXPECT_TRUE(std::filesystem::exists(logs / "notes.txt"));
	}

	TEST_F(DiagnosticsTest, RunsOlderThanTheAgeGoWithAllTheirSegments) {
		touch(logs / (run(1) + ".log"), std::chrono::hours(24 * 8));
		touch(logs / (run(1) + ".1.log"), std::chrono::hours(24 * 8));
		touch(logs / (run(2) + ".log"), std::chrono::hours(24 * 6));
		pruneDiagnostics(temp.path(), {});
		EXPECT_EQ(filesIn(logs), std::vector<std::string>{run(2) + ".log"});
	}

	TEST_F(DiagnosticsTest, ARunAProcessIsStillWritingIsLeftAlone) {
		const std::filesystem::path live = logs / (run(1) + ".log");
		ASSERT_TRUE(foundation::openLogFile(live, 1024, "live"));
		touch(logs / (run(2) + ".log"));
		pruneDiagnostics(temp.path(), {.runs = 0});
		foundation::closeLogFile();
		EXPECT_EQ(filesIn(logs), std::vector<std::string>{run(1) + ".log"});
	}

	TEST_F(DiagnosticsTest, ACrashedRunsLogJoinsItsReportAndIsKeptAsLongAsTheCrash) {
		touch(logs / (run(1) + ".log"));
		touch(logs / (run(1) + ".1.log"));
		touch(crashes / (run(1) + ".txt"));
		touch(crashes / (run(1) + ".dmp"));
		pruneDiagnostics(temp.path(), {.runs = 0});
		EXPECT_TRUE(filesIn(logs).empty());
		EXPECT_EQ(filesIn(crashes), (std::vector<std::string>{run(1) + ".1.log", run(1) + ".dmp", run(1) + ".log", run(1) + ".txt"}));
	}

	TEST_F(DiagnosticsTest, OnlyTheNewestCrashesAreKeptAndNoneOlderThanTheirAge) {
		for (int n = 0; n < 12; ++n) {
			touch(crashes / (run(n) + ".txt"));
			touch(crashes / (run(n) + ".dmp"));
		}
		touch(crashes / (run(20) + ".txt"), std::chrono::hours(24 * 31));
		pruneDiagnostics(temp.path(), {.crashes = 10});
		EXPECT_EQ(filesIn(crashes).size(), 20U); // runs 2 to 11; run 20 is the newest, but too old
		EXPECT_FALSE(std::filesystem::exists(crashes / (run(1) + ".dmp")));
		EXPECT_TRUE(std::filesystem::exists(crashes / (run(2) + ".dmp")));
		EXPECT_FALSE(std::filesystem::exists(crashes / (run(20) + ".txt")));
	}

	TEST_F(DiagnosticsTest, StartOpensTheRunsLogUnderItsHeaderAndInstallsTheCrashHandler) {
		std::unique_ptr<Diagnostics> diagnostics = Diagnostics::start(temp.path(), "sandbox");
		ASSERT_NE(diagnostics, nullptr);
		EXPECT_EQ(diagnostics->logFile().parent_path(), logs);
		EXPECT_EQ(diagnostics->crashReport().parent_path(), crashes);
		EXPECT_EQ(foundation::logFilePath(), diagnostics->logFile());
		EXPECT_TRUE(crashHandlerInstalled());
		const std::filesystem::path log = diagnostics->logFile();
		const std::filesystem::path report = diagnostics->crashReport();
		diagnostics.reset();
		EXPECT_FALSE(crashHandlerInstalled());
		EXPECT_EQ(foundation::logFileHandle(), foundation::kNoFile);
		EXPECT_TRUE(readText(log).starts_with("sandbox, "));
		EXPECT_FALSE(std::filesystem::exists(report)); // no crash, no report
	}

	class AppDiagnosticsTest : public AppTest {};

	TEST_F(AppDiagnosticsTest, ANamedAppLogsToItsFolderUntilItIsDestroyed) {
		const TempDir temp;
		AppConfig	  named = config();
		named.applicationName = "sandbox";
		named.diagnosticsDirectory = temp / "diagnostics";
		start(named);
		EXPECT_TRUE(crashHandlerInstalled());
		ASSERT_EQ(filesIn(temp / "diagnostics" / "logs").size(), 1U);
		app.reset();
		EXPECT_FALSE(crashHandlerInstalled());
		const std::string text = readText(temp / "diagnostics" / "logs" / filesIn(temp / "diagnostics" / "logs")[0]);
		EXPECT_TRUE(text.starts_with("sandbox, "));
#ifdef DEVELOPMENT_BUILD
		EXPECT_NE(text.find("App destroyed"), std::string::npos); // the teardown too; Release logs only errors
#endif
	}

	TEST_F(AppDiagnosticsTest, AnUnnamedAppHasNoLogFileAndNoCrashHandler) {
		start();
		EXPECT_FALSE(crashHandlerInstalled());
		EXPECT_EQ(foundation::logFileHandle(), foundation::kNoFile);
	}

} // namespace app::testing
