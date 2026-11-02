#include "ui/detail/Dispatcher.h"

#include "ui/Context.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ui::detail {

	namespace {
		constexpr float	 kDragThreshold = 6.0F; // px a press may wander and still click
		constexpr double kWheelLatch = 0.15;	// seconds a wheel sequence stays with its scroller (R9.32)
		// GLFW reports the wheel in notches; three lines of 16 px a notch (R9.3's line mode).
		constexpr float kWheelNotch = 48.0F;
		constexpr float kBarWidth = 6.0F;
		constexpr float kBarInset = 2.0F;
		constexpr float kMinThumb = 24.0F;
		constexpr float kPageOverlap = 16.0F; // a page is the view less this, so a line stays in sight
	} // namespace

	Scrollbar scrollbarOf(Foundation::Vec2 size, Foundation::Vec2 scroll, Foundation::Vec2 scrollMax) {
		Scrollbar bar;
		if (scrollMax.y <= 0.0F || size.y <= kBarInset * 2.0F) {
			return bar;
		}
		bar.shown = true;
		bar.track = {size.x - kBarWidth - kBarInset, kBarInset, kBarWidth, size.y - (kBarInset * 2.0F)};
		const float thumbHeight = std::max(bar.track.height * size.y / (size.y + scrollMax.y), std::min(kMinThumb, bar.track.height));
		const float travel = bar.track.height - thumbHeight;
		bar.thumb = {bar.track.x, bar.track.y + (travel * scroll.y / scrollMax.y), kBarWidth, thumbHeight};
		return bar;
	}

	Dispatcher::Dispatcher(Context& owner)
		: context(owner) {}

	Component* Dispatcher::find(ComponentId id) const {
		return context.find(id);
	}

	bool Dispatcher::within(ComponentId id, ComponentId root) const {
		for (Component* node = find(id); node != nullptr; node = find(node->parentId)) {
			if (node->selfId == root) {
				return true;
			}
		}
		return false;
	}

	// --- Delivery ---

	void Dispatcher::deliver(Component& target, PointerEvent& event) {
		event.target = &target;
		for (Component* node = &target; node != nullptr && !event.consumed; node = find(node->parentId)) {
			event.currentTarget = node;
			event.local = node->screenToLocal(event.screen);
			if (const auto* callback = node->callbackFor(event.type)) {
				(*callback)(event);
			}
			node->handlePointer(event);
		}
	}

	void Dispatcher::deliverTo(Component& one, PointerEvent event) {
		event.target = &one;
		event.currentTarget = &one;
		event.local = one.screenToLocal(event.screen);
		if (const auto* callback = one.callbackFor(event.type)) {
			(*callback)(event);
		}
		one.handlePointer(event);
	}

	// Leave innermost first, then enter outermost first; neither bubbles (R9.8).
	void Dispatcher::hoverTo(ComponentId target, const PointerEvent& base) {
		std::vector<ComponentId> chain;
		for (Component* node = find(target); node != nullptr; node = find(node->parentId)) {
			chain.push_back(node->selfId);
		}
		const std::vector<ComponentId> before = std::exchange(hoverChain, chain);
		for (const ComponentId id : before) {
			Component* left = find(id);
			if (left != nullptr && std::ranges::find(chain, id) == chain.end()) {
				left->hovered = false;
				PointerEvent leave = base;
				leave.type = PointerEvent::Type::Leave;
				deliverTo(*left, leave);
			}
		}
		for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
			Component* entered = find(*it);
			if (entered != nullptr && std::ranges::find(before, *it) == before.end()) {
				entered->hovered = true;
				PointerEvent enter = base;
				enter.type = PointerEvent::Type::Enter;
				deliverTo(*entered, enter);
			}
		}
	}

	void Dispatcher::setPressed(ComponentId target, bool value) {
		for (Component* node = find(target); node != nullptr; node = find(node->parentId)) {
			node->pressed = value;
		}
	}

	ComponentId Dispatcher::commonAncestor(ComponentId a, ComponentId b) const {
		for (Component* node = find(b); node != nullptr; node = find(node->parentId)) {
			if (within(a, node->selfId)) {
				return node->selfId;
			}
		}
		return {};
	}

	// --- Events ---

	bool Dispatcher::dispatch(const foundation::InputEvent& event, float scale) {
		using Type = foundation::InputEvent::Type;
		PointerEvent pointerEvent{
			.screen = event.position / scale,
			.button = event.mouseButton,
			.mods = event.mods,
			.time = event.time,
		};
		switch (event.type) {
			case Type::PointerMove:
				pointerEvent.type = PointerEvent::Type::Move;
				return pointerMove(pointerEvent);
			case Type::PointerDown:
				pointerEvent.type = PointerEvent::Type::Down;
				return pointerDown(pointerEvent);
			case Type::PointerUp:
				pointerEvent.type = PointerEvent::Type::Up;
				return pointerUp(pointerEvent);
			case Type::Scroll: {
				pointerEvent.type = PointerEvent::Type::Wheel;
				pointerEvent.screen = pointer;
				Foundation::Vec2 delta{-event.scroll.x * kWheelNotch, -event.scroll.y * kWheelNotch};
				if ((event.mods & foundation::ModShift) != 0 && delta.x == 0.0F) {
					delta = {delta.y, 0.0F};
				}
				pointerEvent.wheel = delta;
				return wheel(pointerEvent);
			}
			case Type::PointerLeave:
				leaveWindow(event.time);
				return false;
			case Type::FocusLost:
				cancel();
				return false;
			default:
				return false;
		}
	}

	bool Dispatcher::pointerMove(PointerEvent& event) {
		pointer = event.screen;
		pointerKnown = true;
		if (Component* scroller = find(thumbScroller)) {
			dragThumb(*scroller, event.screen.y);
			return true;
		}
		Component* captor = find(captured);
		if (captor != nullptr) {
			// Boundaries go against the captor alone while it holds the pointer (R9.10).
			const bool inside = captor->containsPoint(captor->screenToLocal(event.screen));
			if (inside != captor->hovered) {
				captor->hovered = inside;
				PointerEvent boundary = event;
				boundary.type = inside ? PointerEvent::Type::Enter : PointerEvent::Type::Leave;
				deliverTo(*captor, boundary);
			}
			deliver(*captor, event);
			return true;
		}
		const ComponentId hit = context.hitTest(event.screen);
		hoverTo(hit, event);
		if (Component* target = find(hit)) {
			deliver(*target, event);
			return !pressing || pressOwned;
		}
		return (pressing && pressOwned) || context.pointerInert();
	}

	bool Dispatcher::pointerDown(PointerEvent& event) {
		pointer = event.screen;
		pointerKnown = true;
		if (pressing) {
			// A second button while one is held: a move with the button set (R9.30).
			event.type = PointerEvent::Type::Move;
			return pointerMove(event);
		}
		bool			  onThumb = false;
		const ComponentId hit = find(captured) != nullptr ? captured : context.hitTest(event.screen, &onThumb);
		if (find(captured) == nullptr && context.pressOutside(hit, event.button)) {
			// Closing the menus was the whole of this press: its gesture is the ui's, and clicks nothing.
			pressing = true;
			pressCancelled = true;
			pressButton = event.button;
			pressAt = event.screen;
			pressTarget = {};
			pressOwned = true;
			return true;
		}
		pressing = true;
		pressCancelled = false;
		pressButton = event.button;
		pressAt = event.screen;
		pressTarget = hit;
		Component* target = find(hit);
		pressOwned = target != nullptr || context.pointerInert();
		if (target == nullptr) {
			context.pointerPressed({});
			return pressOwned;
		}
		if (onThumb) {
			const Scrollbar		   bar = scrollbarOf(target->size, target->scroll, target->scrollMax);
			const Foundation::Vec2 local = target->screenToLocal(event.screen);
			if (local.y >= bar.thumb.y && local.y < bar.thumb.bottom()) {
				thumbScroller = hit;
				thumbGrab = local.y - bar.thumb.y;
			} else {
				// The track pages toward the press.
				const float page = std::max(target->size.y - kPageOverlap, kPageOverlap);
				target->scrollBy({0.0F, local.y < bar.thumb.y ? -page : page});
			}
			pressCancelled = true; // a scrollbar press never clicks
			return true;
		}
		setPressed(hit, true);
		deliver(*target, event);
		context.pointerPressed(hit);
		return true;
	}

	bool Dispatcher::pointerUp(PointerEvent& event) {
		pointer = event.screen;
		pointerKnown = true;
		if (!pressing) {
			const ComponentId hit = context.hitTest(event.screen);
			if (Component* target = find(hit)) {
				deliver(*target, event);
				return true;
			}
			return context.pointerInert();
		}
		if (event.button != pressButton) {
			event.type = PointerEvent::Type::Move;
			return pointerMove(event);
		}
		const bool owned = pressOwned;
		pressing = false;
		thumbScroller = {};
		setPressed(pressTarget, false);
		const ComponentId wasCaptured = captured;
		const ComponentId hit = find(captured) != nullptr ? captured : context.hitTest(event.screen);
		if (Component* target = find(hit)) {
			deliver(*target, event);
		}
		const Foundation::Vec2 travel = event.screen - pressAt;
		const bool			   clicks = owned && !pressCancelled && event.button == foundation::MouseButton::Left &&
							(travel.x * travel.x + travel.y * travel.y) <= kDragThreshold * kDragThreshold;
		if (clicks) {
			// On the nearest common ancestor of the press and the release, or the captor (R9.31).
			const ComponentId clickAt = find(wasCaptured) != nullptr ? wasCaptured : commonAncestor(pressTarget, hit);
			Component*		  clicked = find(clickAt);
			if (clicked != nullptr && clicked->enabledInTree) {
				PointerEvent click = event;
				click.type = PointerEvent::Type::Click;
				click.consumed = false;
				deliver(*clicked, click);
			}
		}
		pressTarget = {};
		if (find(captured) != nullptr) {
			captured = {};
			hoverAgain();
		}
		return owned;
	}

	bool Dispatcher::wheel(PointerEvent& event) {
		if (Component* latched = find(wheelLatch); latched != nullptr && event.time - wheelTime < kWheelLatch) {
			wheelTime = event.time;
			latched->scrollBy(event.wheel);
			return true;
		}
		const ComponentId hit = context.hitTest(event.screen);
		Component*		  target = find(hit);
		if (target == nullptr) {
			return context.pointerInert();
		}
		deliver(*target, event);
		if (event.consumed) {
			return true;
		}
		if (Component* scroller = scrollerFor(hit, event.wheel)) {
			wheelLatch = scroller->selfId;
			wheelTime = event.time;
			scroller->scrollBy(event.wheel);
		}
		return true;
	}

	Component* Dispatcher::scrollerFor(ComponentId from, Foundation::Vec2 delta) const {
		for (Component* node = find(from); node != nullptr; node = find(node->parentId)) {
			if (node->overflow != Overflow::Scroll) {
				continue;
			}
			const bool movesX = (delta.x < 0.0F && node->scroll.x > 0.0F) || (delta.x > 0.0F && node->scroll.x < node->scrollMax.x);
			const bool movesY = (delta.y < 0.0F && node->scroll.y > 0.0F) || (delta.y > 0.0F && node->scroll.y < node->scrollMax.y);
			if (movesX || movesY) {
				return node;
			}
		}
		return nullptr;
	}

	void Dispatcher::dragThumb(Component& scroller, float pointerY) {
		const Scrollbar bar = scrollbarOf(scroller.size, scroller.scroll, scroller.scrollMax);
		const float		travel = bar.track.height - bar.thumb.height;
		if (!bar.shown || travel <= 0.0F) {
			return;
		}
		const float thumbTop = scroller.screenToLocal({0.0F, pointerY}).y - thumbGrab - bar.track.y;
		scroller.scrollTo({scroller.scroll.x, std::clamp(thumbTop / travel, 0.0F, 1.0F) * scroller.scrollMax.y});
	}

	// --- Interruptions ---

	void Dispatcher::hoverAgain() {
		if (!pointerKnown || find(captured) != nullptr || find(thumbScroller) != nullptr) {
			return;
		}
		hoverTo(context.hitTest(pointer), PointerEvent{.screen = pointer});
	}

	void Dispatcher::leaveWindow(double time) {
		pointerKnown = false;
		if (find(captured) == nullptr) {
			hoverTo({}, PointerEvent{.screen = pointer, .time = time});
		}
	}

	void Dispatcher::cancel() {
		const PointerEvent base{.type = PointerEvent::Type::Cancel, .screen = pointer};
		if (Component* captor = find(captured)) {
			deliverTo(*captor, base);
		} else if (Component* pressed = find(pressTarget); pressed != nullptr && pressing) {
			deliverTo(*pressed, base);
		}
		setPressed(pressTarget, false);
		captured = {};
		pressing = false;
		pressCancelled = true;
		pressTarget = {};
		thumbScroller = {};
		wheelLatch = {};
		hoverTo({}, base);
	}

	void Dispatcher::removing(ComponentId root) {
		if (captured.valid() && within(captured, root)) {
			if (Component* captor = find(captured)) {
				deliverTo(*captor, PointerEvent{.type = PointerEvent::Type::Cancel, .screen = pointer});
			}
			captured = {};
			pressCancelled = true;
		}
		if (pressTarget.valid() && within(pressTarget, root)) {
			pressCancelled = true;
		}
		if (thumbScroller.valid() && within(thumbScroller, root)) {
			thumbScroller = {};
		}
	}

	void Dispatcher::capture(ComponentId captor) {
		captured = captor;
	}

	Cursor Dispatcher::cursor() const {
		const std::vector<ComponentId> captorChain{captured};
		for (const ComponentId id : find(captured) != nullptr ? captorChain : hoverChain) {
			for (Component* node = find(id); node != nullptr; node = find(node->parentId)) {
				if (node->cursor) {
					return *node->cursor;
				}
			}
			break;
		}
		return Cursor::Default;
	}

} // namespace ui::detail
