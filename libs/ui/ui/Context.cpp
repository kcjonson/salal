#include "ui/Context.h"

#include "ui/detail/Dispatcher.h"
#include "ui/detail/FocusManager.h"
#include "ui/detail/LayoutEngine.h"
#include "ui/theme/Tokens.h"

#include <primitives/Primitives.h>
#include <utils/Log.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace ui {

	namespace detail {
		Registry* registryOf(const Context& context) {
			return &context.registry;
		}
	} // namespace detail

	namespace {
		// Set for a scope and cleared however it ends, so a component that throws in render() doesn't
		// leave the context refusing changes.
		class ScopedFlag {
		  public:
			explicit ScopedFlag(bool& scoped)
				: flag(scoped) {
				flag = true;
			}
			~ScopedFlag() { flag = false; }
			ScopedFlag(const ScopedFlag&) = delete;
			ScopedFlag& operator=(const ScopedFlag&) = delete;
			ScopedFlag(ScopedFlag&&) = delete;
			ScopedFlag& operator=(ScopedFlag&&) = delete;

		  private:
			bool& flag;
		};

		bool inside(const Foundation::Rect& rect, Foundation::Vec2 point) {
			return point.x >= rect.x && point.y >= rect.y && point.x < rect.right() && point.y < rect.bottom();
		}

		Foundation::Rect intersect(const Foundation::Rect& a, const Foundation::Rect& b) {
			const float left = std::max(a.x, b.x);
			const float top = std::max(a.y, b.y);
			const float right = std::min(a.right(), b.right());
			const float bottom = std::min(a.bottom(), b.bottom());
			return {left, top, std::max(right - left, 0.0F), std::max(bottom - top, 0.0F)};
		}

		constexpr double kMaxUpdateDt = 0.25; // seconds; a stalled frame doesn't jump animations (R13.9)

		bool clips(Overflow overflow) {
			return overflow == Overflow::Clip || overflow == Overflow::Scroll;
		}

		const Foundation::Color kScrollThumb(1.0F, 1.0F, 1.0F, 0.35F);
		const Foundation::Color kScrollTrack(1.0F, 1.0F, 1.0F, 0.06F);

		// ui's tokens alone, loaded once; every context starts from them. They were checked when ui built.
		const Theme& uiTheme() {
			static const Theme kTheme = Theme::load({}).theme;
			return kTheme;
		}
	} // namespace

	Context::Context(Foundation::Vec2 viewportSize)
		: animator(std::make_unique<Animator>(clock)),
		  theme(uiTheme()),
		  windowSize(viewportSize),
		  viewport(viewportSize),
		  dispatcher(std::make_unique<detail::Dispatcher>(*this)),
		  focusManager(std::make_unique<detail::FocusManager>(*this)) {
		applyMotion();
	}

	// Tweens without their own duration and easing take the theme's.
	void Context::applyMotion() {
		animator->setDefaults(theme.get(tokens::motion::duration::normal), Easing(theme.get(tokens::motion::easing::standard)));
	}

	void Context::setTheme(Theme value) {
		if (drawing) {
			LOG_ERROR(UI, "setTheme while the ui draws; the theme is unchanged");
			return;
		}
		theme = std::move(value);
		applyMotion();
		for (const ComponentId rootId : roots) {
			if (Component* rootComponent = registry.find(rootId)) {
				restyle(*rootComponent);
			}
		}
		geometryDirty = true;
	}

	void Context::restyle(Component& component) {
		component.setStyle(component.declaredStyle);
		for (const ComponentId childId : component.children) {
			if (Component* child = registry.find(childId)) {
				restyle(*child);
			}
		}
	}

	Context::~Context() = default;

	Ref<Component> Context::root() {
		if (registry.find(defaultRoot) == nullptr) {
			defaultRoot = addRoot<Component>({.id = "root"}).componentId();
		}
		return {defaultRoot, &registry};
	}

	ComponentId Context::adoptRoot(std::unique_ptr<Component> component, const Component::Args& args) {
		if (drawing) {
			LOG_ERROR(UI, "addRoot while the ui draws; nothing added");
			return {};
		}
		Component& added = *component;
		added.setCommon(args);
		added.position = {0.0F, 0.0F};
		added.sizeSpec = Size(viewport);
		added.size = viewport;
		added.margin = {};
		added.isolation = true;
		const ComponentId rootId = registry.insert(std::move(component));
		added.selfId = rootId;
		added.context = this;
		roots.push_back(rootId);
		dirtyBoundaries.push_back(rootId);
		geometryDirty = true;
		orderDirty = true;
		added.setStyle(added.declaredStyle);
		{
			const Registry::DispatchScope scope(registry);
			added.onMount();
		}
		return rootId;
	}

	void Context::removeRoot(Ref<Component> rootRef) {
		const ComponentId rootId = rootRef.componentId();
		if (rootRef.registry != &registry || std::ranges::find(roots, rootId) == roots.end() || registry.find(rootId) == nullptr) {
			LOG_WARNING(UI, "removeRoot of a component that isn't one of this context's roots; ignored");
			return;
		}
		if (drawing) {
			LOG_ERROR(UI, "removeRoot while the ui draws; ignored");
			return;
		}
		std::erase(roots, rootId);
		orderDirty = true;
		removing(rootId);
		registry.release(rootId);
	}

	void Context::setViewport(Foundation::Vec2 size) {
		if (size == windowSize) {
			return;
		}
		windowSize = size;
		resizeRoots();
	}

	void Context::setScale(float value) {
		const float clamped = std::clamp(value, 0.25F, 4.0F);
		if (clamped == scale) {
			return;
		}
		scale = clamped;
		resizeRoots();
	}

	void Context::resizeRoots() {
		viewport = windowSize / scale;
		for (const ComponentId rootId : roots) {
			if (Component* rootComponent = registry.find(rootId)) {
				rootComponent->sizeSpec = Size(viewport);
				rootComponent->size = viewport;
				rootComponent->layoutDirty = true;
				dirtyBoundaries.push_back(rootId);
			}
		}
		for (const OverlayEntry& open : overlayStack) {
			const auto* absolute = std::get_if<Absolute>(&open.options.placement);
			if (Component* entry = registry.find(open.id); entry != nullptr && absolute != nullptr && absolute->cover) {
				entry->sizeSpec = Size(viewport);
				entry->layoutChanged(true);
			}
		}
		geometryDirty = true;
	}

	void Context::update(double dt) {
		const Registry::DispatchScope scope(registry);
		const double				  seconds = std::isfinite(dt) ? std::clamp(dt, 0.0, kMaxUpdateDt) : 0.0;
		updateTooltip(); // a hover since the last update started then, not now
		clock.advance(seconds * 1000.0);
		animator->tick();
		const std::vector<ComponentId> requested = std::exchange(updateRequests, {});
		for (const ComponentId id : requested) {
			Component* component = registry.find(id);
			if (component == nullptr) {
				continue;
			}
			component->updateRequested = false;
			bool shown = true;
			for (const Component* node = component; node != nullptr && shown; node = registry.find(node->parentId)) {
				shown = node->isShown();
			}
			if (shown) {
				component->update(static_cast<float>(seconds));
			}
		}
		updateTooltip();
	}

	// From each relayout boundary something under changed, a root's box being the viewport; then the
	// screen maps and clips, the paint order, onLayout for whatever moved, focus's upkeep, and the hover
	// again when anything moved under a still pointer (R9.9). Callbacks may ask for another pass, the
	// next frame's.
	void Context::layout() {
		const Registry::DispatchScope scope(registry);
		std::vector<ComponentId>	  moved;
		bool						  changed = hoverStale;
		if (!dirtyBoundaries.empty()) {
			++layoutPass;
			detail::LayoutEngine		   engine(registry, layoutPass, moved);
			const std::vector<ComponentId> boundaries = std::exchange(dirtyBoundaries, {});
			for (const ComponentId boundaryId : boundaries) {
				Component* boundary = registry.find(boundaryId);
				if (boundary == nullptr || !boundary->layoutDirty) {
					continue;
				}
				if (!boundary->parentId.valid()) {
					boundary->size = viewport;
					boundary->offset = {0.0F, 0.0F};
				}
				engine.arrange(*boundary);
			}
			geometryDirty = true;
		}
		changed = refreshGeometry() || changed;
		placeOverlays();
		changed = refreshGeometry() || changed;
		if (orderDirty) {
			buildOrder();
			orderDirty = false;
			changed = true;
		}
		for (const ComponentId movedId : moved) {
			Component* component = registry.find(movedId);
			if (component != nullptr && component->handlers && component->handlers->onLayout) {
				component->handlers->onLayout(*component);
			}
		}
		focusManager->afterLayout();
		changed = refreshGeometry() || hoverStale || changed;
		hoverStale = false;
		if (changed) {
			dispatcher->hoverAgain();
		}
	}

	bool Context::refreshGeometry() {
		if (!geometryDirty) {
			return false;
		}
		for (const ComponentId rootId : roots) {
			if (Component* rootComponent = registry.find(rootId)) {
				updateScreen(*rootComponent, {}, std::nullopt, true, false);
			}
		}
		geometryDirty = false;
		return true;
	}

	// A scroller's children sit under its scroll offset; a disabled ancestor disables them; a
	// pointerEvents of Unit or None keeps the pointer off everything under it (R8.29).
	void Context::updateScreen(
		Component&							   component,
		const Affine&						   parent,
		const std::optional<Foundation::Rect>& parentClip,
		bool								   parentEnabled,
		bool								   blocked
	) {
		component.screen = parent * Affine::local(component.offset + component.margin.topLeft(), component.size, component.transform);
		component.clip = parentClip;
		component.enabledInTree = parentEnabled && component.enabled;
		component.hitBlocked = blocked;
		std::optional<Foundation::Rect> childClip = parentClip;
		if (clips(component.overflow)) {
			const Foundation::Rect box = component.screenBounds();
			childClip = childClip ? intersect(*childClip, box) : box;
		}
		const PointerEvents pointer = component.getPointerEvents();
		const bool			childBlocked = blocked || pointer == PointerEvents::Unit || pointer == PointerEvents::None;
		const Affine		childParent =
			   component.scroll == Foundation::Vec2{0.0F, 0.0F} ? component.screen : component.screen * Affine::translation(-component.scroll);
		for (const ComponentId childId : component.children) {
			if (Component* child = registry.find(childId)) {
				const bool entry = child->isOverlay();
				updateScreen(
					*child,
					child->detached ? Affine{} : childParent,
					entry ? std::nullopt : childClip,
					component.enabledInTree,
					!entry && childBlocked
				);
			}
		}
	}

	void Context::buildOrder() {
		order.clear();
		orderScratch.clear();
		for (const ComponentId rootId : roots) {
			Component* rootComponent = registry.find(rootId);
			if (rootComponent != nullptr && rootComponent->isShown()) {
				orderScratch.push_back({.component = rootComponent, .z = rootComponent->zIndex.value_or(0)});
			}
		}
		const std::size_t rootCount = orderScratch.size();
		std::ranges::stable_sort(orderScratch, {}, &OrderItem::z);
		paintingSlot = 0;
		for (std::size_t i = 0; i < rootCount; ++i) {
			paintContext(*orderScratch[i].component);
		}
		orderScratch.clear();
		int stackSlot = 0;
		for (const bool tooltips : {false, true}) {
			for (const OverlayEntry& entry : overlayStack) {
				Component* component = registry.find(entry.id);
				if (component == nullptr || (entry.options.kind == OverlayKind::Tooltip) != tooltips || !shownInTree(*component)) {
					continue;
				}
				paintingSlot = tooltips ? kTooltipSlot : std::min(++stackSlot, kMaxStackEntries);
				paintContext(*component);
			}
		}
		paintingSlot = 0;

		// A component's key is its slot over its rank in that slot.
		int			slotNow = -1;
		std::size_t rank = 0;
		for (const ComponentId id : order) {
			Component* component = registry.find(id);
			if (component->slot != slotNow) {
				slotNow = component->slot;
				rank = 0;
			}
			if (rank > static_cast<std::size_t>(kMaxRank) && !rankOverflowLogged) {
				LOG_ERROR(UI, "more than %d visible components in slot %d; the rest share its last key", kMaxRank + 1, slotNow);
				rankOverflowLogged = true;
			}
			component->key = (slotNow << kRankBits) | static_cast<std::int32_t>(std::min(rank, static_cast<std::size_t>(kMaxRank)));
			component->lastKey = component->key;
			++rank;
		}
		// Each component's last key is its subtree's highest, children after parents in reverse.
		for (auto it = order.rbegin(); it != order.rend(); ++it) {
			const Component* component = registry.find(*it);
			if (Component* parentNode = registry.find(component->parentId)) {
				parentNode->lastKey = std::max(parentNode->lastKey, component->lastKey);
			}
		}

		std::vector<std::pair<std::int32_t, ComponentId>> bars;
		for (const ComponentId id : order) {
			const Component* component = registry.find(id);
			if (component->overflow == Overflow::Scroll) {
				bars.emplace_back(component->lastKey, id);
			}
		}
		std::ranges::stable_sort(bars, {}, &std::pair<std::int32_t, ComponentId>::first);
		hitList.clear();
		hitList.reserve(order.size() + bars.size());
		std::size_t bar = 0;
		for (const ComponentId id : order) {
			if (registry.find(id)->slot == kTooltipSlot) {
				continue;
			}
			hitList.push_back({.id = id, .scrollbar = false});
			const std::int32_t key = registry.find(id)->key;
			for (; bar < bars.size() && bars[bar].first == key; ++bar) {
				hitList.push_back({.id = bars[bar].second, .scrollbar = true});
			}
		}
	}

	// The context's root, then its members by (z, tree order), each nested context in place as one.
	// Members of every open context share orderScratch: a nested call appends after this one's and
	// truncates back, so indices hold where references wouldn't.
	void Context::paintContext(Component& contextRoot) {
		contextRoot.slot = paintingSlot;
		order.push_back(contextRoot.selfId);
		const std::size_t begin = orderScratch.size();
		collectContext(contextRoot, 0);
		const std::size_t end = orderScratch.size();
		std::stable_sort(
			orderScratch.begin() + static_cast<std::ptrdiff_t>(begin),
			orderScratch.begin() + static_cast<std::ptrdiff_t>(end),
			[](const OrderItem& a, const OrderItem& b) { return a.z < b.z; }
		);
		for (std::size_t i = begin; i < end; ++i) {
			Component& member = *orderScratch[i].component;
			if (member.isolation) {
				paintContext(member);
			} else {
				member.slot = paintingSlot;
				order.push_back(member.selfId);
			}
		}
		orderScratch.resize(begin);
	}

	void Context::collectContext(Component& parent, int inheritedZ) {
		for (const ComponentId childId : parent.children) {
			Component* child = registry.find(childId);
			if (child == nullptr || !child->isShown() || child->isOverlay()) {
				continue; // an entry paints in its own slot
			}
			const int z = child->zIndex.value_or(inheritedZ);
			orderScratch.push_back({.component = child, .z = z});
			if (!child->isolation) {
				collectContext(*child, z);
			}
		}
	}

	// --- Input ---

	bool Context::dispatch(const foundation::InputEvent& event) {
		const Registry::DispatchScope scope(registry);
		layout();
		using Type = foundation::InputEvent::Type;
		switch (event.type) {
			case Type::KeyDown:
			case Type::KeyUp:
			case Type::Char:
			case Type::ButtonDown:
			case Type::ButtonUp:
			case Type::Axis:
			case Type::DeviceDisconnected:
				return focusManager->dispatch(event);
			case Type::PointerDown:
				closeTooltip();
				tipHeld = true;
				return dispatcher->dispatch(event, scale);
			default:
				return dispatcher->dispatch(event, scale);
		}
	}

	// Top down through the list painting reads, so the winner is the topmost painted component the
	// point is in: inside every clip above it, its own box after its transform is undone, edges
	// half-open (R8.12). Disabled components occlude (R9.5); a transparent one doesn't (R8.3).
	ComponentId Context::hitTest(Foundation::Vec2 point, bool* onScrollbar) const {
		const int floor = modalFloor();
		for (auto it = hitList.rbegin(); it != hitList.rend(); ++it) {
			const Component* component = registry.find(it->id);
			if (component != nullptr && component->slot < floor) {
				break; // inert under a modal
			}
			if (component == nullptr || (component->clip && !inside(*component->clip, point))) {
				continue;
			}
			const Foundation::Vec2 local = component->screenToLocal(point);
			if (it->scrollbar) {
				const detail::Scrollbar bar = detail::scrollbarOf(component->size, component->scroll, component->scrollMax);
				if (bar.shown && inside(bar.track, local)) {
					if (onScrollbar != nullptr) {
						*onScrollbar = true;
					}
					return component->selfId;
				}
				continue;
			}
			const PointerEvents pointer = component->getPointerEvents();
			if (component->hitBlocked || component->effectiveOpacity <= 0.0F ||
				(pointer != PointerEvents::Auto && pointer != PointerEvents::Unit)) {
				continue;
			}
			if (component->containsPoint(local)) {
				return component->selfId;
			}
		}
		return {};
	}

	Cursor Context::cursor() const {
		return dispatcher->cursor();
	}

	void Context::cancelInteraction() {
		const Registry::DispatchScope scope(registry);
		dispatcher->cancel();
	}

	// The subtree's tweens and timers go with it. Focus goes first, while the entries it reads are open.
	void Context::removing(ComponentId root) {
		dispatcher->removing(root);
		focusManager->removing(root);
		std::erase_if(overlayStack, [&](const OverlayEntry& entry) { return within(entry.id, root); });
		std::vector<ComponentId> subtree{root};
		for (std::size_t i = 0; i < subtree.size(); ++i) {
			if (const Component* node = registry.find(subtree[i])) {
				subtree.insert(subtree.end(), node->children.begin(), node->children.end());
			}
		}
		animator->ownersRemoved(subtree);
	}

	bool Context::shownInTree(const Component& component) const {
		for (const Component* node = &component; node != nullptr; node = registry.find(node->parentId)) {
			if (!node->isShown()) {
				return false;
			}
		}
		return true;
	}

	bool Context::within(ComponentId id, ComponentId root) const {
		for (const Component* node = registry.find(id); node != nullptr; node = registry.find(node->parentId)) {
			if (node->selfId == root) {
				return true;
			}
		}
		return false;
	}

	// --- The overlay stack ---

	// An entry leaves its opener's flow and becomes a stacking context. A menu closes its player's
	// other menus, except the ones it was opened from inside.
	void Context::openOverlay(ComponentId entryId, ComponentId opener, OverlayOptions options) {
		Component* entry = registry.find(entryId);
		if (entry == nullptr) {
			return;
		}
		const auto stacked =
			std::ranges::count_if(overlayStack, [](const OverlayEntry& each) { return each.options.kind != OverlayKind::Tooltip; });
		if (options.kind != OverlayKind::Tooltip && stacked >= kMaxStackEntries) {
			LOG_ERROR(UI, "%d overlay stack entries open; this one shares the top slot", kMaxStackEntries);
		}
		entry->overlayKind = options.kind;
		entry->isolation = true;
		entry->layout.position = Position::Absolute;
		if (const auto* absolute = std::get_if<Absolute>(&options.placement)) {
			entry->detached = true;
			if (absolute->cover) {
				entry->sizeSpec = Size(viewport);
			}
		}
		entry->layoutChanged(true);
		orderDirty = true;
		geometryDirty = true;
		std::vector<ComponentId> closing;
		if (options.kind == OverlayKind::Menu) {
			for (auto open = overlayStack.rbegin(); open != overlayStack.rend(); ++open) { // the topmost first, so each hears its dismissal
				if (open->options.kind == OverlayKind::Menu && open->options.player == options.player && !within(opener, open->id)) {
					closing.push_back(open->id);
				}
			}
		}
		const ComponentId restore = focusManager->focused(options.player);
		overlayStack.push_back({.id = entryId, .opener = opener, .options = std::move(options), .restore = restore});
		for (const ComponentId id : closing) {
			dismiss(id);
		}
	}

	void Context::closeOverlay(Ref<Component> entryRef) {
		Component* entry = entryRef.registry == &registry ? entryRef.get() : nullptr;
		if (entry == nullptr || !entry->isOverlay()) {
			return;
		}
		if (Component* opener = registry.find(entry->parentId)) {
			const Registry::DispatchScope scope(registry);
			opener->removeChild(entryRef);
		}
	}

	void Context::dismissOverlay(Ref<Component> entryRef) {
		if (entryRef.registry == &registry) {
			dismiss(entryRef.componentId());
		}
	}

	void Context::dismiss(ComponentId entryId) {
		const auto open = std::ranges::find(overlayStack, entryId, &OverlayEntry::id);
		if (open == overlayStack.end()) {
			return;
		}
		const Registry::DispatchScope scope(registry);
		const std::function<void()>	  onDismiss = open->options.onDismiss;
		if (onDismiss) {
			onDismiss();
		}
		closeOverlay({entryId, &registry});
	}

	void Context::bringToFront(Ref<Component> entryRef) {
		if (entryRef.registry != &registry || entryRef.get() == nullptr || !entryRef.get()->isOverlay()) {
			return;
		}
		const ComponentId front = entryRef.componentId();
		std::stable_partition(overlayStack.begin(), overlayStack.end(), [&](const OverlayEntry& entry) {
			return !within(entry.id, front);
		});
		orderDirty = true;
	}

	std::vector<ComponentId> Context::overlays() const {
		std::vector<ComponentId> ids;
		ids.reserve(overlayStack.size());
		for (const OverlayEntry& entry : overlayStack) {
			ids.push_back(entry.id);
		}
		return ids;
	}

	// After the layout: an anchored overlay where placement puts it against its origin on screen,
	// converted to its opener's child space (inside its scroll); an absolute one at its screen point,
	// which is its own space, or over the viewport.
	void Context::placeOverlays() {
		const Foundation::Rect screen{0.0F, 0.0F, viewport.x, viewport.y};
		for (const OverlayEntry& open : overlayStack) {
			Component* entry = registry.find(open.id);
			Component* opener = registry.find(open.opener);
			if (entry == nullptr || opener == nullptr || !entry->isShown()) {
				continue;
			}
			const Foundation::Vec2 size = entry->size + entry->margin.total();
			Foundation::Vec2	   at{0.0F, 0.0F};
			if (const auto* anchored = std::get_if<Anchored>(&open.options.placement)) {
				Foundation::Rect origin = opener->screenBounds();
				if (anchored->point) {
					const Foundation::Vec2 point = opener->localToScreen(*anchored->point);
					origin = {point.x, point.y, 0.0F, 0.0F};
				}
				const Foundation::Rect rect = place(origin, size, screen, *anchored);
				at = opener->screenToLocal({rect.x, rect.y}) + opener->scroll;
			} else if (const auto& absolute = std::get<Absolute>(open.options.placement); !absolute.cover) {
				at = absolute.at;
				if (absolute.margin) {
					at.x = std::clamp(at.x, *absolute.margin, std::max(viewport.x - *absolute.margin - size.x, *absolute.margin));
					at.y = std::clamp(at.y, *absolute.margin, std::max(viewport.y - *absolute.margin - size.y, *absolute.margin));
				}
			}
			if (at != entry->offset) {
				entry->offset = at;
				entry->position = at;
				geometryDirty = true;
			}
		}
	}

	// A press outside the pointer player's menus closes them; it is consumed unless it is a secondary
	// press or lands on an overlay trigger (R9.13).
	bool Context::pressOutside(ComponentId hit, foundation::MouseButton button) {
		const int				 pointerPlayer = focusManager->mousePlayer();
		std::vector<ComponentId> closing;
		for (auto it = overlayStack.rbegin(); it != overlayStack.rend(); ++it) {
			if (it->options.kind == OverlayKind::Menu && it->options.player == pointerPlayer && !within(hit, it->id)) {
				closing.push_back(it->id);
			}
		}
		if (closing.empty()) {
			return false;
		}
		bool trigger = false;
		for (const Component* node = registry.find(hit); node != nullptr && !trigger; node = registry.find(node->parentId)) {
			trigger = node->overlayTrigger;
		}
		for (const ComponentId id : closing) {
			dismiss(id);
		}
		return button == foundation::MouseButton::Left && !trigger;
	}

	int Context::modalFloor() const {
		const int pointerPlayer = focusManager->mousePlayer();
		int		  floor = 0;
		for (const OverlayEntry& open : overlayStack) {
			const Component* entry = registry.find(open.id);
			if (entry != nullptr && open.options.kind == OverlayKind::Dialog && open.options.player == pointerPlayer && entry->isShown()) {
				floor = std::max(floor, entry->slot);
			}
		}
		return floor;
	}

	bool Context::cancelOverlay(int player) {
		ComponentId target;
		for (const Component* node = registry.find(focusManager->focused(player)); node != nullptr && !target.valid();
			 node = registry.find(node->parentId)) {
			if (node->overlayKind && *node->overlayKind != OverlayKind::Toast && *node->overlayKind != OverlayKind::Tooltip) {
				target = node->selfId;
			}
		}
		for (auto it = overlayStack.rbegin(); it != overlayStack.rend() && !target.valid(); ++it) {
			if (it->options.kind == OverlayKind::Menu && it->options.player == player) {
				target = it->id;
			}
		}
		if (!target.valid()) {
			return false;
		}
		dismiss(target);
		return true;
	}

	void Context::focusMoved(int player, ComponentId from) {
		const ComponentId		 to = focusManager->focused(player);
		std::vector<ComponentId> closing;
		for (auto it = overlayStack.rbegin(); it != overlayStack.rend(); ++it) {
			if (it->options.kind == OverlayKind::Menu && it->options.player == player && within(from, it->id) && !within(to, it->id)) {
				closing.push_back(it->id);
			}
		}
		for (const ComponentId id : closing) {
			dismiss(id);
		}
	}

	ComponentId Context::overlayRestore(ComponentId focusedId, ComponentId root) const {
		ComponentId restore;
		for (const Component* node = registry.find(focusedId); node != nullptr; node = registry.find(node->parentId)) {
			if (const auto open = std::ranges::find(overlayStack, node->selfId, &OverlayEntry::id); open != overlayStack.end()) {
				restore = open->restore;
			}
			if (node->selfId == root) {
				break;
			}
		}
		return restore;
	}

	// The innermost hovered component with a tooltip owns the tier's one hover tooltip.
	void Context::updateTooltip() {
		ComponentId owner;
		for (const ComponentId id : dispatcher->hovered()) {
			if (const Component* node = registry.find(id); node != nullptr && node->tooltip) {
				owner = id;
				break;
			}
		}
		if (owner != tipOwner) {
			closeTooltip();
			tipOwner = owner;
			tipSince = clock.now();
			tipHeld = false;
		}
		Component* component = registry.find(tipOwner);
		if (component == nullptr || tipHeld || registry.find(tipEntry) != nullptr || clock.now() - tipSince < kTooltipDelayMs) {
			return;
		}
		tipEntry = component->tooltip(*component, component->screenToLocal(dispatcher->pointerAt())).componentId();
	}

	void Context::closeTooltip() {
		if (registry.find(tipEntry) != nullptr) {
			closeOverlay({tipEntry, &registry});
		}
		tipEntry = {};
	}

	void Context::pointerPressed(ComponentId hit) {
		focusManager->pointerPressed(hit);
	}

	// --- Focus ---

	bool Context::focus(Ref<Component> target, int player) {
		if (target.registry != &registry) {
			return false;
		}
		const Registry::DispatchScope scope(registry);
		return focusManager->focus(target.componentId(), player);
	}

	void Context::clearFocus(int player) {
		const Registry::DispatchScope scope(registry);
		focusManager->clear(player);
	}

	Ref<Component> Context::focused(int player) {
		return {focusManager->focused(player), &registry};
	}

	bool Context::focusNext(int player) {
		const Registry::DispatchScope scope(registry);
		return focusManager->next(player, false);
	}

	bool Context::focusPrevious(int player) {
		const Registry::DispatchScope scope(registry);
		return focusManager->next(player, true);
	}

	bool Context::focusDirection(Direction direction, int player) {
		const Registry::DispatchScope scope(registry);
		return focusManager->direction(player, direction);
	}

	void Context::pushScope(Ref<Component> scopeRoot, int player) {
		if (scopeRoot.registry != &registry) {
			return;
		}
		const Registry::DispatchScope scope(registry);
		focusManager->pushScope(scopeRoot.componentId(), player);
	}

	void Context::popScope(int player) {
		const Registry::DispatchScope scope(registry);
		focusManager->popScope(player);
	}

	void Context::setNavigation(int player, foundation::ActionMap map) {
		focusManager->setNavigation(player, std::move(map));
	}

	void Context::setPlayerDevices(int player, std::span<const foundation::DeviceId> devices) {
		focusManager->setDevices(player, devices);
	}

	InputMode Context::inputMode() const {
		return focusManager->inputMode();
	}

	void Context::capture(ComponentId captorId) {
		dispatcher->capture(captorId);
	}

	ComponentId Context::captor() const {
		return dispatcher->captor();
	}

	// --- Drawing ---

	void Context::render() {
		layout();
		const Registry::DispatchScope scope(registry);
		const ScopedFlag			  flag(drawing);
		if (scale != 1.0F) {
			Renderer::Primitives::PushTransform(glm::scale(Foundation::Mat4(1.0F), Foundation::Vec3(scale, scale, 1.0F)));
		}
		for (const ComponentId rootId : roots) {
			if (Component* rootComponent = registry.find(rootId)) {
				renderComponent(*rootComponent, 1.0F);
			}
		}
		for (const OverlayEntry& open : overlayStack) {
			if (Component* entry = registry.find(open.id); entry != nullptr && entry->detached && shownInTree(*entry)) {
				renderComponent(*entry, 1.0F);
			}
		}
		if (scale != 1.0F) {
			Renderer::Primitives::PopTransform();
		}
	}

	// Depth first in tree order, so each component draws under its ancestors' transforms and clips; its
	// key, not the walk, puts it in paint order. Overflow::Clip and Scroll cut the children, not the
	// component's own draws; a scroller's bar draws over its subtree, at the subtree's last key.
	void Context::renderComponent(Component& component, float parentOpacity) {
		if (!component.isShown()) {
			return;
		}
		component.effectiveOpacity = parentOpacity * component.style.opacity.value_or(1.0F) * component.fade * component.stateOpacity();
		const Affine local = Affine::local(component.offset + component.margin.topLeft(), component.size, component.transform);
		const bool	 moved = !local.isTranslation() || local.tx != 0.0F || local.ty != 0.0F;
		if (moved) {
			Renderer::Primitives::PushTransform(local.toMat4());
		}
		component.render();
		const bool clipped = clips(component.overflow) && !component.children.empty();
		if (clipped) {
			Renderer::Primitives::pushClip({0.0F, 0.0F, component.size.x, component.size.y});
		}
		const bool scrolled = component.scroll != Foundation::Vec2{0.0F, 0.0F};
		if (scrolled) {
			Renderer::Primitives::PushTransform(Affine::translation(-component.scroll).toMat4());
		}
		for (const ComponentId childId : component.children) {
			if (Component* child = registry.find(childId)) {
				if (child->detached) {
					continue; // drawn from the top, in screen space
				}
				const bool entry = child->isOverlay();
				if (entry) {
					Renderer::Primitives::pushClipReset();
				}
				renderComponent(*child, component.effectiveOpacity);
				if (entry) {
					Renderer::Primitives::popClip();
				}
			}
		}
		if (scrolled) {
			Renderer::Primitives::PopTransform();
		}
		if (component.overflow == Overflow::Scroll) {
			drawScrollbar(component);
		}
		if (clipped) {
			Renderer::Primitives::popClip();
		}
		if (moved) {
			Renderer::Primitives::PopTransform();
		}
	}

	void Context::drawScrollbar(const Component& scroller) const {
		const detail::Scrollbar bar = detail::scrollbarOf(scroller.size, scroller.scroll, scroller.scrollMax);
		if (!bar.shown) {
			return;
		}
		const float radius = bar.track.width * 0.5F;
		const auto	rounded = [&](const Foundation::Rect& rect, Foundation::Color color) {
			 color.a *= scroller.effectiveOpacity;
			 Renderer::Primitives::drawRect({
				 .bounds = rect,
				 .style =
					 {.fill = color,
					  .border = Foundation::BorderStyle{.color = {0.0F, 0.0F, 0.0F, 0.0F}, .width = 0.0F, .cornerRadius = radius}},
				 .key = scroller.lastKey,
			 });
		};
		rounded(bar.track, kScrollTrack);
		rounded(bar.thumb, kScrollThumb);
	}

} // namespace ui
