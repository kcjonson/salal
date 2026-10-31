#include "ui/Registry.h"

#include "ui/Component.h"

#include <utility>

namespace ui {

	Registry::Registry() = default;

	// Components are destroyed in slot order, never through one another, so the order doesn't matter.
	Registry::~Registry() {
		doomed.clear();
		slots.clear();
	}

	ComponentId Registry::insert(std::unique_ptr<Component> component) {
		std::uint32_t index = 0;
		if (!freeSlots.empty()) {
			index = freeSlots.back();
			freeSlots.pop_back();
		} else {
			index = static_cast<std::uint32_t>(slots.size());
			slots.emplace_back();
		}
		Slot& slot = slots[index];
		slot.component = std::move(component);
		++live;
		return {.index = index, .gen = slot.gen};
	}

	void Registry::release(ComponentId root) {
		if (find(root) == nullptr) {
			return;
		}
		std::vector<ComponentId> pending{root};
		while (!pending.empty()) {
			const ComponentId id = pending.back();
			pending.pop_back();
			Slot& slot = slots[id.index];
			for (const ComponentId child : slot.component->getChildren()) {
				pending.push_back(child);
			}
			++slot.gen;
			if (slot.gen == 0) {
				slot.gen = 1; // wrapped: 0 is no component
			}
			doomed.push_back(std::move(slot.component));
			doomedSlots.push_back(id.index);
			--live;
		}
		if (dispatchDepth == 0) {
			collect();
		}
	}

	void Registry::collect() {
		// A destructor could release more; take the list first.
		while (!doomed.empty()) {
			std::vector<std::unique_ptr<Component>> dying = std::exchange(doomed, {});
			dying.clear();
		}
		freeSlots.insert(freeSlots.end(), doomedSlots.begin(), doomedSlots.end());
		doomedSlots.clear();
	}

	Registry::DispatchScope::DispatchScope(Registry& owner)
		: registry(owner) {
		++registry.dispatchDepth;
	}

	Registry::DispatchScope::~DispatchScope() {
		if (--registry.dispatchDepth == 0) {
			registry.collect();
		}
	}

} // namespace ui
