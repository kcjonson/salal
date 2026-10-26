#include "primitives/Stroke.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace Renderer {

	namespace {

		constexpr float kMinSegment = 1e-4F;
		constexpr float kMiterLimit = 4.0F;

		float cross(const Foundation::Vec2& a, const Foundation::Vec2& b) {
			return (a.x * b.y) - (a.y * b.x);
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

		StrokePiece disk(const Foundation::Vec2& center, float radius) {
			return {.center = center, .axis = {1.0F, 0.0F}, .halfLength = radius, .halfWidth = radius, .cornerRadius = radius};
		}

	} // namespace

	void strokePolyline(const Primitives::PolylineArgs& args, std::vector<StrokePiece>& out) {
		out.clear();
		const float halfWidth = args.width * 0.5F;
		if (!(halfWidth > 0.0F)) {
			return;
		}

		static thread_local std::vector<Foundation::Vec2> points;
		distinctPoints(args.points, args.closed, points);
		const size_t count = points.size();
		if (count < 2) {
			return;
		}
		// Two points closed are one segment there and back: draw the segment.
		const bool	 closed = args.closed && count > 2;
		const size_t segmentCount = closed ? count : count - 1;

		// Per segment, its unit direction and how far it runs past each end point.
		static thread_local std::vector<Foundation::Vec2> directions;
		static thread_local std::vector<float>			  startReach;
		static thread_local std::vector<float>			  endReach;
		directions.resize(segmentCount);
		startReach.assign(segmentCount, 0.0F);
		endReach.assign(segmentCount, 0.0F);
		for (size_t i = 0; i < segmentCount; ++i) {
			directions[i] = glm::normalize(points[(i + 1) % count] - points[i]);
		}

		if (!closed) {
			if (args.cap == Primitives::LineCap::Square) {
				startReach.front() = halfWidth;
				endReach.back() = halfWidth;
			} else if (args.cap == Primitives::LineCap::Round) {
				out.push_back(disk(points.front(), halfWidth));
				out.push_back(disk(points.back(), halfWidth));
			}
		}

		// Joint j is the point between segment j - 1 (in) and segment j (out).
		const size_t firstJoint = closed ? 0 : 1;
		const size_t jointEnd = closed ? count : count - 1;
		for (size_t joint = firstJoint; joint < jointEnd; ++joint) {
			const size_t in = (joint + segmentCount - 1) % segmentCount;
			const size_t outgoing = joint % segmentCount;
			out.push_back(disk(points[joint], halfWidth));
			if (args.join == Primitives::LineJoin::Miter) {
				// Up to a right-angle turn, each segment run on by halfWidth * tan(turn / 2)
				// puts its outer corner on the miter point, and the two boxes together are the
				// mitered joint. Past a right angle a box would poke out beyond the miter, so
				// the joint keeps the disk alone.
				const float cosTurn = glm::dot(directions[in], directions[outgoing]);
				if (cosTurn >= 0.0F) {
					const float reach = halfWidth * std::abs(cross(directions[in], directions[outgoing])) / (1.0F + cosTurn);
					endReach[in] = reach;
					startReach[outgoing] = reach;
				}
			}
		}

		for (size_t i = 0; i < segmentCount; ++i) {
			const Foundation::Vec2& direction = directions[i];
			const Foundation::Vec2	start = points[i] - (direction * startReach[i]);
			const Foundation::Vec2	end = points[(i + 1) % count] + (direction * endReach[i]);
			out.push_back(
				{.center = (start + end) * 0.5F,
				 .axis = direction,
				 .halfLength = glm::length(end - start) * 0.5F,
				 .halfWidth = halfWidth,
				 .cornerRadius = 0.0F}
			);
		}
	}

	void offsetOutline(std::span<const Foundation::Vec2> outline, float distance, std::vector<Foundation::Vec2>& out) {
		out.clear();
		static thread_local std::vector<Foundation::Vec2> points;
		distinctPoints(outline, true, points);
		const size_t count = points.size();
		if (count < 3) {
			return;
		}

		float twiceArea = 0.0F;
		for (size_t i = 0; i < count; ++i) {
			twiceArea += cross(points[i], points[(i + 1) % count]);
		}
		if (twiceArea == 0.0F) {
			return;
		}
		// With positive signed area the outside is on the right of each edge, (d.y, -d.x).
		const float side = twiceArea > 0.0F ? 1.0F : -1.0F;
		const auto	outward = [side](const Foundation::Vec2& direction) {
			 return Foundation::Vec2(direction.y, -direction.x) * side;
		};

		out.reserve(count);
		for (size_t i = 0; i < count; ++i) {
			const Foundation::Vec2& point = points[i];
			const Foundation::Vec2	incoming = glm::normalize(point - points[(i + count - 1) % count]);
			const Foundation::Vec2	outgoing = glm::normalize(points[(i + 1) % count] - point);
			const Foundation::Vec2	sum = outward(incoming) + outward(outgoing);
			const float				length = glm::length(sum);
			// The moved edges meet along the normals' bisector at 1 / cos(turn / 2), which
			// is 2 / |sum|; a hairpin has no bisector and points straight on.
			const Foundation::Vec2 bisector = length > kMinSegment ? sum / length : incoming;
			const float			   reach = length > kMinSegment ? std::min(2.0F / length, kMiterLimit) : kMiterLimit;
			out.push_back(point + (bisector * (distance * reach)));
		}
	}

} // namespace Renderer
