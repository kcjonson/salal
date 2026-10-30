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
#include "backend/Backend.h"
#include "font/FontRenderer.h"
#include "graphics/ClipTypes.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "math/Types.h"
#include "primitives/FontFamily.h"
#include "primitives/InstanceData.h"
#include <cstddef>
#include <cstdint>
#include <glm/vec4.hpp>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace renderer {
	struct TessellatedMesh;
}

namespace Renderer { // NOLINT(readability-identifier-naming)

	// Forward declarations
	class CoordinateSystem;

	namespace Primitives { // NOLINT(readability-identifier-naming)

		// --- Initialization ---

		// Start the renderer on `backend`, which it owns from here until shutdown():
		// backend::gl::GLBackend::create() on the GL context the platform made, or a
		// backend::NullBackend for tests that run without a GPU. It draws with
		// `baseShader`, the base shader salal_base_shader() assembled for the consumer
		// and its hooks (the generated <namespace>::source(), docs/shader-hooks.md),
		// which needs to outlive only this call. Every call before init() or after
		// shutdown() does nothing.
		void init(std::unique_ptr<backend::Backend> backend, const backend::ShaderSource& baseShader);
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
		//   if (fontRenderer->Initialize(atlasSources, kBodyFont)) {
		//       Renderer::Primitives::setFontRenderer(fontRenderer.get());
		//   }
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
		// This configures the batch with the MSDF font atlas, and its distance range,
		// that a glyph quad without an atlas of its own samples. FontRenderer's quads
		// carry theirs, so this serves quads a caller builds by hand. Must be called
		// after init() and before rendering any text.
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

		// --- Frame Lifecycle ---

		// Clear the bound colour target to one colour, straight alpha like every colour
		// at this API; the target stores it premultiplied.
		void clear(const Foundation::Color& color);

		// The backbuffer as RGBA8, bottom row first as GL reads it, at the size the
		// last setViewport() with no RenderTarget open recorded. Its colour is
		// premultiplied by its alpha, as everything the renderer draws and clears is.
		// `width` and `height` are set first; the pixels are read only when `rgba`
		// holds width * height * 4 bytes, so a call with an empty span sizes the buffer
		// for the next one. Synchronous by design: it waits for the GPU.
		void readBackbuffer(std::span<uint8_t> rgba, int& width, int& height);

		void beginFrame();
		void endFrame(); // Flushes all batches

		// GPU code outside the renderer left depth test, culling or scissor on, or bound
		// a uniform buffer to binding point 0, which is the renderer's: set the fixed
		// state again (all three off) and rebind the renderer's uniforms before it draws.
		// Other bindings need no call; every flush makes its own, since a game's GL runs
		// between flushes.
		void invalidateState();

		// Draw-order barrier without a flush: every draw from here sorts after every
		// draw before it, whatever their zIndex, so two passes (a covered scene and
		// the one over it, the scenes and an overlay) keep their zIndex spaces apart
		// and the later pass paints on top. It costs no draw call: the batch sorts on
		// the barrier count ahead of z. The app sets one before each stacked scene
		// and each overlay renders.
		void sortBarrier();

		// Flush pending batched primitives now: upload them and submit their draw
		// calls. GPU work of a caller's own that must land over what was drawn so
		// far needs it; for draw order alone, sortBarrier() is enough.
		void flush();

		// Size of the bound target in pixels: the projection for batched drawing.
		// With no RenderTarget open it also refreshes the backbuffer size the renderer
		// remembers (what RenderTarget::end() restores and readBackbuffer() reports):
		// the coordinate system's framebuffer size when one is set, else width x height.
		// The platform layer calls it with the framebuffer size whenever that changes.
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

		// How a draw combines with what is under it. Colours are straight alpha at
		// this API; the shader premultiplies, and every mode blends premultiplied.
		//   Alpha     "over": the colour covers the destination by its alpha.
		//   Additive  the colour, times its alpha, is added to the destination, which
		//             keeps its own alpha. Glows, particles, bloom compositing.
		// Both draw in one GL blend state (additive zeroes the output alpha), so mixing
		// them never splits a draw, and the mode never changes draw order.
		enum class BlendMode : uint8_t { Alpha, Additive };

		// Arguments for DrawRect
		struct RectArgs {
			Foundation::Rect	  bounds;
			Foundation::RectStyle style;
			const char*			  id = nullptr; // Optional: for inspection/debugging
			int					  zIndex = 0;	// Optional: explicit draw order
			BlendMode			  blend = BlendMode::Alpha;
		};

		// Arguments for DrawLine
		struct LineArgs {
			Foundation::Vec2	  start;
			Foundation::Vec2	  end;
			Foundation::LineStyle style;
			const char*			  id = nullptr;
			int					  zIndex = 0;
			BlendMode			  blend = BlendMode::Alpha;
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
			BlendMode				 blend = BlendMode::Alpha;
			bool					 antiAlias = false; // Optional: feather the mesh's boundary
		};

