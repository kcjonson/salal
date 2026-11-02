// The crash handler: installed one at a time and put back, and each kind of crash reported with its stack,
// checked in a child process (gtest's death tests) that the handler ends.

#include "diagnostics/CrashHandler.h"

#include "tests/ScopedEnv.h"
#include "tests/TempDir.h"

#include <gtest/gtest.h>

#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace app::testing {

	namespace {

		// A folder at a fixed path, since a death test's child may run the test again from the top in a
		// process of its own and writes where the parent then looks.
		class CrashFolder {
		  public:
			CrashFolder()
				: root(
					  std::filesystem::temp_directory_path() / "salal-tests" /
					  ("crash-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()))
				  ) {
				std::filesystem::create_directories(root);
			}
			~CrashFolder() {
				std::error_code error;
				std::filesystem::remove_all(root, error);
			}

			CrashFolder(const CrashFolder&) = delete;
			CrashFolder& operator=(const CrashFolder&) = delete;
			CrashFolder(CrashFolder&&) = delete;
			CrashFolder& operator=(CrashFolder&&) = delete;

			CrashFiles	files() const { return {.report = root / "run.txt", .dump = root / "run.dmp", .header = "the crash test's run"}; }
			std::string report() const { return readText(files().report); }

		  private:
			std::filesystem::path root;
		};

		void crash(const CrashFiles& files, std::string_view kind) {
			const std::unique_ptr<CrashHandler> handler = CrashHandler::install(files);
			crashOnPurpose(kind);
		}

		constexpr std::string::size_type kNone = std::string::npos;

	} // namespace

	TEST(CrashHandlerTest, ItIsInstalledOneAtATimeAndPutsBackWhatItReplaced) {
		const std::terminate_handler before = std::get_terminate();
		EXPECT_FALSE(crashHandlerInstalled());
		{
			const CrashFolder			  folder;
			std::unique_ptr<CrashHandler> handler = CrashHandler::install(folder.files());
			ASSERT_NE(handler, nullptr);
			EXPECT_TRUE(crashHandlerInstalled());
			EXPECT_EQ(CrashHandler::install(folder.files()), nullptr);
			handler.reset();
			EXPECT_FALSE(std::filesystem::exists(folder.files().report)); // written only for a crash
		}
		EXPECT_FALSE(crashHandlerInstalled());
		EXPECT_EQ(std::get_terminate(), before);
		EXPECT_FALSE(crashOnPurpose("nothing"));
	}

	TEST(CrashHandlerDeathTest, AnAbortIsReportedWithItsStack) {
		const CrashFolder folder;
#ifdef _WIN32
		EXPECT_DEATH(crash(folder.files(), "abort"), "Crash: abort");
		EXPECT_TRUE(std::filesystem::exists(folder.files().dump));
#else
		EXPECT_DEATH(crash(folder.files(), "abort"), "Crash: SIGABRT");
#endif
		const std::string report = folder.report();
		EXPECT_NE(report.find("the crash test's run"), kNone);
		EXPECT_NE(report.find("Stack:"), kNone);
	}

	TEST(CrashHandlerDeathTest, AnUncaughtExceptionIsReportedWithItsWhat) {
		const CrashFolder folder;
		EXPECT_DEATH(crash(folder.files(), "throw"), "uncaught exception: thrown on purpose");
		EXPECT_NE(folder.report().find("Stack:"), kNone);
	}

	TEST(CrashHandlerDeathTest, AFaultIsReportedWithItsAddressAndStack) {
		const CrashFolder folder;
#ifdef _WIN32
		// gtest catches a test body's SEH exceptions unless told not to, and the child reads this.
		const ScopedEnv uncaught("GTEST_CATCH_EXCEPTIONS", "0");
		EXPECT_DEATH(crash(folder.files(), "segv"), "Crash: EXCEPTION_ACCESS_VIOLATION");
		EXPECT_TRUE(std::filesystem::exists(folder.files().dump));
		EXPECT_NE(folder.report().find("writing address 0x0000000000000000"), kNone);
#else
		EXPECT_DEATH(crash(folder.files(), "segv"), "Crash: SIGSEGV");
		EXPECT_NE(folder.report().find("at address 0x0000000000000000"), kNone);
#endif
		EXPECT_NE(folder.report().find("Stack:"), kNone);
	}

	TEST(CrashHandlerDeathTest, AStackOverflowOnTheInstallingThreadIsReported) {
		const CrashFolder folder;
#ifdef _WIN32
		const ScopedEnv uncaught("GTEST_CATCH_EXCEPTIONS", "0");
		EXPECT_DEATH(crash(folder.files(), "overflow"), "Crash: EXCEPTION_STACK_OVERFLOW");
		EXPECT_TRUE(std::filesystem::exists(folder.files().dump));
#else
		EXPECT_DEATH(crash(folder.files(), "overflow"), "Crash: SIG"); // SIGSEGV, or SIGBUS on macOS
#endif
		EXPECT_NE(folder.report().find("Stack:"), kNone);
	}

} // namespace app::testing
