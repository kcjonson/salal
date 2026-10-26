#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Renderer { // NOLINT(readability-identifier-naming)

	/**
	 * The window's size in logical pixels and in framebuffer (physical) pixels, as the
	 * platform layer reports it. The app feeds it to CoordinateSystem::setViewport from
	 * its resize callbacks; the renderer never asks the window system itself.
	 */
	struct Viewport {
		int logicalWidth = 0;
		int logicalHeight = 0;
		int framebufferWidth = 0;
		int framebufferHeight = 0;

		/// Physical pixels per logical pixel (e.g. 2.0 on Retina); 1 when unsized.
		[[nodiscard]] float pixelRatio() const {
			return logicalWidth > 0 ? static_cast<float>(framebufferWidth) / static_cast<float>(logicalWidth) : 1.0F;
		}
	};

	/**
	 * CoordinateSystem abstracts high-DPI display complexity and provides coordinate system management.
	 *
	 * Key concepts:
	 * - LOGICAL PIXELS (Window Coordinates): What developers work with. Consistent regardless of DPI.
	 * - PHYSICAL PIXELS (Framebuffer Coordinates): Actual pixels on screen. On 2x Retina display,
	 *   100x100 logical pixels = 200x200 physical pixels.
	 * - PIXEL RATIO: Ratio between physical and logical pixels (e.g., 2.0 on Retina).
	 *
	 * Design:
	 * - All public APIs use logical pixels (window coordinates)
	 * - Projection matrices use logical pixels for consistent coordinate spaces
	 * - Mouse input is already in logical pixels from the window system
	 */
	class CoordinateSystem {
	  public:
		CoordinateSystem() = default;
		~CoordinateSystem() = default;

		// Non-copyable, non-movable
		CoordinateSystem(const CoordinateSystem&) = delete;
		CoordinateSystem& operator=(const CoordinateSystem&) = delete;
		CoordinateSystem(CoordinateSystem&&) = delete;
		CoordinateSystem& operator=(CoordinateSystem&&) = delete;

		/**
		 * Set the window size the projections and conversions are computed from.
		 * Call on creation and from every resize callback.
		 */
		void setViewport(const Viewport& viewport);

		/**
		 * The size set last; 1920x1080 at ratio 1 until setViewport is called.
		 */
		const Viewport& viewport() const { return current; }

		/**
		 * Create screen-space orthographic projection matrix.
		 * (0,0) at top-left, Y increases downward.
		 * Uses logical pixels (window size), not physical pixels.
		 */
		glm::mat4 CreateScreenSpaceProjection() const;

		/**
		 * Create world-space orthographic projection matrix.
		 * (0,0) at center, Y increases upward.
		 * Uses logical pixels (window size), not physical pixels.
		 */
		glm::mat4 CreateWorldSpaceProjection() const;

		/**
		 * Get window size in logical pixels.
		 */
		glm::vec2 getWindowSize() const;

		/**
		 * Get the pixel ratio (physical pixels / logical pixels).
		 */
		float getPixelRatio() const { return current.pixelRatio(); }

		/**
		 * Convert window coordinates (logical pixels) to framebuffer coordinates (physical pixels).
		 */
		glm::vec2 WindowToFramebuffer(const glm::vec2& windowCoords) const;

		/**
		 * Convert framebuffer coordinates (physical pixels) to window coordinates (logical pixels).
		 */
		glm::vec2 FramebufferToWindow(const glm::vec2& fbCoords) const;

		// Percentage-based layout helpers
		// These allow UI elements to use relative sizing (e.g., "50%" of screen width)

		/**
		 * Convert percentage of window width to logical pixels.
		 * @param percent Percentage (0-100)
		 */
		float PercentWidth(float percent) const;

		/**
		 * Convert percentage of window height to logical pixels.
		 * @param percent Percentage (0-100)
		 */
		float PercentHeight(float percent) const;

		/**
		 * Convert percentage dimensions to logical pixel size.
		 * @param widthPercent Width percentage (0-100)
		 * @param heightPercent Height percentage (0-100)
		 */
		glm::vec2 PercentSize(float widthPercent, float heightPercent) const;

		/**
		 * Convert percentage position to logical pixel position.
		 * @param xPercent X percentage (0-100)
		 * @param yPercent Y percentage (0-100)
		 */
		glm::vec2 PercentPosition(float xPercent, float yPercent) const;

	  private:
		// What the class answered before any window was attached, kept so an unsized
		// CoordinateSystem still projects something sensible.
		Viewport current{1920, 1080, 1920, 1080};
	};

} // namespace Renderer
