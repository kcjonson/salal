#include "ui/theme/dtcg/Document.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <set>

namespace ui::dtcg {

	using nlohmann::json;

	namespace {

		using Severity = Diagnostic::Severity;

		bool isToken(const json& value) {
			return value.is_object() && value.contains("$value");
		}

		bool isGroup(const json& value) {
			return value.is_object() && !value.contains("$value");
		}

		std::string join(const std::string& path, const std::string& name) {
			return path.empty() ? name : path + "." + name;
		}

		// A child entry of a group: a token or group name, or $root (format 6.2).
		bool isChildKey(const std::string& key) {
			return key == "$root" || !key.starts_with('$');
		}

		std::string inQuotes(std::string_view text) {
			return "\"" + std::string(text) + "\"";
		}

		constexpr const char* kSalal = "salal"; // salal's key in $extensions

		std::optional<TextTransform> textTransformNamed(const json& value) {
			if (value.is_string()) {
				for (const TextTransformName& known : kTextTransforms) {
					if (known.name == value.get<std::string>()) {
						return known.value;
					}
				}
			}
			return std::nullopt;
		}

		// "none or uppercase", from the table.
		std::string textTransformChoices() {
			std::string choices;
			for (size_t i = 0; i < kTextTransforms.size(); ++i) {
				if (i > 0) {
					choices += i + 1 == kTextTransforms.size() ? " or " : ", ";
				}
				choices += kTextTransforms[i].name;
			}
			return choices;
		}

		// Structural checks on one document before it merges: names, the $ properties, tokens without
		// children (format 5.1, 5.2, 6.1, 6.3).
		class Checker {
		  public:
			Checker(const std::string& file, std::vector<Diagnostic>& diagnostics)
				: file(file),
				  diagnostics(diagnostics) {}

			bool checkDocument(const json& document) {
				if (!document.is_object()) {
					error("", "a tokens file is a JSON object");
					return false;
				}
				checkGroup(document, "", true);
				return ok;
			}

		  private:
			void error(const std::string& path, std::string message) {
				diagnostics.push_back({.severity = Severity::Error, .file = file, .path = path, .message = std::move(message)});
				ok = false;
			}

			void warning(const std::string& path, std::string message) {
				diagnostics.push_back({.severity = Severity::Warning, .file = file, .path = path, .message = std::move(message)});
			}

			// salal's extension: on a group, its text transform and nothing else.
			void checkSalal(const json& salal, const std::string& path, bool token) {
				if (token) {
					error(path, "$extensions.salal goes on a style's group, not on a token");
					return;
				}
				if (!salal.is_object()) {
					error(path, "$extensions.salal is not an object");
					return;
				}
				for (const auto& [key, value] : salal.items()) {
					if (key != "textTransform") {
						error(path, "$extensions.salal has no property " + inQuotes(key) + "; it takes textTransform");
					} else if (!textTransformNamed(value).has_value()) {
						error(path, "$extensions.salal.textTransform " + value.dump() + " is not " + textTransformChoices());
					}
				}
			}

			// The $ properties tokens and groups share. False if `key` is none of them.
			bool checkShared(const std::string& key, const json& value, const std::string& path, bool token) {
				if (key == "$type") {
					if (!value.is_string()) {
						error(path, "$type is not a string");
					} else if (!parseTokenType(value.get<std::string>()).has_value()) {
						error(path, "unknown $type " + inQuotes(value.get<std::string>()));
					}
				} else if (key == "$description") {
					if (!value.is_string()) {
						error(path, "$description is not a string");
					}
				} else if (key == "$deprecated") {
					if (!value.is_boolean() && !value.is_string()) {
						error(path, "$deprecated is not true, false or a string");
					}
				} else if (key == "$extensions") {
					if (!value.is_object()) {
						error(path, "$extensions is not an object");
					} else if (value.contains(kSalal)) {
						checkSalal(value.at(kSalal), path, token);
					}
				} else {
					return false;
				}
				return true;
			}

			void checkToken(const json& token, const std::string& path) {
				for (const auto& [key, value] : token.items()) {
					if (!key.starts_with('$')) {
						error(path, "has a $value and a child " + inQuotes(key) + "; a token can't hold tokens or groups");
					} else if (key != "$value" && !checkShared(key, value, path, true)) {
						warning(path, "unknown property " + key + " ignored");
					}
				}
			}

			void checkGroup(const json& group, const std::string& path, bool top) {
				for (const auto& [key, value] : group.items()) {
					const std::string child = join(path, key);
					if (isChildKey(key)) {
						if (key != "$root") {
							if (key.empty()) {
								error(child, "a name can't be empty");
								continue;
							}
							if (key.find_first_of("{}.") != std::string::npos) {
								error(child, "a name can't contain {, } or .");
								continue;
							}
						}
						if (!value.is_object()) {
							error(child, "is neither a token (an object with $value) nor a group");
						} else if (isToken(value)) {
							checkToken(value, child);
						} else if (key == "$root") {
							error(child, "$root is a token, so it needs a $value");
						} else {
							checkGroup(value, child, false);
						}
					} else if (key == "$extends") {
						if (!value.is_string()) {
							error(path, "$extends is not a reference string");
						}
					} else if (!checkShared(key, value, path, false) && !(top && key == "$schema")) {
						warning(path, "unknown property " + key + " ignored");
					}
				}
			}

