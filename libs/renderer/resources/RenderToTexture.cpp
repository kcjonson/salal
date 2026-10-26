#include "resources/RenderToTexture.h"

#include "backend/gl/GLFramebuffer.h"
#include "backend/gl/GLNames.h"
#include "backend/gl/GLTexture.h"

#include <GL/glew.h>
#include <stdexcept>

namespace Renderer {

	struct RenderToTexture::Impl {
		GLFramebuffer fbo;	   // RAII framebuffer wrapper
		GLTexture	  texture; // RAII texture wrapper
		TextureHandle handle;
		GLint		  prevViewport[4] = {0, 0, 0, 0};
		GLint		  prevFbo = 0;
		bool		  inUse = false;

		Impl(int width, int height)
			// Create texture using RAII wrapper (sets up filtering and wrapping)
			: fbo(GLFramebuffer::create()),
			  texture(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr) {
			// Unbind texture after GLTexture constructor (it leaves it bound)
			GLTexture::unbind();

			// Attach texture to framebuffer
			fbo.bind();
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

			GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			GLFramebuffer::unbind();
			if (status != GL_FRAMEBUFFER_COMPLETE) {
				// RAII wrappers will clean up automatically when exception is thrown
				throw std::runtime_error("RenderToTexture FBO incomplete");
			}
			handle = backend::gl::registerTexture(texture);
		}

		~Impl() { backend::gl::unregisterTexture(handle); }

		Impl(const Impl&) = delete;
		Impl& operator=(const Impl&) = delete;
		Impl(Impl&&) = delete;
		Impl& operator=(Impl&&) = delete;
	};

	RenderToTexture::RenderToTexture(int width, int height)
		: impl(std::make_unique<Impl>(width, height)) {}

	RenderToTexture::~RenderToTexture() = default;
	RenderToTexture::RenderToTexture(RenderToTexture&&) noexcept = default;
	RenderToTexture& RenderToTexture::operator=(RenderToTexture&&) noexcept = default;

	void RenderToTexture::begin() {
		if (!impl || impl->inUse) {
			return;
		}
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &impl->prevFbo);
		glGetIntegerv(GL_VIEWPORT, impl->prevViewport);
		impl->fbo.bind();
		glViewport(0, 0, impl->texture.width(), impl->texture.height());
		impl->inUse = true;
	}

	void RenderToTexture::end() {
		if (!impl || !impl->inUse) {
			return;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(impl->prevFbo));
		glViewport(impl->prevViewport[0], impl->prevViewport[1], impl->prevViewport[2], impl->prevViewport[3]);
		impl->inUse = false;
	}

	TextureHandle RenderToTexture::texture() const {
		return impl ? impl->handle : TextureHandle{};
	}

	int RenderToTexture::width() const {
		return impl ? impl->texture.width() : 0;
	}

	int RenderToTexture::height() const {
		return impl ? impl->texture.height() : 0;
	}

} // namespace Renderer
