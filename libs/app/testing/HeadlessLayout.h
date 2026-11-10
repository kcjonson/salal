#pragma once

// For a game's tests on a HeadlessPlatform: the top scene's ui laid out at a window size, through the
// App's ui scale and the scene's frame as a player's window would be, then linted, and which of its
// scrollers show a bar. No pixels and no test framework: each answer is text a test compares to "".

#include "application/App.h"
#include "platform/HeadlessPlatform.h"

#include <string>
#include <vector>

namespace app {

	// The window resized to width x height framebuffer pixels (the platform's pixel ratio turns them into
	// its logical ones), two frames run so a scene that rebuilds in onResize has settled, and the top
	// scene's ui linted against the window: a line per violation, "<rule> <path> at <x>,<y> <w>x<h>" and
	// " against <other>" where the rule has one, in ui px. Empty when the layout is clean, and for a
	// scene with no ui. A window that can't be that size (the platform's minimum size) is one line saying
	// what it is instead. The window keeps the size.
	[[nodiscard]] std::string lintAt(App& app, HeadlessPlatform& platform, int width, int height);

	// The top scene's scrollers that show a scrollbar now, as of its last layout: each one's path, as the
	// lint writes paths, a component's id or its type and index. A test checks that only its lists are here.
	[[nodiscard]] std::vector<std::string> scrollbarsShown(App& app);

} // namespace app
