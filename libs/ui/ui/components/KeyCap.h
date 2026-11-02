#pragma once

// A keyboard key in a shortcut legend, after worldsim's KeyCap (libs/ui/components/keycap,
// a4226e17): an inset rounded key with a thicker bottom edge and its label upper case and centred,
// hugging the label with an 18 px floor so one character isn't narrower than a word. Display only;
// its theme styles are ui::styles::keycap and keycapEdge.

#include "ui/shapes/Rectangle.h"
#include "ui/shapes/Text.h"

#include <string>

namespace ui {

	class KeyCap : public Rectangle {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string label;
		};

		explicit KeyCap(const Args& args);

		void setLabel(std::string value);

		[[nodiscard]] const char* typeName() const override { return "KeyCap"; }
		[[nodiscard]] StyleId	  themeStyle() const override;

	  protected:
		void onMount() override;

	  private:
		std::string label;
		Ref<Text>	text;
	};

} // namespace ui
