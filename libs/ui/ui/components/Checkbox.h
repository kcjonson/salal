#pragma once

// A checkbox (R12.9): a box, its accent fill and tick when checked, and a label beside it, the whole
// row pressable. A press toggles it and calls onChange with the new state. The box is
// ui::styles::check.

#include "ui/components/Control.h"
#include "ui/shapes/Line.h"
#include "ui/shapes/Text.h"

#include <functional>
#include <string>

namespace ui {

	class Checkbox : public Control {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string				  label;
			bool					  checked = false;
			std::function<void(bool)> onChange;
		};

		explicit Checkbox(const Args& args);

		[[nodiscard]] bool isChecked() const { return checked; }
		// Sets it without calling onChange.
		void setChecked(bool value);
		void setStyle(const Style& declared) override;

		[[nodiscard]] const char* typeName() const override { return "Checkbox"; }
		[[nodiscard]] StyleId	  themeStyle() const override;
		void					  describe(nlohmann::json& node) const override;

	  protected:
		void onMount() override;
		void activate() override;

	  private:
		void show();

		std::string				  label;
		bool					  checked;
		std::function<void(bool)> onChange;
		Ref<Rectangle>			  box;
		Ref<Line>				  tickShort;
		Ref<Line>				  tickLong;
	};

} // namespace ui
