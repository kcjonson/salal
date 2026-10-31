#pragma once

// The engine's own flags, apart from App::create so a test passes no argv:
//   --scene=<name>       the scene to start in, by the name it was added under
//   --http-port <port>   the dev server's port, which also turns the server on; --http-port=<port> too
//   --no-vsync           present without waiting for the display, for perf runs; the game hands it to
//                        WindowConfig::vsync, which a settings file's video.vsync, read at create, overrides
//   --help
// Anything else is left for the game.

#include <optional>
#include <string>

namespace app {

	struct CommandLine {
		std::optional<std::string> scene;
		std::optional<int>		   httpPort;
		bool					   vsync = true;
		bool					   help = false;
		bool valid = true; // false, logged, for a --http-port with no port (or a flag where its port goes) or one that isn't 1 to 65535
	};

	CommandLine parseCommandLine(int argc, char** argv);

} // namespace app
