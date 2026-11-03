#include "vector/ArtBuild.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>

namespace renderer::svg {

	namespace {

		constexpr std::array<std::string_view, 7> kShapes{"path", "rect", "circle", "ellipse", "line", "polyline", "polygon"};
		constexpr std::array<std::string_view, 3> kContainers{"svg", "g", "defs"};
		constexpr std::array<std::string_view, 3> kPaints{"linearGradient", "radialGradient", "stop"};
		constexpr std::array<std::string_view, 3> kDescriptive{"title", "desc", "metadata"};

		template <size_t N>
		bool among(const std::array<std::string_view, N>& names, std::string_view name) {
			return std::find(names.begin(), names.end(), name) != names.end();
		}

		std::string lowered(std::string text) {
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; }), text.end());
			return text;
		}

		// A file name as a C++ string literal.
		std::string quoted(std::string_view text) {
			std::string out = "\"";
			for (const char c : text) {
				if (c == '"' || c == '\\') {
					out += '\\';
				}
				out += c;
			}
			return out + "\"";
		}

		// A url(#id) paint, checked once the file's gradients are all known.
		struct PaintReference {
			std::string id;
			std::string value;
			Tag			tag;
		};

		class ArtChecker {
		  public:
			ArtChecker(std::string_view file, std::vector<Problem>& problems)
				: file(file),
				  problems(problems) {}

			bool run(std::string_view text) {
				Scanner		scanner(text);
				Tag			tag;
				std::string error;
				bool		sawRoot = false;
				while (scanner.next(tag, error)) {
					if (tag.closing) {
						skipDepth = std::max(skipDepth - 1, 0);
						continue;
					}
					if (!sawRoot) {
						sawRoot = true;
						if (tag.name != "svg") {
							report(tag, "is the root element; art's root is <svg>");
							return false;
						}
					}
					open(tag);
				}
				if (!error.empty()) {
					problems.push_back({.file = std::string(file), .line = scanner.currentLine(), .message = error});
					return false;
				}
				if (!sawRoot) {
					problems.push_back({.file = std::string(file), .message = "has no <svg> element"});
					return false;
				}
				if (!drew) {
					problems.push_back({.file = std::string(file), .message = "draws nothing: no path or basic shape"});
					failed = true;
				}
				for (const PaintReference& paint : paints) {
					if (!gradients.contains(paint.id)) {
						report(
							paint.tag,
							"paints with " + paint.value + ", which names no gradient in the file; art is painted in colours or gradients"
						);
					}
				}
				return !failed;
			}

		  private:
			void report(const Tag& tag, std::string message) {
				problems.push_back(
					{.file = std::string(file), .line = tag.line, .element = "<" + tag.name + ">", .message = std::move(message)}
				);
				failed = true;
			}

			// Skipped elements (an editor's namespaced ones and descriptions) count their depth so
			// their contents are skipped with them; a self-closing one opens nothing.
			void open(const Tag& tag) {
				if (skipDepth > 0 || tag.name.find(':') != std::string::npos || among(kDescriptive, tag.name)) {
					if (!tag.selfClosing) {
						++skipDepth;
					}
					return;
				}
				const bool gradient = tag.name == "linearGradient" || tag.name == "radialGradient";
				if (among(kShapes, tag.name)) {
					drew = true;
				} else if (!among(kContainers, tag.name) && !among(kPaints, tag.name)) {
					report(
						tag,
						"is not drawn by loadSVG: art is paths and basic shapes in groups, painted in colours or gradients, with no "
						"filters, masks, clip paths, text, images, patterns, markers, <use>, <symbol> or <style>"
					);
				}
				if (gradient && tag.attributes.contains("id")) {
					gradients.insert(tag.attributes.at("id"));
				}
				for (const auto& [name, value] : propertiesOf(tag)) {
					const std::string lower = lowered(value);
					if ((name == "filter" || name == "mask" || name == "clip-path" || name.starts_with("marker")) && lower != "none") {
						report(tag, "has a " + name + ", which loadSVG doesn't draw");
					} else if (name == "stroke-dasharray" && lower != "none") {
						report(
							tag,
							"has a stroke-dasharray; loadSVG strokes solid, so outline the dashes or draw them with the renderer's "
							"DashPattern"
						);
					} else if ((name == "href" || name == "xlink:href") && !gradient) {
						report(tag, "references " + value + "; art has no references but one gradient's to another");
					} else if ((name == "fill" || name == "stroke") && lower.starts_with("url(")) {
						if (lower.starts_with("url(#") && lower.ends_with(")")) {
							const size_t	  open = value.find('#');
							const std::string id = value.substr(open + 1, value.rfind(')') - open - 1);
							paints.push_back({.id = id.substr(0, id.find_last_not_of(" \t\r\n") + 1), .value = value, .tag = tag});
						} else {
							report(tag, "paints with " + value + "; art has no references outside the file");
						}
					}
				}
			}

			std::string_view			file;
			std::vector<Problem>&		problems;
			std::set<std::string>		gradients;
			std::vector<PaintReference> paints;
			int							skipDepth = 0;
			bool						failed = false;
			bool						drew = false;
		};

	} // namespace

	bool checkArt(std::string_view file, std::string_view text, std::vector<Problem>& problems) {
		return ArtChecker(file, problems).run(text);
	}

	ArtSources
	generateArtSources(std::span<const ArtFile> files, const std::string& nameSpace, const std::string& header, const std::string& origin) {
		std::vector<const ArtFile*> sorted;
		for (const ArtFile& file : files) {
			sorted.push_back(&file);
		}
		std::sort(sorted.begin(), sorted.end(), [](const ArtFile* a, const ArtFile* b) { return a->name < b->name; });

		const std::string banner = "// Generated by salal_art() from " + origin + ". Do not edit.\n";
		ArtSources		  out;
		out.header = "#pragma once\n\n" + banner + "\n#include <cstddef>\n#include <span>\n#include <string_view>\n\n";
		out.header += "namespace " + nameSpace + " {\n";
		out.header += "\t// An embedded SVG's bytes, by its file name less .svg, for renderer::loadSVG; empty for a name the\n";
		out.header += "\t// folder didn't have.\n\tstd::span<const std::byte> art(std::string_view name);\n\n";
		out.header += "\t// Every name art() answers, sorted.\n\tstd::span<const std::string_view> artNames();\n}\n";

		out.source = banner + "\n#include \"" + header + "\"\n\n#include <array>\n\n";
		out.source += "namespace " + nameSpace + " {\n\n\tnamespace {\n";
		for (size_t i = 0; i < sorted.size(); ++i) {
			const std::string& text = sorted[i]->text;
			out.source += "\t\t// " + sorted[i]->name + ".svg\n";
			out.source += "\t\tconstexpr std::array<unsigned char, " + std::to_string(text.size()) + "> kFile" + std::to_string(i) + "{{";
			for (size_t b = 0; b < text.size(); ++b) {
				out.source += (b % 24 == 0 ? "\n\t\t\t" : " ") + std::to_string(static_cast<unsigned char>(text[b])) + ",";
			}
			out.source += "\n\t\t}};\n";
		}
		out.source += "\t\tconstexpr std::array<std::string_view, " + std::to_string(sorted.size()) + "> kNames{{";
		for (const ArtFile* file : sorted) {
			out.source += "\n\t\t\t" + quoted(file->name) + ",";
		}
		out.source += "\n\t\t}};\n";
		out.source += "\t\tconstexpr std::array<std::span<const unsigned char>, " + std::to_string(sorted.size()) + "> kFiles{{";
		for (size_t i = 0; i < sorted.size(); ++i) {
			out.source += "\n\t\t\tkFile" + std::to_string(i) + ",";
		}
		out.source += "\n\t\t}};\n\t} // namespace\n\n";
		out.source += "\tstd::span<const std::byte> art(std::string_view name) {\n";
		out.source += "\t\tfor (size_t i = 0; i < kNames.size(); ++i) {\n\t\t\tif (kNames[i] == name) {\n";
		out.source += "\t\t\t\treturn std::as_bytes(kFiles[i]);\n\t\t\t}\n\t\t}\n\t\treturn {};\n\t}\n\n";
		out.source += "\tstd::span<const std::string_view> artNames() {\n\t\treturn kNames;\n\t}\n\n}\n";
		return out;
	}

} // namespace renderer::svg
