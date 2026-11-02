#include "ui/theme/Theme.h"

#include "ui/theme/Tokens.h"
#include "ui/theme/dtcg/Resolver.h"

#include <algorithm>
#include <set>
#include <utility>

namespace ui {

	namespace {

		using dtcg::Diagnostic;
		using Severity = Diagnostic::Severity;

		Foundation::Color toColor(const dtcg::ColorValue& color) {
			return {color.r, color.g, color.b, color.a};
		}

		Foundation::TextTransform toTextTransform(dtcg::TextTransform transform) {
			switch (transform) {
				case dtcg::TextTransform::Uppercase:
					return Foundation::TextTransform::Uppercase;
				case dtcg::TextTransform::None:
					break;
			}
			return Foundation::TextTransform::None;
		}

		std::string textOf(const EmbeddedFile& file) {
			std::string text;
			for (std::string_view piece : file.pieces) {
				text += piece;
			}
			return text;
		}

		dtcg::Layer layerOf(const TokenTable& table) {
			dtcg::Layer layer{.label = std::string(table.name), .entry = std::string(table.entry)};
			for (const EmbeddedFile& file : table.files) {
				if (!file.name.ends_with(".resolver.json")) {
					layer.files.emplace_back(file.name);
				}
			}
			layer.read = [&table](const std::string& name) -> std::optional<std::string> {
				for (const EmbeddedFile& file : table.files) {
					if (file.name == name) {
						return textOf(file);
					}
				}
				return std::nullopt;
			};
			return layer;
		}

		// matchWeight's order for one weight, lowest first: the tier, then the place in it.
		std::pair<int, float> weightRank(float weight, float desired) {
			if (desired >= 400.0F && desired <= 500.0F) {
				if (weight >= desired && weight <= 500.0F) {
					return {0, weight};
				}
				return weight < desired ? std::pair{1, -weight} : std::pair{2, weight};
			}
			if (desired < 400.0F) {
				return weight <= desired ? std::pair{0, -weight} : std::pair{1, weight};
			}
			return weight >= desired ? std::pair{0, weight} : std::pair{1, -weight};
		}

		std::string stylePath(std::string_view style, std::string_view field) {
			return std::string(style) + "." + std::string(field);
		}

		// Tokens layer by path, so a game's style that shares a name with one of ui's (its own `tab`)
		// merges into ui's without a word. That's how a game restyles ui's components on purpose, so
		// it's an info, not a warning: one for each ui style a later layer sets a field or salal's
		// extension of, naming them.
		void reportOverriddenStyles(
			const dtcg::TokenSet&		 set,
			const dtcg::SalalExtensions& extensions,
			std::string_view			 uiLabel,
			std::vector<Diagnostic>&	 diagnostics
		) {
			const std::string uiPrefix = std::string(uiLabel) + "/";
			for (std::string_view style : tokens_detail::kTable.styles) {
				std::string fields;
				std::string file;
				const auto	add = [&](std::string_view field, const std::string& from) {
					 fields += (fields.empty() ? "" : ", ") + std::string(field);
					 if (file.empty()) {
						 file = from;
					 }
				};
				for (const dtcg::StyleField& field : dtcg::kStyleFields) {
					const auto origin = set.origins().find(stylePath(style, field.name));
					if (origin != set.origins().end() && !origin->second.starts_with(uiPrefix)) {
						add(field.name, origin->second);
					}
				}
				const auto extension = extensions.find(style);
				if (extension != extensions.end() && extension->second.textTransform.has_value() &&
					!extension->second.file.starts_with(uiPrefix)) {
					add("textTransform", extension->second.file);
				}
				if (!fields.empty()) {
					diagnostics.push_back(
						{.severity = Severity::Info,
						 .file = file,
						 .path = std::string(style),
						 .message = "redefines ui's style (" + fields + "); a game's own styles belong under a group of their own"}
					);
				}
			}
		}

		class FamilyMap {
		  public:
			FamilyMap(std::span<const FontFamilyName> names, std::vector<Diagnostic>& diagnostics)
				: names(names),
				  diagnostics(diagnostics) {}

