#pragma once

// An environment variable set, or unset, for a test's scope and put back after, so the per-user
// directories can be pointed at a temp directory.

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace app::testing {

	class ScopedEnv {
	  public:
		ScopedEnv(const char* variable, const std::optional<std::string>& value)
			: name(variable),
			  previous(get(variable)) {
			set(variable, value);
		}
		~ScopedEnv() { set(name, previous); }

		ScopedEnv(const ScopedEnv&) = delete;
		ScopedEnv& operator=(const ScopedEnv&) = delete;
		ScopedEnv(ScopedEnv&&) = delete;
		ScopedEnv& operator=(ScopedEnv&&) = delete;

	  private:
		static std::optional<std::string> get(const char* variable) {
#ifdef _WIN32
			char*		value = nullptr;
			std::size_t length = 0;
			if (_dupenv_s(&value, &length, variable) != 0 || value == nullptr) {
				return std::nullopt;
			}
			std::string copy(value);
			std::free(value); // NOLINT(cppcoreguidelines-no-malloc)
			return copy;
#else
			const char* value = std::getenv(variable); // NOLINT(concurrency-mt-unsafe)
			return value != nullptr ? std::optional<std::string>(value) : std::nullopt;
#endif
		}

		static void set(const char* variable, const std::optional<std::string>& value) {
#ifdef _WIN32
			_putenv_s(variable, value ? value->c_str() : ""); // an empty value unsets it on Windows
#else
			if (value) {
				setenv(variable, value->c_str(), 1); // NOLINT(concurrency-mt-unsafe)
			} else {
				unsetenv(variable); // NOLINT(concurrency-mt-unsafe)
			}
#endif
		}

		const char*				   name;
		std::optional<std::string> previous;
	};

	// Every variable the per-user directories are read from, pointed under `root`.
	class ScopedUserRoots {
	  public:
	  explicit ScopedUserRoots(const std::filesystem::path& root)
#ifdef _WIN32
		  : config("APPDATA", (root / "Roaming").string()),
			data("LOCALAPPDATA", (root / "Local").string()){}
#else
			: config("XDG_CONFIG_HOME", (root / "config").string()),
			  data("XDG_DATA_HOME", (root / "data").string()),
			  home("HOME", root.string()) {
		}
#endif

			private : ScopedEnv config;
		ScopedEnv data;
#ifndef _WIN32
		ScopedEnv home;
#endif
	};

} // namespace app::testing
