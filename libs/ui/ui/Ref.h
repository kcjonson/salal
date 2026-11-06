#pragma once

// How anything holds a component past one statement (docs/ui-design.md, "Component API"): an id in its
// scene's slot map, cheap to copy, whose get() is null once the component is removed. Its member
// templates are defined in ui/Component.h, which includes this.

#include "ui/Registry.h"

#include <concepts>

namespace ui {

	class Component;
	class Context;

	namespace detail {
		class DragService;
	} // namespace detail

	template <typename T>
	class Ref {
	  public:
		Ref() = default;
		// A Ref to a derived type is a Ref to its base.
		template <typename U>
			requires std::derived_from<U, T>
		Ref(const Ref<U>& other) // NOLINT(google-explicit-constructor)
			: id(other.id),
			  registry(other.registry) {}

		// The component, for the current statement; null once it has been removed.
		[[nodiscard]] T* get() const;
		// Builds C under the component, as Component::addChild does; an empty Ref, logged, once it's gone.
		template <typename C>
		Ref<C> addChild(const typename C::Args& args) const;

		[[nodiscard]] ComponentId componentId() const { return id; }
		template <typename U>
		bool operator==(const Ref<U>& other) const {
			return id == other.id && registry == other.registry;
		}

	  private:
		template <typename>
		friend class Ref;
		friend class Component;
		friend class Context;
		friend class detail::DragService;

		Ref(ComponentId componentId, Registry* owner)
			: id(componentId),
			  registry(owner) {}

		ComponentId id;
		Registry*	registry = nullptr;
	};

} // namespace ui
