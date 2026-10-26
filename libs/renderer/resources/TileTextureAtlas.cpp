#include "resources/TileTextureAtlas.h"

#include "backend/gl/GLNames.h"
#include "backend/gl/GLTexture.h"

#include <GL/glew.h>
#include <algorithm>
#include <utility>

namespace Renderer {

	TileTextureAtlas::TileTextureAtlas(int atlasSize)
		// Create square texture using RAII wrapper (sets up filtering and wrapping)
		: texture_(std::make_unique<GLTexture>(atlasSize, atlasSize, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr)) {
		// Unbind texture after GLTexture constructor (it leaves it bound)
		GLTexture::unbind();
		handle_ = backend::gl::registerTexture(*texture_);
	}

	TileTextureAtlas::~TileTextureAtlas() {
		release();
	}

	TileTextureAtlas::TileTextureAtlas(TileTextureAtlas&& other) noexcept
		: texture_(std::move(other.texture_)),
		  handle_(std::exchange(other.handle_, TextureHandle{})),
		  cursorX_(std::exchange(other.cursorX_, 0)),
		  cursorY_(std::exchange(other.cursorY_, 0)),
		  currentRowHeight_(std::exchange(other.currentRowHeight_, 0)) {}

	TileTextureAtlas& TileTextureAtlas::operator=(TileTextureAtlas&& other) noexcept {
		if (this != &other) {
			release();
			texture_ = std::move(other.texture_);
			handle_ = std::exchange(other.handle_, TextureHandle{});
			cursorX_ = std::exchange(other.cursorX_, 0);
			cursorY_ = std::exchange(other.cursorY_, 0);
			currentRowHeight_ = std::exchange(other.currentRowHeight_, 0);
		}
		return *this;
	}

	void TileTextureAtlas::release() {
		backend::gl::unregisterTexture(handle_);
		handle_ = {};
		texture_.reset();
	}

	int TileTextureAtlas::size() const {
		return texture_ ? texture_->width() : 0;
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
		if (!region.valid || !texture_ || !texture_->isValid() || rgbaData == nullptr) {
			return false;
		}
		texture_->bind();
		glTexSubImage2D(GL_TEXTURE_2D, 0, region.x, region.y, region.width, region.height, GL_RGBA, GL_UNSIGNED_BYTE, rgbaData);
		GLTexture::unbind();
		return true;
	}

} // namespace Renderer
