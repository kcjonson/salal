#include "primitives/BoundaryLoops.h"
#include "vector/Tessellator.h"
#include "vector/Types.h"
#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <gtest/gtest.h>
#include <numbers>
#include <vector>

using namespace Renderer;

namespace {

	constexpr float kPi = std::numbers::pi_v<float>;

	const std::array<Foundation::Vec2, 4> kSquare = {{{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}}};

	std::vector<uint16_t> loop(const BoundaryLoops& loops, size_t k) {
		return {loops.vertices.begin() + loops.starts[k], loops.vertices.begin() + loops.starts[k + 1]};
	}

	renderer::TessellatedMesh tessellate(const std::vector<Foundation::Vec2>& outline) {
		renderer::TessellatedMesh mesh;
		renderer::Tessellator	  tessellator;
		EXPECT_TRUE(tessellator.Tessellate(renderer::VectorPath(outline), mesh));
		return mesh;
	}

	// Twice the loop's signed area: positive keeps the mesh on its left.
	float twiceArea(const BoundaryLoops& loops, size_t k, const std::vector<Foundation::Vec2>& vertices) {
		const std::vector<uint16_t> indices = loop(loops, k);
		float						area = 0.0F;
		for (size_t i = 0; i < indices.size(); ++i) {
			const Foundation::Vec2 a = vertices[indices[i]];
			const Foundation::Vec2 b = vertices[indices[(i + 1) % indices.size()]];
			area += (a.x * b.y) - (a.y * b.x);
		}
		return area;
	}

} // namespace

TEST(BoundaryLoopsTest, ASquaresTwoTrianglesHaveOneLoopRoundItsCorners) {
	BoundaryLoops				  loops;
	const std::array<uint16_t, 6> indices = {0, 1, 2, 0, 2, 3};
	ASSERT_TRUE(findBoundaryLoops(kSquare, indices, loops));
	ASSERT_EQ(loops.count(), 1U);
	EXPECT_EQ(loops.starts, (std::vector<uint32_t>{0, 4}));
	EXPECT_EQ(loop(loops, 0), (std::vector<uint16_t>{0, 1, 2, 3}));
}

TEST(BoundaryLoopsTest, ALoopKeepsTheMeshOnItsLeftWhateverWindingItsTrianglesHave) {
	BoundaryLoops				  loops;
	const std::array<uint16_t, 6> reversed = {0, 2, 1, 0, 3, 2};
	ASSERT_TRUE(findBoundaryLoops(kSquare, reversed, loops));
	EXPECT_EQ(loop(loops, 0), (std::vector<uint16_t>{0, 1, 2, 3}));

	const std::array<uint16_t, 6> mixed = {0, 1, 2, 0, 3, 2};
	ASSERT_TRUE(findBoundaryLoops(kSquare, mixed, loops));
	EXPECT_EQ(loop(loops, 0), (std::vector<uint16_t>{0, 1, 2, 3}));
}

TEST(BoundaryLoopsTest, AConcaveStarHasOneLoopThroughItsTenCorners) {
	std::vector<Foundation::Vec2> star;
	for (int i = 0; i < 10; ++i) {
		const float radius = (i % 2 == 0) ? 140.0F : 60.0F;
		const float angle = (-kPi * 0.5F) + (2.0F * kPi * static_cast<float>(i) / 10.0F);
		star.emplace_back(radius * std::cos(angle), radius * std::sin(angle));
	}
	const renderer::TessellatedMesh mesh = tessellate(star);
	BoundaryLoops					loops;
	ASSERT_TRUE(findBoundaryLoops(mesh.vertices, mesh.indices, loops));
	ASSERT_EQ(loops.count(), 1U);
	const std::vector<uint16_t> corners = loop(loops, 0);
	ASSERT_EQ(corners.size(), 10U);
	// Round the star in order, alternating tips and notches.
	for (size_t i = 0; i < corners.size(); ++i) {
		const float radius = glm::length(mesh.vertices[corners[i]]);
		const float next = glm::length(mesh.vertices[corners[(i + 1) % corners.size()]]);
		EXPECT_NEAR(radius + next, 200.0F, 1e-3F) << "corner " << i;
	}
	EXPECT_GT(twiceArea(loops, 0, mesh.vertices), 0.0F);
}

TEST(BoundaryLoopsTest, APentagramsLoopRunsThroughItsCrossings) {
	// Five points joined every second one, filled nonzero: the middle is filled, and the
	// boundary is the star's outline, through the five points where its edges cross.
	std::vector<Foundation::Vec2> pentagram;
	for (int i = 0; i < 5; ++i) {
		const float angle = (-kPi * 0.5F) + (2.0F * kPi * static_cast<float>((i * 2) % 5) / 5.0F);
		pentagram.emplace_back(140.0F * std::cos(angle), 140.0F * std::sin(angle));
	}
	const renderer::TessellatedMesh mesh = tessellate(pentagram);
	BoundaryLoops					loops;
	ASSERT_TRUE(findBoundaryLoops(mesh.vertices, mesh.indices, loops));
	ASSERT_EQ(loops.count(), 1U);
	const std::vector<uint16_t> corners = loop(loops, 0);
	ASSERT_EQ(corners.size(), 10U);
	const float crossing = 140.0F * std::cos(2.0F * kPi / 5.0F) / std::cos(kPi / 5.0F);
	for (size_t i = 0; i < corners.size(); ++i) {
		const float radius = glm::length(mesh.vertices[corners[i]]);
		const float next = glm::length(mesh.vertices[corners[(i + 1) % corners.size()]]);
		EXPECT_NEAR(radius + next, 140.0F + crossing, 1e-2F) << "corner " << i;
	}
}

