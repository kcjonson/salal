#pragma once

// Stroke geometry for drawPolyline and a polygon's border: the boxes a stroked
// polyline is drawn with, and an outline pushed outward. Plain math, no GL; the
// batch draws each piece as one anti-aliased SDF quad.

#include "math/Types.h"
#include "primitives/Primitives.h"
#include <span>
#include <vector>

namespace Renderer { // NOLINT(readability-identifier-naming)

	// One quad of a stroke: a box 2 halfLength long and 2 halfWidth wide around
	// `center`, its length along the unit `axis`, its corners rounded by
	// `cornerRadius`. A round joint or cap is a disk: all three equal.
	struct StrokePiece {
		Foundation::Vec2 center{0.0F, 0.0F};
		Foundation::Vec2 axis{1.0F, 0.0F};
		float			 halfLength = 0.0F;
		float			 halfWidth = 0.0F;
		float			 cornerRadius = 0.0F;
	};

	// The pieces that stroke `args.points` at `args.width` with its cap, join and
	// closed flag (its colour, id, z and blend are the caller's). Segments are
	// butt-ended boxes, extended by half the width for a square cap or to the miter
	// point at a mitered joint; every joint and round cap adds a disk, which also
	// covers the seam where two butt ends meet. Points closer than a ten-thousandth
	// of a unit to the one before are dropped; fewer than two points draw nothing.
	void strokePolyline(const Primitives::PolylineArgs& args, std::vector<StrokePiece>& out);

	// `outline`, a closed polygon of either winding, with every edge moved
	// `distance` outward and each vertex at its moved edges' meeting point. A
	// vertex sharper than the miter limit of 4 (an interior angle under about 29
	// degrees) moves only 4 times `distance`. Empty for fewer than three distinct
	// points or zero area.
	void offsetOutline(std::span<const Foundation::Vec2> outline, float distance, std::vector<Foundation::Vec2>& out);

} // namespace Renderer
