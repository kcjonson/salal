#include "application/App.h"

#include "detail/Guard.h"
#include "storage/UserDirectories.h"

#include <primitives/Primitives.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <timeapi.h> // needs windows.h first
#endif

namespace app {

	namespace {
		// Primitives are module-static, and an App starts and stops them.
		bool g_appAlive = false; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

		bool validFrame(const FrameConfig& frame) {
			const bool valid = std::isfinite(frame.fixedStep) && frame.fixedStep > 0.0 && std::isfinite(frame.maxFrameDelta) &&
							   frame.maxFrameDelta > 0.0 && frame.maxTicksPerFrame >= 1 && frame.frameCapHz >= 0;
			if (!valid) {
				LOG_ERROR(
					Engine,
					"App::create: FrameConfig needs a positive fixedStep, maxFrameDelta and maxTicksPerFrame and a frameCapHz of 0 or more "
					"(given %g, %g, %d, %d)",
					frame.fixedStep,
					frame.maxFrameDelta,
					frame.maxTicksPerFrame,
					frame.frameCapHz
				);
			}
			return valid;
		}

		// A programming error in the game: logged, on stderr too for whoever reads the crash, then abort.
		[[noreturn]] void misuse(const char* message) {
			LOG_ERROR(Engine, "%s", message);
			std::fprintf(stderr, "%s\n", message);
			std::abort();
		}

		// A flag set for a scope and cleared however the scope ends.
		class ScopedFlag {
		  public:
			explicit ScopedFlag(bool& scoped)
				: flag(scoped) {
				flag = true;
			}
			~ScopedFlag() { flag = false; }
			ScopedFlag(const ScopedFlag&) = delete;
			ScopedFlag& operator=(const ScopedFlag&) = delete;
			ScopedFlag(ScopedFlag&&) = delete;
			ScopedFlag& operator=(ScopedFlag&&) = delete;

		  private:
			bool& flag;
		};

		// The OS timer at 1 ms while run() paces to a cap, so its sleeps wake on time; the Windows
		// default of 15.6 ms would hold frames near 60 FPS.
		class TimerResolution {
		  public:
			explicit TimerResolution(bool wanted)
				: active(wanted) {
#ifdef _WIN32
				if (active) {
					timeBeginPeriod(1);
				}
#endif
			}
			~TimerResolution() {
#ifdef _WIN32
				if (active) {
					timeEndPeriod(1);
				}
#endif
			}
			TimerResolution(const TimerResolution&) = delete;
			TimerResolution& operator=(const TimerResolution&) = delete;
			TimerResolution(TimerResolution&&) = delete;
			TimerResolution& operator=(TimerResolution&&) = delete;

		  private:
			[[maybe_unused]] bool active;
		};

		// The App's own keys in the settings.
		constexpr std::string_view kVsync = "video.vsync";
		constexpr std::string_view kFullscreenMonitor = "video.fullscreenMonitor";
		constexpr std::string_view kWindowWidth = "video.windowWidth";
		constexpr std::string_view kWindowHeight = "video.windowHeight";
		constexpr std::string_view kUiScale = "ui.scale";
		constexpr std::string_view kReducedMotion = "ui.reducedMotion";

		using UserDir = std::filesystem::path (*)(std::string_view organization, std::string_view application);

		// A relative path under the game's per-user directory; an absolute one as it is; empty, and a
		// relative one with no per-user directory to go under (logged), as none.
		std::filesystem::path userPath(const std::filesystem::path& path, UserDir userDir, const AppConfig& config, const char* what) {
			if (path.empty() || path.is_absolute()) {
				return path;
			}
			const std::filesystem::path root = userDir(config.organization, config.applicationName);
			if (root.empty()) {
				LOG_ERROR(Engine, "App::create: no per-user directory for the %s (organization and applicationName name it); none", what);
				return {};
			}
			return root / path;
		}

		bool sameSize(const Renderer::Viewport& a, const Renderer::Viewport& b) {
			return a.logicalWidth == b.logicalWidth && a.logicalHeight == b.logicalHeight && a.framebufferWidth == b.framebufferWidth &&
				   a.framebufferHeight == b.framebufferHeight;
		}

		// A resource path wherever findResource finds it; as given otherwise, so the error names it.
		std::string resourcePath(const std::string& path) {
			std::string found = Foundation::findResourceString(path);
			return found.empty() ? path : found;
		}
	} // namespace

