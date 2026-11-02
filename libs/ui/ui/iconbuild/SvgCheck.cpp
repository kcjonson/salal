#include "ui/iconbuild/SvgCheck.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>

namespace ui::iconbuild {

	namespace {

		struct Tag {
			std::string						   name;
			std::map<std::string, std::string> attributes;
			int								   line = 0;
			bool							   closing = false;
			bool							   selfClosing = false;
		};

		// Just enough XML for an icon: tags and their attributes, comments, processing instructions,
		// CDATA and a doctype skipped, text ignored.
		class Scanner {
		  public:
			explicit Scanner(std::string_view text)
				: text(text) {}

			// False at the end; `error` is set if the markup is broken.
			bool next(Tag& tag, std::string& error) {
				while (true) {
					const size_t open = text.find('<', pos);
					if (open == std::string_view::npos) {
						return false;
					}
					advanceTo(open);
					if (skip("<!--", "-->", error) || skip("<?", "?>", error) || skip("<![CDATA[", "]]>", error)) {
						if (!error.empty()) {
							return false;
						}
						continue;
					}
					if (text.substr(pos).starts_with("<!")) {
						const size_t end = text.find('>', pos);
						if (end == std::string_view::npos) {
							error = "the markup ends inside a <!...>";
							return false;
						}
						if (text.substr(pos, end - pos).find('[') != std::string_view::npos) {
							error = "has a DOCTYPE with an internal subset; entities are references, and references are refused";
							return false;
						}
						advanceTo(end + 1);
						continue;
					}
					return readTag(tag, error);
				}
			}

			int currentLine() const { return line; }

		  private:
			void advanceTo(size_t to) {
				line += static_cast<int>(
					std::count(text.begin() + static_cast<std::ptrdiff_t>(pos), text.begin() + static_cast<std::ptrdiff_t>(to), '\n')
				);
				pos = to;
			}

			bool skip(std::string_view open, std::string_view close, std::string& error) {
				if (!text.substr(pos).starts_with(open)) {
					return false;
				}
				const size_t end = text.find(close, pos + open.size());
				if (end == std::string_view::npos) {
					error = "the markup ends inside a " + std::string(open);
					return true;
				}
				advanceTo(end + close.size());
				return true;
			}

			static bool isNameChar(char c) {
				return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == ':' || c == '.';
			}

			void skipSpace() {
				while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
					advanceTo(pos + 1);
				}
			}

			bool readTag(Tag& tag, std::string& error) {
				tag = Tag{.line = line};
				advanceTo(pos + 1);
				if (pos < text.size() && text[pos] == '/') {
					tag.closing = true;
					advanceTo(pos + 1);
				}
				const size_t nameStart = pos;
				while (pos < text.size() && isNameChar(text[pos])) {
					++pos;
				}
				tag.name = std::string(text.substr(nameStart, pos - nameStart));
				if (tag.name.empty()) {
					error = "has a tag with no name";
					return false;
				}
				while (true) {
					skipSpace();
					if (pos >= text.size()) {
						error = "the markup ends inside <" + tag.name + ">";
						return false;
					}
					if (text[pos] == '>') {
						advanceTo(pos + 1);
						return true;
					}
					if (text.substr(pos).starts_with("/>")) {
						tag.selfClosing = true;
						advanceTo(pos + 2);
						return true;
					}
					const size_t attributeStart = pos;
					while (pos < text.size() && isNameChar(text[pos])) {
						++pos;
					}
					const std::string attribute(text.substr(attributeStart, pos - attributeStart));
					skipSpace();
					if (attribute.empty() || pos >= text.size() || text[pos] != '=') {
						error = "has a malformed attribute in <" + tag.name + ">";
						return false;
					}
					advanceTo(pos + 1);
					skipSpace();
					const char	 quote = pos < text.size() ? text[pos] : '\0';
					const size_t end = quote == '"' || quote == '\'' ? text.find(quote, pos + 1) : std::string_view::npos;
					if (end == std::string_view::npos) {
						error = "has an unquoted or unterminated attribute " + attribute + " in <" + tag.name + ">";
						return false;
					}
					tag.attributes[attribute] = std::string(text.substr(pos + 1, end - pos - 1));
					advanceTo(end + 1);
				}
			}

