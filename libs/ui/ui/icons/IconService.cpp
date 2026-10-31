#include "ui/icons/IconService.h"

#include <primitives/Primitives.h>
#include <utils/Log.h>

#include <algorithm>

namespace ui {

	bool IconService::add(const IconAtlasData& data) {
		std::string				 error;
		std::optional<IconAtlas> loaded = IconAtlas::load(data, error);
		if (!loaded.has_value()) {
			LOG_ERROR(UI, "%s", error.c_str());
			return false;
		}
		atlases.push_back({.icons = std::move(*loaded)});
		return true;
	}

	IconService::Atlas* IconService::atlasFor(std::string_view name) {
		for (auto it = atlases.rbegin(); it != atlases.rend(); ++it) {
			if (it->icons.find(name) != nullptr) {
				return &*it;
			}
		}
		return nullptr;
	}

	bool IconService::has(std::string_view name) const {
		return std::any_of(atlases.begin(), atlases.end(), [&](const Atlas& atlas) { return atlas.icons.find(name) != nullptr; });
	}

	std::optional<Renderer::FontRenderer::GlyphQuad>
	IconService::quad(std::string_view name, glm::vec2 topLeft, float size, glm::vec4 color) {
		Atlas* atlas = atlasFor(name);
		if (atlas == nullptr) {
			if (warned.emplace(name).second) {
				LOG_WARNING(UI, "No icon named \"%.*s\" in the scene's icon atlases", static_cast<int>(name.size()), name.data());
			}
			return std::nullopt;
		}
		if (!atlas->texture.isValid()) {
			atlas->texture = Renderer::Primitives::createTexture({
				.width = atlas->icons.width(),
				.height = atlas->icons.height(),
				.format = Renderer::TextureFormat::Rgb8,
				.linear = true,
				.pixels = atlas->icons.pixels().data(),
			});
			if (!atlas->texture.isValid()) {
				return std::nullopt;
			}
		}
		return atlas->icons.quad(name, topLeft, size, color, atlas->texture.handle());
	}

} // namespace ui
