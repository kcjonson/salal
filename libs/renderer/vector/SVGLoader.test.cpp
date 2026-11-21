#include "vector/SVGLoader.h"

#include "vector/Tessellator.h"
#include "vector/Types.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <vector>

namespace {

	// Write `content` to a unique temp .svg, return its path. Caller removes it.
	std::filesystem::path writeTempSvg(const std::string& name, const std::string& content) {
		const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
		std::ofstream				out(p);
		out << content;
		out.close();
		return p;
	}

	renderer::LoadedSVGShape strokeShape(const std::vector<Foundation::Vec2>& verts, bool closed, float width) {
		renderer::LoadedSVGShape s;
		s.hasFill = false;
		s.hasStroke = true;
		s.strokeWidth = width;
		s.strokeColor = Foundation::Color::white();
		s.paths.emplace_back(verts, closed);
		return s;
	}

} // namespace

// A stroked straight segment flattens to 2 vertices; loadSVG must keep it (it used to drop any
// path with < 3 vertices, which silently discarded stroke-only lines and their whole shape).
TEST(SVGLoaderTest, LoadStrokeOnlyLineKeepsTwoVertexPath) {
	const std::filesystem::path svg = writeTempSvg(
		"worldsim_stroke_line.svg",
		R"(<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 20 20">
  <path d="M0,10 L20,10" stroke="#ff0000" stroke-width="2" fill="none"/>
</svg>)"
	);

	std::vector<renderer::LoadedSVGShape> shapes;
	const bool							  ok = renderer::loadSVG(svg.string(), 0.5F, shapes);
	std::filesystem::remove(svg);

	ASSERT_TRUE(ok);
	ASSERT_EQ(shapes.size(), 1U);
	EXPECT_FALSE(shapes[0].hasFill);
	EXPECT_TRUE(shapes[0].hasStroke);
	ASSERT_EQ(shapes[0].paths.size(), 1U);
	EXPECT_GE(shapes[0].paths[0].vertices.size(), 2U);
}

// The same text from memory, as salal_art() embeds it, loads as it does from a file; text nanosvg
// can't parse fails the same way.
TEST(SVGLoaderTest, BytesInMemoryLoadAsTheFileDoes) {
	const std::string					  text = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20">
  <rect x="2" y="2" width="16" height="10" fill="#336699"/>
  <circle cx="10" cy="15" r="4" fill="#993366" stroke="#000000" stroke-width="1"/>
</svg>)";
	const std::filesystem::path			  svg = writeTempSvg("salal_memory_twin.svg", text);
	std::vector<renderer::LoadedSVGShape> fromFile;
	const bool							  fileOk = renderer::loadSVG(svg.string(), 0.5F, fromFile);
	std::filesystem::remove(svg);
	std::vector<renderer::LoadedSVGShape> fromMemory;
	ASSERT_TRUE(fileOk);
	ASSERT_TRUE(renderer::loadSVG(std::as_bytes(std::span(text)), 0.5F, fromMemory, "twin"));
	ASSERT_EQ(fromMemory.size(), fromFile.size());
	for (size_t i = 0; i < fromFile.size(); ++i) {
		EXPECT_EQ(fromMemory[i].fillColor.toVec4(), fromFile[i].fillColor.toVec4());
		EXPECT_EQ(fromMemory[i].hasStroke, fromFile[i].hasStroke);
		ASSERT_EQ(fromMemory[i].paths.size(), fromFile[i].paths.size());
		EXPECT_EQ(fromMemory[i].paths[0].vertices, fromFile[i].paths[0].vertices);
	}

	const std::string empty;
	EXPECT_FALSE(renderer::loadSVG(std::as_bytes(std::span(empty)), 0.5F, fromMemory, "empty"));
	EXPECT_TRUE(fromMemory.empty());
}

// An open 2-point stroke is exactly one bevel-band quad: 4 verts, 2 triangles, no join wedges.
TEST(SVGLoaderTest, StrokeOnlyOpenSegmentEmitsOneQuad) {
	const auto				  shape = strokeShape({{0.0F, 0.0F}, {10.0F, 0.0F}}, /*closed=*/false, 2.0F);
	renderer::TessellatedMesh mesh;
	renderer::appendShapeMesh(shape, mesh);

	EXPECT_EQ(mesh.vertices.size(), 4U);
	EXPECT_EQ(mesh.indices.size(), 6U);
	EXPECT_EQ(mesh.getTriangleCount(), 2U);
	EXPECT_EQ(mesh.colors.size(), mesh.vertices.size()); // per-vertex colors stay parallel
}

