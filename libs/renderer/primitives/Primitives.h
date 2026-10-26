#pragma once

// Primitive Rendering API - Unified 2D drawing interface.
//
// This API provides immediate-mode drawing functions used by:
// - RmlUI backend (screen-space UI panels)
// - Game world rendering (tiles, entities)
// - World-space UI (health bars, tooltips)
// - Custom UI components
//
// Implementation uses batching to minimize draw calls while maintaining a simple API.
// Uses C++20 designated initializers (.Args{} pattern) for clean, readable code.

#include "Handle.h"
#include "Texture.h"
#include "font/FontRenderer.h"
#include "graphics/ClipTypes.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "math/Types.h"
#include "primitives/FontFamily.h"
#include "primitives/InstanceData.h"
#include <cstdint>
#include <glm/vec4.hpp>
#include <span>
#include <string>
#include <vector>

namespace renderer {
	struct TessellatedMesh;
}

namespace Renderer { // NOLINT(readability-identifier-naming)

	// Forward declarations
	class Renderer;
	class CoordinateSystem;

	namespace Primitives { // NOLINT(readability-identifier-naming)

		// --- Initialization ---

		void init(Renderer* renderer);
		void shutdown();

		// Set the coordinate system (must be called after Init)
		void setCoordinateSystem(CoordinateSystem* coordSystem);

		// Get viewport dimensions in logical pixels.
		//
		// Returns window size in logical (DPI-independent) coordinates, suitable for
		// UI layout and mouse coordinate conversion. On Retina displays, this returns
		// half the framebuffer dimensions; on standard displays, it matches getViewport().
		//
		// Use this instead of getViewport() when you need dimensions that match GLFW's
		// window coordinates (what mouse events report, what UI layout uses).
		void getLogicalViewport(int& width, int& height);

		// Set the font renderer for text rendering.
		//
		// This function stores a FontRenderer instance that Text shapes can retrieve
		// and use for rendering. The application owns the instance and its lifetime.
		//
		// Should be called during application initialization if text rendering is needed.
		// Pass nullptr to disable text rendering.
		//
		// Example:
		//   auto fontRenderer = std::make_unique<Renderer::FontRenderer>();
		//   fontRenderer->Initialize();
		//   Renderer::Primitives::setFontRenderer(fontRenderer.get());
		void setFontRenderer(FontRenderer* fontRenderer);

		// Get the current font renderer instance.
		//
		// Returns the FontRenderer previously set via SetFontRenderer(), or nullptr
		// if no font renderer has been configured. Used internally by Text shapes
		// to access font rendering capabilities.
		//
		// Returns: Pointer to FontRenderer, or nullptr if not set
		FontRenderer* getFontRenderer();

		// Set the default font atlas for text rendering.
		//
		// This configures the batch with the MSDF font atlas that text quads sample
		// when no family is given. Must be called after init() and before rendering
		// any text.
		//
		// Parameters:
		//   - atlas: the MSDF font atlas, from FontRenderer::getAtlasTexture()
		//   - pixelRange: Distance field pixel range (default 4.0, from atlas generation)
		void setFontAtlas(TextureHandle atlas, float pixelRange = 4.0F);

		// Set the tile atlas texture and UV rects per surface index.
		// Consumed by the engine tile pass (ChunkRenderer), not the batch renderer.
		void setTileAtlas(TextureHandle atlas, const std::vector<glm::vec4>& rects);

		// Tile atlas accessors for the tile render pass
		TextureHandle				  getTileAtlasTexture();
		const std::vector<glm::vec4>& getTileAtlasRects();

		// --- Textures ---
		//
		// Every texture the renderer samples lives in its texture table, keyed by
		// TextureHandle; the handle is the texture to every caller outside the
		// renderer. A destroyed handle is stale: later calls with it do nothing, and
		// a draw that samples it gets no texture (warned once).

		// A new texture from `desc`, uploaded from desc.pixels when given; invalid,
		// with an error logged, if it could not be made.
		TextureHandle createTexture(const TextureDesc& desc);

		// Replace a rectangle of the texture with `pixels`, in the texture's format.
		void updateTexture(TextureHandle handle, int x, int y, int width, int height, const void* pixels);

