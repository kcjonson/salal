#pragma once

#include "Handle.h"
#include <cstdint>
#include <memory>

namespace Renderer {

	// Simple FBO wrapper for render-to-texture use cases (tile atlas baking, tests).
	// Uses RAII for automatic GPU resource cleanup; the GL objects live behind Impl.
	class RenderToTexture {
	  public:
		RenderToTexture(int width, int height);
		~RenderToTexture();

		RenderToTexture(const RenderToTexture&) = delete;
		RenderToTexture& operator=(const RenderToTexture&) = delete;

		RenderToTexture(RenderToTexture&&) noexcept;
		RenderToTexture& operator=(RenderToTexture&&) noexcept;

		// Bind FBO and set viewport to texture dimensions. Saves previous FBO/viewport.
		void begin();

		// Restore previous FBO/viewport bindings.
		void end();

		// Get the color texture (2D, RGBA8).
		[[nodiscard]] TextureHandle texture() const;
		[[nodiscard]] int			width() const;
		[[nodiscard]] int			height() const;

	  private:
		struct Impl;
		std::unique_ptr<Impl> impl;
	};

} // namespace Renderer
