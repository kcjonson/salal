#pragma once

// The ui tree snapshot, /api/ui/tree (R13.21 to R13.24). One JSON node per component, every node with
// the same fields in every game, so one agent toolkit reads them all; a value that is unknown, or
// doesn't apply to the type, is null, never zero (docs/ui-design.md, "Remaining rules"):
//
//   id            null when the component has none
//   type          typeName()
//   bounds        the box in the parent's space; screenBounds the box in the viewport, transformed
//   margin        per side
//   zIndex        its own, null when unset; isolation; key, its draw key, the paint order
//   visible, enabled, opacity (effective), clip (effective, null for none), contentOffset, transform
//   focusable, pointerEvents (its own, resolved), handlesPointer, state (hovered and pressed; the
//                 rest come with focus), value
//   scroll        a scroller's offset and how far it goes, {x, y, maxX, maxY}; null on anything else,
//                 as is contentOffset, the scroll as its children see it
//   text          a Text's content, measured extent and line count, and overflow
//   style         the fill, text and border colours it resolves to
//   inkBounds     where it may draw in the viewport, its ink extent included
//   children      in tree order
//
// The document carries the scene's name (null for none) and the logical viewport, in whole pixels.
// Serialising never throws on a non-UTF-8 id; it replaces the bad bytes, since it runs in the frame.

#include <math/Types.h>

#include <string>
#include <string_view>

namespace ui {

	class Context;

	// `context` null: a scene without ui, `{"scene":..., "viewport":..., "roots":[]}`.
	[[nodiscard]] std::string treeJson(const Context* context, std::string_view scene, Foundation::Vec2 viewport);

} // namespace ui
