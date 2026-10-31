#include "ui/detail/FocusManager.h"

#include "ui/Context.h"

#include <utils/Log.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <string_view>

namespace ui::detail {

	namespace {
		std::uint8_t bitOf(int player) {
			return static_cast<std::uint8_t>(1U << static_cast<unsigned>(player));
		}

		bool validPlayer(int player) {
			if (player >= 0 && player < kMaxPlayers) {
				return true;
			}
			LOG_WARNING(UI, "player %d is out of range; focus is per player slot, 0 to %d", player, kMaxPlayers - 1);
			return false;
		}

		float low(const Foundation::Rect& rect, int axis) {
			return axis == 0 ? rect.x : rect.y;
		}

		float high(const Foundation::Rect& rect, int axis) {
			return axis == 0 ? rect.right() : rect.bottom();
		}

		float middle(const Foundation::Rect& rect, int axis) {
			return (low(rect, axis) + high(rect, axis)) * 0.5F;
		}
	} // namespace

	FocusManager::FocusManager(Context& owner)
		: context(owner),
		  defaults(defaultNavigation()) {}

	Component* FocusManager::find(ComponentId id) const {
		return context.find(id);
	}

	bool FocusManager::within(ComponentId id, ComponentId root) const {
		for (Component* node = find(id); node != nullptr; node = find(node->parentId)) {
			if (node->selfId == root) {
				return true;
			}
		}
		return false;
	}

	// --- Routing ---

	bool FocusManager::holds(int player, foundation::DeviceId device) const {
		const bool anyHeld = std::ranges::any_of(players, [](const Player& each) { return !each.devices.empty(); });
		return anyHeld ? std::ranges::find(players[player].devices, device) != players[player].devices.end() : player == 0;
	}

	int FocusManager::mousePlayer() const {
		for (int player = 0; player < kMaxPlayers; ++player) {
			if (std::ranges::find(players[player].devices, foundation::DeviceId{.kind = foundation::DeviceKind::Mouse}) !=
				players[player].devices.end()) {
				return player;
			}
		}
		return 0;
	}

	std::pair<int, FocusManager::Action> FocusManager::route(const foundation::InputEvent& event, float previous) const {
		int first = -1;
		for (int player = 0; player < kMaxPlayers; ++player) {
			if (!holds(player, event.device)) {
				continue;
			}
			const foundation::ActionMap& map = players[player].navigation ? *players[player].navigation : defaults;
			if (const Action action = actionOf(map, event, previous); action != Action::None) {
				return {player, action};
			}
			if (first < 0) {
				first = player;
			}
		}
		return {first, Action::None};
	}

	// A key or pad button by its binding, an axis when it crosses its binding's threshold.
	FocusManager::Action
	FocusManager::actionOf(const foundation::ActionMap& map, const foundation::InputEvent& event, float previous) const {
		using Source = foundation::Binding::Source;
		using Type = foundation::InputEvent::Type;
		static constexpr std::pair<std::string_view, Action> kNamed[] = {
			{actions::kActivate, Action::Activate},
			{actions::kCancel, Action::Cancel},
			{actions::kUp, Action::Up},
			{actions::kDown, Action::Down},
			{actions::kLeft, Action::Left},
			{actions::kRight, Action::Right},
			{actions::kNext, Action::Next},
			{actions::kPrevious, Action::Previous},
		};
		for (const auto& [name, action] : kNamed) {
			for (const foundation::Binding& binding : map.bindings(name)) {
				bool matches = false;
				switch (event.type) {
					case Type::KeyDown:
					case Type::KeyUp:
						matches = binding.source == Source::Key && binding.control == static_cast<std::uint16_t>(event.key);
						break;
					case Type::ButtonDown:
						matches = binding.source == Source::GamepadButton && binding.control == static_cast<std::uint16_t>(event.button);
						break;
					case Type::Axis:
						matches = binding.source == Source::GamepadAxis && binding.control == static_cast<std::uint16_t>(event.axis) &&
								  previous * binding.scale <= binding.threshold && event.value * binding.scale > binding.threshold;
						break;
					default:
						break;
				}
				if (matches) {
					return action;
				}
			}
		}
		return Action::None;
	}

