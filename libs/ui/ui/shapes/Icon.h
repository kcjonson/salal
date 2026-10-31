#pragma once

// An icon by name from the scene's icon service (docs/ui-design.md, "Icons"): icons/home.svg is
// `Icon{.name = "home"}`. One tinted text-mode quad, the largest square that fits its box, centred,
// in its style's `color`; its theme style is ui::styles::icon. An unknown name draws nothing.

#include "ui/Component.h"

#include <string>

namespace ui {

	class Icon : public Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string name;
		};

		explicit Icon(const Args& args);

		[[nodiscard]] const std::string& getName() const { return name; }
		void							 setName(std::string value) { name = std::move(value); }

		[[nodiscard]] const char* typeName() const override { return "Icon"; }
		[[nodiscard]] StyleId	  themeStyle() const override;
		void					  describe(nlohmann::json& node) const override;

	  protected:
		void render() override;

	  private:
		std::string name;
	};

} // namespace ui
