#pragma once

// A context's pointer dispatch, internal to ui (ui/Input.h has the contract). One pointer, the mouse:
// its hover chain, its capture, its press and the click it may make, the wheel's latch and a
// scrollbar thumb's drag.

#include "ui/Component.h"

#include <input/InputTypes.h>

#include <vector>

namespace ui {
	class Context;
}

namespace ui::detail {

	// A scroller's bar in its own box: shown only while it can scroll down its axis.
	struct Scrollbar {
		Foundation::Rect track{0.0F, 0.0F, 0.0F, 0.0F};
		Foundation::Rect thumb{0.0F, 0.0F, 0.0F, 0.0F};
		bool			 shown = false;
	};
	Scrollbar scrollbarOf(Foundation::Vec2 size, Foundation::Vec2 scroll, Foundation::Vec2 scrollMax);

	class Dispatcher {
	  public:
		explicit Dispatcher(Context& owner);

		// One platform event, at `scale` logical px to the ui's px; true when the ui took it, so the
		// scene shouldn't.
		bool dispatch(const foundation::InputEvent& event, float scale);
		// The pointer hasn't moved but what's under it may have (R9.9).
		void hoverAgain();
		// Everything mid-interaction ends: the scene was covered, or the window lost focus.
		void cancel();
		// `root` and its subtree are about to go: a captor among them gets its cancel first (R9.10).
		void removing(ComponentId root);
		void capture(ComponentId captor);

		[[nodiscard]] ComponentId captor() const { return captured; }
		[[nodiscard]] Cursor	  cursor() const;
		// What the pointer is over, innermost first, and where it is in the ui's px.
		[[nodiscard]] const std::vector<ComponentId>& hovered() const { return hoverChain; }
		[[nodiscard]] Foundation::Vec2				  pointerAt() const { return pointer; }

	  private:
		bool pointerDown(PointerEvent& event);
		bool pointerUp(PointerEvent& event);
		bool pointerMove(PointerEvent& event);
		bool wheel(PointerEvent& event);
		void leaveWindow(double time);

		// From `target` up through its ancestors until one consumes it.
		void deliver(Component& target, PointerEvent& event);
		// To `one` alone, as enter and leave go.
		void deliverTo(Component& one, PointerEvent event);
		// The hover chain for `target`: it and its ancestors, innermost first; enter and leave for the
		// difference (R9.8). An empty target clears it.
		void					  hoverTo(ComponentId target, const PointerEvent& base);
		void					  setPressed(ComponentId target, bool value);
		[[nodiscard]] Component*  find(ComponentId id) const;
		[[nodiscard]] bool		  within(ComponentId id, ComponentId root) const;
		[[nodiscard]] ComponentId commonAncestor(ComponentId a, ComponentId b) const;
		// The innermost scroller at or above `from` that can still move along `delta`.
		[[nodiscard]] Component* scrollerFor(ComponentId from, Foundation::Vec2 delta) const;
		void					 dragThumb(Component& scroller, float pointerY);

		Context&				 context;
		Foundation::Vec2		 pointer{0.0F, 0.0F};
		bool					 pointerKnown = false;
		std::vector<ComponentId> hoverChain; // innermost first
		ComponentId				 captured;
		// The press: its target, where it went down, which button, and whether the ui has the gesture.
		ComponentId				pressTarget;
		Foundation::Vec2		pressAt{0.0F, 0.0F};
		foundation::MouseButton pressButton = foundation::MouseButton::Left;
		bool					pressing = false;
		bool					pressCancelled = false;
		bool					pressOwned = false;
		ComponentId				thumbScroller; // a scrollbar thumb being dragged
		float					thumbGrab = 0.0F;
		ComponentId				wheelLatch;
		double					wheelTime = 0.0;
	};

} // namespace ui::detail
