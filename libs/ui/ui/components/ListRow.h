#pragma once

// A selectable row for lists, after worldsim's ListRow (libs/ui/components/list, a4226e17): an
// optional leading icon (a child, gap 30), a left-aligned label that takes the room, and optional
// trailing text, over a hairline at its foot. Selected, it takes the active wash and an accent bar
// at its left edge; dim, its label is the secondary colour. A press calls onPress. Its theme style
// is ui::styles::listRow.

#include "ui/components/Control.h"
#include "ui/shapes/Icon.h"
#include "ui/shapes/Text.h"

#include <functional>
#include <string>

namespace ui {

	class ListRow : public Control {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string			  label;
			std::string			  trailing;
			std::string			  icon;
			bool				  selected = false;
			bool				  dim = false;	 // unavailable to look at, still pressable
			float				  indent = 0.0F; // px before the label, for nesting
			std::function<void()> onPress;
		};

		explicit ListRow(const Args& args);

		[[nodiscard]] bool isSelected() const { return isActive(); }
		void			   setSelected(bool value);
		void			   setLabel(std::string value);
		void			   setTrailing(std::string value);
		[[nodiscard]] bool isDim() const { return dim; }
		void			   setDim(bool value);
		void			   setOnPress(std::function<void()> callback) { onPress = std::move(callback); }

		[[nodiscard]] const char* typeName() const override { return "ListRow"; }
		[[nodiscard]] StyleId	  themeStyle() const override;

	  protected:
		void onMount() override;
		void activate() override;

	  private:
		std::string			  label;
		std::string			  trailing;
		std::string			  iconName;
		bool				  dim;
		float				  indent;
		bool				  selectedAtStart;
		std::function<void()> onPress;
		Ref<Icon>			  shownIcon;
		Ref<Text>			  text;
		Ref<Text>			  trailingText;
		Ref<Rectangle>		  bar;
	};

} // namespace ui
