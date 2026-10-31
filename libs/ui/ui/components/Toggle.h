#pragma once

// A switch (R12.9): a pill track with a knob at its left when off and its right, the track filled
// with the accent, when on; a label beside it, the whole row pressable. A press flips it and calls
// onChange with the new state; the knob slides and the track fades over motion.duration.fast. The
// track is ui::styles::toggleTrack.

#include "ui/components/Control.h"
#include "ui/shapes/Circle.h"
#include "ui/shapes/Text.h"

#include <functional>
#include <string>

namespace ui {

	class Toggle : public Control {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string				  label;
			bool					  on = false;
			std::function<void(bool)> onChange;
		};

		explicit Toggle(const Args& args);

		[[nodiscard]] bool isOn() const { return on; }
		// Sets it without calling onChange.
		void setOn(bool value);
		void setStyle(const Style& declared) override;

		[[nodiscard]] const char* typeName() const override { return "Toggle"; }
		[[nodiscard]] StyleId	  themeStyle() const override;
		void					  describe(nlohmann::json& node) const override;

	  protected:
		void onMount() override;
		void activate() override;

	  private:
		void							show();
		[[nodiscard]] Foundation::Color trackFill() const;

		std::string				  label;
		bool					  on;
		std::function<void(bool)> onChange;
		Ref<Rectangle>			  track;
		Ref<Circle>				  knob;
		Tween<float>			  slide;
		Tween<Foundation::Color>  fade;
	};

} // namespace ui
