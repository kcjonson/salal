#include "primitives/Stroke.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/geometric.hpp>
#include <initializer_list>
#include <utility>

namespace Renderer {

	namespace {

		constexpr float kMinSegment = 1e-4F;
		constexpr float kMiterLimit = 4.0F;
		// Below this length of the sum of a joint's two directions (within about 8.6
		// degrees of a hairpin) the joint is left uncut: its cut would run far down both
		// segments, and the bisector's direction is poorly defined.
		constexpr float kHairpinSum = 0.15F;

		float cross(const Foundation::Vec2& a, const Foundation::Vec2& b) {
			return (a.x * b.y) - (a.y * b.x);
		}

		Foundation::Vec2 leftNormal(const Foundation::Vec2& direction) {
			return {-direction.y, direction.x};
		}

		// `points` without consecutive repeats, and for a closed line without a last
		// point that repeats the first.
		void distinctPoints(std::span<const Foundation::Vec2> points, bool closed, std::vector<Foundation::Vec2>& out) {
			out.clear();
			for (const Foundation::Vec2& point : points) {
				if (out.empty() || glm::length(point - out.back()) >= kMinSegment) {
					out.push_back(point);
				}
			}
			if (closed && out.size() > 1 && glm::length(out.back() - out.front()) < kMinSegment) {
				out.pop_back();
			}
		}

		// Where segment j - 1 (arriving along `in`) meets segment j (leaving along `out`).
		// The cut is the bisector's line, perpendicular to in + out, from `innerCut` to
		// `outerCut`: where the two strips a pixel wider than the stroke cross it.
		struct Joint {
			Foundation::Vec2 point{0.0F, 0.0F};
			Foundation::Vec2 in{1.0F, 0.0F};
			Foundation::Vec2 out{1.0F, 0.0F};
			Foundation::Vec2 inner{0.0F, 0.0F}; // Unit, along the bisector's line into the turn; zero for a hairpin
			float			 cosHalf = 1.0F;	// Of half the turn
			float			 sinHalf = 0.0F;
			bool			 innerLeft = true;		 // The turn's inside is on both segments' left
			bool			 pastRightAngle = false; // Turns more than a right angle
			bool			 exact = false;			 // Mitered whatever its turn when cut (a polygon's notch)
			bool			 cut = false;
			bool			 cornered = false; // Cut past a right angle, round: its outer end is on each segment's square extension
			Foundation::Vec2 innerCut{0.0F, 0.0F};
			Foundation::Vec2 outerCut{0.0F, 0.0F};
		};

		bool convex(const StrokePiece& piece, const Foundation::Vec2& origin, const Foundation::Vec2& along, float scale) {
			if (piece.cornerCount < 3) {
				return false;
			}
			// In the segment's own frame, so the test sees the cut as the batch will.
			const Foundation::Vec2 across = leftNormal(along);
			const auto			   local = [&](const Foundation::Vec2& p) {
				return Foundation::Vec2(glm::dot(p - origin, along), glm::dot(p - origin, across));
			};
			const float tolerance = 1e-3F * scale * scale;
			float		twiceArea = 0.0F;
			for (uint8_t i = 0; i < piece.cornerCount; ++i) {
				const Foundation::Vec2 a = local(piece.corners[i]);
				const Foundation::Vec2 b = local(piece.corners[(i + 1) % piece.cornerCount]);
				const Foundation::Vec2 c = local(piece.corners[(i + 2) % piece.cornerCount]);
				if (cross(b - a, c - b) < -tolerance) {
					return false;
				}
				twiceArea += cross(a, b);
			}
			return twiceArea > 0.0F;
		}

		void addCorner(StrokePiece& piece, const Foundation::Vec2& corner) {
			piece.corners[piece.cornerCount++] = corner;
		}