		void destroyTexture(TextureHandle handle);

		// Set a callback to update frame counter for FontRenderer cache LRU tracking.
		//
		// The application registers FontRenderer::updateFrame() here when it creates
		// the font renderer.
		//
		// The callback will be invoked by BeginFrame() before rendering.
		using FrameUpdateCallback = void (*)();
		void setFrameUpdateCallback(FrameUpdateCallback callback);

		// --- Frame Lifecycle ---

		// Clear the bound colour target to one colour.
		void clear(const Foundation::Color& color);

		// The backbuffer as RGBA8, bottom row first as GL reads it, at the size the
		// last setViewport() with no RenderTarget open recorded. `width` and `height`
		// are set first; the pixels are read only when `rgba` holds width * height * 4
		// bytes, so a call with an empty span sizes the buffer for the next one.
		// Synchronous by design: it waits for the GPU.
		void readBackbuffer(std::span<uint8_t> rgba, int& width, int& height);

		void beginFrame();
		void endFrame(); // Flushes all batches

		// Draw-order barrier: flush pending batched primitives now. Groups only
		// z-sort against each other within one flush, so flushing between two
		// passes (e.g. world drawing vs UI) keeps their zIndex spaces independent
		// and guarantees the later pass paints on top.
		void flush();

		// Size of the bound target in pixels: the projection for batched drawing,
		// and, with no RenderTarget open, the backbuffer size the renderer remembers
		// (what RenderTarget::end() restores and readBackbuffer() reports). The
		// platform layer calls it with the framebuffer size whenever that changes.
		void setViewport(int width, int height);

		// Get current viewport dimensions
		void getViewport(int& width, int& height);

		// --- Coordinate System Helpers ---

		// Get screen-space projection matrix (requires SetCoordinateSystem)
		Foundation::Mat4 getScreenSpaceProjection();

		// Get world-space projection matrix (requires SetCoordinateSystem)
		Foundation::Mat4 getWorldSpaceProjection();

		// Percentage-based layout helpers (requires SetCoordinateSystem)
		float			 PercentWidth(float percent);
		float			 PercentHeight(float percent);
		Foundation::Vec2 PercentSize(float widthPercent, float heightPercent);
		Foundation::Vec2 PercentPosition(float xPercent, float yPercent);

		// --- Drawing Functions ---

		// Arguments for DrawRect
		struct RectArgs {
			Foundation::Rect	  bounds;
			Foundation::RectStyle style;
			const char*			  id = nullptr; // Optional: for inspection/debugging
			int					  zIndex = 0;	// Optional: explicit draw order
		};

		// Arguments for DrawLine
		struct LineArgs {
			Foundation::Vec2	  start;
			Foundation::Vec2	  end;
			Foundation::LineStyle style;
			const char*			  id = nullptr;
			int					  zIndex = 0;
		};

		// Arguments for DrawTriangles
		struct TrianglesArgs {
			const Foundation::Vec2*	 vertices = nullptr; // Vertex positions (array)
			const uint16_t*			 indices = nullptr;	 // Triangle indices (array)
			size_t					 vertexCount = 0;	 // Number of vertices
			size_t					 indexCount = 0;	 // Number of indices (triangles * 3)
			Foundation::Color		 color;				 // Fill color (used if colors == nullptr)
			const Foundation::Color* colors = nullptr;	 // Optional: per-vertex colors (array, same size as vertices)
			const char*				 id = nullptr;		 // Optional: for inspection/debugging
			int						 zIndex = 0;		 // Optional: explicit draw order
		};

		// Draw a rectangle with optional fill and border
		void drawRect(const RectArgs& args);

		// Draw a line
		void drawLine(const LineArgs& args);

		// Draw triangles from a mesh (for vector graphics tessellation)
		void drawTriangles(const TrianglesArgs& args);

		// Arguments for DrawCircle
		struct CircleArgs {
			Foundation::Vec2		center;
			float					radius;
			Foundation::CircleStyle style;
			const char*				id = nullptr;
			int						zIndex = 0;
		};

