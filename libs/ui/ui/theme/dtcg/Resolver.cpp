#include "ui/theme/dtcg/Resolver.h"

#include <filesystem>
#include <set>

namespace ui::dtcg {

	using nlohmann::json;

	namespace {

		using Severity = Diagnostic::Severity;

		constexpr std::string_view kSetsPrefix = "#/sets/";
		constexpr std::string_view kModifiersPrefix = "#/modifiers/";

		std::string relativeTo(const std::string& file, const std::string& reference) {
			const std::filesystem::path base = std::filesystem::path(file).parent_path();
			return (base / std::filesystem::path(reference)).lexically_normal().generic_string();
		}

		std::string displayName(const Layer& layer, const std::string& name) {
			return layer.label.empty() ? name : layer.label + "/" + name;
		}

		std::optional<json> readDocument(const Layer& layer, const std::string& name, std::vector<Diagnostic>& diagnostics) {
			const std::optional<std::string> text = layer.read ? layer.read(name) : std::nullopt;
			if (!text.has_value()) {
				diagnostics.push_back(
					{.severity = Severity::Error, .file = displayName(layer, name), .path = "", .message = "cannot be read"}
				);
				return std::nullopt;
			}
			json document = parseJson(*text, displayName(layer, name), diagnostics);
			if (document.is_discarded()) {
				return std::nullopt;
			}
			return document;
		}

		std::optional<Modifier> modifierFrom(const json& modifier, const std::string& name) {
			if (!modifier.is_object() || !modifier.contains("contexts") || !modifier["contexts"].is_object()) {
				return std::nullopt;
			}
			Modifier result{.name = name};
			for (const auto& [context, unused] : modifier["contexts"].items()) {
				result.contexts.push_back(context);
			}
			if (modifier.contains("default") && modifier["default"].is_string()) {
				result.defaultContext = modifier["default"].get<std::string>();
			}
			return result;
		}

		// One resolver file evaluated for one set of inputs (module section 6), merging into `into`.
		class ResolverRun {
		  public:
			ResolverRun(
				const Layer&			 layer,
				const json&				 document,
				const ResolverInputs&	 inputs,
				TokenSet&				 into,
				std::vector<Diagnostic>& diagnostics
			)
				: layer(layer),
				  document(document),
				  inputs(inputs),
				  into(into),
				  diagnostics(diagnostics) {}

			void run() {
				if (!document.is_object()) {
					error("", "a resolver file is a JSON object");
					return;
				}
				if (!document.contains("version") || document["version"] != "2025.10") {
					error("", "version is not \"2025.10\"");
					return;
				}
				if (!document.contains("resolutionOrder") || !document["resolutionOrder"].is_array()) {
					error("", "resolutionOrder is missing or not an array");
					return;
				}
				std::set<std::string> inlineNames;
				for (const json& item : document["resolutionOrder"]) {
					if (item.is_object() && item.contains("$ref") && item["$ref"].is_string()) {
						const std::string reference = item["$ref"].get<std::string>();
						if (const json* set = named("sets", kSetsPrefix, reference)) {
							applySet(*set, reference);
						} else if (const json* modifier = named("modifiers", kModifiersPrefix, reference)) {
							applyModifier(*modifier, reference.substr(kModifiersPrefix.size()));
						} else {
							error("", "resolutionOrder entry " + reference + " names no set or modifier of this file");
						}
						continue;
					}
					if (!item.is_object() || !item.contains("type") || !item.contains("name") || !item["name"].is_string()) {
						error("", "a resolutionOrder entry is a {\"$ref\"} or an inline set or modifier with a type and a name");
						continue;
					}
					const std::string name = item["name"].get<std::string>();
					if (!inlineNames.insert(name).second) {
						error("", "two resolutionOrder entries are named " + name);
						continue;
					}
					if (item["type"] == "set") {
						applySet(item, "resolutionOrder/" + name);
					} else if (item["type"] == "modifier") {
						applyModifier(item, name);
					} else {
						error("", "resolutionOrder entry " + name + " has a type other than \"set\" or \"modifier\"");
					}
				}
			}