			const std::string&		 file;
			std::vector<Diagnostic>& diagnostics;
			bool					 ok = true;
		};

		// --- Decoding a resolved $value into its type (format 8, 9; colour module 4) ---

		std::string inKey(std::string_view key, const std::string& why) {
			return std::string(key) + ": " + why;
		}

		bool onlyKeys(const json& object, std::initializer_list<std::string_view> allowed, std::string& why) {
			for (const auto& [key, unused] : object.items()) {
				if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
					why = "unexpected property " + inQuotes(key);
					return false;
				}
			}
			return true;
		}

		bool requireKeys(const json& object, std::initializer_list<std::string_view> required, std::string& why) {
			for (std::string_view key : required) {
				if (!object.contains(key)) {
					why = "missing " + inQuotes(key);
					return false;
				}
			}
			return true;
		}

		bool exactKeys(const json& object, std::initializer_list<std::string_view> keys, std::string& why) {
			if (!object.is_object()) {
				why = "not an object";
				return false;
			}
			return onlyKeys(object, keys, why) && requireKeys(object, keys, why);
		}

		std::optional<float> asNumber(const json& value) {
			if (!value.is_number()) {
				return std::nullopt;
			}
			return static_cast<float>(value.get<double>());
		}

		std::optional<int> hexNumber(std::string_view digits) {
			if (digits.empty() || digits.find_first_not_of("0123456789abcdefABCDEF") != std::string_view::npos) {
				return std::nullopt;
			}
			return static_cast<int>(std::strtol(std::string(digits).c_str(), nullptr, 16));
		}

		float hexChannel(std::string_view digits) {
			return static_cast<float>(*hexNumber(digits)) / 255.0F;
		}

		constexpr std::array<std::string_view, 14> kColorSpaces{
			"srgb",
			"srgb-linear",
			"hsl",
			"hwb",
			"lab",
			"lch",
			"oklab",
			"oklch",
			"display-p3",
			"a98-rgb",
			"prophoto-rgb",
			"rec2020",
			"xyz-d65",
			"xyz-d50",
		};

		std::optional<ColorValue> decodeColor(const json& value, std::string& why) {
			if (!value.is_object()) {
				why = "a color is an object with colorSpace and components";
				return std::nullopt;
			}
			if (!onlyKeys(value, {"colorSpace", "components", "alpha", "hex"}, why) ||
				!requireKeys(value, {"colorSpace", "components"}, why)) {
				return std::nullopt;
			}
			const json& space = value["colorSpace"];
			if (!space.is_string() || std::find(kColorSpaces.begin(), kColorSpaces.end(), space.get<std::string>()) == kColorSpaces.end()) {
				why = "colorSpace " + space.dump() + " is not one of the format's colour spaces";
				return std::nullopt;
			}
			const json& components = value["components"];
			if (!components.is_array() || components.size() != 3) {
				why = "components is not an array of three";
				return std::nullopt;
			}
			std::array<float, 3> channels{};
			for (size_t i = 0; i < 3; ++i) {
				if (components[i].is_string() && components[i].get<std::string>() == "none") {
					channels[i] = 0.0F;
				} else if (const std::optional<float> channel = asNumber(components[i])) {
					channels[i] = *channel;
				} else {
					why = "components[" + std::to_string(i) + "] is neither a number nor \"none\"";
					return std::nullopt;
				}
			}
			ColorValue color;
			if (value.contains("alpha")) {
				const std::optional<float> alpha = asNumber(value["alpha"]);
				if (!alpha.has_value() || *alpha < 0.0F || *alpha > 1.0F) {
					why = "alpha is not a number from 0 to 1";
					return std::nullopt;
				}
				color.a = *alpha;
			}
			std::optional<std::string> hex;
			if (value.contains("hex")) {
				const json& hexValue = value["hex"];
				const bool	shaped =
					hexValue.is_string() && hexValue.get<std::string>().size() == 7 && hexValue.get<std::string>()[0] == '#';
				if (!shaped || !hexNumber(hexValue.get<std::string>().substr(1)).has_value()) {
					why = "hex is not a six-digit CSS colour like \"#ff00ff\"";
					return std::nullopt;
				}
				hex = hexValue.get<std::string>();
			}
			if (space.get<std::string>() == "srgb") {
				for (float channel : channels) {
					if (channel < 0.0F || channel > 1.0F) {
						why = "an srgb component is outside 0 to 1";
						return std::nullopt;
					}
				}
				color.r = channels[0];
				color.g = channels[1];
				color.b = channels[2];
				return color;
			}
			if (!hex.has_value()) {
				why = "colorSpace " + inQuotes(space.get<std::string>()) + " has no hex fallback; the renderer blends sRGB";
				return std::nullopt;
			}
			color.r = hexChannel(hex->substr(1, 2));
			color.g = hexChannel(hex->substr(3, 2));
			color.b = hexChannel(hex->substr(5, 2));
			return color;
		}

