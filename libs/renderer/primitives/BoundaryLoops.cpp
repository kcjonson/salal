#include "primitives/BoundaryLoops.h"

#include <algorithm>
#include <utility>

namespace Renderer {

	namespace {

		uint32_t edgeKey(uint32_t from, uint32_t to) {
			return (from << 16U) | to;
		}

		bool fail(BoundaryLoops& out) {
			out.vertices.clear();
			out.starts.clear();
			return false;
		}

	} // namespace

	bool findBoundaryLoops(std::span<const Foundation::Vec2> vertices, std::span<const uint16_t> indices, BoundaryLoops& out) {
		out.vertices.clear();
		out.starts.clear();

		// Every directed edge, sorted, so an edge's reverse and its repeats are a search away.
		static thread_local std::vector<uint32_t> edges;
		edges.clear();
		for (size_t i = 0; i + 2 < indices.size(); i += 3) {
			const uint16_t a = indices[i];
			uint16_t	   b = indices[i + 1];
			uint16_t	   c = indices[i + 2];
			if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) {
				return fail(out);
			}
			const Foundation::Vec2 ab = vertices[b] - vertices[a];
			const Foundation::Vec2 ac = vertices[c] - vertices[a];
			if ((ab.x * ac.y) - (ab.y * ac.x) < 0.0F) {
				std::swap(b, c);
			}
			edges.push_back(edgeKey(a, b));
			edges.push_back(edgeKey(b, c));
			edges.push_back(edgeKey(c, a));
		}
		std::sort(edges.begin(), edges.end());

		// The boundary vertex after each one, or -1.
		static thread_local std::vector<int32_t> next;
		next.assign(vertices.size(), -1);
		bool anyBoundary = false;
		for (size_t e = 0; e < edges.size();) {
			size_t runEnd = e + 1;
			while (runEnd < edges.size() && edges[runEnd] == edges[e]) {
				++runEnd;
			}
			const uint32_t from = edges[e] >> 16U;
			const uint32_t to = edges[e] & 0xFFFFU;
			if (!std::binary_search(edges.begin(), edges.end(), edgeKey(to, from))) {
				if (runEnd - e > 1 || next[from] >= 0) {
					return fail(out);
				}
				next[from] = static_cast<int32_t>(to);
				anyBoundary = true;
			}
			e = runEnd;
		}
		if (!anyBoundary) {
			return fail(out);
		}

		// Walk each loop until it comes back to where it started; a walk that stops
		// anywhere else is an open chain.
		for (size_t first = 0; first < next.size(); ++first) {
			if (next[first] < 0) {
				continue;
			}
			out.starts.push_back(static_cast<uint32_t>(out.vertices.size()));
			size_t at = first;
			while (next[at] >= 0) {
				out.vertices.push_back(static_cast<uint16_t>(at));
				const int32_t to = next[at];
				next[at] = -1;
				at = static_cast<size_t>(to);
			}
			if (at != first) {
				return fail(out);
			}
		}
		out.starts.push_back(static_cast<uint32_t>(out.vertices.size()));
		return true;
	}

} // namespace Renderer