		// The pieces for distinct `points` (at least two, and more than two when closed).
		// `exact`, empty or one flag per point, marks the joints mitered whatever their turn.
		void partition(
			std::span<const Foundation::Vec2> points,
			std::span<const uint8_t>		  exact,
			bool							  closed,
			float							  width,
			Primitives::LineCap				  cap,
			Primitives::LineJoin			  join,
			float							  pixel,
			std::vector<StrokePiece>&		  out
		) {
			out.clear();
			const float	 halfWidth = width * 0.5F;
			const float	 reach = halfWidth + pixel;
			const size_t count = points.size();
			const size_t segmentCount = closed ? count : count - 1;

			static thread_local std::vector<Foundation::Vec2> directions;
			static thread_local std::vector<float>			  lengths;
			directions.resize(segmentCount);
			lengths.resize(segmentCount);
			for (size_t i = 0; i < segmentCount; ++i) {
				const Foundation::Vec2 delta = points[(i + 1) % count] - points[i];
				lengths[i] = glm::length(delta);
				directions[i] = delta / lengths[i];
			}

			// Joint j is the point between segment j - 1 and segment j.
			static thread_local std::vector<Joint> joints;
			joints.assign(count, Joint{});
			const size_t firstJoint = closed ? 0 : 1;
			const size_t jointEnd = closed ? count : count - 1;
			for (size_t j = firstJoint; j < jointEnd; ++j) {
				Joint& joint = joints[j];
				joint.point = points[j];
				joint.in = directions[(j + segmentCount - 1) % segmentCount];
				joint.out = directions[j % segmentCount];
				joint.exact = !exact.empty() && exact[j] != 0;
				const Foundation::Vec2 sum = joint.in + joint.out;
				const float			   sumLength = glm::length(sum);
				joint.cosHalf = sumLength * 0.5F;
				joint.sinHalf = glm::length(joint.out - joint.in) * 0.5F;
				joint.innerLeft = cross(joint.in, joint.out) >= 0.0F;
				joint.pastRightAngle = glm::dot(joint.in, joint.out) < 0.0F;
				if (sumLength > kMinSegment) {
					joint.inner = leftNormal(sum) * ((joint.innerLeft ? 1.0F : -1.0F) / sumLength);
				}
				if (sumLength < kHairpinSum) {
					continue;
				}
				// The strips' inner edges cross on the bisector at reach / cos(turn / 2), and
				// so do their outer edges, which a miter or a round joint up to a right angle
				// runs to. A round joint past it ends each strip a reach past the point
				// first: the cut leaves that square extension at reach / sin(turn / 2).
				joint.cut = true;
				joint.cornered = joint.pastRightAngle && !(join == Primitives::LineJoin::Miter && joint.exact);
				joint.innerCut = joint.point + (joint.inner * (reach / joint.cosHalf));
				joint.outerCut = joint.point - (joint.inner * (reach / (joint.cornered ? joint.sinHalf : joint.cosHalf)));
			}

			const auto mitered = [join](const Joint& joint) {
				return join == Primitives::LineJoin::Miter && (!joint.pastRightAngle || (joint.exact && joint.cut));
			};
			// How far past its point a joint's SDF runs: to the miter point, or a capsule's end.
			const auto extent = [&](const Joint& joint) {
				return mitered(joint) ? halfWidth * joint.sinHalf / joint.cosHalf : halfWidth;
			};
			const bool	capRound = cap == Primitives::LineCap::Round;
			const float capExtent = cap == Primitives::LineCap::Butt ? 0.0F : halfWidth;

			// A piece's side at the start of its segment (`start`, along `t`, left normal
			// `n`) and at its end, `length` on, appended in winding order: the joint's cut,
			// or a straight cut a pixel past the SDF's `reachPast` the end point.
			const auto addStart = [&](StrokePiece&			  piece,
									  const Foundation::Vec2& start,
									  const Foundation::Vec2& t,
									  const Foundation::Vec2& n,
									  const Joint*			  joint,
									  float					  reachPast) {
				if (joint != nullptr && joint->cut) {
					const Foundation::Vec2 corner = joint->point - (t * reach) + (n * (joint->innerLeft ? -reach : reach));
					if (joint->innerLeft) {
						addCorner(piece, joint->innerCut);
						addCorner(piece, joint->outerCut);
						if (joint->cornered) {
							addCorner(piece, corner);
						}
					} else {
						if (joint->cornered) {
							addCorner(piece, corner);
						}
						addCorner(piece, joint->outerCut);
						addCorner(piece, joint->innerCut);
					}
					return;
				}
				const float x = -(reachPast + pixel);
				addCorner(piece, start + (t * x) + (n * reach));
				addCorner(piece, start + (t * x) - (n * reach));
			};
			const auto addEnd = [&](StrokePiece&			piece,
									const Foundation::Vec2& start,
									const Foundation::Vec2& t,
									const Foundation::Vec2& n,
									float					length,
									const Joint*			joint,
									float					reachPast) {
				if (joint != nullptr && joint->cut) {
					const Foundation::Vec2 corner = joint->point + (t * reach) + (n * (joint->innerLeft ? -reach : reach));
					if (joint->innerLeft) {
						if (joint->cornered) {
							addCorner(piece, corner);
						}
						addCorner(piece, joint->outerCut);
						addCorner(piece, joint->innerCut);
					} else {
						addCorner(piece, joint->innerCut);
						addCorner(piece, joint->outerCut);
						if (joint->cornered) {
							addCorner(piece, corner);
						}
					}
					return;
				}
				const float x = length + reachPast + pixel;
				addCorner(piece, start + (t * x) - (n * reach));
				addCorner(piece, start + (t * x) + (n * reach));
			};
			// The SDF from `from` to `to` along the segment, a capsule when `round`.
			const auto shade =
				[&](StrokePiece& piece, const Foundation::Vec2& start, const Foundation::Vec2& t, float from, float to, bool round) {
					piece.center = start + (t * ((from + to) * 0.5F));
					piece.axis = t;
					piece.halfLength = (to - from) * 0.5F;
					piece.halfWidth = halfWidth;
					piece.cornerRadius = round ? halfWidth : 0.0F;
				};

			// A piece that comes out concave, its segment too short for its joints' cuts,
			// uncuts both joints and the stroke is laid out again. A pass that changes
			// anything uncuts a joint, so this settles within one pass per joint.
			for (size_t pass = 0; pass <= count; ++pass) {
				out.clear();
				bool changed = false;
				for (size_t i = 0; i < segmentCount; ++i) {
					const Foundation::Vec2& start = points[i];
					const Foundation::Vec2& t = directions[i];
					const Foundation::Vec2	n = leftNormal(t);
					const float				length = lengths[i];
					Joint*					startJoint = (!closed && i == 0) ? nullptr : &joints[i];
					Joint*					endJoint = (!closed && i + 1 == segmentCount) ? nullptr : &joints[(i + 1) % count];
					const bool				startRound = startJoint != nullptr ? !mitered(*startJoint) : capRound;
					const bool				endRound = endJoint != nullptr ? !mitered(*endJoint) : capRound;
					const float				startExtent = startJoint != nullptr ? extent(*startJoint) : capExtent;
					const float				endExtent = endJoint != nullptr ? extent(*endJoint) : capExtent;

					const size_t firstPiece = out.size();
					if (startRound == endRound) {
						StrokePiece piece;
						addEnd(piece, start, t, n, length, endJoint, endExtent);
						addStart(piece, start, t, n, startJoint, startExtent);
						shade(piece, start, t, -startExtent, length + endExtent, startRound);
						out.push_back(piece);
					} else {
						// Each half's SDF runs on a reach past the middle, so its own end is
						// nowhere near the cut.
						const float			   middle = length * 0.5F;
						const Foundation::Vec2 middleRight = start + (t * middle) - (n * reach);
						const Foundation::Vec2 middleLeft = start + (t * middle) + (n * reach);
						StrokePiece			   first;
						addCorner(first, middleRight);
						addCorner(first, middleLeft);
						addStart(first, start, t, n, startJoint, startExtent);
						shade(first, start, t, -startExtent, middle + reach + (startRound ? halfWidth : 0.0F), startRound);
						StrokePiece second;
						addEnd(second, start, t, n, length, endJoint, endExtent);
						addCorner(second, middleLeft);
						addCorner(second, middleRight);
						shade(second, start, t, middle - reach - (endRound ? halfWidth : 0.0F), length + endExtent, endRound);
						out.push_back(first);
						out.push_back(second);
					}
					const bool concave =
						std::any_of(out.begin() + static_cast<std::ptrdiff_t>(firstPiece), out.end(), [&](const StrokePiece& piece) {
							return !convex(piece, start, t, reach);
						});
					if (concave) {
						for (Joint* joint : {startJoint, endJoint}) {
							if (joint != nullptr && joint->cut) {
								joint->cut = false;
								changed = true;
							}
						}
					}
				}
				if (!changed) {
					break;
				}
			}

			// An uncut joint that would miter past a right angle is round instead, since a
			// box run on that far pokes out of the turn's inside; a flat triangle covers what
			// the miter would have, between both segments' outer corners and the miter point.
			for (size_t j = firstJoint; j < jointEnd; ++j) {
				const Joint& joint = joints[j];
				if (joint.cut || !joint.exact || !joint.pastRightAngle || join != Primitives::LineJoin::Miter ||
					joint.inner == Foundation::Vec2(0.0F, 0.0F)) {
					continue;
				}
				const float			   side = joint.innerLeft ? -halfWidth : halfWidth;
				const Foundation::Vec2 miterPoint = joint.point - (joint.inner * (halfWidth / joint.cosHalf));
				Foundation::Vec2	   inCorner = joint.point + (leftNormal(joint.in) * side);
				Foundation::Vec2	   outCorner = joint.point + (leftNormal(joint.out) * side);
				if (cross(inCorner - miterPoint, outCorner - inCorner) < 0.0F) {
					std::swap(inCorner, outCorner);
				}
				StrokePiece wedge{.center = joint.point};
				addCorner(wedge, miterPoint);
				addCorner(wedge, inCorner);
				addCorner(wedge, outCorner);
				out.push_back(wedge);
			}
		}

	} // namespace

