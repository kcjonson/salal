#pragma once

// An offscreen colour target: a texture from the renderer's table and a backend
// render target drawing into it. begin() makes it the bound target and end()
// rebinds the one below it on the renderer's own target stack (the backbuffer at
// the bottom), so the GPU is never asked what is bound. The texture goes with the
// target. Needs Primitives::init(); before it, create() logs and is invalid.

#include "Handle.h"
#include "Texture.h"
#include <cstdint>
#include <memory>
#include <span>

namespace Renderer {

	class RenderTarget {
	  public:
		// An invalid target: begin, end and readPixels do nothing, texture() is invalid.
		RenderTarget();
		~RenderTarget();

		RenderTarget(const RenderTarget&) = delete;
		RenderTarget& operator=(const RenderTarget&) = delete;

		RenderTarget(RenderTarget&&) noexcept;
		RenderTarget& operator=(RenderTarget&&) noexcept;

		// A width x height target with one colour attachment in `format`, its
		// contents undefined until cleared; invalid, with an error logged, if the
		// framebuffer cannot be completed.
		static RenderTarget create(int width, int height, TextureFormat format = TextureFormat::Rgba8);

		[[nodiscard]] bool isValid() const { return impl != nullptr; }

		// Bind as the current target, viewport over the whole texture.
		void begin();

		// Rebind the target below (the backbuffer at the bottom) and its viewport. An
		// end() while an inner target is still open is refused (logged once) and the
		// target stays begun, so ending the inner one and then this one recovers.
		void end();

		[[nodiscard]] TextureHandle texture() const;
		[[nodiscard]] int			width() const;
		[[nodiscard]] int			height() const;

		// Read the colour attachment as RGBA8 into `out`, width() * height() * 4
		// bytes, bottom row first as GL reads it. The colour is premultiplied by its
		// alpha, as everything the renderer draws and clears is; un-premultiply it for
		// a straight-alpha image. Synchronous by design: it waits for the GPU. Works
		// whether or not the target is begun.
		void readPixels(std::span<uint8_t> out) const;

		// Whether any target is begun; Primitives::setViewport() records the window's
		// size only when none is.
		[[nodiscard]] static bool anyBegun();

	  private:
		struct Impl;
		std::unique_ptr<Impl> impl;
	};

} // namespace Renderer