	std::unique_ptr<App> App::create(AppConfig config, std::unique_ptr<Platform> platform) {
		foundation::Logger::initialize();
		if (g_appAlive) {
			LOG_ERROR(Engine, "App::create: another App is alive; the renderer is module-static, so there is one at a time");
			return nullptr;
		}
		if (platform == nullptr) {
			LOG_ERROR(Engine, "App::create: no platform");
			return nullptr;
		}
		if (!validFrame(config.frame)) {
			return nullptr;
		}
		if (config.settingsVersion < 1) {
			LOG_ERROR(Engine, "App::create: a settingsVersion of %d; versions start at 1", config.settingsVersion);
			return nullptr;
		}
		std::unique_ptr<App> created(new App(std::move(config), std::move(platform)));
		if (!created->start()) {
			return nullptr;
		}
		return created;
	}

	App::App(AppConfig appConfig, std::unique_ptr<Platform> appPlatform)
		: config(std::move(appConfig)),
		  platform(std::move(appPlatform)) {
		g_appAlive = true;
	}

	bool App::start() {
		Foundation::setResourceRoots(config.resourceRoots);
		openStorage();
		applyWindowSettings();

		const Renderer::Viewport window = platform->viewport();
		coordinates.setViewport(window);

		std::unique_ptr<Renderer::backend::Backend> backend = platform->createBackend();
		if (backend == nullptr) {
			LOG_ERROR(Engine, "App::create: the platform has no renderer backend");
			return false;
		}
		Renderer::Primitives::init(std::move(backend), config.baseShader);
		Renderer::Primitives::setCoordinateSystem(&coordinates);
		Renderer::Primitives::setViewport(window.framebufferWidth, window.framebufferHeight);

		if (!startFonts()) {
			return false;
		}
		if (config.metrics) {
			metricsCollector = std::make_unique<Renderer::MetricsCollector>();
		}
		if (!devServerState.start(config.devServer)) {
			return false;
		}
		// Registered first, so a game's own providers, registered after, answer instead.
		uiTreeProvider = devServerState.stateProvider("ui.tree", [this] { return sceneStack.uiTreeJson(coordinates.viewport()); });
		uiLintProvider = devServerState.stateProvider("ui.lint", [this] { return sceneStack.uiLintJson(coordinates.viewport()); });

		LOG_INFO(
			Engine,
			"App started%s%s: %dx%d (framebuffer %dx%d)",
			config.applicationName.empty() ? "" : " for ",
			config.applicationName.c_str(),
			window.logicalWidth,
			window.logicalHeight,
			window.framebufferWidth,
			window.framebufferHeight
		);
		return true;
	}

	void App::openStorage() {
		userSettings = Settings::load(
			userPath(config.settingsFile, userConfigDir, config, "settings"), config.settingsVersion, config.migrateSettings
		);
		scaleOfUi = std::clamp(userSettings.get<float>(kUiScale, 1.0F), 0.25F, 4.0F);
		motionReduced = userSettings.get<bool>(kReducedMotion, false);
		saveStore = SaveStore(userPath(config.savesDirectory, userDataDir, config, "saves"), config.saveFormat);
	}

	// The App's keys applied to the window before anything reads its size: vsync, the monitor, then the
	// windowed size, which is then where fullscreen returns to. A key that isn't there, or holds the wrong
	// type, leaves the window as its WindowConfig made it. The window is already showing, so a saved
	// fullscreen or size is a visible switch at launch.
	void App::applyWindowSettings() {
		if (const std::optional<bool> vsync = userSettings.get<std::optional<bool>>(kVsync, std::nullopt)) {
			platform->setVsync(*vsync);
		}
		if (const nlohmann::json* monitor = userSettings.find(kFullscreenMonitor); monitor != nullptr) {
			if (const auto chosen = detail::fromJson<std::optional<int>>(*monitor)) {
				platform->setFullscreen(*chosen);
			} else {
				LOG_WARNING(
					Engine, "App: video.fullscreenMonitor is a %s, not a monitor or null; the window stays as it is", monitor->type_name()
				);
			}
		}
		const int width = userSettings.get<int>(kWindowWidth, 0);
		const int height = userSettings.get<int>(kWindowHeight, 0);
		if (width > 0 && height > 0) {
			platform->setWindowSize(width, height);
		}
	}

