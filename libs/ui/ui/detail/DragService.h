#pragma once

// A context's drag and drop, internal to ui (ui/Drag.h has the contract): the pointer's candidate
// press, the drag under way and its ghost in the drag tier, the target it's over, and the keys' path.
// The dispatcher hands it the pointer, focus the navigation actions, and the context its layout,
// removals and cancels.

#include "ui/Component.h"
#include "ui/Drag.h"

#include <optional>
#include <vector>

namespace ui {
	class Context;
}

namespace ui::detail {

	// A navigation action as a key drag reads it.
	enum class DragKey : std::uint8_t { Drop, Cancel, Up, Down, Left, Right, Next, Previous };

	class DragService {
	  public:
		explicit DragService(Context& owner);

		[[nodiscard]] bool		  underWay() const { return active; }
		[[nodiscard]] bool		  byPointer() const { return active && drag.via == DragVia::Pointer; }
		[[nodiscard]] bool		  byKeys() const { return active && drag.via == DragVia::Keys; }
		[[nodiscard]] const Drag* current() const { return active ? &drag : nullptr; }
		// The ghost while it's lifted into the drag tier, and how far it has moved.
		[[nodiscard]] Component*	   lifted() const;
		[[nodiscard]] Foundation::Vec2 travel() const { return drag.travel; }
		// NotAllowed while the pointer holds a drag over a target that refuses it.
		[[nodiscard]] std::optional<Cursor> cursor() const;
		// Whether `target` takes the drag under way; false between drags or for a component that isn't one.
		[[nodiscard]] bool accepts(const Component& target) const;

		// --- The pointer, from the dispatcher ---

		// A primary press reached a source at or above `hit` with nothing consuming it below the source.
		void press(ComponentId hit, Foundation::Vec2 at, const PointerEvent& delivered);
		// The press's move: a candidate past the threshold starts its drag (true, after which the
		// dispatcher treats the press as cancelled); a drag under way follows the pointer.
		[[nodiscard]] bool pendingPast(Foundation::Vec2 at) const;
		bool			   startPointer(Foundation::Vec2 at);
		void			   pointerMoved(Foundation::Vec2 at);
		// The press ended: a drag under way drops where the pointer is, or doesn't.
		void release(Foundation::Vec2 at);
		void forgetPress() { pending.reset(); }

		// --- Keys, from focus ---

		// A player's activate that nothing consumed, with `focused` focused: a source at or above it is
		// picked up. False when there's none or a drag is under way.
		bool pickUp(int player, ComponentId focused);
		// A navigation action while a drag is under way: true when the drag took it.
		bool key(int player, DragKey key);

		// --- The context ---

		// Ends the drag under way, not dropped; false when there's none.
		bool cancel();
		// `root` and its subtree are about to go.
		void removing(ComponentId root);
		// After a layout that moved something: the pointer's target again under a still pointer, a key
		// drag's ghost back over its target, and a source hidden or disabled cancelled.
		void afterLayout();

	  private:
		struct Pending {
			ComponentId		 source;
			Foundation::Vec2 at;
		};

		// `pickedAt` is where it was picked up on screen, which a built ghost is handed in the source's space.
		bool start(Component& source, DragVia via, int player, Foundation::Vec2 at, Foundation::Vec2 pickedAt);
		// Ends it: the ghost set down, the target's leave or drop, then the source's onDragEnd. `going`
		// is a subtree being removed, whose ghost goes with it.
		void finish(bool drop, ComponentId going = {});
		// The drag is over `next` now (null for none): leave and enter as it changes, then over when
		// `over`.
		void setTarget(Component* next, bool over);
		void moveGhost(Foundation::Vec2 travel);
		// The innermost target at or above what the pointer hits at `at`, outside the source.
		[[nodiscard]] Component* targetAt(Foundation::Vec2 at) const;
		// A key drag's targets: every target that accepts, in tree order.
		[[nodiscard]] std::vector<Component*> keyTargets() const;
		void								  collectTargets(Component& node, std::vector<Component*>& out) const;
		void								  placeOnTarget();
		[[nodiscard]] Foundation::Vec2		  restCentre() const;
		void								  call(const std::function<void(const Drag&)>& callback) const;

		[[nodiscard]] Component* find(ComponentId id) const;
		[[nodiscard]] bool		 within(ComponentId id, ComponentId root) const;

		Context&						 context;
		std::optional<Pending>			 pending;
		bool							 active = false;
		Drag							 drag;
		Foundation::Vec2				 pressAt{0.0F, 0.0F}; // where the pointer picked it up
		bool							 builtGhost = false;  // the ghost is the source's `ghost`, not itself
		std::function<void(const Drag&)> onEnd;				  // the source's, kept so a source that goes still hears it
		std::optional<Foundation::Vec2>	 lostAt;			  // where a key drag's target was when it went
	};

} // namespace ui::detail
