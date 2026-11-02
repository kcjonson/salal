#pragma once

// A labelled readout, after worldsim's Stat (libs/ui/components/stat, a4226e17): a small upper-case
// label over a large value with an optional smaller unit after it. The tone colours the value; the
// control size sets the value's size (the stat.valueSize tokens). Display only.

#include "ui/Component.h"
#include "ui/shapes/Text.h"
#include "ui/theme/Variants.h"

#include <string>

namespace ui {

	class Stat : public Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string label;
			std::string value;
			std::string unit; // empty: none
			Tone		tone = Tone::Default;
			ControlSize controlSize = ControlSize::Md;
		};

		explicit Stat(const Args& args);

		void setValue(std::string text);
		void setTone(Tone value);
		// Sizes and colours the value again, so a theme change reaches it.
		void setStyle(const Style& value) override;

		[[nodiscard]] const char* typeName() const override { return "Stat"; }

	  protected:
		void onMount() override;

	  private:
		void restyleValue();

		std::string label;
		std::string value;
		std::string unit;
		Tone		tone;
		ControlSize controlSize;
		Ref<Text>	valueText;
		Ref<Text>	unitText;
	};

} // namespace ui
