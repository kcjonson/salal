#pragma once

// The framing surface, Panel's structure from worldsim (libs/ui/components/panel, a4226e17) as a node
// (gap 17): a pane with an optional header row, its title on the left and an actions slot on the
// right, over a body slot that grows to fill it. Code fills the slots: body().addChild<...>(...),
// actions().addChild<...>(...). Worldsim's kicker and corner ticks are worldsim's own. Its theme
// style is ui::styles::pane, the title's ui::styles::text::title.

#include "ui/shapes/Rectangle.h"
#include "ui/shapes/Text.h"

#include <string>

namespace ui {

	class Panel : public Rectangle {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string title; // empty, with no actions: no header
			bool		actions = false;
		};

		explicit Panel(const Args& args);

		// Where the content goes: a column with a gap, inside the panel's padding.
		[[nodiscard]] Ref<Component> body() const { return bodySlot; }
		// A row at the header's right; empty unless the panel was made with `actions`.
		[[nodiscard]] Ref<Component> actions() const { return actionsSlot; }
		void						 setTitle(std::string value);

		[[nodiscard]] const char* typeName() const override { return "Panel"; }
		[[nodiscard]] StyleId	  themeStyle() const override;

	  protected:
		void onMount() override;

	  private:
		std::string	   title;
		bool		   withActions;
		Ref<Text>	   titleText;
		Ref<Component> actionsSlot;
		Ref<Component> bodySlot;
	};

} // namespace ui