		// Draw a rectangle with optional fill and border
		void drawRect(const RectArgs& args);

		// Draw a line
		void drawLine(const LineArgs& args);

		// Draw triangles from a mesh (for vector graphics tessellation). Aliased unless
		// `antiAlias`: then a ring one device pixel wide runs outside the mesh's
		// boundary, each boundary vertex's colour fading to transparent across it, as
		// drawPolygon's fill has. The boundary is found per call from the indices, which
		// costs, so leave it off for large per-frame batches; a mesh whose boundary
		// doesn't close into loops, or pinches (one vertex starting two boundary edges),
		// draws aliased.
		void drawTriangles(const TrianglesArgs& args);

		// Arguments for DrawCircle
		struct CircleArgs {
			Foundation::Vec2		center;
			float					radius;
			Foundation::CircleStyle style;
			const char*				id = nullptr;
			int						zIndex = 0;
			BlendMode				blend = BlendMode::Alpha;
		};

		// Draw a circle with optional fill and border, each one quad in drawArc's SDF mode
		// and anti-aliased like it: the fill a disk, the border a ring drawn over it at the
		// same z. Each edge ramps over a device pixel either side, so a circle a pixel or two
		// across carries more ink than its area: 1.8 times at a radius of half a device pixel,
		// 1.2 at one.
		//
		// Parameters:
		//   - center: Circle center position in current coordinate space
		//   - radius: Circle radius in pixels; zero or less draws nothing
		//   - style.fill: Fill color (set alpha to 0 to disable fill)
		//   - style.border: Optional border; `position` puts its width inside the radius,
		//     centred on it (the default) or outside it, and `cornerRadius` is unused
		//   - id: Optional debug identifier
		//   - zIndex: Draw order (higher values drawn later)
		void drawCircle(const CircleArgs& args);

		// How an open polyline's two ends finish: flat at the end point, a half disk
		// around it, or a half square past it.
		enum class LineCap : uint8_t { Butt, Round, Square };

		// How two segments meet. Round puts a disk on the joint. Miter runs the outer
		// edges on to their meeting point for a turn of up to a right angle and rounds
		// sharper turns (a miter limit of sqrt 2, rounding where SVG bevels).
		enum class LineJoin : uint8_t { Round, Miter };

		// Arguments for DrawPolygon
		struct PolygonArgs {
			std::span<const Foundation::Vec2> vertices; // One closed outline, in order; the last point joins the first
			Foundation::PolygonStyle		  style;
			const char*						  id = nullptr;
			int								  zIndex = 0;
			BlendMode						  blend = BlendMode::Alpha;
		};

		// Draw a filled polygon, convex or not, self-crossing filled by the nonzero
		// rule. The outline is tessellated once per distinct vertex list and cached,
		// with its boundary, so a silhouette drawn every frame under PushTransform costs
		// one tessellation. The fill is anti-aliased by a ring one device pixel wide
		// outside its boundary, the fill colour fading to transparent across it, so it
		// covers its area plus about half a pixel along its edge. Two opaque polygons
		// that share an edge show no seam; two translucent ones show a band about a pixel
		// wide along it, denser than either, where each one's ring lies over the other's
		// fill. A fill whose boundary doesn't close into loops, as where the outline
		// touches itself at a point, is drawn aliased, and a debug log says so once.
		//
		// A border is drawPolyline's stroke, closed and mitered (strokeOutline), so its
		// pieces overlap where drawPolyline's do. Its outer edge runs its width outside the
		// outline and its inner edge a pixel inside, over the fill's edge, so no background
		// shows between them. A notch is mitered into its corner, except beside an edge
		// too short for its cut, where it is round with a flat piece into the corner.
		void drawPolygon(const PolygonArgs& args);

		// Arguments for DrawPolyline
		struct PolylineArgs {
			std::span<const Foundation::Vec2> points;
			float							  width = 1.0F;
			Foundation::Color				  color = Foundation::Color::white();
			LineCap							  cap = LineCap::Butt;
			LineJoin						  join = LineJoin::Round;
			bool		closed = false; // Join the last point back to the first, uncapped; two points stay an open segment
			const char* id = nullptr;
			int			zIndex = 0;
			BlendMode	blend = BlendMode::Alpha;
		};

		// Draw a stroked polyline, anti-aliased: a convex SDF piece per segment, cut
		// along each joint's bisector so neighbouring pieces share the cut and every
		// pixel belongs to one of them, round joins and caps exact. A translucent or
		// additive stroke covers each pixel once, except where the line crosses itself,
		// where a segment is too short for the cuts its sharp joints need, and at a turn
		// within about 8.6 degrees of a hairpin: there pieces overlap. Points within a
		// quarter of a device pixel of the line drawn without them are dropped first, so
		// sub-pixel noise in the input (a hand's jitter, integer mouse samples) leaves no
		// short segments behind; noise larger than that still can. A closed line keeps
		// three points or more, and an end segment turns its cap by no more than that.
		void drawPolyline(const PolylineArgs& args);

