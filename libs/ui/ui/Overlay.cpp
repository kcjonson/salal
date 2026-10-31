#include "ui/Overlay.h"

#include <algorithm>

namespace ui {

	namespace {
		// Where the content starts along an edge of `length` from `start`, aligned within it.
		float aligned(Align align, float start, float length, float extent) {
			switch (align) {
				case Align::Center:
					return start + ((length - extent) * 0.5F);
				case Align::End:
					return start + length - extent;
				case Align::Start:
					break;
			}
			return start;
		}

		Foundation::Rect onSide(Side side, const Foundation::Rect& anchor, Foundation::Vec2 size, const Anchored& placement) {
			switch (side) {
				case Side::Below:
					return {aligned(placement.align, anchor.x, anchor.width, size.x), anchor.bottom() + placement.gap, size.x, size.y};
				case Side::Above:
					return {aligned(placement.align, anchor.x, anchor.width, size.x), anchor.y - placement.gap - size.y, size.x, size.y};
				case Side::Right:
					return {anchor.right() + placement.gap, aligned(placement.align, anchor.y, anchor.height, size.y), size.x, size.y};
				case Side::Left:
					break;
			}
			return {anchor.x - placement.gap - size.x, aligned(placement.align, anchor.y, anchor.height, size.y), size.x, size.y};
		}

		Side opposite(Side side) {
			switch (side) {
				case Side::Below:
					return Side::Above;
				case Side::Above:
					return Side::Below;
				case Side::Right:
					return Side::Left;
				case Side::Left:
					break;
			}
			return Side::Right;
		}

		// Whether a rect on `side` runs past the viewport, less the margin, on that side.
		bool overflows(Side side, const Foundation::Rect& rect, const Foundation::Rect& viewport, float margin) {
			switch (side) {
				case Side::Below:
					return rect.bottom() > viewport.bottom() - margin;
				case Side::Above:
					return rect.y < viewport.y + margin;
				case Side::Right:
					return rect.right() > viewport.right() - margin;
				case Side::Left:
					break;
			}
			return rect.x < viewport.x + margin;
		}

		float clamped(float at, float extent, float low, float high, float margin) {
			const float lowest = low + margin;
			const float highest = high - margin - extent;
			return highest < lowest ? lowest : std::clamp(at, lowest, highest);
		}
	} // namespace

	// The preferred side, the opposite one when the preferred overflows and the opposite doesn't, then
	// clamped inside the viewport, its top-left edge winning when it can't fit.
	Foundation::Rect
	place(const Foundation::Rect& anchor, Foundation::Vec2 size, const Foundation::Rect& viewport, const Anchored& placement) {
		Foundation::Rect rect = onSide(placement.side, anchor, size, placement);
		if (overflows(placement.side, rect, viewport, placement.margin)) {
			const Side			   flipped = opposite(placement.side);
			const Foundation::Rect other = onSide(flipped, anchor, size, placement);
			if (!overflows(flipped, other, viewport, placement.margin)) {
				rect = other;
			}
		}
		rect.x = clamped(rect.x, rect.width, viewport.x, viewport.right(), placement.margin);
		rect.y = clamped(rect.y, rect.height, viewport.y, viewport.bottom(), placement.margin);
		return rect;
	}

} // namespace ui
