#pragma once

// The one node every ui element is, leaf or widget (docs/ui-design.md, "Node model" and "Component
// API"). A parent's addChild builds a component from its Args and mounts it; the component adds its own
// children in onMount, never in its constructor, which touches no service. render() draws the
// component's own content only, and the context walks the children.
//
// Coordinates are logical pixels, y down, local to the parent's box. Layout (ui/Layout.h) places each
// component: its margin box at `position` under a Free parent, in the flow under a Flex one. render()
// draws with (0, 0) at the component's own box, inside its margin. screenBounds() and localToScreen()
// reach the viewport; a component never adds up its ancestors' positions itself.
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
//       void onMount() override {
//           fill = addChild<ui::Rectangle>({.size = {ui::percent(value * 100.0F), ui::percent(100.0F)}});
//       }
//     private:
//       float value;
//       ui::Ref<ui::Rectangle> fill;
//   };

#include "ui/Focus.h"
#include "ui/Input.h"
#include "ui/Layout.h"
#include "ui/Registry.h"
#include "ui/Style.h"
#include "ui/Transform.h"

#include <graphics/Rect.h>
#include <math/Types.h>

#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <typeinfo>
#include <vector>

// The attributes every component's Args starts with, in this order, since designated initializers
// follow declaration order: identity, geometry, layout, look, ordering, then the callbacks. A macro
// because a designated initializer can't name a base struct's fields (P2287 is aimed at C++29). A
// component's own fields come after it.
//
//   id         for the tree snapshot, the lint's paths and tests; empty for none
//   visible    false takes the subtree out of layout, drawing, the lint and the paint order (state, not
//              style)
//   enabled    reported by the snapshot; input reads it
//   position   the margin box's top-left in the parent's box, under a Free parent or Absolute
//   size       px, ui::percent() of the parent's box, or ui::kAuto (the default): its content
//   minSize    clamps a resolved size; auto is 0
//   maxSize    clamps a resolved size; auto is none
//   margin     space around the box, outside its size
//   transform  render and hit testing only, never layout or the lint
//   layout     display, flex and positioning (ui/Layout.h)
//   style      how it looks
//   overflow   Clip cuts its children at its box; Scroll cuts and scrolls them
//   zIndex     its place among everything in its nearest stacking context; unset takes its parent's
//   isolation  true makes it a stacking context, whose subtree paints as one at its zIndex
//   pointerEvents  what the pointer can hit (ui/Input.h); unset, the type's default
//   focusable  it can take a player's focus (ui/Focus.h)
//   tabIndex   above 0 comes first in Tab order, ascending; below 0 is focusable by code and the
//              pointer only; unset is 0, tree order
//   focusGroup makes it one Tab stop whose arrows move among its focusable members
//   cursor     the window's cursor over it; unset, its parent's
//   onClick, onPointerDown/Up/Move/Enter/Leave, onWheel  pointer callbacks, before the type's own
//              handling; a click lands on the nearest common ancestor of the press and the release
//   onKeyDown, onKeyUp  keys, characters and the navigation actions while it or a descendant has a
//              player's focus; onKeyDown hears Down, Char, Activate, Cancel and Navigate
//   onFocus, onBlur  a player's focus arriving and leaving; neither bubbles
//   onLayout   called after a layout pass that moved or resized it
#define UI_COMMON_ARGS                                                                                                                     \
	::std::string							   id;                                                                                         \
	bool									   visible = true;                                                                             \
	bool									   enabled = true;                                                                             \
	::Foundation::Vec2						   position{0.0F, 0.0F};                                                                       \
	::ui::Size								   size{};                                                                                     \
	::ui::Size								   minSize{};                                                                                  \
	::ui::Size								   maxSize{};                                                                                  \
	::ui::Edges								   margin{};                                                                                   \
	::ui::Transform							   transform{};                                                                                \
	::ui::Layout							   layout{};                                                                                   \
	::ui::Style								   style{};                                                                                    \
	::ui::Overflow							   overflow = ::ui::Overflow::Visible;                                                         \
	::std::optional<int>					   zIndex;                                                                                     \
	bool									   isolation = false;                                                                          \
	::std::optional<::ui::PointerEvents>	   pointerEvents;                                                                              \
	bool									   focusable = false;                                                                          \
	::std::optional<int>					   tabIndex;                                                                                   \
	::std::optional<::ui::FocusGroup>		   focusGroup;                                                                                 \
	::std::optional<::ui::Cursor>			   cursor;                                                                                     \
	::std::function<void(::ui::PointerEvent&)> onClick;                                                                                    \
	::std::function<void(::ui::PointerEvent&)> onPointerDown;                                                                              \
	::std::function<void(::ui::PointerEvent&)> onPointerUp;                                                                                \
	::std::function<void(::ui::PointerEvent&)> onPointerMove;                                                                              \
	::std::function<void(::ui::PointerEvent&)> onPointerEnter;                                                                             \
	::std::function<void(::ui::PointerEvent&)> onPointerLeave;                                                                             \
	::std::function<void(::ui::PointerEvent&)> onWheel;                                                                                    \
	::std::function<void(::ui::KeyEvent&)>	   onKeyDown;                                                                                  \
	::std::function<void(::ui::KeyEvent&)>	   onKeyUp;                                                                                    \
	::std::function<void(::ui::Component&)>	   onFocus;                                                                                    \
	::std::function<void(::ui::Component&)>	   onBlur;                                                                                     \
	::std::function<void(::ui::Component&)>	   onLayout;

