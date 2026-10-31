#pragma once

// A set of exclusive options (R12.35): each a ring, filled with a dot when it's the one, and its
// label, in a column or a row. The group is one Tab stop whose arrows move among its options (a
// focus group); a press selects an option and calls onChange with its index.

#include "ui/components/Control.h"

#include <functional>
#include <string>
#include <vector>

namespace ui {

	class RadioGroup : public Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::vector<std::string> options;
			int						 selected = 0; // -1: none
			FlexDirection			 direction = FlexDirection::Column;
			std::function<void(int)> onChange;
		};

		explicit RadioGroup(const Args& args);

		[[nodiscard]] int getSelected() const { return selected; }
		// Selects without calling onChange.
		void setSelected(int index);
		// Shows the selection again, so a theme change reaches it.
		void setStyle(const Style& declared) override;

		[[nodiscard]] const char* typeName() const override { return "RadioGroup"; }

		// An option, by the group.
		void choose(int index);

	  protected:
		void onMount() override;

	  private:
		void show();

		std::vector<std::string>  labels;
		int						  selected;
		FlexDirection			  direction;
		std::function<void(int)>  onChange;
		std::vector<Ref<Control>> options;
	};

} // namespace ui