	void strokePolyline(const Primitives::PolylineArgs& args, float pixel, std::vector<StrokePiece>& out) {
		out.clear();
		if (!(args.width > 0.0F)) {
			return;
		}
		static thread_local std::vector<Foundation::Vec2> points;
		distinctPoints(args.points, args.closed, points);
		if (points.size() < 2) {
			return;
		}
		// Two points closed are one segment there and back: draw the segment.
		partition(points, {}, args.closed && points.size() > 2, args.width, args.cap, args.join, pixel, out);
	}

	namespace {

		// One vertex of a closed outline: its point, the unit directions of the edges
		// in and out, their outward normals, and `miter`, the vector that takes the
		// point to where the two edges meet once both move one unit outward.
		struct OutlineCorner {
			Foundation::Vec2 point;
			Foundation::Vec2 incoming;
			Foundation::Vec2 outgoing;
			Foundation::Vec2 normalIn;
			Foundation::Vec2 normalOut;
			Foundation::Vec2 miter;
			bool			 notch = false; // A reflex vertex: the outline turns away from its inside
		};

		// The corners of `outline`; false for fewer than three distinct points or zero area.
		bool outlineCorners(std::span<const Foundation::Vec2> outline, std::vector<OutlineCorner>& out) {
			out.clear();
			static thread_local std::vector<Foundation::Vec2> points;
			distinctPoints(outline, true, points);
			const size_t count = points.size();
			if (count < 3) {
				return false;
			}
			float twiceArea = 0.0F;
			for (size_t i = 0; i < count; ++i) {
				twiceArea += cross(points[i], points[(i + 1) % count]);
			}
			if (twiceArea == 0.0F) {
				return false;
			}
			// With positive signed area the outside is on the right of each edge, (d.y, -d.x),
			// and a convex vertex turns left.
			const float side = twiceArea > 0.0F ? 1.0F : -1.0F;
			const auto	outward = [side](const Foundation::Vec2& direction) {
				 return Foundation::Vec2(direction.y, -direction.x) * side;
			};

			out.reserve(count);
			for (size_t i = 0; i < count; ++i) {
				OutlineCorner corner{.point = points[i]};
				corner.incoming = glm::normalize(corner.point - points[(i + count - 1) % count]);
				corner.outgoing = glm::normalize(points[(i + 1) % count] - corner.point);
				corner.normalIn = outward(corner.incoming);
				corner.normalOut = outward(corner.outgoing);
				corner.notch = cross(corner.incoming, corner.outgoing) * side < 0.0F;
				// The moved edges meet along the normals' bisector at 1 / cos(turn / 2), which
				// is 2 / |sum|; a hairpin has no bisector and points straight on. Only a convex
				// spike is limited: a notch's meeting point is where its border has to reach.
				const Foundation::Vec2 sum = corner.normalIn + corner.normalOut;
				const float			   length = glm::length(sum);
				if (length > kMinSegment) {
					const float reach = 2.0F / length;
					corner.miter = (sum / length) * (corner.notch ? reach : std::min(reach, kMiterLimit));
				} else {
					corner.miter = corner.incoming * kMiterLimit;
				}
				out.push_back(corner);
			}
			return true;
		}

	} // namespace

