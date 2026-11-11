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

	// The ui's scale from the window's resolution (docs/app-api.md, "UI scale"). With a reference, a ui px
	// is a pixel on a screen of that size: the scale is max(floor, factor x min(W / referenceWidth,
	// H / referenceHeight)) device pixels per ui px, W x H the framebuffer, so the ui's canvas is the
	// reference or more at factor 1 and grows on the axis with room; the operating system's display scale
	// doesn't enter into it. With none, the factor alone is the scale over the window's logical pixels.
	// The factor is the player's setting, ui.scale.
	struct UiScaleConfig {
		int	  referenceWidth = 0; // device pixels; both 0 for no reference
		int	  referenceHeight = 0;
		float floor = 0.0F;		 // the least scale with a reference, whatever the window and the factor
		float minFactor = 0.25F; // what ui.scale and setUiScale are held to
		float maxFactor = 4.0F;
	};

	struct AppConfig {
		std::string								   organization, applicationName; // name the per-user directories
		FrameConfig								   frame;
		UiScaleConfig							   uiScale;
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
		// The run's log file and crash reports (diagnostics/Diagnostics.h); empty is <temp>/<applicationName>, a
		// relative path is under <temp>, and with no applicationName either there's no log file and no crash
		// handler.
		std::filesystem::path diagnosticsDirectory;
		DevServerConfig		  devServer; // enabled, port, request timeout; a no-op where the server isn't compiled in
		bool				  metrics = false;
	};

} // namespace app
