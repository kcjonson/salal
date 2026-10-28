#include "primitives/Stroke.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/geometric.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <vector>

using namespace Renderer;
using Primitives::LineCap;
using Primitives::LineJoin;

namespace {

	constexpr float kTolerance = 1e-5F;
	const float		kRoot3 = std::sqrt(3.0F);

	void
	expectShape(const StrokePiece& piece, Foundation::Vec2 center, Foundation::Vec2 axis, float halfLength, float halfWidth, float radius) {
		EXPECT_NEAR(piece.center.x, center.x, kTolerance);
		EXPECT_NEAR(piece.center.y, center.y, kTolerance);
		EXPECT_NEAR(piece.axis.x, axis.x, kTolerance);
		EXPECT_NEAR(piece.axis.y, axis.y, kTolerance);
		EXPECT_NEAR(piece.halfLength, halfLength, kTolerance);
		EXPECT_NEAR(piece.halfWidth, halfWidth, kTolerance);
		EXPECT_NEAR(piece.cornerRadius, radius, kTolerance);
	}

	void expectCorners(const StrokePiece& piece, const std::vector<Foundation::Vec2>& corners) {
		ASSERT_EQ(piece.cornerCount, corners.size());
		for (size_t i = 0; i < corners.size(); ++i) {
			EXPECT_NEAR(piece.corners[i].x, corners[i].x, kTolerance) << "corner " << i;
			EXPECT_NEAR(piece.corners[i].y, corners[i].y, kTolerance) << "corner " << i;
		}
	}

	bool sameBits(Foundation::Vec2 a, Foundation::Vec2 b) {
		return std::memcmp(&a, &b, sizeof(a)) == 0;
	}

	// How many of a's corners are in b, bit for bit: two across a cut or a split
	// segment's middle, none where the pieces just overlap.
	int sharedCorners(const StrokePiece& a, const StrokePiece& b) {
		int shared = 0;
		for (uint8_t i = 0; i < a.cornerCount; ++i) {
			for (uint8_t j = 0; j < b.cornerCount; ++j) {
				if (sameBits(a.corners[i], b.corners[j])) {
					++shared;
					break;
				}
			}
		}
		return shared;
	}

	// Convex with positive winding, in double precision.
	bool isConvex(const StrokePiece& piece) {
		if (piece.cornerCount < 3) {
			return false;
		}
		double twiceArea = 0.0;
		for (uint8_t i = 0; i < piece.cornerCount; ++i) {
			const Foundation::Vec2 a = piece.corners[i];
			const Foundation::Vec2 b = piece.corners[(i + 1) % piece.cornerCount];
			const Foundation::Vec2 c = piece.corners[(i + 2) % piece.cornerCount];
			const double		   turn = ((static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - b.y)) -
								((static_cast<double>(b.y) - a.y) * (static_cast<double>(c.x) - b.x));
			if (turn < -1e-6) {
				return false;
			}
			twiceArea += (static_cast<double>(a.x) * b.y) - (static_cast<double>(a.y) * b.x);
		}
		return twiceArea > 0.0;
	}

	// Strictly inside the piece's polygon.
	bool contains(const StrokePiece& piece, Foundation::Vec2 p) {
		for (uint8_t i = 0; i < piece.cornerCount; ++i) {
			const Foundation::Vec2 a = piece.corners[i];
			const Foundation::Vec2 b = piece.corners[(i + 1) % piece.cornerCount];
			if (((b.x - a.x) * (p.y - a.y)) - ((b.y - a.y) * (p.x - a.x)) <= 0.0F) {
				return false;
			}
		}
		return true;
	}

	bool isFlat(const StrokePiece& piece) {
		return piece.halfLength == 0.0F && piece.halfWidth == 0.0F;
	}

