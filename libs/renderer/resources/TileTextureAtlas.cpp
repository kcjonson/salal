#include "resources/TileTextureAtlas.h"

#include "backend/gl/GLTextures.h"

#include <algorithm>
#include <utility>

namespace Renderer {

	TileTextureAtlas::TileTextureAtlas(int atlasSize)
		: handle_(backend::gl::createTexture({.width = atlasSize, .height = atlasSize})),
		  size_(handle_.isValid() ? atlasSize : 0) {}

	TileTextureAtlas::~TileTextureAtlas() {
		release();
	}

	TileTextureAtlas::TileTextureAtlas(TileTextureAtlas&& other) noexcept
		: handle_(std::exchange(other.handle_, TextureHandle{})),
		  size_(std::exchange(other.size_, 0)),
		  cursorX_(std::exchange(other.cursorX_, 0)),
		  cursorY_(std::exchange(other.cursorY_, 0)),
		  currentRowHeight_(std::exchange(other.currentRowHeight_, 0)) {}

	TileTextureAtlas& TileTextureAtlas::operator=(TileTextureAtlas&& other) noexcept {
		if (this != &other) {
			release();
			handle_ = std::exchange(other.handle_, TextureHandle{});
			size_ = std::exchange(other.size_, 0);
			cursorX_ = std::exchange(other.cursorX_, 0);
			cursorY_ = std::exchange(other.cursorY_, 0);
			currentRowHeight_ = std::exchange(other.currentRowHeight_, 0);
		}
		return *this;
	}

	void TileTextureAtlas::release() {
		backend::gl::destroyTexture(handle_);
		handle_ = {};
		size_ = 0;
	}

	AtlasRegion TileTextureAtlas::allocate(int width, int height) {
		int atlasSize = size();
		if (width <= 0 || height <= 0 || width > atlasSize || height > atlasSize) {
			return {};
		}

		if (cursorX_ + width > atlasSize) {
			cursorX_ = 0;
			cursorY_ += currentRowHeight_;
			currentRowHeight_ = 0;
		}

		if (cursorY_ + height > atlasSize) {
			return {};
		}

		AtlasRegion region{cursorX_, cursorY_, width, height, true};
		cursorX_ += width;
		currentRowHeight_ = std::max(currentRowHeight_, height);
		return region;
	}

	bool TileTextureAtlas::upload(const AtlasRegion& region, const uint8_t* rgbaData) {
		if (!region.valid || !handle_.isValid() || rgbaData == nullptr) {
			return false;
		}
		backend::gl::updateTexture(handle_, region.x, region.y, region.width, region.height, rgbaData);
		return true;
	}

} // namespace Renderer
