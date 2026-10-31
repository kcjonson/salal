#pragma once

// The scene's slot map, where its components live (docs/ui-design.md, "The slot map"). Each component
// is allocated once and filed in a slot; a ComponentId is the slot's index and its generation, so a
// lookup is an index and a compare. Components never move, and nothing keeps a pointer into the slot
// array, only ids. Game code never names it: the context owns one, and Ref and Component go through it.
//
// Removing a subtree makes its ids stale at once, so every Ref to it reads null from that statement on,
// and destroys the components when the outermost dispatch ends, so a component that removes itself or
// its parent from inside a call the framework made into it is still alive until that call returns.
// Outside a dispatch they are destroyed at once.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ui {

	class Component;

	struct ComponentId {
		std::uint32_t index = 0;
		std::uint32_t gen = 0; // 0 is no component; a slot's generations start at 1

		[[nodiscard]] bool valid() const { return gen != 0; }
		bool			   operator==(const ComponentId&) const = default;
	};

	class Registry {
	  public:
		Registry();
		~Registry();

		Registry(const Registry&) = delete;
		Registry& operator=(const Registry&) = delete;
		Registry(Registry&&) = delete;
		Registry& operator=(Registry&&) = delete;

		// The component `id` names; null for a removed one, an empty id or another registry's.
		[[nodiscard]] Component* find(ComponentId id) const {
			if (id.index >= slots.size()) {
				return nullptr;
			}
			const Slot& slot = slots[id.index];
			return slot.gen == id.gen ? slot.component.get() : nullptr;
		}

		ComponentId insert(std::unique_ptr<Component> component);
		// `root` and everything under it, through Component's children, read stale from now and are
		// destroyed after the dispatch. The caller has unlinked it from its parent.
		void release(ComponentId root);

		// Live components, not counting removed ones still waiting for their dispatch to end.
		[[nodiscard]] std::size_t size() const { return live; }

		// A call into component code: removals inside it free when the outermost scope ends. The context
		// opens one around onMount, update and render; input dispatch opens its own.
		class DispatchScope {
		  public:
			explicit DispatchScope(Registry& owner);
			~DispatchScope();
			DispatchScope(const DispatchScope&) = delete;
			DispatchScope& operator=(const DispatchScope&) = delete;
			DispatchScope(DispatchScope&&) = delete;
			DispatchScope& operator=(DispatchScope&&) = delete;

		  private:
			Registry& registry;
		};

	  private:
		struct Slot {
			std::unique_ptr<Component> component;
			std::uint32_t			   gen = 1;
		};

		void collect();

		std::vector<Slot>						slots;
		std::vector<std::uint32_t>				freeSlots;
		std::vector<std::unique_ptr<Component>> doomed; // released, waiting for the dispatch to end
		std::vector<std::uint32_t>				doomedSlots;
		std::size_t								live = 0;
		int										dispatchDepth = 0;
	};

} // namespace ui
