#pragma once

// The user settings file: one JSON object, versioned by the game, read once and saved when the game
// says. Keys are addressed by dotted paths ("video.vsync"), and a section is a subtree the game owns
// (its key bindings, say). Every key in the file survives a save, the ones this build doesn't know
// included, which is how an older and a newer build share one file. The file's top-level "version" is
// the Settings' own: read and taken out at load, written back by save, and no path may start with it.
//
// The App holds one, from AppConfig::settingsFile, and keeps its own keys in it: video.vsync,
// video.fullscreenMonitor (null for windowed), video.windowWidth and video.windowHeight.

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace app {

	namespace detail {

		template <typename T>
		struct IsOptional : std::false_type {};
		template <typename T>
		struct IsOptional<std::optional<T>> : std::true_type {};

		// What get returns for a fallback of type T: a string for a string literal, T otherwise.
		template <typename T>
		using SettingOf =
			std::conditional_t<std::is_same_v<std::decay_t<T>, const char*> || std::is_same_v<std::decay_t<T>, char*>, std::string, T>;

		template <typename T>
		bool finite(const T& value) {
			if constexpr (IsOptional<T>::value) {
				return !value || finite(*value);
			} else if constexpr (std::is_floating_point_v<T>) {
				return std::isfinite(value);
			} else {
				return true;
			}
		}

		// A JSON value as T when it holds one: a bool only from a bool, an integer only from an integer
		// in T's range, a floating point from any number, a string from a string, an optional from null
		// (empty) or from what its value type takes, and anything else as nlohmann converts it.
		template <typename T>
		std::optional<T> fromJson(const nlohmann::json& value) {
			if constexpr (IsOptional<T>::value) {
				if (value.is_null()) {
					return T{};
				}
				std::optional<typename T::value_type> inner = fromJson<typename T::value_type>(value);
				return inner ? std::optional<T>(T(std::move(*inner))) : std::nullopt;
			} else if constexpr (std::is_same_v<T, nlohmann::json>) {
				return value;
			} else if constexpr (std::is_same_v<T, bool>) {
				return value.is_boolean() ? std::optional<T>(value.get<bool>()) : std::nullopt;
			} else if constexpr (std::is_integral_v<T>) {
				if (value.is_number_unsigned()) {
					const auto number = value.get<std::uint64_t>();
					return std::in_range<T>(number) ? std::optional<T>(static_cast<T>(number)) : std::nullopt;
				}
				if (value.is_number_integer()) {
					const auto number = value.get<std::int64_t>();
					return std::in_range<T>(number) ? std::optional<T>(static_cast<T>(number)) : std::nullopt;
				}
				return std::nullopt;
			} else if constexpr (std::is_floating_point_v<T>) {
				return value.is_number() ? std::optional<T>(value.get<T>()) : std::nullopt;
			} else if constexpr (std::is_same_v<T, std::string>) {
				return value.is_string() ? std::optional<T>(value.get<std::string>()) : std::nullopt;
			} else {
				try {
					return value.get<T>();
				} catch (const nlohmann::json::exception&) {
					return std::nullopt;
				}
			}
		}

	} // namespace detail

	class Settings {
	  public:
		// Brings the file's values, found at fromVersion, up to the version load was given. A file with no
		// version reads as version 0.
		using Migrate = std::function<void(nlohmann::json& values, int fromVersion)>;

		// In memory with no file, at version 1: save() writes nothing.
		Settings() = default;

		// The settings in `file`, at the game's `version` (1 or more; below 1 is taken as 1, logged). An
		// empty path is settings in memory with no file. A missing file is empty settings, saved where it
		// would have been. A file that can't be read, isn't JSON, or is JSON but not an object is logged and
		// moved aside to <file>.corrupt (.corrupt.1 and on when that's taken, so an earlier copy is never
		// replaced), and the settings start empty; so do they when `migrate` throws or leaves something
		// other than an object, the file moved aside the same way. When it can't be moved, the settings
		// are this run's alone and save() won't write over it. A file older than `version` goes through
		// `migrate`, once, and is at `version` after it (with no migrate, as it is). A file newer than
		// `version` loads as it is, isn't migrated down, and keeps its version.
		static Settings load(const std::filesystem::path& file, int version, Migrate migrate = {});

		// The value at `path` as T, or a std::string for a string literal fallback; `fallback` when
		// there's nothing there, or something T doesn't take (a string read as a bool, an integer out of
		// T's range, an object read as a number), which is logged. A key with a dot in its name can't be
		// reached by path; read it through its section.
		template <typename T>
		detail::SettingOf<T> get(std::string_view path, T fallback) const {
			using Setting = detail::SettingOf<T>;
			const nlohmann::json* value = find(path);
			if (value == nullptr) {
				return Setting(std::move(fallback));
			}
			std::optional<Setting> converted = detail::fromJson<Setting>(*value);
			if (!converted) {
				typeMismatch(path);
				return Setting(std::move(fallback));
			}
			return std::move(*converted);
		}

		// Sets the value at `path`, making the objects along it. What isn't an object along the way is
		// replaced by one, logged, so a set is always read back. An empty std::optional sets null. A
		// floating point that isn't finite, which JSON can't hold, is refused, logged.
		template <typename T>
		void set(std::string_view path, T value) {
			if (!detail::finite(value)) {
				notFinite(path);
				return;
			}
			nlohmann::json* target = make(path);
			if (target == nullptr) {
				return;
			}
			if constexpr (detail::IsOptional<T>::value) {
				*target = value ? nlohmann::json(std::move(*value)) : nlohmann::json(nullptr);
			} else {
				*target = nlohmann::json(std::move(value));
			}
		}

		// Whether anything, null included, is at `path`.
		bool contains(std::string_view path) const;

		// What's at `path`, making nothing; null when there's nothing there or the path is invalid. For
		// reading a section without creating it: `if (const auto* input = settings.find("input")) ...`.
		const nlohmann::json* find(std::string_view path) const;

		// The object at `path`, made empty when there's nothing there, and replaced by an empty one,
		// logged, when what's there isn't an object. It stays valid until a set replaces it or the
		// Settings are loaded over. An invalid path (empty, with an empty segment, or starting with
		// "version") gives an object that isn't saved, logged.
		nlohmann::json& section(std::string_view path);

		// Writes the file safely (a temp file, flushed, then renamed over it) with every key, and the
		// version; creates its directory when that's missing. False, logged, when that fails, and when the
		// file was corrupt and couldn't be moved aside. With no file there's nothing to write, and it's true.
		bool save();

		// Empty for settings in memory.
		const std::filesystem::path& file() const { return filePath; }
		// The version save() writes: the game's, or a newer file's own.
		int version() const { return fileVersion; }

	  private:
		nlohmann::json* make(std::string_view path);
		void			typeMismatch(std::string_view path) const;
		void			notFinite(std::string_view path) const;

		std::filesystem::path filePath;
		bool				  fileKept = false; // a corrupt file that couldn't be moved aside, which save() mustn't replace
		int					  fileVersion = 1;
		nlohmann::json		  values = nlohmann::json::object();
		nlohmann::json		  detached = nlohmann::json::object();
	};

} // namespace app