			// The face nearest `weight` of the first of the value's names the game registered; the
			// default family, with a warning unless the game registered none, if there's none.
			Renderer::FontFamily
			map(const dtcg::FontFamilyValue& value, float weight, const dtcg::ResolvedToken& token, std::string_view path) {
				for (const std::string& wanted : value.names) {
					const FontFamilyName* best = nullptr;
					for (const FontFamilyName& name : names) {
						if (name.name == wanted &&
							(best == nullptr || weightRank(name.weight, weight) < weightRank(best->weight, weight))) {
							best = &name;
						}
					}
					if (best != nullptr) {
						return best->family;
					}
				}
				if (!names.empty() && warned.insert(std::string(path)).second) {
					diagnostics.push_back(
						{.severity = Severity::Warning,
						 .file = token.file,
						 .path = std::string(path),
						 .message =
							 "names no font family the game registered (" + value.names.front() + "); it draws in the default family"}
					);
				}
				return Renderer::FontFamily::Default;
			}

		  private:
			std::span<const FontFamilyName> names;
			std::vector<Diagnostic>&		diagnostics;
			std::set<std::string>			warned;
		};

	} // namespace

	ThemeLoad Theme::load(const ThemeArgs& args) {
		ThemeLoad					   result;
		std::vector<const TokenTable*> chain;
		for (const TokenTable* table = args.tokens; table != nullptr; table = table->base) {
			chain.insert(chain.begin(), table);
		}
		if (chain.empty() || chain.front() != &tokens_detail::kTable) {
			chain.insert(chain.begin(), &tokens_detail::kTable);
		}

		std::vector<dtcg::Layer> layers;
		for (const TokenTable* table : chain) {
			layers.push_back(layerOf(*table));
		}
		const dtcg::TokenSet	   set = dtcg::loadLayers(layers, args.inputs, result.diagnostics);
		dtcg::SalalExtensions	   extensions;
		const dtcg::ResolvedTokens resolved = dtcg::resolve(set, {.rootFontSize = args.rootFontSize}, result.diagnostics, &extensions);
		reportOverriddenStyles(set, extensions, layers.front().label, result.diagnostics);

		std::set<std::string, std::less<>> known;
		std::set<std::string, std::less<>> stylePaths;
		for (const TokenTable* table : chain) {
			for (const TokenEntry& entry : table->tokens) {
				known.emplace(entry.path);
			}
			for (std::string_view style : table->styles) {
				stylePaths.emplace(style);
				for (const dtcg::StyleField& field : dtcg::kStyleFields) {
					known.insert(stylePath(style, field.name));
				}
			}
		}
		for (const auto& [path, extension] : extensions) {
			if (!stylePaths.contains(path)) {
				result.diagnostics.push_back(
					{.severity = Severity::Warning,
					 .file = extension.file,
					 .path = path,
					 .message = "has salal's extension, but no table names it as a style, so nothing reads it"}
				);
			}
		}
		for (const auto& [path, token] : resolved) {
			if (!known.contains(path)) {
				result.diagnostics.push_back(
					{.severity = Severity::Warning,
					 .file = token.file,
					 .path = path,
					 .message = "no table names this token; check the name, or pass the game's table"}
				);
			}
		}

		const auto missing = [&](const TokenTable& table, std::string_view path) {
			result.diagnostics.push_back(
				{.severity = args.strict ? Severity::Error : Severity::Warning,
				 .file = std::string(table.name),
				 .path = std::string(path),
				 .message = "is in " + std::string(table.name) + "'s table but not in the loaded tokens; it reads as its type's default"}
			);
		};
		FamilyMap families(args.fontFamilies, result.diagnostics);

		std::vector<std::string_view> names;
		for (const FontFamilyName& registered : args.fontFamilies) {
			auto listed = std::ranges::find(names, registered.name);
			if (listed == names.end()) {
				listed = names.insert(names.end(), registered.name);
			}
			result.theme.faces.push_back(
				{.name = static_cast<std::size_t>(listed - names.begin()), .family = registered.family, .weight = registered.weight}
			);
		}
		for (const TokenTable* table : chain) {
			Table loaded{.table = table};
			for (const TokenEntry& entry : table->tokens) {
				Slot	   slot;
				const auto found = resolved.find(entry.path);
				if (found == resolved.end()) {
					missing(*table, entry.path);
				} else if (found->second.type != entry.type) {
					result.diagnostics.push_back(
						{.severity = Severity::Error,
						 .file = found->second.file,
						 .path = std::string(entry.path),
						 .message = "is a " + std::string(dtcg::tokenTypeName(found->second.type)) + " in the loaded tokens and a " +
									std::string(dtcg::tokenTypeName(entry.type)) + " in " + std::string(table->name) + "'s table"}
					);
				} else {
					slot.value = found->second.value;
					if (const auto* family = std::get_if<dtcg::FontFamilyValue>(&*slot.value)) {
						slot.family = families.map(*family, 400.0F, found->second, entry.path);
					} else if (const auto* typography = std::get_if<dtcg::TypographyValue>(&*slot.value)) {
						slot.family = families.map(typography->fontFamily, typography->fontWeight, found->second, entry.path);
					}
				}
				loaded.tokens.push_back(std::move(slot));
			}

			for (std::string_view style : table->styles) {
				Style	   themed;
				bool	   any = false;
				const auto field = [&](std::string_view name, dtcg::TokenType type) -> const dtcg::ResolvedToken* {
					const auto found = resolved.find(stylePath(style, name));
					if (found == resolved.end() || found->second.type != type) {
						return nullptr;
					}
					any = true;
					return &found->second;
				};
				const auto color = [&](std::string_view name, std::optional<Foundation::Color>& out) {
					if (const dtcg::ResolvedToken* token = field(name, dtcg::TokenType::Color)) {
						out = toColor(std::get<dtcg::ColorValue>(token->value));
					}
				};
				const auto number = [&](std::string_view name, dtcg::TokenType type, std::optional<float>& out) {
					if (const dtcg::ResolvedToken* token = field(name, type)) {
						out = std::get<float>(token->value);
					}
				};

				// The family maps once the weight is known, which the font or the fontWeight field sets.
				const dtcg::FontFamilyValue* familyValue = nullptr;
				const dtcg::ResolvedToken*	 familyToken = nullptr;
				std::string					 familyPath;
				if (const dtcg::ResolvedToken* token = field("font", dtcg::TokenType::Typography)) {
					const auto& font = std::get<dtcg::TypographyValue>(token->value);
					familyValue = &font.fontFamily;
					familyToken = token;
					familyPath = stylePath(style, "font");
					themed.fontSize = font.fontSize;
					themed.fontWeight = font.fontWeight;
					themed.lineHeight = font.lineHeight;
					themed.letterSpacing = font.letterSpacing;
				}
				color("backgroundColor", themed.backgroundColor);
				color("borderColor", themed.borderColor);
				number("borderWidth", dtcg::TokenType::Dimension, themed.borderWidth);
				number("borderRadius", dtcg::TokenType::Dimension, themed.borderRadius);
				// The renderer draws one outer shadow; the first outer layer is it.
				if (const dtcg::ResolvedToken* token = field("boxShadow", dtcg::TokenType::Shadow)) {
					for (const dtcg::ShadowLayer& layer : std::get<dtcg::ShadowValue>(token->value).layers) {
						if (!layer.inset) {
							themed.boxShadow = Foundation::BoxShadow{
								.color = toColor(layer.color),
								.blur = layer.blur,
								.spread = layer.spread,
								.offset = {layer.offsetX, layer.offsetY}
							};
							break;
						}
					}
				}
				number("opacity", dtcg::TokenType::Number, themed.opacity);
				color("color", themed.color);
				if (const dtcg::ResolvedToken* token = field("fontFamily", dtcg::TokenType::FontFamily)) {
					familyValue = &std::get<dtcg::FontFamilyValue>(token->value);
					familyToken = token;
					familyPath = stylePath(style, "fontFamily");
				}
				number("fontSize", dtcg::TokenType::Dimension, themed.fontSize);
				number("fontWeight", dtcg::TokenType::FontWeight, themed.fontWeight);
				number("lineHeight", dtcg::TokenType::Number, themed.lineHeight);
				number("letterSpacing", dtcg::TokenType::Dimension, themed.letterSpacing);
				if (familyValue != nullptr) {
					themed.fontFamily = families.map(*familyValue, themed.fontWeight.value_or(400.0F), *familyToken, familyPath);
				}
				if (const auto found = extensions.find(style); found != extensions.end() && found->second.textTransform.has_value()) {
					themed.textTransform = toTextTransform(*found->second.textTransform);
					any = true;
				}
				if (!any) {
					missing(*table, style);
				}
				loaded.styles.push_back(themed);
			}
			result.theme.tables.push_back(std::move(loaded));
		}
		return result;
	}

