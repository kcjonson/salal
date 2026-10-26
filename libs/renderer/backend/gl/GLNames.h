#pragma once

// The handle-to-GL-name registry. Consumers see TextureHandle; the GL side of the
// renderer, and a game pass that binds a renderer texture to its own sampler, read
// the GL name here. A registered handle names a texture it does not own: whoever
// created the texture deletes it and unregisters the handle.
//
// This header is the opt-in GL path and is excluded from the layering check that
// keeps <GL/glew.h> off the renderer's public headers.

#include "Handle.h"
#include <GL/glew.h>

namespace Renderer::backend::gl {

	TextureHandle registerTexture(GLuint name);
	void		  unregisterTexture(TextureHandle handle);

	// 0, GL's "no texture", for an invalid or stale handle.
	GLuint name(TextureHandle handle);

} // namespace Renderer::backend::gl
