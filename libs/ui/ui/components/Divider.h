#pragma once

// A horizontal rule, after worldsim's Divider (libs/ui/components/divider, a4226e17): a hairline
// across its width, or with a label, two hairlines either side of the upper-case label. Its width
// is the parent's unless its size says otherwise. Display only; the lines are ui::styles::divider.

#include "ui/Component.h"
#include "ui/shapes/Text.h"

#include <string>

namespace ui {

	class Divider : public Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string label; // empty: a bare rule
		};

		explicit Divider(const Args& args);

		[[nodiscard]] const char* typeName() const override { return "Divider"; }

	  protected:
		void onMount() override;

	  private:
		std::string label;
	};

} // namespace ui