		std::optional<float> decodeUnitValue(
			const json&												  value,
			std::string_view										  what,
			std::initializer_list<std::pair<std::string_view, float>> units,
			std::string&											  why
		) {
			if (!exactKeys(value, {"value", "unit"}, why)) {
				why = std::string("a ") + std::string(what) + " is {\"value\", \"unit\"}: " + why;
				return std::nullopt;
			}
			const std::optional<float> number = asNumber(value["value"]);
			if (!number.has_value()) {
				why = "value is not a number";
				return std::nullopt;
			}
			for (const auto& [unit, scale] : units) {
				if (value["unit"].is_string() && value["unit"].get<std::string>() == unit) {
					return *number * scale;
				}
			}
			std::string names;
			for (const auto& [unit, scale] : units) {
				names += (names.empty() ? "" : " or ") + std::string(unit);
			}
			why = "unit " + value["unit"].dump() + " is not " + names;
			return std::nullopt;
		}

		std::optional<float> decodeDimension(const json& value, const ResolveOptions& options, std::string& why) {
			return decodeUnitValue(value, "dimension", {{"px", 1.0F}, {"rem", options.rootFontSize}}, why);
		}

		std::optional<float> decodeDuration(const json& value, std::string& why) {
			return decodeUnitValue(value, "duration", {{"ms", 1.0F}, {"s", 1000.0F}}, why);
		}

		std::optional<CubicBezierValue> decodeCubicBezier(const json& value, std::string& why) {
			if (!value.is_array() || value.size() != 4 ||
				!std::all_of(value.begin(), value.end(), [](const json& n) { return n.is_number(); })) {
				why = "a cubicBezier is an array of four numbers";
				return std::nullopt;
			}
			const CubicBezierValue curve{
				.x1 = *asNumber(value[0]), .y1 = *asNumber(value[1]), .x2 = *asNumber(value[2]), .y2 = *asNumber(value[3])
			};
			if (curve.x1 < 0.0F || curve.x1 > 1.0F || curve.x2 < 0.0F || curve.x2 > 1.0F) {
				why = "a cubicBezier's x values are from 0 to 1";
				return std::nullopt;
			}
			return curve;
		}

		constexpr std::array<std::pair<std::string_view, float>, 18> kFontWeights{{
			{"thin", 100.0F},
			{"hairline", 100.0F},
			{"extra-light", 200.0F},
			{"ultra-light", 200.0F},
			{"light", 300.0F},
			{"normal", 400.0F},
			{"regular", 400.0F},
			{"book", 400.0F},
			{"medium", 500.0F},
			{"semi-bold", 600.0F},
			{"demi-bold", 600.0F},
			{"bold", 700.0F},
			{"extra-bold", 800.0F},
			{"ultra-bold", 800.0F},
			{"black", 900.0F},
			{"heavy", 900.0F},
			{"extra-black", 950.0F},
			{"ultra-black", 950.0F},
		}};

		std::optional<float> decodeFontWeight(const json& value, std::string& why) {
			if (const std::optional<float> number = asNumber(value)) {
				if (*number < 1.0F || *number > 1000.0F) {
					why = "a numeric fontWeight is from 1 to 1000";
					return std::nullopt;
				}
				return number;
			}
			if (value.is_string()) {
				for (const auto& [name, weight] : kFontWeights) {
					if (name == value.get<std::string>()) {
						return weight;
					}
				}
			}
			why = "fontWeight " + value.dump() +
				  " is not a number from 1 to 1000 or one of the format's names (\"bold\", \"semi-bold\", ...)";
			return std::nullopt;
		}

		std::optional<FontFamilyValue> decodeFontFamily(const json& value, std::string& why) {
			FontFamilyValue family;
			if (value.is_string()) {
				family.names.push_back(value.get<std::string>());
			} else if (value.is_array()) {
				for (const json& name : value) {
					if (!name.is_string()) {
						why = "a fontFamily array holds strings only";
						return std::nullopt;
					}
					family.names.push_back(name.get<std::string>());
				}
			}
			if (family.names.empty() ||
				std::any_of(family.names.begin(), family.names.end(), [](const std::string& n) { return n.empty(); })) {
				why = "a fontFamily is a name or a non-empty array of names";
				return std::nullopt;
			}
			return family;
		}

		constexpr std::array<std::string_view, 8> kLineStyles{"solid", "dashed", "dotted", "double", "groove", "ridge", "outset", "inset"};

		std::optional<StrokeStyleValue> decodeStrokeStyle(const json& value, const ResolveOptions& options, std::string& why) {
			StrokeStyleValue stroke;
			if (value.is_string()) {
				if (std::find(kLineStyles.begin(), kLineStyles.end(), value.get<std::string>()) == kLineStyles.end()) {
					why = "strokeStyle " + value.dump() + " is not a CSS line style";
					return std::nullopt;
				}
				stroke.keyword = value.get<std::string>();
				return stroke;
			}
			if (!exactKeys(value, {"dashArray", "lineCap"}, why)) {
				why = "a strokeStyle is a keyword or {\"dashArray\", \"lineCap\"}: " + why;
				return std::nullopt;
			}
			if (!value["dashArray"].is_array() || value["dashArray"].empty()) {
				why = "dashArray is not a non-empty array";
				return std::nullopt;
			}
			for (const json& dash : value["dashArray"]) {
				const std::optional<float> length = decodeDimension(dash, options, why);
				if (!length.has_value()) {
					why = inKey("dashArray", why);
					return std::nullopt;
				}
				stroke.dashArray.push_back(*length);
			}
			const json& cap = value["lineCap"];
			if (!cap.is_string() || (cap != "round" && cap != "butt" && cap != "square")) {
				why = "lineCap is not round, butt or square";
				return std::nullopt;
			}
			stroke.lineCap = cap.get<std::string>();
			return stroke;
		}