	float& FocusManager::axisValue(foundation::DeviceId device, foundation::GamepadAxis axis) {
		for (AxisValue& each : axes) {
			if (each.device == device && each.axis == axis) {
				return each.value;
			}
		}
		return axes.emplace_back(AxisValue{.device = device, .axis = axis, .value = 0.0F}).value;
	}

	// --- Events ---

	bool FocusManager::dispatch(const foundation::InputEvent& event) {
		using Type = foundation::InputEvent::Type;
		switch (event.type) {
			case Type::KeyDown:
				return keyDown(event);
			case Type::KeyUp:
			case Type::Char:
				return keyOther(event);
			case Type::ButtonDown:
				return padAction(event, 0.0F);
			case Type::Axis: {
				const float previous = std::exchange(axisValue(event.device, event.axis), event.value);
				return padAction(event, previous);
			}
			case Type::DeviceDisconnected:
				std::erase_if(axes, [&](const AxisValue& each) { return each.device == event.device; });
				return false;
			default:
				return false;
		}
	}

	// Next and previous are the ui's before any component hears the key (R9.16); otherwise the key
	// goes to the focused component, then its action.
	bool FocusManager::keyDown(const foundation::InputEvent& event) {
		auto [player, action] = route(event, 0.0F);
		if (player < 0) {
			return false;
		}
		mode = InputMode::Keyboard;
		if (action == Action::Next && (event.mods & foundation::ModShift) != 0) {
			action = Action::Previous;
		}
		if (action == Action::Next || action == Action::Previous) {
			return next(player, action == Action::Previous);
		}
		KeyEvent key{.type = KeyEvent::Type::Down, .key = event.key, .mods = event.mods, .repeat = event.repeat, .player = player};
		if (deliver(players[player].focused, key)) {
			return true;
		}
		return act(player, action, key);
	}

	// A release goes to the player whose map binds the key, a character to the first holding the
	// keyboard with focus somewhere.
	bool FocusManager::keyOther(const foundation::InputEvent& event) {
		int player = route(event, 0.0F).first;
		if (event.type == foundation::InputEvent::Type::Char) {
			for (int each = 0; each < kMaxPlayers; ++each) {
				if (holds(each, event.device) && find(players[each].focused) != nullptr) {
					player = each;
					break;
				}
			}
		}
		if (player < 0) {
			return false;
		}
		KeyEvent key{
			.type = event.type == foundation::InputEvent::Type::Char ? KeyEvent::Type::Char : KeyEvent::Type::Up,
			.key = event.key,
			.codepoint = event.codepoint,
			.mods = event.mods,
			.player = player,
		};
		return deliver(players[player].focused, key);
	}

	bool FocusManager::padAction(const foundation::InputEvent& event, float previous) {
		const auto [player, action] = route(event, previous);
		if (player < 0) {
			return false;
		}
		if (event.type == foundation::InputEvent::Type::ButtonDown || action != Action::None) {
			mode = InputMode::Controller;
		}
		return act(player, action, KeyEvent{.mods = event.mods, .player = player});
	}

	bool FocusManager::act(int player, Action action, const KeyEvent& base) {
		if (action == Action::None) {
			return false;
		}
		if (action == Action::Next || action == Action::Previous) {
			return next(player, action == Action::Previous);
		}
		KeyEvent event = base;
		event.consumed = false;
		switch (action) {
			case Action::Activate:
				event.type = KeyEvent::Type::Activate;
				break;
			case Action::Cancel:
				event.type = KeyEvent::Type::Cancel;
				break;
			default:
				event.type = KeyEvent::Type::Navigate;
				event.direction = action == Action::Up	   ? Direction::Up
								  : action == Action::Down ? Direction::Down
								  : action == Action::Left ? Direction::Left
														   : Direction::Right;
				break;
		}
		if (deliver(players[player].focused, event)) {
			return true;
		}
		if (event.type == KeyEvent::Type::Cancel) {
			return context.cancelOverlay(player);
		}
		return event.type == KeyEvent::Type::Navigate && direction(player, event.direction);
	}

