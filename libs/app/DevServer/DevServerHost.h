#pragma once

// The dev server's HTTP side: cpp-httplib on 127.0.0.1, its routes, the mailbox that parks a request
// until the game thread answers it, the injected input waiting for the frame, and the log and metrics
// the streams read. Requests are taken and answered by DevServer on the game thread; everything here
// that a request touches is guarded, since cpp-httplib runs each request on a pool thread.
//
// A parked request waits for DevServerConfig::requestTimeout. One the game thread hasn't taken by
// then leaves the mailbox and is answered 503, so it is never run later; one it has taken is being
// run, and waits the moment more for its answer.

#include "DevServer/DevServer.h"
#include "DevServer/InputParser.h"
#include "DevServer/RingBuffer.h"

#include <input/InputTypes.h>
#include <metrics/PerformanceMetrics.h>
#include <utils/Log.h>

#include <nlohmann/json_fwd.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace httplib {
	class Server;
	struct Request;
	struct Response;
} // namespace httplib

namespace app::detail {

	class DevRegistry;

	// A request parked for the game thread.
	struct DevJob {
		enum class Kind : std::uint8_t { Control, Route, State, Screenshot };
		Kind		kind = Kind::Route;
		DevRequest	request;
		DevResponse response;
		bool		taken = false;		   // the game thread has it
		bool		done = false;		   // and has answered
		bool		afterShutdown = false; // an exit: answered once ~App has torn the app down
	};

	// One log line for /stream/logs.
	struct LogEntry {
		foundation::LogLevel	level{};
		foundation::LogCategory category{};
		char					message[256]{};
		std::uint64_t			timestamp = 0;	// Unix milliseconds
		const char*				file = nullptr; // __FILE__, static storage
		int						line = 0;
	};

	// The verb in /api/dev/<verb> (one path segment, as the route matches), lowercased; empty for any
	// other path.
	std::string parseDevVerb(const std::string& path);

	// JSON text, with invalid UTF-8 in a log message or a name replaced rather than thrown over.
	std::string dump(const nlohmann::ordered_json& json);

	// {"error":<message>} with `status`.
	DevResponse errorReply(int status, std::string_view message);

	class DevServerHost {
	  public:
		// Listening on 127.0.0.1 at config.port (any free port for 0), its logger sink installed; null,
		// logged, when the port can't be had.
		static std::unique_ptr<DevServerHost> start(const DevServerConfig& config, std::shared_ptr<const DevRegistry> registry);
		// Uninstalls the sink, answers exit requests (shutdown complete) and lets every other parked
		// request go (503), closes the port and joins the server's threads.
		~DevServerHost();

		DevServerHost(const DevServerHost&) = delete;
		DevServerHost& operator=(const DevServerHost&) = delete;
		DevServerHost(DevServerHost&&) = delete;
		DevServerHost& operator=(DevServerHost&&) = delete;

		int			port() const { return boundPort; }
		std::size_t parked() const;

		// The game thread's side. What it takes it must answer, each exactly once.
		std::vector<std::shared_ptr<DevJob>> takePreFrame();   // control actions, then routes, each in arrival order
		std::vector<std::shared_ptr<DevJob>> takePostRender(); // screenshots, then state, each in arrival order
		void								 answer(DevJob& job, DevResponse response);
		void								 answerAfterShutdown(DevJob& job);
		std::vector<foundation::InputEvent>	 takeInput();
		// The latest metrics (none without a collector) and the top scene's name, for the streams.
		void publish(const Foundation::PerformanceMetrics* metrics, std::string sceneName);

	  private:
		DevServerHost(const DevServerConfig& config, std::shared_ptr<const DevRegistry> registry);

		static void sink(void* context, const foundation::LogRecord& record);

		void		addRoutes();
		void		input(const httplib::Request& req, httplib::Response& res);
		void		control(const httplib::Request& req, httplib::Response& res);
		void		streamMetrics(httplib::Response& res);
		void		streamLogs(httplib::Response& res);
		bool		stateKnown(std::string_view what) const;
		DevResponse park(DevJob::Kind kind, DevRequest request);
		DevResponse exitReply(std::unique_lock<std::mutex>& lock);

		DevServerConfig					   config;
		std::shared_ptr<const DevRegistry> registry;
		std::unique_ptr<httplib::Server>   server;
		std::thread						   serverThread;
		int								   boundPort = 0;
		std::atomic<bool>				   streaming{false}; // the streams run while it holds

		mutable std::mutex					mailbox;
		std::condition_variable				changed;
		std::deque<std::shared_ptr<DevJob>> controls;
		std::deque<std::shared_ptr<DevJob>> routes;
		std::deque<std::shared_ptr<DevJob>> screenshots;
		std::deque<std::shared_ptr<DevJob>> states;
		std::vector<foundation::InputEvent> injected;
		VirtualPads							virtualPads{};
		bool								stopping = false;
		bool								shutdownComplete = false;

		mutable std::mutex			   publishedMutex;
		Foundation::PerformanceMetrics metrics{};
		std::string					   sceneName;

		RingBuffer<LogEntry, 1000> logs;
	};

} // namespace app::detail
