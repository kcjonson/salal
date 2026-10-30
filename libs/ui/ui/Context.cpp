#include "ui/Context.h"

#include "ui/detail/Dispatcher.h"
#include "ui/detail/LayoutEngine.h"

#include <primitives/Primitives.h>
#include <utils/Log.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
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

		bool clips(Overflow overflow) {
			return overflow == Overflow::Clip || overflow == Overflow::Scroll;
		}

		const Foundation::Color kScrollThumb(1.0F, 1.0F, 1.0F, 0.35F);
		const Foundation::Color kScrollTrack(1.0F, 1.0F, 1.0F, 0.06F);
	} // namespace

	Context::Context(Foundation::Vec2 viewportSize)
		: windowSize(viewportSize),
		  viewport(viewportSize),
		  dispatcher(std::make_unique<detail::Dispatcher>(*this)) {}

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
		geometryDirty = true;
	}

	// The animator and the components that ask for updates arrive with phase 3's animation; until then
	// a frame's update only lets removals made outside a dispatch settle.
	void Context::update(double /*dt*/) {
		const Registry::DispatchScope scope(registry);
	}

	// From each relayout boundary something under changed, a root's box being the viewport; then the
	// screen maps and clips, the paint order, onLayout for whatever moved, and the hover again when
	// anything moved under a still pointer (R9.9). Callbacks may ask for another pass, the next frame's.
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
		if (geometryDirty) {
			for (const ComponentId rootId : roots) {
				if (Component* rootComponent = registry.find(rootId)) {
					updateScreen(*rootComponent, {}, std::nullopt, true, false);
				}
			}
			geometryDirty = false;
			changed = true;
		}
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
		hoverStale = false;
		if (changed) {
			dispatcher->hoverAgain();
		}
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
				updateScreen(*child, childParent, childClip, component.enabledInTree, childBlocked);
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
		for (std::size_t i = 0; i < rootCount; ++i) {
			paintContext(*orderScratch[i].component);
		}
		orderScratch.clear();

		if (order.size() > static_cast<std::size_t>(kMaxRank) + 1 && !rankOverflowLogged) {
			LOG_ERROR(
				UI,
				"%zu visible components in one slot, past the %d a draw key ranks; the rest share its last key",
				order.size(),
				kMaxRank + 1
			);
			rankOverflowLogged = true;
		}
		for (std::size_t rank = 0; rank < order.size(); ++rank) {
			Component* component = registry.find(order[rank]);
			component->key = static_cast<std::int32_t>(std::min(rank, static_cast<std::size_t>(kMaxRank)));
			component->lastKey = component->key;
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
				order.push_back(member.selfId);
			}
		}
		orderScratch.resize(begin);
	}

	void Context::collectContext(Component& parent, int inheritedZ) {
		for (const ComponentId childId : parent.children) {
			Component* child = registry.find(childId);
			if (child == nullptr || !child->isShown()) {
				continue;
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
		return dispatcher->dispatch(event, scale);
	}

	// Top down through the list painting reads, so the winner is the topmost painted component the
	// point is in: inside every clip above it, its own box after its transform is undone, edges
	// half-open (R8.12). Disabled components occlude (R9.5); a transparent one doesn't (R8.3).
	ComponentId Context::hitTest(Foundation::Vec2 point, bool* onScrollbar) const {
		for (auto it = hitList.rbegin(); it != hitList.rend(); ++it) {
			const Component* component = registry.find(it->id);
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

	void Context::removing(ComponentId root) {
		dispatcher->removing(root);
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
		component.effectiveOpacity = parentOpacity * component.style.opacity.value_or(1.0F);
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
				renderComponent(*child, component.effectiveOpacity);
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