	bool FocusManager::deliver(ComponentId target, KeyEvent& event) {
		Component* focusedComponent = find(target);
		if (focusedComponent == nullptr) {
			return false;
		}
		event.target = focusedComponent;
		for (Component* node = focusedComponent; node != nullptr && !event.consumed; node = find(node->parentId)) {
			event.currentTarget = node;
			if (node->handlers) {
				const auto& callback = event.type == KeyEvent::Type::Up ? node->handlers->onKeyUp : node->handlers->onKeyDown;
				if (callback) {
					callback(event);
				}
			}
			node->handleKey(event);
		}
		return event.consumed;
	}

	void FocusManager::pointerPressed(ComponentId hit) {
		mode = InputMode::Pointer;
		const int player = mousePlayer();
		for (Component* node = find(hit); node != nullptr; node = find(node->parentId)) {
			if (node->focusable && canFocus(*node, player)) {
				move(player, node->selfId, false, false);
				return;
			}
			if (node->isOverlay()) {
				return; // nothing to focus in it: focus stays, rather than going past it to its opener
			}
		}
		if (players[player].scopes.empty()) {
			move(player, {}, false, false);
		}
	}

	// --- Focus ---

	void FocusManager::move(int player, ComponentId to, bool visible, bool reveal) {
		Player&			   state = players[player];
		const std::uint8_t bit = bitOf(player);
		state.lost = false;
		state.restore = {};
		state.visible = visible;
		state.reveal = reveal ? to : ComponentId{};
		if (to == state.focused) {
			if (Component* same = find(to)) {
				same->focusVisibleBy = visible ? (same->focusVisibleBy | bit) : (same->focusVisibleBy & ~bit);
			}
			return;
		}
		const ComponentId from = std::exchange(state.focused, to);
		if (Component* old = find(from)) {
			old->focusedBy &= ~bit;
			old->focusVisibleBy &= ~bit;
			if (old->handlers && old->handlers->onBlur) {
				old->handlers->onBlur(*old);
			}
		}
		if (state.focused != to) {
			return; // the blur moved focus itself
		}
		Component* now = find(to);
		if (now == nullptr) {
			state.focused = {};
			return;
		}
		now->focusedBy |= bit;
		if (visible) {
			now->focusVisibleBy |= bit;
		}
		for (Component* node = find(now->parentId); node != nullptr; node = find(node->parentId)) {
			if (node->focusGroup) {
				node->activeChild = to;
			}
		}
		if (now->handlers && now->handlers->onFocus) {
			now->handlers->onFocus(*now);
		}
		if (to.valid() && state.focused == to) {
			context.focusMoved(player, from);
		}
	}

	bool FocusManager::focus(ComponentId target, int player) {
		if (!validPlayer(player)) {
			return false;
		}
		Component* component = find(target);
		if (component == nullptr || !canFocus(*component, player)) {
			return false;
		}
		move(player, target, mode != InputMode::Pointer, true);
		return true;
	}

	void FocusManager::clear(int player) {
		if (validPlayer(player)) {
			move(player, {}, false, false);
		}
	}

	ComponentId FocusManager::focused(int player) const {
		return player >= 0 && player < kMaxPlayers && find(players[player].focused) != nullptr ? players[player].focused : ComponentId{};
	}

	ComponentId FocusManager::scopeRoot(int player) const {
		return players[player].scopes.empty() ? ComponentId{} : players[player].scopes.back().root;
	}

	bool FocusManager::canFocus(const Component& component, int player) const {
		const ComponentId scope = scopeRoot(player);
		return component.canReceiveFocus() && (!scope.valid() || within(component.selfId, scope));
	}

	void FocusManager::setNavigation(int player, foundation::ActionMap map) {
		if (validPlayer(player)) {
			players[player].navigation = std::move(map);
		}
	}

	void FocusManager::setDevices(int player, std::span<const foundation::DeviceId> devices) {
		if (validPlayer(player)) {
			players[player].devices.assign(devices.begin(), devices.end());
		}
	}