		// Arguments for DrawArc. Angles are radians from +x, and a positive sweep turns
		// the way atan2(y, x) does in screen space: clockwise on screen, since y points
		// down. A negative sweep runs the other way from startAngle; a sweep of 2 pi or
		// more is the full ring. innerRadius 0 makes a pie slice or a disk.
		struct ArcArgs {
			Foundation::Vec2  center;
			float			  innerRadius = 0.0F;
			float			  outerRadius = 0.0F;
			float			  startAngle = 0.0F;
			float			  sweep = 2.0F * std::numbers::pi_v<float>;
			Foundation::Color color = Foundation::Color::white();
			const char*		  id = nullptr;
			int				  zIndex = 0;
			BlendMode		  blend = BlendMode::Alpha;
		};

		// Draw a ring, arc or pie: one quad, its shape a signed distance field in the
		// shader, anti-aliased like the rounded rectangle, with straight radial ends.
		void drawArc(const ArcArgs& args);

		// Arguments for DrawImage. `uvMin` maps to the top-left of `bounds` and `uvMax`
		// to the bottom-right, v counting texture rows from row 0. A texture uploaded
		// through createTexture (first row at row 0) draws upright with the defaults; a
		// RenderTarget's texture holds its bottom row at row 0, so draw it upright with
		// uvMin {0, 1} and uvMax {1, 0}.
		struct ImageArgs {
			TextureHandle	  texture;
			Foundation::Rect  bounds;
			Foundation::Vec2  uvMin{0.0F, 0.0F};
			Foundation::Vec2  uvMax{1.0F, 1.0F};
			Foundation::Color tint = Foundation::Color::white(); // Multiplies the texel; its alpha is the opacity
			const char*		  id = nullptr;
			int				  zIndex = 0;
			BlendMode		  blend = BlendMode::Alpha;
		};

		// Draw a texture into a rectangle, sampled linearly or nearest as the texture
		// was made. Texels are taken as premultiplied alpha, which a RenderTarget's
		// output already is; upload straight-alpha pixels premultiplied. Images batch
		// with everything else, splitting the draw only where the texture changes.
		void drawImage(const ImageArgs& args);

		// Arguments for DrawText
		struct TextArgs {
			std::string		  text;
			Foundation::Vec2  position;											 // Top-left position
			float			  scale = 1.0F;										 // Text scale (1.0F = 16px base size)
			Foundation::Color color = Foundation::Color(1.0F, 1.0F, 1.0F, 1.0F); // RGBA
			FontFamily		  font = FontFamily::Default;						 // Atlas to render with (the game's default)
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
			BlendMode				  blend = BlendMode::Alpha;
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
		// and friends), each in its own colour, sampling the atlas it carries at that
		// atlas's distance range, so one call can hold several families. Text is
		// interleaved with shapes in submission order.
		void drawGlyphs(const FontRenderer::GlyphQuad* quads, size_t count, float zIndex = 0.0F, BlendMode blend = BlendMode::Alpha);

		// --- World-Space Instancing ---

		// The view an instanced or baked world draw is projected with: world meters
		// to screen pixels through position, zoom and pixelsPerMeter, centred on the
		// viewport.
		struct WorldCamera {
			Foundation::Vec2 position{0.0F, 0.0F};
			float			 zoom = 1.0F;
			float			 pixelsPerMeter = 1.0F;
		};

		// Upload a tessellated mesh once for drawInstanced(); the handle releases the
		// GPU buffers when destroyed or passed to releaseInstancedMesh().
		InstancedMeshHandle uploadInstancedMesh(const renderer::TessellatedMesh& mesh, uint32_t maxInstances = 10000);
		void				releaseInstancedMesh(InstancedMeshHandle& handle);

		// Draw `count` instances of an uploaded mesh immediately (not batched), with
		// the per-instance transforms applied on the GPU under `camera` and the base
		// shader's hooks run on every vertex (setHookUniforms below).
		void drawInstanced(const InstancedMeshHandle& handle, const InstanceData* instances, uint32_t count, const WorldCamera& camera);

		// Alpha multiplied into instanced and baked draws (u_bakedAlpha); 1 is opaque.
		void setBakedAlpha(float alpha);

		// Fill a hook's uniform block (docs/shader-hooks.md) for the instanced draws
		// issued from here on; a draw already issued keeps the values it was given.
		// `bytes` is the block's std140 contents, the block's size exactly: a C++
		// struct laid out like the GLSL, through std::as_bytes. `block` is the constant
		// the generated header has for it, `hook` the hook's name (its file name before
		// .vert.glsl). A block the base shader init() was given does not have, or the
		// wrong size, is logged and ignored. Every block starts zeroed, which a hook
		// takes as off.
		void setHookUniforms(const backend::HookUniformBlock& block, std::span<const std::byte> bytes);
		void setHookUniforms(std::string_view hook, std::span<const std::byte> bytes);

		// Bracket a caller's own world-space mesh draws: the base shader in its baked
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