		template <typename T, typename Decode>
		bool field(const json& object, std::string_view key, T& out, std::string& why, Decode decode) {
			std::optional<T> decoded = decode(object[std::string(key)], why);
			if (!decoded.has_value()) {
				why = inKey(key, why);
				return false;
			}
			out = std::move(*decoded);
			return true;
		}

		std::optional<BorderValue> decodeBorder(const json& value, const ResolveOptions& options, std::string& why) {
			BorderValue border;
			if (!exactKeys(value, {"color", "width", "style"}, why)) {
				why = "a border is {\"color\", \"width\", \"style\"}: " + why;
				return std::nullopt;
			}
			const bool ok =
				field(value, "color", border.color, why, decodeColor) &&
				field(value, "width", border.width, why, [&](const json& v, std::string& w) { return decodeDimension(v, options, w); }) &&
				field(value, "style", border.style, why, [&](const json& v, std::string& w) { return decodeStrokeStyle(v, options, w); });
			return ok ? std::optional(border) : std::nullopt;
		}

		std::optional<TransitionValue> decodeTransition(const json& value, std::string& why) {
			TransitionValue transition;
			if (!exactKeys(value, {"duration", "delay", "timingFunction"}, why)) {
				why = "a transition is {\"duration\", \"delay\", \"timingFunction\"}: " + why;
				return std::nullopt;
			}
			const bool ok = field(value, "duration", transition.durationMs, why, decodeDuration) &&
							field(value, "delay", transition.delayMs, why, decodeDuration) &&
							field(value, "timingFunction", transition.timingFunction, why, decodeCubicBezier);
			return ok ? std::optional(transition) : std::nullopt;
		}

		// A reference inside an array resolves to the target's whole $value; when that is itself an
		// array (a shadow or gradient token), its elements take the reference's place.
		std::vector<const json*> spliced(const json& value) {
			std::vector<const json*> elements;
			for (const json& element : value) {
				if (element.is_array()) {
					for (const json& inner : element) {
						elements.push_back(&inner);
					}
				} else {
					elements.push_back(&element);
				}
			}
			return elements;
		}

		std::optional<ShadowLayer> decodeShadowLayer(const json& value, const ResolveOptions& options, std::string& why) {
			ShadowLayer layer;
			if (!value.is_object() || !onlyKeys(value, {"color", "offsetX", "offsetY", "blur", "spread", "inset"}, why) ||
				!requireKeys(value, {"color", "offsetX", "offsetY", "blur", "spread"}, why)) {
				why = "a shadow is {\"color\", \"offsetX\", \"offsetY\", \"blur\", \"spread\"} with an optional \"inset\"" +
					  (why.empty() ? "" : ": " + why);
				return std::nullopt;
			}
			const auto dimension = [&](const json& v, std::string& w) {
				return decodeDimension(v, options, w);
			};
			const bool ok = field(value, "color", layer.color, why, decodeColor) &&
							field(value, "offsetX", layer.offsetX, why, dimension) &&
							field(value, "offsetY", layer.offsetY, why, dimension) && field(value, "blur", layer.blur, why, dimension) &&
							field(value, "spread", layer.spread, why, dimension);
			if (!ok) {
				return std::nullopt;
			}
			if (value.contains("inset")) {
				if (!value["inset"].is_boolean()) {
					why = "inset is not true or false";
					return std::nullopt;
				}
				layer.inset = value["inset"].get<bool>();
			}
			return layer;
		}

		std::optional<ShadowValue> decodeShadow(const json& value, const ResolveOptions& options, std::string& why) {
			ShadowValue shadow;
			if (value.is_object()) {
				std::optional<ShadowLayer> layer = decodeShadowLayer(value, options, why);
				if (!layer.has_value()) {
					return std::nullopt;
				}
				shadow.layers.push_back(*layer);
				return shadow;
			}
			if (!value.is_array() || value.empty()) {
				why = "a shadow is a shadow object or a non-empty array of them";
				return std::nullopt;
			}
			for (const json* element : spliced(value)) {
				std::optional<ShadowLayer> layer = decodeShadowLayer(*element, options, why);
				if (!layer.has_value()) {
					return std::nullopt;
				}
				shadow.layers.push_back(*layer);
			}
			return shadow;
		}

