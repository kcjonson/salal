#pragma once

// The icon service (docs/ui-design.md, "Icons"), one in each scene's ui context: the icon atlases
// salal_icons() built, each uploaded on first use and owned here, and the one glyph quad an icon by
// name draws as. Icons go through drawGlyphs beside text; FontRenderer's families are untouched.

#include "ui/icons/IconAtlas.h"

#include <Texture.h>

#include <set>
#include <string>
#include <vector>

namespace ui {

	class IconService {
	  public:
		// Adds a game's atlas (my_game::iconAtlas()); its names hide the same names in atlases added
		// before it. False, logged, if it doesn't load.
		bool add(const IconAtlasData& data);

		[[nodiscard]] bool has(std::string_view name) const;

		// `name` drawn into the square at `topLeft`, `size` px a side, in `color`. Null, warned once a
		// name, if no atlas has it, or if its atlas can't be uploaded.
		std::optional<Renderer::FontRenderer::GlyphQuad> quad(std::string_view name, glm::vec2 topLeft, float size, glm::vec4 color);

	  private:
		struct Atlas {
			IconAtlas		  icons;
			Renderer::Texture texture; // made on the first draw, so a headless scene uploads nothing
		};

		Atlas* atlasFor(std::string_view name);

		std::vector<Atlas>				   atlases;
		std::set<std::string, std::less<>> warned;
	};

} // namespace ui
