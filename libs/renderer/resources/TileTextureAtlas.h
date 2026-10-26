#pragma once

#include "Handle.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace Renderer {

	class GLTexture;

	struct AtlasRegion {
		int	 x = 0;
		int	 y = 0;
		int	 width = 0;
		int	 height = 0;
		bool valid = false;
	};

	// Simple shelf-packed 2D texture atlas for tile patterns.
	// Uses RAII for automatic GPU resource cleanup.
	class TileTextureAtlas {
	  public:
		explicit TileTextureAtlas(int atlasSize = 4096);
		~TileTextureAtlas();

		TileTextureAtlas(const TileTextureAtlas&) = delete;
		TileTextureAtlas& operator=(const TileTextureAtlas&) = delete;

		TileTextureAtlas(TileTextureAtlas&& other) noexcept;
		TileTextureAtlas& operator=(TileTextureAtlas&& other) noexcept;

		// Reserve a region of the atlas; returns {valid=false} if it does not fit.
		AtlasRegion allocate(int width, int height);

		// Upload RGBA8 data into a reserved region.
		// Returns true if upload succeeded, false if parameters are invalid.
		bool upload(const AtlasRegion& region, const uint8_t* rgbaData);

		[[nodiscard]] TextureHandle texture() const { return handle_; }
		[[nodiscard]] int			size() const;

	  private:
		void release();

		std::unique_ptr<GLTexture> texture_; // RAII texture wrapper
		TextureHandle			   handle_;
		int						   cursorX_ = 0;
		int						   cursorY_ = 0;
		int						   currentRowHeight_ = 0;
	};

} // namespace Renderer