		std::optional<GradientValue> decodeGradient(const json& value, std::string& why) {
			if (!value.is_array() || value.empty()) {
				why = "a gradient is a non-empty array of stops";
				return std::nullopt;
			}
			GradientValue gradient;
			for (const json* element : spliced(value)) {
				GradientStop stop;
				if (!exactKeys(*element, {"color", "position"}, why)) {
					why = "a gradient stop is {\"color\", \"position\"}: " + why;
					return std::nullopt;
				}
				const std::optional<float> position = asNumber((*element)["position"]);
				if (!field(*element, "color", stop.color, why, decodeColor)) {
					return std::nullopt;
				}
				if (!position.has_value()) {
					why = "position is not a number";
					return std::nullopt;
				}
				stop.position = std::clamp(*position, 0.0F, 1.0F);
				gradient.stops.push_back(stop);
			}
			return gradient;
		}

		std::optional<TypographyValue> decodeTypography(const json& value, const ResolveOptions& options, std::string& why) {
			TypographyValue typography;
			if (!exactKeys(value, {"fontFamily", "fontSize", "fontWeight", "letterSpacing", "lineHeight"}, why)) {
				why = "a typography is {\"fontFamily\", \"fontSize\", \"fontWeight\", \"letterSpacing\", \"lineHeight\"}: " + why;
				return std::nullopt;
			}
			const auto dimension = [&](const json& v, std::string& w) {
				return decodeDimension(v, options, w);
			};
			const auto number = [](const json& v, std::string& w) {
				std::optional<float> n = asNumber(v);
				if (!n.has_value()) {
					w = "not a number";
				}
				return n;
			};
			const bool ok = field(value, "fontFamily", typography.fontFamily, why, decodeFontFamily) &&
							field(value, "fontSize", typography.fontSize, why, dimension) &&
							field(value, "fontWeight", typography.fontWeight, why, decodeFontWeight) &&
							field(value, "letterSpacing", typography.letterSpacing, why, dimension) &&
							field(value, "lineHeight", typography.lineHeight, why, number);
			return ok ? std::optional(typography) : std::nullopt;
		}

		template <typename T>
		std::optional<Value> wrap(std::optional<T> value) {
			return value.has_value() ? std::optional<Value>(std::move(*value)) : std::nullopt;
		}

		std::optional<Value> decode(TokenType type, const json& value, const ResolveOptions& options, std::string& why) {
			switch (type) {
				case TokenType::Color:
					return wrap(decodeColor(value, why));
				case TokenType::Dimension:
					return wrap(decodeDimension(value, options, why));
				case TokenType::FontFamily:
					return wrap(decodeFontFamily(value, why));
				case TokenType::FontWeight:
					return wrap(decodeFontWeight(value, why));
				case TokenType::Duration:
					return wrap(decodeDuration(value, why));
				case TokenType::CubicBezier:
					return wrap(decodeCubicBezier(value, why));
				case TokenType::Number:
					if (!value.is_number()) {
						why = "a number token's $value is a JSON number";
						return std::nullopt;
					}
					return Value(*asNumber(value));
				case TokenType::StrokeStyle:
					return wrap(decodeStrokeStyle(value, options, why));
				case TokenType::Border:
					return wrap(decodeBorder(value, options, why));
				case TokenType::Transition:
					return wrap(decodeTransition(value, why));
				case TokenType::Shadow:
					return wrap(decodeShadow(value, options, why));
				case TokenType::Gradient:
					return wrap(decodeGradient(value, why));
				case TokenType::Typography:
					return wrap(decodeTypography(value, options, why));
			}
			why = "unknown type";
			return std::nullopt;
		}

		// --- References ---

		bool isCurlyReference(const std::string& text) {
			return text.size() >= 3 && text.front() == '{' && text.back() == '}' && text.find_first_of("{}", 1) == text.size() - 1;
		}

		std::vector<std::string> splitDots(const std::string& path) {
			std::vector<std::string> segments;
			size_t					 start = 0;
			while (start <= path.size()) {
				const size_t dot = path.find('.', start);
				const size_t end = dot == std::string::npos ? path.size() : dot;
				segments.push_back(path.substr(start, end - start));
				if (dot == std::string::npos) {
					break;
				}
				start = dot + 1;
			}
			return segments;
		}

		// "#/a/b~1c" -> {"a", "b/c"} (RFC 6901, after percent-decoding the fragment); nullopt if malformed.
		std::optional<std::vector<std::string>> pointerSegments(const std::string& pointer) {
			if (!pointer.starts_with("#/")) {
				return std::nullopt;
			}
			std::string decoded;
			for (size_t i = 1; i < pointer.size(); ++i) {
				if (pointer[i] == '%' && i + 2 < pointer.size()) {
					const std::optional<int> byte = hexNumber(std::string_view(pointer).substr(i + 1, 2));
					if (!byte.has_value()) {
						return std::nullopt;
					}
					decoded += static_cast<char>(*byte);
					i += 2;
				} else {
					decoded += pointer[i];
				}
			}
			std::vector<std::string> segments;
			size_t					 start = 1;
			while (start <= decoded.size()) {
				const size_t slash = decoded.find('/', start);
				std::string	 segment = decoded.substr(start, (slash == std::string::npos ? decoded.size() : slash) - start);
				std::string	 unescaped;
				for (size_t i = 0; i < segment.size(); ++i) {
					if (segment[i] == '~' && i + 1 < segment.size() && (segment[i + 1] == '0' || segment[i + 1] == '1')) {
						unescaped += segment[i + 1] == '1' ? '/' : '~';
						++i;
					} else if (segment[i] == '~') {
						return std::nullopt;
					} else {
						unescaped += segment[i];
					}
				}
				segments.push_back(std::move(unescaped));
				if (slash == std::string::npos) {
					break;
				}
				start = slash + 1;
			}
			return segments;
		}

