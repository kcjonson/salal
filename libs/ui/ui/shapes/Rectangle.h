#pragma once

// A box (R12.1): its background, a border inside its edge, rounded corners and a shadow, from its
// style. One draw, two with a shadow. Unset, the background is white and there is no border.

#include "ui/Component.h"

namespace ui {

	class Rectangle : public Component {
	  public:
		[[nodiscard]] const char* typeName() const override { return "Rectangle"; }
		[[nodiscard]] float		  inkExtent() const override;
		void					  describe(nlohmann::json& node) const override;

	  protected:
		void render() override;
	};

} // namespace ui