// Closing the polyline adds join wedges at every corner, so it has more geometry than the open one.
TEST(SVGLoaderTest, ClosedStrokeAddsJoinsVsOpen) {
	const std::vector<Foundation::Vec2> corner = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}};

	renderer::TessellatedMesh openMesh;
	renderer::appendShapeMesh(strokeShape(corner, /*closed=*/false, 2.0F), openMesh);

	renderer::TessellatedMesh closedMesh;
	renderer::appendShapeMesh(strokeShape(corner, /*closed=*/true, 2.0F), closedMesh);

	EXPECT_GT(openMesh.vertices.size(), 8U);						 // 2 quads + at least one join wedge
	EXPECT_GT(closedMesh.vertices.size(), openMesh.vertices.size()); // extra segment + joins all the way round
}

// hasFill still drives the fill pass; a filled triangle emits at least one tessellated triangle.
TEST(SVGLoaderTest, FillEmittedWhenHasFill) {
	renderer::LoadedSVGShape shape;
	shape.hasFill = true;
	shape.fillColor = Foundation::Color::white();
	shape.paths.emplace_back(std::vector<Foundation::Vec2>{{0.0F, 0.0F}, {10.0F, 0.0F}, {5.0F, 10.0F}}, true);

	renderer::TessellatedMesh mesh;
	renderer::appendShapeMesh(shape, mesh);

	EXPECT_GE(mesh.vertices.size(), 3U);
	EXPECT_GE(mesh.getTriangleCount(), 1U);
	EXPECT_EQ(mesh.colors.size(), mesh.vertices.size());
}

namespace {

	using Foundation::AffineTransform2D;
	using Foundation::Vec2;

	// A face of an upright box seen at an angle: u along the face, v down it, as a game draws side
	// art onto a parallelogram. matrix(e.x, e.y, 0, heightRatio, 0, 0) with e = (0.8, 0.6).
	constexpr AffineTransform2D kFaceMatrix{.a = 0.8F, .b = 0.6F, .c = 0.0F, .d = 0.35F, .e = 2.0F, .f = -1.0F};

	// A face almost edge-on: its projected width is 3e-6 of its length, determinant about 1e-6.
	constexpr AffineTransform2D kEdgeOnMatrix{.a = 3e-6F, .b = 1.0F, .c = 0.0F, .d = 0.35F, .e = 0.0F, .f = 0.0F};

	renderer::GradientFill linearGradient() {
		renderer::GradientFill gradient;
		gradient.type = renderer::GradientFill::Type::Linear;
		// Gradient y' = x / 10: red at x = 0 to blue at x = 10.
		gradient.xform[0] = 0.0F;
		gradient.xform[1] = 0.1F;
		gradient.xform[3] = 0.0F;
		gradient.stops = {
			{.offset = 0.0F, .color = Foundation::Color(1.0F, 0.0F, 0.0F, 1.0F)},
			{.offset = 0.5F, .color = Foundation::Color(0.2F, 0.8F, 0.1F, 0.6F)},
			{.offset = 1.0F, .color = Foundation::Color(0.0F, 0.0F, 1.0F, 1.0F)},
		};
		return gradient;
	}

	renderer::GradientFill radialGradient() {
		renderer::GradientFill gradient = linearGradient();
		gradient.type = renderer::GradientFill::Type::Radial;
		// Centre (5, 5), radius 5.
		gradient.xform[0] = 0.2F;
		gradient.xform[1] = 0.0F;
		gradient.xform[2] = 0.0F;
		gradient.xform[3] = 0.2F;
		gradient.xform[4] = -1.0F;
		gradient.xform[5] = -1.0F;
		return gradient;
	}

	// A 10 x 10 square with a 4 x 4 hole, gradient filled and stroked: two subpaths, so it goes
	// through the tessellator's sweep.
	renderer::LoadedSVGShape squareWithHole(const renderer::GradientFill& gradient) {
		renderer::LoadedSVGShape shape;
		shape.hasFill = true;
		shape.gradient = gradient;
		shape.fillColor = gradient.stops.front().color;
		shape.hasStroke = true;
		shape.strokeWidth = 0.5F;
		shape.strokeColor = Foundation::Color(0.2F, 0.14F, 0.1F, 0.8F);
		shape.paths.emplace_back(std::vector<Vec2>{{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}}, true);
		shape.paths.emplace_back(std::vector<Vec2>{{3.0F, 3.0F}, {3.0F, 7.0F}, {7.0F, 7.0F}, {7.0F, 3.0F}}, true);
		return shape;
	}

