#pragma once

// The overlay stack (docs/ui-design.md, "Ordering"): dialogs, menus, selects, context menus, toasts and
// tooltips are entries a component opens with Component::openOverlay. An entry is its opener's child,
// so it lives and dies with the opener and its events bubble through it, but it is out of the
// opener's flow, it is a stacking context, and it paints in a slot of its own: entries in open order
// above the scene, latest on top, anything opened from an entry above that entry, and tooltips in a
// tier above the whole stack. Its opener's clips don't reach it. It is placed one of two ways, both
// after each layout: anchored to an origin in its opener and moving with it, or absolute, in screen
// coordinates whatever its opener does.

#include <graphics/Rect.h>
#include <math/Types.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <variant>

namespace ui {

	// Draw-key slots: the scene is 0, each stack entry takes the next in stack order, and the tiers sit
	// at the top of what a key holds.
	inline constexpr int kMaxStackEntries = 2045;
	inline constexpr int kTooltipSlot = 2046;
	inline constexpr int kDragSlot = 2047; // reserved for the drag service

	enum class OverlayKind : std::uint8_t {
		// Exclusive for its player (opening one closes the others not holding it); closed by a press
		// outside it, which it consumes (R9.13).
		Menu,
		// Modal for its player: what's under it takes no pointer; a press outside doesn't close it.
		Dialog,
		// Neither modal nor closed by a press; brought to the front as it arrives.
		Toast,
		// In the tooltip tier, above every entry, and never hit by the pointer.
		Tooltip,
	};

	enum class Side : std::uint8_t { Below, Above, Right, Left };
	enum class Align : std::uint8_t { Start, Center, End };

	// From an origin, moving with it (R3.8): on `side` of the opener's box, or of `point` in the
	// opener's space, `gap` px off it and aligned along it; flipped to the opposite side when that
	// side overflows the viewport, then kept `margin` px inside it (R12.30). It follows the opener as
	// that scrolls, moves or turns, under the opener's transform and opacity.
	struct Anchored {
		Side							side = Side::Below;
		Align							align = Align::Start;
		float							gap = 4.0F;
		float							margin = 8.0F;
		std::optional<Foundation::Vec2> point; // instead of the opener's box: a context menu at the press
	};

	// In screen px, whatever its opener does: its top-left at `at`, kept `margin` px inside the
	// viewport when that is set, or over the whole viewport. The opener's position, transform and
	// opacity play no part, though it still owns the overlay.
	struct Absolute {
		Foundation::Vec2	 at{0.0F, 0.0F};
		std::optional<float> margin;
		bool				 cover = false; // a dialog's scrim
	};

	struct OverlayOptions {
		OverlayKind						 kind = OverlayKind::Menu;
		int								 player = 0; // whose: exclusivity and modality are per player
		std::variant<Anchored, Absolute> placement = Anchored{};
		// Called when the stack closes it (an outside press, another menu opening); not when code does.
		std::function<void()> onDismiss;
	};

	// R12.30: the screen rect for content of `size` against `anchor`, both in viewport px.
	Foundation::Rect
	place(const Foundation::Rect& anchor, Foundation::Vec2 size, const Foundation::Rect& viewport, const Anchored& placement);

} // namespace ui