		bool isWithin(const std::string& path, const std::string& ancestor) {
			return ancestor.empty() || path == ancestor || path.starts_with(ancestor + ".");
		}

		// Group $extends (format 6.4): a deep merge of the target under the group, the group's own
		// entries winning and a token replaced whole. Expanded on the merged tree before any alias.
		class Extender {
		  public:
			Extender(json& tree, std::map<std::string, std::string>& origin, std::vector<Diagnostic>& diagnostics)
				: tree(tree),
				  origin(origin),
				  diagnostics(diagnostics) {}

			void expandWithin(const std::string& path) {
				json* group = groupAt(path);
				if (group == nullptr) {
					return;
				}
				if (group->contains("$extends")) {
					extend(path);
					group = groupAt(path);
				}
				std::vector<std::string> children;
				for (const auto& [key, value] : group->items()) {
					if (!key.starts_with('$') && isGroup(value)) {
						children.push_back(key);
					}
				}
				for (const std::string& child : children) {
					expandWithin(join(path, child));
				}
			}

		  private:
			json* groupAt(const std::string& path) {
				json* node = &tree;
				if (path.empty()) {
					return node;
				}
				for (const std::string& segment : splitDots(path)) {
					if (!isGroup(*node) || !node->contains(segment)) {
						return nullptr;
					}
					node = &(*node)[segment];
				}
				return isGroup(*node) ? node : nullptr;
			}

			void error(const std::string& path, std::string message) {
				const auto found = origin.find(path);
				diagnostics.push_back(
					{.severity = Severity::Error,
					 .file = found == origin.end() ? "" : found->second,
					 .path = path,
					 .message = std::move(message)}
				);
			}

			static void overlay(json& base, const json& local) {
				for (const auto& [key, value] : local.items()) {
					if (isChildKey(key) && isGroup(value) && base.contains(key) && isGroup(base[key])) {
						overlay(base[key], value);
					} else {
						base[key] = value;
					}
				}
			}

			void extend(const std::string& path) {
				json*			  group = groupAt(path);
				const std::string reference = (*group)["$extends"].get<std::string>();
				group->erase("$extends");
				std::string target;
				if (isCurlyReference(reference)) {
					target = reference.substr(1, reference.size() - 2);
				} else if (const std::optional<std::vector<std::string>> segments = pointerSegments(reference)) {
					for (const std::string& segment : *segments) {
						target = join(target, segment);
					}
				} else {
					error(path, "$extends " + inQuotes(reference) + " is not a reference");
					return;
				}
				if (isWithin(path, target) || isWithin(target, path)) {
					error(path, "$extends " + inQuotes(reference) + " names this group, a group above it or one inside it");
					return;
				}
				for (const std::string& open : inProgress) {
					if (isWithin(target, open)) {
						error(path, "$extends " + inQuotes(reference) + " circles back through " + open);
						return;
					}
				}
				inProgress.insert(path);
				expandWithin(target);
				inProgress.erase(path);
				const json* source = groupAt(target);
				if (source == nullptr) {
					error(path, "$extends " + inQuotes(reference) + " names no group");
					return;
				}
				json merged = *source;
				group = groupAt(path);
				overlay(merged, *group);
				*group = std::move(merged);
				const std::string								 prefix = target + ".";
				std::vector<std::pair<std::string, std::string>> inherited;
				for (auto it = origin.lower_bound(prefix); it != origin.end() && it->first.starts_with(prefix); ++it) {
					inherited.emplace_back(path + "." + it->first.substr(prefix.size()), it->second);
				}
				for (auto& [inheritedPath, file] : inherited) {
					origin.try_emplace(inheritedPath, file);
				}
			}

			json&								tree;
			std::map<std::string, std::string>& origin;
			std::vector<Diagnostic>&			diagnostics;
			std::set<std::string>				inProgress;
		};

		class Resolution {
		  public:
			Resolution(
				const json&								  tree,
				const std::map<std::string, std::string>& origin,
				const ResolveOptions&					  options,
				std::vector<Diagnostic>&				  diagnostics
			)
				: tree(tree),
				  origin(origin),
				  options(options),
				  diagnostics(diagnostics) {
				collect(tree, "", std::nullopt);
			}

			ResolvedTokens run() {
				ResolvedTokens resolved;
				for (auto& [path, entry] : entries) {
					resolveToken(path);
					if (entry.state == State::Done) {
						resolved.emplace(
							path, ResolvedToken{.type = *entry.type, .value = std::move(*entry.decoded), .file = fileOf(path)}
						);
					}
				}
				return resolved;
			}

		  private:
			enum class State : std::uint8_t { Pending, Resolving, Done, Failed };

