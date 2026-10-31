#pragma once

// A button, after worldsim's Button (libs/ui/components/button, a4226e17): a label and an optional
// leading icon (a child, gap 30) centred in a row, its height and font size from its control size
// (gap 19). Primary and Danger fill with their tone and draw the label in the tone's on-colour;
// Secondary is ui::styles::button; Ghost has no fill or border. A press calls onPress.

#include "ui/components/Control.h"
#include "ui/shapes/Icon.h"
#include "ui/shapes/Text.h"
#include "ui/theme/Variants.h"

#include <functional>
#include <string>

namespace ui {

	enum class ButtonVariant : std::uint8_t { Primary, Secondary, Ghost, Danger };

	class Button : public Control {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string			  label;
			std::string			  icon; // a name in the scene's icon atlases; empty: none
			ButtonVariant		  variant = ButtonVariant::Secondary;
			ControlSize			  controlSize = ControlSize::Md;
			std::function<void()> onPress;
		};

		explicit Button(const Args& args);

		[[nodiscard]] const std::string& getLabel() const { return label; }
		void							 setLabel(std::string value);
		void							 setOnPress(std::function<void()> callback) { onPress = std::move(callback); }
		// Tints again, so a theme change reaches the variant.
		void setStyle(const Style& declared) override;

		[[nodiscard]] const char* typeName() const override { return "Button"; }
		[[nodiscard]] StyleId	  themeStyle() const override;

	  protected:
		void onMount() override;
		void activate() override;

	  private:
		void tint();

		std::string			  label;
		std::string			  iconName;
		ButtonVariant		  variant;
		ControlSize			  controlSize;
		std::function<void()> onPress;
		Ref<Icon>			  icon;
		Ref<Text>			  text;
	};

} // namespace ui
