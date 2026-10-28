// The App's settings and saves: its video keys applied from the settings file at create on the headless
// platform, setVsync and setFullscreen kept in them, setFullscreen's resize taken at once and reaching
// onResize at the next poll, a windowed resize remembered, the per-user paths, and saves() over its
// directory. The App never saves the settings itself.

#include "application/App.h"
#include "storage/UserDirectories.h"
#include "tests/AppHarness.h"
#include "tests/ScopedEnv.h"
#include "tests/TempDir.h"

#include <primitives/Primitives.h>

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace app::testing {

	namespace {

		inline constexpr char kA[] = "A";
		using SceneA = Recording<kA>;

		std::string sizeOf(const Renderer::Viewport& viewport) {
			return std::to_string(viewport.logicalWidth) + "x" + std::to_string(viewport.logicalHeight) + " (" +
				   std::to_string(viewport.framebufferWidth) + "x" + std::to_string(viewport.framebufferHeight) + ")";
		}

		std::string primitivesSize() {
			int width = 0;
			int height = 0;
			Renderer::Primitives::getViewport(width, height);
			return std::to_string(width) + "x" + std::to_string(height);
		}

		class AppSettingsTest : public AppTest {
		  protected:
			AppConfig withFiles() const {
				AppConfig files = config();
				files.settingsFile = temp / "settings.json";
				files.savesDirectory = temp / "saves";
				return files;
			}

			static void twoMonitors(HeadlessPlatform& headless) {
				headless.addMonitor(1920, 1080);
				headless.addMonitor(2560, 1440);
			}

			TempDir temp;
		};

	} // namespace

	TEST_F(AppSettingsTest, VsyncFullscreenAndTheWindowSizeAreAppliedFromTheSettingsAtCreate) {
		const std::string file =
			R"({"version": 1, "video": {"vsync": false, "fullscreenMonitor": 1, "windowWidth": 1024, "windowHeight": 640}})";
		writeText(temp / "settings.json", file);
		start(withFiles(), twoMonitors);

		EXPECT_FALSE(platform->vsync());
		EXPECT_EQ(platform->fullscreenMonitor(), 1);
		EXPECT_EQ(sizeOf(app->viewport()), "2560x1440 (2560x1440)");
		EXPECT_EQ(primitivesSize(), "2560x1440");

		// The window size is where fullscreen returns to.
		app->setFullscreen(std::nullopt);
		EXPECT_EQ(sizeOf(app->viewport()), "1024x640 (1024x640)");
		EXPECT_EQ(readText(temp / "settings.json"), file); // the App changed its settings, and didn't save them
	}

	TEST_F(AppSettingsTest, KeysThatAreNotThereLeaveTheWindowAsItsConfigMadeIt) {
		start(withFiles(), [](HeadlessPlatform& headless) {
			twoMonitors(headless);
			headless.setFullscreen(0); // as a WindowConfig with a fullscreenMonitor would
		});
		EXPECT_TRUE(platform->vsync());
		EXPECT_EQ(platform->fullscreenMonitor(), 0);
		EXPECT_EQ(sizeOf(app->viewport()), "1920x1080 (1920x1080)");
		EXPECT_FALSE(app->settings().contains("video"));
		EXPECT_FALSE(std::filesystem::exists(temp / "settings.json"));
	}

	TEST_F(AppSettingsTest, ANullFullscreenMonitorIsWindowed) {
		writeText(temp / "settings.json", R"({"version": 1, "video": {"fullscreenMonitor": null}})");
		start(withFiles(), [](HeadlessPlatform& headless) {
			twoMonitors(headless);
			headless.setFullscreen(0);
		});
		EXPECT_EQ(platform->fullscreenMonitor(), std::nullopt);
		EXPECT_EQ(sizeOf(app->viewport()), "800x600 (800x600)");
	}

	TEST_F(AppSettingsTest, TheGamesMigrationRunsBeforeTheAppReadsItsKeys) {
		writeText(temp / "settings.json", R"({"version": 1, "vsync": false})");
		AppConfig migrating = withFiles();
		migrating.settingsVersion = 2;
		migrating.migrateSettings = [](nlohmann::json& values, int fromVersion) {
			EXPECT_EQ(fromVersion, 1);
			values["video"]["vsync"] = values["vsync"];
			values.erase("vsync");
		};
		start(migrating);
		EXPECT_FALSE(platform->vsync());
		EXPECT_EQ(app->settings().version(), 2);
	}

	TEST_F(AppSettingsTest, SetFullscreenTakesTheNewSizeAtOnceAndScenesHearOfItAtTheNextPoll) {
		start(withFiles(), twoMonitors);
		app->addOverlay(std::make_unique<RecordingOverlay>("O"));
		app->scenes().push<SceneA>();
		frame();
		hooks()["A"].update = [](App& owner, const FrameContext& /*frame*/) {
			owner.setFullscreen(0);
			record(
				"A",
				"viewport " + sizeOf(owner.viewport()) + ", coordinates " + sizeOf(owner.coordinateSystem().viewport()) + ", primitives " +
					primitivesSize()
			);
		};
		hooks()["A"].render = [](App& owner) {
			record("A", "drawn at " + sizeOf(owner.viewport()));
		};
		take();

		frame();
		EXPECT_EQ(count("A.viewport 1920x1080 (1920x1080), coordinates 1920x1080 (1920x1080), primitives 1920x1080"), 1);
		EXPECT_EQ(count("A.drawn at 1920x1080 (1920x1080)"), 1); // the frame it was asked in draws at the new size
		EXPECT_EQ(count("A.onResize"), 0);
		hooks()["A"].update = {};

		take();
		frame();
		EXPECT_EQ(head(2), (std::vector<std::string>{"O.onResize 1920x1080", "A.onResize 1920x1080"}));
		frame();
		EXPECT_EQ(count("A.onResize"), 1);
		EXPECT_EQ(app->settings().get<std::optional<int>>("video.fullscreenMonitor", std::nullopt), 0);

		// And back, from outside a frame.
		take();
		app->setFullscreen(std::nullopt);
		EXPECT_EQ(sizeOf(app->viewport()), "800x600 (800x600)");
		EXPECT_EQ(primitivesSize(), "800x600");
		frame();
		EXPECT_EQ(count("O.onResize 800x600"), 1);
		EXPECT_EQ(count("A.onResize 800x600"), 1);
		EXPECT_TRUE(app->settings().contains("video.fullscreenMonitor"));
		EXPECT_EQ(app->settings().get<std::optional<int>>("video.fullscreenMonitor", 3), std::nullopt);
	}

	TEST_F(AppSettingsTest, AMonitorThatIsNotConnectedLeavesItWindowedAndIsKeptAsAsked) {
		start(withFiles());
		app->setFullscreen(1);
		EXPECT_EQ(platform->fullscreenMonitor(), std::nullopt);
		EXPECT_EQ(sizeOf(app->viewport()), "800x600 (800x600)");
		EXPECT_EQ(app->settings().get<std::optional<int>>("video.fullscreenMonitor", std::nullopt), 1);
	}

	TEST_F(AppSettingsTest, SetVsyncGoesToThePlatformAndTheSettingsWhichTheGameSaves) {
		start(withFiles());
		app->setVsync(false);
		EXPECT_FALSE(platform->vsync());
		EXPECT_EQ(app->settings().get("video.vsync", true), false);
		EXPECT_FALSE(std::filesystem::exists(temp / "settings.json"));
		ASSERT_TRUE(app->settings().save());
		EXPECT_EQ(
			nlohmann::json::parse(readText(temp / "settings.json")), nlohmann::json::parse(R"({"version": 1, "video": {"vsync": false}})")
		);
	}

	TEST_F(AppSettingsTest, AWindowedResizeIsRememberedForTheNextLaunchAndAFullscreenOneIsNot) {
		start(withFiles(), twoMonitors);
		platform->resize(900, 700);
		frame();
		EXPECT_EQ(app->settings().get("video.windowWidth", 0), 900);
		EXPECT_EQ(app->settings().get("video.windowHeight", 0), 700);
		app->setFullscreen(0);
		frame();
		EXPECT_EQ(app->settings().get("video.windowWidth", 0), 900);
		app->setFullscreen(std::nullopt);
		ASSERT_TRUE(app->settings().save());
		app.reset();

		start(withFiles(), twoMonitors);
		EXPECT_EQ(platform->fullscreenMonitor(), std::nullopt);
		EXPECT_EQ(sizeOf(app->viewport()), "900x700 (900x700)");
	}

	TEST_F(AppSettingsTest, SavesGoThroughTheAppsStoreInTheGamesFormat) {
		AppConfig saving = withFiles();
		saving.saveFormat = {.version = 2};
		start(saving);
		ASSERT_TRUE(app->saves().writeJson("run", nlohmann::json{{"seed", 7}}));
		EXPECT_EQ(app->saves().readJson("run").value_or(nullptr), nlohmann::json({{"seed", 7}}));
		EXPECT_TRUE(std::filesystem::exists(temp / "saves" / "run.save"));
		ASSERT_EQ(app->saves().slots().size(), 1U);
		EXPECT_EQ(app->saves().slots()[0].version, 2);
	}

	TEST_F(AppSettingsTest, RelativePathsAreUnderThePerUserDirectories) {
		const ScopedUserRoots roots(temp.path());
		AppConfig			  relative = config();
		relative.organization = "kcjonson";
		relative.applicationName = "Sandbox";
		relative.settingsFile = "settings.json";
		relative.savesDirectory = "saves";
		start(relative);
		EXPECT_EQ(app->settings().file(), userConfigDir("kcjonson", "Sandbox") / "settings.json");
		EXPECT_EQ(app->saves().directory(), userDataDir("kcjonson", "Sandbox") / "saves");
		EXPECT_TRUE(app->settings().file().string().starts_with(temp.path().string()));
		EXPECT_TRUE(app->saves().directory().string().starts_with(temp.path().string()));
	}

	TEST_F(AppSettingsTest, RelativePathsWithNoPerUserDirectoryAreNone) {
		AppConfig unnamed = config();
		unnamed.settingsFile = "settings.json"; // no organization or applicationName to put it under
		unnamed.savesDirectory = "saves";
		start(unnamed);
		EXPECT_TRUE(app->settings().file().empty());
		EXPECT_TRUE(app->saves().directory().empty());
		app->setVsync(false);
		EXPECT_TRUE(app->settings().save()); // in memory, nothing to write
		EXPECT_FALSE(app->saves().write("run", {}));
	}

} // namespace app::testing
