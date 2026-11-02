#pragma once

// The CPU half of the icon service (docs/ui-design.md, "Icons"): an atlas salal_icons() built, its
// pixels decoded for upload, its glyphs found by name, and the one tinted text-mode quad an icon
// draws as. The service owns the texture it uploads from pixels() and passes its handle to quad(),
// so icons go through drawGlyphs beside text and FontRenderer's families are untouched.

#include "ui/icons/IconAtlasData.h"

#include <font/FontRenderer.h>

#include <optional>
#include <string>
#include <vector>

namespace ui {

	class IconAtlas {
	  public:
		// One icon's cell, which spans the icon's square and the field's padding around it.
		struct Glyph {
			glm::vec2 uvMin;	   // bottom-left, as the renderer's text atlases have it
			glm::vec2 uvMax;	   // top-right
			float	  padding = 0; // how far the cell reaches past the icon's square on each side, in icon sizes
		};

		// Null with `error` set if the PNG or the JSON doesn't decode, or a name has no glyph.
		static std::optional<IconAtlas> load(const IconAtlasData& data, std::string& error);

		const Glyph* find(std::string_view name) const;

		// `name` drawn into the square at `topLeft`, `size` px a side; null if there's no such icon.
		std::optional<Renderer::FontRenderer::GlyphQuad>
		quad(std::string_view name, glm::vec2 topLeft, float size, glm::vec4 color, Renderer::TextureHandle atlas) const;

		// RGB, the bottom row first, as FontRenderer uploads its atlases.
		const std::vector<unsigned char>& pixels() const { return rgb; }
		int								  width() const { return atlasWidth; }
		int								  height() const { return atlasHeight; }
		float							  distanceRange() const { return range; }

	  private:
		std::vector<std::pair<std::string, Glyph>> glyphs; // sorted by name
		std::vector<unsigned char>				   rgb;
		int										   atlasWidth = 0;
		int										   atlasHeight = 0;
		float									   range = 0.0F;
	};

} // namespace ui
