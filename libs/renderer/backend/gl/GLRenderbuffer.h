#pragma once

// GLRenderbuffer - RAII wrapper for an OpenGL renderbuffer, a render target's mask.

#include <GL/glew.h>

namespace Renderer {

	/// Movable but not copyable - only one owner of the GPU resource.
	class GLRenderbuffer {
	  public:
		GLRenderbuffer() = default;

		/// A width x height renderbuffer in `internalFormat`, left unbound.
		static GLRenderbuffer create(GLenum internalFormat, int width, int height) {
			GLRenderbuffer buffer;
			glGenRenderbuffers(1, &buffer.rboHandle);
			glBindRenderbuffer(GL_RENDERBUFFER, buffer.rboHandle);
			glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, width, height);
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
			return buffer;
		}

		~GLRenderbuffer() { release(); }

		GLRenderbuffer(const GLRenderbuffer&) = delete;
		GLRenderbuffer& operator=(const GLRenderbuffer&) = delete;

		GLRenderbuffer(GLRenderbuffer&& other) noexcept
			: rboHandle(other.rboHandle) {
			other.rboHandle = 0;
		}

		GLRenderbuffer& operator=(GLRenderbuffer&& other) noexcept {
			if (this != &other) {
				release();
				rboHandle = other.rboHandle;
				other.rboHandle = 0;
			}
			return *this;
		}

		[[nodiscard]] GLuint handle() const { return rboHandle; }
		[[nodiscard]] bool	 isValid() const { return rboHandle != 0; }

		void release() {
			if (rboHandle != 0) {
				glDeleteRenderbuffers(1, &rboHandle);
				rboHandle = 0;
			}
		}

	  private:
		GLuint rboHandle = 0;
	};

} // namespace Renderer
