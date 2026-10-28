// The dev server against an App on a HeadlessPlatform, over loopback on a port the OS picks: each
// route's status and body, registrations and their handles, the control actions with step and the
// input that waits out a pause, the providers and their defaults, the scene stack's state, the new
// input events through to the tick snapshot, where in the frame each is answered, a busy port, a
// request the frame never reaches, and exit answered only once the App is gone. A request that waits
// for the frame is sent from another thread, left to park, and answered by exactly one frame.
//
// Without the server compiled in, the same API, doing nothing.

#include "DevServer/DevServer.h"
#include "application/App.h"
#include "tests/AppHarness.h"

#include <backend/NullBackend.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>

#include <gtest/gtest.h>

#ifdef SALAL_DEV_SERVER
#include <httplib.h>
#include <nlohmann/json.hpp>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace app::testing {

	namespace {

		inline constexpr char kMain[] = "Main";
		inline constexpr char kSecond[] = "Second";

		using MainScene = Recording<kMain>;
		using SecondScene = Recording<kSecond>;

		DevResponse ok(std::string body = "{}") {
			return {.body = std::move(body)};
		}

	} // namespace

#ifdef SALAL_DEV_SERVER

	static_assert(kDevServerCompiledIn);

	namespace {

		using foundation::DeviceKind;
		using foundation::GamepadAxis;
		using foundation::GamepadButton;
		using foundation::InputEvent;
		using foundation::Key;
		using foundation::MouseButton;

		struct Reply {
			int			status = -1; // no connection
			std::string body;
			std::string contentType;
			std::string disposition;
			std::string connection;
			std::string allowOrigin;
		};

		Reply fetch(int port, const std::string& path) {
			httplib::Client client("127.0.0.1", port);
			client.set_read_timeout(std::chrono::seconds(30));
			const httplib::Result result = client.Get(path);
			if (!result) {
				return {};
			}
			return {
				.status = result->status,
				.body = result->body,
				.contentType = result->get_header_value("Content-Type"),
				.disposition = result->get_header_value("Content-Disposition"),
				.connection = result->get_header_value("Connection"),
				.allowOrigin = result->get_header_value("Access-Control-Allow-Origin"),
			};
		}

		// What a scene pushed with arguments exports: its label is its name, and a state of "throw" throws.
		struct StateArgs {
			std::string label;
			std::string state;
		};

		class StateScene final : public TypedScene<StateArgs> {
		  public:
			explicit StateScene(StateArgs&& sceneArgs)
				: args(std::move(sceneArgs)) {}
			const char* name() const override { return args.label.c_str(); }
			void		render(float /*alpha*/) override {}
			std::string exportState() const override {
				if (args.state == "throw") {
					throw std::runtime_error("nope");
				}
				return args.state;
			}

		  private:
			StateArgs args;
		};

		// A scene whose name() is null, which the server reads at step 10 every frame.
		class NamelessScene final : public TypedScene<> {
		  public:
			const char* name() const override { return nullptr; }
			void		render(float /*alpha*/) override {}
		};

		// A headless platform that writes its presents and vsync changes into the journal.
		class PresentingPlatform final : public Platform {
		  public:
			const Clock&								clock() const override { return time; }
			Renderer::Viewport							viewport() const override { return {800, 600, 800, 600}; }
			std::unique_ptr<Renderer::backend::Backend> createBackend() override {
				auto made = std::make_unique<Renderer::backend::NullBackend>();
				backend = made.get();
				return made;
			}
			void					   pollEvents(PlatformEvents& /*events*/) override {}
			bool					   focused() const override { return true; }
			bool					   closeRequested() const override { return false; }
			void					   present() override { record("platform", "present"); }
			void					   setVsync(bool enabled) override { record("platform", enabled ? "vsync on" : "vsync off"); }
			void					   setFullscreen(std::optional<int> /*monitor*/) override {}
			std::optional<int>		   fullscreenMonitor() const override { return std::nullopt; }
			void					   setWindowSize(int /*width*/, int /*height*/) override {}
			std::optional<std::string> keyLabel(foundation::Key /*key*/) const override { return std::nullopt; }
			std::string				   clipboardText() const override { return {}; }
			void					   setClipboardText(const std::string& /*text*/) override {}

			ManualClock						time;
			Renderer::backend::NullBackend* backend = nullptr; // the renderer's, alive as long as the App
		};

		const foundation::ButtonState& keyIn(const foundation::InputSnapshot& snapshot, Key key) {
			return snapshot.keys.at(static_cast<std::size_t>(key));
		}

		class DevServerTest : public AppTest {
		  protected:
			static AppConfig served(double requestTimeout = 5.0) {
				AppConfig serving = config();
				serving.devServer = {.enabled = true, .port = 0, .requestTimeout = requestTimeout};
				return serving;
			}

			App& serve(AppConfig appConfig = served()) {
				App& started = start(std::move(appConfig));
				EXPECT_TRUE(started.devServer().serving());
				return started;
			}

			int port() const { return app->devServer().port(); }

			// A route that answers without a frame.
			Reply get(const std::string& path) const { return fetch(port(), path); }

			std::future<Reply> send(const std::string& path) const {
				return std::async(std::launch::async, [served = port(), path] { return fetch(served, path); });
			}

			void waitParked(std::size_t count) const {
				for (int i = 0; i < 5000 && app->devServer().parkedRequests() < count; ++i) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				ASSERT_GE(app->devServer().parkedRequests(), count);
			}

			// A route that waits for the frame: parked, then one frame, which answers it.
			Reply ask(const std::string& path) {
				std::future<Reply> pending = send(path);
				waitParked(1);
				frame();
				return pending.get();
			}

			DevServer& server() { return app->devServer(); }
		};

		const std::string kTimedOut = R"({"error":"timed out waiting for the app's frame; dropped"})";

	} // namespace

	// --- Starting ---

	TEST_F(DevServerTest, ServesHealthOnThePortTheOsPicked) {
		serve();
		EXPECT_GT(port(), 0);
		const Reply health = get("/api/health");
		EXPECT_EQ(health.status, 200);
		EXPECT_EQ(health.contentType, "application/json");
		EXPECT_EQ(health.body, R"({"status":"ok","uptime":0})");
	}

	// The developer client reads the routes and streams from a page of its own origin.
	TEST_F(DevServerTest, EveryReplyAllowsAnyOrigin) {
		serve();
		EXPECT_EQ(get("/api/health").allowOrigin, "*");
		const Reply missing = get("/api/dev/nothing");
		EXPECT_EQ(missing.status, 404);
		EXPECT_EQ(missing.allowOrigin, "*");

		std::string		stream;
		std::string		origin;
		httplib::Client client("127.0.0.1", port());
		client.Get(
			"/stream/metrics",
			[&origin](const httplib::Response& response) {
				origin = response.get_header_value("Access-Control-Allow-Origin");
				return true;
			},
			[&stream](const char* data, std::size_t size) {
				stream.append(data, size);
				return false;
			}
		);
		EXPECT_EQ(origin, "*");
		EXPECT_TRUE(stream.starts_with("event: metric")) << stream;
	}

	TEST_F(DevServerTest, ServesNothingUnlessEnabled) {
		start();
		EXPECT_FALSE(server().serving());
		EXPECT_EQ(server().port(), 0);
		EXPECT_EQ(server().parkedRequests(), 0U);
		const DevServer::Registration route = server().route("idle", [](const DevRequest&) { return ok(); });
		EXPECT_TRUE(route.active());
		EXPECT_TRUE(frame());
	}

	// The exit's reply closes the connection from the server's side, which leaves the port in TIME_WAIT
	// on Linux and macOS; a relaunch on it at once must still bind.
	TEST_F(DevServerTest, ARelaunchRightAfterAnExitGetsThePortBack) {
		serve();
		const int		   port = this->port();
		std::future<Reply> exit = send("/api/control?action=exit");
		waitParked(1);
		EXPECT_FALSE(frame());
		app.reset();
		ASSERT_EQ(exit.get().body, R"({"status":"ok","action":"exit","shutdown":"complete"})");

		AppConfig samePort = served();
		samePort.devServer.port = port;
		serve(std::move(samePort));
		EXPECT_EQ(this->port(), port);
		EXPECT_EQ(get("/api/health").status, 200);
	}

	TEST_F(DevServerTest, ATimeoutPastAnHourIsHeldToAnHour) {
		serve(served(std::numeric_limits<double>::max()));
		const DevServer::Registration route = server().route("slow", [](const DevRequest&) { return ok(R"({"slow":true})"); });
		const Reply					  reply = ask("/api/dev/slow");
		EXPECT_EQ(reply.status, 200);
		EXPECT_EQ(reply.body, R"({"slow":true})");
	}

	TEST_F(DevServerTest, ABusyPortFailsCreateAndLeavesNothingBehind) {
		httplib::Server blocker;
		const int		busy = blocker.bind_to_any_port("127.0.0.1");
		ASSERT_GT(busy, 0);
		std::thread listening([&blocker] { blocker.listen_after_bind(); });
		blocker.wait_until_ready();

		AppConfig onBusy = served();
		onBusy.devServer.port = busy;
		EXPECT_EQ(App::create(std::move(onBusy), std::make_unique<HeadlessPlatform>(800, 600)), nullptr);

		blocker.stop();
		listening.join();
		serve();
	}

	TEST_F(DevServerTest, APortOutOfRangeOrATimeoutThatIsntPositiveFailsCreate) {
		for (const DevServerConfig bad : {
				 DevServerConfig{.enabled = true, .port = 65536},
				 DevServerConfig{.enabled = true, .port = -1},
				 DevServerConfig{.enabled = true, .requestTimeout = 0.0},
				 DevServerConfig{.enabled = true, .requestTimeout = std::numeric_limits<double>::quiet_NaN()},
			 }) {
			AppConfig refused = config();
			refused.devServer = bad;
			EXPECT_EQ(App::create(std::move(refused), std::make_unique<HeadlessPlatform>(800, 600)), nullptr);
		}
		AppConfig ignored = config();
		ignored.devServer = {.port = -1}; // not enabled, so not checked
		EXPECT_NE(App::create(std::move(ignored), std::make_unique<HeadlessPlatform>(800, 600)), nullptr);
	}

	TEST_F(DevServerTest, AnswersWithoutAFrameWhereNothingWaitsForTheGame) {
		serve();
		const Reply index = get("/");
		EXPECT_EQ(index.status, 200);
		EXPECT_EQ(index.contentType, "text/html");
		EXPECT_NE(index.body.find("Developer Server Running"), std::string::npos);

		const Reply metrics = get("/api/metrics");
		EXPECT_EQ(metrics.status, 200);
		EXPECT_EQ(nlohmann::json::parse(metrics.body).at("fps"), 0.0);

		EXPECT_EQ(get("/api/nothing").status, 404);
		EXPECT_EQ(server().parkedRequests(), 0U);
	}

	TEST_F(DevServerTest, MetricsArePublishedEachFrame) {
		AppConfig withMetrics = served();
		withMetrics.metrics = true;
		serve(std::move(withMetrics));
		app->scenes().push<MainScene>();
		frame();
		frame();
		EXPECT_GT(nlohmann::json::parse(get("/api/health").body).at("uptime").get<std::uint64_t>(), 0U);
		EXPECT_GT(nlohmann::json::parse(get("/api/metrics").body).at("timestamp").get<std::uint64_t>(), 0U);

		std::string		stream;
		httplib::Client client("127.0.0.1", port());
		client.Get("/stream/metrics", [&stream](const char* data, std::size_t size) {
			stream.append(data, size);
			return stream.find("\n\n") == std::string::npos;
		});
		EXPECT_TRUE(stream.starts_with("event: metric\ndata: {")) << stream;
		EXPECT_NE(stream.find(R"(,"sceneName":"Main"})"), std::string::npos) << stream;
	}

