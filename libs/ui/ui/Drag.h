#pragma once

// Drag and drop (docs/ui-design.md, "Drag and drop"). A component declares itself a drag source or a
// drop target (Component::setDragSource, setDropTarget) and its scene's context runs the gesture: a
// press on a source that travels past kDragThreshold starts a drag and captures the pointer, or a
// player's activate on a focused source picks it up for the keys. The ghost, the source itself unless
// the source builds one, is lifted into the drag tier above everything, where the pointer never hits
// it. The drag is over the innermost target at or above what's under it, which accepts or refuses;
// a release, or the activate, drops it there. The player's cancel, a second button, the window
// losing focus, the scene being covered or the source going end it not dropped.

#include "ui/Ref.h"

#include <math/Types.h>

#include <any>
#include <cstdint>
#include <functional>
#include <string>

namespace ui {

	// px a press travels before it stops being a click, and a drag source's press starts its drag.
	inline constexpr float kDragThreshold = 6.0F;

	// What drives the drag: the pointer, or a player's navigation actions.
	enum class DragVia : std::uint8_t { Pointer, Keys };

	// A drag as its source's and targets' callbacks see it.
	struct Drag {
		std::string	   kind;  // the source's
		std::any	   value; // the source's, for std::any_cast
		Ref<Component> source;
		Ref<Component> ghost;			 // the source when it is its own ghost
		Ref<Component> target;			 // the drop target it's over; empty over none
		bool		   accepted = false; // the target accepts it
		bool		   dropped = false;	 // onDragEnd's: it was dropped on a target that accepted it
		DragVia		   via = DragVia::Pointer;
		int			   player = 0;
		// Where it is in the ui's px: the pointer, or for the keys the target's centre (the source's
		// over none). How far the ghost has moved from where it was picked up.
		Foundation::Vec2 at{0.0F, 0.0F};
		Foundation::Vec2 travel{0.0F, 0.0F};
	};

	struct DragSource {
		std::string kind;  // what it carries, by name: a target may filter on it, and the tree shows it
		std::any	value; // what it carries, for the target
		// Builds the ghost under the source (source.addChild) as the drag starts, `at` where it was picked
		// up in the source's space (the press, or its centre for the keys); placed absolutely, and removed
		// when the drag ends. Unset, the source is its own ghost.
		std::function<Ref<Component>(Component& source, Foundation::Vec2 at)> ghost;
		std::function<void(const Drag&)>									  onDragStart;
		// Once for every start: dropped, or not, for a refusal, a release over no target, a cancel or the
		// source going (its callback is still called).
		std::function<void(const Drag&)> onDragEnd;
	};

	struct DropTarget {
		// Whether it takes the drag, asked whenever the context needs to know (arriving, each move, each
		// key step, the tree), so cheap and free of effects; unset, it takes anything.
		std::function<bool(const Drag&)> accepts;
		std::function<void(const Drag&)> onDragEnter;
		std::function<void(const Drag&)> onDragOver; // each move over it, and once when a key drag arrives
		// Every enter is closed by a leave, or by the drop when it's dropped there; a target that goes
		// while the drag is over it hears nothing more.
		std::function<void(const Drag&)> onDragLeave;
		std::function<void(const Drag&)> onDrop; // before the source's onDragEnd
	};

} // namespace ui
