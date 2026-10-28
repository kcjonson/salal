// The settings file: a round trip, unknown keys kept, a corrupt file moved aside, migration, a newer
// file kept as it is, and what get, set and section do with paths and types the file doesn't match.

#include "settings/Settings.h"
#include "tests/TempDir.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace app {

	namespace {

		using testing::filesIn;
		using testing::readText;
		using testing::TempDir;
		using testing::writeText;

		nlohmann::json fileJson(const std::filesystem::path& file) {
			return nlohmann::json::parse(readText(file));
		}

	} // namespace

	TEST(SettingsTest, WhatIsSetIsReadBackAfterASaveAndALoad) {
		const TempDir temp;
		const auto	  file = temp / "settings.json";
		Settings	  settings = Settings::load(file, 3);
		EXPECT_EQ(settings.version(), 3);
		settings.set("video.vsync", false);
		settings.set("video.fullscreenMonitor", std::optional<int>{});
		settings.set("audio.volume", 0.75);
		settings.set("player.name", std::string("Kevin"));
		settings.set("player.slot", 2);
		settings.section("input")["custom"] = nlohmann::json::array({1, 2});
		ASSERT_TRUE(settings.save());

		EXPECT_EQ(fileJson(file), nlohmann::json::parse(R"({"version": 3, "video": {"vsync": false, "fullscreenMonitor": null},
				"audio": {"volume": 0.75}, "player": {"name": "Kevin", "slot": 2}, "input": {"custom": [1, 2]}})"));
		EXPECT_EQ(filesIn(temp.path()), std::vector<std::string>{"settings.json"});

		const Settings loaded = Settings::load(file, 3);
		EXPECT_EQ(loaded.version(), 3);
		EXPECT_EQ(loaded.get("video.vsync", true), false);
		EXPECT_EQ(loaded.get<std::optional<int>>("video.fullscreenMonitor", 5), std::nullopt);
		EXPECT_TRUE(loaded.contains("video.fullscreenMonitor"));
		EXPECT_EQ(loaded.get("audio.volume", 0.0), 0.75);
		EXPECT_EQ(loaded.get<std::string>("player.name", ""), "Kevin");
		EXPECT_EQ(loaded.get("player.slot", 0), 2);
		EXPECT_EQ(loaded.get("missing.key", 7), 7);
		EXPECT_FALSE(loaded.contains("missing.key"));
		EXPECT_FALSE(loaded.contains("version")); // the Settings' own, not a key
	}

	TEST(SettingsTest, KeysThisBuildDoesNotKnowSurviveALoadAndASave) {
		const TempDir temp;
		const auto	  file = temp / "settings.json";
		writeText(file, R"({"version": 2, "video": {"vsync": true, "hdr": "auto"}, "newerBuild": {"photoMode": [1, 2, 3]}, "flag": null})");
		Settings settings = Settings::load(file, 2);
		settings.set("video.vsync", false);
		ASSERT_TRUE(settings.save());
		EXPECT_EQ(
			fileJson(file),
			nlohmann::json::parse(
				R"({"version": 2, "video": {"vsync": false, "hdr": "auto"}, "newerBuild": {"photoMode": [1, 2, 3]}, "flag": null})"
			)
		);
	}

	TEST(SettingsTest, AMissingFileIsEmptySettingsSavedWhereItWouldHaveBeen) {
		const TempDir temp;
		const auto	  file = temp.path() / "not" / "yet" / "settings.json";
		Settings	  settings = Settings::load(file, 1);
		EXPECT_EQ(settings.get("video.vsync", true), true);
		EXPECT_FALSE(std::filesystem::exists(file)); // nothing written until save
		ASSERT_TRUE(settings.save());
		EXPECT_EQ(fileJson(file), nlohmann::json::parse(R"({"version": 1})"));
	}

	TEST(SettingsTest, AFileThatIsNotJsonOrNotAnObjectOrUnreadableIsMovedAsideAndTheSettingsStartEmpty) {
		const TempDir temp;
		int			  run = 0;
		for (const std::string content : {"{\"video\": {\"vsync\": fals", "[1, 2, 3]", "42", ""}) {
			const std::filesystem::path directory = temp.path() / std::to_string(run++);
			std::filesystem::create_directory(directory);
			const auto file = directory / "settings.json";
			writeText(file, content);
			writeText(directory / "settings.json.corrupt", "an older one"); // never replaced
			Settings settings = Settings::load(file, 4);
			EXPECT_EQ(settings.version(), 4) << content;
			EXPECT_EQ(settings.get("video.vsync", true), true) << content;
			EXPECT_EQ(filesIn(directory), (std::vector<std::string>{"settings.json.corrupt", "settings.json.corrupt.1"})) << content;
			EXPECT_EQ(readText(directory / "settings.json.corrupt"), "an older one");
			EXPECT_EQ(readText(directory / "settings.json.corrupt.1"), content);
			ASSERT_TRUE(settings.save());
			EXPECT_EQ(fileJson(file), nlohmann::json::parse(R"({"version": 4})"));
		}

		// A directory where the file should be can't be read either.
		const auto taken = temp / "taken.json";
		std::filesystem::create_directory(taken);
		const Settings settings = Settings::load(taken, 1);
		EXPECT_TRUE(std::filesystem::is_directory(temp / "taken.json.corrupt"));
		EXPECT_FALSE(std::filesystem::exists(taken));
		EXPECT_FALSE(settings.contains("anything"));
	}

	TEST(SettingsTest, AnOlderFileIsMigratedOnceAndSavedAtTheBuildsVersion) {
		const TempDir temp;
		const auto	  file = temp / "settings.json";
		const auto	  original = R"({"version": 1, "vsync": false, "kept": "yes"})";
		writeText(file, original);
		std::vector<int> calls;
		Settings		 settings = Settings::load(file, 3, [&](nlohmann::json& values, int fromVersion) {
			calls.push_back(fromVersion);
			EXPECT_FALSE(values.contains("version"));
			values["video"]["vsync"] = values["vsync"];
			values.erase("vsync");
		});
		EXPECT_EQ(calls, std::vector<int>{1});
		EXPECT_EQ(settings.version(), 3);
		EXPECT_EQ(settings.get("video.vsync", true), false);
		EXPECT_EQ(readText(file), original); // the file changes when the game saves, not before
		ASSERT_TRUE(settings.save());
		EXPECT_EQ(fileJson(file), nlohmann::json::parse(R"({"version": 3, "video": {"vsync": false}, "kept": "yes"})"));

		const Settings again = Settings::load(file, 3, [&](nlohmann::json& /*values*/, int fromVersion) { calls.push_back(fromVersion); });
		EXPECT_EQ(calls, std::vector<int>{1}); // at the build's version, no migration
	}

	TEST(SettingsTest, AFileWithNoVersionOrABadOneMigratesFromVersionZero) {
		const TempDir temp;
		const auto	  file = temp / "settings.json";
		for (const std::string content :
			 {R"({"a": 1})", R"({"version": "2", "a": 1})", R"({"version": -1, "a": 1})", R"({"version": 1.5, "a": 1})"}) {
			writeText(file, content);
			std::optional<int> from;
			const Settings	   settings = Settings::load(file, 2, [&](nlohmann::json& /*values*/, int fromVersion) { from = fromVersion; });
			EXPECT_EQ(from, 0) << content;
			EXPECT_EQ(settings.version(), 2) << content;
			EXPECT_EQ(settings.get("a", 0), 1) << content;
		}

		// With no Migrate the keys stay as they are, at the build's version.
		writeText(file, R"({"version": 1, "a": 1})");
		Settings settings = Settings::load(file, 2);
		EXPECT_EQ(settings.version(), 2);
		EXPECT_EQ(settings.get("a", 0), 1);
	}

	TEST(SettingsTest, AMigrateThatThrowsOrLeavesSomethingOtherThanAnObjectMovesTheFileAside) {
		const TempDir temp;
		const auto	  file = temp / "settings.json";
		writeText(file, R"({"version": 1, "a": 1})");
		Settings thrown =
			Settings::load(file, 2, [](nlohmann::json& /*values*/, int /*fromVersion*/) { throw std::runtime_error("bad step"); });
		EXPECT_EQ(thrown.version(), 2);
		EXPECT_FALSE(thrown.contains("a"));
		EXPECT_EQ(readText(temp / "settings.json.corrupt"), R"({"version": 1, "a": 1})");

		writeText(file, R"({"version": 1, "b": 1})");
		const Settings replaced = Settings::load(file, 2, [](nlohmann::json& values, int /*fromVersion*/) { values = 5; });
		EXPECT_FALSE(replaced.contains("b"));
		EXPECT_EQ(readText(temp / "settings.json.corrupt"), R"({"version": 1, "a": 1})");
		EXPECT_EQ(readText(temp / "settings.json.corrupt.1"), R"({"version": 1, "b": 1})");
	}

	TEST(SettingsTest, ANewerFileLoadsAsItIsAndKeepsItsVersionOnSave) {
		const TempDir temp;
		const auto	  file = temp / "settings.json";
		writeText(file, R"({"version": 9, "video": {"vsync": false, "upscaler": "fsr"}})");
		bool	 migrated = false;
		Settings settings = Settings::load(file, 3, [&](nlohmann::json& /*values*/, int /*fromVersion*/) { migrated = true; });
		EXPECT_FALSE(migrated);
		EXPECT_EQ(settings.version(), 9);
		EXPECT_EQ(settings.get("video.vsync", true), false);
		settings.set("video.vsync", true);
		ASSERT_TRUE(settings.save());
		EXPECT_EQ(fileJson(file), nlohmann::json::parse(R"({"version": 9, "video": {"vsync": true, "upscaler": "fsr"}})"));
	}

	TEST(SettingsTest, GetGivesTheFallbackForAValueOfAnotherType) {
		Settings settings;
		settings.set("flag", "yes");
		settings.set("number", 1.5);
		settings.set("big", std::int64_t{5000000000});
		settings.set("negative", -1);
		settings.set("object.inner", 1);
		EXPECT_EQ(settings.get("flag", false), false);
		EXPECT_EQ(settings.get("number", 7), 7);		// a float isn't an integer
		EXPECT_EQ(settings.get("number", 0.0F), 1.5F);	// but is a float
		EXPECT_EQ(settings.get("negative", 0.0), -1.0); // and an integer is a float
		EXPECT_EQ(settings.get("big", 3), 3);			// past an int's range
		EXPECT_EQ(settings.get("big", std::int64_t{0}), 5000000000);
		EXPECT_EQ(settings.get("negative", 4U), 4U); // past an unsigned's
		EXPECT_EQ(settings.get<std::string>("number", "f"), "f");
		EXPECT_EQ(settings.get("object", 2), 2);
		EXPECT_EQ(settings.get("object.inner.deeper", 3), 3); // through a number
		EXPECT_EQ(settings.get<std::optional<bool>>("flag", true), true);
		EXPECT_EQ(settings.get<nlohmann::json>("object", nullptr), nlohmann::json::parse(R"({"inner": 1})"));
		EXPECT_EQ(settings.get<std::string>("flag", ""), "yes"); // left as it was
	}

	TEST(SettingsTest, SetReplacesWhatIsNotAnObjectAlongItsPathSoItIsAlwaysReadBack) {
		Settings settings;
		settings.set("video", 5);
		settings.set("video.vsync", false);
		EXPECT_EQ(settings.get("video.vsync", true), false);
		settings.set("video.window.width", 800);
		settings.set("video.window", true); // a value over an object replaces it, as any set does
		EXPECT_EQ(settings.get("video.window", false), true);
		EXPECT_FALSE(settings.contains("video.window.width"));
		EXPECT_EQ(settings.get("video.vsync", true), false);
	}

	TEST(SettingsTest, ASectionIsMadeWhenMissingAndReplacedWhenNotAnObject) {
		Settings		settings;
		nlohmann::json& input = settings.section("input.bindings");
		input["jump"] = "Space";
		EXPECT_EQ(settings.get<std::string>("input.bindings.jump", ""), "Space");
		EXPECT_EQ(&settings.section("input.bindings"), &input);

		settings.set("audio", nlohmann::json::array({1}));
		EXPECT_TRUE(settings.section("audio").is_object());
		EXPECT_TRUE(settings.section("audio").empty());
	}

	TEST(SettingsTest, AnInvalidPathReadsNothingSetsNothingAndItsSectionIsNotSaved) {
		Settings settings;
		for (const std::string path : {"", ".", "a..b", ".a", "a."}) {
			settings.set(path, 1);
			EXPECT_EQ(settings.get(path, 2), 2) << path;
			EXPECT_FALSE(settings.contains(path)) << path;
			settings.section(path)["x"] = 1;
		}
		EXPECT_EQ(settings.get<nlohmann::json>("a", nullptr), nullptr);
	}

	TEST(SettingsTest, SettingsWithNoFileSaveNothing) {
		Settings settings = Settings::load({}, 2);
		EXPECT_TRUE(settings.file().empty());
		EXPECT_EQ(settings.version(), 2);
		settings.set("a", 1);
		EXPECT_TRUE(settings.save());
		EXPECT_EQ(settings.get("a", 0), 1);
	}

	TEST(SettingsTest, ASaveThatCannotBeWrittenIsFalseAndTheFileIsAsItWas) {
		const TempDir temp;
		const auto	  file = temp / "settings.json";
		Settings	  settings = Settings::load(file, 1);
		std::filesystem::create_directories(file / "inside");
		EXPECT_FALSE(settings.save());
		EXPECT_TRUE(std::filesystem::is_directory(file));

		std::filesystem::remove_all(file);
		settings.set("name", std::string("\xFF\xFE")); // not UTF-8, which JSON text has to be
		EXPECT_FALSE(settings.save());
		EXPECT_FALSE(std::filesystem::exists(file));
	}

} // namespace app
