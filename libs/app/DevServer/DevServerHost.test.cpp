// The /api/dev/<verb> path's verb, and the error reply's shape. No sockets.

#ifdef SALAL_DEV_SERVER

#include "DevServer/DevServerHost.h"

#include <gtest/gtest.h>

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

} // namespace app::detail

#endif