namespace ui {

	class Component;
	class Context;

	namespace detail {
		class Dispatcher;
		class FocusManager;
		class LayoutEngine;
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
		// Visible and not display: None, so laid out, drawn and linted.
		[[nodiscard]] bool isShown() const { return visible && layout.display != Display::None; }
		[[nodiscard]] bool isEnabled() const { return enabled; }
		void			   setEnabled(bool value);
		// Enabled itself and under no disabled ancestor (R8.3), as of the last layout.
		[[nodiscard]] bool isEnabledInTree() const { return enabledInTree; }

		// The declared offset, which places it under a Free parent or Absolute.
		[[nodiscard]] Foundation::Vec2 getPosition() const { return position; }
		void						   setPosition(Foundation::Vec2 value);
		// The box's size as the last layout resolved it.
		[[nodiscard]] Foundation::Vec2 getSize() const { return size; }
		// The declared size; a Vec2 is px.
		[[nodiscard]] const Size&	   getSizeSpec() const { return sizeSpec; }
		virtual void				   setSize(const Size& value);
		void						   setMinSize(const Size& value);
		void						   setMaxSize(const Size& value);
		[[nodiscard]] const Edges&	   getMargin() const { return margin; }
		void						   setMargin(const Edges& value);
		[[nodiscard]] const Layout&	   getLayout() const { return layout; }
		void						   setLayout(const Layout& value);
		[[nodiscard]] Overflow		   getOverflow() const { return overflow; }
		void						   setOverflow(Overflow value);
		[[nodiscard]] const Transform& getTransform() const { return transform; }
		void						   setTransform(const Transform& value);
		// The margin box in the parent's box, where the last layout put it.
		[[nodiscard]] Foundation::Rect getBounds() const;
		// The box in viewport pixels; under a rotation, the axis-aligned bounds of the turned box. Valid
		// after the frame's layout, as are the two maps.
		[[nodiscard]] Foundation::Rect screenBounds() const;
		[[nodiscard]] Foundation::Vec2 localToScreen(Foundation::Vec2 point) const;
		[[nodiscard]] Foundation::Vec2 screenToLocal(Foundation::Vec2 point) const;
		// The clip its ancestors' Overflow::Clip put on it, in the viewport; none when nothing clips it.
		[[nodiscard]] const std::optional<Foundation::Rect>& getClip() const { return clip; }

		// The style it draws with: the one it was given, resolved against its scene's theme once mounted.
		[[nodiscard]] const Style& getStyle() const { return style; }
		virtual void			   setStyle(const Style& value);
		// The style as given, before the theme: what a component that tints itself leaves alone.
		[[nodiscard]] const Style& getDeclaredStyle() const { return declaredStyle; }

