#pragma once

// What a texture in the renderer's table is made of: the formats it stores and
// the description Primitives::createTexture takes. No GL here; the GL side maps
// a TextureFormat to its internal format in backend/gl/GLTextures.cpp.

#include <cstdint>

namespace Renderer { // NOLINT(readability-identifier-naming)

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

} // namespace Renderer
