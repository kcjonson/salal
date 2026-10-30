#include "ui/iconbuild/IconAtlasBuilder.h"

#include <icon_test/Icons.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace ui::iconbuild;

namespace {

	std::vector<Problem> problemsWith(std::string_view svg) {
		std::vector<Problem> problems;
		checkSvg("bad.svg", svg, problems);
		return problems;
	}

	// The one problem `svg` has, as the build prints it.
	std::string onlyProblem(std::string_view svg) {
		const std::vector<Problem> problems = problemsWith(svg);
		if (problems.size() != 1) {
			std::string all = std::to_string(problems.size()) + " problems:";
			for (const Problem& problem : problems) {
				all += "\n" + problem.text();
			}
			return all;
		}
		return problems[0].text();
	}

	std::string wrap(std::string_view inside, std::string_view viewBox = "0 0 24 24") {
		return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" + std::string(viewBox) + "\">\n" + std::string(inside) + "\n</svg>";
	}

	std::vector<IconSource> testIcons() {
		std::vector<IconSource> icons;
		for (const auto& entry : std::filesystem::directory_iterator(SALAL_UI_TEST_ICONS)) {
			std::ifstream	   file(entry.path(), std::ios::binary);
			std::ostringstream text;
			text << file.rdbuf();
			icons.push_back({.name = entry.path().stem().string(), .file = entry.path().filename().string(), .text = text.str()});
		}
		return icons;
	}

	// Whether the field says inside at a texel: the median of the three channels past the edge.
	bool inside(const BuiltAtlas& atlas, int x, int y) {
		const std::uint8_t* texel = &atlas.rgb[(static_cast<size_t>(y) * static_cast<size_t>(atlas.width) + static_cast<size_t>(x)) * 3];
		const int			median = std::max(std::min(texel[0], texel[1]), std::min(std::max(texel[0], texel[1]), texel[2]));
		return median > 127;
	}

} // namespace

TEST(IconRules, AFilledIconPasses) {
	for (const IconSource& icon : testIcons()) {
		std::vector<Problem> problems;
		EXPECT_TRUE(checkSvg(icon.file, icon.text, problems).has_value()) << icon.file;
		EXPECT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0].text());
	}
	std::vector<Problem>		  problems;
	const std::optional<SvgFacts> frame =
		checkSvg("frame.svg", wrap("<path fill-rule=\"evenodd\" d=\"M0 0H4V4H0Z\"/>", "0 0 48 48"), problems);
	ASSERT_TRUE(frame.has_value());
	EXPECT_EQ(frame->fillRule, FillRule::EvenOdd);
	EXPECT_FLOAT_EQ(frame->viewBoxSize, 48.0F);
}

