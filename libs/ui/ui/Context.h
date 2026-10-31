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
#include "ui/icons/IconService.h"
#include "ui/theme/Theme.h"

#include <math/Types.h>

#include <cstddef>
#include <cstdint>
#include <input/InputTypes.h>

#include <memory>
#include <optional>
#include <vector>

namespace ui {

	namespace detail {
		class Dispatcher;
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

		// The theme every component's style resolves against (docs/ui-design.md, "Theme"): ui's tokens
		// until a game sets its own. Setting one restyles every component, as a mode switch does.
		[[nodiscard]] const Theme& getTheme() const { return theme; }
		void					   setTheme(Theme value);

		// The scene's icon atlases; a game adds its own (my_game::iconAtlas()) before its icons draw.
		[[nodiscard]] IconService& icons() { return iconService; }

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
		// the topmost hit, a press's gesture stays with whoever had the press, and the wheel scrolls.
		bool dispatch(const foundation::InputEvent& event);
		// The topmost component the pointer hits at a point, in the ui's px; none for empty space. A
		// scroller is the hit over its scrollbar, which sets `onScrollbar`.
		[[nodiscard]] ComponentId hitTest(Foundation::Vec2 point, bool* onScrollbar = nullptr) const;
		// The cursor the hovered component, or the pointer's captor, asks for.
		[[nodiscard]] Cursor cursor() const;
		// Ends anything mid-interaction: a held press, a capture, a drag, the hover. The scene was
		// covered, or the window lost focus.
		void cancelInteraction();

		// --- The popup stack (ui/Popup.h) ---

		// Closes an entry as code does, with no onDismiss: the same as removing it.
		void closePopup(Ref<Component> entry);
		// Closes an entry as the stack does: onDismiss, then closed.
		void dismissPopup(Ref<Component> entry);
		// The entry, and every entry opened from inside it, above the others, keeping their order.
		void bringToFront(Ref<Component> entry);
		// The open entries in stack order, bottom to top.
		[[nodiscard]] std::vector<ComponentId> popups() const;

		// Every visible component, bottom to top, as of the last layout.
		[[nodiscard]] const std::vector<ComponentId>& paintOrder() const { return order; }
		[[nodiscard]] Component*					  find(ComponentId id) const { return registry.find(id); }
		[[nodiscard]] Registry&						  getRegistry() { return registry; }
		[[nodiscard]] const Registry&				  getRegistry() const { return registry; }

	  private:
		friend class Component;
		friend class detail::Dispatcher;
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
		void refreshGeometry(bool& changed);
		void openPopup(ComponentId entry, ComponentId opener, PopupOptions options);
		void dismiss(ComponentId entry);
		void placePopups();
		// A press at `hit` closes the pointer player's menus it is outside; true when that consumes it.
		bool pressOutside(ComponentId hit, foundation::MouseButton button);
		// The slot of the pointer player's topmost dialog: what's in a lower slot is inert to the pointer.
		[[nodiscard]] int  modalFloor() const;
		[[nodiscard]] bool pointerInert() const { return modalFloor() > 0; }
		[[nodiscard]] bool within(ComponentId id, ComponentId root) const;
		void			   removing(ComponentId root);
		void			   capture(ComponentId captor);
		ComponentId		   captor() const;
		void			   drawScrollbar(const Component& scroller) const;
		void			   buildOrder();
		void			   paintContext(Component& contextRoot);
		void			   collectContext(Component& parent, int inheritedZ);
		void			   renderComponent(Component& component, float parentOpacity);
		void			   restyle(Component& component);

		mutable Registry		 registry;
		Theme					 theme;
		IconService				 iconService;
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
		std::vector<HitEntry>				hitList;
		std::unique_ptr<detail::Dispatcher> dispatcher;
		struct PopupEntry {
			ComponentId	 id;
			ComponentId	 opener;
			PopupOptions options;
		};
		std::vector<PopupEntry>	 popupStack;	   // in stack order
		int						 paintingSlot = 0; // the slot buildOrder fills
		std::vector<ComponentId> dirtyBoundaries;  // where the next pass lays out from
		std::uint32_t			 layoutPass = 0;
		std::vector<OrderItem>	 orderScratch;
		bool					 geometryDirty = true;
		bool					 orderDirty = true;
		bool					 drawing = false;
		bool					 hoverStale = false; // something moved under a still pointer
		bool					 rankOverflowLogged = false;
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