		// Draw a circle with optional fill and border.
		//
		// Circles are tessellated into a 64-segment triangle fan on the CPU,
		// providing smooth appearance without requiring special shaders.
		// Borders are rendered as connected line segments.
		//
		// Parameters:
		//   - center: Circle center position in current coordinate space
		//   - radius: Circle radius in pixels
		//   - style.fill: Fill color (set alpha to 0 to disable fill)
		//   - style.border: Optional border (color and width)
		//   - id: Optional debug identifier
		//   - zIndex: Draw order (higher values drawn later)
		void drawCircle(const CircleArgs& args);

		// Arguments for DrawText
		struct TextArgs {
			std::string		  text;
			Foundation::Vec2  position;											 // Top-left position
			float			  scale = 1.0F;										 // Text scale (1.0F = 16px base size)
			Foundation::Color color = Foundation::Color(1.0F, 1.0F, 1.0F, 1.0F); // RGBA
			FontFamily		  font = FontFamily::Roboto;						 // Atlas to render with (default Roboto)
			// CSS text-shadow: when shadowColor.a > 0, the string is drawn once at
			// position+shadowOffset in shadowColor (behind), then normally.
			Foundation::Color shadowColor = Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F);
			Foundation::Vec2  shadowOffset{0.0F, 0.0F};
			// CSS text-align / vertical alignment. When boxWidth/boxHeight > 0, the
			// text is aligned within [position, position+box]; otherwise drawn at
			// position (the box-less default matches plain top-left placement).
			Foundation::HorizontalAlign hAlign = Foundation::HorizontalAlign::Left;
			Foundation::VerticalAlign	vAlign = Foundation::VerticalAlign::Top;
			float						boxWidth = 0.0F;  // align box width (px); 0 = no horizontal box
			float						boxHeight = 0.0F; // align box height (px); 0 = no vertical box
			// CSS letter-spacing: extra px inserted between glyphs (not after last).
			float letterSpacing = 0.0F;
			// CSS text-transform applied to the string at draw time.
			Foundation::TextTransform transform = Foundation::TextTransform::None;
			const char*				  id = nullptr;
			float					  zIndex = 0.0F;
		};

		// Draw text using the font renderer.
		//
		// IMPORTANT: Requires SetFontRenderer() to be called during initialization.
		// Text rendering uses batched command queue for proper z-ordering with shapes.
		//
		// Parameters:
		//   - text: String to render
		//   - position: Top-left position in current coordinate space
		//   - scale: Text size scale (1.0F = 16px base font size)
		//   - color: RGBA text color
		//   - zIndex: Draw order (higher values drawn later, allows proper layering with shapes)
		void drawText(const TextArgs& args);

		// Batch glyph quads a caller laid out itself (FontRenderer::generateGlyphQuads
		// and friends), each in its own colour, sampling the family's atlas. Text is
		// interleaved with shapes in submission order.
		void drawGlyphs(const FontRenderer::GlyphQuad* quads, size_t count, FontFamily family = FontFamily::Roboto, float zIndex = 0.0F);

		// --- World-Space Instancing ---

		// The view an instanced or baked world draw is projected with: world meters
		// to screen pixels through position, zoom and pixelsPerMeter, centred on the
		// viewport.
		struct WorldCamera {
			Foundation::Vec2 position{0.0F, 0.0F};
			float			 zoom = 1.0F;
			float			 pixelsPerMeter = 1.0F;
		};

		// The groundcover deform (instancing.glsl): clumps rest at `openness` of
		// full size and open and bend away within `cursorRadius` of `cursorWorld`.
		// Off by default; a no-op for ordinary instanced entities.
		struct GroundcoverParams {
			bool			 enabled = false;
			float			 openness = 1.0F;
			float			 reach = 0.0F;
			Foundation::Vec2 cursorWorld{0.0F, 0.0F};
			float			 cursorRadius = 0.0F;
			float			 cursorStrength = 0.0F;
		};

		// Upload a tessellated mesh once for drawInstanced(); the handle releases the
		// GPU buffers when destroyed or passed to releaseInstancedMesh().
		InstancedMeshHandle uploadInstancedMesh(const renderer::TessellatedMesh& mesh, uint32_t maxInstances = 10000);
		void				releaseInstancedMesh(InstancedMeshHandle& handle);

