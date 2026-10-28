#pragma once

// Where a game's per-user files go, and which names may become a file or directory name there. The
// directories are computed from the environment, never created here; what writes into one creates it.

#include <filesystem>
#include <string_view>

namespace app {

	// Whether `name` can be a file or directory name on every platform salal runs on: 1 to 64 characters
	// from ASCII letters, digits, space, '-', '_' and '.', not starting or ending with a space or a dot,
	// and not a Windows device name (CON, PRN, AUX, NUL, COM0 to COM9, LPT0 to LPT9, in any case, alone
	// or before a dot). So no separators, no "." or "..", nothing a filesystem folds or refuses.
	bool validFileName(std::string_view name);

	// The per-user settings directory: %APPDATA%\<organization>\<application> on Windows,
	// ~/Library/Application Support/<organization>/<application> on macOS, and
	// $XDG_CONFIG_HOME/<organization>/<application> on Linux, ~/.config/... when that isn't set to an
	// absolute path. Empty, logged, for a name validFileName refuses or a root the environment doesn't
	// give as an absolute path.
	std::filesystem::path userConfigDir(std::string_view organization, std::string_view application);

	// The per-user data directory, for saves: beside the settings directory, not inside it, so clearing
	// one leaves the other. %LOCALAPPDATA%\<organization>\<application> on Windows (saves can be large,
	// and Local doesn't roam), ~/Library/Application Support/<organization>/<application> Data on macOS
	// (which has one root for both), and $XDG_DATA_HOME/<organization>/<application> on Linux,
	// ~/.local/share/... when that isn't set to an absolute path. Empty, logged, as userConfigDir is.
	std::filesystem::path userDataDir(std::string_view organization, std::string_view application);

} // namespace app
