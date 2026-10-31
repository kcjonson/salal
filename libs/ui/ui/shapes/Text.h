#pragma once

// A run of text (R12.4) in its box. An axis its size leaves at 0 hugs the text, measured with the
// font it draws with; a given width with `wrap` breaks words to fit it, and the text aligns in its
// box by style.textAlign and style.verticalAlign. Explicit newlines always break. `position` is the
// box's top-left, the top of its first line box, never the baseline.
//
// Style: color (white), fontFamily (the game's default), fontSize (16), textAlign, verticalAlign,
// letterSpacing (single-line text only) and textTransform. Its glyphs are laid out once and kept
// until the text, its style, its size or its opacity changes.

#include "ui/Component.h"

#include <font/FontRenderer.h>

#include <string>
#include <vector>

namespace ui {

	class Text : public Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string text;
			bool		wrap = false;
		};

		explicit Text(const Args& args);

		[[nodiscard]] const std::string& getText() const { return text; }
		void							 setText(std::string value);
		void							 setSize(Foundation::Vec2 value) override;
		void							 setStyle(const Style& value) override;
		// The text's own extent at its box's width, and how many lines it takes.
		[[nodiscard]] Foundation::Vec2 getMeasured() const { return measured; }
		[[nodiscard]] int			   getLineCount() const { return lineCount; }

		[[nodiscard]] const char* typeName() const override { return "Text"; }
		// ui::styles::text::body.
		[[nodiscard]] StyleId themeStyle() const override;
		void				  describe(nlohmann::json& node) const override;

	  protected:
		void onMount() override;
		void render() override;

	  private:
		[[nodiscard]] std::string shown() const; // with textTransform applied
		[[nodiscard]] bool		  multiline(const std::string& content) const;
		[[nodiscard]] float		  wrapWidth() const;
		void					  measure();
		void					  layOut();

		std::string									   text;
		bool										   wrap;
		bool										   hugWidth = true;
		bool										   hugHeight = true;
		Foundation::Vec2							   measured{0.0F, 0.0F};
		int											   lineCount = 0;
		std::vector<Renderer::FontRenderer::GlyphQuad> glyphs;
		float										   glyphOpacity = -1.0F; // what glyphs were coloured for; -1 for not laid out
	};

} // namespace ui
