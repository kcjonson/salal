#include "vector/ArtBuild.h"

#include "generated/TestArt.h"
#include "vector/SVGLoader.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

	using renderer::svg::Problem;

	std::string all(const std::vector<Problem>& problems) {
		std::string text;
		for (const Problem& problem : problems) {
			text += problem.text() + "\n";
		}
		return text;
	}

	bool reports(const std::vector<Problem>& problems, int line, std::string_view element, std::string_view message) {
		return std::any_of(problems.begin(), problems.end(), [&](const Problem& problem) {
			return problem.line == line && problem.element == element && problem.message.find(message) != std::string::npos;
		});
	}

} // namespace

// What loadSVG reads passes: shapes in groups, colours, a gradient (and one referring to another), a
// stroke, an editor's namespaced elements and a description, skipped with what's inside them.
TEST(ArtBuildTest, ShapesGroupsAndGradientsPass) {
	std::vector<Problem> problems;
	EXPECT_TRUE(
		renderer::svg::checkArt(
			"ok.svg",
			R"svg(<?xml version="1.0"?>
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 20 20">
  <title>A plate</title>
  <sodipodi:namedview><inkscape:grid/></sodipodi:namedview>
  <defs>
    <linearGradient id="base"><stop offset="0" stop-color="#fff"/><stop offset="1" stop-color="#000"/></linearGradient>
    <radialGradient id="glow" xlink:href="#base" cx="10" cy="10" r="8"/>
  </defs>
  <g transform="translate(1 1)" opacity="0.8">
    <path d="M0 0 L10 0 L10 10 Z" fill="url(#glow)"/>
    <ellipse cx="5" cy="15" rx="4" ry="2" style="fill: #c84; stroke: #321; stroke-width: 1"/>
    <polyline points="0,0 5,5 10,0" fill="none" stroke="url( #base )"/>
  </g>
</svg>)svg",
			problems
		)
	) << all(problems);
	EXPECT_TRUE(problems.empty());
}

// What nanosvg drops, or would draw as plain geometry, fails, each by its line and element.
TEST(ArtBuildTest, WhatNanosvgDropsFailsNamingTheLineAndElement) {
	std::vector<Problem> problems;
	EXPECT_FALSE(
		renderer::svg::checkArt(
			"plate.svg",
			R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20">
  <style>.a { fill: red }</style>
  <mask id="m"><rect width="20" height="20" fill="#fff"/></mask>
  <rect width="20" height="20" mask="url(#m)" clip-path="url(#c)"/>
  <text x="2" y="10">Hi</text>
  <use href="#m"/>
  <path d="M0 0 L5 5" stroke="#000" stroke-dasharray="2 1"/>
  <circle r="3" fill="url(#nowhere)"/>
  <circle r="3" fill="url(other.svg#paint)"/>
</svg>)svg",
			problems
		)
	);
	EXPECT_TRUE(reports(problems, 2, "<style>", "is not drawn by loadSVG")) << all(problems);
	EXPECT_TRUE(reports(problems, 3, "<mask>", "is not drawn by loadSVG")) << all(problems);
	EXPECT_TRUE(reports(problems, 4, "<rect>", "has a mask")) << all(problems);
	EXPECT_TRUE(reports(problems, 4, "<rect>", "has a clip-path")) << all(problems);
	EXPECT_TRUE(reports(problems, 5, "<text>", "is not drawn by loadSVG")) << all(problems);
	EXPECT_TRUE(reports(problems, 6, "<use>", "references #m")) << all(problems);
	EXPECT_TRUE(reports(problems, 7, "<path>", "has a stroke-dasharray")) << all(problems);
	EXPECT_TRUE(reports(problems, 8, "<circle>", "names no gradient in the file")) << all(problems);
	EXPECT_TRUE(reports(problems, 9, "<circle>", "no references outside the file")) << all(problems);
	EXPECT_EQ(problems.front().text(), "plate.svg:2: <style> " + problems.front().message);

	problems.clear();
	EXPECT_FALSE(renderer::svg::checkArt("empty.svg", R"(<svg viewBox="0 0 1 1"><g/></svg>)", problems));
	EXPECT_EQ(problems.size(), 1U);
	EXPECT_NE(problems[0].message.find("draws nothing"), std::string::npos);
}

// salal_art() on tests/art: each file's bytes by name, which loadSVG reads from memory.
TEST(ArtBuildTest, AnEmbeddedFileLoadsByItsName) {
	ASSERT_EQ(test_art::artNames().size(), 1U);
	EXPECT_EQ(test_art::artNames()[0], "badge");
	EXPECT_TRUE(test_art::art("nothing").empty());

	std::vector<renderer::LoadedSVGShape> shapes;
	ASSERT_TRUE(renderer::loadSVG(test_art::art("badge"), 0.5F, shapes, "badge"));
	ASSERT_EQ(shapes.size(), 2U);
	EXPECT_EQ(shapes[0].gradient.type, renderer::GradientFill::Type::Linear);
	EXPECT_TRUE(shapes[0].hasStroke);
	EXPECT_EQ(shapes[0].width, 40.0F);
}