		  private:
			void error(const std::string& path, std::string message) {
				diagnostics.push_back(
					{.severity = Severity::Error, .file = displayName(layer, layer.entry), .path = path, .message = std::move(message)}
				);
			}

			// The set or modifier `reference` points at in this file (#/sets/<name>), or null.
			const json* named(const char* section, std::string_view prefix, const std::string& reference) const {
				if (!reference.starts_with(prefix) || !document.contains(section) || !document[section].is_object()) {
					return nullptr;
				}
				const std::string name = reference.substr(prefix.size());
				const json&		  entries = document[section];
				return entries.contains(name) ? &entries[name] : nullptr;
			}

			void applySet(const json& set, const std::string& where) {
				if (!set.is_object() || !set.contains("sources") || !set["sources"].is_array()) {
					error("", where + " has no sources array");
					return;
				}
				if (!openSets.insert(where).second) {
					error("", where + " includes itself");
					return;
				}
				for (const json& source : set["sources"]) {
					applySource(source, where);
				}
				openSets.erase(where);
			}

			void applyModifier(const json& modifier, const std::string& name) {
				if (!modifier.is_object() || !modifier.contains("contexts") || !modifier["contexts"].is_object() ||
					modifier["contexts"].empty()) {
					error("", "modifier " + name + " has no contexts");
					return;
				}
				const json& contexts = modifier["contexts"];
				if (contexts.size() == 1) {
					diagnostics.push_back(
						{.severity = Severity::Warning,
						 .file = displayName(layer, layer.entry),
						 .path = "",
						 .message = "modifier " + name + " has one context; a modifier chooses between two or more"}
					);
				}
				std::optional<std::string> context;
				if (modifier.contains("default")) {
					if (!modifier["default"].is_string() || !contexts.contains(modifier["default"].get<std::string>())) {
						error("", "modifier " + name + "'s default is not one of its contexts");
						return;
					}
					context = modifier["default"].get<std::string>();
				}
				if (const auto input = inputs.find(name); input != inputs.end()) {
					context = input->second;
				}
				if (!context.has_value()) {
					error("", "modifier " + name + " has no input and no default");
					return;
				}
				if (!contexts.contains(*context)) {
					error("", "modifier " + name + " has no context " + *context);
					return;
				}
				if (!contexts[*context].is_array()) {
					error("", "modifier " + name + "'s context " + *context + " is not an array");
					return;
				}
				for (const json& source : contexts[*context]) {
					applySource(source, "modifier " + name);
				}
			}

			void applySource(const json& source, const std::string& where) {
				if (!source.is_object()) {
					error("", "a source in " + where + " is neither a {\"$ref\"} nor a token object");
					return;
				}
				if (!source.contains("$ref")) {
					into.merge(source, displayName(layer, layer.entry), diagnostics);
					return;
				}
				if (!source["$ref"].is_string()) {
					error("", "a $ref in " + where + " is not a string");
					return;
				}
				const std::string reference = source["$ref"].get<std::string>();
				json			  loaded;
				std::string		  loadedName;
				if (reference.starts_with(kModifiersPrefix)) {
					error("", where + " points at " + reference + "; only the resolution order may name a modifier");
					return;
				}
				if (reference.starts_with("#/resolutionOrder")) {
					error("", where + " points into the resolution order");
					return;
				}
				if (const json* set = named("sets", kSetsPrefix, reference)) {
					applySet(*set, reference);
					return;
				}
				if (reference.starts_with('#')) {
					try {
						loaded = document.at(json::json_pointer(reference.substr(1)));
					} catch (const json::exception&) {
						error("", where + " points at " + reference + ", which is not in this file");
						return;
					}
					loadedName = layer.entry;
				} else {
					const size_t		hash = reference.find('#');
					const std::string	name = relativeTo(layer.entry, reference.substr(0, hash));
					std::optional<json> file = readDocument(layer, name, diagnostics);
					if (!file.has_value()) {
						return;
					}
					loaded = std::move(*file);
					loadedName = name;
					if (hash != std::string::npos) {
						std::string fragment = reference.substr(hash + 1);
						if (!fragment.starts_with('/')) {
							fragment.insert(fragment.begin(), '/');
						}
						try {
							loaded = loaded.at(json::json_pointer(fragment));
						} catch (const json::exception&) {
							error("", where + " points at " + reference + ", which is not in " + name);
							return;
						}
					}
				}
				// Keys beside a $ref override the target's own, shallowly (module 4.2.2).
				for (const auto& [key, value] : source.items()) {
					if (key != "$ref" && loaded.is_object()) {
						loaded[key] = value;
					}
				}
				into.merge(loaded, displayName(layer, loadedName), diagnostics);
			}

