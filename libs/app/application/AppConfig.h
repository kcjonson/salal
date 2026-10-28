#pragma once

#include "DevServer/DevServer.h"
#include "saves/SaveStore.h"
#include "settings/Settings.h"

#include <backend/Backend.h>
#include <font/FontRenderer.h>
#include <graphics/Color.h>
#include <primitives/FontFamily.h>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace app {

	struct FrameConfig {
		double fixedStep = 1.0 / 60.0; // seconds per fixed tick; asteroids sets 1.0 / 120.0
		double maxFrameDelta = 0.25;   // a longer frame counts as this long (worldsim's clamp today)
		int	   maxTicksPerFrame = 8; // past this the whole steps left are dropped: the sim runs slower than wall time instead of spiralling
		int	   frameCapHz = 0;		 // 0 is vsync alone; worldsim sets 120 (its sleep-then-spin pacing, Windows timeBeginPeriod)
		bool   freezeTicksOnFocusLoss = true;
	};

	struct AppConfig {
		std::string								   organization, applicationName; // name the per-user directories
		FrameConfig								   frame;
		Renderer::backend::ShaderSource			   baseShader;	// the game's base shader with its hooks (salal_base_shader)
		std::span<const Renderer::FontAtlasSource> fontAtlases; // resource paths; empty: no text, logged once, not a failure
		Renderer::FontFamily					   defaultFont = Renderer::FontFamily::Default;
		Foundation::Color clearColor{0.1F, 0.1F, 0.15F, 1.0F}; // through Primitives::clear, each frame before the scenes render
		std::vector<std::filesystem::path>
			resourceRoots; // searched before the exe directory and cwd; replaces any set before create, cleared by ~App
		// Relative paths are under userConfigDir and userDataDir(organization, applicationName); empty is none.
		std::filesystem::path settingsFile;
		int					  settingsVersion = 1; // the game's settings format, saved as the file's "version"
		Settings::Migrate	  migrateSettings;	   // brings a file older than settingsVersion up to it
		std::filesystem::path savesDirectory;
		SaveFormat			  saveFormat;
		DevServerConfig		  devServer; // enabled, port, request timeout; a no-op where the server isn't compiled in
		bool				  metrics = false;
	};

} // namespace app
