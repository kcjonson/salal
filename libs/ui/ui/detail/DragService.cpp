#include "ui/detail/DragService.h"

#include "ui/Context.h"
#include "ui/detail/Dispatcher.h"
#include "ui/detail/FocusManager.h"

#include <utils/Log.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace ui::detail {

	namespace {
		Foundation::Vec2 centreOf(const Foundation::Rect& rect) {
			return {rect.x + (rect.width * 0.5F), rect.y + (rect.height * 0.5F)};
		}

		float lengthSquared(Foundation::Vec2 v) {
			return (v.x * v.x) + (v.y * v.y);
		}
	} // namespace

	DragService::DragService(Context& owner)
		: context(owner) {}

	Component* DragService::find(ComponentId id) const {
		return context.find(id);
	}

	bool DragService::within(ComponentId id, ComponentId root) const {
		return context.within(id, root);
	}

	Component* DragService::lifted() const {
		return active ? drag.ghost.get() : nullptr;
	}

	std::optional<Cursor> DragService::cursor() const {
		if (byPointer() && drag.target.get() != nullptr && !drag.accepted) {
			return Cursor::NotAllowed;
		}
		return std::nullopt;
	}

	bool DragService::accepts(const Component& target) const {
		if (!active || target.dropTarget == nullptr || !target.enabledInTree || within(target.selfId, drag.source.componentId())) {
			return false;
		}
		if (!target.dropTarget->accepts) {
			return true;
		}
		Drag asked = drag;
		asked.target = target.self();
		return target.dropTarget->accepts(asked);
	}

	// A copy of the callback, so one that clears its own component's source or target runs to its end,
	// and a copy of the drag, which the callback may end under it.
	void DragService::call(const std::function<void(const Drag&)>& callback) const {
		if (callback) {
			const std::function<void(const Drag&)> copy = callback;
			const Drag							   seen = drag;
			copy(seen);
		}
	}

	// --- The pointer ---

	// The innermost source at or above the hit, unless something below it consumed the press: a control
	// inside a card that shouldn't drag the card consumes its press.
	void DragService::press(ComponentId hit, Foundation::Vec2 at, const PointerEvent& delivered) {
		pending.reset();
		if (active) {
			return;
		}
		const Component* consumer = delivered.consumed ? delivered.currentTarget : nullptr;
		for (Component* node = find(hit); node != nullptr; node = find(node->parentId)) {
			if (node->dragSource != nullptr) {
				if (node->enabledInTree) {
					pending = Pending{.source = node->selfId, .at = at};
				}
				return;
			}
			if (node == consumer) {
				return;
			}
		}
	}

	bool DragService::pendingPast(Foundation::Vec2 at) const {
		return pending && !active && lengthSquared(at - pending->at) > kDragThreshold * kDragThreshold;
	}

	bool DragService::startPointer(Foundation::Vec2 at) {
		const std::optional<Pending> was = std::exchange(pending, std::nullopt);
		Component*					 source = was ? find(was->source) : nullptr;
		if (source == nullptr || active) {
			return false;
		}
		return start(*source, DragVia::Pointer, context.focusManager->mousePlayer(), at, was->at);
	}

	void DragService::pointerMoved(Foundation::Vec2 at) {
		if (!byPointer()) {
			return;
		}
		drag.at = at;
		moveGhost(at - pressAt);
		setTarget(targetAt(at), true);
	}

	// The release finds its target again first, so a target that refuses as it's entered is heard.
	void DragService::release(Foundation::Vec2 at) {
		pending.reset();
		if (!byPointer()) {
			return;
		}
		drag.at = at;
		moveGhost(at - pressAt);
		setTarget(targetAt(at), false);
		finish(true);
	}

	Component* DragService::targetAt(Foundation::Vec2 at) const {
		const ComponentId source = drag.source.componentId();
		for (Component* node = find(context.hitTest(at)); node != nullptr; node = find(node->parentId)) {
			if (node->dropTarget != nullptr && !within(node->selfId, source)) {
				return node;
			}
		}
		return nullptr;
	}

	// --- Keys ---

	bool DragService::pickUp(int player, ComponentId focused) {
		if (active) {
			return false;
		}
		for (Component* node = find(focused); node != nullptr; node = find(node->parentId)) {
			if (node->dragSource != nullptr) {
				const Foundation::Vec2 centre = centreOf(node->screenBounds());
				return start(*node, DragVia::Keys, player, centre, centre);
			}
		}
		return false;
	}

	bool DragService::key(int player, DragKey pressed) {
		if (!active || player != drag.player) {
			return false;
		}
		if (byPointer()) {
			if (pressed != DragKey::Cancel) {
				return false;
			}
			cancel();
			return true;
		}
		switch (pressed) {
			case DragKey::Drop:
				finish(true);
				return true;
			case DragKey::Cancel:
				cancel();
				return true;
			case DragKey::Next:
			case DragKey::Previous: {
				const std::vector<Component*> targets = keyTargets();
				if (targets.empty()) {
					return true;
				}
				const bool		  forward = pressed == DragKey::Next;
				const auto		  at = std::ranges::find(targets, drag.target.get());
				const std::size_t count = targets.size();
				std::size_t		  index = forward ? 0 : count - 1;
				if (at != targets.end()) {
					index = (static_cast<std::size_t>(at - targets.begin()) + (forward ? 1 : count - 1)) % count;
				}
				setTarget(targets[index], true);
				break;
			}
			default: {
				const Direction		   way = pressed == DragKey::Up		? Direction::Up
											 : pressed == DragKey::Down ? Direction::Down
											 : pressed == DragKey::Left ? Direction::Left
																		: Direction::Right;
				Component*			   current = drag.target.get();
				const Foundation::Vec2 rest = restCentre();
				const Foundation::Rect origin = current != nullptr ? current->screenBounds() : Foundation::Rect{rest.x, rest.y, 0.0F, 0.0F};
				const std::vector<Component*> targets = keyTargets();
				if (Component* next = nearestOnScreen(origin, way, targets, current)) {
					setTarget(next, true);
				}
				break;
			}
		}
		if (active) {
			placeOnTarget();
		}
		return true;
	}

	std::vector<Component*> DragService::keyTargets() const {
		std::vector<Component*> out;
		for (const ComponentId rootId : context.getRoots()) {
			if (Component* rootComponent = find(rootId)) {
				collectTargets(*rootComponent, out);
			}
		}
		return out;
	}

	// Tree order, overlay entries under their openers; the source's subtree, the ghost's, is skipped.
	void DragService::collectTargets(Component& node, std::vector<Component*>& out) const {
		if (!node.isShown() || node.selfId == drag.source.componentId() || node.lifted) {
			return;
		}
		if (node.dropTarget != nullptr && node.slot >= context.modalFloor(drag.player) && accepts(node)) {
			out.push_back(&node);
		}
		for (const ComponentId childId : node.children) {
			if (Component* child = find(childId)) {
				collectTargets(*child, out);
			}
		}
	}

	// Where the ghost's centre sits before it moves.
	Foundation::Vec2 DragService::restCentre() const {
		if (const Component* ghost = drag.ghost.get()) {
			return centreOf(ghost->screenBounds()) - drag.travel;
		}
		const Component* source = drag.source.get();
		return source != nullptr ? centreOf(source->screenBounds()) : drag.at;
	}

	// A key drag's ghost centred on its target, or set down over none.
	void DragService::placeOnTarget() {
		const Foundation::Vec2 rest = restCentre();
		const Component*	   target = drag.target.get();
		drag.at = target != nullptr ? centreOf(target->screenBounds()) : rest;
		moveGhost(drag.at - rest);
	}

	// --- The drag ---

	bool DragService::start(Component& source, DragVia via, int player, Foundation::Vec2 at, Foundation::Vec2 pickedAt) {
		if (source.dragSource == nullptr || !source.enabledInTree || !context.shownInTree(source)) {
			return false;
		}
		const DragSource						spec = *source.dragSource;
		const std::function<void(const Drag&)>& onStart = spec.onDragStart;
		active = true;
		drag = Drag{
			.kind = spec.kind,
			.value = spec.value,
			.source = source.self(),
			.ghost = source.self(),
			.via = via,
			.player = player,
			.at = at,
		};
		onEnd = spec.onDragEnd;
		pressAt = pickedAt;
		builtGhost = false;
		lostAt.reset();
		if (spec.ghost) {
			const Ref<Component> built = spec.ghost(source, source.screenToLocal(pickedAt));
			if (Component* ghost = built.get(); ghost != nullptr && ghost->parentId == source.selfId) {
				ghost->layout.position = Position::Absolute; // out of the source's flow, as an overlay entry is
				ghost->layoutChanged(true);
				drag.ghost = built;
				builtGhost = true;
			} else if (ghost != nullptr) {
				LOG_WARNING(UI, "%s: a drag's ghost must be built under its source; the source is its own ghost", source.typeName());
			}
		}
		if (Component* ghost = drag.ghost.get()) {
			ghost->lifted = true;
		}
		source.dragging = true;
		source.interactionChanged();
		context.orderDirty = true;
		context.geometryDirty = true;
		context.closeTooltip();
		if (via == DragVia::Pointer) {
			context.dispatcher->capture(source.selfId);
		}
		moveGhost({0.0F, 0.0F});
		call(onStart);
		if (!active) {
			return true; // ended in its own start
		}
		if (via == DragVia::Pointer) {
			pointerMoved(at);
			return true;
		}
		Component* nearest = nullptr;
		float	   best = std::numeric_limits<float>::infinity();
		for (Component* target : keyTargets()) {
			const float distance = lengthSquared(centreOf(target->screenBounds()) - at);
			if (distance < best) {
				nearest = target;
				best = distance;
			}
		}
		setTarget(nearest, true);
		if (active) {
			placeOnTarget();
		}
		return true;
	}

	void DragService::moveGhost(Foundation::Vec2 travel) {
		drag.travel = travel;
		context.placeLifted();
	}

	// Leave the old target and enter the new as it changes; the same target is asked again, since what it
	// accepts may have changed. Each callback may end the drag, after which nothing more is said.
	void DragService::setTarget(Component* next, bool over) {
		Component*	   was = drag.target.get();
		Ref<Component> nextRef = next != nullptr ? next->self() : Ref<Component>();
		if (next != was) {
			if (was != nullptr) {
				drag.target = {};
				drag.accepted = false;
				was->dropActive = false;
				was->interactionChanged();
				if (was->dropTarget != nullptr) {
					call(was->dropTarget->onDragLeave);
				}
				if (!active) {
					return;
				}
			}
			next = nextRef.get();
			if (next != nullptr && next->dropTarget != nullptr) {
				drag.target = nextRef;
				drag.accepted = accepts(*next);
				next->dropActive = drag.accepted;
				next->interactionChanged();
				call(next->dropTarget->onDragEnter);
				if (!active) {
					return;
				}
			}
		} else if (next != nullptr) {
			const bool now = accepts(*next);
			if (now != drag.accepted) {
				drag.accepted = now;
				next->dropActive = now;
				next->interactionChanged();
			}
		}
		Component* target = drag.target.get();
		if (over && target != nullptr && target->dropTarget != nullptr) {
			call(target->dropTarget->onDragOver);
		}
	}

	bool DragService::cancel() {
		pending.reset();
		if (!active) {
			return false;
		}
		finish(false);
		return true;
	}

	// The ghost set down and the state cleared first, so the callbacks see the drag over and may start
	// another; then the target's drop, or its leave, then the source's end.
	void DragService::finish(bool drop, ComponentId going) {
		if (!active) {
			return;
		}
		Component* target = drag.target.get();
		Drag	   ended = drag;
		ended.dropped = drop && target != nullptr && drag.accepted;
		active = false;
		pending.reset();
		lostAt.reset();
		const std::function<void(const Drag&)> end = std::exchange(onEnd, nullptr);
		const bool							   built = std::exchange(builtGhost, false);
		drag = Drag{};
		if (Component* ghost = ended.ghost.get()) {
			ghost->lifted = false;
			Component* owner = find(ghost->parentId);
			if (built && owner != nullptr && !(going.valid() && within(ghost->selfId, going))) {
				owner->removeChild(ended.ghost);
			}
		}
		if (Component* source = ended.source.get()) {
			source->dragging = false;
			source->interactionChanged();
		}
		if (target != nullptr) {
			target->dropActive = false;
			target->interactionChanged();
		}
		if (ended.via == DragVia::Pointer && context.dispatcher->captor() == ended.source.componentId()) {
			context.dispatcher->capture({});
		}
		context.orderDirty = true;
		context.geometryDirty = true;
		context.hoverStale = true;
		if (target != nullptr && target->dropTarget != nullptr) {
			const std::function<void(const Drag&)> handler = ended.dropped ? target->dropTarget->onDrop : target->dropTarget->onDragLeave;
			if (handler) {
				handler(ended);
			}
		}
		if (end) {
			end(ended);
		}
	}

	void DragService::removing(ComponentId root) {
		if (pending && within(pending->source, root)) {
			pending.reset();
		}
		if (!active) {
			return;
		}
		if (within(drag.source.componentId(), root)) {
			finish(false, root);
			return;
		}
		if (builtGhost && within(drag.ghost.componentId(), root)) {
			drag.ghost = {};
			builtGhost = false;
		}
		Component* target = drag.target.get();
		if (target != nullptr && within(target->selfId, root)) {
			if (byKeys()) {
				lostAt = centreOf(target->screenBounds());
			}
			target->dropActive = false;
			drag.target = {};
			drag.accepted = false;
			context.hoverStale = true;
		}
	}

	void DragService::afterLayout() {
		if (!active) {
			return;
		}
		const Component* source = drag.source.get();
		if (source == nullptr || !source->enabledInTree || !context.shownInTree(*source)) {
			cancel();
			return;
		}
		if (byPointer()) {
			setTarget(targetAt(drag.at), false);
			return;
		}
		Component* target = drag.target.get();
		if (target != nullptr && (target->dropTarget == nullptr || !context.shownInTree(*target))) {
			lostAt = centreOf(target->screenBounds());
			setTarget(nullptr, false);
			target = nullptr;
		}
		if (!active) {
			return;
		}
		if (target == nullptr && lostAt) {
			// The target it was on went: the accepting target nearest where it was.
			const Foundation::Vec2 was = *std::exchange(lostAt, std::nullopt);
			Component*			   nearest = nullptr;
			float				   best = std::numeric_limits<float>::infinity();
			for (Component* each : keyTargets()) {
				const float distance = lengthSquared(centreOf(each->screenBounds()) - was);
				if (distance < best) {
					nearest = each;
					best = distance;
				}
			}
			setTarget(nearest, nearest != nullptr);
		} else if (target != nullptr) {
			setTarget(target, false);
		}
		if (active) {
			placeOnTarget();
		}
	}

} // namespace ui::detail
