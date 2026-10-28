#pragma once

// The HTTP dev server: what a running game answers on 127.0.0.1 for tools, scripts and tests
// (docs/app-api.md, "Dev server"). The App owns it and starts it after the window when
// AppConfig::devServer asks; App::devServer() is always there.
//
// Compiled in where SALAL_DEV_SERVER is defined (Debug and RelWithDebInfo by default, Release only
// with SALAL_DEV_SERVER_IN_RELEASE). Elsewhere this API is still declared, every call is an inline
// no-op, and nothing links cpp-httplib or opens a socket, so a game's registrations build in every
// configuration without an #if.
//
// A request that needs the game parks its HTTP thread until the game thread answers it in the
// frame: control actions and routes at step 3, state providers and the screenshot at step 10,
// before present. Handlers therefore run on the game thread and never race it. A request the frame
// doesn't reach within DevServerConfig::requestTimeout is answered 503 and dropped, never run later.
//
// Registrations are handles. The newest live registration of a name answers it; destroying it, or
// reset(), hands the name back to the one before, if that is still alive, so a scene pushed over
// another can register the same verb in its onEnter and give it back when it pops. A registration
// or its removal takes effect at once, for requests already waiting as well as later ones, even
// from inside a handler. A handle that outlives the App does nothing when it dies.

#ifndef SALAL_DEV_SERVER
#include <utils/Log.h>
#endif

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace app {

	class App;
	class PlatformEvents;

	namespace detail {
		class DevRegistry;
		class DevServerHost;
	} // namespace detail

	struct DevServerConfig {
		bool   enabled = false;		  // the game sets it, from --http-port say; ignored where the server isn't compiled in
		int	   port = 0;			  // 0 to 65535; 0 takes any free port, which DevServer::port() reports
		double requestTimeout = 10.0; // seconds a request waits for the frame before it's answered 503 and dropped; at most an hour
	};

	// What a route or control action is asked: its verb or action, and the query's parameters, the
	// first of a repeated key.
	struct DevRequest {
		std::string						   name;
		std::map<std::string, std::string> params;
	};

	struct DevResponse {
		int			status = 200;
		std::string contentType = "application/json";
		std::string body;
	};

	class DevServer {
	  public:
		using Handler = std::function<DevResponse(const DevRequest&)>;
		using Provider = std::function<std::string()>; // JSON

		// Move-only; unregisters when destroyed. An inactive one holds nothing: default-made, moved
		// from, reset, refused, or from a build without the server.
		class Registration {
		  public:
			Registration() = default;
			~Registration();
			Registration(Registration&& other) noexcept;
			Registration& operator=(Registration&& other) noexcept;
			Registration(const Registration&) = delete;
			Registration& operator=(const Registration&) = delete;

			void reset();
			bool active() const;

		  private:
			friend class DevServer;
#ifdef SALAL_DEV_SERVER
			Registration(std::weak_ptr<detail::DevRegistry> owner, std::uint64_t entry);

			std::weak_ptr<detail::DevRegistry> registry;
			std::uint64_t					   id = 0;
#endif
		};

		DevServer(const DevServer&) = delete;
		DevServer& operator=(const DevServer&) = delete;
		DevServer(DevServer&&) = delete;
		DevServer& operator=(DevServer&&) = delete;

		// /api/dev/<verb>, answered at step 3. Verbs are letters, digits, '-' and '_', matched
		// without case. Refused, logged, as an inactive handle, for an empty handler or a bad verb.
		[[nodiscard]] Registration route(std::string verb, Handler handler);
		// /api/control?action=<action>, answered at step 3 after the actions before it. The built-in
		// actions (exit, scene, pause, resume, reload, vsync, step) can't be registered.
		[[nodiscard]] Registration control(std::string action, Handler handler);
		// /api/state?what=<what>, answered at step 10 with the provider's JSON; "ui.tree" and
		// "ui.lint" also answer /api/ui/tree and /api/ui/lint, in place of the empty defaults.
		// "scene" is the server's own and can't be registered. Actions and whats are letters,
		// digits, '-', '_' and '.', matched with case.
		[[nodiscard]] Registration stateProvider(std::string what, Provider provider);

		// Listening now.
		bool serving() const;
		// The port it listens on; 0 when it isn't.
		int port() const;
		// Requests parked for the frame to answer them now.
		std::size_t parkedRequests() const;

	  private:
		friend class App;

		DevServer();
		~DevServer();

		// From App::create, after the window: listens when the config asks; false, logged, when it
		// asks for a port that's busy or out of range, or a timeout that isn't positive.
		bool start(const DevServerConfig& config);
		// Step 3: control actions, then routes, then the injected input, which reaches `events` as a
		// platform's would (a virtual pad's connection through deviceConnected, so Input lists it) unless
		// the app is paused. Once exit has been asked for, what is left is answered 503 without being
		// run, and the input stays queued; an exit asked for here ends the frame.
		void servePreFrame(App& app, PlatformEvents& events);
		// Step 10, before present: the screenshot, then the state providers; the metrics and the top
		// scene's name for the streams.
		void servePostRender(App& app);
		// From ~App, once scenes have exited and GL is torn down: an exit request is answered, every
		// parked request is let go, and the port is closed.
		void stop();

#ifdef SALAL_DEV_SERVER
		std::shared_ptr<detail::DevRegistry>   registry;
		std::unique_ptr<detail::DevServerHost> host;
#endif
	};

#ifndef SALAL_DEV_SERVER
	inline DevServer::Registration::~Registration() = default;
	inline DevServer::Registration::Registration(Registration&& /*other*/) noexcept {}
	inline DevServer::Registration& DevServer::Registration::operator=(Registration&& /*other*/) noexcept {
		return *this;
	}
	inline void DevServer::Registration::reset() {}
	inline bool DevServer::Registration::active() const {
		return false;
	}

	inline DevServer::DevServer() = default;
	inline DevServer::~DevServer() = default;
	inline DevServer::Registration DevServer::route(std::string /*verb*/, Handler /*handler*/) {
		return {};
	}
	inline DevServer::Registration DevServer::control(std::string /*action*/, Handler /*handler*/) {
		return {};
	}
	inline DevServer::Registration DevServer::stateProvider(std::string /*what*/, Provider /*provider*/) {
		return {};
	}
	inline bool DevServer::serving() const {
		return false;
	}
	inline int DevServer::port() const {
		return 0;
	}
	inline std::size_t DevServer::parkedRequests() const {
		return 0;
	}
	// LOG_ERROR, the one level a Release build keeps, so a profiling run on a build without the server
	// says why nothing listens.
	inline bool DevServer::start(const DevServerConfig& config) {
		if (config.enabled) {
			LOG_ERROR(
				Engine,
				"The dev server is enabled but not compiled into this configuration, so nothing listens (SALAL_WITH_DEV_SERVER, "
				"SALAL_DEV_SERVER_IN_RELEASE)"
			);
		}
		return true;
	}
	inline void DevServer::servePreFrame(App& /*app*/, PlatformEvents& /*events*/) {}
	inline void DevServer::servePostRender(App& /*app*/) {}
	inline void DevServer::stop() {}
#endif

} // namespace app
