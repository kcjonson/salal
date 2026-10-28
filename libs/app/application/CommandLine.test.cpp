#include "application/CommandLine.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <vector>

namespace app {

	namespace {

		// argv for `words`, the program's name first.
		CommandLine parse(std::initializer_list<const char*> words) {
			std::vector<std::string> storage{"game"};
			storage.insert(storage.end(), words.begin(), words.end());
			std::vector<char*> argv;
			for (std::string& word : storage) {
				argv.push_back(word.data());
			}
			return parseCommandLine(static_cast<int>(argv.size()), argv.data());
		}

	} // namespace

	TEST(CommandLineTest, NothingGivenIsValidAndEmpty) {
		const CommandLine none = parse({});
		EXPECT_FALSE(none.scene.has_value());
		EXPECT_FALSE(none.httpPort.has_value());
		EXPECT_FALSE(none.help);
		EXPECT_TRUE(none.valid);

		const CommandLine noArgv = parseCommandLine(0, nullptr);
		EXPECT_TRUE(noArgv.valid);
		EXPECT_FALSE(noArgv.scene.has_value());
	}

	TEST(CommandLineTest, SceneHttpPortAndHelp) {
		const CommandLine all = parse({"--scene=main_menu", "--http-port", "8090", "--help"});
		EXPECT_EQ(all.scene, "main_menu");
		EXPECT_EQ(all.httpPort, 8090);
		EXPECT_TRUE(all.help);
		EXPECT_TRUE(all.valid);
	}

	TEST(CommandLineTest, TheLastSceneGivenWins) {
		EXPECT_EQ(parse({"--scene=a", "--scene=b"}).scene, "b");
		EXPECT_EQ(parse({"--scene="}).scene, "");
	}

	TEST(CommandLineTest, APortMustBeAWholeNumberFrom1To65535) {
		for (const char* bad : {"abc", "0", "65536", "-1", "80x", ""}) {
			const CommandLine parsed = parse({"--http-port", bad});
			EXPECT_FALSE(parsed.valid) << bad;
			EXPECT_FALSE(parsed.httpPort.has_value()) << bad;
		}
		EXPECT_EQ(parse({"--http-port", "1"}).httpPort, 1);
		EXPECT_EQ(parse({"--http-port", "65535"}).httpPort, 65535);
	}

	TEST(CommandLineTest, AnHttpPortWithNoPortIsInvalid) {
		const CommandLine parsed = parse({"--scene=a", "--http-port"});
		EXPECT_FALSE(parsed.valid);
		EXPECT_EQ(parsed.scene, "a");
	}

	TEST(CommandLineTest, FlagsTheEngineDoesNotKnowAreTheGames) {
		const CommandLine parsed = parse({"--seed", "42", "--scene", "x", "--http-port=8090"});
		EXPECT_TRUE(parsed.valid);
		EXPECT_FALSE(parsed.scene.has_value());
		EXPECT_FALSE(parsed.httpPort.has_value());
	}

} // namespace app
