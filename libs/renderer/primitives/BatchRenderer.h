#pragma once

// Batch Renderer - Unified rendering for shapes and text.
//
// This is the internal batching implementation used by the Primitives API.
// It accumulates draw commands for both shapes (SDF) and text (MSDF) and
// renders them in a single pass with correct z-ordering.
//
// Also provides GPU instancing for efficient rendering of many identical meshes.
//
// It sits in front of the backend seam: it builds BatchVertex data, the draw runs
// and the uniforms as plain data, and a backend::Backend uploads and draws them.

#include "Handle.h"
#include "backend/Backend.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "math/Types.h"
#include "primitives/InstanceData.h"
#include "primitives/Primitives.h"
#include "primitives/Stroke.h"
#include "vector/Tessellator.h"
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Renderer { // NOLINT(readability-identifier-naming)

	// Forward declaration
	class CoordinateSystem;

	// BatchVertex, the batch's vertex, is in backend/Backend.h: the backend uploads it.
	//
	// data2.w holds the render mode and the additive flag together; base.frag
	// decodes the same table with half-unit margins.
	//
	//   mode              alpha        additive
	//   shape             0, 1, 2      4, 5, 6     border position inside, center, outside
	//   text              -1           -11
	//   instanced, baked  -2           -           written by base.vert, never additive
	//   shadow            -3           -13
	//   arc               -4           -14
	//   image             -5           -15
	constexpr float kRenderModeText = -1.0F;	  // MSDF text
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

	// The GPU blend state a mode draws in. Output is premultiplied and blended
	// (ONE, ONE_MINUS_SRC_ALPHA); Additive is that state with the output alpha
	// zeroed in base.frag, so it shares Alpha's state and never splits a run.
	constexpr backend::BlendState blendState(Primitives::BlendMode /*mode*/) {
		return backend::BlendState::Alpha;
	}

	// One draw call: `indexCount` indices from `indexStart` in the emit order,
	// sampling `texture` (bound for the run; invalid binds nothing) in one GPU
	// blend state.
	struct DrawRun {
		uint32_t			indexStart = 0;
		uint32_t			indexCount = 0;
		TextureHandle		texture;
		backend::BlendState blendState = backend::BlendState::Alpha;
	};

	// What flush() submits: the indices in emit order and the runs they split into.
	// Both point into the batch and stay valid until the next add or flush.
	struct DrawPlan {
		std::span<const uint32_t> indices;
		std::span<const DrawRun>  runs;
	};

	// Batch accumulator - collects geometry before GPU upload
	class BatchRenderer {
	  public:
		// Makes its pipeline (the base shader, from `baseShader`), its stream buffers and
		// their geometry on `renderBackend`, which must outlive it; destroys them again on
		// destruction.
		BatchRenderer(backend::Backend& renderBackend, const backend::ShaderSource& baseShader);
		~BatchRenderer();

		BatchRenderer(const BatchRenderer&) = delete;
		BatchRenderer& operator=(const BatchRenderer&) = delete;
		BatchRenderer(BatchRenderer&&) = delete;
		BatchRenderer& operator=(BatchRenderer&&) = delete;

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

		// Add a stroke as one draw group: its pieces (strokePolyline, strokeOutline), each
		// an SDF box quad padded by a pixel so its edge ramp is not cut off, and its flat
		// `triangles`, three points each (strokeOutline's wedges).
		void addStroke(
			std::span<const StrokePiece>	  pieces,
			std::span<const Foundation::Vec2> triangles,
			const Foundation::Color&		  color,
			float							  zIndex = 0.0F,
			Primitives::BlendMode			  blend = Primitives::BlendMode::Alpha
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

		// The emit order and draw runs of what has been added; flush() submits
		// exactly this. Groups are stable-sorted by z when any carries an explicit z
		// (submission order otherwise), and the emit order is cut into a new run
		// wherever a triangle needs a texture other than the bound one or a group's
		// GPU blend state differs. Shapes, arcs and the like need no texture and
		// never split.
		DrawPlan plan();

		// Upload the vertices and the planned indices, set the uniforms and submit
		// one draw call per run.
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

		// Local units per logical pixel under the current transform.
		float getLocalPixel() const { return pixelPadding; }

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
		std::span<const BatchVertex> getPendingVertices() const { return vertices; }

		// --- GPU Instancing (World-Space) ---

		/// Upload a tessellated mesh to GPU for instanced rendering.
		/// The mesh is uploaded once and reused for all instances.
		/// @param mesh Tessellated mesh to upload
		/// @param maxInstances Maximum number of instances to support (default 10000)
		/// @return Handle for subsequent draw calls
		InstancedMeshHandle uploadInstancedMesh(const renderer::TessellatedMesh& mesh, uint32_t maxInstances = 10000);

		/// Release an instanced mesh's buffers and geometry on `owner` (nothing to
		/// release when it is null, after shutdown) and leave the handle invalid.
		static void releaseInstancedMesh(backend::Backend* owner, InstancedMeshHandle& handle);

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

		/// u_bakedAlpha, the alpha the instanced and baked paths multiply in.
		void setBakedAlpha(float alpha);

		/// Bind the base shader's pipeline in its baked world-space mode (u_instanced = 2) with
		/// the camera uniforms and the projection from the setViewport() size, so a
		/// caller can draw its own world-space VAOs under it until endBakedPass().
		void beginBakedPass(const Primitives::WorldCamera& camera);
		void endBakedPass();

	  private:
		// Vertex data (CPU-side accumulation)
		std::vector<BatchVertex> vertices;
		std::vector<uint32_t>	 indices;

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

		// The backend and what this batch made on it.
		backend::Backend& gpu;
		PipelineHandle	  pipeline;
		BufferHandle	  vertexBuffer;
		BufferHandle	  indexBuffer;
		GeometryHandle	  geometry;

		// The uniforms as the last draw read them. Each draw path sets the fields it
		// uses and submits the whole block, so the others (u_bakedAlpha) carry over
		// between draws as GL uniforms did.
		backend::DrawUniforms uniforms;

		// flush()'s draw calls, reused.
		std::vector<backend::DrawCall> drawCalls;

		// Between beginBakedPass() and endBakedPass(): setBakedAlpha() submits the
		// uniforms at once, for the caller's next draw.
		bool bakedPassOpen = false;

		// Viewport dimensions
		int viewportWidth = 800;
		int viewportHeight = 600;

		// Coordinate system (optional, for DPI-aware rendering)
		CoordinateSystem* coordinateSystem = nullptr;

		// Default font atlas: bound for text quads added with an invalid atlas handle.
		TextureHandle defaultFontAtlas;
		float		  fontPixelRange = 4.0F;

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
