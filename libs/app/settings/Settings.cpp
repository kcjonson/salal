#include "settings/Settings.h"

#include "detail/Guard.h"
#include "detail/SafeFile.h"

#include <utils/Log.h>

#include <span>
#include <vector>

namespace app {

	namespace {

		constexpr std::string_view kVersionKey = "version";
		constexpr std::string_view kCorruptSuffix = ".corrupt";

		// The path's keys; empty for an empty path, one with an empty key ("a..b", ".a"), and one starting
		// with the reserved "version".
		std::vector<std::string> keysOf(std::string_view path) {
			if (path == kVersionKey || path.starts_with(std::string(kVersionKey) + ".")) {
				return {};
			}
			std::vector<std::string> keys;
			while (true) {
				const std::size_t	   dot = path.find('.');
				const std::string_view key = path.substr(0, dot);
				if (key.empty()) {
					return {};
				}
				keys.emplace_back(key);
				if (dot == std::string_view::npos) {
					return keys;
				}
				path.remove_prefix(dot + 1);
			}
		}

		void invalidPath(std::string_view path) {
			LOG_ERROR(
				Engine,
				"Settings: \"%.*s\" isn't a path (keys joined by dots, none empty, not starting with \"version\")",
				static_cast<int>(path.size()),
				path.data()
			);
		}

	} // namespace

	Settings Settings::load(const std::filesystem::path& file, int version, Migrate migrate) {
		Settings settings;
		settings.filePath = file;
		settings.fileVersion = version;
		if (version < 1) {
			LOG_ERROR(Engine, "Settings: a version of %d; versions start at 1, so it's 1", version);
			settings.fileVersion = 1;
		}
		version = settings.fileVersion;
		if (file.empty()) {
			return settings;
		}

		const std::string name = detail::pathText(file);
		const auto		  setAside = [&](const char* why) {
			   LOG_ERROR(Engine, "Settings: %s %s; starting empty", name.c_str(), why);
			   if (!detail::moveAside(file, kCorruptSuffix)) {
				   LOG_ERROR(Engine, "Settings: %s stays where it is, and these settings won't be saved over it", name.c_str());
				   settings.fileKept = true;
			   }
			   return settings;
		};

		const detail::FileRead read = detail::readFile(file);
		if (read.status == detail::FileRead::Status::Missing) {
			LOG_INFO(Engine, "Settings: no file at %s yet", name.c_str());
			return settings;
		}
		if (read.status == detail::FileRead::Status::Unreadable) {
			return setAside("can't be read");
		}
		const auto*	   text = reinterpret_cast<const char*>(read.bytes.data());
		nlohmann::json values = nlohmann::json::parse(text, text + read.bytes.size(), nullptr, false);
		if (values.is_discarded()) {
			return setAside("isn't JSON");
		}
		if (!values.is_object()) {
			return setAside("isn't a JSON object");
		}

		int fromVersion = 0;
		if (const auto found = values.find(std::string(kVersionKey)); found != values.end()) {
			const std::optional<int> given = detail::fromJson<int>(*found);
			if (given && *given >= 0) {
				fromVersion = *given;
			} else {
				LOG_WARNING(Engine, "Settings: %s has a version that isn't a whole number of 0 or more; read as 0", name.c_str());
			}
			values.erase(found);
		}

		if (fromVersion > version) {
			LOG_INFO(Engine, "Settings: %s is version %d, newer than this build's %d; kept as it is", name.c_str(), fromVersion, version);
			settings.fileVersion = fromVersion;
		} else if (fromVersion < version && migrate) {
			const bool migrated = detail::guard("Settings", "migrate", [&] { migrate(values, fromVersion); });
			if (!migrated || !values.is_object()) {
				return setAside("couldn't be migrated");
			}
			LOG_INFO(Engine, "Settings: %s migrated from version %d to %d", name.c_str(), fromVersion, version);
		}
		settings.values = std::move(values);
		return settings;
	}

	bool Settings::contains(std::string_view path) const {
		return find(path) != nullptr;
	}

	nlohmann::json& Settings::section(std::string_view path) {
		nlohmann::json* target = make(path);
		if (target == nullptr) {
			detached = nlohmann::json::object();
			return detached;
		}
		if (!target->is_object()) {
			if (!target->is_null()) {
				LOG_WARNING(
					Engine,
					"Settings: the %s at \"%.*s\" is replaced by an object",
					target->type_name(),
					static_cast<int>(path.size()),
					path.data()
				);
			}
			*target = nlohmann::json::object();
		}
		return *target;
	}

	bool Settings::save() {
		if (filePath.empty()) {
			return true;
		}
		if (fileKept) {
			LOG_ERROR(
				Engine, "Settings: not saving over %s, the corrupt file that couldn't be moved aside", detail::pathText(filePath).c_str()
			);
			return false;
		}
		nlohmann::json written = values;
		written[std::string(kVersionKey)] = fileVersion;
		std::string text;
		try {
			text = written.dump(1, '\t');
		} catch (const nlohmann::json::exception& e) {
			LOG_ERROR(Engine, "Settings: can't write %s: %s", detail::pathText(filePath).c_str(), e.what());
			return false;
		}
		text.push_back('\n');
		return detail::writeFileSafely(filePath, std::as_bytes(std::span(text)));
	}

	const nlohmann::json* Settings::find(std::string_view path) const {
		const std::vector<std::string> keys = keysOf(path);
		if (keys.empty()) {
			invalidPath(path);
			return nullptr;
		}
		const nlohmann::json* node = &values;
		for (const std::string& key : keys) {
			if (!node->is_object()) {
				return nullptr;
			}
			const auto found = node->find(key);
			if (found == node->end()) {
				return nullptr;
			}
			node = &*found;
		}
		return node;
	}

	nlohmann::json* Settings::make(std::string_view path) {
		const std::vector<std::string> keys = keysOf(path);
		if (keys.empty()) {
			invalidPath(path);
			return nullptr;
		}
		nlohmann::json* node = &values;
		for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
			nlohmann::json& child = (*node)[keys[i]];
			if (!child.is_object()) {
				if (!child.is_null()) {
					LOG_WARNING(
						Engine,
						"Settings: setting \"%.*s\" replaces the %s at \"%s\" with an object",
						static_cast<int>(path.size()),
						path.data(),
						child.type_name(),
						keys[i].c_str()
					);
				}
				child = nlohmann::json::object();
			}
			node = &child;
		}
		return &(*node)[keys.back()];
	}

	void Settings::notFinite(std::string_view path) const {
		LOG_WARNING(Engine, "Settings: \"%.*s\" set to a number JSON can't hold; refused", static_cast<int>(path.size()), path.data());
	}

	void Settings::typeMismatch(std::string_view path) const {
		LOG_WARNING(
			Engine,
			"Settings: the value at \"%.*s\" isn't of the type asked for; using the fallback",
			static_cast<int>(path.size()),
			path.data()
		);
	}

} // namespace app