#ifdef DEVELOPMENT_BUILD
	// The logger's sink runs on whichever thread logged; the ring takes a lock for each. A smoke test:
	// an unlocked ring would usually pass it too.
	TEST_F(DevServerTest, TheLogStreamCarriesLinesFromEveryThread) {
		serve();
		std::vector<std::thread> loggers;
		for (int thread = 0; thread < 4; ++thread) {
			loggers.emplace_back([thread] {
				for (int line = 0; line < 100; ++line) {
					LOG_DEBUG(Engine, "logger %d line %d", thread, line);
				}
			});
		}
		for (std::thread& logger : loggers) {
			logger.join();
		}

		const std::vector<std::string> wanted{"logger 0 line 99", "logger 1 line 99", "logger 2 line 99", "logger 3 line 99"};
		std::string					   stream;
		httplib::Client				   client("127.0.0.1", port());
		client.set_read_timeout(std::chrono::seconds(5));
		client.Get("/stream/logs", [&](const char* data, std::size_t size) {
			stream.append(data, size);
			return !std::ranges::all_of(wanted, [&stream](const std::string& line) { return stream.find(line) != std::string::npos; });
		});
		for (const std::string& line : wanted) {
			const std::string event = R"(data: {"level":"DEBUG","category":"Engine","message":")" + line + R"(")";
			EXPECT_NE(stream.find(event), std::string::npos) << line;
		}
		EXPECT_NE(stream.find("Dev server: http://127.0.0.1:" + std::to_string(port())), std::string::npos);
	}