		// Draw `count` instances of an uploaded mesh immediately (not batched), with
		// the per-instance transforms applied on the GPU under `camera`.
		void drawInstanced(const InstancedMeshHandle& handle, const InstanceData* instances, uint32_t count, const WorldCamera& camera);

		// Groundcover and cursor uniforms for subsequent instanced draws.
		void setGroundcover(const GroundcoverParams& params);

		// Alpha multiplied into instanced and baked draws (u_bakedAlpha); 1 is opaque.
		void setBakedAlpha(float alpha);

		// Bracket a caller's own world-space mesh draws: the uber program in its baked
		// mode with `camera` and the projection from the setViewport() size, blending
		// on and depth and culling off, as the batch itself draws. Flush pending
		// batched geometry first if draw order matters. setBakedAlpha() may be called
		// between draws; endBakedPass() restores it to 1.
		void beginBakedPass(const WorldCamera& camera);
		void endBakedPass();

		// --- State Management ---

		// === Clipping System (Shader-based, batching-friendly) ===
		//
		// PushClip/PopClip provide per-vertex clip bounds that preserve batching.
		// All vertices added after PushClip() will be clipped to the specified region.
		// Supports nested clipping (intersection of all active clip regions).
		//
		// Fast path: ClipRect with ClipMode::Inside uses shader-based clipping
		// (zero GL state changes, full batching preserved).
		//
		// Slow path: Complex shapes (rounded rect, circle, path) or ClipMode::Outside
		// will use stencil buffer in future phases (stub functions for now).
		//
		// See /docs/technical/ui-framework/clipping.md for design details.

		// Push a new clip region. Nested clips are intersected.
		// For ClipRect with ClipMode::Inside, this uses the fast shader path.
		void pushClip(const Foundation::ClipSettings& settings);

		// Pop the most recent clip region, restoring the parent clip (if any).
		void popClip();

		// Get current clip bounds as Vec4 (minX, minY, maxX, maxY).
		// Returns (0,0,0,0) if no clip is active.
		Foundation::Vec4 getCurrentClipBounds();

		// Check if any clip region is currently active.
		bool IsClipActive();

		// === Convenience Functions for Future Clip Shapes ===
		//
		// These functions provide a simpler API for common clip shapes.
		// Currently they use bounding-box approximation; future phases will
		// implement proper stencil-buffer clipping for accurate shapes.

		// Push a rounded rectangle clip region.
		// NOTE: Currently uses bounding box approximation. Accurate rounded rect
		// clipping requires stencil buffer (Phase 3).
		void pushClipRoundedRect(const Foundation::Rect& bounds, float cornerRadius);

		// Push a circular clip region.
		// NOTE: Currently uses bounding box approximation. Accurate circular
		// clipping requires stencil buffer (Phase 3).
		void pushClipCircle(const Foundation::Vec2& center, float radius);

		// Push an arbitrary path clip region.
		// NOTE: Currently uses bounding box approximation. Accurate path
		// clipping requires stencil buffer (Phase 3).
		void pushClipPath(const std::vector<Foundation::Vec2>& vertices);

		// === ClipMode::Outside Support ===
		//
		// ClipMode::Outside is defined in ClipTypes.h but not yet implemented.
		// When implemented, it will allow "punch hole" effects where content
		// is visible OUTSIDE the clip shape (e.g., spotlight effects).
		// This requires stencil buffer with inverted test (Phase 3).

		// Legacy scissor functions (deprecated - use PushClip/PopClip instead)
		// Scissor/clipping (for scrollable containers)
		void			 PushScissor(const Foundation::Rect& clipRect);
		void			 PopScissor();
		Foundation::Rect getCurrentScissor();

		// Transform stack (for world-space rendering)
		void			 PushTransform(const Foundation::Mat4& transform);
		void			 PopTransform();
		Foundation::Mat4 getCurrentTransform();

		// --- Statistics ---

		struct RenderStats {
			uint32_t drawCalls = 0;
			uint32_t vertexCount = 0;
			uint32_t triangleCount = 0;
		};

		RenderStats getStats();

	} // namespace Primitives
} // namespace Renderer
