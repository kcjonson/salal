#include "application/CommandLine.h"

#include <utils/Log.h>

#include <charconv>
#include <cstddef>
#include <span>
#include <string_view>

namespace app {

	namespace {
		constexpr std::string_view kScene = "--scene=";
		constexpr std::string_view kHttpPort = "--http-port";
		constexpr std::string_view kHttpPortEquals = "--http-port=";
		constexpr std::string_view kHelp = "--help";

		std::optional<int> parsePort(std::string_view text) {
			int port = 0;
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
			if (error != std::errc{} || end != text.data() + text.size() || port < 1 || port > 65535) {
				return std::nullopt;
			}
			return port;
		}
	} // namespace

	CommandLine parseCommandLine(int argc, char** argv) {
		CommandLine			   commandLine;
		const std::span<char*> args(argv, argc > 0 ? static_cast<std::size_t>(argc) : 0);
		for (std::size_t i = 1; i < args.size(); ++i) {
			const std::string_view arg = args[i];
			if (arg.starts_with(kScene)) {
				commandLine.scene = std::string(arg.substr(kScene.size()));
			} else if (arg == kHttpPort || arg.starts_with(kHttpPortEquals)) {
				std::string_view value;
				if (arg != kHttpPort) {
					value = arg.substr(kHttpPortEquals.size());
				} else if (i + 1 == args.size() || std::string_view(args[i + 1]).starts_with("--")) {
					LOG_ERROR(Engine, "--http-port needs a port");
					commandLine.valid = false;
					continue;
				} else {
					value = args[++i];
				}
				commandLine.httpPort = parsePort(value);
				if (!commandLine.httpPort) {
					LOG_ERROR(Engine, "Invalid port number '%.*s'", static_cast<int>(value.size()), value.data());
					commandLine.valid = false;
				}
			} else if (arg == kHelp) {
				commandLine.help = true;
			}
		}
		return commandLine;
	}

} // namespace app
