#pragma once

// A row of exclusive segments, after worldsim's SegmentedControl (libs/ui/components/
// segmentedcontrol, a4226e17), interactive now: the selected segment takes its tone's wash and
// colour. Segments share any width the control is given and hug their labels otherwise; the control
// is one Tab stop whose arrows move among the segments, and a press selects one and calls onChange
// with its index. The tray is ui::styles::segmented, a segment ui::styles::segment.

#include "ui/components/Control.h"
#include "ui/theme/Variants.h"

#include <functional>
#include <string>
#include <vector>

namespace ui {

	class SegmentedControl : public Rectangle {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::vector<std::string> options;
			int						 selected = 0;
			Tone					 tone = Tone::Accent;
			ControlSize				 controlSize = ControlSize::Md;
			std::function<void(int)> onChange;
		};

		explicit SegmentedControl(const Args& args);

		[[nodiscard]] int getSelected() const { return selected; }
		// Selects without calling onChange.
		void setSelected(int index);
		void setStyle(const Style& declared) override;
		// A segment, by the control.
		void choose(int index);

		[[nodiscard]] const char* typeName() const override { return "SegmentedControl"; }
		[[nodiscard]] StyleId	  themeStyle() const override;

	  protected:
		void onMount() override;

	  private:
		void show();

		std::vector<std::string>  labels;
		int						  selected;
		Tone					  tone;
		ControlSize				  controlSize;
		std::function<void(int)>  onChange;
		std::vector<Ref<Control>> segments;
	};

} // namespace ui
