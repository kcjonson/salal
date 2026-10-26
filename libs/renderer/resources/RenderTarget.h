#pragma once

// An offscreen colour target: a texture from the renderer's table behind a
// framebuffer. begin() makes it the bound target and end() rebinds the one
// below it on the renderer's own target stack (the backbuffer at the bottom),
// so the GL binding is never queried. The texture goes with the target.

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

		// Rebind the target below (the backbuffer at the bottom) and its viewport.
		void end();

		[[nodiscard]] TextureHandle texture() const;
		[[nodiscard]] int			width() const;
		[[nodiscard]] int			height() const;

		// Read the colour attachment as RGBA8 into `out`, width() * height() * 4
		// bytes, bottom row first as GL reads it. Synchronous by design: it waits
		// for the GPU. Works whether or not the target is begun.
		void readPixels(std::span<uint8_t> out) const;

	  private:
		struct Impl;
		std::unique_ptr<Impl> impl;
	};

} // namespace Renderer
