#pragma once

// Grid layout over numbers: CSS Grid Layout 1's placement (8.5) and track sizing (12) with no component
// tree, in taffy's names. The caller passes the container's template and its children's measured sizes and
// gets back a rect per child and the resolved tracks.
//
// - Tracks are px, percent, auto, fr or minmax(min, max). An fr minimum is auto. A percent track in an axis
//   with no definite size is auto; CSS then lays the grid out again at the size that gives, and so does a
//   caller that wants percent tracks resolved.
// - Auto placement is sparse and row first. A child names a start line and a span on either axis; lines
//   count from 1 and 0 places it automatically. Tracks past the template are implicit, sized by
//   gridAutoColumns and gridAutoRows.
// - A child aligns in its area by justifySelf and alignSelf, or the container's justifyItems and
//   alignItems: Stretch fills the area unless the child has a fixed size on that axis, the others size it
//   to fit its content, min(maxContent, max(minContent, area)).
// - Sizes are margin boxes: contributions include margins, and rects are margin boxes in the container's
//   content box.
// - A container size left out sizes that axis to its content (CSS's max-content constraint).
// - Not covered: named lines and areas, negative lines, subgrid, dense packing, repeat(), the min-content,
//   max-content and fit-content() track keywords, baselines, and justifyContent and alignContent: the grid
//   sits at the content box's start and auto tracks stretch into free space, CSS's normal.
//
// Text that wraps measures its height at a width. A caller with wrapping children runs the steps itself:
// placeGridItems, sizeGridTracks for the columns, the heights measured at the columns' widths, then
// sizeGridTracks for the rows and alignGridItems. layoutGrid runs all four on the sizes it is given.

#include <graphics/Rect.h>
#include <math/Types.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ui {

	struct TrackBreadth {
		enum class Unit : std::uint8_t { Auto, Px, Percent, Fr };
		Unit  unit = Unit::Auto;
		float value = 0.0F; // px, percent (25 is 25%) or the fr factor
	};

	// A track's sizing function, minmax(min, max). One breadth is CSS's one-value track: 100px is
	// minmax(100px, 100px), 1fr is minmax(auto, 1fr), auto is minmax(auto, auto).
	struct GridTrack {
		TrackBreadth min;
		TrackBreadth max;

		constexpr GridTrack() = default;
		constexpr GridTrack(TrackBreadth breadth)
			: min{breadth.unit == TrackBreadth::Unit::Fr ? TrackBreadth{} : breadth},
			  max{breadth} {}
		constexpr GridTrack(TrackBreadth min, TrackBreadth max)
			: min{min.unit == TrackBreadth::Unit::Fr ? TrackBreadth{} : min},
			  max{max} {}
	};

	namespace track {
		constexpr TrackBreadth px(float value) {
			return {.unit = TrackBreadth::Unit::Px, .value = value};
		}
		constexpr TrackBreadth percent(float value) {
			return {.unit = TrackBreadth::Unit::Percent, .value = value};
		}
		constexpr TrackBreadth fr(float value) {
			return {.unit = TrackBreadth::Unit::Fr, .value = value};
		}
		inline constexpr TrackBreadth kAuto{};

		constexpr GridTrack minmax(TrackBreadth min, TrackBreadth max) {
			return {min, max};
		}
	} // namespace track

	enum class GridAlign : std::uint8_t { Start, End, Center, Stretch };

	enum class GridAxis : std::uint8_t { Column, Row };

	// A start line and a span on one axis.
	struct GridPlacement {
		std::int16_t  start = 0; // from 1; 0 places automatically
		std::uint16_t span = 1;
	};

	struct GridContainer {
		std::optional<float>	   width; // content box; left out sizes the axis to its content
		std::optional<float>	   height;
		std::span<const GridTrack> gridTemplateColumns;
		std::span<const GridTrack> gridTemplateRows;
		GridTrack				   gridAutoColumns;
		GridTrack				   gridAutoRows;
		float					   columnGap = 0.0F;
		float					   rowGap = 0.0F;
		GridAlign				   justifyItems = GridAlign::Stretch;
		GridAlign				   alignItems = GridAlign::Stretch;
	};

	struct GridItem {
		GridPlacement			 gridColumn;
		GridPlacement			 gridRow;
		Foundation::Vec2		 minContent{0.0F, 0.0F};
		Foundation::Vec2		 maxContent{0.0F, 0.0F};
		std::optional<float>	 width; // fixed sizes: the child's contribution, never stretched
		std::optional<float>	 height;
		std::optional<GridAlign> justifySelf; // left out takes the container's
		std::optional<GridAlign> alignSelf;
	};

	// The tracks an item occupies, from 0.
	struct GridArea {
		std::uint32_t column = 0;
		std::uint32_t row = 0;
		std::uint32_t columnSpan = 1;
		std::uint32_t rowSpan = 1;
	};

	namespace detail {
		enum class TrackKind : std::uint8_t { Fixed, Auto, Flex };

		struct GridTrackState {
			float	  base = 0.0F;
			float	  limit = 0.0F; // growth limit
			float	  planned = 0.0F;
			float	  incurred = 0.0F;
			float	  minValue = 0.0F; // a fixed minimum in px
			float	  maxValue = 0.0F; // a fixed maximum in px, or the fr factor
			TrackKind minKind = TrackKind::Auto;
			TrackKind maxKind = TrackKind::Auto;
			bool	  growable = false; // infinitely growable, for the step after intrinsic maximums
			bool	  frozen = false;
			bool	  touched = false;
		};

		struct GridItemAxis {
			std::uint32_t start = 0;
			std::uint32_t span = 1;
			float		  minimum = 0.0F; // minimum contribution
			float		  minContent = 0.0F;
			float		  maxContent = 0.0F;
			bool		  flexible = false; // spans an fr track
		};
	} // namespace detail

	// Kept between layouts, so a grid laid out every frame allocates only when it grows.
	struct GridLayout {
		std::vector<GridArea>		  areas;   // per item
		std::vector<Foundation::Rect> rects;   // per item
		std::vector<float>			  columns; // track sizes
		std::vector<float>			  rows;
		Foundation::Vec2			  size{0.0F, 0.0F}; // tracks and gaps

		struct Scratch {
			std::vector<detail::GridTrackState> tracks;
			std::vector<detail::GridItemAxis>	items;
			std::vector<std::uint32_t>			order;
			std::vector<std::uint8_t>			occupied;
			std::vector<std::uint32_t>			rowCursor;
			std::vector<float>					columnOffsets;
			std::vector<float>					rowOffsets;
		} scratch;
	};

	// Resolves every item's area and sizes columns and rows to the grid's track counts.
	void placeGridItems(const GridContainer& container, std::span<const GridItem> items, GridLayout& layout);

	// Sizes one axis's tracks from the items' areas and that axis's sizes.
	void sizeGridTracks(GridAxis axis, const GridContainer& container, std::span<const GridItem> items, GridLayout& layout);

	// Positions every item in its area from the sized tracks.
	void alignGridItems(const GridContainer& container, std::span<const GridItem> items, GridLayout& layout);

	void layoutGrid(const GridContainer& container, std::span<const GridItem> items, GridLayout& layout);

} // namespace ui