	// --- Tab order ---

	std::vector<Component*> FocusManager::stops(int player) const {
		std::vector<Component*> out;
		if (Component* scope = find(scopeRoot(player))) {
			collectStops(*scope, out, true);
		} else {
			for (const ComponentId rootId : context.getRoots()) {
				if (Component* rootComponent = find(rootId)) {
					collectStops(*rootComponent, out, true);
				}
			}
		}
		// Positive tabIndex first, ascending, then tree order (R9.18).
		std::ranges::stable_sort(out, {}, [](const Component* each) {
			const int index = each->tabIndex.value_or(0);
			return index > 0 ? index : INT_MAX;
		});
		return out;
	}

	void FocusManager::collectStops(Component& node, std::vector<Component*>& out, bool self) const {
		if (!node.isShown() || !node.enabled) {
			return;
		}
		if (self) {
			if (node.focusGroup) {
				if (Component* stop = groupStop(node)) {
					out.push_back(stop);
				}
				return;
			}
			if (node.focusable && node.tabIndex.value_or(0) >= 0) {
				out.push_back(&node);
			}
		}
		for (const ComponentId childId : node.children) {
			if (Component* child = find(childId)) {
				collectStops(*child, out, true);
			}
		}
	}

	Component* FocusManager::groupStop(Component& group) const {
		std::vector<Component*> members;
		collectStops(group, members, false);
		if (Component* active = find(group.activeChild); active != nullptr && std::ranges::find(members, active) != members.end()) {
			return active;
		}
		if (!members.empty()) {
			return members.front();
		}
		return group.focusable && group.tabIndex.value_or(0) >= 0 ? &group : nullptr;
	}

	bool FocusManager::next(int player, bool backward) {
		if (!validPlayer(player)) {
			return false;
		}
		const std::vector<Component*> list = stops(player);
		if (list.empty()) {
			return false;
		}
		const std::size_t count = list.size();
		const auto		  at = std::ranges::find(list, find(players[player].focused));
		std::size_t		  index = backward ? count - 1 : 0;
		if (at != list.end()) {
			const auto from = static_cast<std::size_t>(at - list.begin());
			index = backward ? (from + count - 1) % count : (from + 1) % count; // wraps (R9.19)
		}
		move(player, list[index]->selfId, true, true);
		return true;
	}

	// --- Directional focus ---

	void FocusManager::collectFocusable(Component& node, std::vector<Component*>& out) const {
		if (!node.isShown() || !node.enabled) {
			return;
		}
		if (node.focusable && node.tabIndex.value_or(0) >= 0) {
			out.push_back(&node);
		}
		for (const ComponentId childId : node.children) {
			if (Component* child = find(childId)) {
				collectFocusable(*child, out);
			}
		}
	}

	Component* FocusManager::groupOf(const Component& component, int player) const {
		const ComponentId scope = scopeRoot(player);
		if (component.selfId == scope) {
			return nullptr;
		}
		for (Component* node = find(component.parentId); node != nullptr; node = find(node->parentId)) {
			if (node->focusGroup) {
				return node;
			}
			if (node->selfId == scope) {
				break;
			}
		}
		return nullptr;
	}

	// A direction goes to an explicit neighbour first, then along the focus group the focus is in, and
	// then to the nearest focusable that way outside the group (R9.26, R9.29).
	bool FocusManager::direction(int player, Direction way) {
		if (!validPlayer(player)) {
			return false;
		}
		Component* current = find(players[player].focused);
		if (current == nullptr || !canFocus(*current, player)) {
			const std::vector<Component*> list = stops(player);
			if (list.empty()) {
				return false;
			}
			move(player, list.front()->selfId, true, true);
			return true;
		}
		if (Component* neighbour = find(current->neighbours[static_cast<int>(way)]); neighbour != nullptr && canFocus(*neighbour, player)) {
			move(player, resolve(*neighbour, *current)->selfId, true, true);
			return true;
		}
		Component* group = groupOf(*current, player);
		if (group != nullptr) {
			if (Component* step = stepInGroup(*group, *current, way)) {
				move(player, step->selfId, true, true);
				return true;
			}
		}
		std::vector<Component*> candidates;
		if (Component* scope = find(scopeRoot(player))) {
			collectFocusable(*scope, candidates);
		} else {
			for (const ComponentId rootId : context.getRoots()) {
				if (Component* rootComponent = find(rootId)) {
					collectFocusable(*rootComponent, candidates);
				}
			}
		}
		if (group != nullptr) {
			std::erase_if(candidates, [&](const Component* each) { return within(each->selfId, group->selfId); });
		}
		Component* best = nearest(*current, way, candidates);
		if (best == nullptr) {
			return false;
		}
		move(player, resolve(*best, *current)->selfId, true, true);
		return true;
	}

