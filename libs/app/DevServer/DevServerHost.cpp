#include "DevServer/DevServerHost.h"

#include "DevServer/DevRegistry.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstring>
#include <map>
#include <optional>
#include <utility>

namespace app::detail {

	namespace {
		constexpr const char* kHost = "127.0.0.1";
		// How long an exit request waits for ~App to tear the app down before it answers anyway.
		constexpr auto kExitWait = std::chrono::seconds(30);
		constexpr auto kStreamPoll = std::chrono::milliseconds(10);
		constexpr auto kStreamInterval = std::chrono::milliseconds(100); // 10 Hz

		// Invalid UTF-8 in a log message or a name is replaced, never thrown over.
		std::string dump(const nlohmann::ordered_json& json) {
			return json.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
		}

		DevResponse jsonReply(const nlohmann::ordered_json& json) {
			return {.body = dump(json)};
		}

		void reply(httplib::Response& res, const DevResponse& response) {
			res.status = response.status;
			res.set_content(response.body, response.contentType);
		}

		// The query's parameters, the first of a repeated key, without `skip`.
		std::map<std::string, std::string> paramsOf(const httplib::Request& req, std::string_view skip = {}) {
			std::map<std::string, std::string> params;
			for (const auto& [key, value] : req.params) {
				if (key != skip) {
					params.emplace(key, value);
				}
			}
			return params;
		}

		std::uint64_t unixMilliseconds() {
			const auto now = std::chrono::system_clock::now().time_since_epoch();
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
		}

		std::string logJson(const LogEntry& entry) {
			return dump({
				{"level", foundation::levelToString(entry.level)},
				{"category", foundation::categoryToString(entry.category)},
				{"message", entry.message},
				{"timestamp", entry.timestamp},
				{"file", entry.file != nullptr ? entry.file : ""},
				{"line", entry.line},
			});
		}

		// A busy port fails the bind instead of being shared: SO_EXCLUSIVEADDRUSE on Windows, where a
		// plain bind can take a port another socket holds with SO_REUSEADDR; on POSIX, none of
		// cpp-httplib's default SO_REUSEPORT, which would let two servers listen on one port.
		void exclusiveAddress([[maybe_unused]] socket_t sock) {
#ifdef _WIN32
			const int exclusive = 1;
			setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#endif
		}

		// Checked on the HTTP thread, so a malformed built-in action is a 400 at once. `step` without
		// ticks steps one; its ticks are left normalised in the request.
		std::optional<DevResponse> invalidBuiltIn(DevRequest& request) {
			const auto param = [&request](const char* key) -> std::optional<std::string> {
				const auto it = request.params.find(key);
				return it != request.params.end() ? std::optional(it->second) : std::nullopt;
			};
			if (request.name == "scene" && param("scene").value_or("").empty()) {
				return errorReply(400, "Scene change requires 'scene' parameter");
			}
			if (request.name == "vsync" && param("value") != "0" && param("value") != "1") {
				return errorReply(400, "Vsync action requires 'value' parameter (0 or 1)");
			}
			if (request.name == "step") {
				const std::string text = param("ticks").value_or("1");
				int				  ticks = 0;
				const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), ticks);
				if (error != std::errc{} || end != text.data() + text.size() || ticks < 1) {
					return errorReply(400, "Step action takes 'ticks', a whole number from 1");
				}
				request.params["ticks"] = std::to_string(ticks);
			}
			return std::nullopt;
		}

		constexpr const char* kIndexPage = R"HTML(<!DOCTYPE html>