		[[nodiscard]] std::optional<int> getZIndex() const { return zIndex; }
		void							 setZIndex(std::optional<int> value);
		[[nodiscard]] bool				 getIsolation() const { return isolation; }
		void							 setIsolation(bool value);
		// This component's place in its scene's paint order, which render() draws at. Valid after the
		// frame's layout.
		[[nodiscard]] std::int32_t drawKey() const { return key; }

		void setOnLayout(std::function<void(Component&)> callback);
		void setOnClick(std::function<void(PointerEvent&)> callback);
		void setOnPointer(PointerEvent::Type type, std::function<void(PointerEvent&)> callback);

		// --- Pointer ---

		[[nodiscard]] PointerEvents			getPointerEvents() const { return pointerEvents.value_or(defaultPointerEvents()); }
		void								setPointerEvents(std::optional<PointerEvents> value);
		[[nodiscard]] std::optional<Cursor> getCursor() const { return cursor; }
		void								setCursor(std::optional<Cursor> value) { cursor = value; }
		// Under the pointer: it or a descendant is the hit (CSS :hover).
		[[nodiscard]] bool isHovered() const { return hovered; }
		// Pressed: a press landed on it or a descendant and hasn't been released or cancelled.
		[[nodiscard]] bool isPressed() const { return pressed; }
		// Every event of the pointer goes to this component until the release, a cancel or
		// releasePointer(); from a press's handler. Its removal or hiding cancels it.
		void			   capturePointer();
		void			   releasePointer();
		[[nodiscard]] bool hasPointerCapture() const;
		// It answers the pointer itself: a pointer callback, or a type that handles it (the snapshot's
		// handlesPointer, which the lint's unreachable-interactive rule reads).
		[[nodiscard]] virtual bool handlesPointer() const;

		// --- Focus ---

		[[nodiscard]] bool							   isFocusable() const { return focusable; }
		void										   setFocusable(bool value) { focusable = value; }
		[[nodiscard]] std::optional<int>			   getTabIndex() const { return tabIndex; }
		void										   setTabIndex(std::optional<int> value) { tabIndex = value; }
		[[nodiscard]] const std::optional<FocusGroup>& getFocusGroup() const { return focusGroup; }
		void										   setFocusGroup(std::optional<FocusGroup> value) { focusGroup = value; }
		// Where a direction goes from here, ahead of the nearest-on-screen search (R9.26).
		void setNeighbour(Direction direction, Ref<Component> neighbour);
		// Takes the player's focus as code does, a ring shown unless the pointer drove input last; false
		// when it can't take focus (canReceiveFocus, or outside the player's scope).
		bool focus(int player = 0);
		// Focusable, shown and enabled, under nothing hidden or disabled, and still in the tree.
		[[nodiscard]] bool canReceiveFocus() const;
		// Some player's focus is on it.
		[[nodiscard]] bool isFocused() const { return focusedBy != 0; }
		[[nodiscard]] bool isFocusedBy(int player) const { return ((focusedBy >> player) & 1U) != 0; }
		// A player's focus is on it with a ring: that player got there by keys, a pad or code since
		// they last used the pointer (CSS :focus-visible).
		[[nodiscard]] bool isFocusVisible() const { return focusVisibleBy != 0; }
		void			   setOnKeyDown(std::function<void(KeyEvent&)> callback);
		void			   setOnKeyUp(std::function<void(KeyEvent&)> callback);
		void			   setOnFocus(std::function<void(Component&)> callback);
		void			   setOnBlur(std::function<void(Component&)> callback);

		// Children kept in step with `items` by key (R8.27): a key still there keeps its component, so
		// its focus, hover and state, and is passed to `update`; a new key is built by `create`, which
		// adds it with addChild and returns its Ref; a key gone is removed; the children end in the
		// items' order. It owns the whole child list, so children it didn't make go too.
		template <typename Item, typename KeyOf, typename Create, typename Update>
		void reconcileChildren(const std::vector<Item>& items, KeyOf keyOf, Create create, Update update);

		// --- Scrolling, for Overflow::Scroll ---

