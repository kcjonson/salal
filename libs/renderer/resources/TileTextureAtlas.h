#pragma once

#include "Handle.h"
#include "Texture.h"
#include <cstdint>

namespace Renderer {

	struct AtlasRegion {
		int	 x = 0;
		int	 y = 0;
		int	 width = 0;
		int	 height = 0;
		bool valid = false;
	};

	// Simple shelf-packed 2D RGBA8 texture atlas for tile patterns. The texture
	// lives in the renderer's table and goes with the atlas. Needs Primitives::init()
	// first; before it the atlas has no texture and size 0.
	class TileTextureAtlas {
	  public:
		explicit TileTextureAtlas(int atlasSize = 4096);
		~TileTextureAtlas() = default;

		TileTextureAtlas(const TileTextureAtlas&) = delete;
		TileTextureAtlas& operator=(const TileTextureAtlas&) = delete;

		TileTextureAtlas(TileTextureAtlas&& other) noexcept;
		TileTextureAtlas& operator=(TileTextureAtlas&& other) noexcept;

		// Reserve a region of the atlas; returns {valid=false} if it does not fit.
		AtlasRegion allocate(int width, int height);

		// Upload RGBA8 data into a reserved region.
		// Returns true if upload succeeded, false if parameters are invalid.
		bool upload(const AtlasRegion& region, const uint8_t* rgbaData);

		[[nodiscard]] TextureHandle texture() const { return texture_.handle(); }
		[[nodiscard]] int			size() const { return size_; }

	  private:
		Texture texture_;
		int		size_ = 0;
		int		cursorX_ = 0;
		int		cursorY_ = 0;
		int		currentRowHeight_ = 0;
	};

} // namespace Renderer
