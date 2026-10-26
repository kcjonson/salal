#include "resources/RenderTarget.h"

#include "backend/Backend.h"
#include "utils/Log.h"

#include <vector>

namespace Renderer {

	namespace {
		// The renderer's stack of begun targets, the window ({}) at the bottom. Leaked
		// on purpose: a RenderTarget held in a static can end itself after a static
		// vector here is gone.
		std::vector<RenderTargetHandle>& stack() {
			static auto& s = *new std::vector<RenderTargetHandle>{RenderTargetHandle{}};
			return s;
		}

		// Pop `target`, which must be the top, and bind the one below. A mismatch (an
		// inner target still open) leaves the stack alone, logs once, and returns false.
		bool pop(RenderTargetHandle target) {
			if (stack().size() < 2 || stack().back() != target) {
				// A caller bug (end() out of nesting order); once, or it would repeat every frame.
				static bool warned = false;
				if (!warned) {
					warned = true;
					LOG_ERROR(Renderer, "RenderTarget::end: target %u is not the bound render target; the end is refused", target.value);
				}
				return false;
			}
			stack().pop_back();
			if (backend::Backend* gpu = backend::active(); gpu != nullptr) {
				gpu->setRenderTarget(stack().back());
			}
			return true;
		}
	} // namespace

	struct RenderTarget::Impl {
		RenderTargetHandle target;
		TextureHandle	   texture;
		int				   width = 0;
		int				   height = 0;
		bool			   inUse = false;

		Impl() = default;

		~Impl() {
			if (inUse) {
				pop(target);
			}
			if (backend::Backend* gpu = backend::active(); gpu != nullptr) {
				gpu->destroyRenderTarget(target);
				gpu->destroyTexture(texture);
			}
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
		backend::Backend* gpu = backend::active();
		if (gpu == nullptr) {
			LOG_ERROR(Renderer, "RenderTarget: the renderer is not initialized");
			return target;
		}

		auto impl = std::make_unique<Impl>();
		impl->width = width;
		impl->height = height;
		impl->texture = gpu->createTexture({.width = width, .height = height, .format = format});
		if (!impl->texture.isValid()) {
			return target;
		}
		impl->target = gpu->createRenderTarget(impl->texture);
		if (!impl->target.isValid()) {
			return target; // the Impl's destructor destroys the texture
		}

		target.impl = std::move(impl);
		return target;
	}

	void RenderTarget::begin() {
		if (!impl || impl->inUse) {
			return;
		}
		stack().push_back(impl->target);
		if (backend::Backend* gpu = backend::active(); gpu != nullptr) {
			gpu->setRenderTarget(impl->target);
		}
		impl->inUse = true;
	}

	void RenderTarget::end() {
		if (!impl || !impl->inUse) {
			return;
		}
		if (pop(impl->target)) {
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
		backend::Backend* gpu = backend::active();
		if (!impl || gpu == nullptr) {
			return;
		}
		const size_t needed = static_cast<size_t>(impl->width) * static_cast<size_t>(impl->height) * 4;
		if (out.size() < needed) {
			LOG_ERROR(Renderer, "RenderTarget::readPixels: %zu bytes given, %zu needed", out.size(), needed);
			return;
		}
		gpu->readPixels(impl->target, out);
	}

	bool RenderTarget::anyBegun() {
		return stack().size() > 1;
	}

} // namespace Renderer