	bool App::startFonts() {
		if (config.fontAtlases.empty()) {
			LOG_INFO(UI, "No font atlases given: no text");
			return true;
		}
		std::vector<Renderer::FontAtlasSource> atlases;
		atlases.reserve(config.fontAtlases.size());
		for (const Renderer::FontAtlasSource& atlas : config.fontAtlases) {
			atlases.push_back({.family = atlas.family, .pngPath = resourcePath(atlas.pngPath), .jsonPath = resourcePath(atlas.jsonPath)});
		}
		fontRenderer = std::make_unique<Renderer::FontRenderer>();
		if (!fontRenderer->Initialize(atlases, config.defaultFont)) {
			LOG_ERROR(UI, "App::create: the font atlases don't give the default family, so no text; stopping");
			fontRenderer.reset();
			return false;
		}
		Renderer::Primitives::setFontRenderer(fontRenderer.get());
		Renderer::Primitives::setFontAtlas(fontRenderer->getAtlasTexture(), 4.0F);
		LOG_INFO(UI, "Font renderer initialized with %zu atlases", atlases.size());
		return true;
	}

	App::~App() {
		shutdown();
		Renderer::Primitives::setFontRenderer(nullptr);
		fontRenderer.reset();
		Renderer::Primitives::shutdown();
		metricsCollector.reset();
		platform.reset();
		// An exit request through the server is answered here, once the scenes have exited and GL is gone.
		devServerState.stop();
		Foundation::setResourceRoots({});
		g_appAlive = false;
		LOG_INFO(Engine, "App destroyed");
	}

	// --- The frame ---

	bool App::tick() {
		if (inFrame) {
			LOG_ERROR(Engine, "App::tick inside a frame; refused");
			return false;
		}
		if (isShutDown || exitRequested || platform->closeRequested()) {
			return false;
		}
		{
			const ScopedFlag frame(inFrame);
			runFrame();
		}
		finishFrame();
		return !exitRequested && !platform->closeRequested();
	}

	// An exit asked for at step 3, through the dev server or a game action it ran, ends the frame there;
	// one asked for before it, by an onResize say, finishes the frame, as a scene's requestExit does.
	void App::runFrame() {
		beginFrame();
		pollEvents();
		endSection(sections.poll);
		const bool exitBefore = exitRequested;
		devServerPreFrame();
		if (exitRequested && !exitBefore) {
			return;
		}
		readFrameInput();
		dispatchEvents();
		applySceneChanges();
		endSection(sections.input);
		runFixedTicks();
		updateScenes();
		endSection(sections.update);
		sceneStack.layoutUi();
		showCursor();
		endSection(sections.layout);
		render();
		devServerPostRender();
		endSection(sections.render);
		present();
		endSection(sections.present);
	}

	// The clock starts at the first frame, so the first delta is 0 however long the game took to load.
	void App::beginFrame() {
		const double now = platform->clock().now();
		if (!clockStarted) {
			frameStart = now;
			clockStarted = true;
		}
		const double elapsed = now - frameStart;
		frameDelta = std::clamp(elapsed, 0.0, config.frame.maxFrameDelta);
		if (elapsed > config.frame.maxFrameDelta) {
			LOG_DEBUG(Engine, "Large delta time detected (%.3fs), capping to %.3fs", elapsed, config.frame.maxFrameDelta);
		}
		frameStart = now;

		// The previous frame closes here, so its time includes its pacing, which no section does.
		if (metricsCollector != nullptr) {
			if (metricsFrameOpen) {
				const Renderer::Primitives::RenderStats stats = Renderer::Primitives::getStats();
				metricsCollector->setRenderStats(stats.drawCalls, stats.vertexCount, stats.triangleCount);
				metricsCollector->setBatchStats(stats.instanceCount, stats.uploadBytes, stats.flushCount, stats.flushMs);
				metricsCollector->setMainLoopTimings(
					sections.poll, sections.input, sections.update, sections.layout, sections.render, sections.present
				);
				metricsCollector->endFrame();
			}
			metricsCollector->beginFrame();
			metricsFrameOpen = true;
		}
		if (frameInCapture && frameCapture.active()) {
			recordCapturedFrame(elapsed);
		}
		frameInCapture = frameCapture.active();
		if (metricsCollector != nullptr || frameInCapture) {
			sectionStart = now;
		}
	}

