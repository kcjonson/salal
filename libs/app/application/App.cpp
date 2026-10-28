#include "application/App.h"

#include "detail/Guard.h"

#include <primitives/Primitives.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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

		frameStart = platform->clock().now();
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
		g_appAlive = false;
		LOG_INFO(Engine, "App destroyed");
	}

	// --- The frame ---

	bool App::tick() {
		if (isShutDown || exitRequested || platform->closeRequested()) {
			return false;
		}
		inFrame = true;
		beginFrame();
		pollEvents();
		endSection(sections.poll);
		readFrameInput();
		dispatchEvents();
		applySceneChanges();
		endSection(sections.input);
		runFixedTicks();
		updateScenes();
		endSection(sections.update);
		render();
		endSection(sections.render);
		present();
		inFrame = false;
		joinOverlays();
		return !exitRequested && !platform->closeRequested();
	}

	void App::beginFrame() {
		const double now = platform->clock().now();
		const double elapsed = now - frameStart;
		frameDelta = std::clamp(elapsed, 0.0, config.frame.maxFrameDelta);
		if (elapsed > config.frame.maxFrameDelta) {
			LOG_DEBUG(Engine, "Large delta time detected (%.3fs), capping to %.3fs", elapsed, config.frame.maxFrameDelta);
		}
		frameStart = now;

		// The previous frame closes here, so its time includes its present and pacing.
		if (metricsCollector != nullptr) {
			if (metricsFrameOpen) {
				const Renderer::Primitives::RenderStats stats = Renderer::Primitives::getStats();
				metricsCollector->setRenderStats(stats.drawCalls, stats.vertexCount, stats.triangleCount);
				metricsCollector->setMainLoopTimings(
					sections.poll, sections.input, sections.update, sections.render, static_cast<float>((now - presentStart) * 1000.0)
				);
				metricsCollector->endFrame();
			}
			metricsCollector->beginFrame();
			metricsFrameOpen = true;
			sectionStart = now;
		}
	}

	void App::pollEvents() {
		platform->pollEvents(*this);
		if (!resized) {
			return;
		}
		resized = false;
		const Renderer::Viewport& window = coordinates.viewport();
		for (auto it = overlays.rbegin(); it != overlays.rend(); ++it) {
			detail::guard("Overlay", "onResize", [&] { (*it)->onResize(window); });
		}
		for (auto it = sceneStack.entries.rbegin(); it != sceneStack.entries.rend(); ++it) {
			Scene& scene = *it->scene;
			detail::guard(scene.name(), "onResize", [&] { scene.onResize(window); });
		}
	}

	void App::inputEvent(const foundation::InputEvent& event) {
		inputState.accumulator.push(event);
	}

	// Inside the platform's callback, as worldsim's does; the scenes hear of it once the poll is over.
	void App::framebufferResized(const Renderer::Viewport& newViewport) {
		coordinates.setViewport(newViewport);
		Renderer::Primitives::setViewport(newViewport.framebufferWidth, newViewport.framebufferHeight);
		resized = true;
	}

	void App::readFrameInput() {
		inputState.frameState = inputState.accumulator.consumeFrame();
	}

	void App::dispatchEvents() {
		if (dispatch) {
			detail::guard("App", "dispatch", [&] { dispatch(*this); });
			return;
		}
		for (const foundation::InputEvent& event : inputState.events()) {
			bool consumed = false;
			for (auto it = overlays.rbegin(); it != overlays.rend() && !consumed; ++it) {
				detail::guard("Overlay", "handleEvent", [&] { consumed = (*it)->handleEvent(event); });
			}
			if (consumed || isPaused) {
				continue;
			}
			if (Scene* top = sceneStack.top(); top != nullptr) {
				detail::guard(top->name(), "handleEvent", [&] { top->handleEvent(event); });
			}
		}
	}

	void App::applySceneChanges() {
		if (!isPaused) {
			sceneStack.apply(liveAlpha());
		}
	}

	// A paused or frozen frame consumes the tick reader and drops it, so a press made meanwhile
	// doesn't fire on resume.
	void App::runFixedTicks() {
		const FrameConfig& frame = config.frame;
		if (isPaused) {
			const int steps = std::exchange(pendingSteps, 0);
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
		for (int ticks = 0; fixedAccumulator >= frame.fixedStep && ticks < frame.maxTicksPerFrame; ++ticks) {
			fixedTick();
			fixedAccumulator -= frame.fixedStep;
		}
		// Past the cap the whole steps left go, and the part of a step stays, so alpha runs on smoothly.
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
		for (const std::unique_ptr<Overlay>& overlay : overlays) {
			detail::guard("Overlay", "update", [&] { overlay->update(frame); });
		}
	}

	// The stack from its topmost opaque scene up, each covered scene at the alpha it had when covered,
	// then the overlays.
	void App::render() {
		Renderer::Primitives::beginFrame();
		if (fontRenderer != nullptr) {
			fontRenderer->updateFrame();
		}
		Renderer::Primitives::clear(config.clearColor);

		const std::vector<SceneStack::Entry>& entries = sceneStack.entries;
		std::size_t							  first = entries.size();
		while (first > 0) {
			Scene& scene = *entries[first - 1].scene;
			bool   opaque = true;
			detail::guard(scene.name(), "opaque", [&] { opaque = scene.opaque(); });
			--first;
			if (opaque) {
				break;
			}
		}
		for (std::size_t i = first; i < entries.size(); ++i) {
			Scene&		scene = *entries[i].scene;
			const float alpha = i + 1 == entries.size() ? liveAlpha() : entries[i].coveredAlpha;
			detail::guard(scene.name(), "render", [&] { scene.render(alpha); });
		}
		for (const std::unique_ptr<Overlay>& overlay : overlays) {
			detail::guard("Overlay", "render", [&] { overlay->render(); });
		}

		Renderer::Primitives::endFrame();
	}

	void App::present() {
		if (metricsCollector != nullptr) {
			presentStart = platform->clock().now();
		}
		platform->present();
	}

	void App::endSection(float& section) {
		if (metricsCollector == nullptr) {
			return;
		}
		const double now = platform->clock().now();
		section = static_cast<float>((now - sectionStart) * 1000.0);
		sectionStart = now;
	}

	float App::liveAlpha() const {
		return static_cast<float>(fixedAccumulator / config.frame.fixedStep);
	}

	void App::joinOverlays() {
		for (std::unique_ptr<Overlay>& overlay : joiningOverlays) {
			overlays.push_back(std::move(overlay));
		}
		joiningOverlays.clear();
	}

	// --- Running and stopping ---

	void App::run() {
		LOG_INFO(Engine, "Starting the main loop");
		const bool capped = config.frame.frameCapHz > 0;
#ifdef _WIN32
		// Sleeps wake within about 1 ms instead of the default 15.6 ms tick, which would cap frames near 60 FPS.
		if (capped) {
			timeBeginPeriod(1);
		}
#endif
		frameStart = platform->clock().now();
		while (tick()) {
			if (capped) {
				pace();
			}
		}
#ifdef _WIN32
		if (capped) {
			timeEndPeriod(1);
		}
#endif
		LOG_INFO(Engine, "Main loop ended");
	}

	// Sleep coarsely, then spin to the frame's deadline: a sleep alone rounds up to the OS timer tick.
	void App::pace() const {
		constexpr double kSpinWindow = 0.002;
		const Clock&	 clock = platform->clock();
		const double	 deadline = frameStart + (1.0 / config.frame.frameCapHz);
		const double	 remaining = deadline - clock.now();
		if (remaining > kSpinWindow + 0.001) {
			std::this_thread::sleep_for(std::chrono::duration<double>(remaining - kSpinWindow));
		}
		while (clock.now() < deadline) {
			std::this_thread::yield();
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
		while (!overlays.empty()) {
			const std::unique_ptr<Overlay> overlay = std::move(overlays.back());
			overlays.pop_back();
			detail::guard("Overlay", "onExit", [&] { overlay->onExit(); });
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
	}

	void App::resume() {
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

	void App::setVsync(bool enabled) {
		platform->setVsync(enabled);
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

	void App::addOverlay(std::unique_ptr<Overlay> overlay) {
		if (overlay == nullptr) {
			return;
		}
		(inFrame ? joiningOverlays : overlays).push_back(std::move(overlay));
	}

} // namespace app