	void offsetOutline(std::span<const Foundation::Vec2> outline, float distance, std::vector<Foundation::Vec2>& out) {
		out.clear();
		static thread_local std::vector<OutlineCorner> corners;
		if (!outlineCorners(outline, corners)) {
			return;
		}
		out.reserve(corners.size());
		for (const OutlineCorner& corner : corners) {
			out.push_back(corner.point + (corner.miter * distance));
		}
	}

	void strokeOutline(std::span<const Foundation::Vec2> outline, float width, float inset, std::vector<StrokePiece>& pieces) {
		pieces.clear();
		static thread_local std::vector<OutlineCorner> corners;
		if (!(width + inset > 0.0F) || !outlineCorners(outline, corners)) {
			return;
		}

		// The stroke's centre line runs halfway between its outer edge, `width` out, and
		// its inner edge, `inset` in, through each corner moved along its miter, and a
		// notch's joint is mitered exactly. Corners that move onto one point are one joint.
		const float										  centre = (width - inset) * 0.5F;
		static thread_local std::vector<Foundation::Vec2> centreLine;
		static thread_local std::vector<uint8_t>		  notches;
		centreLine.clear();
		notches.clear();
		for (const OutlineCorner& corner : corners) {
			const Foundation::Vec2 joint = corner.point + (corner.miter * centre);
			if (!centreLine.empty() && glm::length(joint - centreLine.back()) < kMinSegment) {
				notches.back() = std::max(notches.back(), static_cast<uint8_t>(corner.notch ? 1 : 0));
				continue;
			}
			centreLine.push_back(joint);
			notches.push_back(corner.notch ? 1 : 0);
		}
		if (centreLine.size() > 1 && glm::length(centreLine.back() - centreLine.front()) < kMinSegment) {
			notches.front() = std::max(notches.front(), notches.back());
			centreLine.pop_back();
			notches.pop_back();
		}
		if (centreLine.size() < 2) {
			return;
		}
		partition(
			centreLine, notches, centreLine.size() > 2, width + inset, Primitives::LineCap::Butt, Primitives::LineJoin::Miter, inset, pieces
		);
	}

} // namespace Renderer
