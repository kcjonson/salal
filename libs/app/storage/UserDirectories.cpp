#include "storage/UserDirectories.h"

#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace app {

	namespace {

		constexpr std::size_t kMaxNameLength = 64;

		enum class Root : std::uint8_t { Config, Data };

		// ASCII alone, whatever the locale says.
		bool asciiDigit(char c) {
			return c >= '0' && c <= '9';
		}

		bool asciiLetter(char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
		}

		char lower(char c) {
			return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
		}

		// CON, PRN, AUX and NUL, or COM or LPT and a digit: names Windows gives to devices, with or
		// without an extension.
		bool deviceName(std::string_view stem) {
			std::string folded(stem);
			std::ranges::transform(folded, folded.begin(), lower);
			constexpr std::array<std::string_view, 4> kDevices{"con", "prn", "aux", "nul"};
			if (std::ranges::find(kDevices, folded) != kDevices.end()) {
				return true;
			}
			return folded.size() == 4 && (folded.starts_with("com") || folded.starts_with("lpt")) && asciiDigit(folded[3]);
		}

		// An environment variable as a path; empty when it isn't set. Wide on Windows, so a profile
		// path outside the ANSI code page survives.
#ifdef _WIN32
		std::filesystem::path environmentPath(const wchar_t* name) {
			wchar_t*	value = nullptr;
			std::size_t length = 0;
			if (_wdupenv_s(&value, &length, name) != 0 || value == nullptr) {
				return {};
			}
			std::filesystem::path path(value);
			std::free(value); // NOLINT(cppcoreguidelines-no-malloc): _wdupenv_s allocates with malloc
			return path;
		}
#else
		std::filesystem::path environmentPath(const char* name) {
			const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe): read once, nothing here sets it
			return value != nullptr ? std::filesystem::path(value) : std::filesystem::path();
		}
#endif

		std::filesystem::path absoluteOrEmpty(std::filesystem::path path) {
			return path.is_absolute() ? path : std::filesystem::path();
		}

		// The root the organization's directory goes under.
		std::filesystem::path rootFor(Root root) {
#if defined(_WIN32)
			return absoluteOrEmpty(environmentPath(root == Root::Config ? L"APPDATA" : L"LOCALAPPDATA"));
#elif defined(__APPLE__)
			static_cast<void>(root);
			const std::filesystem::path home = absoluteOrEmpty(environmentPath("HOME"));
			return home.empty() ? home : home / "Library" / "Application Support";
#else
			const std::filesystem::path xdg = absoluteOrEmpty(environmentPath(root == Root::Config ? "XDG_CONFIG_HOME" : "XDG_DATA_HOME"));
			if (!xdg.empty()) {
				return xdg;
			}
			const std::filesystem::path home = absoluteOrEmpty(environmentPath("HOME"));
			if (home.empty()) {
				return home;
			}
			return root == Root::Config ? home / ".config" : home / ".local" / "share";
#endif
		}

		std::string leafFor(Root root, std::string_view application) {
#ifdef __APPLE__
			return std::string(application) + (root == Root::Data ? " Data" : "");
#else
			static_cast<void>(root);
			return std::string(application);
#endif
		}

		std::filesystem::path userDir(Root root, std::string_view organization, std::string_view application) {
			const char* const what = root == Root::Config ? "userConfigDir" : "userDataDir";
			if (!validFileName(organization) || !validFileName(application)) {
				LOG_ERROR(
					Engine,
					"%s: \"%.*s\" and \"%.*s\" have to be valid file names; no directory",
					what,
					static_cast<int>(organization.size()),
					organization.data(),
					static_cast<int>(application.size()),
					application.data()
				);
				return {};
			}
			const std::filesystem::path base = rootFor(root);
			if (base.empty()) {
				LOG_ERROR(Engine, "%s: the environment gives no absolute per-user directory; no directory", what);
				return {};
			}
			// The names are ASCII, so the narrow conversion is exact on every platform.
			return base / std::string(organization) / leafFor(root, application);
		}

	} // namespace

	bool validFileName(std::string_view name) {
		if (name.empty() || name.size() > kMaxNameLength) {
			return false;
		}
		const auto allowed = [](char c) {
			return asciiLetter(c) || asciiDigit(c) || c == ' ' || c == '-' || c == '_' || c == '.';
		};
		if (!std::ranges::all_of(name, allowed)) {
			return false;
		}
		const char first = name.front();
		const char last = name.back();
		if (first == ' ' || first == '.' || last == ' ' || last == '.') {
			return false;
		}
		return !deviceName(name.substr(0, name.find('.')));
	}

	std::filesystem::path userConfigDir(std::string_view organization, std::string_view application) {
		return userDir(Root::Config, organization, application);
	}

	std::filesystem::path userDataDir(std::string_view organization, std::string_view application) {
		return userDir(Root::Data, organization, application);
	}

} // namespace app