		[[nodiscard]] Foundation::Vec2 getScroll() const { return scroll; }
		// How far it scrolls on each axis, from the last layout.
		[[nodiscard]] Foundation::Vec2 getScrollMax() const { return scrollMax; }
		void						   scrollTo(Foundation::Vec2 value);
		void						   scrollBy(Foundation::Vec2 delta) { scrollTo(scroll + delta); }

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
		// The size a leaf's own content needs, for an axis its size leaves auto: none for a component
		// its children size. `width` is set once the width is decided (a text wraps to it); otherwise
		// `availableWidth` is the room there is, infinite when nothing bounds it.
		[[nodiscard]] virtual std::optional<Foundation::Vec2>
		intrinsicSize(std::optional<float> /*width*/, float /*availableWidth*/) const {
			return std::nullopt;
		}
		// The narrowest a flex row may shrink it when its minSize is auto (CSS min-width: auto).
		[[nodiscard]] virtual float minContentWidth() const { return 0.0F; }
		// Its content changed in a way that changes what it measures (a text's string, its font).
		void contentChanged();
		// A pointer event the dispatcher delivers to it, after its callback; consume() stops the bubble.
		virtual void handlePointer(PointerEvent& /*event*/) {}
		// A key, character or action while it or a descendant has focus, after its callback.
		virtual void handleKey(KeyEvent& /*event*/) {}
		// Whether a point in its box hits it: the box, edges half-open (R8.12). A non-rectangular
		// shape narrows it.
		[[nodiscard]] virtual bool containsPoint(Foundation::Vec2 local) const;
		// Its pointerEvents when unset: Passthrough, a container's; a leaf or a widget says Auto.
		[[nodiscard]] virtual PointerEvents defaultPointerEvents() const { return PointerEvents::Passthrough; }

		// Its effective opacity, its own style's times its ancestors'. Valid in render().
		[[nodiscard]] float opacity() const { return effectiveOpacity; }

		Foundation::Vec2 position{0.0F, 0.0F};
		Foundation::Vec2 size{0.0F, 0.0F};
		Style			 style;

	  private:
		friend class Context;
		friend class detail::Dispatcher;
		friend class detail::FocusManager;
		friend class detail::LayoutEngine;

		// A measure the layout pass took, kept while nothing under the component changes.
		struct Measured {
			std::optional<float> width;
			std::optional<float> height;
			Foundation::Vec2	 parentBox{0.0F, 0.0F};
			float				 availableWidth = 0.0F;
			Foundation::Vec2	 result{0.0F, 0.0F};
			std::uint32_t		 pass = 0;
			bool				 valid = false;
		};

		struct Handlers {
			std::function<void(PointerEvent&)> onClick;
			std::function<void(PointerEvent&)> onPointerDown;
			std::function<void(PointerEvent&)> onPointerUp;
			std::function<void(PointerEvent&)> onPointerMove;
			std::function<void(PointerEvent&)> onPointerEnter;
			std::function<void(PointerEvent&)> onPointerLeave;
			std::function<void(PointerEvent&)> onWheel;
			std::function<void(KeyEvent&)>	   onKeyDown;
			std::function<void(KeyEvent&)>	   onKeyUp;
			std::function<void(Component&)>	   onFocus;
			std::function<void(Component&)>	   onBlur;
			std::function<void(Component&)>	   onLayout;
		};
		Handlers&				 handlersMade();
		[[nodiscard]] Component* keyedChild(const std::string& childKey) const;
		void					 finishReconcile(const std::vector<ComponentId>& kept);
		// Its callback for an event type, if it has one.
		[[nodiscard]] const std::function<void(PointerEvent&)>* callbackFor(PointerEvent::Type type) const;

		void		setCommon(const Args& args);
		ComponentId adopt(std::unique_ptr<Component> child);
		Component*	parentComponent() const;
		// Layout must run again from here: its own box changed (`ownBox`, which its parent places) or
		// what it holds did. Marks it and its ancestors up to the relayout boundary.
		void layoutChanged(bool ownBox);
		void orderChanged() const;
		// Its size can't change with what's under it: a root, or both axes px or a percentage.
		[[nodiscard]] bool isLayoutBoundary() const;

