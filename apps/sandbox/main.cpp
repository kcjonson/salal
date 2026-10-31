// The sandbox: salal's renderer-level test scenes on the GLFW platform, with the scene picker over them
// (Picker.h). --scene=<name> starts in that scene with the picker closed; without it the first scene
// runs with the picker open. The dev server serves on 127.0.0.1:8090, or --http-port's port, in the
// configurations it is compiled into; /api/control?action=scene&scene=<name> switches scene. Close the
// window, or ask /api/control?action=exit, to quit.

#include "BaseShader.h"
#include "Picker.h"
#include "Scenes.h"

#include <application/App.h>
#include <application/CommandLine.h>
#include <platform/Platform.h>
#include <primitives/FontFamily.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

	constexpr int kDevServerPort = 8090; // worldsim's ui-sandbox port

	// The one family the sandbox loads (docs/app-api.md, decision 8), staged beside the exe by
	// CMakeLists.txt.
	constexpr Renderer::FontFamily				   kMonoFont{0};
	const std::array<Renderer::FontAtlasSource, 1> kFontAtlases{{
		{.family = kMonoFont, .pngPath = "fonts/JetBrainsMono-SDF.png", .jsonPath = "fonts/JetBrainsMono-SDF.json"},
	}};

	// In the picker's order; the first is where the sandbox starts without --scene.
	const std::array<const sandbox::SceneInfo*, 18> kScenes{
		&sandbox::kSdfMinimalScene,
		&sandbox::kPolygonScene,
		&sandbox::kArcScene,
		&sandbox::kPolylineScene,
		&sandbox::kBlendScene,
		&sandbox::kInputTestScene,
		&sandbox::kUiOrderScene,
		&sandbox::kUiTextScene,
		&sandbox::kUiLayoutScene,
		&sandbox::kUiClipScene,
		&sandbox::kUiScrollScene,
		&sandbox::kUiInputScene,
		&sandbox::kUiMotionScene,
		&sandbox::kUiPopupsScene,
		&sandbox::kUiThemeScene,
		&sandbox::kUiIconsScene,
		&sandbox::kUiFocusScene,
		&sandbox::kUiDisplayScene,
	};

	void printUsage(std::FILE* stream) {
		std::fprintf(stream, "Usage: sandbox [--scene=<name>] [--http-port <port>] [--help]\nScenes:");
		for (const sandbox::SceneInfo* scene : kScenes) {
			std::fprintf(stream, " %s", scene->name);
		}
		std::fprintf(stream, "\n");
	}

} // namespace

int main(int argc, char** argv) {
	const app::CommandLine commandLine = app::parseCommandLine(argc, argv);
	if (commandLine.help || !commandLine.valid) {
		printUsage(commandLine.valid ? stdout : stderr);
		return commandLine.valid ? 0 : 1;
	}
	const bool known = !commandLine.scene ||
					   std::ranges::any_of(kScenes, [&](const sandbox::SceneInfo* scene) { return *commandLine.scene == scene->name; });
	if (!known) {
		std::fprintf(stderr, "No scene is called '%s'.\n", commandLine.scene->c_str());
		printUsage(stderr);
		return 1;
	}

	std::unique_ptr<app::Platform> platform = app::createGlfwPlatform({.title = "salal sandbox"});
	if (platform == nullptr) {
		return 1;
	}
	std::unique_ptr<app::App> app = app::App::create(
		{.applicationName = "sandbox",
		 .baseShader = base_shader::source(),
		 .fontAtlases = kFontAtlases,
		 .defaultFont = kMonoFont,
		 // On by default where the server is compiled in; elsewhere only --http-port asks for it, so a
		 // build without it says nothing listens only when someone asked, and a Release launch logs nothing.
		 .devServer =
			 {.enabled = app::kDevServerCompiledIn || commandLine.httpPort.has_value(),
			  .port = commandLine.httpPort.value_or(kDevServerPort)},
		 .metrics = true},
		std::move(platform)
	);
	if (app == nullptr) {
		return 1;
	}

	std::vector<std::string> names;
	for (const sandbox::SceneInfo* scene : kScenes) {
		app->scenes().add(scene->name, scene->make);
		names.emplace_back(scene->name);
	}
	app->scenes().switchTo(commandLine.scene.value_or(kScenes.front()->name));
	app->addOverlay(std::make_unique<sandbox::Picker>(*app, sandbox::Picker::Args{.scenes = std::move(names), .open = !commandLine.scene}));

	app->run();
	app->shutdown();
	return 0;
}