	// One subpath, radially filled: the tessellator fans it from an inserted centroid.
	renderer::LoadedSVGShape radialPentagon() {
		renderer::LoadedSVGShape shape;
		shape.hasFill = true;
		shape.gradient = radialGradient();
		shape.fillColor = shape.gradient.stops.front().color;
		shape.paths.emplace_back(std::vector<Vec2>{{5.0F, 0.0F}, {10.0F, 4.0F}, {8.0F, 10.0F}, {2.0F, 10.0F}, {0.0F, 4.0F}}, true);
		return shape;
	}

	void expectSameMesh(const renderer::TessellatedMesh& actual, const renderer::TessellatedMesh& expected) {
		ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
		ASSERT_EQ(actual.colors.size(), expected.colors.size());
		EXPECT_EQ(actual.indices, expected.indices);
		for (size_t i = 0; i < expected.vertices.size(); ++i) {
			EXPECT_EQ(actual.vertices[i], expected.vertices[i]) << "vertex " << i;
			EXPECT_EQ(actual.colors[i].toVec4(), expected.colors[i].toVec4()) << "colour " << i;
		}
	}

	float distanceToSegment(Vec2 point, Vec2 start, Vec2 end) {
		const Vec2	along = end - start;
		const float lengthSquared = glm::dot(along, along);
		const float fraction = lengthSquared > 0.0F ? std::clamp(glm::dot(point - start, along) / lengthSquared, 0.0F, 1.0F) : 0.0F;
		return glm::length(point - (start + (along * fraction)));
	}

	// The largest distance from any of `vertices` to the polyline `centreline`.
	float farthestFromCentreline(std::span<const Vec2> vertices, const std::vector<Vec2>& centreline, bool closed) {
		float farthest = 0.0F;
		for (const Vec2& vertex : vertices) {
			float		 nearest = std::numeric_limits<float>::max();
			const size_t segments = closed ? centreline.size() : centreline.size() - 1;
			for (size_t i = 0; i < segments; ++i) {
				nearest = std::min(nearest, distanceToSegment(vertex, centreline[i], centreline[(i + 1) % centreline.size()]));
			}
			farthest = std::max(farthest, nearest);
		}
		return farthest;
	}

	std::vector<Vec2> mapped(const std::vector<Vec2>& points, const AffineTransform2D& transform) {
		std::vector<Vec2> result;
		for (const Vec2& point : points) {
			result.push_back(transform.applyToPoint(point));
		}
		return result;
	}

	bool allFinite(const renderer::TessellatedMesh& mesh) {
		for (size_t i = 0; i < mesh.vertices.size(); ++i) {
			const Foundation::Vec4 colour = mesh.colors[i].toVec4();
			if (!std::isfinite(mesh.vertices[i].x) || !std::isfinite(mesh.vertices[i].y) || !std::isfinite(colour.r) ||
				!std::isfinite(colour.g) || !std::isfinite(colour.b) || !std::isfinite(colour.a)) {
				return false;
			}
		}
		return true;
	}

} // namespace