			std::string_view text;
			size_t			 pos = 0;
			int				 line = 1;
		};

		std::string trim(std::string_view text) {
			const size_t start = text.find_first_not_of(" \t\r\n");
			if (start == std::string_view::npos) {
				return "";
			}
			const size_t end = text.find_last_not_of(" \t\r\n");
			return std::string(text.substr(start, end - start + 1));
		}

		// An element's presentation properties: its attributes with its style="..." declarations over
		// them, as CSS has it.
		std::map<std::string, std::string> propertiesOf(const Tag& tag) {
			std::map<std::string, std::string> properties = tag.attributes;
			const auto						   style = tag.attributes.find("style");
			if (style == tag.attributes.end()) {
				return properties;
			}
			size_t start = 0;
			while (start < style->second.size()) {
				const size_t	  end = std::min(style->second.find(';', start), style->second.size());
				const std::string declaration = style->second.substr(start, end - start);
				const size_t	  colon = declaration.find(':');
				if (colon != std::string::npos) {
					properties[trim(declaration.substr(0, colon))] = trim(declaration.substr(colon + 1));
				}
				start = end + 1;
			}
			return properties;
		}

		// A colour in one spelling, so equal colours compare equal: lowercase, #rgb grown to #rrggbb.
		std::string normalColor(std::string color) {
			std::transform(color.begin(), color.end(), color.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			color.erase(std::remove_if(color.begin(), color.end(), [](unsigned char c) { return std::isspace(c) != 0; }), color.end());
			if (color.size() == 4 && color[0] == '#') {
				color = std::string{'#', color[1], color[1], color[2], color[2], color[3], color[3]};
			}
			if (color == "black") {
				return "#000000";
			}
			if (color == "white") {
				return "#ffffff";
			}
			return color;
		}

		constexpr std::array<std::string_view, 4> kShapes{"path", "rect", "circle", "polygon"};
		constexpr std::array<std::string_view, 3> kContainers{"svg", "g", "defs"};
		constexpr std::array<std::string_view, 3> kDescriptive{"title", "desc", "metadata"};
		constexpr std::array<std::string_view, 3> kStrokedShapes{"line", "polyline", "ellipse"};

		template <size_t N>
		bool among(const std::array<std::string_view, N>& names, std::string_view name) {
			return std::find(names.begin(), names.end(), name) != names.end();
		}

		struct Paint {
			std::string fill = "#000000"; // SVG's initial fill
			FillRule	fillRule = FillRule::NonZero;
		};

		class Checker {
		  public:
			Checker(std::string_view file, std::vector<Problem>& problems)
				: file(file),
				  problems(problems) {}

			std::optional<SvgFacts> run(std::string_view text) {
				Scanner		scanner(text);
				Tag			tag;
				std::string error;
				bool		sawRoot = false;
				while (scanner.next(tag, error)) {
					if (tag.closing) {
						close();
						continue;
					}
					if (!sawRoot) {
						sawRoot = true;
						if (tag.name != "svg") {
							report(tag, "is the root element; an icon's root is <svg>");
							return std::nullopt;
						}
						root(tag);
					}
					open(tag);
				}
				if (!error.empty()) {
					problems.push_back({.file = std::string(file), .line = scanner.currentLine(), .message = error});
					return std::nullopt;
				}
				if (!sawRoot) {
					problems.push_back({.file = std::string(file), .message = "has no <svg> element"});
					return std::nullopt;
				}
				if (!drew) {
					problems.push_back({.file = std::string(file), .message = "draws nothing: no filled path, rect, circle or polygon"});
				}
				if (failed) {
					return std::nullopt;
				}
				return SvgFacts{.viewBoxSize = viewBoxSize, .fillRule = fillRule.value_or(FillRule::NonZero)};
			}

		  private:
			void report(const Tag& tag, std::string message) {
				problems.push_back(
					{.file = std::string(file), .line = tag.line, .element = "<" + tag.name + ">", .message = std::move(message)}
				);
				failed = true;
			}

			void root(const Tag& tag) {
				const auto viewBox = tag.attributes.find("viewBox");
				if (viewBox == tag.attributes.end()) {
					report(tag, "has no viewBox; an icon's viewBox is square");
					return;
				}
				std::array<float, 4> box{};
				const char*			 cursor = viewBox->second.c_str();
				for (float& value : box) {
					while (*cursor == ',' || std::isspace(static_cast<unsigned char>(*cursor)) != 0) {
						++cursor;
					}
					char* end = nullptr;
					value = std::strtof(cursor, &end);
					if (end == cursor) {
						report(tag, "has a viewBox that isn't four numbers");
						return;
					}
					cursor = end;
				}
				if (box[2] <= 0.0F || std::abs(box[2] - box[3]) > 1e-4F * box[2]) {
					report(
						tag, "has a viewBox " + std::to_string(box[2]) + " by " + std::to_string(box[3]) + "; an icon's viewBox is square"
					);
					return;
				}
				viewBoxSize = box[2];
			}

			void open(const Tag& tag) {
				if (skipDepth > 0 || tag.name.find(':') != std::string::npos || among(kDescriptive, tag.name)) {
					// Editor namespaces (sodipodi:, inkscape:) and descriptions draw nothing; their
					// contents aren't checked.
					if (!tag.selfClosing) {
						++skipDepth;
					}
					return;
				}
				Paint									 paint = paints.empty() ? Paint{} : paints.back();
				const std::map<std::string, std::string> properties = propertiesOf(tag);
				if (among(kStrokedShapes, tag.name)) {
					report(
						tag,
						"is not a path, rect, circle or polygon; " +
							std::string(tag.name == "ellipse" ? "make it a path" : "outline it into a filled path")
					);
				} else if (!among(kShapes, tag.name) && !among(kContainers, tag.name)) {
					report(tag, "is refused: an icon has no gradients, masks, clip paths, filters, text, images, styles or references");
				}
				for (const auto& [name, value] : properties) {
					const std::string lower = normalColor(value);
					if (name == "stroke" && lower != "none" && lower != "transparent") {
						report(tag, "has a stroke; outline it before copying the icon in");
					} else if ((name == "mask" || name == "clip-path" || name == "filter") && lower != "none") {
						report(tag, "has a " + name + "; an icon has none");
					} else if (name == "href" || name == "xlink:href") {
						report(tag, "references " + value + "; an icon has no references");
					} else if ((name == "opacity" || name == "fill-opacity") && std::strtof(value.c_str(), nullptr) < 1.0F) {
						report(tag, "has " + name + " " + value + "; an icon is one opaque colour, tinted at run time");
					} else if (name == "fill") {
						if (lower.starts_with("url(")) {
							report(tag, "fills with " + value + "; an icon has no gradients or patterns");
						}
						paint.fill = lower;
					} else if (name == "fill-rule") {
						paint.fillRule = lower == "evenodd" ? FillRule::EvenOdd : FillRule::NonZero;
					}
				}
				if (among(kShapes, tag.name) && paint.fill != "none") {
					drew = true;
					if (!color.has_value()) {
						color = paint.fill;
					} else if (*color != paint.fill) {
						report(
							tag,
							"fills " + paint.fill + " where the icon's colour is " + *color + "; an icon is one colour, tinted at run time"
						);
					}
					if (!fillRule.has_value()) {
						fillRule = paint.fillRule;
					} else if (*fillRule != paint.fillRule) {
						report(tag, "uses another fill rule than the icon's other shapes; one rule an icon");
					}
				}
				if (!tag.selfClosing) {
					paints.push_back(paint);
				}
			}

			// A closing tag; a self-closing one never opened anything.
			void close() {
				if (skipDepth > 0) {
					--skipDepth;
				} else if (!paints.empty()) {
					paints.pop_back();
				}
			}

			std::string_view		   file;
			std::vector<Problem>&	   problems;
			std::vector<Paint>		   paints;
			int						   skipDepth = 0;
			bool					   failed = false;
			bool					   drew = false;
			float					   viewBoxSize = 0.0F;
			std::optional<std::string> color;
			std::optional<FillRule>	   fillRule;
		};

	} // namespace

	std::string Problem::text() const {
		std::string out = file;
		if (line > 0) {
			out += ":" + std::to_string(line);
		}
		out += ": ";
		if (!element.empty()) {
			out += element + " ";
		}
		return out + message;
	}

	std::optional<SvgFacts> checkSvg(std::string_view file, std::string_view text, std::vector<Problem>& problems) {
		return Checker(file, problems).run(text);
	}

} // namespace ui::iconbuild
