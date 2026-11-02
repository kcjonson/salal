#pragma once

// The tree snapshot's value shapes, for the serializer and the types' describe(). Internal to ui.

#include <graphics/Color.h>
#include <graphics/Rect.h>

#include <nlohmann/json.hpp>

#include <optional>

namespace ui::detail {

	inline nlohmann::json rectJson(const Foundation::Rect& rect) {
		return {{"x", rect.x}, {"y", rect.y}, {"w", rect.width}, {"h", rect.height}};
	}

	inline nlohmann::json colorJson(const std::optional<Foundation::Color>& color) {
		if (!color) {
			return nullptr;
		}
		return nlohmann::json::array({color->r, color->g, color->b, color->a});
	}

	// The snapshot's `style`: the fill, text and border colours a type resolves to.
	inline nlohmann::json styleJson(
		const std::optional<Foundation::Color>& fill,
		const std::optional<Foundation::Color>& text,
		const std::optional<Foundation::Color>& border
	) {
		return {{"fill", colorJson(fill)}, {"text", colorJson(text)}, {"border", colorJson(border)}};
	}

	inline Foundation::Color withOpacity(Foundation::Color color, float opacity) {
		color.a *= opacity;
		return color;
	}

} // namespace ui::detail