			struct Entry {
				const json*				 token = nullptr;
				std::optional<TokenType> groupType;
				State					 state = State::Pending;
				json					 value;
				std::optional<TokenType> type;
				std::optional<Value>	 decoded;
			};

			void collect(const json& group, const std::string& path, std::optional<TokenType> inherited) {
				if (group.contains("$type") && group["$type"].is_string()) {
					inherited = parseTokenType(group["$type"].get<std::string>());
				}
				for (const auto& [key, value] : group.items()) {
					if (!isChildKey(key) || !value.is_object()) {
						continue;
					}
					if (isToken(value)) {
						entries[join(path, key)] = Entry{.token = &value, .groupType = inherited};
					} else {
						collect(value, join(path, key), inherited);
					}
				}
			}

			std::string fileOf(const std::string& path) const {
				const auto found = origin.find(path);
				return found == origin.end() ? "" : found->second;
			}

			void error(const std::string& path, std::string message) {
				diagnostics.push_back({.severity = Severity::Error, .file = fileOf(path), .path = path, .message = std::move(message)});
			}

			void fail(const std::string& path, Entry& entry, std::optional<std::string> message) {
				if (message.has_value()) {
					error(path, std::move(*message));
				}
				entry.state = State::Failed;
			}

			void resolveToken(const std::string& path) {
				Entry& entry = entries.at(path);
				if (entry.state != State::Pending) {
					return;
				}
				entry.state = State::Resolving;
				std::optional<TokenType> explicitType;
				if (entry.token->contains("$type")) {
					explicitType = parseTokenType((*entry.token)["$type"].get<std::string>());
				}
				std::optional<TokenType>  aliasType;
				const std::optional<json> value = substitute((*entry.token)["$value"], path, &aliasType);
				if (!value.has_value()) {
					fail(path, entry, std::nullopt);
					return;
				}
				const std::optional<TokenType> type = explicitType ? explicitType : (aliasType ? aliasType : entry.groupType);
				if (!type.has_value()) {
					fail(path, entry, "has no type: no $type on it, its alias target or a group above it");
					return;
				}
				if (explicitType && aliasType && *explicitType != *aliasType) {
					fail(
						path,
						entry,
						"is a " + std::string(tokenTypeName(*explicitType)) + " but aliases a " + std::string(tokenTypeName(*aliasType))
					);
					return;
				}
				std::string why;
				entry.decoded = decode(*type, *value, options, why);
				if (!entry.decoded.has_value()) {
					fail(path, entry, "$value is not a valid " + std::string(tokenTypeName(*type)) + ": " + why);
					return;
				}
				entry.value = *value;
				entry.type = type;
				entry.state = State::Done;
			}

			// The token at `target` resolved for a reference from `from`, or null with an error.
			const Entry* referTo(const std::string& target, const std::string& from, const std::string& spelling) {
				const auto found = entries.find(target);
				if (found == entries.end()) {
					error(from, "references " + spelling + ", which is not a token");
					return nullptr;
				}
				if (found->second.state == State::Resolving) {
					error(from, "circular reference: " + spelling + " leads back to " + from);
					return nullptr;
				}
				resolveToken(target);
				if (found->second.state != State::Done) {
					error(from, "references " + spelling + ", which has errors");
					return nullptr;
				}
				return &found->second;
			}

			std::optional<json> resolvePointer(const std::string& pointer, const std::string& from, std::optional<TokenType>* wholeType) {
				const std::optional<std::vector<std::string>> segments = pointerSegments(pointer);
				if (!segments.has_value()) {
					error(from, "$ref " + inQuotes(pointer) + " is not a JSON Pointer into this token set (#/...)");
					return std::nullopt;
				}
				const json* node = &tree;
				std::string tokenPath;
				size_t		next = 0;
				for (; next < segments->size() && !isToken(*node); ++next) {
					if (!node->is_object() || !node->contains((*segments)[next])) {
						error(from, "$ref " + inQuotes(pointer) + " points at nothing");
						return std::nullopt;
					}
					node = &(*node)[(*segments)[next]];
					tokenPath = join(tokenPath, (*segments)[next]);
				}
				if (!isToken(*node)) {
					error(from, "$ref " + inQuotes(pointer) + " points at a group, not a token");
					return std::nullopt;
				}
				const Entry* target = referTo(tokenPath, from, inQuotes(pointer));
				if (target == nullptr) {
					return std::nullopt;
				}
				if (next == segments->size()) {
					*wholeType = target->type;
					return target->value;
				}
				if ((*segments)[next] != "$value") {
					error(
						from,
						"$ref " + inQuotes(pointer) + " points at a token's " + (*segments)[next] +
							"; only $value and what it holds can be referenced"
					);
					return std::nullopt;
				}
				if (++next == segments->size()) {
					*wholeType = target->type;
				}
				const json* inside = &target->value;
				for (; next < segments->size(); ++next) {
					const std::string& segment = (*segments)[next];
					if (inside->is_object() && inside->contains(segment)) {
						inside = &(*inside)[segment];
					} else if (inside->is_array() && !segment.empty() && segment.find_first_not_of("0123456789") == std::string::npos &&
							   std::stoul(segment) < inside->size()) {
						inside = &(*inside)[std::stoul(segment)];
					} else {
						error(from, "$ref " + inQuotes(pointer) + " points at nothing inside " + tokenPath);
						return std::nullopt;
					}
				}
				return *inside;
			}

