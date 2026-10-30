#include "ui/shapes/Rectangle.h"

#include "ui/detail/Snapshot.h"

#include <primitives/Primitives.h>

#include <algorithm>
#include <cmath>

namespace ui {

	namespace {
		constexpr Foundation::Color kBackground = Foundation::Color::white();
		constexpr Foundation::Color kBorder = Foundation::Color::white();
	} // namespace

	// A shadow reaches its blur and spread past the box, moved by its offset.
	float Rectangle::inkExtent() const {
		if (!style.boxShadow) {
			return 0.0F;
		}
		const Foundation::BoxShadow& shadow = *style.boxShadow;
		return std::max(0.0F, shadow.blur + shadow.spread + std::max(std::abs(shadow.offset.x), std::abs(shadow.offset.y)));
	}

	void Rectangle::describe(nlohmann::json& node) const {
		const bool bordered = style.borderWidth.value_or(0.0F) > 0.0F;
		node["style"] = detail::styleJson(
			style.backgroundColor.value_or(kBackground),
			std::nullopt,
			bordered ? std::optional(style.borderColor.value_or(kBorder)) : std::nullopt
		);
	}

	void Rectangle::render() {
		const float							   alpha = opacity();
		const float							   borderWidth = style.borderWidth.value_or(0.0F);
		const float							   radius = style.borderRadius.value_or(0.0F);
		std::optional<Foundation::BorderStyle> border;
		if (borderWidth > 0.0F || radius > 0.0F) {
			border = Foundation::BorderStyle{
				.color = detail::withOpacity(style.borderColor.value_or(kBorder), borderWidth > 0.0F ? alpha : 0.0F),
				.width = borderWidth,
				.cornerRadius = radius,
				.position = Foundation::BorderPosition::Inside,
			};
		}
		std::optional<Foundation::BoxShadow> shadow = style.boxShadow;
		if (shadow) {
			shadow->color = detail::withOpacity(shadow->color, alpha);
		}
		Renderer::Primitives::drawRect({
			.bounds = {0.0F, 0.0F, size.x, size.y},
			.style =
				{.fill = detail::withOpacity(style.backgroundColor.value_or(kBackground), alpha), .border = border, .boxShadow = shadow},
			.id = getId().empty() ? nullptr : getId().c_str(),
			.zIndex = drawKey(),
		});
	}

} // namespace ui
