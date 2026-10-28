// The search order: the resource roots in order, then the executable's directory, then the working
// directory, and nothing else.

#include "utils/ResourcePath.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <utility>

namespace Foundation {

	namespace {

		namespace fs = std::filesystem;

		std::string uniqueName(const std::string& prefix) {
			return prefix + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
				   std::to_string(std::random_device{}());
		}

		// A fresh directory under the system temp directory, removed with everything in it.
		class TempDir {
		  public:
			TempDir()
				: path(fs::temp_directory_path() / uniqueName("salal-resource-path")) {
				fs::create_directories(path);
			}
			~TempDir() {
				std::error_code ignored;
				fs::remove_all(path, ignored);
			}
			TempDir(const TempDir&) = delete;
			TempDir& operator=(const TempDir&) = delete;

			const fs::path path;
		};

		// A file at `path`, created with its directories and removed again.
		class TempFile {
		  public:
			explicit TempFile(fs::path where)
				: path(std::move(where)) {
				fs::create_directories(path.parent_path());
				std::ofstream(path) << "probe";
			}
			~TempFile() {
				std::error_code ignored;
				fs::remove(path, ignored);
			}
			TempFile(const TempFile&) = delete;
			TempFile& operator=(const TempFile&) = delete;

			const fs::path path;
		};

		// Each test starts with no resource roots and in its own working directory, and puts both back.
		class ResourcePathTest : public ::testing::Test {
		  protected:
			void SetUp() override {
				savedCwd = fs::current_path();
				fs::current_path(cwd.path);
			}
			void TearDown() override {
				setResourceRoots({});
				fs::current_path(savedCwd);
			}

			fs::path	savedCwd;
			TempDir		cwd;
			std::string probe = uniqueName("probe") + ".txt";
		};

	} // namespace

	TEST(ResourcePathExecutableDir, HoldsTheRunningTest) {
#ifdef _WIN32
		const char* const kExecutable = "salal_foundation_tests.exe";
#else
		const char* const kExecutable = "salal_foundation_tests";
#endif
		const fs::path exeDir = getExecutableDir();
		ASSERT_FALSE(exeDir.empty());
		EXPECT_TRUE(exeDir.is_absolute());
		EXPECT_TRUE(fs::exists(exeDir / kExecutable)) << exeDir.string();
	}

	TEST_F(ResourcePathTest, RootsAreSearchedInOrder) {
		const TempDir  first;
		const TempDir  second;
		const TempFile inFirst(first.path / probe);
		const TempFile inSecond(second.path / probe);

		setResourceRoots({first.path, second.path});
		EXPECT_EQ(findResource(probe), first.path / probe);

		setResourceRoots({second.path, first.path});
		EXPECT_EQ(findResource(probe), second.path / probe);
	}

	TEST_F(ResourcePathTest, ALaterRootFindsWhatAnEarlierOneLacks) {
		const TempDir  first;
		const TempDir  second;
		const TempFile inSecond(second.path / "sub" / probe);

		setResourceRoots({first.path, second.path});
		EXPECT_EQ(findResource(fs::path("sub") / probe), second.path / "sub" / probe);
	}

	TEST_F(ResourcePathTest, RootsComeBeforeTheExecutableDirectory) {
		const TempDir  root;
		const TempFile inRoot(root.path / probe);
		const TempFile inExeDir(getExecutableDir() / probe);

		EXPECT_EQ(findResource(probe), getExecutableDir() / probe);
		setResourceRoots({root.path});
		EXPECT_EQ(findResource(probe), root.path / probe);
	}

	TEST_F(ResourcePathTest, TheExecutableDirectoryComesBeforeTheWorkingDirectory) {
		const TempFile inCwd(cwd.path / probe);
		{
			const TempFile inExeDir(getExecutableDir() / probe);
			EXPECT_EQ(findResource(probe), getExecutableDir() / probe);
		}
		EXPECT_EQ(findResource(probe), fs::current_path() / probe);
	}

	TEST_F(ResourcePathTest, NothingElseIsSearched) {
		const TempFile underUiSandbox(cwd.path / "build" / "apps" / "ui-sandbox" / probe);
		EXPECT_EQ(findResource(probe), std::nullopt);
		EXPECT_EQ(findResourceString(probe), "");
	}

	TEST_F(ResourcePathTest, SettingRootsReplacesTheOldOnes) {
		const TempDir  root;
		const TempFile inRoot(root.path / probe);

		setResourceRoots({root.path});
		ASSERT_EQ(findResource(probe), root.path / probe);
		setResourceRoots({});
		EXPECT_EQ(findResource(probe), std::nullopt);
	}

	TEST_F(ResourcePathTest, ARelativeRootIsTakenFromTheWorkingDirectory) {
		const TempFile inAssets(cwd.path / "assets" / probe);

		setResourceRoots({"assets"});
		EXPECT_EQ(findResourceString(probe), (fs::path("assets") / probe).string());
	}

} // namespace Foundation
