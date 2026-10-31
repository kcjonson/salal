#pragma once

// A small status pill, after worldsim's Badge (libs/ui/components/badge, a4226e17): its tone's wash
// with a border in the tone, an optional leading dot, and an upper-case label, hugging the label.
// Display only. Its theme style is ui::styles::badge; a background or border its own style sets
// wins over the tone's.

#include "ui/shapes/Circle.h"
#include "ui/shapes/Rectangle.h"
#include "ui/shapes/Text.h"
#include "ui/theme/Variants.h"

#include <string>

namespace ui {

	class Badge : public Rectangle {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string label;
			Tone		tone = Tone::Default;
			bool		dot = false;
		};

		explicit Badge(const Args& args);

		void setLabel(std::string value);
		void setTone(Tone value);
		// Tints again, so a theme change reaches the tone.
		void setStyle(const Style& value) override;

		[[nodiscard]] const char* typeName() const override { return "Badge"; }
		[[nodiscard]] StyleId	  themeStyle() const override;

	  protected:
		void onMount() override;

	  private:
		void tint();

		std::string label;
		Tone		tone;
		bool		dot;
		Ref<Circle> marker;
		Ref<Text>	text;
	};

} // namespace ui
