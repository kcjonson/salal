#include "primitives/Stroke.h"
#include <glm/geometric.hpp>
#include <gtest/gtest.h>
#include <vector>

using namespace Renderer;
using Primitives::LineCap;
using Primitives::LineJoin;

namespace {

	constexpr float kTolerance = 1e-4F;

	void
	expectPiece(const StrokePiece& piece, Foundation::Vec2 center, Foundation::Vec2 axis, float halfLength, float halfWidth, float radius) {
		EXPECT_NEAR(piece.center.x, center.x, kTolerance);
		EXPECT_NEAR(piece.center.y, center.y, kTolerance);
		EXPECT_NEAR(piece.axis.x, axis.x, kTolerance);
		EXPECT_NEAR(piece.axis.y, axis.y, kTolerance);
		EXPECT_NEAR(piece.halfLength, halfLength, kTolerance);
		EXPECT_NEAR(piece.halfWidth, halfWidth, kTolerance);
		EXPECT_NEAR(piece.cornerRadius, radius, kTolerance);
	}

	void expectDisk(const StrokePiece& piece, Foundation::Vec2 center, float radius) {
		expectPiece(piece, center, {1.0F, 0.0F}, radius, radius, radius);
	}

	const std::vector<Foundation::Vec2> kElbow = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}};

} // namespace

TEST(StrokeTest, RoundJoinIsTwoButtSegmentsAndADisk) {
	std::vector<StrokePiece> pieces;
	strokePolyline({.points = kElbow, .width = 2.0F, .cap = LineCap::Butt, .join = LineJoin::Round}, pieces);
	ASSERT_EQ(pieces.size(), 3U);
	expectDisk(pieces[0], {10.0F, 0.0F}, 1.0F);
	expectPiece(pieces[1], {5.0F, 0.0F}, {1.0F, 0.0F}, 5.0F, 1.0F, 0.0F);
	expectPiece(pieces[2], {10.0F, 5.0F}, {0.0F, 1.0F}, 5.0F, 1.0F, 0.0F);
}

TEST(StrokeTest, MiterRunsBothSegmentsOnToTheMiterPoint) {
	std::vector<StrokePiece> pieces;
	strokePolyline({.points = kElbow, .width = 2.0F, .join = LineJoin::Miter}, pieces);
	ASSERT_EQ(pieces.size(), 3U);
	expectDisk(pieces[0], {10.0F, 0.0F}, 1.0F);
	// A right angle reaches halfWidth * tan(45) = 1 past the joint: x to 11, y from -1.
	expectPiece(pieces[1], {5.5F, 0.0F}, {1.0F, 0.0F}, 5.5F, 1.0F, 0.0F);
	expectPiece(pieces[2], {10.0F, 4.5F}, {0.0F, 1.0F}, 5.5F, 1.0F, 0.0F);
}

TEST(StrokeTest, MiterPastARightAngleKeepsTheDiskAlone) {
	std::vector<StrokePiece>			pieces;
	const std::vector<Foundation::Vec2> hairpin = {{0.0F, 0.0F}, {10.0F, 0.0F}, {0.0F, 2.0F}};
	strokePolyline({.points = hairpin, .width = 2.0F, .join = LineJoin::Miter}, pieces);
	ASSERT_EQ(pieces.size(), 3U);
	expectDisk(pieces[0], {10.0F, 0.0F}, 1.0F);
	EXPECT_NEAR(pieces[1].halfLength, 5.0F, kTolerance);
	EXPECT_NEAR(pieces[2].halfLength, glm::length(Foundation::Vec2(10.0F, -2.0F)) * 0.5F, kTolerance);
}

TEST(StrokeTest, CapsExtendOrAddDisksAtTheOpenEnds) {
	std::vector<StrokePiece> pieces;
	strokePolyline({.points = kElbow, .width = 2.0F, .cap = LineCap::Square}, pieces);
	ASSERT_EQ(pieces.size(), 3U);
	expectPiece(pieces[1], {4.5F, 0.0F}, {1.0F, 0.0F}, 5.5F, 1.0F, 0.0F);
	expectPiece(pieces[2], {10.0F, 5.5F}, {0.0F, 1.0F}, 5.5F, 1.0F, 0.0F);

	strokePolyline({.points = kElbow, .width = 2.0F, .cap = LineCap::Round}, pieces);
	ASSERT_EQ(pieces.size(), 5U);
	expectDisk(pieces[0], {0.0F, 0.0F}, 1.0F);
	expectDisk(pieces[1], {10.0F, 10.0F}, 1.0F);
	expectDisk(pieces[2], {10.0F, 0.0F}, 1.0F);
}