			// `value` with every reference replaced by what it points at. `wholeType` is set when the
			// value as a whole is a reference, to the target's type.
			std::optional<json> substitute(const json& value, const std::string& from, std::optional<TokenType>* wholeType) {
				std::optional<TokenType> unused;
				if (value.is_string() && isCurlyReference(value.get<std::string>())) {
					const std::string& text = value.get<std::string>();
					const Entry*	   target = referTo(text.substr(1, text.size() - 2), from, text);
					if (target == nullptr) {
						return std::nullopt;
					}
					if (wholeType != nullptr) {
						*wholeType = target->type;
					}
					return target->value;
				}
				if (value.is_object() && value.size() == 1 && value.contains("$ref")) {
					if (!value["$ref"].is_string()) {
						error(from, "$ref is not a string");
						return std::nullopt;
					}
					return resolvePointer(value["$ref"].get<std::string>(), from, wholeType != nullptr ? wholeType : &unused);
				}
				if (value.is_object() || value.is_array()) {
					json out = value;
					for (auto it = out.begin(); it != out.end(); ++it) {
						std::optional<json> inner = substitute(*it, from, nullptr);
						if (!inner.has_value()) {
							return std::nullopt;
						}
						*it = std::move(*inner);
					}
					return out;
				}
				return value;
			}

			const json&								  tree;
			const std::map<std::string, std::string>& origin;
			const ResolveOptions&					  options;
			std::vector<Diagnostic>&				  diagnostics;
			std::map<std::string, Entry>			  entries;
		};

		// Every group under `group` with salal's extension, which the checker has held to its shape.
		void collectExtensions(
			const json&								  group,
			const std::string&						  path,
			const std::map<std::string, std::string>& origin,
			SalalExtensions&						  out
		) {
			const auto extensions = group.find("$extensions");
			if (extensions != group.end() && extensions->is_object() && extensions->contains(kSalal)) {
				const json&	   salal = extensions->at(kSalal);
				SalalExtension extension;
				if (salal.is_object() && salal.contains("textTransform")) {
					extension.textTransform = textTransformNamed(salal.at("textTransform"));
				}
				const auto file = origin.find(path);
				extension.file = file == origin.end() ? "" : file->second;
				out[path] = std::move(extension);
			}
			for (const auto& [key, value] : group.items()) {
				if (isChildKey(key) && isGroup(value)) {
					collectExtensions(value, join(path, key), origin, out);
				}
			}
		}

	} // namespace

	nlohmann::json parseJson(std::string_view text, const std::string& file, std::vector<Diagnostic>& diagnostics) {
		try {
			return json::parse(text);
		} catch (const json::parse_error& error) {
			diagnostics.push_back(
				{.severity = Severity::Error, .file = file, .path = "", .message = std::string("invalid JSON: ") + error.what()}
			);
			return json(json::value_t::discarded);
		}
	}

	void TokenSet::merge(const nlohmann::json& document, const std::string& file, std::vector<Diagnostic>& diagnostics) {
		if (document.is_discarded()) {
			return;
		}
		Checker checker(file, diagnostics);
		if (!checker.checkDocument(document)) {
			return;
		}
		mergeGroup(tree, document, "", file, diagnostics);
	}

	void TokenSet::mergeGroup(
		nlohmann::json&			 into,
		const nlohmann::json&	 from,
		const std::string&		 path,
		const std::string&		 file,
		std::vector<Diagnostic>& diagnostics
	) {
		for (const auto& [key, value] : from.items()) {
			if (!isChildKey(key)) {
				into[key] = value;
				origin[path] = file;
				continue;
			}
			const std::string child = join(path, key);
			const bool		  present = into.contains(key);
			if (isToken(value)) {
				if (present && isGroup(into[key])) {
					diagnostics.push_back(
						{.severity = Severity::Error,
						 .file = file,
						 .path = child,
						 .message = "is a token here and a group in an earlier file"}
					);
					continue;
				}
				into[key] = value;
				origin[child] = file;
			} else {
				if (present && isToken(into[key])) {
					diagnostics.push_back(
						{.severity = Severity::Error,
						 .file = file,
						 .path = child,
						 .message = "is a group here and a token in " + origin[child]}
					);
					continue;
				}
				if (!present) {
					into[key] = json::object();
				}
				mergeGroup(into[key], value, child, file, diagnostics);
			}
		}
	}

	ResolvedTokens
	resolve(const TokenSet& set, const ResolveOptions& options, std::vector<Diagnostic>& diagnostics, SalalExtensions* extensions) {
		json							   tree = set.root();
		std::map<std::string, std::string> origin = set.origins();
		Extender(tree, origin, diagnostics).expandWithin("");
		if (extensions != nullptr) {
			collectExtensions(tree, "", origin, *extensions);
		}
		return Resolution(tree, origin, options, diagnostics).run();
	}

} // namespace ui::dtcg
