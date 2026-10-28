#include "settings/Bindings.h"

#include "input/ControlNames.h"

#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app {

	namespace {

		using foundation::Binding;
		using foundation::GamepadAxis;
		using foundation::GamepadButton;
		using foundation::Key;
		using foundation::MouseButton;
		using foundation::StickDeadzone;

		constexpr const char* kActions = "actions";
		constexpr const char* kSticks = "sticks";

		struct SourceField {
			Binding::Source source;
			const char*		field;
		};
		constexpr std::array<SourceField, 4> kSources{{
			{.source = Binding::Source::Key, .field = "key"},
			{.source = Binding::Source::MouseButton, .field = "mouse"},
			{.source = Binding::Source::GamepadButton, .field = "button"},
			{.source = Binding::Source::GamepadAxis, .field = "axis"},
		}};

		const char* fieldFor(Binding::Source source) {
			for (const SourceField& known : kSources) {
				if (known.source == source) {
					return known.field;
				}
			}
			return "";
		}

		// Checked against Count before the cast, since a value past a uint8_t enum's range would wrap.
		template <typename Control>
		std::string_view nameFor(std::uint16_t control) {
			return control < static_cast<std::uint16_t>(Control::Count) ? controlName(static_cast<Control>(control)) : std::string_view();
		}

		std::string_view controlNameOf(const Binding& binding) {
			switch (binding.source) {
				case Binding::Source::Key:
					return nameFor<Key>(binding.control);
				case Binding::Source::MouseButton:
					return nameFor<MouseButton>(binding.control);
				case Binding::Source::GamepadButton:
					return nameFor<GamepadButton>(binding.control);
				case Binding::Source::GamepadAxis:
					return nameFor<GamepadAxis>(binding.control);
			}
			return {};
		}

		template <typename Control>
		std::optional<std::uint16_t> controlFor(std::string_view name) {
			const std::optional<Control> control = controlNamed<Control>(name);
			return control ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(*control)) : std::nullopt;
		}

		std::optional<std::uint16_t> controlFrom(Binding::Source source, std::string_view name) {
			switch (source) {
				case Binding::Source::Key:
					return controlFor<Key>(name);
				case Binding::Source::MouseButton:
					return controlFor<MouseButton>(name);
				case Binding::Source::GamepadButton:
					return controlFor<GamepadButton>(name);
				case Binding::Source::GamepadAxis:
					return controlFor<GamepadAxis>(name);
			}
			return std::nullopt;
		}

		// A float as the shortest decimal that reads back as it through JSON's double, so 0.1F is written
		// 0.1 rather than 0.10000000149011612; null for what JSON can't hold.
		nlohmann::json number(float value) {
			if (!std::isfinite(value)) {
				return nullptr;
			}
			for (int digits = 6; digits <= 9; ++digits) {
				std::array<char, 32> text{};
				std::snprintf(text.data(), text.size(), "%.*g", digits, static_cast<double>(value));
				const double parsed = std::strtod(text.data(), nullptr);
				if (static_cast<float>(parsed) == value) {
					return parsed;
				}
			}
			return static_cast<double>(value);
		}

		// Absent or null leaves `value` as it was; a number sets it; anything else is false.
		bool readNumber(const nlohmann::json& entry, const char* field, float& value) {
			const auto found = entry.find(field);
			if (found == entry.end() || found->is_null()) {
				return true;
			}
			if (!found->is_number()) {
				return false;
			}
			const auto number = found->get<float>(); // a double past a float's range is infinite here
			if (!std::isfinite(number)) {
				return false;
			}
			value = number;
			return true;
		}

		std::optional<nlohmann::json> bindingJson(const Binding& binding) {
			const std::string_view name = controlNameOf(binding);
			if (name.empty()) {
				return std::nullopt;
			}
			nlohmann::json entry = nlohmann::json::object();
			entry[fieldFor(binding.source)] = std::string(name);
			entry["scale"] = number(binding.scale);
			entry["threshold"] = number(binding.threshold);
			entry["deadzone"] = number(binding.deadzone);
			return entry;
		}

		// One source field naming a control this build knows, and numbers where numbers go.
		std::optional<Binding> bindingFrom(const nlohmann::json& entry) {
			if (!entry.is_object()) {
				return std::nullopt;
			}
			std::optional<Binding> binding;
			for (const SourceField& known : kSources) {
				const auto field = entry.find(known.field);
				if (field == entry.end()) {
					continue;
				}
				if (binding || !field->is_string()) {
					return std::nullopt;
				}
				const std::optional<std::uint16_t> control = controlFrom(known.source, field->get_ref<const std::string&>());
				if (!control) {
					return std::nullopt;
				}
				binding = Binding{.source = known.source, .control = *control};
			}
			if (!binding || !readNumber(entry, "scale", binding->scale) || !readNumber(entry, "threshold", binding->threshold) ||
				!readNumber(entry, "deadzone", binding->deadzone)) {
				return std::nullopt;
			}
			return binding;
		}

		std::optional<std::pair<GamepadAxis, GamepadAxis>> stickAxes(const nlohmann::json& entry) {
			if (!entry.is_object()) {
				return std::nullopt;
			}
			const auto x = entry.find("x");
			const auto y = entry.find("y");
			if (x == entry.end() || y == entry.end() || !x->is_string() || !y->is_string()) {
				return std::nullopt;
			}
			const std::optional<GamepadAxis> xAxis = controlNamed<GamepadAxis>(x->get_ref<const std::string&>());
			const std::optional<GamepadAxis> yAxis = controlNamed<GamepadAxis>(y->get_ref<const std::string&>());
			if (!xAxis || !yAxis) {
				return std::nullopt;
			}
			return std::pair{*xAxis, *yAxis};
		}

		// The section's member `key` as an object or an array, made when it's missing and replaced, logged,
		// when it's something else.
		nlohmann::json& member(nlohmann::json& section, const char* key, nlohmann::json::value_t type) {
			nlohmann::json& value = section[key];
			if (value.type() != type) {
				if (!value.is_null()) {
					LOG_WARNING(Engine, "Bindings: the section's \"%s\" was a %s; replaced", key, value.type_name());
				}
				value = type == nlohmann::json::value_t::object ? nlohmann::json::object() : nlohmann::json::array();
			}
			return value;
		}

		void skipped(const char* what, const std::string& action, const nlohmann::json& entry) {
			LOG_WARNING(Engine, "Bindings: skipped %s%s%s: %s", what, action.empty() ? "" : " of ", action.c_str(), entry.dump().c_str());
		}

	} // namespace

	void writeBindings(const foundation::ActionMap& map, nlohmann::json& section) {
		if (!section.is_object()) {
			section = nlohmann::json::object();
		}
		nlohmann::json& actions = member(section, kActions, nlohmann::json::value_t::object);
		for (const std::string_view action : map.actions()) {
			nlohmann::json list = nlohmann::json::array();
			for (const Binding& binding : map.bindings(action)) {
				if (std::optional<nlohmann::json> entry = bindingJson(binding)) {
					list.push_back(std::move(*entry));
				} else {
					LOG_WARNING(
						Engine,
						"Bindings: a binding of %.*s to control %u has no name; not written",
						static_cast<int>(action.size()),
						action.data(),
						static_cast<unsigned>(binding.control)
					);
				}
			}
			actions[std::string(action)] = std::move(list);
		}

		nlohmann::json& sticks = member(section, kSticks, nlohmann::json::value_t::array);
		for (const StickDeadzone& stick : map.stickDeadzones()) {
			const std::string_view x = controlName(stick.x);
			const std::string_view y = controlName(stick.y);
			if (x.empty() || y.empty()) {
				LOG_WARNING(Engine, "Bindings: a stick deadzone on an axis with no name; not written");
				continue;
			}
			nlohmann::json entry = {{"x", std::string(x)}, {"y", std::string(y)}, {"radius", number(stick.radius)}};
			const auto	   same = std::ranges::find_if(sticks, [&](const nlohmann::json& existing) {
				const auto axes = stickAxes(existing);
				return axes && ((axes->first == stick.x && axes->second == stick.y) || (axes->first == stick.y && axes->second == stick.x));
			});
			if (same != sticks.end()) {
				*same = std::move(entry);
			} else {
				sticks.push_back(std::move(entry));
			}
		}
	}

	void readBindings(const nlohmann::json& section, foundation::ActionMap& map) {
		if (!section.is_object()) {
			if (!section.is_null()) {
				LOG_WARNING(Engine, "Bindings: the section is a %s, not an object; nothing read", section.type_name());
			}
			return;
		}
		if (const auto actions = section.find(kActions); actions != section.end()) {
			if (!actions->is_object()) {
				skipped("\"actions\"", "", *actions);
			} else {
				for (const auto& [action, list] : actions->items()) {
					if (!list.is_array()) {
						skipped("the list", action, list);
						continue;
					}
					std::vector<Binding> read;
					for (const nlohmann::json& entry : list) {
						if (const std::optional<Binding> binding = bindingFrom(entry)) {
							read.push_back(*binding);
						} else {
							skipped("a binding", action, entry);
						}
					}
					// A list with nothing this build can read is a newer build's, not a player's clearing.
					if (!list.empty() && read.empty()) {
						LOG_WARNING(Engine, "Bindings: nothing in %s's list could be read; its bindings stay as they were", action.c_str());
						continue;
					}
					map.unbind(action);
					for (const Binding& binding : read) {
						map.bind(action, binding);
					}
				}
			}
		}
		if (const auto sticks = section.find(kSticks); sticks != section.end()) {
			if (!sticks->is_array()) {
				skipped("\"sticks\"", "", *sticks);
				return;
			}
			for (const nlohmann::json& entry : *sticks) {
				const auto axes = stickAxes(entry);
				const auto radius = entry.is_object() ? entry.find("radius") : entry.end();
				if (!axes || radius == entry.end() || !radius->is_number() || !std::isfinite(radius->get<float>())) {
					skipped("a stick", "", entry);
					continue;
				}
				map.setStickDeadzone(axes->first, axes->second, radius->get<float>());
			}
		}
	}

} // namespace app
