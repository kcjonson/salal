#pragma once

// A select, after worldsim's Select (libs/ui/components/select, a4226e17): a field showing the chosen
// option's label, or the placeholder, and a caret (a Chevron, gap 19). Activating it opens a Menu of
// the options below it, the chosen one selected and focused; choosing one closes the menu and calls
// onChange when the value changed. Controlled: setValue sets it without onChange. Its theme style is
// ui::styles::select.

#include "ui/components/Control.h"
#include "ui/components/Menu.h"
#include "ui/shapes/Chevron.h"
#include "ui/shapes/Text.h"
#include "ui/theme/Variants.h"

#include <functional>
#include <string>
#include <vector>

namespace ui {

	struct SelectOption {
		std::string label;
		std::string value;
	};

	class Select : public Control {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::vector<SelectOption>				options;
			std::string								value;
			std::string								placeholder = "Select...";
			ControlSize								controlSize = ControlSize::Md;
			std::function<void(const std::string&)> onChange;
		};

		explicit Select(const Args& args);

		[[nodiscard]] const std::string&			   getValue() const { return value; }
		void										   setValue(const std::string& newValue);
		[[nodiscard]] const std::vector<SelectOption>& getOptions() const { return options; }
		// Closes the menu if it's open.
		void setOptions(std::vector<SelectOption> value);
		// The chosen option's label, or the placeholder when no option has the value.
		[[nodiscard]] std::string getSelectedLabel() const;
		[[nodiscard]] bool		  isOpen() const { return menu.get() != nullptr; }
		[[nodiscard]] Ref<Menu>	  getMenu() const { return menu; }

		[[nodiscard]] const char* typeName() const override { return "Select"; }
		[[nodiscard]] StyleId	  themeStyle() const override;

	  protected:
		void onMount() override;
		void activate() override;

	  private:
		void show();
		void choose(const std::string& chosen);

		std::vector<SelectOption>				options;
		std::string								value;
		std::string								placeholder;
		ControlSize								controlSize;
		std::function<void(const std::string&)> onChange;
		Ref<Text>								text;
		Ref<Chevron>							caret;
		Ref<Menu>								menu;
	};

} // namespace ui
