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
//   None  the component and its subtree take no room and don't draw, as `visible: false`.
//
// CSS's Block, normal flow, isn't built: a Flex column stacks, and Block comes when a game asks.
//
// Sizes are px, a percentage of the parent's box (inside its padding, for a flex child), or auto: a
// text's or a container's content, a stretched or grown flex child's share. An assigned size never
// changes the declared one, so an auto axis is measured again whenever its content changes.

#include <math/Types.h>

#include <cstdint>
#include <optional>

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

	// An absolutely positioned child's offsets from its parent's edges, px, each optional. Left and
	// right both set stretch an auto width between them; right alone anchors to the right edge. On a
	// relatively positioned flex child they nudge it from where the flow put it.
	struct Inset {
		std::optional<float> top;
		std::optional<float> right;
		std::optional<float> bottom;
		std::optional<float> left;

		bool operator==(const Inset&) const = default;
	};

	enum class Display : std::uint8_t { Free, Flex, None };
	enum class FlexDirection : std::uint8_t { Row, Column };
	// alignItems on a flex container, alignSelf on one child. Stretch fills the cross axis for a child
	// whose cross size is auto and starts one whose isn't.
	enum class AlignItems : std::uint8_t { Start, Center, End, Stretch };
	enum class JustifyContent : std::uint8_t { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
	enum class Position : std::uint8_t { Relative, Absolute };
	// Clip cuts the children at the box; Scroll cuts them too and scrolls them, by the wheel and a
	// thumb, over their union plus the padding (R4.13).
	enum class Overflow : std::uint8_t { Visible, Clip, Scroll };

	struct Layout {
		// As a container.
		Display		   display = Display::Free;
		FlexDirection  flexDirection = FlexDirection::Row;
		float		   gap = 0.0F; // px between flex children; negative overlaps them
		Edges		   padding;
		AlignItems	   alignItems = AlignItems::Stretch;
		JustifyContent justifyContent = JustifyContent::Start;
		// As a child.
		float					  flexGrow = 0.0F;
		float					  flexShrink = 1.0F;
		std::optional<AlignItems> alignSelf;
		Position				  position = Position::Relative;
		Inset					  inset;

		bool operator==(const Layout&) const = default;
	};

} // namespace ui
