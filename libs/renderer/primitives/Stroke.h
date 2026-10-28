#pragma once

// Stroke geometry for drawPolyline and a polygon's border: the convex pieces a
// stroke is drawn with, and an outline pushed outward. Plain math, no GL; the
// batch draws each piece as its polygon, shaded by its SDF.

#include "math/Types.h"
#include "primitives/Primitives.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Renderer { // NOLINT(readability-identifier-naming)

	// One piece of a stroke: the convex polygon it owns, corners[0, cornerCount) in
	// order with positive winding (cross products of consecutive edges >= 0), and the
	// SDF it is shaded with there, a box 2 halfLength long and 2 halfWidth wide around
	// `center`, its length along the unit `axis`, its corners rounded by
	// `cornerRadius`. A box of zero size is flat: coverage 1 over the whole polygon.
	struct StrokePiece {
		static constexpr size_t kMaxCorners = 6;

		Foundation::Vec2						  center{0.0F, 0.0F};
		Foundation::Vec2						  axis{1.0F, 0.0F};
		float									  halfLength = 0.0F;
		float									  halfWidth = 0.0F;
		float									  cornerRadius = 0.0F;
		std::array<Foundation::Vec2, kMaxCorners> corners{};
		uint8_t									  cornerCount = 0;
	};

	// The pieces that stroke `args.points` at `args.width` with its cap, join and
	// closed flag (its colour, id, z and blend are the caller's), `pixel` units to a
	// pixel. Each segment's piece runs between its joints' bisectors: two neighbours
	// share the cut's two end points as the same floats, so the rasteriser gives each
	// pixel on it to one of them, and every pixel of the stroke belongs to one piece.
	// Each piece reaches a pixel past its SDF, so the edge ramp fits: a capsule for a
	// round joint or cap, a box run on to the miter point for a miter, a box for a butt
	// or square cap. A segment whose ends differ (a round joint beside a butt cap) is
	// two pieces, cut across its middle. A segment too short for its joints' cuts
	// leaves both joints uncut, as does a joint within about 8.6 degrees of a hairpin:
	// there the pieces end straight at their SDF's reach and overlap. Points closer
	// than a ten-thousandth of a unit to the one before are dropped; fewer than two
	// points draw nothing, and a closed line with only two left is the open segment
	// between them, capped.
	void strokePolyline(const Primitives::PolylineArgs& args, float pixel, std::vector<StrokePiece>& out);

	// `outline`, a closed polygon of either winding, with every edge moved
	// `distance` outward and each vertex at its moved edges' meeting point. A convex
	// vertex sharper than the miter limit of 4 (an interior angle under about 29
	// degrees) moves only 4 times `distance`; a notch's vertex always meets exactly.
	// Empty for fewer than three distinct points or zero area.
	void offsetOutline(std::span<const Foundation::Vec2> outline, float distance, std::vector<Foundation::Vec2>& out);

	// A polygon's border: a closed stroke along `outline` whose outer edge runs
	// `width` outside it and whose inner edge runs `inset` inside it, under the fill's
	// edge, so no background shows between fill and border. `inset` is a pixel, which
	// is also how far its pieces reach past their SDF. Its joints are mitered up to a
	// right-angle turn and round past it on the outside, and a notch is mitered into
	// its corner whatever its turn. A notch sharper than a right angle that isn't, its
	// joint left uncut (strokePolyline) or its centre line folded back over a feature
	// narrower than the border, gets a flat piece that carries the border into it: the
	// triangle from its edges' inner corners at the joint to where the border's inner
	// edges meet, no further from the joint than 4 times half the stroke's width.
	void strokeOutline(std::span<const Foundation::Vec2> outline, float width, float inset, std::vector<StrokePiece>& pieces);

} // namespace Renderer
