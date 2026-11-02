#pragma once

// A button that opens a menu of actions, after worldsim's DropdownButton (libs/ui/components/dropdown,
// a4226e17): a Button with a caret after its label. Activating it opens a Menu of its items on
// `side` of it, below by default, and again closes it; choosing an item closes the menu and runs
// the item. For a value the player picks, use Select.

#include "ui/Overlay.h"
#include "ui/components/Button.h"
#include "ui/components/Menu.h"
#include "ui/shapes/Chevron.h"

#include <vector>

namespace ui {

	class DropdownButton : public Button {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string			  label;
			std::string			  icon;
			ButtonVariant		  variant = ButtonVariant::Secondary;
			ControlSize			  controlSize = ControlSize::Md;
			std::vector<MenuItem> items;
			Side				  side = Side::Below;
		};

		explicit DropdownButton(const Args& args);

		[[nodiscard]] bool						   isOpen() const { return menu.get() != nullptr; }
		void									   openMenu(int player = 0);
		void									   closeMenu();
		[[nodiscard]] Ref<Menu>					   getMenu() const { return menu; }
		[[nodiscard]] const std::vector<MenuItem>& getItems() const { return items; }
		// Closes the menu if it's open.
		void setItems(std::vector<MenuItem> value);
		void setStyle(const Style& declared) override;

		[[nodiscard]] const char* typeName() const override { return "DropdownButton"; }

	  protected:
		void onMount() override;
		void activate() override;

	  private:
		std::vector<MenuItem> items;
		Side				  side;
		Ref<Chevron>		  caret;
		Ref<Menu>			  menu;
	};

} // namespace ui
