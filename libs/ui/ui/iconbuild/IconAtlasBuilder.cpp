#include "ui/iconbuild/IconAtlasBuilder.h"

#include <msdfgen.h>
#include <nanosvg.h>
#include <nlohmann/json.hpp>

// Static, so the renderer's own stb_image_write in the same executable doesn't collide.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>

namespace ui::iconbuild {

	namespace {

		bool validName(const std::string& name) {
			return !name.empty() &&
				   std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) != 0 || c == '-' || c == '_'; });
		}

		// The icon's filled outlines in em units, y up, one contour a path. nanosvg has flattened
		// transforms and turned rects, circles and polygons into cubic paths.
		std::optional<msdfgen::Shape> shapeOf(const IconSource& icon, std::vector<Problem>& problems) {
			std::string												buffer = icon.text; // nanosvg parses in place
			const std::unique_ptr<NSVGimage, decltype(&nsvgDelete)> image(nsvgParse(buffer.data(), "px", 96.0F), &nsvgDelete);
			if (image == nullptr || image->width <= 0.0F) {
				problems.push_back({.file = icon.file, .message = "can't be parsed as an SVG"});
				return std::nullopt;
			}
			if (std::abs(image->width - image->height) > 1e-3F * image->width) {
				problems.push_back(
					{.file = icon.file, .message = "has a width and height that aren't square; an icon draws into a square"}
				);
				return std::nullopt;
			}
			const double size = image->width;
			const auto	 point = [&](const float* xy) {
				  return msdfgen::Point2(xy[0] / size, 1.0 - xy[1] / size);
			};
			msdfgen::Shape shape;
			for (const NSVGshape* svgShape = image->shapes; svgShape != nullptr; svgShape = svgShape->next) {
				if ((svgShape->flags & NSVG_FLAGS_VISIBLE) == 0 || svgShape->fill.type != NSVG_PAINT_COLOR) {
					continue;
				}
				for (const NSVGpath* path = svgShape->paths; path != nullptr; path = path->next) {
					msdfgen::Contour& contour = shape.addContour();
					for (int i = 0; i + 3 < path->npts; i += 3) {
						const float*		  p = &path->pts[static_cast<ptrdiff_t>(i) * 2];
						const msdfgen::Point2 p0 = point(p);
						const msdfgen::Point2 c0 = point(p + 2);
						const msdfgen::Point2 c1 = point(p + 4);
						const msdfgen::Point2 p1 = point(p + 6);
						if (p0 == p1 && c0 == p0 && c1 == p1) {
							continue;
						}
						if (c0 == p0 && c1 == p1) {
							contour.addEdge(msdfgen::EdgeHolder(p0, p1));
						} else {
							contour.addEdge(msdfgen::EdgeHolder(p0, c0, c1, p1));
						}
					}
					// A fill closes every path.
					if (path->npts >= 2) {
						const msdfgen::Point2 first = point(path->pts);
						const msdfgen::Point2 last = point(&path->pts[static_cast<ptrdiff_t>(path->npts - 1) * 2]);
						if (!(first == last)) {
							contour.addEdge(msdfgen::EdgeHolder(last, first));
						}
					}
					if (contour.edges.empty()) {
						shape.contours.pop_back();
					}
				}
			}
			if (shape.contours.empty()) {
				problems.push_back({.file = icon.file, .message = "draws nothing once parsed"});
				return std::nullopt;
			}
			shape.normalize();
			return shape;
		}

		// One icon's cell: the field generated with overlapping shapes allowed, its signs then
		// fixed by a scanline pass under the icon's fill rule, then corrected, as msdfgen's own
		// tool does for SVGs it can't preprocess.
		void renderCell(msdfgen::Shape& shape, FillRule rule, const AtlasOptions& options, BuiltAtlas& atlas, int cellX, int cellY) {
			const double emPx = options.cellSize - options.distanceRange;
			const double pad = options.distanceRange * 0.5;
			msdfgen::edgeColoringSimple(shape, 3.0);
			msdfgen::Bitmap<float, 3>	 field(options.cellSize, options.cellSize);
			const msdfgen::Projection	 projection(msdfgen::Vector2(emPx), msdfgen::Vector2(pad / emPx));
			const msdfgen::Range		 range(options.distanceRange / emPx);
			msdfgen::MSDFGeneratorConfig generate(true, msdfgen::ErrorCorrectionConfig(msdfgen::ErrorCorrectionConfig::DISABLED));
			msdfgen::generateMSDF(field, shape, projection, range, generate);
			msdfgen::distanceSignCorrection(
				field, shape, projection, rule == FillRule::EvenOdd ? msdfgen::FILL_ODD : msdfgen::FILL_NONZERO
			);
			msdfgen::msdfErrorCorrection(field, shape, projection, range, msdfgen::MSDFGeneratorConfig(true));
			for (int row = 0; row < options.cellSize; ++row) {
				const int sourceRow = options.cellSize - 1 - row; // msdfgen's rows go up
				for (int column = 0; column < options.cellSize; ++column) {
					const float* texel = field(column, sourceRow);
					const size_t at =
						(static_cast<size_t>(cellY + row) * static_cast<size_t>(atlas.width) + static_cast<size_t>(cellX + column)) * 3;
					for (int channel = 0; channel < 3; ++channel) {
						atlas.rgb[at + static_cast<size_t>(channel)] =
							static_cast<std::uint8_t>(std::clamp(256.0F * texel[channel], 0.0F, 255.0F));
					}
				}
			}
		}

		std::string jsonOf(const BuiltAtlas& atlas, const AtlasOptions& options) {
			const double		   emPx = options.cellSize - options.distanceRange;
			const double		   pad = options.distanceRange * 0.5 / emPx;
			nlohmann::ordered_json glyphs = nlohmann::ordered_json::array();
			for (const BuiltIcon& icon : atlas.icons) {
				glyphs.push_back({
					{"unicode", icon.codepoint},
					{"advance", 1},
					{"planeBounds", {{"left", -pad}, {"bottom", -pad}, {"right", 1.0 + pad}, {"top", 1.0 + pad}}},
					{"atlasBounds",
					 {{"left", icon.x}, {"bottom", icon.y + options.cellSize}, {"right", icon.x + options.cellSize}, {"top", icon.y}}},
				});
			}
			const nlohmann::ordered_json json = {
				{"atlas",
				 {{"type", "msdf"},
				  {"distanceRange", options.distanceRange},
				  {"distanceRangeMiddle", 0},
				  {"size", emPx},
				  {"width", atlas.width},
				  {"height", atlas.height},
				  {"yOrigin", "top"}}},
				{"metrics",
				 {{"emSize", 1}, {"lineHeight", 1}, {"ascender", 1}, {"descender", 0}, {"underlineY", 0}, {"underlineThickness", 0}}},
				{"glyphs", glyphs},
				{"kerning", nlohmann::ordered_json::array()},
			};
			return json.dump(1, '\t') + "\n";
		}

		std::string cppString(std::string_view text) {
			std::string out = "\"";
			for (char c : text) {
				if (c == '"' || c == '\\') {
					out += '\\';
				}
				out += c;
			}
			return out + "\"";
		}

	} // namespace

	std::optional<BuiltAtlas> buildAtlas(std::vector<IconSource> icons, const AtlasOptions& options, std::vector<Problem>& problems) {
		const size_t problemsBefore = problems.size();
		if (icons.empty()) {
			problems.push_back({.message = "the icons folder has no SVGs"});
			return std::nullopt;
		}
		if (options.cellSize <= options.distanceRange || options.distanceRange <= 0) {
			problems.push_back({.message = "the cell size must be larger than the distance range, and the range above 0"});
			return std::nullopt;
		}
		std::sort(icons.begin(), icons.end(), [](const IconSource& a, const IconSource& b) { return a.name < b.name; });

		struct Ready {
			msdfgen::Shape shape;
			FillRule	   rule;
		};
		std::vector<Ready> ready;
		for (const IconSource& icon : icons) {
			if (!validName(icon.name)) {
				problems.push_back(
					{.file = icon.file, .message = "has a name that isn't letters, digits, - and _; code names an icon by it"}
				);
				continue;
			}
			const std::optional<SvgFacts> facts = checkSvg(icon.file, icon.text, problems);
			if (!facts.has_value()) {
				continue;
			}
			std::optional<msdfgen::Shape> shape = shapeOf(icon, problems);
			if (shape.has_value()) {
				ready.push_back({.shape = std::move(*shape), .rule = facts->fillRule});
			}
		}
		if (problems.size() != problemsBefore) {
			return std::nullopt;
		}

		BuiltAtlas atlas;
		const int  columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(icons.size()))));
		const int  rows = (static_cast<int>(icons.size()) + columns - 1) / columns;
		atlas.width = columns * options.cellSize;
		atlas.height = rows * options.cellSize;
		atlas.rgb.assign(static_cast<size_t>(atlas.width) * static_cast<size_t>(atlas.height) * 3, 0);
		for (size_t i = 0; i < icons.size(); ++i) {
			const int x = static_cast<int>(i) % columns * options.cellSize;
			const int y = static_cast<int>(i) / columns * options.cellSize;
			atlas.icons.push_back({.name = icons[i].name, .codepoint = kFirstCodepoint + static_cast<std::uint32_t>(i), .x = x, .y = y});
			renderCell(ready[i].shape, ready[i].rule, options, atlas, x, y);
		}
		atlas.json = jsonOf(atlas, options);
		return atlas;
	}

	std::vector<std::uint8_t> encodePng(const BuiltAtlas& atlas) {
		std::vector<std::uint8_t> png;
		const auto				  append = [](void* context, void* data, int size) {
			   auto* out = static_cast<std::vector<std::uint8_t>*>(context);
			   out->insert(out->end(), static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + size);
		};
		stbi_write_png_to_func(append, &png, atlas.width, atlas.height, 3, atlas.rgb.data(), atlas.width * 3);
		return png;
	}

	IconSources generateIconSources(
		const BuiltAtlas&			  atlas,
		std::span<const std::uint8_t> png,
		const std::string&			  nameSpace,
		const std::string&			  header,
		const std::string&			  origin
	) {
		const std::string banner = "// Generated by salal_icons() from " + origin + ". Do not edit.\n";
		IconSources		  out;
		out.header = "#pragma once\n\n" + banner + "\n#include <ui/icons/IconAtlasData.h>\n\n";
		out.header += "namespace " + nameSpace + "::icons_detail {\n\textern const ::ui::IconAtlasData kAtlas;\n}\n\n";
		out.header += "namespace " + nameSpace + " {\n\t// The atlas, its JSON and the name table, for the icon service.\n";
		out.header += "\tinline const ::ui::IconAtlasData& iconAtlas() {\n\t\treturn icons_detail::kAtlas;\n\t}\n}\n";

		out.source = banner + "\n#include \"" + header + "\"\n\n#include <array>\n#include <string_view>\n\n";
		out.source += "namespace " + nameSpace + "::icons_detail {\n\tnamespace {\n";
		out.source += "\t\tconstexpr std::array<unsigned char, " + std::to_string(png.size()) + "> kPng{{";
		for (size_t i = 0; i < png.size(); ++i) {
			out.source += (i % 24 == 0 ? "\n\t\t\t" : " ") + std::to_string(png[i]) + ",";
		}
		out.source += "\n\t\t}};\n";
		// The JSON is digits, names and punctuation; its pieces break at lines.
		std::vector<std::string> pieces;
		for (size_t start = 0; start < atlas.json.size();) {
			size_t		 end = std::min(start + 12000, atlas.json.size());
			const size_t newline = atlas.json.find('\n', end);
			end = newline == std::string::npos ? atlas.json.size() : newline + 1;
			pieces.push_back(atlas.json.substr(start, end - start));
			start = end;
		}
		out.source += "\t\tconstexpr std::array<std::string_view, " + std::to_string(pieces.size()) + "> kJson{{\n";
		for (const std::string& piece : pieces) {
			out.source += "\t\t\tR\"icons(" + piece + ")icons\",\n";
		}
		out.source += "\t\t}};\n\t\tconstexpr std::array<::ui::IconName, " + std::to_string(atlas.icons.size()) + "> kNames{{\n";
		for (const BuiltIcon& icon : atlas.icons) {
			out.source += "\t\t\t{.name = " + cppString(icon.name) + ", .codepoint = " + std::to_string(icon.codepoint) + "},\n";
		}
		out.source += "\t\t}};\n\t} // namespace\n\n\tconst ::ui::IconAtlasData kAtlas{\n";
		out.source += "\t\t.name = " + cppString(nameSpace) + ",\n\t\t.png = kPng,\n\t\t.json = kJson,\n\t\t.names = kNames,\n\t};\n}\n";
		return out;
	}

} // namespace ui::iconbuild
