#include "backend/gl/GLTextures.h"

#include "backend/gl/GLTexture.h"
#include "utils/Log.h"

#include <vector>

namespace Renderer::backend::gl {

	namespace {
		struct Slot {
			GLTexture	  texture;
			TextureFormat format = TextureFormat::Rgba8;
			uint16_t	  generation = 0;
			bool		  live = false;
		};

		// Indices 0..0xFFFE: an index of 0xFFFF with generation 0xFFFF would spell the invalid handle.
		constexpr size_t kMaxSlots = 0xFFFF;

		// Leaked on purpose: consumers hold FontRenderer and TileAtlasBuilder in namespace-scope
		// statics whose destructors destroy their textures, which can run after a static vector here is gone.
		std::vector<Slot>& slots() {
			static auto& s = *new std::vector<Slot>;
			return s;
		}

		std::vector<uint16_t>& freeSlots() {
			static auto& s = *new std::vector<uint16_t>;
			return s;
		}

		Slot* slotFor(TextureHandle handle) {
			if (!handle.isValid() || handle.index() >= slots().size()) {
				return nullptr;
			}
			Slot& slot = slots()[handle.index()];
			return slot.live && slot.generation == handle.generation() ? &slot : nullptr;
		}

		struct GLFormat {
			GLenum internal;
			GLenum format;
			GLenum type;
		};

		GLFormat glFormat(TextureFormat format) {
			switch (format) {
				case TextureFormat::Rgb8:
					return {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE};
				case TextureFormat::Rgba16F:
					return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
				case TextureFormat::Rgba32F:
					return {GL_RGBA32F, GL_RGBA, GL_FLOAT};
				case TextureFormat::Rgba8:
				default:
					return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
			}
		}

		// Three-byte pixels break GL's default four-byte row alignment at widths
		// that are not a multiple of four; the other formats are always aligned.
		void setUnpackAlignment(TextureFormat format) {
			glPixelStorei(GL_UNPACK_ALIGNMENT, format == TextureFormat::Rgb8 ? 1 : 4);
		}
	} // namespace

	TextureHandle createTexture(const TextureDesc& desc) {
		if (desc.width <= 0 || desc.height <= 0) {
			LOG_ERROR(Renderer, "createTexture: empty size %dx%d", desc.width, desc.height);
			return {};
		}
		uint16_t index = 0;
		if (!freeSlots().empty()) {
			index = freeSlots().back();
			freeSlots().pop_back();
		} else {
			if (slots().size() >= kMaxSlots) {
				LOG_ERROR(Renderer, "Texture table full (%zu live textures); handle not issued", slots().size());
				return {};
			}
			index = static_cast<uint16_t>(slots().size());
			slots().emplace_back();
		}
		Slot&		   slot = slots()[index];
		const GLFormat gl = glFormat(desc.format);
		setUnpackAlignment(desc.format);
		slot.texture = GLTexture(desc.width, desc.height, gl.internal, gl.format, gl.type, desc.pixels);
		setUnpackAlignment(TextureFormat::Rgba8);
		if (!desc.linear) { // GLTexture's constructor set linear and left the texture bound
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}
		GLTexture::unbind();
		slot.format = desc.format;
		slot.live = true;
		return TextureHandle::make(index, slot.generation);
	}

	void updateTexture(TextureHandle handle, int x, int y, int width, int height, const void* pixels) {
		Slot* slot = slotFor(handle);
		if (slot == nullptr || pixels == nullptr) {
			return;
		}
		const GLFormat gl = glFormat(slot->format);
		slot->texture.bind();
		setUnpackAlignment(slot->format);
		glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, gl.format, gl.type, pixels);
		setUnpackAlignment(TextureFormat::Rgba8);
		GLTexture::unbind();
	}

	void destroyTexture(TextureHandle handle) {
		Slot* slot = slotFor(handle);
		if (slot == nullptr) {
			return;
		}
		slot->texture.release();
		slot->live = false;
		slot->generation = static_cast<uint16_t>(slot->generation + 1); // wraps at 65536 reuses of one slot
		freeSlots().push_back(handle.index());
	}

	GLuint name(TextureHandle handle) {
		const Slot* slot = slotFor(handle);
		if (slot == nullptr && handle.isValid()) {
			// A stale or foreign handle samples texture 0; say so once rather than render silently wrong.
			static bool warned = false;
			if (!warned) {
				warned = true;
				LOG_WARNING(
					Renderer,
					"Texture handle %u (slot %u, generation %u) is not in the table; using texture 0",
					handle.value,
					handle.index(),
					handle.generation()
				);
			}
		}
		return slot != nullptr ? slot->texture.handle() : 0;
	}

} // namespace Renderer::backend::gl