<html>
<head>
    <title>Developer Server</title>
    <style>
        body {
            font-family: 'Consolas', 'Monaco', monospace;
            background: #1e1e1e;
            color: #d4d4d4;
            padding: 40px;
            max-width: 800px;
            margin: 0 auto;
        }
        h1 { color: #4ec9b0; }
        .info { background: #252526; padding: 20px; margin: 20px 0; border-left: 3px solid #007acc; }
        code { background: #1e1e1e; padding: 2px 6px; color: #ce9178; }
        a { color: #4ec9b0; }
    </style>
</head>
<body>
    <h1>Developer Server Running</h1>

    <div class="info">
        <h2>API Endpoints Available:</h2>
        <ul>
            <li><a href="/api/health">/api/health</a> - Server health check</li>
            <li><a href="/api/metrics">/api/metrics</a> - Current performance metrics</li>
            <li><a href="/api/ui/screenshot">/api/ui/screenshot</a> - Capture screenshot (PNG)</li>
            <li><a href="/api/ui/tree">/api/ui/tree</a> - UI element tree</li>
            <li><a href="/api/ui/lint">/api/ui/lint</a> - UI layout lint</li>
            <li><a href="/api/state?what=scene">/api/state?what=scene</a> - The scene stack's state</li>
            <li><code>/api/input?ev=...</code> - Synthetic input</li>
            <li><code>/api/control?action=...</code> - exit, scene, pause, resume, reload, vsync, step, and the game's</li>
            <li><code>/api/dev/&lt;verb&gt;</code> - The game's routes</li>
            <li><a href="/stream/metrics">/stream/metrics</a> - Real-time metrics (SSE)</li>
            <li><a href="/stream/logs">/stream/logs</a> - Real-time logs (SSE)</li>
        </ul>
    </div>
</body>
</html>
)HTML";
	} // namespace

	std::string parseDevVerb(const std::string& path) {
		constexpr std::string_view kPrefix = "/api/dev/";
		if (path.size() <= kPrefix.size() || !path.starts_with(kPrefix)) {
			return {};
		}
		std::string verb = path.substr(kPrefix.size());
		if (const auto slash = verb.find('/'); slash != std::string::npos) {
			verb = verb.substr(0, slash);
		}
		std::ranges::transform(verb, verb.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return verb;
	}

	DevResponse errorReply(int status, std::string_view message) {
		return {.status = status, .body = dump({{"error", message}})};
	}

	// --- Starting and stopping ---

	DevServerHost::DevServerHost(const DevServerConfig& serverConfig, std::shared_ptr<const DevRegistry> registrations)
		: config(serverConfig),
		  registry(std::move(registrations)),
		  server(std::make_unique<httplib::Server>()) {}

	std::unique_ptr<DevServerHost> DevServerHost::start(const DevServerConfig& config, std::shared_ptr<const DevRegistry> registry) {
		std::unique_ptr<DevServerHost> host(new DevServerHost(config, std::move(registry)));
		httplib::Server&			   server = *host->server;
		server.set_socket_options(exclusiveAddress);
		server.set_default_headers({{"Access-Control-Allow-Origin", "*"}});
		host->addRoutes();

		host->boundPort = config.port == 0 ? server.bind_to_any_port(kHost) : (server.bind_to_port(kHost, config.port) ? config.port : -1);
		if (host->boundPort <= 0) {
			if (config.port == 0) {
				LOG_ERROR(Engine, "Dev server: no free port on %s", kHost);
			} else {
				LOG_ERROR(
					Engine,
					"Dev server: port %d is in use; another instance may be running (curl http://%s:%d/api/control?action=exit)",
					config.port,
					kHost,
					config.port
				);
			}
			return nullptr;
		}
		host->streaming = true;
		host->serverThread = std::thread([&server] { server.listen_after_bind(); });
		// stop() does nothing until the server runs, so the destructor could otherwise miss it.
		server.wait_until_ready();
		foundation::Logger::setSink(&DevServerHost::sink, host.get());
		LOG_INFO(Engine, "Dev server: http://%s:%d", kHost, host->boundPort);
		return host;
	}

	// The sink goes first: a thread mid-log must not reach a server that's gone. Exit requests answer
	// now that the app is torn down, and the rest are let go before the pool joins them.
	DevServerHost::~DevServerHost() {
		if (!serverThread.joinable()) {
			return;
		}
		foundation::Logger::setSink(nullptr, nullptr);
		{
			const std::lock_guard lock(mailbox);
			stopping = true;
			shutdownComplete = true;
		}
		streaming = false;
		changed.notify_all();
		server->stop();
		serverThread.join();
		LOG_INFO(Engine, "Dev server stopped");
	}

	void DevServerHost::sink(void* context, const foundation::LogRecord& record) {
		LogEntry entry{
			.level = record.level, .category = record.category, .timestamp = unixMilliseconds(), .file = record.file, .line = record.line
		};
		std::strncpy(entry.message, record.message, sizeof(entry.message) - 1);
		static_cast<DevServerHost*>(context)->logs.write(entry);
	}

	// --- The game thread's side ---

	std::size_t DevServerHost::parked() const {
		const std::lock_guard lock(mailbox);
		return controls.size() + routes.size() + screenshots.size() + states.size();
	}

	namespace {
		void moveInto(std::vector<std::shared_ptr<DevJob>>& taken, std::deque<std::shared_ptr<DevJob>>& queue) {
			for (std::shared_ptr<DevJob>& job : queue) {
				job->taken = true;
				taken.push_back(std::move(job));
			}
			queue.clear();
		}
	} // namespace

	std::vector<std::shared_ptr<DevJob>> DevServerHost::takePreFrame() {
		const std::lock_guard				 lock(mailbox);
		std::vector<std::shared_ptr<DevJob>> taken;
		moveInto(taken, controls);
		moveInto(taken, routes);
		return taken;
	}

	std::vector<std::shared_ptr<DevJob>> DevServerHost::takePostRender() {
		const std::lock_guard				 lock(mailbox);
		std::vector<std::shared_ptr<DevJob>> taken;
		moveInto(taken, screenshots);
		moveInto(taken, states);
		return taken;
	}

	void DevServerHost::answer(DevJob& job, DevResponse response) {
		{
			const std::lock_guard lock(mailbox);
			job.response = std::move(response);
			job.done = true;
		}
		changed.notify_all();
	}

	void DevServerHost::answerAfterShutdown(DevJob& job) {
		{
			const std::lock_guard lock(mailbox);
			job.afterShutdown = true;
			job.done = true;
		}
		changed.notify_all();
	}

	std::vector<foundation::InputEvent> DevServerHost::takeInput() {
		const std::lock_guard lock(mailbox);
		return std::exchange(injected, {});
	}

	void DevServerHost::publish(const Foundation::PerformanceMetrics* latest, std::string topScene) {
		const std::lock_guard lock(publishedMutex);
		if (latest != nullptr) {
			metrics = *latest;
		}
		sceneName = std::move(topScene);
	}

	// --- Parking ---

	bool DevServerHost::stateKnown(std::string_view what) const {
		return what == kSceneState || what == kUiTree || what == kUiLint || registry->has(DevKind::State, what);
	}

	DevResponse DevServerHost::park(DevJob::Kind kind, DevRequest request) {
		const auto							 job = std::make_shared<DevJob>(DevJob{.kind = kind, .request = std::move(request)});
		std::deque<std::shared_ptr<DevJob>>& queue = kind == DevJob::Kind::Control		? controls
													 : kind == DevJob::Kind::Route		? routes
													 : kind == DevJob::Kind::Screenshot ? screenshots
																						: states;
		const bool							 asksExit = kind == DevJob::Kind::Control && job->request.name == "exit";
		const auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(config.requestTimeout));

		std::unique_lock lock(mailbox);
		if (!stopping) {
			queue.push_back(job);
			changed.wait_for(lock, timeout, [&] { return job->done || (stopping && !job->taken); });
			if (job->taken && !job->done) {
				changed.wait(lock, [&] { return job->done; });
			}
		}
		if (job->done) {
			return job->afterShutdown ? exitReply(lock) : std::move(job->response);
		}
		std::erase(queue, job);
		if (stopping) {
			return asksExit ? exitReply(lock) : errorReply(503, "the app is shutting down");
		}
		lock.unlock();
		LOG_WARNING(Engine, "Dev server: '%s' waited %gs for a frame; dropped", job->request.name.c_str(), config.requestTimeout);
		return errorReply(503, "timed out waiting for the app's frame; dropped");
	}

	DevResponse DevServerHost::exitReply(std::unique_lock<std::mutex>& lock) {
		const bool complete = changed.wait_for(lock, kExitWait, [this] { return shutdownComplete; });
		return jsonReply({{"status", "ok"}, {"action", "exit"}, {"shutdown", complete ? "complete" : "timed out"}});
	}

	// --- Routes ---

	void DevServerHost::addRoutes() {
		httplib::Server& http = *server;

		http.Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
			const std::lock_guard lock(publishedMutex);
			reply(res, jsonReply({{"status", "ok"}, {"uptime", metrics.timestamp}}));
		});

		http.Get("/api/metrics", [this](const httplib::Request&, httplib::Response& res) {
			const std::lock_guard lock(publishedMutex);
			res.set_content(metrics.toJSON(), "application/json");
		});

		http.Get("/api/ui/screenshot", [this](const httplib::Request&, httplib::Response& res) {
			const DevResponse shot = park(DevJob::Kind::Screenshot, {.name = "screenshot"});
			reply(res, shot);
			if (shot.status == 200) {
				res.set_header("Content-Disposition", "inline; filename=\"screenshot.png\"");
			}
		});

		http.Get("/api/ui/tree", [this](const httplib::Request&, httplib::Response& res) {
			reply(res, park(DevJob::Kind::State, {.name = std::string(kUiTree)}));
		});

		http.Get("/api/ui/lint", [this](const httplib::Request&, httplib::Response& res) {
			reply(res, park(DevJob::Kind::State, {.name = std::string(kUiLint)}));
		});

		http.Get("/api/state", [this](const httplib::Request& req, httplib::Response& res) {
			const std::string what = req.has_param("what") ? req.get_param_value("what") : "summary";
			if (!stateKnown(what)) {
				reply(res, errorReply(404, "no state provider for '" + what + "'"));
				return;
			}
			reply(res, park(DevJob::Kind::State, {.name = what}));
		});

		http.Get(R"(/api/dev/[A-Za-z0-9_-]+)", [this](const httplib::Request& req, httplib::Response& res) {
			std::string verb = parseDevVerb(req.path);
			if (!registry->has(DevKind::Route, verb)) {
				reply(res, errorReply(404, "no dev route '" + verb + "'"));
				return;
			}
			reply(res, park(DevJob::Kind::Route, {.name = std::move(verb), .params = paramsOf(req)}));
		});

		http.Get("/api/input", [this](const httplib::Request& req, httplib::Response& res) { input(req, res); });
		http.Get("/api/control", [this](const httplib::Request& req, httplib::Response& res) { control(req, res); });
		http.Get("/stream/metrics", [this](const httplib::Request&, httplib::Response& res) { streamMetrics(res); });
		http.Get("/stream/logs", [this](const httplib::Request&, httplib::Response& res) { streamLogs(res); });
		http.Get("/", [](const httplib::Request&, httplib::Response& res) { res.set_content(kIndexPage, "text/html"); });
	}

	// Parsed and queued here, not parked: injected input waits in the server while the app is paused,
	// so a request that waited for it would time out. All of a request's events or none.
	void DevServerHost::input(const httplib::Request& req, httplib::Response& res) {
		const std::size_t count = req.get_param_value_count("ev");
		if (count == 0) {
			reply(res, errorReply(400, "Missing required parameter 'ev'"));
			return;
		}
		std::vector<std::string> evs;
		evs.reserve(count);
		for (std::size_t i = 0; i < count; ++i) {
			evs.push_back(req.get_param_value("ev", i));
		}

		std::unique_lock  lock(mailbox);
		ParsedInput		  parsed = parseInputEvents(evs, virtualPads);
		const std::size_t queued = evs.size();
		if (parsed.error.empty()) {
			injected.insert(injected.end(), parsed.events.begin(), parsed.events.end());
		}
		lock.unlock();

		if (!parsed.error.empty()) {
			reply(res, {.status = 400, .body = dump({{"error", parsed.error}, {"ev", parsed.failed}})});
			return;
		}
		reply(res, jsonReply({{"status", "ok"}, {"queued", queued}}));
	}

	void DevServerHost::control(const httplib::Request& req, httplib::Response& res) {
		if (!req.has_param("action")) {
			reply(res, errorReply(400, "Missing required parameter 'action'"));
			return;
		}
		DevRequest request{.name = req.get_param_value("action"), .params = paramsOf(req, "action")};
		if (isBuiltInControl(request.name)) {
			if (const std::optional<DevResponse> invalid = invalidBuiltIn(request)) {
				reply(res, *invalid);
				return;
			}
		} else if (!registry->has(DevKind::Control, request.name)) {
			reply(res, errorReply(404, "no control action '" + request.name + "'"));
			return;
		}
		const bool asksExit = request.name == "exit";
		reply(res, park(DevJob::Kind::Control, std::move(request)));
		if (asksExit) {
			res.set_header("Connection", "close");
		}
	}

	// --- Streams ---

	void DevServerHost::streamMetrics(httplib::Response& res) {
		res.set_header("Cache-Control", "no-cache");
		res.set_header("Connection", "keep-alive");
		res.set_chunked_content_provider("text/event-stream", [this](std::size_t /*offset*/, httplib::DataSink& sink) {
			auto last = std::chrono::steady_clock::now() - kStreamInterval;
			while (streaming && sink.is_writable()) {
				const auto now = std::chrono::steady_clock::now();
				if (now - last >= kStreamInterval) {
					std::string event;
					{
						const std::lock_guard lock(publishedMutex);
						std::string			  json = metrics.toJSON();
						json.pop_back(); // its closing brace, to add the scene's name inside it
						event = "event: metric\ndata: " + json + ",\"sceneName\":" + dump(sceneName) + "}\n\n";
					}
					if (!sink.write(event.data(), event.size())) {
						break;
					}
					last = now;
				}
				std::this_thread::sleep_for(kStreamPoll);
			}
			return false;
		});
	}

	// Each connection reads the ring from its own cursor, starting a ring's worth back so a new panel
	// sees recent history.
	void DevServerHost::streamLogs(httplib::Response& res) {
		res.set_header("Cache-Control", "no-cache");
		res.set_header("Connection", "keep-alive");
		res.set_chunked_content_provider("text/event-stream", [this](std::size_t /*offset*/, httplib::DataSink& sink) {
			constexpr std::size_t kRing = RingBuffer<LogEntry, 1000>::capacity();
			const std::size_t	  start = logs.writeCursor();
			std::size_t			  cursor = start > kRing ? start - kRing : 0;
			auto				  last = std::chrono::steady_clock::now() - kStreamInterval;
			while (streaming && sink.is_writable()) {
				const auto now = std::chrono::steady_clock::now();
				if (now - last >= kStreamInterval) {
					const std::size_t written = logs.writeCursor();
					cursor = std::max(cursor, written > kRing ? written - kRing : 0); // the oldest not yet overwritten
					LogEntry entry;
					while (cursor < written && logs.peekAt(cursor, entry)) {
						const std::string event = "event: log\ndata: " + logJson(entry) + "\n\n";
						if (!sink.write(event.data(), event.size())) {
							return false;
						}
						++cursor;
					}
					last = now;
				}
				std::this_thread::sleep_for(kStreamPoll);
			}
			return false;
		});
	}

} // namespace app::detail
