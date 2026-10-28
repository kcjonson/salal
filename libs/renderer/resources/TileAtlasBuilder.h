#pragma once

#include "resources/TileTextureAtlas.h"

#include <glm/vec4.hpp>
#include <graphics/Color.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Renderer {

	// Builds a tile texture atlas by loading SVG patterns or generating fallbacks.
	//
	// Design: Uses callbacks for surface info to avoid renderer depending on engine.
	// The caller provides each surface's pattern path and color.
	class TileAtlasBuilder {
	  public:
		struct Config {
			int patternSize = 512; // Size of each pattern (square)
			int atlasSize = 2048;  // Total atlas texture size
		};

		// Default configuration values
		static constexpr Config kDefaultConfig{512, 2048};

		// Callbacks for surface information, which the game provides. The pattern is the resource path
		// of the surface's SVG pattern, found through Foundation::findResource; empty for none. A
		// non-empty path that isn't found is a warning, and the surface gets the checker.
		using SurfacePatternFn = std::function<std::string(int surfaceId)>;
		using SurfaceColorFn = std::function<Foundation::Color(int surfaceId)>;

		explicit TileAtlasBuilder(const Config& config = kDefaultConfig);
		~TileAtlasBuilder() = default;

		TileAtlasBuilder(const TileAtlasBuilder&) = delete;
		TileAtlasBuilder& operator=(const TileAtlasBuilder&) = delete;
		TileAtlasBuilder(TileAtlasBuilder&&) = default;
		TileAtlasBuilder& operator=(TileAtlasBuilder&&) = default;

		// Build atlas for all surface types, returns UV rects for each surface.
		// UV rects are stored as (u0, v0, u1, v1) where u0,v0 is top-left and u1,v1 is bottom-right.
		// @param surfaceCount Number of surface types to allocate
		// @param surfacePatternFn Callback to get the resource path of the surface's SVG pattern
		// @param surfaceColorFn Callback to get fallback color for surface, used when there is no pattern
		[[nodiscard]] std::vector<glm::vec4>
		buildForSurfaces(int surfaceCount, const SurfacePatternFn& surfacePatternFn, const SurfaceColorFn& surfaceColorFn);

		// Get the underlying atlas texture; invalid before buildForSurfaces().
		[[nodiscard]] TextureHandle texture() const;

		// Get the atlas size.
		[[nodiscard]] int atlasSize() const { return config_.atlasSize; }

	  private:
		// Bake the SVG pattern at the resource path, returns true if successful.
		bool loadSvgPattern(const std::string& patternPath, std::vector<uint8_t>& pixels);

		// Generate a checkerboard fallback pattern for the given surface color.
		void generateFallbackPattern(const Foundation::Color& color, std::vector<uint8_t>& pixels, int width, int height);

		// Calculate UV rectangle for an allocated atlas region.
		[[nodiscard]] glm::vec4 calculateUVRect(const AtlasRegion& region) const;

		Config							  config_;
		std::unique_ptr<TileTextureAtlas> atlas_;
	};

} // namespace Renderer
