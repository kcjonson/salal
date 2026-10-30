#include "ui/Context.h"

#include <primitives/Primitives.h>
#include <utils/Log.h>

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
	} // namespace

	Context::Context(Foundation::Vec2 viewportSize)
		: viewport(viewportSize) {}

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
		added.size = viewport;
		added.isolation = true;
		const ComponentId rootId = registry.insert(std::move(component));
		added.selfId = rootId;
		added.context = this;
		roots.push_back(rootId);
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
		registry.release(rootId);
	}

	void Context::setViewport(Foundation::Vec2 size) {
		if (size == viewport) {
			return;
		}
		viewport = size;
		for (const ComponentId rootId : roots) {
			if (Component* rootComponent = registry.find(rootId)) {
				rootComponent->setSize(size);
			}
		}
		geometryDirty = true;
	}

	// The animator and the components that ask for updates arrive with phase 3's animation; until then
	// a frame's update only lets removals made outside a dispatch settle.
	void Context::update(double /*dt*/) {
		const Registry::DispatchScope scope(registry);
	}

	void Context::layout() {
		if (geometryDirty) {
			for (const ComponentId rootId : roots) {
				if (Component* rootComponent = registry.find(rootId)) {
					updateScreen(*rootComponent, {});
				}
			}
			geometryDirty = false;
		}
		if (orderDirty) {
			buildOrder();
			orderDirty = false;
		}
	}

	void Context::updateScreen(Component& component, const Affine& parent) {
		component.screen = parent * Affine::local(component.position, component.size, component.transform);
		for (const ComponentId childId : component.children) {
			if (Component* child = registry.find(childId)) {
				updateScreen(*child, component.screen);
			}
		}
	}

	void Context::buildOrder() {
		order.clear();
		orderScratch.clear();
		for (const ComponentId rootId : roots) {
			Component* rootComponent = registry.find(rootId);
			if (rootComponent != nullptr && rootComponent->visible) {
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
			registry.find(order[rank])->key = static_cast<std::int32_t>(std::min(rank, static_cast<std::size_t>(kMaxRank)));
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
			if (child == nullptr || !child->visible) {
				continue;
			}
			const int z = child->zIndex.value_or(inheritedZ);
			orderScratch.push_back({.component = child, .z = z});
			if (!child->isolation) {
				collectContext(*child, z);
			}
		}
	}

	void Context::render() {
		layout();
		const Registry::DispatchScope scope(registry);
		const ScopedFlag			  flag(drawing);
		for (const ComponentId rootId : roots) {
			if (Component* rootComponent = registry.find(rootId)) {
				renderComponent(*rootComponent, 1.0F);
			}
		}
	}

	// Depth first in tree order, so each component draws under its ancestors' transforms; its key, not
	// the walk, puts it in paint order.
	void Context::renderComponent(Component& component, float parentOpacity) {
		if (!component.visible) {
			return;
		}
		component.effectiveOpacity = parentOpacity * component.style.opacity.value_or(1.0F);
		const Affine local = Affine::local(component.position, component.size, component.transform);
		const bool	 moved = !local.isTranslation() || local.tx != 0.0F || local.ty != 0.0F;
		if (moved) {
			Renderer::Primitives::PushTransform(local.toMat4());
		}
		component.render();
		for (const ComponentId childId : component.children) {
			if (Component* child = registry.find(childId)) {
				renderComponent(*child, component.effectiveOpacity);
			}
		}
		if (moved) {
			Renderer::Primitives::PopTransform();
		}
	}

} // namespace ui
