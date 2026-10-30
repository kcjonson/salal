#pragma once

// How a component looks, with CSS's names. Look only: geometry and layout are the component's own
// properties, so a theme changes how things look and never where they are (docs/ui-design.md,
// "Component API"). Every field is optional, and one left unset falls back to the component type's
// default. A component takes a style value, a theme's or one extended from it:
//
//   .style = pane.extend({.backgroundColor = player.color})

#include <graphics/Color.h>
#include <graphics/PrimitiveStyles.h>
#include <primitives/FontFamily.h>

#include <optional>

namespace ui {

	struct Style {
		std::optional<Foundation::Color>		   backgroundColor;
		std::optional<Foundation::Color>		   borderColor;
		std::optional<float>					   borderWidth; // px, inside the box
		std::optional<float>					   borderRadius;
		std::optional<Foundation::BoxShadow>	   boxShadow;
		std::optional<float>					   opacity; // 0 to 1, multiplied down the tree
		std::optional<Foundation::Color>		   color;	// text, and a line's stroke
		std::optional<Renderer::FontFamily>		   fontFamily;
		std::optional<float>					   fontSize; // px
		std::optional<Foundation::HorizontalAlign> textAlign;
		std::optional<Foundation::VerticalAlign>   verticalAlign;
		std::optional<float>					   letterSpacing; // px between glyphs
		std::optional<Foundation::TextTransform>   textTransform;

		// A copy with the overrides' set fields on top, as JS spreads {...pane, backgroundColor}.
		[[nodiscard]] Style extend(const Style& overrides) const;
	};

} // namespace ui
