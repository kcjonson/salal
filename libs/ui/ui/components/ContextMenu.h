#pragma once

// A context menu, after worldsim's ContextMenu (libs/ui/components/contextmenu, a4226e17): a Menu
// opened at a point in its opener, usually where a secondary press landed, below and right of it,
// flipped above it and kept inside the viewport near an edge. Like any menu, a press outside it,
// cancel, or focus leaving it closes it; a secondary press elsewhere closes it and still reaches
// what's under it, so a right-click there can open the next one.

#include "ui/components/Menu.h"

namespace ui {

	class ContextMenu : public Menu {
	  public:
		using Menu::Menu;

		// Opens one at `at` in `opener`'s space (a PointerEvent's local, delivered to the opener).
		static Ref<ContextMenu> open(Component& opener, Foundation::Vec2 at, const Args& args, int player = 0);

		[[nodiscard]] const char* typeName() const override { return "ContextMenu"; }
	};

} // namespace ui