#endif

	// --- Where in the frame ---

	TEST_F(DevServerTest, RoutesAnswerAtStep3AndStateAfterRenderBeforePresent) {
		auto		 owned = std::make_unique<PresentingPlatform>();
		ManualClock& clock = owned->time;
		app = App::create(served(), std::move(owned));
		ASSERT_NE(app, nullptr);
		const DevServer::Registration route = server().route("order", [](const DevRequest&) {
			record("route", "order");
			return ok();
		});
		const DevServer::Registration state = server().stateProvider("order", [] {
			record("provider", "order");
			return std::string("{}");
		});
		app->scenes().push<MainScene>();
		clock.advance(kStep);
		app->tick();
		take();

		std::future<Reply> routeReply = send("/api/dev/order");
		std::future<Reply> stateReply = send("/api/state?what=order");
		waitParked(2);
		clock.advance(kStep);
		app->tick();
		EXPECT_EQ(routeReply.get().status, 200);
		EXPECT_EQ(stateReply.get().status, 200);
		EXPECT_EQ(
			take(),
			(std::vector<std::string>{
				"route.order", "Main.fixedUpdate 0", "Main.update", "Main.render 0", "provider.order", "platform.present"
			})
		);

		std::future<Reply> vsync = send("/api/control?action=vsync&value=0");
		waitParked(1);
		clock.advance(kStep);
		app->tick();
		EXPECT_EQ(vsync.get().body, R"({"status":"ok","action":"vsync"})");
		EXPECT_EQ(head(1), std::vector<std::string>{"platform.vsync off"});
	}

	// --- Routes ---

	TEST_F(DevServerTest, ARouteGetsItsVerbAndQuery) {
		serve();
		DevRequest					  seen;
		const DevServer::Registration spawn = server().route("Spawn", [&seen](const DevRequest& request) {
			seen = request;
			return ok(R"({"spawned":5})");
		});
		ASSERT_TRUE(spawn.active());

		const Reply reply = ask("/api/dev/SPAWN?n=5&at=1,2&n=6");
		EXPECT_EQ(reply.status, 200);
		EXPECT_EQ(reply.contentType, "application/json");
		EXPECT_EQ(reply.body, R"({"spawned":5})");
		EXPECT_EQ(seen.name, "spawn");
		EXPECT_EQ(seen.params, (std::map<std::string, std::string>{{"at", "1,2"}, {"n", "5"}}));
	}

	TEST_F(DevServerTest, AnUnregisteredVerbOrWhatIs404AtOnce) {
		serve();
		Reply reply = get("/api/dev/nothing");
		EXPECT_EQ(reply.status, 404);
		EXPECT_EQ(reply.body, R"({"error":"no dev route 'nothing'"})");
		reply = get("/api/state?what=nothing");
		EXPECT_EQ(reply.status, 404);
		EXPECT_EQ(reply.body, R"({"error":"no state provider for 'nothing'"})");
		reply = get("/api/state");
		EXPECT_EQ(reply.status, 404);
		EXPECT_EQ(reply.body, R"({"error":"no state provider for 'summary'"})");
		EXPECT_EQ(server().parkedRequests(), 0U);
	}

	TEST_F(DevServerTest, TheNewestRegistrationAnswersAndHandsTheNameBack) {
		serve();
		DevServer::Registration first = server().route("who", [](const DevRequest&) { return ok(R"({"who":"first"})"); });
		DevServer::Registration second = server().route("who", [](const DevRequest&) { return ok(R"({"who":"second"})"); });
		EXPECT_EQ(ask("/api/dev/who").body, R"({"who":"second"})");
		second.reset();
		EXPECT_FALSE(second.active());
		EXPECT_EQ(ask("/api/dev/who").body, R"({"who":"first"})");

		DevServer::Registration moved = std::move(first);
		EXPECT_FALSE(first.active()); // NOLINT(bugprone-use-after-move)
		EXPECT_TRUE(moved.active());
		EXPECT_EQ(ask("/api/dev/who").body, R"({"who":"first"})");
		{ const DevServer::Registration dropped = std::move(moved); }
		EXPECT_EQ(get("/api/dev/who").status, 404);
	}

	TEST_F(DevServerTest, ARegistrationMadeOrDroppedInADrainAppliesAtOnce) {
		serve();
		DevServer::Registration		  late;
		DevServer::Registration		  gone = server().route("gone", [](const DevRequest&) { return ok(); });
		const DevServer::Registration arm = server().control("arm", [&](const DevRequest&) {
			late = server().route("late", [](const DevRequest&) { return ok(R"({"late":true})"); });
			return ok();
		});
		const DevServer::Registration drop = server().control("drop", [&gone](const DevRequest&) {
			gone.reset();
			return ok();
		});

		EXPECT_EQ(get("/api/dev/late").status, 404);
		EXPECT_EQ(ask("/api/control?action=arm").status, 200);
		EXPECT_EQ(ask("/api/dev/late").body, R"({"late":true})");

		// Parked for the same frame, the route asked first: control actions run before routes, so the
		// route the action dropped answers 404.
		std::future<Reply> route = send("/api/dev/gone");
		waitParked(1);
		std::future<Reply> control = send("/api/control?action=drop");
		waitParked(2);
		frame();
		EXPECT_EQ(control.get().status, 200);
		const Reply dropped = route.get();
		EXPECT_EQ(dropped.status, 404);
		EXPECT_EQ(dropped.body, R"({"error":"no dev route 'gone'"})");
	}

	// A scene whose handler is the last owner of something that holds a registration of its own: the
	// handler dies when its handle resets, and that registration's reset reenters the registry.
	TEST_F(DevServerTest, AHandlerWhoseCapturesHoldARegistrationCanBeRemoved) {
		serve();
		struct Tools {
			DevServer::Registration sibling;
		};
		auto tools = std::make_shared<Tools>();
		tools->sibling = server().route("sibling", [](const DevRequest&) { return ok(); });
		DevServer::Registration mine = server().route("mine", [tools](const DevRequest&) { return ok(); });
		tools.reset();

		std::promise<void> removed;
		std::future<void>  finished = removed.get_future();
		std::thread		   remover([&] {
			   mine.reset();
			   removed.set_value();
		   });
		if (finished.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
			std::fprintf(stderr, "Registration::reset deadlocked removing a handler that owns a registration\n");
			std::_Exit(1);
		}
		remover.join();
		EXPECT_EQ(get("/api/dev/mine").status, 404);
		EXPECT_EQ(get("/api/dev/sibling").status, 404);
	}

	TEST_F(DevServerTest, AHandleThatOutlivesTheAppDoesNothing) {
		serve();
		DevServer::Registration kept = server().route("kept", [](const DevRequest&) { return ok(); });
		EXPECT_TRUE(kept.active());
		app.reset();
		EXPECT_FALSE(kept.active());
		kept.reset();
	}

	TEST_F(DevServerTest, RefusedRegistrationsAreInactive) {
		serve();
		const auto handler = [](const DevRequest&) {
			return ok();
		};
		const auto provider = [] {
			return std::string("{}");
		};
		EXPECT_FALSE(server().route("bad verb", handler).active());
		EXPECT_FALSE(server().route("ui.tree", handler).active()); // no dots in a verb
		EXPECT_FALSE(server().route("empty", nullptr).active());
		EXPECT_FALSE(server().control("pause", handler).active());
		EXPECT_FALSE(server().control("step", handler).active());
		EXPECT_FALSE(server().control("", handler).active());
		EXPECT_FALSE(server().stateProvider("scene", provider).active());
		EXPECT_FALSE(server().stateProvider("sim", nullptr).active());
		EXPECT_TRUE(server().stateProvider("sim.v2", provider).active());
	}

	TEST_F(DevServerTest, AHandlerThatThrowsAnswers500AndTheFrameGoesOn) {
		serve();
		app->scenes().push<MainScene>();
		frame();
		const DevServer::Registration route =
			server().route("boom", [](const DevRequest&) -> DevResponse { throw std::runtime_error("boom"); });
		const DevServer::Registration state = server().stateProvider("bad", []() -> std::string { throw std::runtime_error("bad"); });
		const DevServer::Registration control = server().control("oops", [](const DevRequest&) -> DevResponse { throw 42; });

		Reply reply = ask("/api/dev/boom");
		EXPECT_EQ(reply.status, 500);
		EXPECT_EQ(reply.body, R"({"error":"dev route 'boom' threw: boom"})");
		reply = ask("/api/state?what=bad");
		EXPECT_EQ(reply.status, 500);
		EXPECT_EQ(reply.body, R"({"error":"state provider 'bad' threw: bad"})");
		reply = ask("/api/control?action=oops");
		EXPECT_EQ(reply.status, 500);
		EXPECT_EQ(reply.body, R"({"error":"control action 'oops' threw: an unknown exception"})");

		const int renders = count("Main.render");
		EXPECT_TRUE(frame());
		EXPECT_EQ(count("Main.render"), renders + 1);
	}

	// --- Control actions ---

	TEST_F(DevServerTest, PauseStepAndResume) {
		serve();
		app->scenes().push<MainScene>();
		frame();
		EXPECT_EQ(ask("/api/control?action=pause").body, R"({"status":"ok","action":"pause"})");
		EXPECT_TRUE(app->paused());
		const int ticks = count("Main.fixedUpdate");
		frame();
		frame();
		EXPECT_EQ(count("Main.fixedUpdate"), ticks);

		EXPECT_EQ(ask("/api/control?action=step&ticks=3").body, R"({"status":"ok","action":"step","ticks":3})");
		EXPECT_EQ(count("Main.fixedUpdate"), ticks + 3); // in the frame that answered it
		frame();
		EXPECT_EQ(count("Main.fixedUpdate"), ticks + 3);
		EXPECT_EQ(ask("/api/control?action=step").body, R"({"status":"ok","action":"step","ticks":1})");
		EXPECT_EQ(count("Main.fixedUpdate"), ticks + 4);

		for (const char* bad : {"0", "-2", "two", "1.5", ""}) {
			const Reply reply = get(std::string("/api/control?action=step&ticks=") + bad);
			EXPECT_EQ(reply.status, 400) << bad;
			EXPECT_EQ(reply.body, R"({"error":"Step action takes 'ticks', a whole number from 1"})") << bad;
		}

		EXPECT_EQ(ask("/api/control?action=resume").body, R"({"status":"ok","action":"resume"})");
		EXPECT_EQ(count("Main.fixedUpdate"), ticks + 4); // the frame that resumed stays paused
		frame();
		EXPECT_EQ(count("Main.fixedUpdate"), ticks + 5);
		EXPECT_FALSE(app->paused());

		const Reply notPaused = ask("/api/control?action=step&ticks=2");
		EXPECT_EQ(notPaused.status, 409);
		EXPECT_EQ(notPaused.body, R"({"error":"step needs the app paused"})");
	}

	TEST_F(DevServerTest, SceneAndReload) {
		serve();
		app->scenes().add<MainScene>("main");
		app->scenes().add<SecondScene>("second");
		app->scenes().push<MainScene>();
		frame();

		EXPECT_EQ(ask("/api/control?action=scene&scene=second").body, R"({"status":"ok","action":"scene","scene":"second"})");
		EXPECT_EQ(app->scenes().topName(), "second"); // applied at step 6 of the frame that answered it

		Reply reply = ask("/api/control?action=scene&scene=nope");
		EXPECT_EQ(reply.status, 404);
		EXPECT_EQ(reply.body, R"({"error":"no scene added as 'nope'"})");
		reply = get("/api/control?action=scene");
		EXPECT_EQ(reply.status, 400);
		EXPECT_EQ(reply.body, R"({"error":"Scene change requires 'scene' parameter"})");

		EXPECT_EQ(ask("/api/control?action=reload").body, R"({"status":"ok","action":"reload"})");
		EXPECT_EQ(count("Second.built"), 2);

		app->scenes().push<StateScene>({.label = "Unnamed"});
		frame();
		reply = ask("/api/control?action=reload");
		EXPECT_EQ(reply.status, 409);
		EXPECT_EQ(reply.body, R"({"error":"the top scene can't be reloaded: there is none, or it was pushed with arguments"})");
	}

	TEST_F(DevServerTest, MalformedOrUnknownControlActions) {
		serve();
		Reply reply = get("/api/control");
		EXPECT_EQ(reply.status, 400);
		EXPECT_EQ(reply.body, R"({"error":"Missing required parameter 'action'"})");
		reply = get("/api/control?action=fly");
		EXPECT_EQ(reply.status, 404);
		EXPECT_EQ(reply.body, R"({"error":"no control action 'fly'"})");
		reply = get("/api/control?action=vsync&value=2");
		EXPECT_EQ(reply.status, 400);
		EXPECT_EQ(reply.body, R"json({"error":"Vsync action requires 'value' parameter (0 or 1)"})json");
		EXPECT_EQ(server().parkedRequests(), 0U);
	}

	TEST_F(DevServerTest, AGameControlActionGetsItsParamsAndShapesItsReply) {
		serve();
		DevRequest					  seen;
		const DevServer::Registration camera = server().control("camera", [&seen](const DevRequest& request) {
			seen = request;
			return DevResponse{.status = 202, .contentType = "text/plain", .body = "moved"};
		});
		const Reply					  reply = ask("/api/control?action=camera&x=1.5&y=2&x=9");
		EXPECT_EQ(reply.status, 202);
		EXPECT_EQ(reply.contentType, "text/plain");
		EXPECT_EQ(reply.body, "moved");
		EXPECT_EQ(seen.name, "camera");
		EXPECT_EQ(seen.params, (std::map<std::string, std::string>{{"x", "1.5"}, {"y", "2"}}));
	}

	TEST_F(DevServerTest, ExitEndsTheFrameAtStep3AndAnswersOnceTheAppIsGone) {
		serve();
		app->scenes().push<MainScene>();
		frame();
		std::future<Reply> exit = send("/api/control?action=exit");
		waitParked(1);
		take();
		EXPECT_FALSE(frame());
		EXPECT_EQ(take(), std::vector<std::string>{}); // nothing after step 3
		app->shutdown();
		EXPECT_EQ(take(), (std::vector<std::string>{"Main.onExit", "Main.destroyed"}));
		EXPECT_EQ(exit.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);
		app.reset();
		const Reply reply = exit.get();
		EXPECT_EQ(reply.status, 200);
		EXPECT_EQ(reply.body, R"({"status":"ok","action":"exit","shutdown":"complete"})");
		EXPECT_EQ(reply.connection, "close");
	}

	TEST_F(DevServerTest, AfterAnExitTheRestOfStep3IsAnswered503WithoutRunning) {
		serve();
		app->scenes().push<MainScene>();
		frame();
		int		   runs = 0;
		const auto counted = [&runs](const DevRequest&) {
			++runs;
			return ok();
		};
		const DevServer::Registration route = server().route("count", counted);
		const DevServer::Registration control = server().control("count", counted);
		std::future<Reply>			  exit = send("/api/control?action=exit");
		waitParked(1);
		std::future<Reply> laterControl = send("/api/control?action=count");
		waitParked(2);
		std::future<Reply> laterRoute = send("/api/dev/count");
		waitParked(3);
		std::future<Reply> secondExit = send("/api/control?action=exit");
		waitParked(4);
		take();
		EXPECT_FALSE(frame());
		EXPECT_EQ(take(), std::vector<std::string>{});
		for (std::future<Reply>* later : {&laterControl, &laterRoute}) {
			const Reply reply = later->get();
			EXPECT_EQ(reply.status, 503);
			EXPECT_EQ(reply.body, R"({"error":"the app is exiting"})");
		}
		EXPECT_EQ(runs, 0);
		app.reset();
		EXPECT_EQ(exit.get().body, R"({"status":"ok","action":"exit","shutdown":"complete"})");
		EXPECT_EQ(secondExit.get().body, R"({"status":"ok","action":"exit","shutdown":"complete"})");
	}

	TEST_F(DevServerTest, AGameActionThatExitsEndsTheFrameAtStep3) {
		serve();
		app->scenes().push<MainScene>();
		frame();
		const DevServer::Registration quit = server().control("quit", [this](const DevRequest&) {
			app->requestExit();
			return ok();
		});
		std::future<Reply>			  reply = send("/api/control?action=quit");
		waitParked(1);
		take();
		EXPECT_FALSE(frame());
		EXPECT_EQ(reply.get().status, 200);
		EXPECT_EQ(take(), std::vector<std::string>{});
	}

	// --- Waiting for the frame ---

	TEST_F(DevServerTest, ARequestTheFrameDoesntReachIsDroppedNotRunLater) {
		serve(served(0.05));
		int		   runs = 0;
		const auto counted = [&runs](const DevRequest&) {
			++runs;
			return ok();
		};
		const DevServer::Registration route = server().route("late", counted);
		const DevServer::Registration control = server().control("later", counted);
		const DevServer::Registration state = server().stateProvider("latest", [&runs] {
			++runs;
			return std::string("{}");
		});
		for (const char* path :
			 {"/api/dev/late", "/api/control?action=later", "/api/state?what=latest", "/api/control?action=pause", "/api/ui/screenshot"}) {
			const Reply reply = get(path);
			EXPECT_EQ(reply.status, 503) << path;
			EXPECT_EQ(reply.body, kTimedOut) << path;
		}
		EXPECT_EQ(server().parkedRequests(), 0U);
		frame();
		EXPECT_EQ(runs, 0);
		EXPECT_FALSE(app->paused());
	}

	TEST_F(DevServerTest, ParkedRequestsAreLetGoWhenTheAppGoes) {
		serve();
		const DevServer::Registration route = server().route("parked", [](const DevRequest&) { return ok(); });
		std::future<Reply>			  dev = send("/api/dev/parked");
		std::future<Reply>			  exit = send("/api/control?action=exit");
		waitParked(2);
		app.reset();
		Reply reply = dev.get();
		EXPECT_EQ(reply.status, 503);
		EXPECT_EQ(reply.body, R"({"error":"the app is shutting down"})");
		reply = exit.get();
		EXPECT_EQ(reply.status, 200);
		EXPECT_EQ(reply.body, R"({"status":"ok","action":"exit","shutdown":"complete"})");
	}

	// --- State ---

	TEST_F(DevServerTest, TreeAndLintAnswerEmptyWithNoProvider) {
		serve();
		Reply reply = ask("/api/ui/tree");
		EXPECT_EQ(reply.status, 200);
		EXPECT_EQ(reply.body, R"({"viewport":{"width":800,"height":600},"roots":[]})");
		reply = ask("/api/ui/lint");
		EXPECT_EQ(reply.status, 200);
		EXPECT_EQ(reply.body, R"({"count":0,"violations":[]})");
		EXPECT_EQ(ask("/api/state?what=ui.lint").body, R"({"count":0,"violations":[]})");
	}

	TEST_F(DevServerTest, ProvidersAnswerAfterRenderAndTheDefaultsComeBack) {
		serve();
		app->scenes().push<MainScene>();
		frame();
		take();
		DevServer::Registration tree = server().stateProvider("ui.tree", [] {
			record("provider", "ui.tree");
			return std::string(R"({"roots":[1]})");
		});
		DevServer::Registration sim = server().stateProvider("sim", [] { return std::string(R"({"tick":7})"); });

		EXPECT_EQ(ask("/api/ui/tree").body, R"({"roots":[1]})");
		EXPECT_EQ(take(), (std::vector<std::string>{"Main.fixedUpdate 0", "Main.update", "Main.render 0", "provider.ui.tree"}));
		EXPECT_EQ(ask("/api/state?what=ui.tree").body, R"({"roots":[1]})");
		EXPECT_EQ(ask("/api/state?what=sim").body, R"({"tick":7})");

		tree.reset();
		EXPECT_EQ(ask("/api/ui/tree").body, R"({"viewport":{"width":800,"height":600},"roots":[]})");
		sim.reset();
		EXPECT_EQ(get("/api/state?what=sim").status, 404);
	}

	TEST_F(DevServerTest, ASceneWithNoNameIsServedWithAnEmptyOne) {
		serve();
		app->scenes().push<NamelessScene>();
		frame();
		EXPECT_EQ(ask("/api/state?what=scene").body, R"({"scenes":[{"name":"","state":null}]})");
	}

	TEST_F(DevServerTest, SceneStateIsEveryStackedSceneTopFirst) {
		serve();
		EXPECT_EQ(ask("/api/state?what=scene").body, R"({"scenes":[]})");

		app->scenes().push<StateScene>({.label = "Arena", .state = R"({"wave":3, "ships":[1,2]})"});
		app->scenes().push<StateScene>({.label = "Pause"});
		app->scenes().push<StateScene>({.label = "Broken", .state = "throw"});
		app->scenes().push<StateScene>({.label = "Text", .state = "not json"});
		frame();
		EXPECT_EQ(
			ask("/api/state?what=scene").body,
			R"({"scenes":[)"
			R"({"name":"Text","state":null,"error":"exportState gave text that isn't JSON"},)"
			R"({"name":"Broken","state":null,"error":"exportState threw: nope"},)"
			R"({"name":"Pause","state":null},)"
			R"({"name":"Arena","state":{"wave":3, "ships":[1,2]}}]})"
		);
	}

	TEST_F(DevServerTest, TheScreenshotIsAPngOfTheBackbufferReadWithoutGl) {
		serve();
		frame();
		const Reply shot = ask("/api/ui/screenshot");
		EXPECT_EQ(shot.status, 200);
		EXPECT_EQ(shot.contentType, "image/png");
		EXPECT_EQ(shot.disposition, R"(inline; filename="screenshot.png")");
		ASSERT_GE(shot.body.size(), 24U);
		EXPECT_EQ(shot.body.substr(0, 8), std::string("\x89PNG\r\n\x1a\n", 8));
		const auto bigEndian = [&shot](std::size_t at) {
			std::uint32_t value = 0;
			for (std::size_t i = 0; i < 4; ++i) {
				value = (value << 8) | static_cast<unsigned char>(shot.body[at + i]);
			}
			return value;
		};
		EXPECT_EQ(bigEndian(16), 800U);
		EXPECT_EQ(bigEndian(20), 600U);
	}

	TEST_F(DevServerTest, OneReadbackAnswersEveryScreenshotParkedForAFrame) {
		auto				owned = std::make_unique<PresentingPlatform>();
		PresentingPlatform& headless = *owned;
		app = App::create(served(), std::move(owned));
		ASSERT_NE(app, nullptr);
		headless.time.advance(kStep);
		app->tick();
		const int before = headless.backend->pixelReads();

		std::vector<std::future<Reply>> shots;
		for (std::size_t i = 0; i < 3; ++i) {
			shots.push_back(send("/api/ui/screenshot"));
			waitParked(i + 1);
		}
		headless.time.advance(kStep);
		app->tick();
		const Reply first = shots[0].get();
		EXPECT_EQ(first.status, 200);
		EXPECT_EQ(shots[1].get().body, first.body);
		EXPECT_EQ(shots[2].get().body, first.body);
		EXPECT_EQ(headless.backend->pixelReads(), before + 1);
	}

	TEST_F(DevServerTest, AScreenshotWithNoBackbufferIs500) {
		serve();
		frame();
		// What a minimized window leaves the renderer, which the App itself never passes on.
		app->coordinateSystem().setViewport({.logicalWidth = 800, .logicalHeight = 600});
		Renderer::Primitives::setViewport(0, 0);
		const Reply shot = ask("/api/ui/screenshot");
		EXPECT_EQ(shot.status, 500);
		EXPECT_EQ(shot.body, R"json({"error":"no backbuffer to read (0x0)"})json");
	}

	// --- Input ---

	TEST_F(DevServerTest, InjectedEventsReachDispatchAndTheTickSnapshot) {
		serve();
		app->scenes().push<MainScene>();
		frame();
		const Reply queued =
			get("/api/input?ev=keydown,A&ev=char,0x41&ev=connect,vpad1&ev=pad,vpad1,x,down&ev=axis,vpad1,lefty,-0.5&ev=click,12,34");
		EXPECT_EQ(queued.status, 200);
		EXPECT_EQ(queued.body, R"({"status":"ok","queued":6})");
		frame();

		MainScene&							scene = top<MainScene>();
		const std::vector<InputEvent::Type> expected{
			InputEvent::Type::KeyDown,
			InputEvent::Type::Char,
			InputEvent::Type::DeviceConnected,
			InputEvent::Type::ButtonDown,
			InputEvent::Type::Axis,
			InputEvent::Type::PointerMove,
			InputEvent::Type::PointerDown,
			InputEvent::Type::PointerUp,
		};
		ASSERT_EQ(scene.events.size(), expected.size());
		for (std::size_t i = 0; i < expected.size(); ++i) {
			EXPECT_EQ(scene.events[i].type, expected[i]) << i;
			EXPECT_EQ(scene.events[i].time, platform->clock().now()) << i;
		}
		EXPECT_EQ(scene.events[0].key, Key::A);
		EXPECT_EQ(scene.events[1].codepoint, U'A');
		EXPECT_EQ(scene.events[3].device, (foundation::DeviceId{.kind = DeviceKind::Gamepad, .index = 17}));

		const foundation::InputSnapshot& tick = scene.ticks.back();
		EXPECT_TRUE(keyIn(tick, Key::A).pressed());
		const foundation::GamepadState& pad = tick.pads.at(17);
		EXPECT_TRUE(pad.connected);
		EXPECT_TRUE(pad.buttons.at(static_cast<std::size_t>(GamepadButton::X)).pressed());
		EXPECT_EQ(pad.axes.at(static_cast<std::size_t>(GamepadAxis::LeftY)), -0.5F);
		const foundation::ButtonState& left = tick.mouse.at(static_cast<std::size_t>(MouseButton::Left));
		EXPECT_TRUE(left.pressed());
		EXPECT_TRUE(left.released());
		EXPECT_EQ(tick.pointer, glm::vec2(12.0F, 34.0F));

		EXPECT_EQ(get("/api/input?ev=disconnect,vpad1").status, 200);
		frame();
		EXPECT_FALSE(top<MainScene>().ticks.back().pads.at(17).connected);
		EXPECT_EQ(top<MainScene>().events.back().type, InputEvent::Type::DeviceDisconnected);
	}

	// A virtual pad connects through PlatformEvents::deviceConnected, as a real one does, so Input
	// lists it and a slot can claim it; its model GUID is its own, so it never takes the slot a real
	// pad's model is waiting on.
	TEST_F(DevServerTest, AVirtualPadIsADeviceASlotClaims) {
		serve();
		app->scenes().push<MainScene>();
		platform->connectGamepad({.id = padId(0), .name = "Real Pad", .modelGuid = "030000005e040000ea02000000007801"});
		frame();
		app->input().claim(0, padId(0));
		platform->disconnectGamepad(0);
		frame();
		ASSERT_FALSE(app->input().slotConnected(0)); // waiting for the real pad's model

		EXPECT_EQ(get("/api/input?ev=connect,vpad1").status, 200);
		frame();
		const auto devices = app->input().devices();
		const auto vpad = std::ranges::find(devices, padId(17), &DeviceInfo::id);
		ASSERT_NE(vpad, devices.end());
		EXPECT_EQ(vpad->name, "vpad1");
		EXPECT_EQ(vpad->modelGuid, "salal-dev-server-vpad1");
		EXPECT_FALSE(app->input().slotConnected(0));

		app->input().claim(1, padId(17));
		EXPECT_TRUE(app->input().slotConnected(1));
		EXPECT_EQ(app->input().deviceForSlot(1), padId(17));
		EXPECT_EQ(get("/api/input?ev=pad,vpad1,a,down").status, 200);
		frame();
		const foundation::GamepadState& pad = top<MainScene>().ticks.back().pads.at(17);
		EXPECT_TRUE(pad.connected);
		EXPECT_TRUE(pad.buttons.at(static_cast<std::size_t>(GamepadButton::A)).pressed());
	}

	TEST_F(DevServerTest, DisconnectingAVirtualPadReleasesWhatItHeld) {
		serve();
		app->scenes().push<MainScene>();
		frame();
		EXPECT_EQ(get("/api/input?ev=connect,vpad0&ev=pad,vpad0,b,down&ev=axis,vpad0,leftx,0.5").status, 200);
		frame();
		app->input().claim(0, padId(16));
		ASSERT_TRUE(top<MainScene>().ticks.back().pads.at(16).buttons.at(static_cast<std::size_t>(GamepadButton::B)).down);

		EXPECT_EQ(get("/api/input?ev=disconnect,vpad0").status, 200);
		frame();
		const foundation::GamepadState& pad = top<MainScene>().ticks.back().pads.at(16);
		const foundation::ButtonState&	b = pad.buttons.at(static_cast<std::size_t>(GamepadButton::B));
		EXPECT_FALSE(pad.connected);
		EXPECT_FALSE(b.down);
		EXPECT_TRUE(b.released());
		EXPECT_EQ(pad.axes.at(static_cast<std::size_t>(GamepadAxis::LeftX)), 0.0F);
		EXPECT_EQ(std::ranges::find(app->input().devices(), padId(16), &DeviceInfo::id), app->input().devices().end());
		EXPECT_FALSE(app->input().slotConnected(0));
	}

	TEST_F(DevServerTest, InjectedInputWaitsInTheServerWhilePaused) {
		serve();
		app->scenes().push<MainScene>();
		frame();
		ask("/api/control?action=pause");
		EXPECT_EQ(get("/api/input?ev=keydown,A").body, R"({"status":"ok","queued":1})");
		frame();
		frame();
		EXPECT_TRUE(top<MainScene>().events.empty());

		ask("/api/control?action=resume"); // that frame stays paused, so the key waits once more
		EXPECT_TRUE(top<MainScene>().events.empty());
		const std::size_t ticks = top<MainScene>().ticks.size();
		frame();
		const MainScene& scene = top<MainScene>();
		ASSERT_EQ(scene.events.size(), 1U);
		EXPECT_EQ(scene.events[0].type, InputEvent::Type::KeyDown);
		EXPECT_EQ(scene.events[0].key, Key::A);
		ASSERT_EQ(scene.ticks.size(), ticks + 1);
		EXPECT_TRUE(keyIn(scene.ticks.back(), Key::A).pressed());
	}

	TEST_F(DevServerTest, BadInputIsA400AndQueuesNothing) {
		serve();
		app->scenes().push<MainScene>();
		frame();
		Reply reply = get("/api/input?ev=keydown,A&ev=bogus");
		EXPECT_EQ(reply.status, 400);
		EXPECT_EQ(
			reply.body,
			R"json({"error":"unknown event type (expected move|down|up|click|scroll|keydown|keyup|char|pad|axis|connect|disconnect)","ev":"bogus"})json"
		);
		reply = get("/api/input?ev=pad,vpad0,a,down");
		EXPECT_EQ(reply.status, 400);
		EXPECT_EQ(reply.body, R"json({"error":"vpad0 isn't connected (connect,vpad0 first)","ev":"pad,vpad0,a,down"})json");
		reply = get("/api/input?ev=connect,vpad4");
		EXPECT_EQ(reply.status, 400);
		EXPECT_EQ(reply.body, R"json({"error":"unknown device 'vpad4' (expected vpad0 to vpad3)","ev":"connect,vpad4"})json");
		reply = get("/api/input");
		EXPECT_EQ(reply.status, 400);
		EXPECT_EQ(reply.body, R"({"error":"Missing required parameter 'ev'"})");
		frame();
		EXPECT_TRUE(top<MainScene>().events.empty());
	}

	TEST_F(DevServerTest, InputWaitingForAPausedAppIsCapped) {
		// Each event distinct: cpp-httplib keeps one of a repeated key=value pair.
		const auto chars = [](int first, int count) {
			std::string events;
			for (int i = 0; i < count; ++i) {
				events += "&ev=char," + std::to_string(first + i);
			}
			return events;
		};
		serve();
		app->scenes().push<MainScene>();
		frame();
		ask("/api/control?action=pause");
		EXPECT_EQ(get("/api/input?ev=connect,vpad2" + chars(100, 499)).body, R"({"status":"ok","queued":500})");
		EXPECT_EQ(get("/api/input?ev=char,65" + chars(1000, 499)).body, R"({"status":"ok","queued":500})");
		const Reply full = get("/api/input?ev=connect,vpad3" + chars(2000, 25));
		EXPECT_EQ(full.status, 429);
		EXPECT_EQ(
			full.body,
			R"({"error":"1000 injected events already wait for the app, which takes them only while it runs unpaused; at most 1024 wait"})"
		);
		EXPECT_EQ(get("/api/input?ev=connect,vpad3").status, 200); // the refused connect left vpad3 unconnected

		app->resume();
		frame();
		const MainScene& scene = top<MainScene>();
		ASSERT_EQ(scene.events.size(), 1001U);
		EXPECT_EQ(scene.events.front().type, InputEvent::Type::DeviceConnected);
		EXPECT_EQ(scene.events.back().type, InputEvent::Type::DeviceConnected);
	}

#else

	static_assert(!kDevServerCompiledIn);

	// Compiled out: the API is there, every call does nothing, and an enabled config is ignored.
	TEST_F(AppTest, WithoutTheServerTheApiIsThereAndDoesNothing) {
		AppConfig enabled = config();
		enabled.devServer = {.enabled = true, .port = 8099};
		App&	   app = start(std::move(enabled));
		DevServer& server = app.devServer();
		EXPECT_FALSE(server.serving());
		EXPECT_EQ(server.port(), 0);
		EXPECT_EQ(server.parkedRequests(), 0U);
		DevServer::Registration route = server.route("spawn", [](const DevRequest&) { return ok(); });
		DevServer::Registration control = server.control("camera", [](const DevRequest&) { return ok(); });
		DevServer::Registration state = server.stateProvider("sim", [] { return std::string("{}"); });
		EXPECT_FALSE(route.active());
		EXPECT_FALSE(control.active());
		EXPECT_FALSE(state.active());
		DevServer::Registration moved = std::move(route);
		moved.reset();
		app.scenes().push<MainScene>();
		EXPECT_TRUE(frame());
		EXPECT_EQ(count("Main.render"), 1);
	}

#endif

} // namespace app::testing
