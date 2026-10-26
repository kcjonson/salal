#pragma once

// Uber Batch Renderer - Unified rendering for shapes and text.
//
// This is the internal batching implementation used by the Primitives API.
// It accumulates draw commands for both shapes (SDF) and text (MSDF) and
// renders them in a single pass with correct z-ordering.
//
// Also provides GPU instancing for efficient rendering of many identical meshes.
//
// GL objects and uniform locations live behind BatchRenderer::GL in the .cpp, so
// this header carries no GL types.

#include "Handle.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "math/Types.h"
#include "primitives/InstanceData.h"
#include "primitives/Primitives.h"
#include "primitives/Stroke.h"
#include "vector/Tessellator.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Renderer { // NOLINT(readability-identifier-naming)

	// Forward declaration
	class CoordinateSystem;

	// Unified vertex format for shapes and text (uber shader)
	// Layout matches uber.vert attributes:
	//   location 0: a_position (vec2)
	//   location 1: a_texCoord (vec2) - UV for text and images, rectLocalPos for shapes, arc-local for arcs
	//   location 2: a_color (vec4)
	//   location 3: a_data1 (vec4) - borderData for shapes, blur for shadows, unused otherwise
	//   location 4: a_data2 (vec4) - per-mode parameters, the render mode in w
	//   location 5: a_clipBounds (vec4) - (minX, minY, maxX, maxY) or (0,0,0,0) for no clip
	struct UberVertex {
		Foundation::Vec2 position;	 // Screen-space position
		Foundation::Vec2 texCoord;	 // UV for text and images, rectLocalPos for shapes
		Foundation::Vec4 color;		 // Fill color RGBA, straight alpha
		Foundation::Vec4 data1;		 // Border: (color.rgb, width) for shapes, (blur, 0, 0, 0) for shadows
		Foundation::Vec4 data2;		 // Shape: (halfW, halfH, cornerRadius, borderPos), Text: (pixelRange, 0, 0, -1)
		Foundation::Vec4 clipBounds; // Clip rect (minX, minY, maxX, maxY), or (0,0,0,0) for no clipping
	};

	// data2.w holds the render mode and the additive flag together; uber.frag
	// decodes the same table with half-unit margins.
	//
	//   mode              alpha        additive
	//   shape             0, 1, 2      4, 5, 6     border position inside, center, outside
	//   text              -1           -11
	//   instanced, baked  -2           -           written by uber.vert, never additive
	//   shadow            -3           -13
	//   arc               -4           -14
	//   image             -5           -15
	constexpr float kRenderModeText = -1.0F;	  // MSDF text
	constexpr float kRenderModeInstanced = -2.0F; // Instanced and baked world meshes (uber.vert)
	constexpr float kRenderModeShadow = -3.0F;	  // Box-shadow / glow (SDF soft falloff)
	constexpr float kRenderModeArc = -4.0F;		  // Ring, arc or pie (SDF)
	constexpr float kRenderModeImage = -5.0F;	  // Plain texture sample
	constexpr float kAdditiveShapeOffset = 4.0F;  // Added to a shape's border position
	constexpr float kAdditiveModeOffset = -10.0F; // Added to a negative mode

	// data2.w for a border position (0..2) or a negative mode, drawn with `blend`.
	constexpr float renderMode(float mode, Primitives::BlendMode blend) {
		if (blend != Primitives::BlendMode::Additive) {
			return mode;
		}
		return mode >= 0.0F ? mode + kAdditiveShapeOffset : mode + kAdditiveModeOffset;
	}

	// The GL blend state a mode draws in. Output is premultiplied and blended
	// (ONE, ONE_MINUS_SRC_ALPHA); Additive is that state with the output alpha
	// zeroed in uber.frag, so it shares Alpha's state and never splits a run.
	constexpr Primitives::BlendMode glBlendState(Primitives::BlendMode mode) {
		return mode == Primitives::BlendMode::Additive ? Primitives::BlendMode::Alpha : mode;
	}

	// One glDrawElements: `indexCount` indices from `indexStart` in the emit order,
	// sampling `texture` (bound for the run; invalid binds nothing) in one GL blend
	// state.
	struct DrawRun {
		uint32_t			  indexStart = 0;
		uint32_t			  indexCount = 0;
		TextureHandle		  texture;
		Primitives::BlendMode blendState = Primitives::BlendMode::Alpha;
	};

	// What flush() submits: the indices in emit order and the runs they split into.
	// Both point into the batch and stay valid until the next add or flush.
	struct DrawPlan {
		std::span<const uint32_t> indices;
		std::span<const DrawRun>  runs;
	};

	// Batch accumulator - collects geometry before GPU upload
	class BatchRenderer { // NOLINT(cppcoreguidelines-special-member-functions)
	  public:
		BatchRenderer();
		~BatchRenderer();

		// Initialize OpenGL resources (shaders, VBOs)
		void init();

		// Cleanup OpenGL resources
		void shutdown();

		// --- Shape rendering (SDF) ---

		// Add shape quad to batch (with optional SDF border and corner radius).
		// When `gradient` is set, the quad's four corners are colored from the
		// gradient stops (interpolated by the GPU) instead of the flat fillColor;
		// the border/SDF/corner radius are unaffected.
		void addQuad(
			const Foundation::Rect&							 bounds,
			const Foundation::Color&						 fillColor,
			const std::optional<Foundation::BorderStyle>&	 border = std::nullopt,
			float											 cornerRadius = 0.0F,
			const std::optional<Foundation::LinearGradient>& gradient = std::nullopt,
			float											 zIndex = 0.0F,
			Primitives::BlendMode							 blend = Primitives::BlendMode::Alpha
		);

		// Add an outer box-shadow / glow quad behind a rect. The SDF shader renders
		// it as a soft falloff over `shadow.blur` px outside the (spread-grown)
		// shape. Emit before the element's own quad so it sits behind.
		void addShadowQuad(
			const Foundation::Rect&		 bounds,
			const Foundation::BoxShadow& shadow,
			float						 cornerRadius,
			float						 zIndex = 0.0F,
			Primitives::BlendMode		 blend = Primitives::BlendMode::Alpha
		);

		// Add raw triangles (for circles, polygons, etc.)
		// If inputColors is provided, uses per-vertex colors; otherwise uses uniform color
		void addTriangles(
			const Foundation::Vec2*	 inputVertices,
			const uint16_t*			 inputIndices,
			size_t					 vertexCount,
			size_t					 indexCount,
			const Foundation::Color& color,
			const Foundation::Color* inputColors = nullptr,
			float					 zIndex = 0.0F,
			Primitives::BlendMode	 blend = Primitives::BlendMode::Alpha
		);

		// Add the pieces of a stroke (strokePolyline) as one draw group, each an SDF
		// box quad padded by a pixel so its edge ramp is not cut off.
		void addStrokePieces(
			std::span<const StrokePiece> pieces,
			const Foundation::Color&	 color,
			float						 zIndex = 0.0F,
			Primitives::BlendMode		 blend = Primitives::BlendMode::Alpha
		);

		// Add a ring, arc or pie as one SDF quad (see Primitives::ArcArgs).
		void addArc(const Primitives::ArcArgs& args);

		// Add a textured quad sampling args.texture (see Primitives::ImageArgs). An
		// invalid texture adds nothing.
		void addImageQuad(const Primitives::ImageArgs& args);

		// --- Text rendering (MSDF) ---

		// Add text glyph quad to batch
		// position: screen-space top-left of glyph
		// size: glyph dimensions in screen pixels
		// uvMin/uvMax: texture coordinates in MSDF atlas
		// color: text color with alpha
		// runOrigin: pen origin of the text run at the baseline (same value for
		//   every glyph of a run, i.e. FontRenderer::GlyphQuad::runOrigin). Under
		//   identity/translate-only transforms the run origin is snapped to the
		//   device pixel grid and every glyph shifted by the same delta, keeping
		//   glyph edges stable without disturbing advances/kerning.
		// atlas: MSDF atlas this glyph samples from. An invalid handle selects the
		//   default atlas set via setFontAtlas() (Roboto). A frame may mix multiple
		//   atlases; flush() splits draws by atlas so each glyph samples the right
		//   texture.
		void addTextQuad(
			const Foundation::Vec2&	 position,
			const Foundation::Vec2&	 size,
			const Foundation::Vec2&	 uvMin,
			const Foundation::Vec2&	 uvMax,
			const Foundation::Color& color,
			const Foundation::Vec2&	 runOrigin,
			TextureHandle			 atlas = {},
			float					 zIndex = 0.0F,
			Primitives::BlendMode	 blend = Primitives::BlendMode::Alpha
		);

		// Set the default MSDF font atlas (used by text quads added with an invalid
		// atlas handle). pixelRange comes from atlas generation.
		void setFontAtlas(TextureHandle atlas, float pixelRange = 4.0F);

		// --- Rendering ---

		// The emit order and draw runs of what has been added, built without GL so
		// tests can run headless; flush() submits exactly this. Groups are
		// stable-sorted by z when any carries an explicit z (submission order
		// otherwise), and the emit order is cut into a new run wherever a triangle
		// needs a texture other than the bound one or a group's GL blend state
		// differs. Shapes, arcs and the like need no texture and never split.
		DrawPlan plan();

		// Flush accumulated geometry to GPU and render
		void flush();

		// Frame lifecycle
		void beginFrame();
		void endFrame();

		// Set viewport dimensions for projection matrix.
		// IMPORTANT: Must be called before drawInstanced() to ensure correct projection.
		void setViewport(int width, int height);

		// Get current viewport dimensions
		void getViewport(int& width, int& height) const;

		// Set coordinate system (for DPI-aware projection matrices)
		void setCoordinateSystem(CoordinateSystem* coordSystem);

		// --- Clipping ---

		// Set current clip bounds (applied to all subsequent vertices)
		// bounds: (minX, minY, maxX, maxY) in screen coordinates
		void setClipBounds(const Foundation::Vec4& bounds);

		// Clear clip bounds (disables clipping)
		void clearClipBounds();

		// Get current clip bounds
		const Foundation::Vec4& getClipBounds() const { return currentClipBounds; }

		// --- Transform ---

		// Set the current transform matrix.
		// The transform is baked into vertex positions when vertices are added (not at flush time).
		// This allows different transforms for different parts of the scene within a single batch.
		// Used for content offset (scrolling) in containers.
		void setTransform(const Foundation::Mat4& transform);

		// Get current transform matrix
		const Foundation::Mat4& getTransform() const { return currentTransform; }

		// Rendering statistics structure
		struct RenderStats {
			uint32_t drawCalls = 0;
			uint32_t vertexCount = 0;
			uint32_t triangleCount = 0;
		};

		// Statistics
		size_t		getVertexCount() const { return vertices.size(); }
		size_t		getDrawCallCount() const { return drawCallCount; }
		RenderStats getStats() const;

		// The vertices added since the last flush, in the order added.
		std::span<const UberVertex> getPendingVertices() const { return vertices; }

		// --- GPU Instancing (World-Space) ---

		/// Upload a tessellated mesh to GPU for instanced rendering.
		/// The mesh is uploaded once and reused for all instances.
		/// @param mesh Tessellated mesh to upload
		/// @param maxInstances Maximum number of instances to support (default 10000)
		/// @return Handle for subsequent draw calls
		InstancedMeshHandle uploadInstancedMesh(const renderer::TessellatedMesh& mesh, uint32_t maxInstances = 10000);

		/// Release GPU resources for an instanced mesh.
		/// @param handle Handle to release (will be invalidated)
		static void releaseInstancedMesh(InstancedMeshHandle& handle);

		/// Draw multiple instances of a mesh with GPU instancing.
		/// Transforms are computed on GPU using camera uniforms.
		/// @param handle Mesh handle from uploadInstancedMesh
		/// @param instances Array of per-instance data (world position, rotation, scale, color)
		/// @param count Number of instances to draw
		/// @param cameraPosition Camera world position (center of view)
		/// @param cameraZoom Camera zoom level (1.0 = normal)
		/// @param pixelsPerMeter World scale factor
		void drawInstanced(
			const InstancedMeshHandle& handle,
			const InstanceData*		   instances,
			uint32_t				   count,
			Foundation::Vec2		   cameraPosition,
			float					   cameraZoom,
			float					   pixelsPerMeter
		);

		/// The groundcover deform and cursor uniforms (instancing.glsl), read by the
		/// instanced path while `params.enabled`.
		void setGroundcover(const Primitives::GroundcoverParams& params);

		/// u_bakedAlpha, the alpha the instanced and baked paths multiply in.
		void setBakedAlpha(float alpha);

		/// Bind the uber program in its baked world-space mode (u_instanced = 2) with
		/// the camera uniforms and the projection from the setViewport() size, so a
		/// caller can draw its own world-space VAOs under it until endBakedPass().
		void beginBakedPass(const Primitives::WorldCamera& camera);
		void endBakedPass();

	  private:
		// Vertex data (CPU-side accumulation)
		std::vector<UberVertex> vertices;
		std::vector<uint32_t>	indices;

		// Per-vertex texture tag, parallel to `vertices`. Invalid = no texture
		// requirement (shapes, arcs); valid = the MSDF atlas a text vertex or the
		// texture an image vertex samples. plan() splits runs on these.
		std::vector<TextureHandle> vertexAtlas;

		// Per-draw-call groups. Each add* records one {indexStart, indexCount, zIndex,
		// blend}. plan() keeps submission order unless some group carries an explicit
		// (non-zero) z, in which case groups are stable-sorted by z and the emit-order
		// index list is rebuilt. zIndex originates in the component layer.
		struct DrawGroup {
			uint32_t			  indexStart;
			uint32_t			  indexCount;
			float				  zIndex;
			Primitives::BlendMode blend;
		};
		std::vector<DrawGroup> drawGroups;
		bool				   anyExplicitZ = false;

		// plan()'s output, reused across flushes: the z-sorted index list (unused in
		// submission order) and the runs.
		std::vector<uint32_t> sortedIndices;
		std::vector<DrawRun>  runs;

		// Record the index range [groupStart, current end) as one draw group.
		void recordGroup(uint32_t groupStart, float zIndex, Primitives::BlendMode blend);

		// Append the four corners of a quad, TL, TR, BR, BL, positions through the
		// current transform, and its two triangles, all tagged with `texture`.
		void pushQuad(
			const Foundation::Vec2 (&corners)[4],
			const Foundation::Vec2 (&texCoords)[4],
			const Foundation::Vec4& color,
			const Foundation::Vec4& data1,
			const Foundation::Vec4& data2,
			TextureHandle			texture = {}
		);

		// OpenGL resources and uniform locations (defined in BatchRenderer.cpp)
		struct GL;
		std::unique_ptr<GL> gl;

		// Viewport dimensions
		int viewportWidth = 800;
		int viewportHeight = 600;

		// Coordinate system (optional, for DPI-aware rendering)
		CoordinateSystem* coordinateSystem = nullptr;

		// Default font atlas: bound for text quads added with an invalid atlas handle.
		TextureHandle defaultFontAtlas;
		float		  fontPixelRange = 4.0F;

		// Last value written to u_bakedAlpha; endBakedPass() restores 1 only if it moved.
		float bakedAlpha = 1.0F;

		// Current clip bounds (applied to all vertices)
		// (0,0,0,0) means no clipping
		Foundation::Vec4 currentClipBounds{0.0F, 0.0F, 0.0F, 0.0F};

		// Classification of currentTransform, cached once per setTransform() to
		// avoid per-vertex checks. Text snapping runs for Identity/TranslateOnly
		// (where "on the pixel grid" is well-defined); General falls back to the
		// full matrix multiply with no snapping.
		enum class TransformClass : uint8_t { kIdentity, kTranslateOnly, kGeneral };

		// Current transform matrix (baked into vertex positions at add-time)
		Foundation::Mat4 currentTransform{1.0F}; // Identity
		TransformClass	 transformClass = TransformClass::kIdentity;
		Foundation::Vec2 transformTranslation{0.0F, 0.0F}; // Valid for kTranslateOnly (zero for kIdentity)

		// Local units per logical pixel under currentTransform (the inverse of its
		// area scale), the padding SDF quads get so their edge ramp fits.
		float pixelPadding = 1.0F;

		// Caches addTextQuad's run-origin snap: every glyph of a text run shares the
		// same {pixelRatio, translation, runOrigin}, so the delta is computed once per
		// run instead of per glyph. pixelRatio 0 marks the cache empty.
		struct TextSnapCache {
			float			 pixelRatio = 0.0F;
			Foundation::Vec2 translation{0.0F, 0.0F};
			Foundation::Vec2 runOrigin{0.0F, 0.0F};
			Foundation::Vec2 delta{0.0F, 0.0F};
		};
		TextSnapCache textSnapCache;

		// Statistics
		size_t drawCallCount = 0;
		size_t frameVertexCount = 0;   // Cumulative vertex count for the frame
		size_t frameTriangleCount = 0; // Cumulative triangle count for the frame
	};

} // namespace Renderer