	void App::recordCapturedFrame(double frameSeconds) {
		const Renderer::Primitives::RenderStats stats = Renderer::Primitives::getStats();
		frameCapture.add({
			.frameMs = static_cast<float>(frameSeconds * 1000.0),
			.cpuMs = sections.poll + sections.input + sections.update + sections.layout + sections.render,
			.presentMs = sections.present,
			.layoutMs = sections.layout,
			.renderMs = static_cast<float>(renderSeconds * 1000.0),
			.flushMs = stats.flushMs,
			.drawCalls = stats.drawCalls,
			.vertices = stats.vertexCount,
			.triangles = stats.triangleCount,
			.instances = stats.instanceCount,
			.uploadBytes = stats.uploadBytes,
			.minimized = framebufferEmpty,
		});
		const Renderer::Primitives::GpuFrameTime gpu = Renderer::Primitives::getGpuFrameTime();
		if (gpu.frames != gpuFramesSeen) {
			gpuFramesSeen = gpu.frames;
			frameCapture.addGpu(gpu.ms);
		}
	}

	void App::pollEvents() {
		platform->pollEvents(*this);
		if (!resized) {
			return;
		}
		resized = false;
		const Renderer::Viewport& window = coordinates.viewport();
		for (auto it = debugLayers.rbegin(); it != debugLayers.rend(); ++it) {
			detail::guard("DebugLayer", "onResize", [&] { (*it)->onResize(window); });
		}
		sceneStack.resized(window);
	}

	void App::inputEvent(const foundation::InputEvent& event) {
		if (event.type == foundation::InputEvent::Type::DeviceConnected || event.type == foundation::InputEvent::Type::DeviceDisconnected) {
			LOG_WARNING(Engine, "A connection came through inputEvent, which has no name or model for it; dropped");
			return;
		}
		inputState.push(event);
	}

	void App::deviceConnected(const DeviceInfo& device, double time) {
		inputState.connect(device, time);
	}

	void App::deviceDisconnected(foundation::DeviceId device, double time) {
		inputState.disconnect(device, time);
	}

	// Inside the platform's callback, as worldsim's does; the scenes hear of it once the poll is over.
	// A minimized window reports an empty framebuffer, which resizes nothing: the scenes keep the size
	// they had, and run() sleeps rather than spins until a real size comes back.
	void App::framebufferResized(const Renderer::Viewport& newViewport) {
		framebufferEmpty = newViewport.framebufferWidth <= 0 || newViewport.framebufferHeight <= 0 || newViewport.logicalWidth <= 0 ||
						   newViewport.logicalHeight <= 0;
		if (framebufferEmpty) {
			return;
		}
		coordinates.setViewport(newViewport);
		Renderer::Primitives::setViewport(newViewport.framebufferWidth, newViewport.framebufferHeight);
		resized = true;
		if (!platform->fullscreenMonitor()) {
			userSettings.set(kWindowWidth, newViewport.logicalWidth);
			userSettings.set(kWindowHeight, newViewport.logicalHeight);
		}
	}

	// Control actions, then the game's routes; injected input reaches Input here, through the calls a
	// platform makes, unless paused, and waits in the server until resume otherwise.
	void App::devServerPreFrame() {
		devServerState.servePreFrame(*this, static_cast<PlatformEvents&>(*this));
	}

	void App::readFrameInput() {
		inputState.readFrame();
	}

	void App::dispatchEvents() {
		if (dispatch) {
			detail::guard("App", "dispatch", [&] { dispatch(*this); });
			return;
		}
		sceneStack.routeTopUi(inputState);
		for (const foundation::InputEvent& event : inputState.events()) {
			bool consumed = false;
			for (auto it = debugLayers.rbegin(); it != debugLayers.rend() && !consumed; ++it) {
				detail::guard("DebugLayer", "handleEvent", [&] { consumed = (*it)->handleEvent(event); });
			}
			if (consumed || isPaused) {
				continue;
			}
			if (sceneStack.dispatchTopUi(event)) {
				continue;
			}
			if (Scene* top = sceneStack.top(); top != nullptr) {
				detail::guard(top->name(), "handleEvent", [&] { top->handleEvent(event); });
			}
		}
	}