TEST(IconRules, EachBrokenRuleNamesTheFileLineAndElement) {
	EXPECT_EQ(
		onlyProblem(wrap("<rect width=\"4\" height=\"4\"/>", "0 0 24 20")),
		"bad.svg:1: <svg> has a viewBox 24.000000 by 20.000000; an icon's viewBox is square"
	);
	EXPECT_EQ(
		onlyProblem("<svg xmlns=\"http://www.w3.org/2000/svg\"><rect width=\"4\" height=\"4\"/></svg>"),
		"bad.svg:1: <svg> has no viewBox; an icon's viewBox is square"
	);
	EXPECT_EQ(
		onlyProblem(wrap("<path d=\"M0 0H4V4Z\" stroke=\"#000\"/>")),
		"bad.svg:2: <path> has a stroke; outline it before copying the icon in"
	);
	EXPECT_EQ(
		onlyProblem(wrap("<g style=\"stroke: black; stroke-width: 2\"><path d=\"M0 0H4V4Z\"/></g>")),
		"bad.svg:2: <g> has a stroke; outline it before copying the icon in"
	);
	EXPECT_EQ(
		onlyProblem(wrap("<rect width=\"4\" height=\"4\"/>\n<line x1=\"0\" y1=\"0\" x2=\"4\" y2=\"4\"/>")),
		"bad.svg:3: <line> is not a path, rect, circle or polygon; outline it into a filled path"
	);
	EXPECT_NE(onlyProblem(wrap("<rect width=\"4\" height=\"4\"/><text>A</text>")).find("bad.svg:2: <text> is refused"), std::string::npos);
	EXPECT_NE(
		onlyProblem(wrap("<rect width=\"4\" height=\"4\" mask=\"url(#m)\"/>")).find("<rect> has a mask; an icon has none"),
		std::string::npos
	);
	EXPECT_NE(onlyProblem(wrap("<rect width=\"4\" height=\"4\" opacity=\"0.5\"/>")).find("one opaque colour"), std::string::npos);
	EXPECT_NE(
		onlyProblem(wrap("<rect width=\"4\" height=\"4\" fill=\"#ff0000\"/>\n<circle r=\"2\" fill=\"#00ff00\"/>"))
			.find("bad.svg:3: <circle> fills #00ff00 where the icon's colour is #ff0000"),
		std::string::npos
	);
	EXPECT_NE(
		onlyProblem(wrap("<rect width=\"4\" height=\"4\" fill-rule=\"evenodd\"/><circle r=\"2\"/>")).find("another fill rule"),
		std::string::npos
	);
	EXPECT_TRUE(
		problemsWith(wrap("<circle r=\"2\" fill=\"#000\"/><rect width=\"1\" height=\"1\" fill=\"black\"/>")).empty()
	); // one colour, spelt twice
	EXPECT_EQ(
		onlyProblem(wrap("<g fill=\"none\"><rect width=\"4\" height=\"4\"/></g>")),
		"bad.svg: draws nothing: no filled path, rect, circle or polygon"
	);
	EXPECT_EQ(onlyProblem("<svg viewBox=\"0 0 24 24\"><rect"), "bad.svg:1: the markup ends inside <rect>");
}

TEST(IconRules, GradientsImagesAndReferencesAreRefused) {
	const std::vector<Problem> gradient = problemsWith(wrap(
		"<defs><linearGradient id=\"g\"><stop offset=\"0\"/></linearGradient></defs>\n<rect width=\"4\" height=\"4\" fill=\"url(#g)\"/>"
	));
	ASSERT_GE(gradient.size(), 2U);
	EXPECT_EQ(gradient[0].element, "<linearGradient>");
	EXPECT_EQ(gradient.back().text(), "bad.svg:3: <rect> fills with url(#g); an icon has no gradients or patterns");
	const std::vector<Problem> image =
		problemsWith(wrap("<rect width=\"4\" height=\"4\"/><image href=\"a.png\" width=\"4\" height=\"4\"/>"));
	ASSERT_EQ(image.size(), 2U);
	EXPECT_EQ(image[0].element, "<image>");
	EXPECT_NE(image[1].message.find("references a.png"), std::string::npos);
	EXPECT_NE(onlyProblem("<!DOCTYPE svg [<!ENTITY e \"x\">]><svg viewBox=\"0 0 1 1\"/>").find("entities"), std::string::npos);
}

