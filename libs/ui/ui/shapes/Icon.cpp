#include "ui/shapes/Icon.h"

#include "ui/Context.h"
#include "ui/detail/Snapshot.h"
#include "ui/theme/Tokens.h"

#include <primitives/Primitives.h>

#include <algorithm>

namespace ui {

	namespace {
		const Foundation::Color kColor = Foundation::Color::white();
	} // namespace

	Icon::Icon(const Args& args)
		: name(args.name) {}

	StyleId Icon::themeStyle() const {
		return styles::icon.theme;
	}

	void Icon::render() {
		const float side = std::min(size.x, size.y);
		if (side <= 0.0F || getContext() == nullptr) {
			return;
		}
		const Foundation::Color								   color = detail::withOpacity(style.color.value_or(kColor), opacity());
		const std::optional<Renderer::FontRenderer::GlyphQuad> quad =
			getContext()->icons().quad(name, {(size.x - side) * 0.5F, (size.y - side) * 0.5F}, side, {color.r, color.g, color.b, color.a});
		if (quad.has_value()) {
			Renderer::Primitives::drawGlyphs(&*quad, 1, drawKey());
		}
	}

	void Icon::describe(nlohmann::json& node) const {
		node["icon"] = name;
		node["style"] = detail::styleJson(std::nullopt, style.color.value_or(kColor), std::nullopt);
	}

} // namespace ui