	// Signed distance to a piece's SDF, as the shader measures it (negative inside); a
	// flat piece is inside everywhere.
	float pieceDistance(const StrokePiece& piece, Foundation::Vec2 point) {
		if (isFlat(piece)) {
			return -std::numeric_limits<float>::infinity();
		}
		const Foundation::Vec2 local = point - piece.center;
		const float			   along = glm::dot(local, piece.axis);
		const float			   across = (piece.axis.x * local.y) - (piece.axis.y * local.x);
		const float			   radius = std::min(piece.cornerRadius, std::min(piece.halfLength, piece.halfWidth));
		const float			   qx = std::abs(along) - piece.halfLength + radius;
		const float			   qy = std::abs(across) - piece.halfWidth + radius;
		return glm::length(Foundation::Vec2(std::max(qx, 0.0F), std::max(qy, 0.0F))) + std::min(std::max(qx, qy), 0.0F) - radius;
	}

	// Drawn there: inside some piece's polygon and inside its SDF.
	bool covered(const std::vector<StrokePiece>& pieces, Foundation::Vec2 point) {
		return std::ranges::any_of(pieces, [&](const StrokePiece& piece) {
			return contains(piece, point) && pieceDistance(piece, point) <= 0.0F;
		});
	}

	float segmentDistance(Foundation::Vec2 p, Foundation::Vec2 a, Foundation::Vec2 b) {
		const Foundation::Vec2 ab = b - a;
		const float			   t = std::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0F, 1.0F);
		return glm::length(p - (a + (ab * t)));
	}

	// Signed distance to a simple polygon, negative inside.
	float polygonDistance(const std::vector<Foundation::Vec2>& polygon, Foundation::Vec2 p) {
		float distance = std::numeric_limits<float>::max();
		bool  inside = false;
		for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
			const Foundation::Vec2 a = polygon[i];
			const Foundation::Vec2 b = polygon[j];
			distance = std::min(distance, segmentDistance(p, a, b));
			if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + ((b.x - a.x) * (p.y - a.y) / (b.y - a.y))) {
				inside = !inside;
			}
		}
		return inside ? -distance : distance;
	}

	// Every cut and middle is shared bit for bit by consecutive pieces, round the end
	// for a closed line, and every piece is convex.
	void expectAllCut(const std::vector<StrokePiece>& pieces, bool closed) {
		for (size_t i = 0; i < pieces.size(); ++i) {
			EXPECT_TRUE(isConvex(pieces[i])) << "piece " << i;
			if (i + 1 < pieces.size() || closed) {
				EXPECT_EQ(sharedCorners(pieces[i], pieces[(i + 1) % pieces.size()]), 2) << "pieces " << i << " and the next";
			}
		}
	}

	// Over a grid offset off every cut: each point the stroke's edge ramp could reach
	// (`distance` under a pixel) lies in exactly one piece, whose SDF is that distance
	// there down to a pixel inside, except within two pixels of an `unshaded` point.
	// Points further out belong to at most one piece.
	template <typename Distance>
	void expectPartition(
		const std::vector<StrokePiece>&		 pieces,
		float								 pixel,
		Foundation::Vec2					 from,
		Foundation::Vec2					 to,
		Distance							 distance,
		const std::vector<Foundation::Vec2>& unshaded = {}
	) {
		int sampled = 0;
		for (float y = from.y + 0.0371F; y < to.y; y += 0.25F) {
			for (float x = from.x + 0.0213F; x < to.x; x += 0.25F) {
				const Foundation::Vec2 point(x, y);
				const float			   expected = distance(point);
				const StrokePiece*	   owner = nullptr;
				int					   owners = 0;
				for (const StrokePiece& piece : pieces) {
					if (contains(piece, point)) {
						owner = &piece;
						++owners;
					}
				}
				if (expected < pixel - 0.01F) {
					ASSERT_EQ(owners, 1) << "at " << x << ", " << y;
					const bool shaded =
						std::ranges::none_of(unshaded, [&](Foundation::Vec2 u) { return glm::length(point - u) < 2.0F * pixel; });
					// Past a pixel inside, coverage is whole whatever the distance.
					if (shaded) {
						EXPECT_NEAR(std::max(pieceDistance(*owner, point), -pixel), std::max(expected, -pixel), 2e-3F)
							<< "at " << x << ", " << y;
					}
					++sampled;
				} else {
					ASSERT_LE(owners, 1) << "at " << x << ", " << y;
				}
			}
		}
		EXPECT_GT(sampled, 0);
	}

	// The distance to a polyline's round-joined, round-capped stroke.
	auto roundStroke(const std::vector<Foundation::Vec2>& points, float halfWidth, bool closed) {
		return [points, halfWidth, closed](Foundation::Vec2 p) {
			float		 nearest = std::numeric_limits<float>::max();
			const size_t segments = closed ? points.size() : points.size() - 1;
			for (size_t i = 0; i < segments; ++i) {
				nearest = std::min(nearest, segmentDistance(p, points[i], points[(i + 1) % points.size()]));
			}
			return nearest - halfWidth;
		};
	}

	// The outline of an open polyline's stroke with miter joins and butt caps: each
	// side's offset lines meeting at the joints.
	std::vector<Foundation::Vec2> miterOutline(const std::vector<Foundation::Vec2>& points, float halfWidth) {
		std::vector<Foundation::Vec2> left;
		std::vector<Foundation::Vec2> right;
		for (size_t i = 0; i < points.size(); ++i) {
			const Foundation::Vec2 in = i == 0 ? glm::normalize(points[1] - points[0]) : glm::normalize(points[i] - points[i - 1]);
			const Foundation::Vec2 out = i + 1 == points.size() ? in : glm::normalize(points[i + 1] - points[i]);
			const Foundation::Vec2 normalIn(-in.y, in.x);
			const Foundation::Vec2 normalOut(-out.y, out.x);
			const Foundation::Vec2 miter = (normalIn + normalOut) * (halfWidth / (1.0F + glm::dot(normalIn, normalOut)));
			left.push_back(points[i] + miter);
			right.push_back(points[i] - miter);
		}
		left.insert(left.end(), right.rbegin(), right.rend());
		return left;
	}

	const std::vector<Foundation::Vec2> kElbow = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}};

} // namespace

