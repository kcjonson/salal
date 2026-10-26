#include "backend/gl/GLTargets.h"

#include "utils/Log.h"

#include <vector>

namespace Renderer::backend::gl {

	namespace {
		// Leaked for the same reason as the texture table: a RenderTarget held in a
		// static can end itself after a static vector here is gone.
		std::vector<BoundTarget>& stack() {
			static auto& s = *new std::vector<BoundTarget>{BoundTarget{}};
			return s;
		}

		void bind(const BoundTarget& target) {
			glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
			glViewport(0, 0, target.width, target.height);
		}
	} // namespace

	void setBackbufferSize(int width, int height) {
		stack().front().width = width;
		stack().front().height = height;
	}

	void pushTarget(GLuint framebuffer, int width, int height) {
		stack().push_back({framebuffer, width, height});
		bind(stack().back());
	}

	void popTarget(GLuint framebuffer) {
		if (stack().size() < 2 || stack().back().framebuffer != framebuffer) {
			LOG_ERROR(Renderer, "popTarget: framebuffer %u is not the bound render target", framebuffer);
			return;
		}
		stack().pop_back();
		bind(stack().back());
	}

	const BoundTarget& currentTarget() {
		return stack().back();
	}

	const BoundTarget& backbuffer() {
		return stack().front();
	}

} // namespace Renderer::backend::gl
