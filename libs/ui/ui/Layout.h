#pragma once

// Layout's vocabulary, with CSS's names as taffy uses them (docs/ui-design.md, "Component API").
//
// A component's `display` decides how its children are placed:
//
//   Free  the default. Each child sits at its own `position` in the parent's box; an auto size hugs
//         the children's boxes. This is the free placement every screen had before layout.
//   Flex  children flow along flexDirection, spaced by gap inside padding, sized by flexGrow and
//         flexShrink on the main axis and alignItems or alignSelf on the cross one. A child with
//         `position: Absolute` leaves the flow and is placed as under Free, or by `inset`.
//   Grid  children sit in the cells of gridTemplateColumns and gridTemplateRows, inside padding with
//         gap between tracks both ways: each where its gridColumn and gridRow put it, or flowed row by
//         row into the next free cells, past the template into auto tracks (CSS Grid Layout 1's
//         placement and track sizing, ui/detail/Grid.h). alignItems and justifyItems, or a child's
//         alignSelf and justifySelf, align it in its area. `position: Absolute` is as under Flex.
//   None  the component and its subtree take no room and don't draw, as `visible: false`.
//
// CSS's Block, normal flow, isn't built: a Flex column stacks, and Block comes when a game asks.
//
// Sizes are px, a percentage of the parent's box (inside its padding, for a flex or grid child), or
// auto: a text's or a container's content, a stretched or grown flex child's share, a stretched grid
// child's area. An assigned size never changes the declared one, so an auto axis is measured again
// whenever its content changes.