TEST(StrokeTest, ARoundJointCutsAlongItsBisectorAndAButtCapSplitsItsSegment) {
	std::vector<StrokePiece> pieces;
	strokePolyline({.points = kElbow, .width = 2.0F, .cap = LineCap::Butt, .join = LineJoin::Round}, 1.0F, pieces);
	ASSERT_EQ(pieces.size(), 4U);
	// The joint's cut runs from (8, 2) to (12, -2): where strips a pixel wider than the
	// stroke (reach 2) cross the 45-degree bisector. Butt cap beside round joint: each
	// segment is a box half and a capsule half, cut across its middle, each SDF running
	// a reach past it.
	expectCorners(pieces[0], {{5.0F, -2.0F}, {5.0F, 2.0F}, {-1.0F, 2.0F}, {-1.0F, -2.0F}});
	expectShape(pieces[0], {3.5F, 0.0F}, {1.0F, 0.0F}, 3.5F, 1.0F, 0.0F);
	expectCorners(pieces[1], {{12.0F, -2.0F}, {8.0F, 2.0F}, {5.0F, 2.0F}, {5.0F, -2.0F}});
	expectShape(pieces[1], {6.5F, 0.0F}, {1.0F, 0.0F}, 4.5F, 1.0F, 1.0F);
	expectCorners(pieces[2], {{12.0F, 5.0F}, {8.0F, 5.0F}, {8.0F, 2.0F}, {12.0F, -2.0F}});
	expectShape(pieces[2], {10.0F, 3.5F}, {0.0F, 1.0F}, 4.5F, 1.0F, 1.0F);
	expectCorners(pieces[3], {{12.0F, 11.0F}, {8.0F, 11.0F}, {8.0F, 5.0F}, {12.0F, 5.0F}});
	expectShape(pieces[3], {10.0F, 6.5F}, {0.0F, 1.0F}, 3.5F, 1.0F, 0.0F);
	// The cut and the middles are the same floats in both pieces.
	EXPECT_TRUE(sameBits(pieces[1].corners[0], pieces[2].corners[3]));
	EXPECT_TRUE(sameBits(pieces[1].corners[1], pieces[2].corners[2]));
	EXPECT_TRUE(sameBits(pieces[0].corners[0], pieces[1].corners[3]));
	EXPECT_TRUE(sameBits(pieces[2].corners[0], pieces[3].corners[3]));
	expectAllCut(pieces, false);
}

