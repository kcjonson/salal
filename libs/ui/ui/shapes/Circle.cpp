#include "ui/shapes/Circle.h"

#include "ui/detail/Snapshot.h"

#include <primitives/Primitives.h>

#include <algorithm>

namespace ui {

	namespace {
		constexpr Foundation::Color kBackground = Foundation::Color::white();
		constexpr Foundation::Color kBorder = Foundation::Color::white();
	} // namespace

	void Circle::describe(nlohmann::json& node) const {
		const bool bordered = style.borderWidth.value_or(0.0F) > 0.0F;
		node["style"] = detail::styleJson(
			style.backgroundColor.value_or(kBackground),
			std::nullopt,
			bordered ? std::optional(style.borderColor.value_or(kBorder)) : std::nullopt
		);
	}

	void Circle::render() {
		const float							   alpha = opacity();
		const float							   borderWidth = style.borderWidth.value_or(0.0F);
		std::optional<Foundation::BorderStyle> border;
		if (borderWidth > 0.0F) {
			border = Foundation::BorderStyle{
				.color = detail::withOpacity(style.borderColor.value_or(kBorder), alpha),
				.width = borderWidth,
				.position = Foundation::BorderPosition::Inside,
			};
		}
		Renderer::Primitives::drawCircle({
			.center = size * 0.5F,
			.radius = std::min(size.x, size.y) * 0.5F,
			.style = {.fill = detail::withOpacity(style.backgroundColor.value_or(kBackground), alpha), .border = border},
			.id = getId().empty() ? nullptr : getId().c_str(),
			.key = drawKey(),
		});
	}

} // namespace ui