		template <typename A>
		static Args commonOf(const A& args) {
			return {
				.id = args.id,
				.visible = args.visible,
				.enabled = args.enabled,
				.position = args.position,
				.size = args.size,
				.minSize = args.minSize,
				.maxSize = args.maxSize,
				.margin = args.margin,
				.transform = args.transform,
				.layout = args.layout,
				.style = args.style,
				.overflow = args.overflow,
				.zIndex = args.zIndex,
				.isolation = args.isolation,
				.pointerEvents = args.pointerEvents,
				.focusable = args.focusable,
				.tabIndex = args.tabIndex,
				.focusGroup = args.focusGroup,
				.cursor = args.cursor,
				.onClick = args.onClick,
				.onPointerDown = args.onPointerDown,
				.onPointerUp = args.onPointerUp,
				.onPointerMove = args.onPointerMove,
				.onPointerEnter = args.onPointerEnter,
				.onPointerLeave = args.onPointerLeave,
				.onWheel = args.onWheel,
				.onKeyDown = args.onKeyDown,
				.onKeyUp = args.onKeyUp,
				.onFocus = args.onFocus,
				.onBlur = args.onBlur,
				.onLayout = args.onLayout,
			};
		}

		std::string					 id;
		Style						 declaredStyle; // as given; `style` is it resolved
		Size						 sizeSpec;
		Size						 minSpec;
		Size						 maxSpec;
		Edges						 margin;
		Transform					 transform;
		Layout						 layout;
		Overflow					 overflow = Overflow::Visible;
		std::optional<int>			 zIndex;
		std::optional<PointerEvents> pointerEvents;
		std::optional<int>			 tabIndex;
		std::optional<FocusGroup>	 focusGroup;
		std::optional<Cursor>		 cursor;
		std::unique_ptr<Handlers>	 handlers;		// made when the first callback is set
		ComponentId					 neighbours[4]; // by Direction
		ComponentId					 activeChild;	// a focus group's member focused last
		std::string					 reconcileKey;	// its key among its parent's reconciled children
		ComponentId					 selfId;
		ComponentId					 parentId;
		std::vector<ComponentId>	 children;
		Context*					 context = nullptr;
		// From the layout pass: the margin box's top-left in the parent's box, the map to the
		// viewport, the clip, and the last two measures.
		Foundation::Vec2				offset{0.0F, 0.0F};
		Affine							screen;
		std::optional<Foundation::Rect> clip;
		Measured						measured[2];
		Foundation::Vec2				scroll{0.0F, 0.0F};
		Foundation::Vec2				scrollMax{0.0F, 0.0F};
		std::int32_t					key = 0;
		std::int32_t					lastKey = 0; // the highest key in its subtree, which a scrollbar draws at
		float							effectiveOpacity = 1.0F;
		bool							visible = true;
		bool							enabled = true;
		bool							isolation = false;
		bool							layoutDirty = true;
		bool							laidOut = false; // placed by a layout pass yet
		bool							enabledInTree = true;
		bool							hitBlocked = false; // an ancestor's pointerEvents keeps the pointer off it
		bool							hovered = false;
		bool							pressed = false;
		bool							focusable = false;
		std::uint8_t					focusedBy = 0; // a bit for each player whose focus is on it
		std::uint8_t					focusVisibleBy = 0;
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

	template <typename Item, typename KeyOf, typename Create, typename Update>
	void Component::reconcileChildren(const std::vector<Item>& items, KeyOf keyOf, Create create, Update update) {
		std::vector<ComponentId> kept;
		kept.reserve(items.size());
		for (const Item& item : items) {
			std::string itemKey = keyOf(item);
			Component*	existing = keyedChild(itemKey);
			if (existing != nullptr && std::ranges::find(kept, existing->selfId) == kept.end()) {
				update(*existing, item);
				kept.push_back(existing->selfId);
				continue;
			}
			const Ref<Component> made = create(item);
			if (Component* child = made.get(); child != nullptr && child->parentId == selfId) {
				child->reconcileKey = std::move(itemKey);
				kept.push_back(child->selfId);
			}
		}
		finishReconcile(kept);
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