// The two-argument call is the defaults (identity, None, brightness 1), and those leave a shape as
// it was before the options existed: the fill exactly the tessellator's triangles coloured by the
// gradient at each vertex, the stroke bands and the parts exactly the values the two-argument
// appendShapeMesh produced at v0.43.0, written out here.
TEST(SVGLoaderTest, DefaultArgsDrawTheShapeAsDrawn) {
	const auto expectStroke =
		[](const renderer::LoadedSVGShape& shape, const std::vector<Vec2>& vertices, const std::vector<uint16_t>& indices) {
			renderer::TessellatedMesh mesh;
			renderer::appendShapeMesh(shape, mesh);
			EXPECT_EQ(mesh.vertices, vertices);
			EXPECT_EQ(mesh.indices, indices);
			ASSERT_EQ(mesh.parts.size(), 1U);
			EXPECT_EQ(mesh.parts[0].vertexStart, 0U);
			EXPECT_EQ(mesh.parts[0].vertexCount, vertices.size());
		};
	// M0,0 L10,0 at stroke width 2: one quad.
	expectStroke(
		strokeShape({{0.0F, 0.0F}, {10.0F, 0.0F}}, /*closed=*/false, 2.0F),
		{{0.0F, 1.0F}, {0.0F, -1.0F}, {10.0F, 1.0F}, {10.0F, -1.0F}},
		{0, 2, 3, 0, 3, 1}
	);
	// The closed unit square at width 0.1: four quads, then four bevel wedges.
	expectStroke(
		strokeShape({{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}}, /*closed=*/true, 0.1F),
		{{0.0F, 0.0500000007F},	 {0.0F, -0.0500000007F}, {1.0F, 0.0500000007F},	 {1.0F, -0.0500000007F}, {0.949999988F, 0.0F},
		 {1.04999995F, 0.0F},	 {0.949999988F, 1.0F},	 {1.04999995F, 1.0F},	 {1.0F, 0.949999988F},	 {1.0F, 1.04999995F},
		 {0.0F, 0.949999988F},	 {0.0F, 1.04999995F},	 {0.0500000007F, 1.0F},	 {-0.0500000007F, 1.0F}, {0.0500000007F, 0.0F},
		 {-0.0500000007F, 0.0F}, {0.0F, 0.0F},			 {-0.0500000007F, 0.0F}, {0.0F, -0.0500000007F}, {1.0F, 0.0F},
		 {1.0F, -0.0500000007F}, {1.04999995F, 0.0F},	 {1.0F, 1.0F},			 {1.04999995F, 1.0F},	 {1.0F, 1.04999995F},
		 {0.0F, 1.0F},			 {0.0F, 1.04999995F},	 {-0.0500000007F, 1.0F}},
		{0, 2, 3, 0, 3, 1, 4, 6, 7, 4, 7, 5, 8, 10, 11, 8, 11, 9, 12, 14, 15, 12, 15, 13, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27}
	);
	// Each shape is one part named by its id, fill and stroke together.
	renderer::LoadedSVGShape board = squareWithHole(linearGradient());
	board.id = "board";
	renderer::LoadedSVGShape seam = strokeShape({{0.0F, 0.0F}, {10.0F, 0.0F}}, /*closed=*/false, 2.0F);
	seam.id = "seam";
	renderer::TessellatedMesh boardAndSeam;
	renderer::appendShapeMesh(board, boardAndSeam);
	renderer::appendShapeMesh(seam, boardAndSeam);
	ASSERT_EQ(boardAndSeam.parts.size(), 2U);
	EXPECT_EQ(boardAndSeam.parts[0].name, "board");
	EXPECT_EQ(boardAndSeam.parts[0].vertexStart, 0U);
	EXPECT_EQ(boardAndSeam.parts[0].vertexCount, 64U);
	EXPECT_EQ(boardAndSeam.parts[1].name, "seam");
	EXPECT_EQ(boardAndSeam.parts[1].vertexStart, 64U);
	EXPECT_EQ(boardAndSeam.parts[1].vertexCount, 4U);
	EXPECT_EQ(boardAndSeam.vertices.size(), 68U);

	for (const renderer::LoadedSVGShape& shape : {squareWithHole(linearGradient()), squareWithHole(radialGradient()), radialPentagon()}) {
		renderer::TessellatedMesh twoArgument;
		renderer::appendShapeMesh(shape, twoArgument);
		renderer::TessellatedMesh explicitDefaults;
		renderer::appendShapeMesh(
			shape,
			explicitDefaults,
			{.transform = AffineTransform2D::identity(), .vectorEffect = renderer::VectorEffect::None, .brightness = 1.0F}
		);
		expectSameMesh(explicitDefaults, twoArgument);

		renderer::TessellatedMesh	 expected;
		renderer::Tessellator		 tessellator;
		renderer::TessellatorOptions options;
		options.fanFromCentroid = shape.paths.size() == 1;
		ASSERT_TRUE(
			shape.paths.size() == 1 ? tessellator.Tessellate(shape.paths[0], expected, options)
									: tessellator.Tessellate(std::span<const renderer::VectorPath>(shape.paths), expected, options)
		);
		for (const Vec2& vertex : expected.vertices) {
			expected.colors.push_back(shape.gradient.colorAt(vertex));
		}
		// The fill comes first; the stroke bands after it are pinned above.
		ASSERT_GE(twoArgument.vertices.size(), expected.vertices.size());
		ASSERT_GE(twoArgument.indices.size(), expected.indices.size());
		renderer::TessellatedMesh fill;
		fill.vertices.assign(
			twoArgument.vertices.begin(), twoArgument.vertices.begin() + static_cast<std::ptrdiff_t>(expected.vertices.size())
		);
		fill.colors.assign(twoArgument.colors.begin(), twoArgument.colors.begin() + static_cast<std::ptrdiff_t>(expected.colors.size()));
		fill.indices.assign(
			twoArgument.indices.begin(), twoArgument.indices.begin() + static_cast<std::ptrdiff_t>(expected.indices.size())
		);
		expectSameMesh(fill, expected);
	}
}

