#include "primitives/Stroke.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <glm/geometric.hpp>
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

		float segmentDistance(const Foundation::Vec2& point, const Foundation::Vec2& from, const Foundation::Vec2& to) {
			const Foundation::Vec2 along = to - from;
			const float			   lengthSquared = glm::dot(along, along);
			const float			   t = lengthSquared > 0.0F ? std::clamp(glm::dot(point - from, along) / lengthSquared, 0.0F, 1.0F) : 0.0F;
			return glm::length(point - (from + (along * t)));
		}

		// `points` without the interior points that lie within `tolerance` of the segment that
		// would replace them, so input noise (integer mouse samples, a hand's jitter) doesn't
		// leave segments too short for the turns between them. Every dropped point stays
		// within `tolerance` of the line drawn; a run is at most kMaxDropped points, which
		// keeps this linear. The first and last points stay.
		void dropNearlyStraightPoints(std::vector<Foundation::Vec2>& points, float tolerance) {
			constexpr size_t kMaxDropped = 16;
			if (!(tolerance > 0.0F) || points.size() < 3) {
				return;
			}
			size_t kept = 1; // points[0, kept) are the result so far
			size_t anchor = 0;
			for (size_t i = 1; i + 1 < points.size(); ++i) {
				bool drop = i - anchor <= kMaxDropped;
				for (size_t j = anchor + 1; drop && j <= i; ++j) {
					drop = segmentDistance(points[j], points[anchor], points[i + 1]) < tolerance;
				}
				if (!drop) {
					// The run of dropped points is measured against the original anchor.
					points[kept++] = points[i];
					anchor = i;
				}
			}
			points[kept++] = points.back();
			points.resize(kept);
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
			assert(piece.cornerCount < StrokePiece::kMaxCorners);
			piece.corners[piece.cornerCount++] = corner;
		}

		// A stroke to lay out: distinct points, at least two and more than two when closed,
		// and when `exact` isn't empty a flag per point, its joint mitered whatever its turn
		// while it is cut.
		struct Layout {
			std::span<const Foundation::Vec2> points;
			std::span<const uint8_t>		  exact;
			bool							  closed = false;
			float							  width = 0.0F;
			Primitives::LineCap				  cap = Primitives::LineCap::Butt;
			Primitives::LineJoin			  join = Primitives::LineJoin::Round;
			float							  pixel = 1.0F;
		};

		// The pieces for `layout`, and in `cut` a flag per point: whether its joint ended up cut.
		void partition(const Layout& layout, std::vector<StrokePiece>& out, std::vector<uint8_t>& cut) {
			out.clear();
			const std::span<const Foundation::Vec2> points = layout.points;
			const std::span<const uint8_t>			exact = layout.exact;
			const bool								closed = layout.closed;
			const Primitives::LineCap				cap = layout.cap;
			const Primitives::LineJoin				join = layout.join;
			const float								pixel = layout.pixel;
			const float								halfWidth = layout.width * 0.5F;
			const float								reach = halfWidth + pixel;
			const size_t							count = points.size();
			const size_t							segmentCount = closed ? count : count - 1;

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
			// How far past its point a joint's SDF runs: a capsule's end, or a box's to the miter
			// point. A cut miter up to a right angle runs its box on to a pixel past where its
			// piece's cut reaches along the segment, so the box's end face and its edge ramp
			// stay out of the piece; within the piece the box is the stroke either way. Past a
			// right angle (a notch) the box stops at the miter point, since a longer one would
			// show past it along the bisector.
			const auto extent = [&](const Joint& joint) {
				if (!mitered(joint)) {
					return halfWidth;
				}
				const float tanHalf = joint.sinHalf / joint.cosHalf;
				return joint.cut && !joint.pastRightAngle ? (reach * tanHalf) + pixel : halfWidth * tanHalf;
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

			// Each segment's pieces, laid out into its own slot. A segment whose pieces come out
			// concave, too short for its joints' cuts, uncuts both joints and is laid out again,
			// and so is the segment on the other side of each. A joint uncuts once, so no segment
			// is laid out more than three times, however the uncutting spreads.
			static thread_local std::vector<std::array<StrokePiece, 2>> slots;
			static thread_local std::vector<uint8_t>					slotCounts;
			static thread_local std::vector<uint8_t>					queued;
			static thread_local std::vector<size_t>						pending;
			slots.resize(segmentCount);
			slotCounts.assign(segmentCount, 0);
			queued.assign(segmentCount, 1);
			pending.clear();
			for (size_t i = segmentCount; i-- > 0;) {
				pending.push_back(i);
			}
			const auto enqueue = [&](size_t i) {
				if (queued[i] == 0) {
					queued[i] = 1;
					pending.push_back(i);
				}
			};
			// Lays segment i into its slot; true when a piece came out concave.
			const auto lay = [&](size_t i) {
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

				uint8_t& laid = slotCounts[i];
				laid = 0;
				if (startRound == endRound) {
					StrokePiece piece;
					addEnd(piece, start, t, n, length, endJoint, endExtent);
					addStart(piece, start, t, n, startJoint, startExtent);
					shade(piece, start, t, -startExtent, length + endExtent, startRound);
					slots[i][laid++] = piece;
				} else {
					// The halves meet halfway along the free stretch between how far the
					// start's cut reaches along the segment and where the end's begins (a
					// pixel in from a cap or an uncut joint), which gives both cuts the most
					// room. Each half's SDF runs on a reach past the middle, so its own end
					// is nowhere near the cut.
					float from = pixel;
					float to = length - pixel;
					if (startJoint != nullptr && startJoint->cut) {
						from = std::max(glm::dot(startJoint->innerCut - start, t), glm::dot(startJoint->outerCut - start, t));
					}
					if (endJoint != nullptr && endJoint->cut) {
						to = std::min(glm::dot(endJoint->innerCut - start, t), glm::dot(endJoint->outerCut - start, t));
					}
					const float			   middle = from < to ? (from + to) * 0.5F : length * 0.5F;
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
					slots[i][laid++] = first;
					slots[i][laid++] = second;
				}
				return std::any_of(slots[i].begin(), slots[i].begin() + laid, [&](const StrokePiece& piece) {
					return !convex(piece, start, t, reach);
				});
			};
			while (!pending.empty()) {
				const size_t i = pending.back();
				pending.pop_back();
				queued[i] = 0;
				if (!lay(i)) {
					continue;
				}
				// Joint i is between segments i - 1 and i, joint i + 1 between i and i + 1.
				const size_t previous = (i + segmentCount - 1) % segmentCount;
				const size_t next = (i + 1) % segmentCount;
				const bool	 hasStart = closed || i > 0;
				const bool	 hasEnd = closed || i + 1 < segmentCount;
				if (hasStart && joints[i].cut) {
					joints[i].cut = false;
					enqueue(i);
					enqueue(previous);
				}
				if (hasEnd && joints[(i + 1) % count].cut) {
					joints[(i + 1) % count].cut = false;
					enqueue(i);
					enqueue(next);
				}
			}
			for (size_t i = 0; i < segmentCount; ++i) {
				out.insert(out.end(), slots[i].begin(), slots[i].begin() + slotCounts[i]);
			}

			cut.assign(count, 0);
			for (size_t j = firstJoint; j < jointEnd; ++j) {
				cut[j] = joints[j].cut ? 1 : 0;
			}
		}

	} // namespace

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

	void strokePolyline(const Primitives::PolylineArgs& args, float pixel, float tolerance, std::vector<StrokePiece>& out) {
		out.clear();
		if (!(args.width > 0.0F)) {
			return;
		}
		static thread_local std::vector<Foundation::Vec2> points;
		distinctPoints(args.points, args.closed, points);
		dropNearlyStraightPoints(points, tolerance);
		if (points.size() < 2) {
			return;
		}
		// Two points closed are one segment there and back: draw the segment.
		static thread_local std::vector<uint8_t> cut;
		partition(
			{.points = points,
			 .closed = args.closed && points.size() > 2,
			 .width = args.width,
			 .cap = args.cap,
			 .join = args.join,
			 .pixel = pixel},
			out,
			cut
		);
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
		// its inner edge, `inset` in, through each corner moved along its miter. Corners
		// that move onto one point are one joint, and no corner's: -1 in `cornerOf`.
		const float										  centre = (width - inset) * 0.5F;
		const float										  halfWidth = (width + inset) * 0.5F;
		static thread_local std::vector<Foundation::Vec2> centreLine;
		static thread_local std::vector<int32_t>		  cornerOf;
		centreLine.clear();
		cornerOf.clear();
		for (size_t i = 0; i < corners.size(); ++i) {
			const Foundation::Vec2 joint = corners[i].point + (corners[i].miter * centre);
			if (!centreLine.empty() && glm::length(joint - centreLine.back()) < kMinSegment) {
				cornerOf.back() = -1;
				continue;
			}
			centreLine.push_back(joint);
			cornerOf.push_back(static_cast<int32_t>(i));
		}
		if (centreLine.size() > 1 && glm::length(centreLine.back() - centreLine.front()) < kMinSegment) {
			cornerOf.front() = -1;
			centreLine.pop_back();
			cornerOf.pop_back();
		}
		const size_t count = centreLine.size();
		if (count < 2) {
			return;
		}

		// A notch's joint is mitered into its corner whatever its turn where the centre line
		// runs the outline's way on both sides of it. Where it doesn't, the outline's feature
		// is narrower than the border, the centre line folds back over it, and the joint is
		// drawn as any other.
		static thread_local std::vector<uint8_t> exact;
		exact.assign(count, 0);
		for (size_t k = 0; count > 2 && k < count; ++k) {
			if (cornerOf[k] < 0 || !corners[static_cast<size_t>(cornerOf[k])].notch) {
				continue;
			}
			const OutlineCorner&   corner = corners[static_cast<size_t>(cornerOf[k])];
			const Foundation::Vec2 before = centreLine[k] - centreLine[(k + count - 1) % count];
			const Foundation::Vec2 after = centreLine[(k + 1) % count] - centreLine[k];
			exact[k] = glm::dot(before, corner.incoming) > 0.0F && glm::dot(after, corner.outgoing) > 0.0F ? 1 : 0;
		}
		static thread_local std::vector<uint8_t> cut;
		partition(
			{.points = centreLine,
			 .exact = exact,
			 .closed = count > 2,
			 .width = width + inset,
			 .cap = Primitives::LineCap::Butt,
			 .join = Primitives::LineJoin::Miter,
			 .pixel = inset},
			pieces,
			cut
		);

		// A notch sharper than a right angle that no exact miter draws keeps the flat triangle
		// that carries the border into it, from its two edges' inner corners at the joint to
		// where the border's inner edges meet, `inset` inside the corner along its miter, but
		// no further from the joint than the miter limit allows.
		for (size_t k = 0; k < count; ++k) {
			if (cornerOf[k] < 0) {
				continue;
			}
			const OutlineCorner& corner = corners[static_cast<size_t>(cornerOf[k])];
			if (!corner.notch || glm::dot(corner.incoming, corner.outgoing) >= 0.0F || (exact[k] != 0 && cut[k] != 0)) {
				continue;
			}
			const Foundation::Vec2& joint = centreLine[k];
			const float				miterLength = glm::length(corner.miter);
			const Foundation::Vec2	tip = joint - (corner.miter * (halfWidth * std::min(1.0F, kMiterLimit / miterLength)));
			Foundation::Vec2		inCorner = joint - (corner.normalIn * halfWidth);
			Foundation::Vec2		outCorner = joint - (corner.normalOut * halfWidth);
			if (cross(inCorner - tip, outCorner - inCorner) < 0.0F) {
				std::swap(inCorner, outCorner);
			}
			StrokePiece wedge{.center = joint};
			addCorner(wedge, tip);
			addCorner(wedge, inCorner);
			addCorner(wedge, outCorner);
			pieces.push_back(wedge);
		}
	}

} // namespace Renderer
