#pragma once

// A horizontal level, after worldsim's ProgressBar (libs/ui/components/progress, a4226e17): an
// inset track and a fill in its tone, with either a header row above it (label left, value text
// right) or both inside the bar. The value is 0 to 1, clamped; Tone::Auto picks the tone by value.
// Its width is the parent's unless its size says otherwise. Display only. Worldsim's segment
// notches, which only its sandbox drew, aren't ported.

#include "ui/shapes/Rectangle.h"
#include "ui/shapes/Text.h"
#include "ui/theme/Variants.h"

#include <string>

namespace ui {

	class ProgressBar : public Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			float		value = 1.0F;
			Tone		tone = Tone::Auto;
			std::string label;
			std::string valueText;
			ControlSize controlSize = ControlSize::Md;
			bool		inlineLabel = false; // label and value text inside the bar
		};

		explicit ProgressBar(const Args& args);

		[[nodiscard]] float getValue() const { return value; }
		void				setValue(float fraction);
		void				setTone(Tone toneValue);
		void				setValueText(std::string text);
		// A bar mounted with neither a label nor value text, and not inline, has no row to show one in.
		void setLabel(std::string text);
		// Colours the fill again, so a theme change reaches the tone.
		void setStyle(const Style& declared) override;

		[[nodiscard]] const char* typeName() const override { return "ProgressBar"; }

	  protected:
		void onMount() override;

	  private:
		void fill();
		void showTexts();

		float		   value;
		Tone		   tone;
		std::string	   label;
		std::string	   valueText;
		ControlSize	   controlSize;
		bool		   inlineLabel;
		Ref<Rectangle> bar;
		Ref<Component> texts; // the header row, or the track for an inline bar
		Ref<Text>	   shownLabel;
		Ref<Text>	   shownValue;
	};

} // namespace ui
