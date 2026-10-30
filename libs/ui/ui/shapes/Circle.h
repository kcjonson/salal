#pragma once

// A disc (R12.2) inscribed in its box, centred, its radius half the box's shorter side; background and
// border from its style, as an SDF quad. Unset, the background is white.

#include "ui/Component.h"

namespace ui {

	class Circle : public Component {
	  public:
		[[nodiscard]] const char* typeName() const override { return "Circle"; }
		void					  describe(nlohmann::json& node) const override;

	  protected:
		void render() override;
	};

} // namespace ui