TEST(StrokeTest, ACutMitersBoxRunsAPixelPastItsPiece) {
	std::vector<StrokePiece> pieces;
	strokePolyline({.points = kElbow, .width = 2.0F, .join = LineJoin::Miter}, 1.0F, pieces);
	ASSERT_EQ(pieces.size(), 2U);
	// The cut reaches reach * tan(45) = 2 past the joint along each segment, and the box a
	// pixel further, to 3: its end face and that face's ramp stay outside the piece.
	expectCorners(pieces[0], {{12.0F, -2.0F}, {8.0F, 2.0F}, {-1.0F, 2.0F}, {-1.0F, -2.0F}});
	expectShape(pieces[0], {6.5F, 0.0F}, {1.0F, 0.0F}, 6.5F, 1.0F, 0.0F);
	expectCorners(pieces[1], {{12.0F, 11.0F}, {8.0F, 11.0F}, {8.0F, 2.0F}, {12.0F, -2.0F}});
	expectShape(pieces[1], {10.0F, 3.5F}, {0.0F, 1.0F}, 6.5F, 1.0F, 0.0F);
	expectAllCut(pieces, false);
}

TEST(StrokeTest, EveryCutMitersBoxEndsAPixelPastItsPiece) {
	// Turns of 0 to 90 degrees at a pixel of 1 and of 0.4: the box's face at the joint
	// clears the piece by at least a pixel, so the piece is shaded as the stroke right up
	// to its cut. The square caps' straight ends sit a pixel past their boxes by design.
	for (const float pixel : {1.0F, 0.4F}) {
		for (const float degrees : {0.0F, 3.0F, 10.0F, 30.0F, 60.0F, 90.0F}) {
			const float							radians = degrees * 3.14159265F / 180.0F;
			const std::vector<Foundation::Vec2> bend = {
				{0.0F, 0.0F}, {40.0F, 0.0F}, {40.0F + (40.0F * std::cos(radians)), 40.0F * std::sin(radians)}
			};
			std::vector<StrokePiece> pieces;
			strokePolyline({.points = bend, .width = 10.0F, .cap = LineCap::Square, .join = LineJoin::Miter}, pixel, pieces);
			ASSERT_EQ(pieces.size(), 2U);
			expectAllCut(pieces, false);
			for (const StrokePiece& piece : pieces) {
				for (uint8_t k = 0; k < piece.cornerCount; ++k) {
					const float along = std::abs(glm::dot(piece.corners[k] - piece.center, piece.axis));
					if (std::abs(along - (piece.halfLength + pixel)) < 1e-3F) {
						continue; // the cap's end
					}
					EXPECT_LE(along, piece.halfLength - pixel + 1e-3F) << degrees << " degrees, pixel " << pixel << ", corner " << int{k};
				}
			}
		}
	}
}

TEST(StrokeTest, ARoundJointPastARightAngleEndsItsCutOnEachSegmentsSquareExtension) {
	// A 120-degree turn, round caps so each segment is one capsule.
	const std::vector<Foundation::Vec2> bend = {{0.0F, 0.0F}, {10.0F, 0.0F}, {5.0F, 5.0F * kRoot3}};
	std::vector<StrokePiece>			pieces;
	strokePolyline({.points = bend, .width = 2.0F, .cap = LineCap::Round}, 1.0F, pieces);
	ASSERT_EQ(pieces.size(), 2U);
	// Inner end at reach / cos 60 along the bisector, outer end at reach / sin 60, on the
	// edge a reach past the joint; each piece adds the corner of its own extension.
	const Foundation::Vec2 inner(10.0F - (2.0F * kRoot3), 2.0F);
	const Foundation::Vec2 outer(12.0F, -2.0F / kRoot3);
	expectCorners(pieces[0], {{12.0F, -2.0F}, outer, inner, {-2.0F, 2.0F}, {-2.0F, -2.0F}});
	expectShape(pieces[0], {5.0F, 0.0F}, {1.0F, 0.0F}, 6.0F, 1.0F, 1.0F);
	const Foundation::Vec2 t(-0.5F, kRoot3 * 0.5F);
	const Foundation::Vec2 n(-t.y, t.x);
	const Foundation::Vec2 joint(10.0F, 0.0F);
	const Foundation::Vec2 end = bend[2] + (t * 2.0F);
	expectCorners(pieces[1], {end - (n * 2.0F), end + (n * 2.0F), inner, outer, joint - (t * 2.0F) - (n * 2.0F)});
	expectShape(pieces[1], {7.5F, 2.5F * kRoot3}, t, 6.0F, 1.0F, 1.0F);
	expectAllCut(pieces, false);
	expectPartition(pieces, 1.0F, {-4.0F, -4.0F}, {14.0F, 12.0F}, roundStroke(bend, 1.0F, false));
}

