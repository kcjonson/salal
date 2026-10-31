#pragma once

// The sandbox's scenes, each defined in its own file under scenes/. They are the engine's test
// fixtures, ported from worldsim's ui-sandbox: renderer-level drawing and input, ui's ui_ scenes, and
// the perf suite's perf_ scenes (docs/testing/README.md, Performance); no game content.

#include <primitives/FontFamily.h>
#include <scene/Scene.h>

#include <memory>

namespace sandbox {

	// A scene as the stack adds it: the name it goes by on the command line, the dev server and the
	// picker, which is also its Scene::name(), and how to build it.
	struct SceneInfo {
		const char* name;
		std::unique_ptr<app::Scene> (*make)();
	};

	template <typename S>
	std::unique_ptr<app::Scene> makeScene() {
		return std::make_unique<S>();
	}

	extern const SceneInfo kSdfMinimalScene;
	extern const SceneInfo kPolygonScene;
	extern const SceneInfo kArcScene;
	extern const SceneInfo kPolylineScene;
	extern const SceneInfo kBlendScene;
	extern const SceneInfo kInputTestScene;
	extern const SceneInfo kFillHookScene;
	extern const SceneInfo kUiOrderScene;
	extern const SceneInfo kUiTextScene;
	extern const SceneInfo kUiLayoutScene;
	extern const SceneInfo kUiClipScene;
	extern const SceneInfo kUiScrollScene;
	extern const SceneInfo kUiInputScene;
	extern const SceneInfo kUiMotionScene;
	extern const SceneInfo kUiOverlaysScene;
	extern const SceneInfo kUiOverlayComponentsScene;
	extern const SceneInfo kUiThemeScene;
	extern const SceneInfo kUiIconsScene;
	extern const SceneInfo kUiFocusScene;
	extern const SceneInfo kUiDisplayScene;
	extern const SceneInfo kUiControlsScene;
	extern const SceneInfo kPerfQuadsScene;
	extern const SceneInfo kPerfTextScene;
	extern const SceneInfo kPerfWorldScene;
	extern const SceneInfo kPerfUiScene;

	// The families main.cpp loads. The second is the same atlas loaded again, so perf_text has two font
	// atlases to alternate between, as a game with a body and a mono font does.
	inline constexpr Renderer::FontFamily kMonoFont{0};
	inline constexpr Renderer::FontFamily kSecondFont{1};

} // namespace sandbox