#include <math/Types.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace ui {

	// One axis of a size: px, a percentage of the parent's box, or auto.
	struct Length {
		enum class Unit : std::uint8_t { Auto, Px, Percent };

		float value = 0.0F;
		Unit  unit = Unit::Auto;

		constexpr Length() = default;
		constexpr Length(float px) // NOLINT(google-explicit-constructor): .size = {100.0F, 16.0F}
			: value(px),
			  unit(Unit::Px) {}

		[[nodiscard]] constexpr bool isAuto() const { return unit == Unit::Auto; }
		// Px, or the percentage of `parent`; none for auto, or a percentage of a parent not yet known
		// (parent < 0).
		[[nodiscard]] constexpr std::optional<float> resolve(float parent) const {
			switch (unit) {
				case Unit::Px:
					return value;
				case Unit::Percent:
					return parent >= 0.0F ? std::optional(parent * value / 100.0F) : std::nullopt;
				case Unit::Auto:
					break;
			}
			return std::nullopt;
		}

		bool operator==(const Length&) const = default;
	};

	inline constexpr Length kAuto{};

	constexpr Length percent(float value) {
		Length length;
		length.value = value;
		length.unit = Length::Unit::Percent;
		return length;
	}

	struct Size {
		Length width;
		Length height;

		constexpr Size() = default;
		constexpr Size(Length widthLength, Length heightLength)
			: width(widthLength),
			  height(heightLength) {}
		Size(Foundation::Vec2 px) // NOLINT(google-explicit-constructor): a size computed as a vector
			: width(px.x),
			  height(px.y) {}
	};

	// Per side, px: margin and padding.
	struct Edges {
		float top = 0.0F;
		float right = 0.0F;
		float bottom = 0.0F;
		float left = 0.0F;

		constexpr Edges() = default;
		constexpr Edges(float all) // NOLINT(google-explicit-constructor): .padding = 8.0F
			: top(all),
			  right(all),
			  bottom(all),
			  left(all) {}
		constexpr Edges(float vertical, float horizontal)
			: top(vertical),
			  right(horizontal),
			  bottom(vertical),
			  left(horizontal) {}
		constexpr Edges(float topEdge, float rightEdge, float bottomEdge, float leftEdge)
			: top(topEdge),
			  right(rightEdge),
			  bottom(bottomEdge),
			  left(leftEdge) {}

		[[nodiscard]] constexpr float			 horizontal() const { return left + right; }
		[[nodiscard]] constexpr float			 vertical() const { return top + bottom; }
		[[nodiscard]] constexpr Foundation::Vec2 topLeft() const { return {left, top}; }
		[[nodiscard]] constexpr Foundation::Vec2 total() const { return {left + right, top + bottom}; }
		bool									 operator==(const Edges&) const = default;
	};

	// An absolutely positioned child's offsets from its parent's edges: px, a percentage of the parent's
	// box (left and right of its width, top and bottom of its height), or auto, which leaves that edge
	// unset. The box is the parent's whole box, its padding included (CSS's padding box). Left and right
	// both set stretch an auto width between them; right alone anchors to the right edge. On a
	// relatively positioned flex or grid child they nudge it from where the flow put it, a percentage
	// being of the flex container's content box or the grid area.
	struct Inset {
		Length top;
		Length right;
		Length bottom;
		Length left;

		bool operator==(const Inset&) const = default;
	};

	enum class Display : std::uint8_t { Free, Flex, Grid, None };
	enum class FlexDirection : std::uint8_t { Row, Column };
	// alignItems on a flex container, alignSelf on one child: the cross axis. On a grid container
	// alignItems and justifyItems, alignSelf and justifySelf on one child: down and across its area.
	// Stretch fills the axis for a child whose size on it is auto and starts one whose isn't; the others
	// size an auto child to its content, wrapped to the room there is. A child that doesn't fit starts.
	enum class AlignItems : std::uint8_t { Start, Center, End, Stretch };
	// A flex container's only: a grid's tracks start at its content box's start.
	enum class JustifyContent : std::uint8_t { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
	enum class Position : std::uint8_t { Relative, Absolute };
	// Clip cuts the children at the box; Scroll cuts them too and scrolls them, by the wheel and a
	// thumb, over their union plus the padding (R4.13).
	enum class Overflow : std::uint8_t { Visible, Clip, Scroll };

	// The maximum of a grid track's sizing function (taffy's MaxTrackSizingFunction): px, a percentage
	// of the grid's content box, auto (the largest max-content of its items), or fr, a share of the
	// space the other tracks leave.
	struct MaxTrackSizingFunction {
		enum class Unit : std::uint8_t { Auto, Px, Percent, Fr };

		float value = 0.0F;
		Unit  unit = Unit::Auto;

		constexpr MaxTrackSizingFunction() = default;
		constexpr MaxTrackSizingFunction(float px) // NOLINT(google-explicit-constructor): minmax(kAuto, 200.0F)
			: value(px),
			  unit(Unit::Px) {}
		constexpr MaxTrackSizingFunction(Length length) // NOLINT(google-explicit-constructor): minmax(kAuto, percent(50))
			: value(length.value),
			  unit(
				  length.unit == Length::Unit::Px		 ? Unit::Px
				  : length.unit == Length::Unit::Percent ? Unit::Percent
														 : Unit::Auto
			  ) {}

		bool operator==(const MaxTrackSizingFunction&) const = default;
	};

	// CSS's fr: a flexible maximum, a share of the leftover by its factor.
	constexpr MaxTrackSizingFunction fr(float factor) {
		MaxTrackSizingFunction maximum;
		maximum.value = factor;
		maximum.unit = MaxTrackSizingFunction::Unit::Fr;
		return maximum;
	}

	// A grid track's sizing function, minmax(min, max) (taffy's TrackSizingFunction). The minimum is
	// px, a percentage of the grid's content box, or auto (the largest minimum size of its items); fr
	// can't be one. One value is CSS's one-value track: 100.0F is minmax(100px, 100px), kAuto is
	// minmax(auto, auto), fr(1) is minmax(auto, 1fr). A maximum under the minimum is the minimum. A
	// percentage is auto while the grid's size on that axis hangs on its tracks, and resolves once the
	// grid is sized.
	struct TrackSizingFunction {
		Length				   min;
		MaxTrackSizingFunction max;

		constexpr TrackSizingFunction() = default;
		constexpr TrackSizingFunction(float px) // NOLINT(google-explicit-constructor): .gridTemplateColumns = {120.0F, fr(1)}
			: min(px),
			  max(px) {}
		constexpr TrackSizingFunction(Length length) // NOLINT(google-explicit-constructor): {kAuto, percent(25)}
			: min(length),
			  max(length) {}
		constexpr TrackSizingFunction(MaxTrackSizingFunction maximum) // NOLINT(google-explicit-constructor): {fr(1), fr(2)}
			: min(maximum.unit == MaxTrackSizingFunction::Unit::Px		  ? Length(maximum.value)
				  : maximum.unit == MaxTrackSizingFunction::Unit::Percent ? percent(maximum.value)
																		  : kAuto),
			  max(maximum) {}
		constexpr TrackSizingFunction(Length minimum, MaxTrackSizingFunction maximum)
			: min(minimum),
			  max(maximum) {}

		bool operator==(const TrackSizingFunction&) const = default;
	};

	constexpr TrackSizingFunction minmax(Length minimum, MaxTrackSizingFunction maximum) {
		return {minimum, maximum};
	}

	// One edge of a grid child's area on one axis (taffy's GridPlacement): auto, a line, or a span of
	// tracks. Made by line() and span(); the default is auto.
	struct GridPlacement {
		enum class Kind : std::uint8_t { Auto, Line, Span };

		Kind		 kind = Kind::Auto;
		std::int16_t value = 0; // the line or the span

		bool operator==(const GridPlacement&) const = default;
	};

	// A grid line: 1 is the explicit grid's first, -1 its last, counting in from either; a line past
	// either end adds implicit tracks to reach it. Line 0 isn't one, as in CSS: it places automatically.
	constexpr GridPlacement line(int index) {
		if (index == 0) {
			return {};
		}
		return {.kind = GridPlacement::Kind::Line, .value = static_cast<std::int16_t>(std::clamp(index, -32767, 32767))};
	}

	// A span of tracks from the other edge, or from where auto placement puts the child. Under 1 isn't
	// one: it places automatically.
	constexpr GridPlacement span(int count) {
		if (count < 1) {
			return {};
		}
		return {.kind = GridPlacement::Kind::Span, .value = static_cast<std::int16_t>(std::min(count, 32767))};
	}

	// gridColumn and gridRow: a child's start and end edges on that axis (taffy's Line<GridPlacement>),
	// CSS's `start / end`. One edge given is the start, as `grid-column: 2` is `2 / auto`. Lines the
	// wrong way round swap, equal lines span one track, and with no line at all the child is placed
	// automatically, spanning the start's span (or the end's, without one).
	struct GridLines {
		GridPlacement start;
		GridPlacement end;

		constexpr GridLines() = default;
		constexpr GridLines(GridPlacement startEdge) // NOLINT(google-explicit-constructor): .gridColumn = line(2)
			: start(startEdge) {}
		constexpr GridLines(GridPlacement startEdge, GridPlacement endEdge)
			: start(startEdge),
			  end(endEdge) {}

		bool operator==(const GridLines&) const = default;
	};

	struct Layout {
		// As a container.
		Display		  display = Display::Free;
		FlexDirection flexDirection = FlexDirection::Row;
		// px between flex children, negative overlapping them; between a grid's columns and between its
		// rows, where a negative one is 0, as CSS has it.
		float		   gap = 0.0F;
		Edges		   padding;
		AlignItems	   alignItems = AlignItems::Stretch;
		AlignItems	   justifyItems = AlignItems::Stretch; // a grid's only
		JustifyContent justifyContent = JustifyContent::Start;
		// A grid's explicit tracks. Auto placement adds rows past gridTemplateRows, and a line past
		// either adds tracks; those implicit tracks are auto.
		std::vector<TrackSizingFunction> gridTemplateColumns;
		std::vector<TrackSizingFunction> gridTemplateRows;
		// As a child.
		float					  flexGrow = 0.0F;
		float					  flexShrink = 1.0F;
		std::optional<AlignItems> alignSelf;
		std::optional<AlignItems> justifySelf; // in a grid
		GridLines				  gridColumn;
		GridLines				  gridRow;
		Position				  position = Position::Relative;
		Inset					  inset;

		bool operator==(const Layout&) const = default;
	};

} // namespace ui
