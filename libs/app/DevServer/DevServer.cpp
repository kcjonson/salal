#include "DevServer/DevServer.h"

#include "DevServer/DevRegistry.h"
#include "DevServer/DevServerHost.h"
#include "application/App.h"
#include "platform/Platform.h"
#include "scene/SceneStack.h"

#include <PngEncoder.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace app {

	namespace {
		using detail::DevJob;
		using detail::DevKind;
		using detail::errorReply;

		// An hour. Far past it the wait's deadline overflows, and every parked request would 503 at once.
		constexpr double kMaxRequestTimeout = 3600.0;

		using detail::dump;

		DevResponse okReply(const nlohmann::ordered_json& fields) {
			nlohmann::ordered_json body{{"status", "ok"}};
			body.update(fields);
			return {.body = dump(body)};
		}

		// Letters, digits, '-' and '_', and '.' where `dot` allows it.
		bool validName(std::string_view name, bool dot) {
			return !name.empty() && std::ranges::all_of(name, [dot](unsigned char c) {
				return std::isalnum(c) != 0 || c == '-' || c == '_' || (dot && c == '.');
			});
		}

		const char* kindName(DevKind kind) {
			switch (kind) {
				case DevKind::Route:
					return "dev route";
				case DevKind::Control:
					return "control action";
				case DevKind::State:
					return "state provider";
			}
			return "";
		}

		// A call into game code, logged as detail::guard logs one; what it threw comes back for the reply.
		template <typename F>
		std::optional<std::string> attempt(const std::string& owner, const char* what, F&& call) {
			try {
				std::forward<F>(call)();
				return std::nullopt;
			} catch (const std::exception& e) {
				LOG_ERROR(Engine, "Exception in %s::%s: %s", owner.c_str(), what, e.what());
				return std::string(e.what());
			} catch (...) {
				LOG_ERROR(Engine, "Unknown exception in %s::%s", owner.c_str(), what);
				return std::string("an unknown exception");
			}
		}

		// The backbuffer, read before present, as a PNG. GL reads the bottom row first; the window is
		// opaque on screen, so alpha is forced to 255 where blending left less.
		DevResponse screenshot() {
			int width = 0;
			int height = 0;
			Renderer::Primitives::readBackbuffer({}, width, height);
			if (width <= 0 || height <= 0) {
				return errorReply(500, "no backbuffer to read (" + std::to_string(width) + "x" + std::to_string(height) + ")");
			}
			std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
			Renderer::Primitives::readBackbuffer(pixels, width, height);
			std::vector<unsigned char> image = Renderer::flipRowsRGBA(pixels.data(), width, height);
			for (std::size_t alpha = 3; alpha < image.size(); alpha += 4) {
				image[alpha] = 255;
			}
			const std::vector<unsigned char> png = Renderer::encodePngToMemory(image.data(), width, height);
			if (png.empty()) {
				return errorReply(500, "PNG encoding failed");
			}
			return {.contentType = "image/png", .body = std::string(png.begin(), png.end())};
		}
	} // namespace

	// --- Registration ---

	DevServer::Registration::Registration(std::weak_ptr<detail::DevRegistry> owner, std::uint64_t entry)
		: registry(std::move(owner)),
		  id(entry) {}

	DevServer::Registration::~Registration() {
		reset();
	}

	DevServer::Registration::Registration(Registration&& other) noexcept
		: registry(std::move(other.registry)),
		  id(std::exchange(other.id, 0)) {}

	DevServer::Registration& DevServer::Registration::operator=(Registration&& other) noexcept {
		if (this != &other) {
			reset();
			registry = std::move(other.registry);
			id = std::exchange(other.id, 0);
		}
		return *this;
	}

	void DevServer::Registration::reset() {
		if (const std::shared_ptr<detail::DevRegistry> owner = registry.lock(); owner != nullptr && id != 0) {
			owner->remove(id);
		}
		registry.reset();
		id = 0;
	}

	bool DevServer::Registration::active() const {
		return id != 0 && !registry.expired();
	}

	// --- DevServer ---

	DevServer::DevServer()
		: registry(std::make_shared<detail::DevRegistry>()) {}

	DevServer::~DevServer() = default;

	DevServer::Registration DevServer::route(std::string verb, Handler handler) {
		std::ranges::transform(verb, verb.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (!handler || !validName(verb, false)) {
			LOG_ERROR(Engine, "DevServer::route('%s'): refused; a verb is letters, digits, '-' and '_', with a handler", verb.c_str());
			return {};
		}
		return {registry, registry->addHandler(DevKind::Route, std::move(verb), std::move(handler))};
	}

	DevServer::Registration DevServer::control(std::string action, Handler handler) {
		if (!handler || !validName(action, true) || detail::isBuiltInControl(action)) {
			LOG_ERROR(
				Engine,
				"DevServer::control('%s'): refused; an action is letters, digits, '-', '_' and '.', with a handler, and not a built-in one",
				action.c_str()
			);
			return {};
		}
		return {registry, registry->addHandler(DevKind::Control, std::move(action), std::move(handler))};
	}

	DevServer::Registration DevServer::stateProvider(std::string what, Provider provider) {
		if (!provider || !validName(what, true) || what == detail::kSceneState) {
			LOG_ERROR(
				Engine,
				"DevServer::stateProvider('%s'): refused; a what is letters, digits, '-', '_' and '.', with a provider, and not 'scene'",
				what.c_str()
			);
			return {};
		}
		return {registry, registry->addProvider(std::move(what), std::move(provider))};
	}

	bool DevServer::serving() const {
		return host != nullptr;
	}

	int DevServer::port() const {
		return host != nullptr ? host->port() : 0;
	}

	std::size_t DevServer::parkedRequests() const {
		return host != nullptr ? host->parked() : 0;
	}

	bool DevServer::start(const DevServerConfig& config) {
		if (!config.enabled) {
			return true;
		}
		if (config.port < 0 || config.port > 65535) {
			LOG_ERROR(Engine, "App::create: the dev server's port is 0 to 65535 (given %d)", config.port);
			return false;
		}
		if (!std::isfinite(config.requestTimeout) || config.requestTimeout <= 0.0) {
			LOG_ERROR(Engine, "App::create: the dev server's request timeout must be positive (given %g)", config.requestTimeout);
			return false;
		}
		DevServerConfig serving = config;
		if (serving.requestTimeout > kMaxRequestTimeout) {
			LOG_WARNING(Engine, "Dev server: a request timeout of %gs is held to %gs", serving.requestTimeout, kMaxRequestTimeout);
			serving.requestTimeout = kMaxRequestTimeout;
		}
		host = detail::DevServerHost::start(serving, registry);
		return host != nullptr;
	}

	void DevServer::stop() {
		host.reset();
	}

	// --- The frame ---

	namespace {
		DevResponse runHandler(const detail::DevRegistry& registry, DevKind kind, const DevRequest& request) {
			const DevServer::Handler handler = registry.handler(kind, request.name);
			if (!handler) {
				return errorReply(404, std::string("no ") + kindName(kind) + " '" + request.name + "'"); // removed since it was asked
			}
			DevResponse						 response;
			const std::optional<std::string> thrown = attempt(request.name, kindName(kind), [&] { response = handler(request); });
			if (thrown) {
				return errorReply(500, std::string(kindName(kind)) + " '" + request.name + "' threw: " + *thrown);
			}
			return response;
		}

		// The built-in actions but exit, which ~App answers. Their parameters were checked on the HTTP thread.
		DevResponse builtInControl(App& app, const DevRequest& request) {
			const std::string& action = request.name;
			if (action == "scene") {
				const std::string& scene = request.params.at("scene");
				return app.scenes().switchTo(scene) ? okReply({{"action", "scene"}, {"scene", scene}})
													: errorReply(404, "no scene added as '" + scene + "'");
			}
			if (action == "reload") {
				return app.scenes().reload()
						   ? okReply({{"action", "reload"}})
						   : errorReply(409, "the top scene can't be reloaded: there is none, or it was pushed with arguments");
			}
			if (action == "pause") {
				app.pause();
			} else if (action == "resume") {
				app.resume();
			} else if (action == "vsync") {
				const bool enabled = request.params.at("value") == "1";
				app.setVsync(enabled);
				LOG_INFO(Engine, "Vsync %s through the dev server", enabled ? "on" : "off");
			} else if (action == "step") {
				if (!app.paused()) {
					return errorReply(409, "step needs the app paused");
				}
				const int ticks = std::stoi(request.params.at("ticks"));
				app.step(ticks);
				return okReply({{"action", "step"}, {"ticks", ticks}});
			} else if (action == "capture") {
				if (request.params.at("op") == "start") {
					app.startCapture();
					return okReply({{"action", "capture"}, {"op", "start"}});
				}
				const std::optional<CaptureSummary> summary = app.stopCapture();
				if (!summary) {
					return errorReply(409, "no capture is running");
				}
				return okReply({{"action", "capture"}, {"op", "stop"}, {"capture", summary->toJson()}});
			}
			return okReply({{"action", action}});
		}
	} // namespace

	void DevServer::servePreFrame(App& app, PlatformEvents& events) {
		if (host == nullptr) {
			return;
		}
		for (const std::shared_ptr<DevJob>& job : host->takePreFrame()) {
			const DevRequest& request = job->request;
			if (job->kind == DevJob::Kind::Control && request.name == "exit") {
				LOG_INFO(Engine, "Exit requested through the dev server");
				app.requestExit();
				host->answerAfterShutdown(*job);
			} else if (app.exitRequested) {
				host->answer(*job, errorReply(503, "the app is exiting"));
			} else if (job->kind == DevJob::Kind::Route) {
				host->answer(*job, runHandler(*registry, DevKind::Route, request));
			} else if (detail::isBuiltInControl(request.name)) {
				host->answer(*job, builtInControl(app, request));
			} else {
				host->answer(*job, runHandler(*registry, DevKind::Control, request));
			}
		}
		if (app.exitRequested || app.paused()) {
			return;
		}
		// A virtual pad connects as a platform's pad does, with a name and a model GUID of its own, so a
		// slot waiting for a real pad's model never takes it.
		for (foundation::InputEvent& event : host->takeInput()) {
			event.time = app.clock().now();
			if (event.type == foundation::InputEvent::Type::DeviceConnected) {
				const std::string vpad = "vpad" + std::to_string(event.device.index - foundation::kMaxRealGamepads);
				events.deviceConnected({.id = event.device, .name = vpad, .modelGuid = "salal-dev-server-" + vpad}, event.time);
			} else if (event.type == foundation::InputEvent::Type::DeviceDisconnected) {
				events.deviceDisconnected(event.device, event.time);
			} else {
				events.inputEvent(event);
			}
		}
	}

	void DevServer::servePostRender(App& app) {
		if (host == nullptr) {
			return;
		}
		const Scene*						 top = app.scenes().top();
		const std::string					 topName = top != nullptr && top->name() != nullptr ? top->name() : "";
		Renderer::MetricsCollector*			 collector = app.metrics();
		const Foundation::PerformanceMetrics latest =
			collector != nullptr ? collector->getCurrentMetrics() : Foundation::PerformanceMetrics{};
		host->publish(collector != nullptr ? &latest : nullptr, topName);

		// Each stacked scene's name and exportState(), top first: its JSON as it gave it, or null for
		// none, and an error beside a null when it threw or gave something that isn't JSON.
		const auto sceneStates = [&app] {
			std::string							  json = R"({"scenes":[)";
			const std::vector<SceneStack::Entry>& entries = app.scenes().entries;
			for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
				const Scene&					 scene = *it->scene;
				const std::string				 name = scene.name() != nullptr ? scene.name() : "";
				std::string						 state;
				const std::optional<std::string> thrown = attempt(name, "exportState", [&] { state = scene.exportState(); });
				const bool						 valid = !thrown && !state.empty() && nlohmann::json::accept(state);
				json += (it == entries.rbegin() ? "" : ",");
				json += R"({"name":)" + dump(name) + R"(,"state":)" + (valid ? state : "null");
				if (thrown) {
					json += R"(,"error":)" + dump("exportState threw: " + *thrown);
				} else if (!state.empty() && !valid) {
					json += R"(,"error":"exportState gave text that isn't JSON")";
				}
				json += "}";
			}
			return json + "]}";
		};

		std::optional<DevResponse> shot; // one readback answers every screenshot this frame
		for (const std::shared_ptr<DevJob>& job : host->takePostRender()) {
			const std::string& what = job->request.name;
			if (job->kind == DevJob::Kind::Screenshot) {
				if (!shot) {
					shot = screenshot();
				}
				host->answer(*job, *shot);
			} else if (what == detail::kSceneState) {
				host->answer(*job, {.body = sceneStates()});
			} else if (const DevServer::Provider provider = registry->provider(what); provider) {
				std::string						 body;
				const std::optional<std::string> thrown = attempt(what, "stateProvider", [&] { body = provider(); });
				host->answer(*job, thrown ? errorReply(500, "state provider '" + what + "' threw: " + *thrown) : DevResponse{.body = body});
			} else if (what == detail::kUiTree) {
				const Renderer::Viewport& viewport = app.viewport();
				host->answer(
					*job,
					{.body = dump(
						 {{"viewport", {{"width", viewport.logicalWidth}, {"height", viewport.logicalHeight}}},
						  {"roots", nlohmann::ordered_json::array()}}
					 )}
				);
			} else if (what == detail::kUiLint) {
				host->answer(*job, {.body = dump({{"count", 0}, {"violations", nlohmann::ordered_json::array()}})});
			} else {
				host->answer(*job, errorReply(404, "no state provider for '" + what + "'")); // removed since it was asked
			}
		}
	}

} // namespace app
