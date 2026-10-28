#pragma once

// An environment variable set, or unset, for a test's scope and put back after, so the per-user
// directories can be pointed at a temp directory.

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

	// Every variable the per-user directories are read from, pointed under the root given, and put back after.
	class ScopedUserRoots {
	  public:
		explicit ScopedUserRoots(const std::filesystem::path& root) {
			for (const auto& [name, value] : variablesUnder(root)) {
				variables.push_back(std::make_unique<ScopedEnv>(name, value));
			}
		}

	  private:
		static std::vector<std::pair<const char*, std::string>> variablesUnder(const std::filesystem::path& root) {
#ifdef _WIN32
			return {{"APPDATA", (root / "Roaming").string()}, {"LOCALAPPDATA", (root / "Local").string()}};
#else
			return {{"HOME", root.string()}, {"XDG_CONFIG_HOME", (root / "config").string()}, {"XDG_DATA_HOME", (root / "data").string()}};
#endif
		}

		std::vector<std::unique_ptr<ScopedEnv>> variables;
	};

} // namespace app::testing