			const Layer&			 layer;
			const json&				 document;
			const ResolverInputs&	 inputs;
			TokenSet&				 into;
			std::vector<Diagnostic>& diagnostics;
			std::set<std::string>	 openSets;
		};

	} // namespace

	std::vector<Modifier> layerModifiers(const Layer& layer) {
		std::vector<Modifier> modifiers;
		if (layer.entry.empty()) {
			return modifiers;
		}
		std::vector<Diagnostic>	  ignored; // loadLayers reports the same problems
		const std::optional<json> document = readDocument(layer, layer.entry, ignored);
		if (!document.has_value() || !document->is_object() || !document->contains("resolutionOrder") ||
			!(*document)["resolutionOrder"].is_array()) {
			return modifiers;
		}
		for (const json& item : (*document)["resolutionOrder"]) {
			if (!item.is_object()) {
				continue;
			}
			std::optional<Modifier> modifier;
			if (item.contains("$ref") && item["$ref"].is_string() && item["$ref"].get<std::string>().starts_with(kModifiersPrefix)) {
				const std::string name = item["$ref"].get<std::string>().substr(kModifiersPrefix.size());
				if (document->contains("modifiers") && (*document)["modifiers"].contains(name)) {
					modifier = modifierFrom((*document)["modifiers"][name], name);
				}
			} else if (item.contains("type") && item["type"] == "modifier" && item.contains("name") && item["name"].is_string()) {
				modifier = modifierFrom(item, item["name"].get<std::string>());
			}
			if (modifier.has_value()) {
				modifiers.push_back(std::move(*modifier));
			}
		}
		return modifiers;
	}

	std::vector<ResolverInputs> permutations(std::span<const Modifier> modifiers) {
		std::vector<ResolverInputs> result{{}};
		for (const Modifier& modifier : modifiers) {
			std::vector<ResolverInputs> next;
			for (const ResolverInputs& partial : result) {
				for (const std::string& context : modifier.contexts) {
					ResolverInputs inputs = partial;
					inputs[modifier.name] = context;
					next.push_back(std::move(inputs));
				}
			}
			if (!next.empty()) {
				result = std::move(next);
			}
		}
		return result;
	}

	TokenSet loadLayers(std::span<const Layer> layers, const ResolverInputs& inputs, std::vector<Diagnostic>& diagnostics) {
		std::set<std::string, std::less<>> known;
		for (const Layer& layer : layers) {
			for (const Modifier& modifier : layerModifiers(layer)) {
				known.insert(modifier.name);
			}
		}
		for (const auto& [name, context] : inputs) {
			if (!known.contains(name)) {
				diagnostics.push_back(
					{.severity = Severity::Error,
					 .file = "",
					 .path = "",
					 .message = "input names modifier " + name + ", which no resolver has"}
				);
			}
		}
		TokenSet set;
		for (const Layer& layer : layers) {
			if (layer.entry.empty()) {
				for (const std::string& name : layer.files) {
					if (std::optional<json> document = readDocument(layer, name, diagnostics)) {
						set.merge(*document, displayName(layer, name), diagnostics);
					}
				}
				continue;
			}
			if (std::optional<json> document = readDocument(layer, layer.entry, diagnostics)) {
				ResolverRun(layer, *document, inputs, set, diagnostics).run();
			}
		}
		return set;
	}

} // namespace ui::dtcg