TEST(StrokeTest, AJointNearAHairpinIsLeftUncutAndItsPiecesOverlap) {
	// A 174-degree turn: within 8.6 degrees of a hairpin.
	const std::vector<Foundation::Vec2> hairpin = {{0.0F, 0.0F}, {10.0F, 0.0F}, {0.0F, 1.0F}};
	std::vector<StrokePiece>			pieces;
	strokePolyline({.points = hairpin, .width = 2.0F, .cap = LineCap::Round}, 1.0F, pieces);
	ASSERT_EQ(pieces.size(), 2U);
	// Each capsule ends straight a pixel past its own round end.
	expectCorners(pieces[0], {{12.0F, -2.0F}, {12.0F, 2.0F}, {-2.0F, 2.0F}, {-2.0F, -2.0F}});
	EXPECT_EQ(sharedCorners(pieces[0], pieces[1]), 0);
	EXPECT_TRUE(isConvex(pieces[1]));
}

TEST(StrokeTest, ASegmentTooShortForItsCutsLeavesExactlyItsJointsUncut) {
	// Segment 1 is one unit long between two left turns of 90 degrees, whose cuts need
	// it a reach (3) longer at each end. Joint 3, a right turn after a long segment, is cut.
	const std::vector<Foundation::Vec2> hook = {{0.0F, 0.0F}, {20.0F, 0.0F}, {20.0F, 1.0F}, {0.0F, 1.0F}, {0.0F, 21.0F}};
	std::vector<StrokePiece>			pieces;
	strokePolyline({.points = hook, .width = 4.0F}, 1.0F, pieces);
	// Segment 0 and 3: a butt cap beside a round joint, two pieces each.
	ASSERT_EQ(pieces.size(), 6U);
	EXPECT_EQ(sharedCorners(pieces[0], pieces[1]), 2); // segment 0's middle
	// Joints 1 and 2, uncut, share no cut; their pieces' square ends meet at one corner.
	EXPECT_LT(sharedCorners(pieces[1], pieces[2]), 2);
	EXPECT_LT(sharedCorners(pieces[2], pieces[3]), 2);
	EXPECT_EQ(sharedCorners(pieces[3], pieces[4]), 2); // joint 3
	EXPECT_EQ(sharedCorners(pieces[4], pieces[5]), 2); // segment 3's middle
	// The short segment's capsule ends straight a pixel past its round ends.
	expectCorners(pieces[2], {{23.0F, 4.0F}, {17.0F, 4.0F}, {17.0F, -3.0F}, {23.0F, -3.0F}});
	expectShape(pieces[2], {20.0F, 0.5F}, {0.0F, 1.0F}, 2.5F, 2.0F, 2.0F);
	for (const StrokePiece& piece : pieces) {
		EXPECT_TRUE(isConvex(piece));
	}
}

TEST(StrokeTest, AMiterBesideATurnThatRoundsSplitsItsSegment) {
	// A 27-degree turn, mitered, then a 135-degree one, which rounds.
	const std::vector<Foundation::Vec2> line = {{0.0F, 0.0F}, {20.0F, 0.0F}, {40.0F, 10.0F}, {25.0F, 15.0F}};
	std::vector<StrokePiece>			pieces;
	strokePolyline({.points = line, .width = 4.0F, .join = LineJoin::Miter}, 1.0F, pieces);
	ASSERT_EQ(pieces.size(), 5U);
	expectAllCut(pieces, false);
	EXPECT_EQ(pieces[1].cornerRadius, 0.0F); // the miter's half
	EXPECT_EQ(pieces[2].cornerRadius, 2.0F); // the round joint's half
	EXPECT_EQ(pieces[3].cornerRadius, 2.0F);
	EXPECT_EQ(pieces[4].cornerRadius, 0.0F); // the butt cap's half
}

