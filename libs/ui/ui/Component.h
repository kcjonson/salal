#pragma once

// The one node every ui element is, leaf or widget (docs/ui-design.md, "Node model" and "Component
// API"). A parent's addChild builds a component from its Args and mounts it; the component adds its own
// children in onMount, never in its constructor, which touches no service. render() draws the
// component's own content only, and the context walks the children.
//
// Coordinates are logical pixels, y down, local to the parent: `position` is the box's top-left in the
// parent's space, and render() draws with (0, 0) at the component's own top-left. screenBounds() and
// localToScreen() reach the viewport; a component never adds up its ancestors' positions itself.
//
// A Ref is how anything holds a component past one statement: an id, cheap to copy, whose get() is
// null once the component is removed.
//
//   class HealthBar : public ui::Component {
//     public:
//       struct Args {
//           UI_COMMON_ARGS
//           float value = 1.0F;
//       };
//       explicit HealthBar(const Args& args) : value(args.value) {}
//     protected:
//       void onMount() override { fill = addChild<ui::Rectangle>({.size = {size.x * value, size.y}}); }
//     private:
//       float value;
//       ui::Ref<ui::Rectangle> fill;
//   };

#include "ui/Registry.h"
#include "ui/Style.h"
#include "ui/Transform.h"

#include <graphics/Rect.h>
#include <math/Types.h>

#include <nlohmann/json_fwd.hpp>

#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <typeinfo>
#include <vector>

// The attributes every component's Args starts with, in this order, since designated initializers
// follow declaration order: identity, geometry, look, ordering. A macro because a designated
// initializer can't name a base struct's fields (P2287 is aimed at C++29). A component's own fields
// come after it.
//
//   id         for the tree snapshot, the lint's paths and tests; empty for none
//   visible    false takes the subtree out of drawing, the lint and the paint order (state, not style)
//   enabled    reported by the snapshot; input reads it
//   position   the box's top-left in the parent's space
//   size       the box, px
//   transform  render and hit testing only
//   style      how it looks
//   zIndex     its place among everything in its nearest stacking context; unset takes its parent's
//   isolation  true makes it a stacking context, whose subtree paints as one at its zIndex
#define UI_COMMON_ARGS                                                                                                                     \
	::std::string		 id;                                                                                                               \
	bool				 visible = true;                                                                                                   \
	bool				 enabled = true;                                                                                                   \
	::Foundation::Vec2	 position{0.0F, 0.0F};                                                                                             \
	::Foundation::Vec2	 size{0.0F, 0.0F};                                                                                                 \
	::ui::Transform		 transform{};                                                                                                      \
	::ui::Style			 style{};                                                                                                          \
	::std::optional<int> zIndex;                                                                                                           \
	bool				 isolation = false;

namespace ui {

	class Component;
	class Context;

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

		Ref(ComponentId componentId, Registry* owner)
			: id(componentId),
			  registry(owner) {}