	Component* FocusManager::stepInGroup(Component& group, Component& from, Direction way) const {
		std::vector<Component*> members;
		collectStops(group, members, false);
		const FocusGroup& spec = *group.focusGroup;
		const bool		  horizontal = way == Direction::Left || way == Direction::Right;
		if (spec.orientation == FocusGroup::Orientation::Both) {
			return nearest(from, way, members);
		}
		if ((spec.orientation == FocusGroup::Orientation::Horizontal) != horizontal) {
			return nullptr;
		}
		const auto at = std::ranges::find(members, &from);
		if (at == members.end()) {
			return nullptr;
		}
		const bool forward = way == Direction::Right || way == Direction::Down;
		const auto index = at - members.begin();
		const auto count = static_cast<std::ptrdiff_t>(members.size());
		const auto stepped = index + (forward ? 1 : -1);
		if (stepped >= 0 && stepped < count) {
			return members[static_cast<std::size_t>(stepped)];
		}
		return spec.wrap && count > 1 ? members[static_cast<std::size_t>((stepped + count) % count)] : nullptr;
	}

	Component* FocusManager::nearest(const Component& from, Direction way, std::span<Component* const> candidates) const {
		const int			   along = way == Direction::Left || way == Direction::Right ? 0 : 1;
		const int			   across = 1 - along;
		const bool			   forward = way == Direction::Right || way == Direction::Down;
		const Foundation::Rect origin = from.screenBounds();
		Component*			   best = nullptr;
		float				   bestScore = std::numeric_limits<float>::infinity();
		for (Component* candidate : candidates) {
			if (candidate == &from) {
				continue;
			}
			const Foundation::Rect box = candidate->screenBounds();
			// Its centre past the origin's, and its far edge past the origin's, that way.
			const bool ahead = forward ? middle(box, along) > middle(origin, along) && high(box, along) > high(origin, along)
									   : middle(box, along) < middle(origin, along) && low(box, along) < low(origin, along);
			if (!ahead) {
				continue;
			}
			const float gap = std::max(0.0F, forward ? low(box, along) - high(origin, along) : low(origin, along) - high(box, along));
			const float offset = std::abs(middle(box, across) - middle(origin, across));
			const float overlap =
				std::max(0.0F, std::min(high(box, across), high(origin, across)) - std::max(low(box, across), low(origin, across)));
			const float score = gap + (2.0F * offset) - overlap;
			if (score < bestScore) {
				best = candidate;
				bestScore = score;
			}
		}
		return best;
	}

	Component* FocusManager::resolve(Component& candidate, const Component& from) const {
		Component* outermost = nullptr;
		for (Component* node = find(candidate.parentId); node != nullptr; node = find(node->parentId)) {
			if (node->focusGroup && !within(from.selfId, node->selfId)) {
				outermost = node;
			}
		}
		if (outermost != nullptr) {
			Component* active = find(outermost->activeChild);
			if (active != nullptr && active->canReceiveFocus() && within(active->selfId, outermost->selfId)) {
				return active;
			}
		}
		return &candidate;
	}

	// --- Scopes ---

	void FocusManager::pushScope(ComponentId root, int player) {
		if (!validPlayer(player) || find(root) == nullptr) {
			return;
		}
		Player& state = players[player];
		state.scopes.push_back({.root = root, .restore = state.focused});
		if (!within(state.focused, root)) {
			const std::vector<Component*> list = stops(player);
			move(player, list.empty() ? ComponentId{} : list.front()->selfId, mode != InputMode::Pointer, true);
		}
	}