// A mirror keeps the fill's triangles and both stroke modes keep the band's width: it scales
// nothing.
TEST(SVGLoaderTest, AMirrorKeepsTheFillAndTheStrokeWidth) {
	const AffineTransform2D	  mirror{.a = -1.0F, .b = 0.0F, .c = 0.0F, .d = 1.0F, .e = 0.0F, .f = 0.0F};
	renderer::LoadedSVGShape  shape = radialPentagon();
	renderer::TessellatedMesh plain;
	renderer::appendShapeMesh(shape, plain);
	renderer::TessellatedMesh mirrored;
	renderer::appendShapeMesh(shape, mirrored, {.transform = mirror});
	EXPECT_EQ(mirrored.getTriangleCount(), plain.getTriangleCount());

	for (const renderer::VectorEffect effect : {renderer::VectorEffect::None, renderer::VectorEffect::NonScalingStroke}) {
		// Down the line, x spans the band's width; along it, y does.
		renderer::TessellatedMesh vertical;
		renderer::appendShapeMesh(
			strokeShape({{3.0F, 0.0F}, {3.0F, 10.0F}}, false, 2.0F), vertical, {.transform = mirror, .vectorEffect = effect}
		);
		renderer::TessellatedMesh horizontal;
		renderer::appendShapeMesh(
			strokeShape({{0.0F, 3.0F}, {10.0F, 3.0F}}, false, 2.0F), horizontal, {.transform = mirror, .vectorEffect = effect}
		);
		const auto [left, right] = std::ranges::minmax(vertical.vertices | std::views::transform([](Vec2 vertex) { return vertex.x; }));
		const auto [top, bottom] = std::ranges::minmax(horizontal.vertices | std::views::transform([](Vec2 vertex) { return vertex.y; }));
		EXPECT_FLOAT_EQ(left, -4.0F);
		EXPECT_FLOAT_EQ(right, -2.0F);
		EXPECT_FLOAT_EQ(top, 2.0F);
		EXPECT_FLOAT_EQ(bottom, 4.0F);
	}
}

// With VectorEffect::None a skewed stroke is the band drawn in the shape's space and then mapped,
// vertex for vertex, joins included, as SVG transforms a stroke's outline.
TEST(SVGLoaderTest, ASkewWithoutTheVectorEffectMapsTheFinishedBand) {
	const auto				  outline = strokeShape({{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}}, /*closed=*/true, 0.1F);
	renderer::TessellatedMesh drawn;
	renderer::appendShapeMesh(outline, drawn);
	renderer::TessellatedMesh skewed;
	renderer::appendShapeMesh(outline, skewed, {.transform = kFaceMatrix});
	ASSERT_EQ(skewed.vertices.size(), drawn.vertices.size());
	EXPECT_EQ(skewed.indices, drawn.indices);
	for (size_t i = 0; i < drawn.vertices.size(); ++i) {
		EXPECT_EQ(skewed.vertices[i], kFaceMatrix.applyToPoint(drawn.vertices[i]));
	}
}

