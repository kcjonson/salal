#pragma once

// Primitive Rendering Styles
//
// Style structs for primitive rendering API using C++20 designated initializers.
// Supports optional borders, gradients, and other visual effects.

#include "graphics/Color.h"
#include <optional>

namespace Foundation { // NOLINT(readability-identifier-naming)

	// Border positioning mode (CSS-like)
	enum class BorderPosition {
		Inside, // Border drawn inside rect bounds (content area reduced)
		Center, // Border centered on rect edge (default, matches CSS)
		Outside // Border drawn outside rect bounds (total size increased)
	};

	// The sides of a rect its border is drawn on.
	struct BorderSides {
		bool top = true;
		bool right = true;
		bool bottom = true;
		bool left = true;
	};

	// Optional border style for rectangles and circles (a circle draws only its color,
	// width and position). A side left out of `sides` has no border, the sides beside it
	// run straight on to the rect's edge and end square there, and with a width the
	// corners beside it are square, fill and shadow too: `cornerRadius` rounds only a
	// corner whose two sides both have the border, as a tab's top corners.
	struct BorderStyle {
		Color		   color = Color::white();
		float		   width = 1.0F;
		float		   cornerRadius = 0.0F;
		BorderPosition position = BorderPosition::Center;
		BorderSides	   sides{};
	};

	// Two-stop linear gradient for rectangle fills. The gradient is realized by
	// the batch renderer as per-corner vertex colors interpolated across the quad,
	// so it composes with the rounded-corner/border SDF.
	struct LinearGradient {
		Color from;				  // start color (top, or left if horizontal)
		Color to;				  // end color (bottom, or right if horizontal)
		bool  horizontal = false; // false = vertical (top->bottom)
	};

	// An outer shadow or glow: a rect's (CSS box-shadow, outset only), a circle's and
	// a polygon's, and text's (CSS text-shadow with a spread, which is a halo's width;
	// Primitives::drawGlyphShadow). Rendered by the SDF shader as a soft falloff over
	// `blur` px either side of the `spread`-grown shape's edge, so it is one draw,
	// perfectly rounded, no stacked geometry; a polygon's is its fill again (PolygonStyle).
	struct BoxShadow {
		Color color = Color(0.0F, 0.0F, 0.0F, 0.5F);
		float blur = 8.0F;			 // softness radius (px)
		float spread = 0.0F;		 // grow the shadow shape before blurring (px)
		Vec2  offset = {0.0F, 0.0F}; // shadow displacement (px); +y is down
	};

	// Rectangle visual style
	struct RectStyle {
		Color						  fill = Color::white(); // fallback when no gradient
		std::optional<BorderStyle>	  border = std::nullopt;
		std::optional<LinearGradient> gradient = std::nullopt;
		std::optional<BoxShadow>	  boxShadow = std::nullopt;
	};

	// Line visual style
	struct LineStyle {
		Color color = Color::white();
		float width = 1.0F;
	};

	// Circle visual style. The shadow is a rect's: a disk grown by `spread`, its edge
	// fading over `blur` either side.
	struct CircleStyle {
		Color					   fill = Color::white();
		std::optional<BorderStyle> border = std::nullopt;
		std::optional<BoxShadow>   shadow = std::nullopt;
	};

	// Polygon visual style. The border is a stroke outside the outline, `width`
	// wide, that reaches a pixel under the fill's edge so nothing shows between them.
	// The shadow is the fill's shape grown by `spread`, solid to that outline and
	// fading to nothing `blur` outside it, where a rect's or a circle's fades from
	// `blur` inside its edge: at the same blur a polygon's shadow reads about half a
	// blur wider.
	struct PolygonStyle {
		Color					 fill = Color::white();
		std::optional<LineStyle> border = std::nullopt;
		std::optional<BoxShadow> shadow = std::nullopt;
	};

	// Text horizontal alignment
	enum class HorizontalAlign { Left, Center, Right };

	// Text vertical alignment
	enum class VerticalAlign { Top, Middle, Bottom };

	// CSS text-transform: case folding applied at draw time.
	enum class TextTransform { None, Uppercase };

	// Text visual style
	struct TextStyle {
		Color			color = Color::white();
		float			fontSize = 16.0F;
		HorizontalAlign hAlign = HorizontalAlign::Left;
		VerticalAlign	vAlign = VerticalAlign::Top;
		bool			wordWrap = false; // CSS word-wrap: when true and width is set, text wraps
	};

} // namespace Foundation