	const Theme::Slot* Theme::slot(const TokenTable* table, std::uint32_t index) const {
		for (const Table& loaded : tables) {
			if (loaded.table == table) {
				return index < loaded.tokens.size() ? &loaded.tokens[index] : nullptr;
			}
		}
		return nullptr;
	}

	template <typename T>
	T Theme::valueOr(const TokenTable* table, std::uint32_t index, T fallback) const {
		const Slot* found = slot(table, index);
		if (found == nullptr || !found->value.has_value()) {
			return fallback;
		}
		const T* value = std::get_if<T>(&*found->value);
		return value != nullptr ? *value : fallback;
	}

	Foundation::Color Theme::get(ColorToken token) const {
		return toColor(valueOr(token.table, token.index, dtcg::ColorValue{}));
	}

	float Theme::get(DimensionToken token) const {
		return valueOr(token.table, token.index, 0.0F);
	}

	float Theme::get(DurationToken token) const {
		return valueOr(token.table, token.index, 0.0F);
	}

	float Theme::get(NumberToken token) const {
		return valueOr(token.table, token.index, 0.0F);
	}

	float Theme::get(FontWeightToken token) const {
		return valueOr(token.table, token.index, 400.0F);
	}

	Renderer::FontFamily Theme::get(FontFamilyToken token) const {
		const Slot* found = slot(token.table, token.index);
		return found != nullptr ? found->family : Renderer::FontFamily::Default;
	}

