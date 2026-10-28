#pragma once

// The engine's own flags, apart from App::create so a test passes no argv:
//   --scene=<name>       the scene to start in, by the name it was added under
//   --http-port <port>   the dev server's port, which also turns the server on
//   --help
// Anything else is left for the game.

#include <optional>
#include <string>

namespace app {

	struct CommandLine {
		std::optional<std::string> scene;
		std::optional<int>		   httpPort;
		bool					   help = false;
		bool					   valid = true; // false, logged, for a --http-port with no port or one that isn't 1 to 65535
	};

	CommandLine parseCommandLine(int argc, char** argv);

} // namespace app
