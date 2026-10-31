#pragma once

// A straight stroke (R12.3) from `start` to `end`, given in the parent's space, in style.color (white
// unset). Its box is the endpoints' bounds grown by half the width on each side, so `position` and
// `size` in its Args are ignored; moving it moves both endpoints, and layout can't resize it.

#include "ui/Component.h"

namespace ui {

	class Line : public Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			Foundation::Vec2 start{0.0F, 0.0F};
			Foundation::Vec2 end{0.0F, 0.0F};
			float			 lineWidth = 1.0F;
			bool			 roundCaps = false;
		};

		explicit Line(const Args& args);

		void setSize(Foundation::Vec2 /*value*/) override {}
		// Both endpoints, in the parent's space.
		void					  setEndpoints(Foundation::Vec2 start, Foundation::Vec2 end);
		[[nodiscard]] const char* typeName() const override { return "Line"; }
		void					  describe(nlohmann::json& node) const override;

	  protected:
		void onMount() override;
		void render() override;

	  private:
		Foundation::Vec2 initialStart;
		Foundation::Vec2 initialEnd;
		Foundation::Vec2 localStart{0.0F, 0.0F}; // in its own box
		Foundation::Vec2 localEnd{0.0F, 0.0F};
		float			 lineWidth;
		bool			 roundCaps;
	};

} // namespace ui
