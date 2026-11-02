#include "ui/icons/IconAtlas.h"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <map>
#include <memory>

namespace ui {

	std::optional<IconAtlas> IconAtlas::load(const IconAtlasData& data, std::string& error) {
		IconAtlas atlas;
		// stb's flip is process-wide and FontRenderer sets it; decode unflipped and flip here.
		stbi_set_flip_vertically_on_load(0);
		int		   width = 0;
		int		   height = 0;
		int		   channels = 0;
		const auto decoded = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>(
			stbi_load_from_memory(data.png.data(), static_cast<int>(data.png.size()), &width, &height, &channels, 3), &stbi_image_free
		);
		if (decoded == nullptr) {
			error = std::string(data.name) + "'s icon atlas PNG doesn't decode";
			return std::nullopt;
		}
		const size_t rowBytes = static_cast<size_t>(width) * 3;
		atlas.rgb.resize(rowBytes * static_cast<size_t>(height));
		for (int row = 0; row < height; ++row) {
			std::copy_n(
				decoded.get() + rowBytes * static_cast<size_t>(height - 1 - row),
				rowBytes,
				atlas.rgb.data() + rowBytes * static_cast<size_t>(row)
			);
		}
		atlas.atlasWidth = width;
		atlas.atlasHeight = height;

		std::string json;
		for (std::string_view piece : data.json) {
			json += piece;
		}
		try {
			const nlohmann::json  parsed = nlohmann::json::parse(json);
			const nlohmann::json& meta = parsed.at("atlas");
			if (meta.at("width").get<int>() != width || meta.at("height").get<int>() != height || meta.at("yOrigin") != "top") {
				error = std::string(data.name) + "'s icon JSON doesn't describe its PNG";
				return std::nullopt;
			}
			atlas.range = meta.at("distanceRange").get<float>();
			std::map<std::uint32_t, Glyph> byCodepoint;
			for (const nlohmann::json& glyph : parsed.at("glyphs")) {
				const nlohmann::json& bounds = glyph.at("atlasBounds");
				const nlohmann::json& plane = glyph.at("planeBounds");
				const float			  left = bounds.at("left").get<float>() / static_cast<float>(width);
				const float			  right = bounds.at("right").get<float>() / static_cast<float>(width);
				// yOrigin top in the file; the uploaded rows go bottom first.
				const float top = 1.0F - bounds.at("top").get<float>() / static_cast<float>(height);
				const float bottom = 1.0F - bounds.at("bottom").get<float>() / static_cast<float>(height);
				byCodepoint[glyph.at("unicode").get<std::uint32_t>()] =
					Glyph{.uvMin = {left, bottom}, .uvMax = {right, top}, .padding = -plane.at("left").get<float>()};
			}
			for (const IconName& name : data.names) {
				const auto found = byCodepoint.find(name.codepoint);
				if (found == byCodepoint.end()) {
					error = std::string(data.name) + "'s icon " + std::string(name.name) + " has no glyph in its JSON";
					return std::nullopt;
				}
				atlas.glyphs.emplace_back(std::string(name.name), found->second);
			}
		} catch (const nlohmann::json::exception& exception) {
			error = std::string(data.name) + "'s icon JSON: " + exception.what();
			return std::nullopt;
		}
		std::sort(atlas.glyphs.begin(), atlas.glyphs.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		return atlas;
	}

	const IconAtlas::Glyph* IconAtlas::find(std::string_view name) const {
		const auto found =
			std::lower_bound(glyphs.begin(), glyphs.end(), name, [](const std::pair<std::string, Glyph>& entry, std::string_view key) {
				return entry.first < key;
			});
		return found != glyphs.end() && found->first == name ? &found->second : nullptr;
	}

	std::optional<Renderer::FontRenderer::GlyphQuad>
	IconAtlas::quad(std::string_view name, glm::vec2 topLeft, float size, glm::vec4 color, Renderer::TextureHandle atlas) const {
		const Glyph* glyph = find(name);
		if (glyph == nullptr) {
			return std::nullopt;
		}
		const float pad = glyph->padding * size;
		return Renderer::FontRenderer::GlyphQuad{
			.position = topLeft - glm::vec2(pad),
			.size = glm::vec2(size + 2.0F * pad),
			.uvMin = glyph->uvMin,
			.uvMax = glyph->uvMax,
			.color = color,
			.runOrigin = topLeft,
			.atlas = atlas,
			.pixelRange = range,
		};
	}

} // namespace ui