	// A new top's first tick sees held state only, not the press that put it there.
	void App::applySceneChanges() {
		if (isPaused) {
			return;
		}
		const std::uint64_t topBefore = sceneStack.topId();
		sceneStack.apply(liveAlpha());
		if (sceneStack.topId() != topBefore) {
			inputState.accumulator.consumeTick();
		}
	}

	// A paused or frozen frame consumes the tick reader and drops it, so a press made meanwhile
	// doesn't fire on resume. A tick that pauses ends the frame's ticks.
	void App::runFixedTicks() {
		const FrameConfig& frame = config.frame;
		if (isPaused) {
			// Steps asked for after an in-frame resume() wait for the frame's end, where it drops them.
			const int steps = resumeAtFrameEnd ? 0 : std::exchange(pendingSteps, 0);
			if (steps == 0) {
				inputState.accumulator.consumeTick();
			}
			for (int i = 0; i < steps; ++i) {
				fixedTick();
			}
			return;
		}
		if (frame.freezeTicksOnFocusLoss && !platform->focused()) {
			inputState.accumulator.consumeTick();
			return;
		}
		fixedAccumulator += frameDelta;
		for (int ticks = 0; fixedAccumulator >= frame.fixedStep && ticks < frame.maxTicksPerFrame && !isPaused; ++ticks) {
			fixedTick();
			fixedAccumulator -= frame.fixedStep;
		}
		// Past the cap, or once a tick paused, the whole steps left go and the part of a step stays, so
		// alpha runs on smoothly and resume doesn't replay them.
		if (fixedAccumulator >= frame.fixedStep) {
			fixedAccumulator = std::fmod(fixedAccumulator, frame.fixedStep);
		}
	}

	void App::fixedTick() {
		tickInput = inputState.accumulator.consumeTick();
		SceneStack::Entry* top = sceneStack.topEntry();
		if (top == nullptr) {
			return;
		}
		const TickContext tick{.tick = top->ticks, .step = config.frame.fixedStep, .input = tickInput};
		Scene&			  scene = *top->scene;
		detail::guard(scene.name(), "fixedUpdate", [&] { scene.fixedUpdate(tick); });
		++top->ticks;
	}

	void App::updateScenes() {
		if (isPaused) {
			return;
		}
		const FrameContext frame{.dt = frameDelta, .input = inputState.frame()};
		if (Scene* top = sceneStack.top(); top != nullptr) {
			detail::guard(top->name(), "update", [&] { top->update(frame); });
		}
		sceneStack.updateTopUi(frameDelta);
		for (const std::unique_ptr<DebugLayer>& layer : debugLayers) {
			detail::guard("DebugLayer", "update", [&] { layer->update(frame); });
		}
	}

	// The stack, each scene's ui over it, then the debug layers, each debug layer its own sort domain over
	// everything before it. The ui was laid out just before, paused or not, so a resize while paused
	// draws right. The frame's statistics and GPU time start here, once, so a pass that begins a batch of
	// its own inside the frame (an offscreen render) adds to them.
	void App::render() {
		const double start = frameInCapture ? platform->clock().now() : 0.0;
		Renderer::Primitives::beginFrameStats();
		Renderer::Primitives::beginFrame();
		if (fontRenderer != nullptr) {
			fontRenderer->updateFrame();
		}
		Renderer::Primitives::clear(config.clearColor);
		sceneStack.render(liveAlpha());
		for (const std::unique_ptr<DebugLayer>& layer : debugLayers) {
			Renderer::Primitives::sortBarrier();
			detail::guard("DebugLayer", "render", [&] { layer->render(); });
		}

		Renderer::Primitives::endFrame();
		if (frameInCapture) {
			renderSeconds = platform->clock().now() - start;
		}
	}

	// The screenshot, then the state providers, read before present.
	void App::devServerPostRender() {
		devServerState.servePostRender(*this);
	}

	void App::present() {
		platform->present();
	}

	void App::endSection(float& section) {
		if (metricsCollector == nullptr && !frameInCapture) {
			return;
		}
		const double now = platform->clock().now();
		section = static_cast<float>((now - sectionStart) * 1000.0);
		sectionStart = now;
	}

	void App::showCursor() {
		const ui::Cursor wanted = sceneStack.topUiCursor();
		if (wanted != shownCursor) {
			platform->setCursor(wanted);
			shownCursor = wanted;
		}
	}

	float App::liveAlpha() const {
		return static_cast<float>(fixedAccumulator / config.frame.fixedStep);
	}

