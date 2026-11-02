#pragma once

// A disc (R12.2) inscribed in its box, centred, its radius half the box's shorter side; background,
// border and boxShadow from its style, as SDF quads. Unset, the background is white.

#include "ui/Component.h"

namespace ui {

	class Circle : public Component {
	  public:
		[[nodiscard]] const char* typeName() const override { return "Circle"; }
		[[nodiscard]] float		  inkExtent() const override;
		void					  describe(nlohmann::json& node) const override;

	  protected:
		void						render() override;
		[[nodiscard]] PointerEvents defaultPointerEvents() const override { return PointerEvents::Auto; }
		// The disc, not its box.
		[[nodiscard]] bool containsPoint(Foundation::Vec2 local) const override;
	};

} // namespace ui
