#pragma once

// Grid's placement and track sizing as plain functions over numbers, with no component tree: CSS Grid
// Layout Level 1's grid item placement algorithm (8.5), row first and sparse, and its track sizing
// algorithm (11.4 to 11.8). The layout pass (LayoutEngine.cpp) measures the children and calls these.
//
// Out, as docs/ui-design.md leaves them: named lines and areas, repeat(), the min-content, max-content
// and fit-content() track sizes, gridAutoColumns and gridAutoRows (implicit tracks are auto, CSS's
// initial value), dense packing and column-first flow, subgrid, baselines, and justifyContent and
// alignContent on a grid (CSS's normal: the tracks start at the content box's start and auto tracks
// stretch into free space).

#include "ui/Layout.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ui::detail {

	// The tracks an item covers on one axis, the first counted from 0 at the implicit grid's start, so
	// after any implicit tracks a negative line put before the explicit grid.
	struct TrackRange {
		std::uint32_t start = 0;
		std::uint32_t count = 1;

		[[nodiscard]] std::uint32_t end() const { return start + count; }
		bool						operator==(const TrackRange&) const = default;
	};

	struct GridArea {
		TrackRange column;
		TrackRange row;
	};

	struct GridItemLines {
		GridLines column;
		GridLines row;
	};

	struct GridPlacementArgs {
		std::uint32_t				   explicitColumns = 0; // gridTemplateColumns' and gridTemplateRows' lengths
		std::uint32_t				   explicitRows = 0;
		std::span<const GridItemLines> items;
	};

	// The implicit grid placement leaves: each item's area and each axis's track count, of which the
	// first `columnsBefore` and `rowsBefore` are implicit tracks before the explicit grid.
	struct PlacedGrid {
		std::vector<GridArea> areas; // per item, in their order
		std::uint32_t		  columns = 0;
		std::uint32_t		  rows = 0;
		std::uint32_t		  columnsBefore = 0;
		std::uint32_t		  rowsBefore = 0;
	};

	void placeGridItems(const GridPlacementArgs& args, PlacedGrid& grid);

	// One axis's tracks for a placed grid: auto, then the template's, then auto to `count`.
	void gridTracks(
		std::span<const TrackSizingFunction> templateTracks,
		std::uint32_t						 before,
		std::uint32_t						 count,
		std::vector<TrackSizingFunction>&	 tracks
	);

	// An item on the axis being sized, in outer sizes: its margins added, its own min and max sizes
	// applied.
	struct TrackContribution {
		TrackRange tracks;
		float	   minContent = 0.0F;
		float	   maxContent = 0.0F;
		// Its minimum contribution where that isn't CSS's automatic minimum: its size when its own size
		// is definite, or its minSize. Left out, the automatic minimum (6.6): its min-content where it
		// spans an auto minimum and, spanning more than one track, no fr track, at most the spanned
		// tracks' fixed maximums when they all have one; 0 otherwise.
		std::optional<float> minimum;
	};

	struct GridTrackSizing {
		std::span<const TrackSizingFunction> tracks;
		std::optional<float>				 available; // the content box on this axis; left out, the tracks size to their content
		float								 gap = 0.0F;
		std::span<const TrackContribution>	 items;
	};

	// Each track's size, into `sizes`.
	void sizeGridTracks(const GridTrackSizing& sizing, std::vector<float>& sizes);

	// What `sizes` and the gaps between them add up to.
	float gridExtent(std::span<const float> sizes, float gap);

} // namespace ui::detail
