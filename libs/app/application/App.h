#pragma once

// The application: the window and its frame, input, the scene stack with the overlays above it,
// and the game's context object. A game's main makes the platform and the App, adds its scenes and
// pushes the first, then run()s, shutdown()s, tears its own resources down and destroys the App; a
// test drives tick() on a HeadlessPlatform, advancing its ManualClock.
//
// One App at a time: the renderer's Primitives are module-static, and the App starts and stops them.

#include "application/AppConfig.h"
#include "detail/TypeKey.h"
#include "input/Input.h"
#include "platform/Platform.h"
#include "scene/Overlay.h"
#include "scene/SceneStack.h"

#include <CoordinateSystem/CoordinateSystem.h>
#include <font/FontRenderer.h>
#include <input/InputTypes.h>
#include <metrics/MetricsCollector.h>

#include <functional>
#include <memory>
#include <type_traits>
#include <vector>

namespace app {

	class App final : private PlatformEvents {
	  public:
		// The App on `platform`, with the renderer started on the platform's backend and the game's
		// fonts loaded; null, logged, for no platform, a FrameConfig with a step, delta or tick cap that
		// isn't positive, no backend, font atlases that don't give the default family, or an App
		// already alive. Never exits the process.
		static std::unique_ptr<App> create(AppConfig config, std::unique_ptr<Platform> platform);

		// shutdown() if it hasn't run, then the fonts, the renderer and the window.
		~App();

		App(const App&) = delete;
		App& operator=(const App&) = delete;
		App(App&&) = delete;
		App& operator=(App&&) = delete;

		// One frame, as docs/app-api.md numbers it. False once exit is requested or the window's close
		// is, after finishing that frame; from then on, and after shutdown(), false with no frame run.
		// The clock starts at the first frame, whose delta is 0. Called from inside a frame it refuses,
		// logged, and returns false.
		bool tick();
		// The native runner: while (tick()) pace(), pacing being the platform's vsync and, with a
		// frameCapHz, a sleep then a spin to the cap. A clock that doesn't run in real time (a
		// HeadlessPlatform's) has nothing to pace against, so run() doesn't wait on it; a minimized
		// window sleeps a little each frame instead. Refused, logged, from inside a frame.
		void run();
		// The scenes exit top down, then the overlays, last added first, and all are destroyed; the
		// renderer and the window stay up for the game's teardown, until ~App. Inside a frame it asks
		// for exit instead, logged, since scenes are mid-call.
		void shutdown();
		void requestExit();

		// Fixed ticks and update stop for scenes and overlays, and scene changes wait; dispatch goes to
		// overlays only, and render goes on. A pause from inside a fixed tick ends that frame's ticks.
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

		// Replaces step 5's default dispatch (each event to the overlays, last added first, then the top
		// scene unless paused). Runs every frame, paused or not; an empty function restores the default.
		void setDispatch(std::function<void(App&)> dispatch);
		void setVsync(bool enabled);

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

		Input&						input();
		const Clock&				clock() const;
		const Renderer::Viewport&	viewport() const;
		Renderer::CoordinateSystem& coordinateSystem();
		// Null unless AppConfig::metrics.
		Renderer::MetricsCollector* metrics();

		// Persistent across scene changes, above the stack. One added during a frame joins once the frame
		// is over.
		void addOverlay(std::unique_ptr<Overlay> overlay);

	  private:
		// Each section's length in the frame metrics times, in milliseconds. Pacing is in none of them,
		// only in the frame's time.
		struct Sections {
			float poll = 0.0F;	  // step 2
			float input = 0.0F;	  // steps 3 to 6
			float update = 0.0F;  // steps 7 and 8
			float render = 0.0F;  // steps 9 and 10
			float present = 0.0F; // step 11
		};

		App(AppConfig appConfig, std::unique_ptr<Platform> appPlatform);
		bool start();
		bool startFonts();

		void inputEvent(const foundation::InputEvent& event) override;
		void framebufferResized(const Renderer::Viewport& newViewport) override;

		void  bindContext(void* value, const void* type);
		void* contextFor(const void* type) const;

		// The frame's steps; 3 and 10 are the dev server's.
		void beginFrame();		  // 1
		void pollEvents();		  // 2
		void readFrameInput();	  // 4
		void dispatchEvents();	  // 5
		void applySceneChanges(); // 6
		void runFixedTicks();	  // 7
		void updateScenes();	  // 8
		void render();			  // 9
		void present();			  // 11

		void  fixedTick();
		void  finishFrame();
		void  pace() const;
		float liveAlpha() const;
		void  endSection(float& section);

		AppConfig									config;
		std::unique_ptr<Platform>					platform;
		Renderer::CoordinateSystem					coordinates;
		std::unique_ptr<Renderer::FontRenderer>		fontRenderer;
		std::unique_ptr<Renderer::MetricsCollector> metricsCollector;
		Input										inputState;
		SceneStack									sceneStack{*this};
		std::vector<std::unique_ptr<Overlay>>		overlays;
		std::vector<std::unique_ptr<Overlay>>		joiningOverlays;
		std::function<void(App&)>					dispatch;
		void*										contextValue = nullptr;
		const void*									contextType = nullptr;
		foundation::InputSnapshot					tickInput{};

		double	 frameStart = 0.0;		 // the clock at step 1
		double	 frameDelta = 0.0;		 // this frame's real delta, clamped
		double	 fixedAccumulator = 0.0; // seconds not yet ticked
		int		 pendingSteps = 0;
		bool	 clockStarted = false;
		bool	 resized = false;
		bool	 framebufferEmpty = false; // minimized
		bool	 isPaused = false;
		bool	 resumeAtFrameEnd = false;
		bool	 exitRequested = false;
		bool	 inFrame = false;
		bool	 shuttingDown = false;
		bool	 isShutDown = false;
		bool	 metricsFrameOpen = false;
		double	 sectionStart = 0.0;
		Sections sections;
	};

} // namespace app