	// What waits for the frame to be over: a resume asked for in it, so the frame stays paused and its
	// tick reader is dropped, and the debug layers added in it.
	void App::finishFrame() {
		if (std::exchange(resumeAtFrameEnd, false)) {
			if (pendingSteps > 0) {
				LOG_WARNING(Engine, "App::step: %d steps asked for after resume() in the same frame were dropped", pendingSteps);
			}
			resume();
		}
		for (std::unique_ptr<DebugLayer>& layer : joiningDebugLayers) {
			debugLayers.push_back(std::move(layer));
		}
		joiningDebugLayers.clear();
	}

	// --- Running and stopping ---

	void App::run() {
		if (inFrame) {
			LOG_ERROR(Engine, "App::run inside a frame; refused");
			return;
		}
		LOG_INFO(Engine, "Starting the main loop");
		const TimerResolution timer(config.frame.frameCapHz > 0);
		while (tick()) {
			pace();
		}
		LOG_INFO(Engine, "Main loop ended");
	}

	// Sleep coarsely, then spin to the cap's deadline: a sleep alone rounds up to the OS timer tick. A
	// clock that took no time over the frame, or stood still through a sleep, doesn't run in real time
	// (a ManualClock), so there is no deadline to wait for, and a spin gives up on a clock stuck for
	// longer than any real one is.
	void App::pace() const {
		constexpr auto	 kMinimizedFrame = std::chrono::milliseconds(10);
		constexpr double kSpinWindow = 0.002;
		constexpr int	 kStillReads = 100000;
		if (framebufferEmpty) {
			// Minimized: present may not wait for the display, so sleep rather than spin a core.
			std::this_thread::sleep_for(kMinimizedFrame);
			return;
		}
		if (config.frame.frameCapHz <= 0) {
			return;
		}
		const Clock& clock = platform->clock();
		const double deadline = frameStart + (1.0 / config.frame.frameCapHz);
		double		 now = clock.now();
		if (now == frameStart) {
			return;
		}
		if (deadline - now > kSpinWindow + 0.001) {
			std::this_thread::sleep_for(std::chrono::duration<double>(deadline - now - kSpinWindow));
			const double slept = clock.now();
			if (slept == now) {
				return;
			}
			now = slept;
		}
		for (int still = 0; now < deadline && still < kStillReads;) {
			std::this_thread::yield();
			const double later = clock.now();
			still = later == now ? still + 1 : 0;
			now = later;
		}
	}

	void App::shutdown() {
		if (inFrame) {
			LOG_ERROR(Engine, "App::shutdown inside a frame; requesting exit instead");
			requestExit();
			return;
		}
		if (shuttingDown) {
			return;
		}
		shuttingDown = true;
		if (!isShutDown) {
			LOG_INFO(Engine, "Shutting down");
		}
		isShutDown = true;
		sceneStack.exitAll();
		while (!debugLayers.empty()) {
			const std::unique_ptr<DebugLayer> layer = std::move(debugLayers.back());
			debugLayers.pop_back();
			detail::guard("DebugLayer", "onExit", [&] { layer->onExit(); });
		}
		shuttingDown = false;
	}

	void App::requestExit() {
		if (!exitRequested) {
			LOG_INFO(Engine, "Exit requested");
		}
		exitRequested = true;
	}

	void App::pause() {
		if (!isPaused) {
			LOG_INFO(Engine, "Paused");
		}
		isPaused = true;
		resumeAtFrameEnd = false;
	}

	void App::resume() {
		pendingSteps = 0;
		if (inFrame) {
			resumeAtFrameEnd = isPaused;
			return;
		}
		if (isPaused) {
			LOG_INFO(Engine, "Resumed");
		}
		isPaused = false;
		pendingSteps = 0;
	}

	bool App::paused() const {
		return isPaused;
	}

	void App::step(int ticks) {
		if (!isPaused) {
			LOG_WARNING(Engine, "App::step(%d) while not paused does nothing", ticks);
			return;
		}
		if (ticks > 0) {
			pendingSteps += ticks;
		}
	}

	void App::setDispatch(std::function<void(App&)> newDispatch) {
		dispatch = std::move(newDispatch);
	}

	void App::setUiScale(float scale) {
		scaleOfUi = std::clamp(scale, 0.25F, 4.0F);
		userSettings.set(kUiScale, scaleOfUi);
		sceneStack.scaleUi(scaleOfUi);
	}

