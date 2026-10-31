#pragma once

// A tooltip, after worldsim's Tooltip and TooltipManager (libs/ui/components/tooltip, a4226e17): a
// box on the tooltip style in the tooltip tier, above every overlay and never hit by the pointer. Its
// content is a title, a description and a hotkey hint (a KeyCap), or any components a build function
// adds; its width is measured from them, up to maxWidth, past which text wraps (gap 19). The context
// is what was TooltipManager: a component given ui::tooltip() through setTooltip shows one 16 px
// under the pointer once the pointer rests on it (Context, "The overlay stack"). It fades in.

#include "ui/shapes/Rectangle.h"

#include <functional>
#include <string>

namespace ui {

	struct TooltipText {
		std::string title;
		std::string description;
		std::string hotkey; // such as "Ctrl+S"
	};

	class Tooltip : public Rectangle {
	  public:
		struct Args {
			UI_COMMON_ARGS
			TooltipText text;
			float		maxWidth = 280.0F;
		};

		explicit Tooltip(const Args& args);

		// Opens one under `owner` at `at` in its space, then `build`, when given, adds its content.
		static Ref<Tooltip> open(Component& owner, Foundation::Vec2 at, const Args& args, const std::function<void(Tooltip&)>& build = {});

		[[nodiscard]] const char* typeName() const override { return "Tooltip"; }
		[[nodiscard]] StyleId	  themeStyle() const override;

	  protected:
		void						onMount() override;
		[[nodiscard]] PointerEvents defaultPointerEvents() const override { return PointerEvents::None; }

	  private:
		TooltipText	 text;
		float		 maxWidth;
		Tween<float> appear;
	};

	// What Component::setTooltip takes: a Tooltip of `text`, or one `build` fills with any components.
	[[nodiscard]] Component::TooltipOpener tooltip(TooltipText text);
	[[nodiscard]] Component::TooltipOpener tooltip(std::function<void(Tooltip&)> build);

} // namespace ui
