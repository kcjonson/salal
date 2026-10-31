#pragma once

// A scene's ui: its registry, its roots and the viewport they are sized from, and from later phases
// its services (docs/ui-design.md, "Services and input routing"). One per scene, shared by all of its
// roots; there are no singletons. app makes one the first time a scene asks for it, and destroys it
// once the scene's onExit has run.
//
// Each frame: update(), layout() (the layout pass from each relayout boundary that changed, then
// screen transforms, clips and the paint order), and render(), which draws after the scene's own
// draws behind a sort barrier, so the scene's keys and ui's never interleave.
//
// Paint order (docs/ui-design.md, "Ordering"): every root is a stacking context, and so is a component
// with `isolation`; nothing else makes one. Inside a context its root paints first, then everything
// under it sorted by zIndex, then tree order, where a component with no zIndex takes its parent's and
// a nested context paints as one, its whole subtree at its zIndex. The roots paint by zIndex, then
// the order they were added. Each component's rank in that one list is its draw key; hit testing and
// focus read the same list.

#include "ui/Component.h"

#include <math/Types.h>

#include <input/ActionMap.h>
#include <input/InputTypes.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace ui {

	namespace detail {
		class Dispatcher;
		class FocusManager;
	} // namespace detail

	// A draw key is (slot << kRankBits) | rank. The scene is slot 0; popup stack entries and the tiers
	// above it take the slots after.
	inline constexpr int		  kRankBits = 20;
	inline constexpr std::int32_t kMaxRank = (1 << kRankBits) - 1;

	class Context {
	  public:
		explicit Context(Foundation::Vec2 viewportSize);
		~Context();

		Context(const Context&) = delete;
		Context& operator=(const Context&) = delete;
		Context(Context&&) = delete;
		Context& operator=(Context&&) = delete;

		// The first root, made on first use with the id "root".
		Ref<Component> root();
		// Another root: a stacking context of its own at (0, 0), the viewport's size, whatever `args` say.
		template <typename T>
		Ref<T>										  addRoot(const typename T::Args& args = {});
		void										  removeRoot(Ref<Component> rootRef);
		[[nodiscard]] const std::vector<ComponentId>& getRoots() const { return roots; }

		// The window's logical size, which the ui's viewport is at its scale; resizes every root.
		void setViewport(Foundation::Vec2 size);
		// The ui's one scale (R7.5): its viewport is the window's logical size over it, so layout and
		// input work in the scaled space and nothing is zoomed as a bitmap. 0.25 to 4; 1 by default.
		void						   setScale(float value);
		[[nodiscard]] float			   getScale() const { return scale; }
		[[nodiscard]] Foundation::Vec2 getViewport() const { return viewport; }

		void update(double dt);
		// Layout, screen transforms and the paint order, where something changed since the last; then
		// onLayout for what moved.
		void layout();
		// Draws every visible component at its key, in its own space.
		void render();

		// One platform event; true when the ui took it, so the scene shouldn't. Pointer events go to
		// the topmost hit, a press's gesture stays with whoever had the press, and the wheel scrolls;
		// keys, characters, pad buttons and axes go to the focus of the player holding the device.
		bool dispatch(const foundation::InputEvent& event);
		// The topmost component the pointer hits at a point, in the ui's px; none for empty space. A
		// scroller is the hit over its scrollbar, which sets `onScrollbar`.
		[[nodiscard]] ComponentId hitTest(Foundation::Vec2 point, bool* onScrollbar = nullptr) const;
		// The cursor the hovered component, or the pointer's captor, asks for.
		[[nodiscard]] Cursor cursor() const;
		// Ends anything mid-interaction: a held press, a capture, a drag, the hover. The scene was
		// covered, or the window lost focus.
		void cancelInteraction();

		// --- Focus (ui/Focus.h) ---

		// One focus per player slot, 0 to kMaxPlayers - 1. focus is refused, false, for a component
		// that can't take it (Component::canReceiveFocus) or is outside the player's scope.
		bool						 focus(Ref<Component> target, int player = 0);
		void						 clearFocus(int player = 0);
		[[nodiscard]] Ref<Component> focused(int player = 0);
		// Tab order, wrapping (R9.18, R9.19); false when there's nothing to focus.
		bool focusNext(int player = 0);
		bool focusPrevious(int player = 0);
		// What a direction does when nothing consumes it: a neighbour, the focus group, the nearest.
		bool focusDirection(Direction direction, int player = 0);
		// Navigation stays inside `scopeRoot` until popScope, or until it goes; focus moves in, and
		// comes back to where it was when the scope closes (gap 11).
		void pushScope(Ref<Component> scopeRoot, int player = 0);
		void popScope(int player = 0);
		// The player's navigation actions (Focus.h's actions); defaultNavigation() until set.
		void setNavigation(int player, foundation::ActionMap map);
		// The devices whose keys, buttons and axes drive the player's focus. While no player holds any,
		// player 0 holds them all; once one does, a device no player holds drives nothing. The pointer
		// isn't routed: a press focuses for the player holding the mouse, else player 0.
		void					setPlayerDevices(int player, std::span<const foundation::DeviceId> devices);
		[[nodiscard]] InputMode inputMode() const;

		// Every visible component, bottom to top, as of the last layout.
		[[nodiscard]] const std::vector<ComponentId>& paintOrder() const { return order; }
		[[nodiscard]] Component*					  find(ComponentId id) const { return registry.find(id); }
		[[nodiscard]] Registry&						  getRegistry() { return registry; }
		[[nodiscard]] const Registry&				  getRegistry() const { return registry; }

	  private:
		friend class Component;
		friend class detail::Dispatcher;
		friend class detail::FocusManager;
		friend Registry* detail::registryOf(const Context& context);

		struct OrderItem {
			Component* component;
			int		   z;
		};

		ComponentId adoptRoot(std::unique_ptr<Component> component, const Component::Args& args);
		void		updateScreen(
				   Component&							  component,
				   const Affine&						  parent,
				   const std::optional<Foundation::Rect>& parentClip,
				   bool									  parentEnabled,
				   bool									  blocked
			   );
		void resizeRoots();
		// Screen maps, clips and enabled flags again if anything moved; true when it ran.
		bool		refreshGeometry();
		void		pointerPressed(ComponentId hit);
		void		removing(ComponentId root);
		void		capture(ComponentId captor);
		ComponentId captor() const;
		void		drawScrollbar(const Component& scroller) const;
		void		buildOrder();
		void		paintContext(Component& contextRoot);
		void		collectContext(Component& parent, int inheritedZ);
		void		renderComponent(Component& component, float parentOpacity);

		mutable Registry		 registry;
		std::vector<ComponentId> roots;
		ComponentId				 defaultRoot;
		Foundation::Vec2		 windowSize;
		Foundation::Vec2		 viewport;
		float					 scale = 1.0F;
		std::vector<ComponentId> order;
		// The paint order as the pointer meets it: each component, and each scroller's bar after the
		// last of its subtree, where it paints.
		struct HitEntry {
			ComponentId id;
			bool		scrollbar;
		};
		std::vector<HitEntry>				  hitList;
		std::unique_ptr<detail::Dispatcher>	  dispatcher;
		std::unique_ptr<detail::FocusManager> focusManager;
		std::vector<ComponentId>			  dirtyBoundaries; // where the next pass lays out from
		std::uint32_t						  layoutPass = 0;
		std::vector<OrderItem>				  orderScratch;
		bool								  geometryDirty = true;
		bool								  orderDirty = true;
		bool								  drawing = false;
		bool								  hoverStale = false; // something moved under a still pointer
		bool								  rankOverflowLogged = false;
	};

	template <typename T>
	Ref<T> Context::addRoot(const typename T::Args& args) {
		static_assert(std::derived_from<T, Component>, "a root is a component");
		std::unique_ptr<T> component;
		if constexpr (std::constructible_from<T, const typename T::Args&>) {
			component = std::make_unique<T>(args);
		} else {
			component = std::make_unique<T>();
		}
		const ComponentId rootId = adoptRoot(std::move(component), Component::commonOf(args));
		return rootId.valid() ? Ref<T>(rootId, &registry) : Ref<T>();
	}

} // namespace ui
