// Which names may become file names, and the per-user directories each platform's environment gives,
// read from environment variables the tests point at a temp directory.

#include "storage/UserDirectories.h"
#include "tests/ScopedEnv.h"
#include "tests/TempDir.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace app {

	namespace {

		using testing::ScopedEnv;
		using testing::TempDir;

		std::string text(const std::filesystem::path& path) {
			return path.generic_string();
		}

	} // namespace

	TEST(UserDirectoriesTest, FileNamesAreLettersDigitsSpacesDashesUnderscoresAndInnerDots) {
		for (const std::string& name : std::vector<std::string>{
				 "slot1", "Slot 1", "auto-save_2", "v1.2", "a", "LPT", "COMA", "console", "nul-1", std::string(64, 'a')
			 }) {
			EXPECT_TRUE(validFileName(name)) << name;
		}
	}

	TEST(UserDirectoriesTest, SeparatorsDotsAtTheEndsDeviceNamesAndLongNamesAreRefused) {
		for (const std::string& name : std::vector<std::string>{
				 "",
				 "a/b",
				 "a\\b",
				 "a:b",
				 "a*b",
				 "a?b",
				 "a\"b",
				 "a<b",
				 "a|b",
				 ".",
				 "..",
				 ".hidden",
				 "trailing.",
				 " leading",
				 "trailing ",
				 "tab\there",
				 "CON",
				 "con",
				 "Prn",
				 "aux.save",
				 "NUL.txt",
				 "COM0",
				 "com9",
				 "LPT1",
				 "lpt9.bak",
				 "na\xC3\xAFve", // UTF-8 i diaeresis
				 std::string(65, 'a'),
			 }) {
			EXPECT_FALSE(validFileName(name)) << name;
		}
	}

	TEST(UserDirectoriesTest, TheDirectoriesFollowThePlatformsEnvironment) {
		const TempDir	  temp;
		const std::string root = temp.path().string();
#if defined(_WIN32)
		const ScopedEnv roaming("APPDATA", root + "\\Roaming");
		const ScopedEnv local("LOCALAPPDATA", root + "\\Local");
		EXPECT_EQ(text(userConfigDir("kcjonson", "Asteroids")), text(temp.path() / "Roaming" / "kcjonson" / "Asteroids"));
		EXPECT_EQ(text(userDataDir("kcjonson", "Asteroids")), text(temp.path() / "Local" / "kcjonson" / "Asteroids"));
#elif defined(__APPLE__)
		const ScopedEnv				home("HOME", root);
		const std::filesystem::path support = temp.path() / "Library" / "Application Support" / "kcjonson";
		EXPECT_EQ(text(userConfigDir("kcjonson", "Asteroids")), text(support / "Asteroids"));
		EXPECT_EQ(text(userDataDir("kcjonson", "Asteroids")), text(support / "Asteroids Data"));
#else
		const ScopedEnv home("HOME", root);
		{
			const ScopedEnv config("XDG_CONFIG_HOME", root + "/config");
			const ScopedEnv data("XDG_DATA_HOME", root + "/data");
			EXPECT_EQ(text(userConfigDir("kcjonson", "Asteroids")), text(temp.path() / "config" / "kcjonson" / "Asteroids"));
			EXPECT_EQ(text(userDataDir("kcjonson", "Asteroids")), text(temp.path() / "data" / "kcjonson" / "Asteroids"));
		}
		{
			// Unset, or not absolute as the XDG spec asks, they fall back under HOME.
			const ScopedEnv config("XDG_CONFIG_HOME", std::nullopt);
			const ScopedEnv data("XDG_DATA_HOME", "relative/data");
			EXPECT_EQ(text(userConfigDir("kcjonson", "Asteroids")), text(temp.path() / ".config" / "kcjonson" / "Asteroids"));
			EXPECT_EQ(text(userDataDir("kcjonson", "Asteroids")), text(temp.path() / ".local" / "share" / "kcjonson" / "Asteroids"));
		}
#endif
		// Beside each other, neither inside the other, and only computed.
		const std::string config = text(userConfigDir("kcjonson", "Asteroids"));
		const std::string data = text(userDataDir("kcjonson", "Asteroids"));
		EXPECT_FALSE(data.starts_with(config + "/"));
		EXPECT_FALSE(config.starts_with(data + "/"));
		EXPECT_NE(config, data);
		EXPECT_FALSE(std::filesystem::exists(config));
		EXPECT_FALSE(std::filesystem::exists(data));
	}

	TEST(UserDirectoriesTest, ANameThatIsNotAFileNameOrNoRootGivesNoDirectory) {
		const TempDir temp;
#if defined(_WIN32)
		const ScopedEnv roaming("APPDATA", temp.path().string());
		const ScopedEnv local("LOCALAPPDATA", temp.path().string());
#else
		const ScopedEnv home("HOME", temp.path().string());
#endif
		EXPECT_TRUE(userConfigDir("", "Asteroids").empty());
		EXPECT_TRUE(userConfigDir("kcjonson", "../Asteroids").empty());
		EXPECT_TRUE(userDataDir("kcjonson", "CON").empty());
		EXPECT_FALSE(userConfigDir("kcjonson", "Asteroids").empty());

#if defined(_WIN32)
		const ScopedEnv noRoaming("APPDATA", std::nullopt);
		const ScopedEnv relativeLocal("LOCALAPPDATA", "relative");
#else
		const ScopedEnv noHome("HOME", std::nullopt);
		const ScopedEnv noConfig("XDG_CONFIG_HOME", std::nullopt);
		const ScopedEnv noData("XDG_DATA_HOME", std::nullopt);
#endif
		EXPECT_TRUE(userConfigDir("kcjonson", "Asteroids").empty());
		EXPECT_TRUE(userDataDir("kcjonson", "Asteroids").empty());
	}

} // namespace app