TEST(StrokeTest, ClosedLineJoinsEveryPointAndIgnoresCaps) {
	std::vector<StrokePiece>			pieces;
	const std::vector<Foundation::Vec2> square = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}, {0.0F, 0.0F}};
	strokePolyline({.points = square, .width = 2.0F, .cap = LineCap::Round, .join = LineJoin::Miter, .closed = true}, pieces);
	// The repeated first point is dropped: four joints, four segments, no caps.
	ASSERT_EQ(pieces.size(), 8U);
	expectDisk(pieces[0], {0.0F, 0.0F}, 1.0F);
	expectPiece(pieces[4], {5.0F, 0.0F}, {1.0F, 0.0F}, 6.0F, 1.0F, 0.0F);
	expectPiece(pieces[7], {0.0F, 5.0F}, {0.0F, -1.0F}, 6.0F, 1.0F, 0.0F);
}

TEST(StrokeTest, RepeatsAndEmptyInputDrawNothingExtra) {
	std::vector<StrokePiece>			pieces;
	const std::vector<Foundation::Vec2> point = {{3.0F, 3.0F}, {3.0F, 3.0F}};
	strokePolyline({.points = point, .width = 2.0F}, pieces);
	EXPECT_TRUE(pieces.empty());

	const std::vector<Foundation::Vec2> repeated = {{0.0F, 0.0F}, {0.0F, 0.0F}, {5.0F, 0.0F}};
	strokePolyline({.points = repeated, .width = 2.0F}, pieces);
	ASSERT_EQ(pieces.size(), 1U);
	expectPiece(pieces[0], {2.5F, 0.0F}, {1.0F, 0.0F}, 2.5F, 1.0F, 0.0F);

	strokePolyline({.points = kElbow, .width = 0.0F}, pieces);
	EXPECT_TRUE(pieces.empty());
}

TEST(StrokeTest, OffsetOutlineMovesEveryEdgeOutwardForEitherWinding) {
	const std::vector<Foundation::Vec2> square = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}};
	const std::vector<Foundation::Vec2> reversed(square.rbegin(), square.rend());
	const std::vector<Foundation::Vec2> grown = {{-1.0F, -1.0F}, {11.0F, -1.0F}, {11.0F, 11.0F}, {-1.0F, 11.0F}};

	std::vector<Foundation::Vec2> out;
	offsetOutline(square, 1.0F, out);
	ASSERT_EQ(out.size(), 4U);
	for (size_t i = 0; i < 4; ++i) {
		EXPECT_NEAR(out[i].x, grown[i].x, kTolerance);
		EXPECT_NEAR(out[i].y, grown[i].y, kTolerance);
	}

	offsetOutline(reversed, 1.0F, out);
	ASSERT_EQ(out.size(), 4U);
	for (size_t i = 0; i < 4; ++i) {
		EXPECT_NEAR(out[i].x, grown[3 - i].x, kTolerance);
		EXPECT_NEAR(out[i].y, grown[3 - i].y, kTolerance);
	}
}

TEST(StrokeTest, OffsetOutlineLimitsASpikeAndRejectsZeroArea) {
	std::vector<Foundation::Vec2>		out;
	const std::vector<Foundation::Vec2> spike = {{0.0F, 0.0F}, {100.0F, 1.0F}, {0.0F, 2.0F}};
	offsetOutline(spike, 1.0F, out);
	ASSERT_EQ(out.size(), 3U);
	EXPECT_NEAR(glm::length(out[1] - spike[1]), 4.0F, kTolerance);
	EXPECT_GT(out[1].x, 100.0F);

	const std::vector<Foundation::Vec2> line = {{0.0F, 0.0F}, {5.0F, 0.0F}, {10.0F, 0.0F}};
	offsetOutline(line, 1.0F, out);
	EXPECT_TRUE(out.empty());
}