// A horizontal stroke under matrix(1, 0, 0, 0.35, 0, 0) keeps its band width with
// NonScalingStroke and is 0.35 as tall with None, as SVG's vector-effect has it.
TEST(SVGLoaderTest, NonScalingStrokeKeepsItsWidthUnderAVerticalSquash) {
	const auto squash = AffineTransform2D{.a = 1.0F, .b = 0.0F, .c = 0.0F, .d = 0.35F, .e = 0.0F, .f = 0.0F};
	const auto shape = strokeShape({{0.0F, 0.0F}, {10.0F, 0.0F}}, /*closed=*/false, 2.0F);
	auto	   bandHeight = [&](renderer::VectorEffect effect) {
		  renderer::TessellatedMesh mesh;
		  renderer::appendShapeMesh(shape, mesh, {.transform = squash, .vectorEffect = effect});
		  float top = std::numeric_limits<float>::max();
		  float bottom = std::numeric_limits<float>::lowest();
		  for (const Vec2& vertex : mesh.vertices) {
			  top = std::min(top, vertex.y);
			  bottom = std::max(bottom, vertex.y);
		  }
		  return bottom - top;
	};
	EXPECT_FLOAT_EQ(bandHeight(renderer::VectorEffect::NonScalingStroke), 2.0F);
	EXPECT_FLOAT_EQ(bandHeight(renderer::VectorEffect::None), 0.7F);
}

// On a skewed face a line along the face and a line down it both keep the stroke width measured
// perpendicular to their own direction on screen, and the joins of a closed outline stay within
// half the width of the mapped outline.
TEST(SVGLoaderTest, NonScalingStrokeKeepsItsWidthOnASkewedFace) {
	for (const std::vector<Vec2>& line : {std::vector<Vec2>{{0.0F, 0.5F}, {1.0F, 0.5F}}, std::vector<Vec2>{{0.5F, 0.0F}, {0.5F, 1.0F}}}) {
		renderer::TessellatedMesh mesh;
		renderer::appendShapeMesh(
			strokeShape(line, /*closed=*/false, 0.03F),
			mesh,
			{.transform = kFaceMatrix, .vectorEffect = renderer::VectorEffect::NonScalingStroke}
		);
		const Vec2 start = kFaceMatrix.applyToPoint(line[0]);
		const Vec2 end = kFaceMatrix.applyToPoint(line[1]);
		const Vec2 normal = glm::normalize(Vec2(-(end - start).y, (end - start).x));
		for (const Vec2& vertex : mesh.vertices) {
			EXPECT_NEAR(std::abs(glm::dot(vertex - start, normal)), 0.015F, 0.015F * 0.01F);
		}
	}

	const std::vector<Vec2>	  outline = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
	renderer::TessellatedMesh mesh;
	renderer::appendShapeMesh(
		strokeShape(outline, /*closed=*/true, 0.03F),
		mesh,
		{.transform = kFaceMatrix, .vectorEffect = renderer::VectorEffect::NonScalingStroke}
	);
	EXPECT_LE(farthestFromCentreline(mesh.vertices, mapped(outline, kFaceMatrix), /*closed=*/true), 0.015F * 1.001F);
}

// The fill is coloured where it was drawn and then mapped: each mapped vertex has its source
// vertex's colour, for a linear and a radial gradient, so the gradient is painted on the face.
TEST(SVGLoaderTest, AGradientUnderATransformKeepsItsSourceVertexColours) {
	for (renderer::LoadedSVGShape shape : {squareWithHole(linearGradient()), radialPentagon()}) {
		shape.hasStroke = false;
		renderer::TessellatedMesh source;
		renderer::appendShapeMesh(shape, source);
		renderer::TessellatedMesh onFace;
		renderer::appendShapeMesh(shape, onFace, {.transform = kFaceMatrix, .vectorEffect = renderer::VectorEffect::NonScalingStroke});
		ASSERT_EQ(onFace.vertices.size(), source.vertices.size());
		EXPECT_EQ(onFace.indices, source.indices);
		for (size_t i = 0; i < source.vertices.size(); ++i) {
			EXPECT_EQ(onFace.vertices[i], kFaceMatrix.applyToPoint(source.vertices[i]));
			EXPECT_EQ(onFace.colors[i].toVec4(), source.colors[i].toVec4());
		}
	}
}

