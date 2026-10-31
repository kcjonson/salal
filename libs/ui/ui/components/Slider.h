#pragma once

// A horizontal slider, after worldsim's Slider (libs/ui/components/slider, a4226e17): an optional
// label, a track filled to the value with a thumb at the fill's end, and the value as text when
// asked for. Dragging sets the value (the pointer is captured from the press to the release); a
// player's left and right move it a step, or a fiftieth of the range when it has none, and up and
// down are left to directional focus. Each change calls onChange. The track is
// ui::styles::progressTrack.

#include "ui/components/Control.h"
#include "ui/shapes/Circle.h"
#include "ui/shapes/Text.h"

#include <functional>
#include <string>

namespace ui {

	class Slider : public Control {
	  public:
		struct Args {
			UI_COMMON_ARGS
			float							  min = 0.0F;
			float							  max = 1.0F;
			float							  step = 0.0F; // 0: continuous
			float							  value = 0.0F;
			std::string						  label;
			bool							  showValue = false;
			std::function<std::string(float)> format; // the value's text; unset, two decimals
			std::function<void(float)>		  onChange;
		};

		explicit Slider(const Args& args);

		[[nodiscard]] float getValue() const { return value; }
		// Sets it, snapped and clamped, without calling onChange.
		void setValue(float newValue);
		void setStyle(const Style& declared) override;

		[[nodiscard]] const char* typeName() const override { return "Slider"; }
		[[nodiscard]] StyleId	  themeStyle() const override;

	  protected:
		void onMount() override;
		void handlePointer(PointerEvent& event) override;
		void handleKey(KeyEvent& event) override;

	  private:
		[[nodiscard]] float snapped(float raw) const;
		void				change(float raw);
		void				show();

		float							  min;
		float							  max;
		float							  step;
		float							  value;
		std::string						  label;
		bool							  showValue;
		std::function<std::string(float)> format;
		std::function<void(float)>		  onChange;
		Ref<Component>					  trackArea;
		Ref<Rectangle>					  fill;
		Ref<Circle>						  thumb;
		Ref<Text>						  valueText;
	};

} // namespace ui
