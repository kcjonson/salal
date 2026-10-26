#pragma once

// The renderer's texture table: every GL texture the renderer creates lives here,
// owned, keyed by TextureHandle. Consumers see the handle; the GL side of the
// renderer, and a game pass that binds a renderer texture to its own sampler,
// read the GL name here. A destroyed slot's generation advances, so a stale
// handle names nothing.
//
// This header is the opt-in GL path and is excluded from the layering check that
// keeps <GL/glew.h> off the renderer's public headers.

#include "Handle.h"
#include "Texture.h"
#include <GL/glew.h>

namespace Renderer::backend::gl {

	// A new 2D texture in the table, set up through GLTexture and left unbound;
	// invalid, with an error logged, for an empty size or a full table.
	TextureHandle createTexture(const TextureDesc& desc);

	// Replace the rectangle with `pixels` in the texture's format; nothing for a stale
	// handle, and a rectangle outside the texture is refused with an error logged.
	void updateTexture(TextureHandle handle, int x, int y, int width, int height, const void* pixels);

	// Delete the GL texture and free the slot; nothing for a stale or invalid handle.
	void destroyTexture(TextureHandle handle);

	// 0, GL's "no texture", for an invalid or stale handle.
	GLuint name(TextureHandle handle);

} // namespace Renderer::backend::gl