TEST(StrokeTest, CapsEndStraightAPixelPastTheirReach) {
	const std::vector<Foundation::Vec2> line = {{0.0F, 0.0F}, {10.0F, 0.0F}};
	std::vector<StrokePiece>			pieces;
	strokePolyline({.points = line, .width = 2.0F, .cap = LineCap::Butt}, 1.0F, pieces);
	ASSERT_EQ(pieces.size(), 1U);
	expectCorners(pieces[0], {{11.0F, -2.0F}, {11.0F, 2.0F}, {-1.0F, 2.0F}, {-1.0F, -2.0F}});
	expectShape(pieces[0], {5.0F, 0.0F}, {1.0F, 0.0F}, 5.0F, 1.0F, 0.0F);

	strokePolyline({.points = line, .width = 2.0F, .cap = LineCap::Round}, 1.0F, pieces);
	ASSERT_EQ(pieces.size(), 1U);
	expectCorners(pieces[0], {{12.0F, -2.0F}, {12.0F, 2.0F}, {-2.0F, 2.0F}, {-2.0F, -2.0F}});
	expectShape(pieces[0], {5.0F, 0.0F}, {1.0F, 0.0F}, 6.0F, 1.0F, 1.0F);

	strokePolyline({.points = line, .width = 2.0F, .cap = LineCap::Square}, 0.5F, pieces);
	ASSERT_EQ(pieces.size(), 1U);
	expectCorners(pieces[0], {{11.5F, -1.5F}, {11.5F, 1.5F}, {-1.5F, 1.5F}, {-1.5F, -1.5F}});
	expectShape(pieces[0], {5.0F, 0.0F}, {1.0F, 0.0F}, 6.0F, 1.0F, 0.0F);
}

TEST(StrokeTest, AClosedLineCutsEveryJointAndIgnoresCaps) {
	const std::vector<Foundation::Vec2> square = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}, {0.0F, 0.0F}};
	std::vector<StrokePiece>			pieces;
	strokePolyline({.points = square, .width = 2.0F, .cap = LineCap::Round, .join = LineJoin::Miter, .closed = true}, 1.0F, pieces);
	// The repeated first point is dropped: four segments, each a box run on a pixel past
	// both its cuts, every joint cut.
	ASSERT_EQ(pieces.size(), 4U);
	expectCorners(pieces[0], {{12.0F, -2.0F}, {8.0F, 2.0F}, {2.0F, 2.0F}, {-2.0F, -2.0F}});
	expectShape(pieces[0], {5.0F, 0.0F}, {1.0F, 0.0F}, 8.0F, 1.0F, 0.0F);
	expectShape(pieces[3], {0.0F, 5.0F}, {0.0F, -1.0F}, 8.0F, 1.0F, 0.0F);
	expectAllCut(pieces, true);
}

TEST(StrokeTest, RepeatsAndEmptyInputDrawNothingExtra) {
	std::vector<StrokePiece>			pieces;
	const std::vector<Foundation::Vec2> point = {{3.0F, 3.0F}, {3.0F, 3.0F}};
	strokePolyline({.points = point, .width = 2.0F}, 1.0F, pieces);
	EXPECT_TRUE(pieces.empty());

	const std::vector<Foundation::Vec2> repeated = {{0.0F, 0.0F}, {0.0F, 0.0F}, {5.0F, 0.0F}};
	strokePolyline({.points = repeated, .width = 2.0F}, 1.0F, pieces);
	ASSERT_EQ(pieces.size(), 1U);
	expectShape(pieces[0], {2.5F, 0.0F}, {1.0F, 0.0F}, 2.5F, 1.0F, 0.0F);

	strokePolyline({.points = kElbow, .width = 0.0F}, 1.0F, pieces);
	EXPECT_TRUE(pieces.empty());
}