// brightness multiplies the fill, the gradient and the stroke in RGB, clamps to 1, and leaves
// alpha alone.
TEST(SVGLoaderTest, BrightnessScalesEveryColourAndKeepsAlpha) {
	renderer::LoadedSVGShape solid;
	solid.fillColor = Foundation::Color(0.5F, 0.25F, 0.8F, 0.5F);
	solid.hasStroke = true;
	solid.strokeWidth = 1.0F;
	solid.strokeColor = Foundation::Color(0.4F, 0.6F, 0.1F, 0.7F);
	solid.paths.emplace_back(std::vector<Vec2>{{0.0F, 0.0F}, {10.0F, 0.0F}, {5.0F, 10.0F}}, true);
	renderer::TessellatedMesh dimmed;
	renderer::appendShapeMesh(solid, dimmed, {.brightness = 0.5F});
	renderer::TessellatedMesh brightened;
	renderer::appendShapeMesh(solid, brightened, {.brightness = 1.5F});
	renderer::TessellatedMesh plain;
	renderer::appendShapeMesh(solid, plain);
	ASSERT_EQ(dimmed.colors.size(), plain.colors.size());
	for (size_t i = 0; i < plain.colors.size(); ++i) {
		const Foundation::Vec4 colour = plain.colors[i].toVec4();
		EXPECT_EQ(dimmed.colors[i].toVec4(), Foundation::Vec4(colour.r * 0.5F, colour.g * 0.5F, colour.b * 0.5F, colour.a));
		const Foundation::Vec4 bright(
			std::min(colour.r * 1.5F, 1.0F), std::min(colour.g * 1.5F, 1.0F), std::min(colour.b * 1.5F, 1.0F), colour.a
		);
		EXPECT_EQ(brightened.colors[i].toVec4(), bright);
		EXPECT_EQ(dimmed.vertices[i], plain.vertices[i]);
	}

	// The gradient is evaluated, then brightened, as CSS filters the painted colour: green from 0.2
	// to 0.8 at brightness 2 is 1.0 halfway, where brightening the stops first would give 0.7.
	renderer::LoadedSVGShape gradientShape;
	gradientShape.gradient = linearGradient();
	gradientShape.gradient.stops = {
		{.offset = 0.0F, .color = Foundation::Color(0.0F, 0.2F, 0.0F, 1.0F)},
		{.offset = 1.0F, .color = Foundation::Color(0.0F, 0.8F, 0.0F, 1.0F)},
	};
	gradientShape.fillColor = gradientShape.gradient.stops.front().color;
	// The apex and the fan's centroid both sit at x = 5, halfway along the gradient.
	gradientShape.paths.emplace_back(std::vector<Vec2>{{0.0F, 0.0F}, {10.0F, 0.0F}, {5.0F, 10.0F}}, true);
	renderer::TessellatedMesh doubled;
	renderer::appendShapeMesh(gradientShape, doubled, {.brightness = 2.0F});
	bool sawHalfway = false;
	for (size_t i = 0; i < doubled.vertices.size(); ++i) {
		const Foundation::Color evaluated = gradientShape.gradient.colorAt(doubled.vertices[i]);
		EXPECT_FLOAT_EQ(doubled.colors[i].g, std::min(evaluated.g * 2.0F, 1.0F));
		if (doubled.vertices[i].x == 5.0F) {
			sawHalfway = true;
			EXPECT_FLOAT_EQ(doubled.colors[i].g, 1.0F);
		}
	}
	EXPECT_TRUE(sawHalfway);
}

class SVGLoaderDeathTest : public testing::Test {
  protected:
	void SetUp() override {
#ifdef NDEBUG
		GTEST_SKIP() << "asserts are compiled out";
#endif
	}
};

// brightness is a finite number of at least 0; a debug build stops on anything else.
TEST_F(SVGLoaderDeathTest, BrightnessMustBeFiniteAndNotNegative) {
	const auto				  shape = strokeShape({{0.0F, 0.0F}, {10.0F, 0.0F}}, /*closed=*/false, 2.0F);
	renderer::TessellatedMesh mesh;
	EXPECT_DEATH(renderer::appendShapeMesh(shape, mesh, {.brightness = -1.0F}), "brightness must be a finite number");
	EXPECT_DEATH(renderer::appendShapeMesh(shape, mesh, {.brightness = std::numeric_limits<float>::quiet_NaN()}), "brightness must be");
	EXPECT_DEATH(renderer::appendShapeMesh(shape, mesh, {.brightness = std::numeric_limits<float>::infinity()}), "brightness must be");
}