TEST(IconAtlas, BuildsOneCellAnIconInNameOrder) {
	std::vector<Problem>			problems;
	const std::optional<BuiltAtlas> atlas = buildAtlas(testIcons(), {.cellSize = 48, .distanceRange = 8}, problems);
	ASSERT_TRUE(atlas.has_value()) << (problems.empty() ? "" : problems[0].text());
	ASSERT_EQ(atlas->icons.size(), 4U);
	EXPECT_EQ(atlas->width, 96);
	EXPECT_EQ(atlas->height, 96);
	EXPECT_EQ(atlas->icons[0].name, "arrow-up");
	EXPECT_EQ(atlas->icons[0].codepoint, kFirstCodepoint);
	EXPECT_EQ(atlas->icons[3].name, "frame");
	EXPECT_EQ(atlas->icons[3].codepoint, kFirstCodepoint + 3);
	EXPECT_EQ(atlas->icons[3].x, 48);
	EXPECT_EQ(atlas->icons[3].y, 48);

	// The viewBox fills a cell less 4 px each side, so viewBox unit u lands at px 4 + 40 u / size.
	// corner: the top-left quarter, the right way up.
	EXPECT_TRUE(inside(*atlas, 12, 48 + 12));
	EXPECT_FALSE(inside(*atlas, 12, 48 + 36));
	EXPECT_FALSE(inside(*atlas, 36, 48 + 12));
	// frame: even-odd, so its middle is a hole.
	EXPECT_FALSE(inside(*atlas, 48 + 24, 48 + 24));
	EXPECT_TRUE(inside(*atlas, 48 + 12, 48 + 24));
	EXPECT_FALSE(inside(*atlas, 48 + 3, 48 + 24));
	// badge: a circle of radius 10 at the middle, placed by its group's transform.
	EXPECT_TRUE(inside(*atlas, 48 + 24, 24));
	EXPECT_FALSE(inside(*atlas, 48 + 24, 4));
	// arrow-up: wide at the head, narrow at the stem.
	EXPECT_TRUE(inside(*atlas, 14, 24));
	EXPECT_FALSE(inside(*atlas, 14, 38));

	const nlohmann::json json = nlohmann::json::parse(atlas->json);
	EXPECT_EQ(json["atlas"]["type"], "msdf");
	EXPECT_EQ(json["atlas"]["distanceRange"], 8);
	EXPECT_EQ(json["atlas"]["yOrigin"], "top");
	ASSERT_EQ(json["glyphs"].size(), 4U);
	EXPECT_EQ(json["glyphs"][3]["unicode"], kFirstCodepoint + 3);
	EXPECT_EQ(json["glyphs"][3]["atlasBounds"]["left"], 48);
	EXPECT_EQ(json["glyphs"][3]["atlasBounds"]["bottom"], 96);
	EXPECT_DOUBLE_EQ(json["glyphs"][3]["planeBounds"]["left"].get<double>(), -0.1);
}

TEST(IconAtlas, ABrokenIconFailsTheWholeBuild) {
	std::vector<IconSource> icons = testIcons();
	icons.push_back({.name = "with space", .file = "with space.svg", .text = wrap("<rect width=\"4\" height=\"4\"/>")});
	icons.push_back({.name = "stroked", .file = "stroked.svg", .text = wrap("<path d=\"M0 0H4\" stroke=\"red\"/>")});
	std::vector<Problem> problems;
	EXPECT_FALSE(buildAtlas(icons, {}, problems).has_value());
	ASSERT_EQ(problems.size(), 2U);
	EXPECT_NE(problems[0].text().find("stroked.svg:2: <path> has a stroke"), std::string::npos);
	EXPECT_NE(problems[1].text().find("with space.svg: has a name that isn't letters"), std::string::npos);
}

TEST(IconAtlas, ThePngAndTheGeneratedTableCarryTheAtlas) {
	std::vector<Problem>			problems;
	const std::optional<BuiltAtlas> atlas = buildAtlas(testIcons(), {}, problems);
	ASSERT_TRUE(atlas.has_value());
	const std::vector<std::uint8_t> png = encodePng(*atlas);
	int								width = 0;
	int								height = 0;
	int								channels = 0;
	stbi_set_flip_vertically_on_load(0);
	stbi_uc* decoded = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &width, &height, &channels, 3);
	ASSERT_NE(decoded, nullptr);
	EXPECT_EQ(width, atlas->width);
	EXPECT_EQ(height, atlas->height);
	EXPECT_EQ(std::memcmp(decoded, atlas->rgb.data(), atlas->rgb.size()), 0);
	stbi_image_free(decoded);

	// salal_icons() ran the same build over the same folder for this test binary.
	const ui::IconAtlasData& data = icon_test::iconAtlas();
	EXPECT_EQ(data.name, "icon_test");
	EXPECT_EQ(std::vector<std::uint8_t>(data.png.begin(), data.png.end()), png);
	std::string json;
	for (std::string_view piece : data.json) {
		json += piece;
	}
	EXPECT_EQ(json, atlas->json);
	ASSERT_EQ(data.names.size(), 4U);
	EXPECT_EQ(data.names[2].name, "corner");
	EXPECT_EQ(data.names[2].codepoint, kFirstCodepoint + 2);
}
