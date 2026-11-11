#pragma once

// Batch Renderer - Unified rendering for shapes and text.
//
// This is the internal batching implementation used by the Primitives API.
// It accumulates draw commands for both shapes (SDF) and text (MSDF) and
// renders them in a single pass with correct z-ordering.
//
// Also provides GPU instancing for efficient rendering of many identical meshes.
//
// It sits in front of the backend seam: it builds a BatchInstance per quad, the draw
// runs and the uniforms as plain data, and a backend::Backend uploads and draws them.

#include "Handle.h"
#include "backend/Backend.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "math/Types.h"
#include "metrics/GPUFrameTimer.h"
#include "primitives/InstanceData.h"
#include "primitives/Primitives.h"
#include "primitives/Stroke.h"
#include "vector/Tessellator.h"
#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <glm/mat2x2.hpp>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Renderer { // NOLINT(readability-identifier-naming)

	class CoordinateSystem;
	struct BoundaryLoops;

	// What addTriangles adds: an indexed mesh, in one colour or a colour per vertex, moved by
	// `offset` in its own units, and optionally its boundary loops (findBoundaryLoops) for a
	// feather ring `featherWidth` local units wide, or a device pixel at 0 (a shadow's is its blur).
	struct TriangleMeshArgs {
		std::span<const Foundation::Vec2> vertices;
		std::span<const uint16_t>		  indices;
		Foundation::Color				  color;			// Every vertex's, without `colors`
		const Foundation::Color*		  colors = nullptr; // One per vertex
		int32_t							  key = 0;
		Primitives::BlendMode			  blend = Primitives::BlendMode::Alpha;
		const BoundaryLoops*			  feather = nullptr;
		Foundation::Vec2				  offset{0.0F, 0.0F};
		float							  featherWidth = 0.0F;
	};

	// A text quad's edge: its outline grown by `spread` px and softened over `blur` px either side,
	// a shadow's or a halo's (Primitives::drawGlyphShadow); zero for a glyph's own.
	struct TextEdge {
		float spread = 0.0F;
		float blur = 0.0F;
	};

	// BatchInstance (backend/Backend.h), the batch's quad: its mode is how base.frag
	// shades it (base.vert and base.frag have the same table), and sets what its fields
	// hold. Every quad's fills are its colour, premultiplied, the same at each corner
	// unless the table says otherwise.
	//
	//   Flat    a triangle, its last corner repeated, or a quad; fills: each corner's colour
	//   Rect    geometry: half size, then the quad's half extent; radii: the corners' (tl, tr,
	//           br, bl); fills: each corner's, for a gradient, or with a fill hook its one fill
	//           then its three params; slot: its fill hook's index, 0 for none; border: the
	//           border's colour; shape.x: its width; flags: its position and its bare sides;
	//           a dashed border's shape.y: the dash, shape.z: the gap, negative for round caps,
	//           shape.w: the pattern's offset at the top-left corner's middle
	//   Shadow  geometry: half size, then the quad's half extent; radii: the corners'; shape.z: the blur
	//   Arc     geometry: inner and outer radius, half the sweep, the middle's angle; shape.y: how
	//           far the quad reaches past the outer radius
	//   Image   geometry: the uv rect, the top-left corner's then the bottom-right's
	//   Text    geometry: the uv rect; shape.x: a shadow's spread, shape.y: its blur, shape.z: the
	//           atlas's distance range in texels
	//   Stroke  a convex piece, a fan's quad; geometry: the rows of the map from a screen offset
	//           to the piece's frame; radii: its centre on screen; shape: its half size; border:
	//           its corner radius; all floats; fills: its colour in corner 0's, then when dashed
	//           the dash and gap as halves (the gap negative for round caps) and as a float the
	//           pattern's offset at its centre
	//
	// A rect's border dash and a stroke's share base.frag's cut, the rect's measured round its
	// outline and the stroke's along its piece's axis.
	//
	// Halves (radii, shape) are glm::packHalf2x16's, rgba8 (fills, border) glm::packUnorm4x8's.
	enum class QuadMode : uint8_t { Flat, Rect, Shadow, Arc, Image, Text, Stroke };

	// BatchInstance::flags
	constexpr uint8_t kQuadAdditive = 1U;	 // the colour adds to what is under it, its alpha 0
	constexpr uint8_t kQuadBorderShift = 1U; // a rect's border position from this bit, two bits: inside, centred, outside
	constexpr uint8_t kQuadBareShift = 3U;	 // a rect's sides with no border from this bit, four: top, right, bottom, left

	// A rect's corner radii, (top-left, top-right, bottom-right, bottom-left).
	using CornerRadii = std::array<float, 4>;

	// `radius` at each corner, but square beside a side `border` leaves bare when it has a width.
	CornerRadii rectCornerRadii(float radius, const std::optional<Foundation::BorderStyle>& border);

	// A dash pattern as the shader cuts it: `dash` drawn, then `gap` not, the pattern standing
	// `offset` in where the path's length is 0 (in [0, dash + gap)), each dash butt or round.
	// A gap of 0 is solid.
	struct ResolvedDash {
		float dash = 0.0F;
		float gap = 0.0F;
		float offset = 0.0F;
		bool  round = false;
	};

	// `pattern` on a stroke `width` wide along a path `length` long: a square cap as a butt
	// one a width longer and starting half a width early, and on a closed path the dash and
	// gap stretched together so a whole number of periods fits its length. Solid (gap 0)
	// when the pattern's gap is used up or anything is negative or not finite.
	ResolvedDash resolveDash(const Foundation::DashPattern& pattern, float width, float length, bool closed);

	// The length of a rect's border's centre line, the box half `halfSize` and its corners
	// `radii` moved out by `centre` (negative inward), as the shader measures it.
	float rectOutlineLength(Foundation::Vec2 halfSize, const CornerRadii& radii, float centre);

	// The GPU blend state a mode draws in. Output is premultiplied and blended
	// (ONE, ONE_MINUS_SRC_ALPHA); Additive is that state with the output alpha
	// zeroed in base.frag, so it shares Alpha's state and never splits a run.
	constexpr backend::BlendState blendState(Primitives::BlendMode /*mode*/) {
		return backend::BlendState::Alpha;
	}

	// One draw call: `instanceCount` quads from `instanceStart` in the emit order, in
	// one GPU blend state, binding up to kTextureSlots textures, which its quads pick
	// by their slot; the rest invalid, binding nothing.
	struct DrawRun {
		uint32_t										  instanceStart = 0;
		uint32_t										  instanceCount = 0;
		std::array<TextureHandle, backend::kTextureSlots> textures{};
		backend::BlendState								  blendState = backend::BlendState::Alpha;
	};

	// What flush() submits: the quads in emit order and the runs they split into.
	// Both point into the batch and stay valid until the next add or flush.
	struct DrawPlan {
		std::span<const BatchInstance> instances;
		std::span<const DrawRun>	   runs;
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

		// Add shape quad to batch (with optional SDF border and corner radii, rectCornerRadii's
		// for its border). When `gradient` is set, the quad's four corners are colored from the
		// gradient stops (interpolated by the GPU) instead of the flat fillColor;
		// the border/SDF/corner radius are unaffected. With `fill.hook`, one of this
		// batch's base shader's fill hooks, the hook gives the colour over fillColor and a
		// gradient is warned once per `id` and dropped; a hook from another shader is logged
		// once per `id` and the quad drawn as if it named none.
		void addQuad(
			const Foundation::Rect&							 bounds,
			const Foundation::Color&						 fillColor,
			const std::optional<Foundation::BorderStyle>&	 border = std::nullopt,
			const CornerRadii&								 radii = {},
			const std::optional<Foundation::LinearGradient>& gradient = std::nullopt,
			int32_t											 key = 0,
			Primitives::BlendMode							 blend = Primitives::BlendMode::Alpha,
			const Primitives::Fill&							 fill = {},
			const char*										 id = nullptr
		);

		// Whether a quad added since the last flush names `hook`, one of the base shader's fill
		// hooks: then its block's new values would reach that quad, drawn before them.
		bool holdsFill(std::string_view hook) const;

		// Add an outer box-shadow / glow quad behind a rect, or a circle (its radius at every
		// corner). The SDF shader renders it as a soft falloff over `shadow.blur` px either
		// side of the (spread-grown) shape's edge, each radius grown by the spread too. Emit
		// before the element's own quad so it sits behind.
		void addShadowQuad(
			const Foundation::Rect&		 bounds,
			const Foundation::BoxShadow& shadow,
			const CornerRadii&			 radii,
			int32_t						 key = 0,
			Primitives::BlendMode		 blend = Primitives::BlendMode::Alpha
		);

		// Add an indexed triangle mesh as one draw group (drawTriangles, drawLine, a
		// polygon's fill and shadow), a flat quad a triangle. With `mesh.feather`, a ring
		// mesh.featherWidth wide (one device pixel at 0) runs outside each loop in the same
		// group, each boundary vertex's colour fading to alpha 0 across it, a linear coverage
		// ramp. Its outer edge sits on the miter of the vertex's two edges, at most 4 widths
		// out, outward through any transform, mirrored or not. A ring wider than an edge is
		// long folds over itself at that edge's notch.
		void addTriangles(const TriangleMeshArgs& mesh);

		// Add a stroke as one draw group: its pieces (strokePolyline, strokeOutline), each
		// its polygon as a fan of quads, shaded by its SDF box and cut by `dash` along it.
		void addStroke(
			std::span<const StrokePiece> pieces,
			const Foundation::Color&	 color,
			int32_t						 key = 0,
			Primitives::BlendMode		 blend = Primitives::BlendMode::Alpha,
			const ResolvedDash&			 dash = {}
		);

		// Add a ring, arc or pie as one SDF quad (see Primitives::ArcArgs).
		void addArc(const Primitives::ArcArgs& args);

		// Add a textured quad sampling args.texture (see Primitives::ImageArgs). An
		// invalid texture adds nothing, and so does a stale one, warned once.
		void addImageQuad(const Primitives::ImageArgs& args);

		// --- Text rendering (MSDF) ---

		// Add text glyph quad to batch
		// position: screen-space top-left of glyph
		// size: glyph dimensions in screen pixels
		// uvMin/uvMax: texture coordinates in MSDF atlas
		// color: text color with alpha
		// runOrigin: pen origin of the text run at the baseline (same value for
		//   every glyph of a run, i.e. FontRenderer::GlyphQuad::runOrigin). Under
		//   a transform that keeps the axes (identity, a translation, an upright
		//   scale with one) the run origin is snapped to the device pixel grid
		//   and every glyph shifted by the same delta, keeping glyph edges stable
		//   without disturbing advances/kerning.
		// atlas: MSDF atlas this glyph samples from. An invalid handle selects the
		//   default atlas set via setFontAtlas(), the game's default family's. A
		//   frame may mix atlases; plan() gives each glyph its atlas's texture slot in
		//   its draw. A stale atlas adds nothing, warned once.
		// pixelRange: that atlas's distance range in texels, which the shader turns
		//   into the edge width. With an invalid atlas, setFontAtlas()'s range instead.
		// edge: a shadow's spread and blur, which the shader fits inside the field.
		void addTextQuad(
			const Foundation::Vec2&	 position,
			const Foundation::Vec2&	 size,
			const Foundation::Vec2&	 uvMin,
			const Foundation::Vec2&	 uvMax,
			const Foundation::Color& color,
			const Foundation::Vec2&	 runOrigin,
			TextureHandle			 atlas,
			float					 pixelRange,
			int32_t					 key = 0,
			Primitives::BlendMode	 blend = Primitives::BlendMode::Alpha,
			const TextEdge&			 edge = {}
		);

		// Set the default MSDF font atlas and its distance range, which text quads
		// added with an invalid atlas handle sample.
		void setFontAtlas(TextureHandle atlas, float pixelRange = 4.0F);

		// --- Textures ---

		// Free a texture its owner let go. Draws added from here that sample it add
		// nothing; one already added still samples it, so with any draw pending the
		// free waits for the end of the next flush.
		void releaseTexture(TextureHandle texture);

		// --- Rendering ---

		// The emit order and draw runs of what has been added; flush() submits
		// exactly this. Groups are stable-sorted by sort domain, then key, when any
		// carries a non-zero key (submission order otherwise), and the emit order is
		// cut into a new run where a quad needs a texture when the run already binds
		// kTextureSlots others, or where a group's GPU blend state differs. Each
		// textured quad's slot is set to its texture's in its run. Shapes, arcs and the
		// like need no texture and never split.
		DrawPlan plan();

		// Start a new sort domain: everything added from here sorts after everything
		// added before, whatever its key. No flush, and no run is cut for it.
		void sortBarrier() { ++sortDomain; }

		// Upload the quads in emit order, set the uniforms and submit one draw call
		// per run.
		void flush();

		// Frame lifecycle. beginFrameStats() starts the frame's statistics and GPU frame; beginFrame()
		// starts a batch, which a frame may hold several of (an offscreen pass inside it).
		void beginFrameStats();
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

		// Clip every add from here to `bounds`, (minX, minY, maxX, maxY) in screen
		// coordinates. Bounds with no area are the empty clip: every add is culled,
		// adding nothing, until the clip changes.
		void setClipBounds(const Foundation::Vec4& bounds);

		// Clear clip bounds (disables clipping)
		void clearClipBounds();

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

		// The local length that spans at most a device pixel in any direction under the
		// current transform (which stretches most along its largest singular value) and the
		// coordinate system's pixel ratio.
		float getLocalDevicePixel() const;

		// Rendering statistics structure
		struct RenderStats {
			uint32_t drawCalls = 0;
			uint32_t vertexCount = 0;
			uint32_t triangleCount = 0;
			uint32_t instanceCount = 0;
			uint64_t uploadBytes = 0;
			uint32_t flushCount = 0;
			float	 flushMs = 0.0F;
		};

		// GPU timing of flush(), drawInstanced(), the baked pass and whatever a caller brackets with
		// begin/endGpuSpan() (Primitives::clear), as GPUFrameTimer spans, off until turned on.
		void				 setGpuTiming(bool enabled);
		bool				 getGpuTiming() const { return gpuTiming; }
		void				 beginGpuSpan();
		void				 endGpuSpan();
		const GPUFrameTimer& getGpuTimer() const { return gpuTimer; }

		// Statistics
		size_t		getInstanceCount() const { return instances.size(); }
		size_t		getDrawGroupCount() const { return drawGroups.size(); } // the key sort's units, since the last flush
		size_t		getDrawCallCount() const { return drawCallCount; }
		RenderStats getStats() const;

		// The quads added since the last flush, in the order added.
		std::span<const BatchInstance> getPendingInstances() const { return instances; }

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
		// The quads added since the last flush, in the order added.
		std::vector<BatchInstance> instances;

		// Per-quad texture tag, parallel to `instances`. Invalid = no texture
		// requirement (shapes, arcs); valid = the MSDF atlas a glyph or the texture an
		// image samples. plan() splits runs on these.
		std::vector<TextureHandle> instanceTextures;

		// Per-draw-call groups. Each add* records one {instanceStart, instanceCount,
		// order, blend}, or extends the last when it has the same order and blend (every
		// glyph of a text run after the first). `order` is the domain, the count of
		// sortBarrier() calls since the last flush, in the high word and the key with
		// its sign bit flipped in the low one, so one unsigned compare sorts by domain,
		// then key, negative keys first. plan() keeps submission order unless some
		// group carries a non-zero key, in which case groups are stable-sorted by
		// order and their quads copied out in that order.
		struct DrawGroup {
			uint32_t			  instanceStart;
			uint32_t			  instanceCount;
			uint64_t			  order;
			Primitives::BlendMode blend;
		};
		std::vector<DrawGroup> drawGroups;
		bool				   anyKey = false;
		uint32_t			   sortDomain = 0;

		// plan()'s output, reused across flushes: the quads in key order and their
		// textures (unused in submission order), and the runs.
		std::vector<BatchInstance> sortedInstances;
		std::vector<TextureHandle> sortedTextures;
		std::vector<DrawRun>	   runs;

		// Record the quads [groupStart, current end) as one draw group.
		void recordGroup(uint32_t groupStart, int32_t key, Primitives::BlendMode blend);

		// Append a quad of `mode` over `corners` (TL, TR, BR, BL, before the transform),
		// drawn with `blend` and sampling `texture`, with the current clip; the caller
		// fills in the rest.
		BatchInstance&
		pushQuad(QuadMode mode, const Foundation::Vec2 (&corners)[4], Primitives::BlendMode blend, TextureHandle texture = {});

		// The same with corners already on screen, as a mesh's and a stroke's are.
		BatchInstance&
		pushScreenQuad(QuadMode mode, const Foundation::Vec2 (&corners)[4], Primitives::BlendMode blend, TextureHandle texture = {});

		// Scratch for addTriangles: the mesh's positions on screen and colours.
		std::vector<Foundation::Vec2> meshPositions;
		std::vector<uint32_t>		  meshColors;

		// Whether a draw may sample `texture`, a valid handle: false, warned once, when
		// it is stale or waiting to be freed.
		bool sampleable(TextureHandle texture);

		// Destroy the textures whose frees waited for a flush.
		void freePending();

		// Textures released while draws were pending, freed at the end of the next flush.
		std::vector<TextureHandle> pendingFrees;

		// The last texture sampleable() found live; releaseTexture() forgets it, so a
		// text run checks its atlas with the backend once.
		TextureHandle liveTexture;

		bool warnedStale = false;

		// The base shader's fill hooks by index, fillHooks[i] index i + 1's name, which a rect's
		// Fill is checked against; the indexes the quads added since the last flush name; and
		// the ids addQuad warned for, once each.
		std::vector<std::string> fillHooks;
		std::bitset<256>		 pendingFills;
		std::set<std::string>	 warnedGradientIds;
		std::set<std::string>	 warnedForeignFillIds;

		// The index `hook` has in the base shader, or 0, logged once per `id`, when it is another's.
		uint8_t fillIndex(const backend::FillHook& hook, const char* id);

		// addTriangles' feather ring along `loops`, over meshPositions and meshColors, `width`
		// logical pixels wide on screen.
		void addFeather(const BoundaryLoops& loops, float width, Primitives::BlendMode blend);

		// The backend and what this batch made on it.
		backend::Backend& gpu;
		PipelineHandle	  pipeline;
		BufferHandle	  instanceBuffer;
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

		// The empty clip is set: every add returns before adding anything.
		bool clipEmpty = false;

		// Classification of currentTransform, cached once per setTransform() to
		// avoid per-vertex checks. Text snapping runs for Identity, TranslateOnly
		// and ScaleTranslate (a positive scale on each axis and a translation, which
		// is what a ui scale is), where "on the pixel grid" is well-defined; General
		// (rotation, shear, a flip) falls back to the full matrix multiply with no
		// snapping.
		enum class TransformClass : uint8_t { kIdentity, kTranslateOnly, kScaleTranslate, kGeneral };

		// Current transform matrix (baked into vertex positions at add-time)
		Foundation::Mat4 currentTransform{1.0F}; // Identity
		TransformClass	 transformClass = TransformClass::kIdentity;
		Foundation::Vec2 transformTranslation{0.0F, 0.0F}; // Valid for every class but kGeneral (zero for kIdentity)
		Foundation::Vec2 transformScale{1.0F, 1.0F};	   // Valid for kScaleTranslate; 1 for kIdentity and kTranslateOnly

		// The inverse of currentTransform's linear part, which takes a stroke's screen
		// offsets back to its own units; a transform that flattens everything has none,
		// and draws no stroke.
		glm::mat2 inverseLinear{1.0F};
		bool	  invertible = true;

		// addFeather's outer points, one a boundary vertex of the loop it is on.
		std::vector<Foundation::Vec2> featherOuter;

		// Local units per logical pixel under currentTransform (the inverse of its
		// area scale), the padding SDF quads get so their edge ramp fits.
		float pixelPadding = 1.0F;

		// currentTransform's largest singular value: how far it stretches a unit at most.
		float largestScale = 1.0F;

		// Caches addTextQuad's run-origin snap: every glyph of a text run shares the
		// same {pixelRatio, scale, translation, runOrigin}, so the delta is computed once
		// per run instead of per glyph. pixelRatio 0 marks the cache empty.
		struct TextSnapCache {
			float			 pixelRatio = 0.0F;
			Foundation::Vec2 scale{1.0F, 1.0F};
			Foundation::Vec2 translation{0.0F, 0.0F};
			Foundation::Vec2 runOrigin{0.0F, 0.0F};
			Foundation::Vec2 delta{0.0F, 0.0F};
		};
		TextSnapCache textSnapCache;

		// Statistics
		size_t	 drawCallCount = 0;
		size_t	 frameVertexCount = 0;	 // Cumulative vertex count for the frame
		size_t	 frameTriangleCount = 0; // Cumulative triangle count for the frame
		size_t	 frameInstanceCount = 0; // The batch's quads and drawInstanced's mesh instances this frame
		uint64_t frameUploadBytes = 0;	 // Quad and mesh instance bytes written to the backend this frame
		size_t	 frameFlushCount = 0;	 // Flushes that submitted something this frame
		double	 frameFlushSeconds = 0.0;

		GPUFrameTimer gpuTimer;
		bool		  gpuTiming = false;
	};

} // namespace Renderer