	void App::setReducedMotion(bool reduced) {
		motionReduced = reduced;
		userSettings.set(kReducedMotion, motionReduced);
		sceneStack.reduceMotionUi(motionReduced);
	}

	void App::setVsync(bool enabled) {
		platform->setVsync(enabled);
		userSettings.set(kVsync, enabled);
	}

	// The platform's resize reaches the next poll; where the window has its new size already, the
	// coordinate system and Primitives follow it now rather than disagree with GL until then.
	void App::setFullscreen(std::optional<int> monitor) {
		platform->setFullscreen(monitor);
		userSettings.set(kFullscreenMonitor, monitor);
		const Renderer::Viewport window = platform->viewport();
		if (!sameSize(window, coordinates.viewport())) {
			framebufferResized(window);
		}
	}

	// --- What scenes reach ---

	SceneStack& App::scenes() {
		return sceneStack;
	}

	void App::bindContext(void* value, const void* type) {
		if (contextType != nullptr) {
			misuse("App::setContext called twice; the context is set once, before the first scene");
		}
		contextValue = value;
		contextType = type;
	}

	void* App::contextFor(const void* type) const {
		if (contextType == nullptr) {
			misuse("App::context with no context set");
		}
		if (type != contextType) {
			misuse("App::context asked for a type other than the one setContext was given");
		}
		return contextValue;
	}

	Input& App::input() {
		return inputState;
	}

	std::optional<int> App::fullscreenMonitor() const {
		return platform->fullscreenMonitor();
	}

	Settings& App::settings() {
		return userSettings;
	}

	SaveStore& App::saves() {
		return saveStore;
	}

	DevServer& App::devServer() {
		return devServerState;
	}

	Clipboard& App::clipboard() {
		return clipboardState;
	}

	const Clock& App::clock() const {
		return platform->clock();
	}

	const Renderer::Viewport& App::viewport() const {
		return coordinates.viewport();
	}

	Renderer::CoordinateSystem& App::coordinateSystem() {
		return coordinates;
	}

	Renderer::MetricsCollector* App::metrics() {
		return metricsCollector.get();
	}

	// GPU timing restarts with the capture, so a result from a frame before it isn't counted in it, and a
	// frame begun before it, under a capture started again, isn't recorded.
	void App::startCapture() {
		if (!frameCapture.active()) {
			gpuTimingBeforeCapture = Renderer::Primitives::getGpuTiming();
		}
		Renderer::Primitives::setGpuTiming(false);
		Renderer::Primitives::setGpuTiming(true);
		const Renderer::Primitives::GpuFrameTime gpu = Renderer::Primitives::getGpuFrameTime();
		gpuFramesSeen = gpu.frames;
		gpuDroppedAtCapture = gpu.dropped;
		captureViewport = coordinates.viewport();
		frameInCapture = false;
		frameCapture.start();
		LOG_INFO(Engine, "Capture started");
	}

	// The GPU timing goes back to how the capture found it.
	std::optional<CaptureSummary> App::stopCapture() {
		if (!frameCapture.active()) {
			return std::nullopt;
		}
		const Renderer::Primitives::GpuFrameTime gpu = Renderer::Primitives::getGpuFrameTime();
		Renderer::Primitives::setGpuTiming(gpuTimingBeforeCapture);
		frameInCapture = false;
		CaptureSummary summary = frameCapture.stop();
		summary.gpuDropped = gpu.dropped - gpuDroppedAtCapture;
		summary.gpuSupported = gpu.supported;
		const Renderer::Viewport& window = coordinates.viewport();
		summary.framebufferWidth = window.framebufferWidth;
		summary.framebufferHeight = window.framebufferHeight;
		summary.resized = !sameSize(window, captureViewport);
		summary.vsync = platform->vsync();
		LOG_INFO(
			Engine,
			"Capture stopped: %llu frames over %.2fs",
			static_cast<unsigned long long>(summary.frames), // NOLINT(google-runtime-int)
			summary.seconds
		);
		return summary;
	}

	bool App::capturing() const {
		return frameCapture.active();
	}

	void App::addDebugLayer(std::unique_ptr<DebugLayer> layer) {
		if (layer == nullptr) {
			return;
		}
		(inFrame ? joiningDebugLayers : debugLayers).push_back(std::move(layer));
	}

} // namespace app