	void FocusManager::popScope(int player) {
		if (!validPlayer(player) || players[player].scopes.empty()) {
			return;
		}
		Player&		state = players[player];
		const Scope closed = state.scopes.back();
		state.scopes.pop_back();
		if (Component* back = find(closed.restore); back != nullptr && canFocus(*back, player)) {
			move(player, closed.restore, mode != InputMode::Pointer, true);
		} else if (Component* current = find(state.focused); current == nullptr || !canFocus(*current, player)) {
			const std::vector<Component*> list = stops(player);
			move(player, list.empty() ? ComponentId{} : list.front()->selfId, mode != InputMode::Pointer, true);
		}
	}

	// --- Upkeep ---

	void FocusManager::removing(ComponentId root) {
		for (int player = 0; player < kMaxPlayers; ++player) {
			Player& state = players[player];
			if (state.focused.valid() && within(state.focused, root)) {
				if (Component* gone = find(state.focused)) {
					gone->focusedBy &= ~bitOf(player);
					gone->focusVisibleBy &= ~bitOf(player);
				}
				// Focus in an overlay that closes goes back to where it was when the overlay opened.
				state.restore = context.overlayRestore(state.focused, root);
				state.focused = {};
				state.lost = true;
			}
			// A scope rooted in the subtree closes, and focus goes back to where it came from.
			const auto closed = std::ranges::find_if(state.scopes, [&](const Scope& scope) { return within(scope.root, root); });
			if (closed != state.scopes.end()) {
				state.restore = closed->restore;
				state.lost = true;
				state.scopes.erase(closed, state.scopes.end());
			}
		}
	}

	void FocusManager::afterLayout() {
		for (int player = 0; player < kMaxPlayers; ++player) {
			Player&	   state = players[player];
			const bool wasVisible = state.visible;
			if (Component* current = find(state.focused); current != nullptr && !canFocus(*current, player)) {
				move(player, {}, false, false); // hidden, disabled or no longer focusable: blurred, then on
				state.lost = true;
			} else if (current == nullptr && state.focused.valid()) {
				state.focused = {};
				state.lost = true;
			}
			if (state.lost && !state.focused.valid()) {
				ComponentId target = state.restore;
				if (Component* back = find(target); back == nullptr || !canFocus(*back, player)) {
					const std::vector<Component*> list = stops(player);
					target = list.empty() ? ComponentId{} : list.front()->selfId;
				}
				state.lost = false;
				state.restore = {};
				if (target.valid()) {
					move(player, target, wasVisible, true);
				}
			}
			const ComponentId reveal = std::exchange(state.reveal, {});
			if (Component* shown = find(reveal); shown != nullptr && reveal == state.focused) {
				scrollIntoView(*shown);
			}
		}
	}

	// Each scroller above it, innermost first, scrolls the least that brings its box into view.
	void FocusManager::scrollIntoView(Component& target) {
		for (Component* node = find(target.parentId); node != nullptr; node = find(node->parentId)) {
			if (node->overflow != Overflow::Scroll) {
				continue;
			}
			context.refreshGeometry();
			const Foundation::Rect box = target.screenBounds();
			const Foundation::Vec2 a = node->screenToLocal({box.x, box.y});
			const Foundation::Vec2 b = node->screenToLocal({box.right(), box.bottom()});
			const Foundation::Vec2 first = glm::min(a, b);
			const Foundation::Vec2 last = glm::max(a, b);
			Foundation::Vec2	   delta{0.0F, 0.0F};
			for (int axis = 0; axis < 2; ++axis) {
				if (first[axis] < 0.0F) {
					delta[axis] = first[axis];
				} else if (last[axis] > node->size[axis]) {
					delta[axis] = std::min(last[axis] - node->size[axis], first[axis]);
				}
			}
			node->scrollBy(delta);
		}
	}

} // namespace ui::detail