TEST(StrokeTest, EveryPointOfARoundStrokeBelongsToOnePieceShadedByTheStrokesDistance) {
	// Turns of 67 degrees, then past a right angle, then a closed triangle.
	const std::vector<Foundation::Vec2> zigzag = {{0.0F, 0.0F}, {30.0F, 20.0F}, {60.0F, 0.0F}, {90.0F, 20.0F}, {120.0F, 0.0F}};
	std::vector<StrokePiece>			pieces;
	strokePolyline({.points = zigzag, .width = 8.0F, .cap = LineCap::Round}, 1.0F, pieces);
	expectAllCut(pieces, false);
	expectPartition(pieces, 1.0F, {-6.0F, -6.0F}, {126.0F, 26.0F}, roundStroke(zigzag, 4.0F, false));

	const std::vector<Foundation::Vec2> sharp = {{0.0F, 0.0F}, {40.0F, 0.0F}, {5.0F, 15.0F}, {45.0F, 25.0F}};
	strokePolyline({.points = sharp, .width = 6.0F, .cap = LineCap::Round}, 0.5F, pieces);
	expectAllCut(pieces, false);
	expectPartition(pieces, 0.5F, {-5.0F, -5.0F}, {50.0F, 30.0F}, roundStroke(sharp, 3.0F, false));

	const std::vector<Foundation::Vec2> triangle = {{0.0F, 0.0F}, {40.0F, 0.0F}, {20.0F, 30.0F}};
	strokePolyline({.points = triangle, .width = 6.0F, .closed = true}, 1.0F, pieces);
	ASSERT_EQ(pieces.size(), 3U);
	expectAllCut(pieces, true);
	expectPartition(pieces, 1.0F, {-6.0F, -6.0F}, {46.0F, 36.0F}, roundStroke(triangle, 3.0F, true));
}

TEST(StrokeTest, EveryPointOfAMiteredStrokeBelongsToOnePieceShadedByTheOutlinesDistance) {
	const std::vector<Foundation::Vec2> zigzag = {{0.0F, 0.0F}, {30.0F, 20.0F}, {60.0F, 0.0F}, {90.0F, 20.0F}, {120.0F, 10.0F}};
	std::vector<StrokePiece>			pieces;
	strokePolyline({.points = zigzag, .width = 8.0F, .join = LineJoin::Miter}, 1.0F, pieces);
	ASSERT_EQ(pieces.size(), 4U);
	expectAllCut(pieces, false);
	// Within two pixels of a miter point each box measures to its own end, where the
	// outline measures to the other segment's edge: the tip's ramp is a little softer
	// than the outline's.
	const std::vector<Foundation::Vec2> outline = miterOutline(zigzag, 4.0F);
	std::vector<Foundation::Vec2>		tips(outline.begin() + 1, outline.begin() + 4);
	tips.insert(tips.end(), outline.begin() + 6, outline.begin() + 9);
	expectPartition(pieces, 1.0F, {-6.0F, -8.0F}, {126.0F, 28.0F}, [&](Foundation::Vec2 p) { return polygonDistance(outline, p); }, tips);
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
	EXPECT_NEAR(glm::length(out[1] - spike[1]), 4.0F, 1e-4F);
	EXPECT_GT(out[1].x, 100.0F);

	const std::vector<Foundation::Vec2> line = {{0.0F, 0.0F}, {5.0F, 0.0F}, {10.0F, 0.0F}};
	offsetOutline(line, 1.0F, out);
	EXPECT_TRUE(out.empty());
}

namespace {

	// A 40 x 40 block with a notch cut up from its bottom edge to (20, 15): 37 degrees.
	const std::vector<Foundation::Vec2> kNotched =
		{{0.0F, 0.0F}, {15.0F, 0.0F}, {20.0F, 15.0F}, {25.0F, 0.0F}, {40.0F, 0.0F}, {40.0F, 40.0F}, {0.0F, 40.0F}};

} // namespace

