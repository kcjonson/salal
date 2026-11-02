#include "ui/components/ContextMenu.h"

namespace ui {

	Ref<ContextMenu> ContextMenu::open(Component& opener, Foundation::Vec2 at, const Args& args, int player) {
		Ref<ContextMenu> menu = opener.openOverlay<ContextMenu>(
			args,
			{.kind = OverlayKind::Menu,
			 .player = player,
			 .placement = Anchored{.side = Side::Below, .align = Align::Start, .gap = 0.0F, .point = at}}
		);
		if (ContextMenu* shown = menu.get()) {
			shown->opened(player);
		}
		return menu;
	}

} // namespace ui
