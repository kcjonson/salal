#pragma once

// The popup stack (docs/ui-design.md, "Ordering"): dialogs, menus, selects, context menus, toasts and
// tooltips are entries a component opens with Component::openPopup. An entry is its opener's child,
// so it lives and dies with the opener and its events bubble through it, but it is out of the
// opener's flow, it is a stacking context, and it paints in a slot of its own: entries in open order
// above the scene, latest on top, anything opened from an entry above that entry, and tooltips in a
// tier above the whole stack. Its opener's clips don't reach it. The context places it next to its
// opener after each layout (R12.30), so it follows the opener when that scrolls or moves.

#include <graphics/Rect.h>
#include <math/Types.h>

#include <cstdint>
#include <functional>
#include <optional>

namespace ui {

	// Draw-key slots: the scene is 0, each stack entry takes the next in stack order, and the tiers sit
	// at the top of what a key holds.
	inline constexpr int kMaxStackEntries = 2045;
	inline constexpr int kTooltipSlot = 2046;
	inline constexpr int kDragSlot = 2047; // reserved for the drag service

	enum class PopupKind : std::uint8_t {
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

	// Where an entry goes against its anchor: on `side`, `gap` px off it, aligned along it; flipped to
	// the opposite side when that side overflows the viewport, then kept `margin` px inside it.
	struct Placement {
		Side							side = Side::Below;
		Align							align = Align::Start;
		float							gap = 4.0F;
		float							margin = 8.0F;
		std::optional<Foundation::Vec2> at; // a point in the viewport to anchor on instead of the opener (a context menu)
	};

	struct PopupOptions {
		PopupKind				 kind = PopupKind::Menu;
		int						 player = 0;	// whose: exclusivity and modality are per player
		std::optional<Placement> placement;		// unset, it sits at its `position` in its opener
		bool					 cover = false; // the whole viewport, a dialog's scrim
		// Called when the stack closes it (an outside press, another menu opening); not when code does.
		std::function<void()> onDismiss;
	};

	// R12.30: the screen rect for content of `size` against `anchor`, both in viewport px.
	Foundation::Rect
	place(const Foundation::Rect& anchor, Foundation::Vec2 size, const Foundation::Rect& viewport, const Placement& placement);

} // namespace ui