TEST(StrokeTest, OffsetOutlineMeetsExactlyAtASharpNotch) {
	// A 15-degree notch is past the miter limit, which applies to spikes only.
	const std::vector<Foundation::Vec2> notched = {
		{0.0F, 0.0F}, {18.0F, 0.0F}, {20.0F, 15.0F}, {22.0F, 0.0F}, {40.0F, 0.0F}, {40.0F, 40.0F}, {0.0F, 40.0F}
	};
	std::vector<Foundation::Vec2> out;
	offsetOutline(notched, 1.0F, out);
	ASSERT_EQ(out.size(), notched.size());
	// One unit from both walls of the notch.
	const Foundation::Vec2 wall = glm::normalize(notched[2] - notched[1]);
	const Foundation::Vec2 fromWall = out[2] - notched[1];
	EXPECT_NEAR(std::abs((wall.x * fromWall.y) - (wall.y * fromWall.x)), 1.0F, 1e-4F);
	EXPECT_NEAR(out[2].x, 20.0F, 1e-4F);
	EXPECT_LT(out[2].y, 15.0F - 4.0F);
}

TEST(StrokeTest, OutlineBorderRunsFromInsetToWidth) {
	const std::vector<Foundation::Vec2> square = {{8.0F, 8.0F}, {24.0F, 8.0F}, {24.0F, 24.0F}, {8.0F, 24.0F}};
	std::vector<StrokePiece>			pieces;
	strokeOutline(square, 4.0F, 1.0F, pieces);
	ASSERT_EQ(pieces.size(), 4U);
	expectAllCut(pieces, true);
	EXPECT_TRUE(covered(pieces, {4.05F, 16.0F}));
	EXPECT_TRUE(covered(pieces, {8.95F, 16.0F}));
	EXPECT_FALSE(covered(pieces, {3.95F, 16.0F}));
	EXPECT_FALSE(covered(pieces, {9.05F, 16.0F}));
	EXPECT_TRUE(covered(pieces, {4.05F, 4.1F})); // the mitered corner
}

TEST(StrokeTest, OutlineBorderMitersASharpNotchIntoItsCorner) {
	std::vector<StrokePiece> pieces;
	strokeOutline(kNotched, 4.0F, 1.0F, pieces);
	// Seven mitered joints, all cut, the notch's too: no flat piece.
	ASSERT_EQ(pieces.size(), 7U);
	expectAllCut(pieces, true);
	EXPECT_TRUE(std::ranges::none_of(pieces, isFlat));
	// Down the notch's middle the walls are (15 - y) * sin(18.4 degrees) away: within
	// the border's 4 from y = 2.35 up to the corner, and the inner edges, a unit inside
	// both walls, meet 1 / sin(18.4 degrees) = 3.16 above it.
	for (float y = 18.05F; y > 2.5F; y -= 0.25F) {
		EXPECT_TRUE(covered(pieces, {20.01F, y})) << "y " << y;
	}
	EXPECT_FALSE(covered(pieces, {20.01F, 18.3F}));
	EXPECT_FALSE(covered(pieces, {20.01F, 1.5F}));
}

TEST(StrokeTest, ANotchLeftUncutBesideAShortEdgeIsRoundWithAFlatPieceIntoItsCorner) {
	// The notch at (20, 22) turns 133 degrees; the edge after it, 4 long, is too short for
	// its cut and the convex corner's.
	const std::vector<Foundation::Vec2> outline = {
		{0.0F, 0.0F}, {40.0F, 0.0F}, {40.0F, 30.0F}, {24.0F, 30.0F}, {20.0F, 22.0F}, {18.6F, 25.75F}, {0.0F, 25.75F}
	};
	std::vector<StrokePiece> pieces;
	strokeOutline(outline, 4.0F, 1.0F, pieces);
	const auto flat = std::ranges::find_if(pieces, isFlat);
	ASSERT_NE(flat, pieces.end());
	EXPECT_EQ(std::ranges::count_if(pieces, isFlat), 1);
	EXPECT_EQ(flat->cornerCount, 3);
	EXPECT_TRUE(isConvex(*flat));
	// Its tip is where the border's inner edges meet, a unit inside both walls.
	std::vector<Foundation::Vec2> inset;
	offsetOutline(outline, -1.0F, inset);
	EXPECT_NEAR(flat->corners[0].x, inset[4].x, 1e-4F);
	EXPECT_NEAR(flat->corners[0].y, inset[4].y, 1e-4F);
	// Between that tip and the notch's corner only the flat piece draws.
	const Foundation::Vec2 between = (inset[4] + outline[4]) * 0.5F;
	EXPECT_TRUE(covered(pieces, between));
	EXPECT_TRUE(contains(*flat, between));
}