// Seen almost edge-on (determinant about 1e-6), a filled, stroked outline and an open zigzag with
// sharp joins come out finite, every stroke vertex within half the width of the mapped centreline
// with NonScalingStroke, and the fill inside the mapped outline's box. With None the band is
// mapped as drawn, so it collapses with the face.
TEST(SVGLoaderTest, ANearlyEdgeOnFaceGivesNoNansAndNoExplodedBands) {
	const std::vector<Vec2>	 outline = {{0.0F, 0.0F}, {0.5F, 0.0F}, {0.5F, 1.0F}, {0.0F, 1.0F}};
	const std::vector<Vec2>	 zigzag = {{0.0F, 0.0F}, {0.5F, 0.02F}, {0.0F, 0.04F}, {0.5F, 0.06F}, {0.25F, 0.5F}};
	renderer::LoadedSVGShape filled = strokeShape(outline, /*closed=*/true, 0.03F);
	filled.hasFill = true;
	filled.gradient = linearGradient();
	filled.fillColor = filled.gradient.stops.front().color;

	for (const AffineTransform2D& transform : {kEdgeOnMatrix, AffineTransform2D{.a = 0.02F, .b = 1.0F, .c = 0.0F, .d = 0.35F}}) {
		renderer::TessellatedMesh fillAndStroke;
		renderer::appendShapeMesh(
			filled, fillAndStroke, {.transform = transform, .vectorEffect = renderer::VectorEffect::NonScalingStroke}
		);
		ASSERT_TRUE(allFinite(fillAndStroke));
		renderer::LoadedSVGShape fillOnly = filled;
		fillOnly.hasStroke = false;
		renderer::TessellatedMesh fill;
		renderer::appendShapeMesh(fillOnly, fill, {.transform = transform});
		ASSERT_GT(fill.vertices.size(), 0U);
		const std::vector<Vec2> mappedOutline = mapped(outline, transform);
		for (const Vec2& vertex : fill.vertices) {
			EXPECT_GE(vertex.x, -1e-6F);
			EXPECT_LE(vertex.x, mappedOutline[1].x + 1e-6F);
			EXPECT_GE(vertex.y, -1e-6F);
			EXPECT_LE(vertex.y, mappedOutline[2].y + 1e-6F);
		}
		const std::span<const Vec2> outlineBand = std::span(fillAndStroke.vertices).subspan(fill.vertices.size());
		ASSERT_FALSE(outlineBand.empty());
		EXPECT_LE(farthestFromCentreline(outlineBand, mappedOutline, /*closed=*/true), 0.015F * 1.001F);

		renderer::TessellatedMesh zigzagBand;
		renderer::appendShapeMesh(
			strokeShape(zigzag, /*closed=*/false, 0.03F),
			zigzagBand,
			{.transform = transform, .vectorEffect = renderer::VectorEffect::NonScalingStroke}
		);
		ASSERT_TRUE(allFinite(zigzagBand));
		ASSERT_FALSE(zigzagBand.vertices.empty());
		EXPECT_LE(farthestFromCentreline(zigzagBand.vertices, mapped(zigzag, transform), /*closed=*/false), 0.015F * 1.001F);

		renderer::TessellatedMesh scaledBand;
		renderer::appendShapeMesh(strokeShape(zigzag, /*closed=*/false, 0.03F), scaledBand, {.transform = transform});
		ASSERT_TRUE(allFinite(scaledBand));
		for (const Vec2& vertex : scaledBand.vertices) {
			EXPECT_LE(std::abs(vertex.x), 0.6F * transform.a);
		}
	}
}

// Geometry that would take the mesh past 16-bit indices is dropped whole, the fill and each stroke
// band, under any transform and either vector effect, and the mesh is left as it was.
TEST(SVGLoaderTest, GeometryPastTheVertexLimitIsDroppedUnderATransform) {
	renderer::LoadedSVGShape shape = strokeShape({{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}}, /*closed=*/true, 0.1F);
	shape.hasFill = true;
	shape.fillColor = Foundation::Color::white();
	for (const renderer::VectorEffect effect : {renderer::VectorEffect::None, renderer::VectorEffect::NonScalingStroke}) {
		renderer::TessellatedMesh mesh;
		mesh.vertices.assign(65534, Vec2(0.0F, 0.0F));
		mesh.colors.assign(65534, Foundation::Color::white());
		renderer::appendShapeMesh(shape, mesh, {.transform = kFaceMatrix, .vectorEffect = effect, .brightness = 0.8F});
		EXPECT_EQ(mesh.vertices.size(), 65534U);
		EXPECT_EQ(mesh.colors.size(), 65534U);
		EXPECT_TRUE(mesh.indices.empty());
		EXPECT_TRUE(mesh.parts.empty());
	}
}
