#pragma once

// The boundary of an indexed triangle mesh as closed loops of vertex indices: what a
// fill's feather ring runs along (drawPolygon, drawTriangles with antiAlias).

#include "math/Types.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Renderer { // NOLINT(readability-identifier-naming)

	// Loop k is vertices[starts[k], starts[k + 1]), so `starts` ends with
	// vertices.size(); no loops, no starts. Every loop keeps the mesh on its left,
	// (-d.y, d.x) of each edge's direction d, whatever winding its triangles have.
	struct BoundaryLoops {
		std::vector<uint16_t> vertices;
		std::vector<uint32_t> starts;

		bool   empty() const { return starts.empty(); }
		size_t count() const { return starts.empty() ? 0 : starts.size() - 1; }
	};

	// The edges of the triangles in `indices` (each turned to positive winding over
	// `vertices` first, a zero-area one to whichever winding runs back along more of its
	// neighbours' edges) that no other triangle runs back along, chained into loops.
	// False, with `out` empty, when they don't chain: a vertex that starts two boundary
	// edges (a pinch, where two parts of the mesh meet at a point), a boundary edge two
	// triangles run the same way, an index past `vertices`, or no boundary at all.
	bool findBoundaryLoops(std::span<const Foundation::Vec2> vertices, std::span<const uint16_t> indices, BoundaryLoops& out);

} // namespace Renderer
