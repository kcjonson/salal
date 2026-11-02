#pragma once

// A menu, after worldsim's Menu (libs/ui/components/menu, a4226e17): a column of items on the
// overlay style, each a row with an optional icon, its label and a trailing hint such as a shortcut;
// a disabled one dims and can't be chosen, a selected one takes the active wash. The rows are one
// focus group, wrapping, so the arrows move along them. Choosing an item closes the menu, then calls
// its onSelect. Menu::open opens one as a menu overlay beside its opener, focus on its selected item
// or its first; Select, DropdownButton and ContextMenu are built on it.

#include "ui/Overlay.h"
#include "ui/components/Control.h"
#include "ui/shapes/Rectangle.h"

#include <functional>
#include <string>
#include <vector>

namespace ui {

	struct MenuItem {
		std::string			  label;
		std::function<void()> onSelect;
		bool				  enabled = true;
		std::string			  icon; // a name in the scene's icon atlases; empty: none
		std::string			  hint; // trailing, such as a shortcut
		bool				  selected = false;
	};

	class Menu : public Rectangle {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::vector<MenuItem> items;
		};

		explicit Menu(const Args& args);

		// Opens one beside `opener` as `player`'s menu, which closes the player's other menus, and puts
		// that player's focus on it.
		static Ref<Menu> open(Component& opener, const Args& args, Anchored placement = {}, int player = 0);

		[[nodiscard]] const std::vector<MenuItem>& getItems() const { return items; }
		void									   setItems(std::vector<MenuItem> value);
		// Item `index`'s row; empty past the end.
		[[nodiscard]] Ref<Control> row(std::size_t index) const;
		// Closes it if it's open as an overlay, then calls the item's onSelect; nothing for a disabled
		// item or past the end.
		void choose(std::size_t index);
		// The player's focus on the selected item, else the first enabled one.
		void focusItem(int player);

		[[nodiscard]] const char* typeName() const override { return "Menu"; }
		[[nodiscard]] StyleId	  themeStyle() const override;

	  protected:
		void onMount() override;
		// Just opened as an overlay: it fades in, and the player's focus goes to it.
		void opened(int player);

	  private:
		void build();

		std::vector<MenuItem>	  items;
		std::vector<Ref<Control>> rows;
		Tween<float>			  appear;
	};

} // namespace ui
