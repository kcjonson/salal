#include "resources/RenderTarget.h"

#include "backend/gl/GLFramebuffer.h"
#include "backend/gl/GLTargets.h"
#include "backend/gl/GLTextures.h"
#include "utils/Log.h"

#include <GL/glew.h>

namespace Renderer {

	struct RenderTarget::Impl {
		GLFramebuffer fbo;
		TextureHandle texture;
		int			  width = 0;
		int			  height = 0;
		bool		  inUse = false;

		Impl() = default;

		~Impl() {
			if (inUse) {
				backend::gl::popTarget(fbo);
			}
			backend::gl::destroyTexture(texture);
		}

		Impl(const Impl&) = delete;
		Impl& operator=(const Impl&) = delete;
		Impl(Impl&&) = delete;
		Impl& operator=(Impl&&) = delete;
	};

	RenderTarget::RenderTarget() = default;
	RenderTarget::~RenderTarget() = default;
	RenderTarget::RenderTarget(RenderTarget&&) noexcept = default;
	RenderTarget& RenderTarget::operator=(RenderTarget&&) noexcept = default;

	RenderTarget RenderTarget::create(int width, int height, TextureFormat format) {
		RenderTarget target;
		if (width <= 0 || height <= 0) {
			LOG_ERROR(Renderer, "RenderTarget: empty size %dx%d", width, height);
			return target;
		}

		auto impl = std::make_unique<Impl>();
		impl->width = width;
		impl->height = height;
		impl->texture = backend::gl::createTexture({.width = width, .height = height, .format = format});
		if (!impl->texture.isValid()) {
			return target;
		}

		impl->fbo = GLFramebuffer::create();
		impl->fbo.bind();
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, backend::gl::name(impl->texture), 0);
		const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		glBindFramebuffer(GL_FRAMEBUFFER, backend::gl::currentTarget().framebuffer);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			LOG_ERROR(Renderer, "RenderTarget %dx%d: framebuffer incomplete (0x%x)", width, height, status);
			return target;
		}

		target.impl = std::move(impl);
		return target;
	}

	void RenderTarget::begin() {
		if (!impl || impl->inUse) {
			return;
		}
		backend::gl::pushTarget(impl->fbo, impl->width, impl->height);
		impl->inUse = true;
	}

	void RenderTarget::end() {
		if (!impl || !impl->inUse) {
			return;
		}
		if (backend::gl::popTarget(impl->fbo)) {
			impl->inUse = false;
		}
	}

	TextureHandle RenderTarget::texture() const {
		return impl ? impl->texture : TextureHandle{};
	}

	int RenderTarget::width() const {
		return impl ? impl->width : 0;
	}

	int RenderTarget::height() const {
		return impl ? impl->height : 0;
	}

	void RenderTarget::readPixels(std::span<uint8_t> out) const {
		if (!impl) {
			return;
		}
		const size_t needed = static_cast<size_t>(impl->width) * static_cast<size_t>(impl->height) * 4;
		if (out.size() < needed) {
			LOG_ERROR(Renderer, "RenderTarget::readPixels: %zu bytes given, %zu needed", out.size(), needed);
			return;
		}
		glBindFramebuffer(GL_READ_FRAMEBUFFER, impl->fbo);
		glReadPixels(0, 0, impl->width, impl->height, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
		glBindFramebuffer(GL_READ_FRAMEBUFFER, backend::gl::currentTarget().framebuffer);
	}

} // namespace Renderer
