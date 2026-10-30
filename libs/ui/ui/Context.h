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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ui {

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

		// Resizes every root.
		void						   setViewport(Foundation::Vec2 size);
		[[nodiscard]] Foundation::Vec2 getViewport() const { return viewport; }

		void update(double dt);
		// Layout, screen transforms and the paint order, where something changed since the last; then
		// onLayout for what moved.
		void layout();
		// Draws every visible component at its key, in its own space.
		void render();

		// Every visible component, bottom to top, as of the last layout.
		[[nodiscard]] const std::vector<ComponentId>& paintOrder() const { return order; }
		[[nodiscard]] Component*					  find(ComponentId id) const { return registry.find(id); }
		[[nodiscard]] Registry&						  getRegistry() { return registry; }
		[[nodiscard]] const Registry&				  getRegistry() const { return registry; }

	  private:
		friend class Component;
		friend Registry* detail::registryOf(const Context& context);

		struct OrderItem {
			Component* component;
			int		   z;
		};

		ComponentId adoptRoot(std::unique_ptr<Component> component, const Component::Args& args);
		void		updateScreen(Component& component, const Affine& parent, const std::optional<Foundation::Rect>& parentClip);
		void		buildOrder();
		void		paintContext(Component& contextRoot);
		void		collectContext(Component& parent, int inheritedZ);
		void		renderComponent(Component& component, float parentOpacity);

		mutable Registry		 registry;
		std::vector<ComponentId> roots;
		ComponentId				 defaultRoot;
		Foundation::Vec2		 viewport;
		std::vector<ComponentId> order;
		std::vector<ComponentId> dirtyBoundaries; // where the next pass lays out from
		std::uint32_t			 layoutPass = 0;
		std::vector<OrderItem>	 orderScratch;
		bool					 geometryDirty = true;
		bool					 orderDirty = true;
		bool					 drawing = false;
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
