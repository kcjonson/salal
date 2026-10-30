#pragma once

// A run of text (R12.4) in its box. An auto axis hugs the text, measured with the font, size, letter
// spacing and case it draws with (R10.14); with `wrap`, a decided or bounded width breaks words to
// fit it and the height follows. Explicit newlines always break. The text aligns in its box by
// style.textAlign and style.verticalAlign; `position` is the box's top-left, the top of its first
// line box, never the baseline. In a flex row it shrinks no narrower than its longest word, or its
// whole width when it doesn't wrap.
//
// Style: color (white), fontFamily (the game's default), fontSize (16), textAlign, verticalAlign,
// letterSpacing (single-line text only) and textTransform. Its glyphs are laid out once and kept
// until the text, its style, its size or its opacity changes.

#include "ui/Component.h"

#include <font/FontRenderer.h>

#include <optional>
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

		struct Extent {
			Foundation::Vec2 size{0.0F, 0.0F};
			int				 lines = 0;
		};

		explicit Text(const Args& args);

		[[nodiscard]] const std::string& getText() const { return text; }
		void							 setText(std::string value);
		void							 setStyle(const Style& value) override;
		[[nodiscard]] bool				 wraps() const { return wrap; }
		// The text's own extent laid out in its box as it is now, and its line count.
		[[nodiscard]] Extent measureExtent() const;

		[[nodiscard]] const char* typeName() const override { return "Text"; }
		void					  describe(nlohmann::json& node) const override;

	  protected:
		void										  render() override;
		[[nodiscard]] std::optional<Foundation::Vec2> intrinsicSize(std::optional<float> width, float availableWidth) const override;
		[[nodiscard]] float							  minContentWidth() const override;

	  private:
		[[nodiscard]] std::string shown() const; // with textTransform applied
		[[nodiscard]] Extent	  measureAt(std::optional<float> wrapWidth) const;
		void					  layOut();

		std::string									   text;
		bool										   wrap;
		std::vector<Renderer::FontRenderer::GlyphQuad> glyphs;
		Foundation::Vec2							   glyphSize{-1.0F, -1.0F}; // the box glyphs were laid out in
		float										   glyphOpacity = -1.0F;	// what they were coloured for; -1 for not laid out
	};

} // namespace ui