	Typography Theme::get(TypographyToken token) const {
		const Slot* found = slot(token.table, token.index);
		const auto	value = valueOr(token.table, token.index, dtcg::TypographyValue{});
		return {
			.family = found != nullptr ? found->family : Renderer::FontFamily::Default,
			.fontSize = value.fontSize,
			.fontWeight = value.fontWeight,
			.letterSpacing = value.letterSpacing,
			.lineHeight = value.lineHeight,
		};
	}

	dtcg::CubicBezierValue Theme::get(CubicBezierToken token) const {
		return valueOr(token.table, token.index, dtcg::CubicBezierValue{});
	}

	dtcg::ShadowValue Theme::get(ShadowToken token) const {
		return valueOr(token.table, token.index, dtcg::ShadowValue{});
	}

	dtcg::BorderValue Theme::get(BorderToken token) const {
		return valueOr(token.table, token.index, dtcg::BorderValue{});
	}

	dtcg::TransitionValue Theme::get(TransitionToken token) const {
		return valueOr(token.table, token.index, dtcg::TransitionValue{});
	}

	dtcg::GradientValue Theme::get(GradientToken token) const {
		return valueOr(token.table, token.index, dtcg::GradientValue{});
	}

	dtcg::StrokeStyleValue Theme::get(StrokeStyleToken token) const {
		return valueOr(token.table, token.index, dtcg::StrokeStyleValue{});
	}

	const Style& Theme::style(StyleId id) const {
		static const Style kUnset;
		for (const Table& loaded : tables) {
			if (loaded.table == id.table) {
				return id.index < loaded.styles.size() ? loaded.styles[id.index] : kUnset;
			}
		}
		return kUnset;
	}

	Style Theme::resolve(const Style& declared, StyleId typeStyle) const {
		Style resolved = style(typeStyle).extend(style(declared.theme)).extend(declared);
		resolved.theme = declared.theme;
		if (resolved.fontFamily && resolved.fontWeight) {
			resolved.fontFamily = face(*resolved.fontFamily, *resolved.fontWeight);
		}
		return resolved;
	}

	// A family registered under two names picks among the first name's faces.
	Renderer::FontFamily Theme::face(Renderer::FontFamily family, float weight) const {
		const auto own = std::ranges::find(faces, family, &Face::family);
		if (own == faces.end()) {
			return family;
		}
		const Face* best = &*own;
		for (const Face& each : faces) {
			if (each.name == own->name && weightRank(each.weight, weight) < weightRank(best->weight, weight)) {
				best = &each;
			}
		}
		return best->family;
	}

	std::size_t matchWeight(std::span<const float> weights, float desired) {
		std::size_t best = 0;
		for (std::size_t i = 1; i < weights.size(); ++i) {
			if (weightRank(weights[i], desired) < weightRank(weights[best], desired)) {
				best = i;
			}
		}
		return best;
	}

} // namespace ui