		ComponentId id;
		Registry*	registry = nullptr;
	};

	class Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
		};

		Component() = default;
		virtual ~Component() = default;

		Component(const Component&) = delete;
		Component& operator=(const Component&) = delete;
		Component(Component&&) = delete;
		Component& operator=(Component&&) = delete;

		// Builds T from `args` under this component and mounts it: T(args) where T has that
		// constructor, else T(), then the common attributes, then T's onMount. Refused, logged, as an
		// empty Ref before this component is mounted (from a constructor) and while the context draws.
		template <typename T>
		Ref<T> addChild(const typename T::Args& args);
		// Unlinks `child` now; it and its subtree read null from here and are destroyed after the
		// dispatch. Logged and ignored for a component that isn't this one's child.
		void						 removeChild(Ref<Component> child);
		[[nodiscard]] Ref<Component> parent() const;
		[[nodiscard]] Ref<Component> self() const;
		// In tree order, which is the order they were added.
		[[nodiscard]] const std::vector<ComponentId>& getChildren() const { return children; }
		// The context of the scene it was mounted in; null only inside its constructor.
		[[nodiscard]] Context* getContext() const { return context; }

		[[nodiscard]] const std::string& getId() const { return id; }
		[[nodiscard]] bool				 isVisible() const { return visible; }
		void							 setVisible(bool value);
		[[nodiscard]] bool				 isEnabled() const { return enabled; }
		void							 setEnabled(bool value) { enabled = value; }

		[[nodiscard]] Foundation::Vec2 getPosition() const { return position; }
		void						   setPosition(Foundation::Vec2 value);
		[[nodiscard]] Foundation::Vec2 getSize() const { return size; }
		virtual void				   setSize(Foundation::Vec2 value);
		[[nodiscard]] const Transform& getTransform() const { return transform; }
		void						   setTransform(const Transform& value);
		// The box in the parent's space.
		[[nodiscard]] Foundation::Rect getBounds() const { return {position.x, position.y, size.x, size.y}; }
		// The box in viewport pixels; under a rotation, the axis-aligned bounds of the turned box. Valid
		// after the frame's layout, as are the two maps.
		[[nodiscard]] Foundation::Rect screenBounds() const;
		[[nodiscard]] Foundation::Vec2 localToScreen(Foundation::Vec2 point) const;
		[[nodiscard]] Foundation::Vec2 screenToLocal(Foundation::Vec2 point) const;

		// The style it draws with: the one it was given, resolved against its scene's theme once mounted.
		[[nodiscard]] const Style& getStyle() const { return style; }
		virtual void			   setStyle(const Style& value);

		[[nodiscard]] std::optional<int> getZIndex() const { return zIndex; }
		void							 setZIndex(std::optional<int> value);
		[[nodiscard]] bool				 getIsolation() const { return isolation; }
		void							 setIsolation(bool value);
		// This component's place in its scene's paint order, which render() draws at. Valid after the
		// frame's layout.
		[[nodiscard]] std::int32_t drawKey() const { return key; }

		// The tree snapshot's and the lint's name for the type.
		[[nodiscard]] virtual const char* typeName() const { return "Component"; }
		// How far past its box it may draw (a shadow, a glow), in px: ink, which isn't overflow (R8.8).
		[[nodiscard]] virtual float inkExtent() const { return 0.0F; }
		// The theme style the type draws with under whatever its own style sets; none by default.
		[[nodiscard]] virtual StyleId themeStyle() const { return {}; }
		// Adds the type's own fields to its tree snapshot node, or overwrites the common ones it knows
		// better (the resolved `style`).
		virtual void describe(nlohmann::json& /*node*/) const {}

	  protected:
		virtual void onMount() {}
		// Its own draws, at drawKey(), in its own space; the opacity it draws with is opacity().
		virtual void render() {}

		// Its effective opacity, its own style's times its ancestors'. Valid in render().
		[[nodiscard]] float opacity() const { return effectiveOpacity; }

		Foundation::Vec2 position{0.0F, 0.0F};
		Foundation::Vec2 size{0.0F, 0.0F};
		Style			 style;

	  private:
		friend class Context;

		void		setCommon(const Args& args);
		ComponentId adopt(std::unique_ptr<Component> child);
		void		geometryChanged() const;
		void		orderChanged() const;

		template <typename A>
		static Args commonOf(const A& args) {
			return {
				.id = args.id,
				.visible = args.visible,
				.enabled = args.enabled,
				.position = args.position,
				.size = args.size,
				.transform = args.transform,
				.style = args.style,
				.zIndex = args.zIndex,
				.isolation = args.isolation,
			};
		}

		std::string				 id;
		Style					 declaredStyle; // as given; `style` is it resolved
		Transform				 transform;
		std::optional<int>		 zIndex;
		ComponentId				 selfId;
		ComponentId				 parentId;
		std::vector<ComponentId> children;
		Context*				 context = nullptr;
		Affine					 screen; // local to viewport, from the frame's layout
		std::int32_t			 key = 0;
		float					 effectiveOpacity = 1.0F;
		bool					 visible = true;
		bool					 enabled = true;
		bool					 isolation = false;
	};

	// --- Templates ---

	namespace detail {
		Registry* registryOf(const Context& context);
		void	  logGoneParent(const char* type);
	} // namespace detail

	template <typename T>
	T* Ref<T>::get() const {
		return registry != nullptr ? static_cast<T*>(registry->find(id)) : nullptr;
	}

	template <typename T>
	template <typename C>
	Ref<C> Ref<T>::addChild(const typename C::Args& args) const {
		Component* component = get();
		if (component == nullptr) {
			detail::logGoneParent(typeid(C).name());
			return {};
		}
		return component->template addChild<C>(args);
	}

	template <typename T>
	Ref<T> Component::addChild(const typename T::Args& args) {
		static_assert(std::derived_from<T, Component>, "addChild builds components");
		std::unique_ptr<T> child;
		if constexpr (std::constructible_from<T, const typename T::Args&>) {
			child = std::make_unique<T>(args);
		} else {
			child = std::make_unique<T>();
		}
		child->setCommon(commonOf(args));
		const ComponentId childId = adopt(std::move(child));
		return childId.valid() ? Ref<T>(childId, detail::registryOf(*context)) : Ref<T>();
	}

} // namespace ui
