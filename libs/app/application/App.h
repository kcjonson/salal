#pragma once

// The application: the window and its frame, input, the scene stack with the debug layers above it,
// the game's context object, the settings file and saves. A game's main makes the platform and the
// App, adds its scenes and pushes the first, then run()s, shutdown()s, tears its own resources down
// and destroys the App; a test drives tick() on a HeadlessPlatform, advancing its ManualClock.
//
// One App at a time: the renderer's Primitives are module-static, and the App starts and stops them.

#include "DevServer/DevServer.h"
#include "application/AppConfig.h"
#include "application/FrameCapture.h"
#include "clipboard/Clipboard.h"
#include "detail/TypeKey.h"
#include "input/Input.h"
#include "platform/Platform.h"
#include "saves/SaveStore.h"
#include "scene/DebugLayer.h"
#include "scene/SceneStack.h"
#include "settings/Settings.h"

#include <CoordinateSystem/CoordinateSystem.h>
#include <font/FontRenderer.h>
#include <input/InputTypes.h>
#include <metrics/MetricsCollector.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace app {

	class App final : private PlatformEvents {
	  public:
		// The App on `platform`, with the renderer started on the platform's backend, the game's fonts
		// loaded and, when AppConfig::devServer asks, the dev server listening; null, logged, for no
		// platform, a FrameConfig with a step, delta or tick cap that isn't positive, a settingsVersion
		// below 1, no backend, font atlases that don't give the default family, a dev server port that's
		// busy, or an App already alive. Never exits the process.
		static std::unique_ptr<App> create(AppConfig config, std::unique_ptr<Platform> platform);

		// shutdown() if it hasn't run, then the fonts, the renderer and the window, then the dev server,
		// which answers an exit request only now.
		~App();

		App(const App&) = delete;
		App& operator=(const App&) = delete;
		App(App&&) = delete;
		App& operator=(App&&) = delete;

		// One frame, as docs/app-api.md numbers it. False once exit is requested or the window's close
		// is, after finishing that frame (an exit through the dev server ends it at step 3); from then
		// on, and after shutdown(), false with no frame run. The clock starts at the first frame, whose
		// delta is 0. Called from inside a frame it refuses, logged, and returns false.
		bool tick();
		// The native runner: while (tick()) pace(), pacing being the platform's vsync and, with a
		// frameCapHz, a sleep then a spin to the cap. A clock that doesn't run in real time (a
		// HeadlessPlatform's) has nothing to pace against, so run() doesn't wait on it; a minimized
		// window sleeps a little each frame instead. Refused, logged, from inside a frame.
		void run();
		// The scenes exit top down, then the debug layers, last added first, and all are destroyed; the
		// renderer and the window stay up for the game's teardown, until ~App. Inside a frame it asks
		// for exit instead, logged, since scenes are mid-call.
		void shutdown();
		void requestExit();

		// Fixed ticks and update stop for scenes and debug layers, and scene changes wait; dispatch goes to
		// debug layers only, and render goes on. A pause from inside a fixed tick ends that frame's ticks.
		void pause();
		// Drops steps not run yet. Inside a frame, takes effect once the frame is over, so that frame stays
		// paused and the press that resumed doesn't reach a tick.
		void resume();
		bool paused() const;
		// While paused, the next frame runs exactly `ticks` fixed ticks of the top scene, whatever the
		// clock did, then renders. Steps asked for before that frame add up; resume() drops them, and
		// steps asked for after an in-frame resume() are dropped at the frame's end, logged. Does
		// nothing, logged, while not paused.
		void step(int ticks);

		// Replaces step 5's default dispatch (each event to the debug layers, last added first, then unless
		// paused the top scene's ui, then the top scene). Runs every frame, paused or not; an empty
		// function restores the default.
		void setDispatch(std::function<void(App&)> dispatch);
		// The window's settings, applied through the platform and kept in settings() as video.vsync and
		// video.fullscreenMonitor (null for windowed), which the App reads at create; the game saves them.
		// The monitor is kept as asked, so one unplugged now is tried again at the next launch.
		void setVsync(bool enabled);
		// The ui's one scale (R7.5), 0.25 to 4, kept in settings() as ui.scale, which the App reads at
		// create: every scene's ui lays out in the window's logical size over it and draws scaled, so a
		// handheld gets larger ui with no second layout.
		void				setUiScale(float scale);
		[[nodiscard]] float uiScale() const { return scaleOfUi; }
		// Reduced motion (R11.13), kept in settings() as ui.reducedMotion: every scene's ui completes its
		// tweens on their first tick. A game sets it from its options or the system's preference.
		void			   setReducedMotion(bool reduced);
		[[nodiscard]] bool reducedMotion() const { return motionReduced; }
		// Fullscreen on the monitor at `monitor` (0 the primary), or windowed. Where the window resizes at
		// once, the viewport, the coordinate system and Primitives take the new size at once too, as GL's
		// viewport does, so the frame it's called in draws at that size; scenes and debug layers hear of it
		// through onResize after the next frame's poll, as of any resize.
		void setFullscreen(std::optional<int> monitor);
		// The monitor the window is fullscreen on now, which video.fullscreenMonitor needn't be (it keeps
		// what was asked for); nullopt while windowed.
		std::optional<int> fullscreenMonitor() const;

		SceneStack& scenes();

		// The game's state that outlives a scene, set once, before the first scene, and read as T or
		// const T. A second setContext, or context<T>() with no context or a T other than the one set,
		// is a programming error: logged, then the process aborts. The type check holds within one
		// module, so a scene in a DLL asking for the exe's context type would abort too.
		template <typename T>
		void setContext(T& value) {
			static_assert(!std::is_const_v<T>, "setContext takes the context mutable; scenes may read it as const T");
			bindContext(&value, detail::typeKey<std::remove_cv_t<T>>());
		}
		template <typename T>
		T& context() {
			return *static_cast<T*>(contextFor(detail::typeKey<std::remove_cv_t<T>>()));
		}

		// settings() are AppConfig::settingsFile's, read at create and never saved by the App; a windowed
		// resize is kept in them as video.windowWidth and video.windowHeight, the size the next launch
		// opens at. saves() are over AppConfig::savesDirectory, in AppConfig::saveFormat.
		Input&						input();
		Settings&					settings();
		SaveStore&					saves();
		DevServer&					devServer(); // always there; its calls do nothing where the server isn't compiled in
		Clipboard&					clipboard();
		const Clock&				clock() const;
		const Renderer::Viewport&	viewport() const;
		Renderer::CoordinateSystem& coordinateSystem();
		// Null unless AppConfig::metrics.
		Renderer::MetricsCollector* metrics();

		// A perf capture (FrameCapture.h) of every frame from the next one on, with the renderer's GPU
		// timing on meanwhile; starting again starts over. The dev server's capture action runs these.
		void startCapture();
		// The capture's summary, with the framebuffer and vsync the run had, and the GPU timing back as
		// the capture found it; nullopt when none was running.
		std::optional<CaptureSummary> stopCapture();
		bool						  capturing() const;

		// Persistent across scene changes, above the stack. One added during a frame joins once the frame
		// is over.
		void addDebugLayer(std::unique_ptr<DebugLayer> layer);

	  private:
		friend class DevServer; // at step 3, whether exit has been asked for

		// Each section's length in the frame metrics times, in milliseconds. Pacing is in none of them,
		// only in the frame's time.
		struct Sections {
			float poll = 0.0F;	  // step 2
			float input = 0.0F;	  // steps 3 to 6
			float update = 0.0F;  // steps 7 and 8
			float layout = 0.0F;  // the ui layout pass, at the start of step 9
			float render = 0.0F;  // steps 9 and 10
			float present = 0.0F; // step 11
		};

		App(AppConfig appConfig, std::unique_ptr<Platform> appPlatform);
		bool start();
		bool startFonts();
		void openStorage();
		void applyWindowSettings();

		void inputEvent(const foundation::InputEvent& event) override;
		void deviceConnected(const DeviceInfo& device, double time) override;
		void deviceDisconnected(foundation::DeviceId device, double time) override;
		void framebufferResized(const Renderer::Viewport& newViewport) override;

		void  bindContext(void* value, const void* type);
		void* contextFor(const void* type) const;

		// The frame and its steps.
		void runFrame();
		void beginFrame();			// 1
		void pollEvents();			// 2
		void devServerPreFrame();	// 3
		void readFrameInput();		// 4
		void dispatchEvents();		// 5
		void applySceneChanges();	// 6
		void runFixedTicks();		// 7
		void updateScenes();		// 8
		void render();				// 9
		void devServerPostRender(); // 10
		void present();				// 11

		void  fixedTick();
		void  finishFrame();
		void  pace() const;
		float liveAlpha() const;
		void  endSection(float& section);
		void  showCursor();
		void  recordCapturedFrame(double frameSeconds);

		AppConfig									config;
		std::unique_ptr<Platform>					platform;
		Renderer::CoordinateSystem					coordinates;
		std::unique_ptr<Renderer::FontRenderer>		fontRenderer;
		std::unique_ptr<Renderer::MetricsCollector> metricsCollector;
		Input										inputState{*platform};
		Clipboard									clipboardState{*platform};
		SceneStack									sceneStack{*this};
		std::vector<std::unique_ptr<DebugLayer>>	debugLayers;
		std::vector<std::unique_ptr<DebugLayer>>	joiningDebugLayers;
		std::function<void(App&)>					dispatch;
		DevServer									devServerState;
		DevServer::Registration						uiTreeProvider;
		DevServer::Registration						uiLintProvider;
		Settings									userSettings;
		SaveStore									saveStore;
		void*										contextValue = nullptr;
		const void*									contextType = nullptr;
		foundation::InputSnapshot					tickInput{};

		double	   frameStart = 0.0;	   // the clock at step 1
		double	   frameDelta = 0.0;	   // this frame's real delta, clamped
		double	   fixedAccumulator = 0.0; // seconds not yet ticked
		int		   pendingSteps = 0;
		bool	   clockStarted = false;
		bool	   resized = false;
		bool	   framebufferEmpty = false; // minimized
		bool	   isPaused = false;
		bool	   resumeAtFrameEnd = false;
		bool	   exitRequested = false;
		bool	   inFrame = false;
		bool	   shuttingDown = false;
		bool	   isShutDown = false;
		bool	   metricsFrameOpen = false;
		double	   sectionStart = 0.0;
		Sections   sections;
		float	   scaleOfUi = 1.0F;
		bool	   motionReduced = false;
		ui::Cursor shownCursor = ui::Cursor::Default;

		FrameCapture	   frameCapture;
		bool			   frameInCapture = false; // this frame began while capturing
		double			   renderSeconds = 0.0;	   // step 9, timed while capturing
		std::uint64_t	   gpuFramesSeen = 0;
		std::uint64_t	   gpuDroppedAtCapture = 0;
		bool			   gpuTimingBeforeCapture = false;
		Renderer::Viewport captureViewport{};
	};

} // namespace app
