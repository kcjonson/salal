#pragma once

// The icon rules of docs/ui-design.md, "Icons", checked on one SVG's text before salal_icons()
// builds it: a square viewBox; filled path, rect, circle and polygon only, no strokes; one colour;
// no gradients, masks, clip paths, filters, text, images or references. A problem names the file,
// the line and the element.

#include <vector/SvgMarkup.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui::iconbuild {

	using renderer::svg::Problem;

	enum class FillRule : std::uint8_t { NonZero, EvenOdd };

	// What the build needs from a file that passes.
	struct SvgFacts {
		float	 viewBoxSize = 0.0F;
		FillRule fillRule = FillRule::NonZero;
	};

	std::optional<SvgFacts> checkSvg(std::string_view file, std::string_view text, std::vector<Problem>& problems);

} // namespace ui::iconbuild
