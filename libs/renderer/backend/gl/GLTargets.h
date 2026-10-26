#pragma once

// The renderer's stack of bound render targets. The bottom entry is the window's
// backbuffer, framebuffer 0 at the size Primitives::setViewport last recorded;
// RenderTarget::begin pushes its framebuffer and end pops it, each binding the
// new top with a full viewport, so nothing here asks GL what is bound.
//
// GL-side only, like the rest of backend/gl.

#include <GL/glew.h>

namespace Renderer::backend::gl {

	struct BoundTarget {
		GLuint framebuffer = 0;
		int	   width = 0;
		int	   height = 0;
	};

	// Record the backbuffer's size. Recorded, not applied: the platform layer
	// owns the window's glViewport.
	void setBackbufferSize(int width, int height);

	// Bind `framebuffer` with a viewport over all of it and make it the current target.
	void pushTarget(GLuint framebuffer, int width, int height);

	// Pop `framebuffer`, which must be the current target, and rebind the one
	// below it with its viewport. A mismatch (an inner target still open) leaves
	// the stack alone, logs once, and returns false.
	bool popTarget(GLuint framebuffer);

	const BoundTarget& currentTarget();
	const BoundTarget& backbuffer();

} // namespace Renderer::backend::gl
