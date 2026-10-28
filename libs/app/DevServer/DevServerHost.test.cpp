// The /api/dev/<verb> path's verb, the error reply's shape, and the host on its own, over loopback.

#ifdef SALAL_DEV_SERVER

#include "DevServer/DevServerHost.h"

#include "DevServer/DevRegistry.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace app::detail {

	TEST(DevVerbTest, VerbFromSimplePath) {
		EXPECT_EQ(parseDevVerb("/api/dev/freebuild"), "freebuild");
		EXPECT_EQ(parseDevVerb("/api/dev/give"), "give");
		EXPECT_EQ(parseDevVerb("/api/dev/spawn"), "spawn");
		EXPECT_EQ(parseDevVerb("/api/dev/colonist"), "colonist");
		EXPECT_EQ(parseDevVerb("/api/dev/teleport"), "teleport");
		EXPECT_EQ(parseDevVerb("/api/dev/foundation"), "foundation");
	}

	TEST(DevVerbTest, VerbIsLowercased) {
		EXPECT_EQ(parseDevVerb("/api/dev/FreeBuild"), "freebuild");
		EXPECT_EQ(parseDevVerb("/api/dev/SPAWN"), "spawn");
	}

	TEST(DevVerbTest, VerbTrimsExtraPathSegments) {
		EXPECT_EQ(parseDevVerb("/api/dev/foundation/extra"), "foundation");
		EXPECT_EQ(parseDevVerb("/api/dev/freebuild/"), "freebuild");
	}

	TEST(DevVerbTest, RejectsNonDevPaths) {
		EXPECT_TRUE(parseDevVerb("/api/control").empty());
		EXPECT_TRUE(parseDevVerb("/api/input").empty());
		EXPECT_TRUE(parseDevVerb("/api/dev").empty());	// no trailing verb
		EXPECT_TRUE(parseDevVerb("/api/dev/").empty()); // empty verb
		EXPECT_TRUE(parseDevVerb("").empty());
	}

	TEST(DevVerbTest, ErrorReplyIsJsonWithTheStatus) {
		const DevResponse reply = errorReply(404, "no dev route \"x\"");
		EXPECT_EQ(reply.status, 404);
		EXPECT_EQ(reply.contentType, "application/json");
		// Out of the macro: MSVC stringizes a raw string with a backslash in it wrongly.
		const std::string escaped = R"({"error":"no dev route \"x\""})";
		EXPECT_EQ(reply.body, escaped);
	}

	// A request the game thread took and never answered, as when something throws out of a drain, is
	// let go when the server stops instead of hanging the pool's join.
	TEST(DevServerHostTest, ATakenRequestNeverAnsweredIsLetGoAtStop) {
		auto registry = std::make_shared<DevRegistry>();
		registry->addHandler(DevKind::Route, "stuck", [](const DevRequest&) { return DevResponse{}; });
		std::unique_ptr<DevServerHost> host = DevServerHost::start({.enabled = true, .requestTimeout = 30.0}, registry);
		ASSERT_NE(host, nullptr);
		std::future<std::pair<int, std::string>> reply = std::async(std::launch::async, [port = host->port()] {
			httplib::Client		  client("127.0.0.1", port);
			const httplib::Result result = client.Get("/api/dev/stuck");
			return result ? std::pair{result->status, result->body} : std::pair{-1, std::string()};
		});
		for (int i = 0; i < 5000 && host->parked() < 1; ++i) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		ASSERT_EQ(host->parked(), 1U);
		ASSERT_EQ(host->takePreFrame().size(), 1U);

		std::promise<void> stopped;
		std::future<void>  finished = stopped.get_future();
		std::thread		   stopper([&] {
			   host.reset();
			   stopped.set_value();
		   });
		if (finished.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
			std::fprintf(stderr, "~DevServerHost hung on a request taken and never answered\n");
			std::_Exit(1);
		}
		stopper.join();
		const auto [status, body] = reply.get();
		EXPECT_EQ(status, 500);
		EXPECT_EQ(body, R"({"error":"the app stopped before answering"})");
	}

} // namespace app::detail

#endif