TEST(BoundaryLoopsTest, ARingHasALoopOutsideAndOneRoundItsHole) {
	// A 30 x 30 square with a 10 x 10 hole, as eight triangles.
	const std::array<Foundation::Vec2, 8> ring = {
		{{0.0F, 0.0F}, {30.0F, 0.0F}, {30.0F, 30.0F}, {0.0F, 30.0F}, {10.0F, 10.0F}, {20.0F, 10.0F}, {20.0F, 20.0F}, {10.0F, 20.0F}}
	};
	const std::array<uint16_t, 24> indices = {0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7};
	BoundaryLoops				   loops;
	ASSERT_TRUE(findBoundaryLoops(ring, indices, loops));
	ASSERT_EQ(loops.count(), 2U);
	EXPECT_EQ(loop(loops, 0), (std::vector<uint16_t>{0, 1, 2, 3}));
	// The hole runs the other way round, the mesh still on its left.
	EXPECT_EQ(loop(loops, 1), (std::vector<uint16_t>{4, 7, 6, 5}));
}

TEST(BoundaryLoopsTest, APinchWhereTwoPartsMeetAtAPointDoesNotChain) {
	// Two triangles that share only the vertex (5, 5), which starts two boundary edges.
	const std::array<Foundation::Vec2, 5> bowtie = {{{0.0F, 0.0F}, {10.0F, 0.0F}, {5.0F, 5.0F}, {0.0F, 10.0F}, {10.0F, 10.0F}}};
	const std::array<uint16_t, 6>		  indices = {0, 1, 2, 2, 4, 3};
	BoundaryLoops						  loops;
	loops.starts = {0, 1};
	loops.vertices = {0};
	EXPECT_FALSE(findBoundaryLoops(bowtie, indices, loops));
	EXPECT_TRUE(loops.empty());
	EXPECT_TRUE(loops.vertices.empty());

	// A polygon whose outline touches itself at (10, 10) tessellates to the same pinch.
	const renderer::TessellatedMesh mesh = tessellate(
		{{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {20.0F, 10.0F}, {20.0F, 20.0F}, {10.0F, 20.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}}
	);
	EXPECT_FALSE(findBoundaryLoops(mesh.vertices, mesh.indices, loops));
	EXPECT_TRUE(loops.empty());
}

TEST(BoundaryLoopsTest, OverlapsBadIndicesAndNothingDoNotChain) {
	BoundaryLoops loops;
	// Two triangles running the edge 0 to 1 the same way: one lies over the other.
	const std::array<Foundation::Vec2, 4> fold = {{{0.0F, 0.0F}, {10.0F, 0.0F}, {5.0F, 5.0F}, {5.0F, 8.0F}}};
	const std::array<uint16_t, 6>		  overlapping = {0, 1, 2, 0, 1, 3};
	EXPECT_FALSE(findBoundaryLoops(fold, overlapping, loops));

	const std::array<uint16_t, 3> pastTheEnd = {0, 1, 4};
	EXPECT_FALSE(findBoundaryLoops(kSquare, pastTheEnd, loops));

	EXPECT_FALSE(findBoundaryLoops(kSquare, {}, loops));
	EXPECT_TRUE(loops.empty());
}

TEST(BoundaryLoopsTest, AZeroAreaTriangleTakesTheWindingThatCancelsItsNeighbours) {
	// A square with a point halfway along its bottom edge, the sliver (0, 1, 2) along that
	// edge given either way round: its edge back from 2 to 0 cancels its neighbour's.
	const std::array<Foundation::Vec2, 5> square = {{{0.0F, 0.0F}, {5.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}}};
	BoundaryLoops						  loops;
	for (const std::array<uint16_t, 9>& indices :
		 {std::array<uint16_t, 9>{0, 1, 2, 0, 2, 3, 0, 3, 4}, std::array<uint16_t, 9>{0, 2, 1, 0, 2, 3, 0, 3, 4}}) {
		ASSERT_TRUE(findBoundaryLoops(square, indices, loops));
		EXPECT_EQ(loop(loops, 0), (std::vector<uint16_t>{0, 1, 2, 3, 4}));
	}
}

TEST(BoundaryLoopsTest, OutlinesWithCollinearPointsChainInEitherWinding) {
	const std::vector<Foundation::Vec2> outline = {{0.0F, 0.0F}, {5.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {5.0F, 10.0F}, {0.0F, 10.0F}};
	const std::vector<Foundation::Vec2> reversed(outline.rbegin(), outline.rend());
	for (const std::vector<Foundation::Vec2>* each : {&outline, &reversed}) {
		const renderer::TessellatedMesh mesh = tessellate(*each);
		BoundaryLoops					loops;
		ASSERT_TRUE(findBoundaryLoops(mesh.vertices, mesh.indices, loops));
		EXPECT_EQ(loops.count(), 1U);
		EXPECT_GT(twiceArea(loops, 0, mesh.vertices), 0.0F);
	}
}
