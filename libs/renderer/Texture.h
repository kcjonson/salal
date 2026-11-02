#pragma once

// What a texture in the renderer's table is made of: the formats it stores, the
// description Primitives::createTexture takes, and the Texture that owns one. No
// GL here; the GL side maps a TextureFormat to its internal format in
// backend/gl/GLBackend.cpp.

#include "Handle.h"
#include <cstdint>

namespace Renderer { // NOLINT(readability-identifier-naming)

	class Texture;
	struct TextureDesc;

	namespace Primitives { // NOLINT(readability-identifier-naming)
		Texture createTexture(const TextureDesc& desc);
	}

	enum class TextureFormat : uint8_t { Rgba8, Rgb8, Rgba16F, Rgba32F };

	// `pixels`, when given, is one tightly packed width x height image in `format`
	// (8-bit channels for Rgba8 and Rgb8, half floats for Rgba16F, floats for
	// Rgba32F), its first row at texture row 0. `linear` picks linear filtering,
	// nearest otherwise; edges always clamp.
	struct TextureDesc {
		int			  width = 0;
		int			  height = 0;
		TextureFormat format = TextureFormat::Rgba8;
		bool		  linear = true;
		const void*	  pixels = nullptr;
	};

	// A texture in the renderer's table, from Primitives::createTexture, freed when
	// its owner goes. Move-only, as InstancedMeshHandle is. Draws carry the copyable
	// handle(), which is stale once the texture is freed: a draw sampling a stale
	// handle draws nothing (warned once). A texture freed while draws already batched
	// sample it lives until the batch flushes. After Primitives::shutdown() there is
	// nothing to free.
	class Texture {
	  public:
		Texture() = default;
		~Texture();

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		Texture(Texture&& other) noexcept;
		Texture& operator=(Texture&& other) noexcept;

		[[nodiscard]] TextureHandle handle() const { return texture; }
		[[nodiscard]] bool			isValid() const { return texture.isValid(); }

	  private:
		friend Texture Primitives::createTexture(const TextureDesc& desc);

		explicit Texture(TextureHandle made)
			: texture(made) {}

		TextureHandle texture;
	};

} // namespace Renderer
