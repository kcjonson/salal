#pragma once

// The seam between the renderer and a GPU API (docs/renderer-boundary.md). The
// front end (BatchRenderer, Primitives, RenderTarget, GPUTimer) builds plain data
// and hands it here; a backend owns every GPU object behind a handle and issues
// the calls. backend/gl/ is OpenGL 3.3 core, the only GPU backend; NullBackend
// records what it is asked and touches no GPU, for headless tests.
//
// GL-free, like every header under backend/ outside backend/gl/.

#include "Handle.h"
#include "Texture.h"
#include "graphics/Color.h"
#include "math/Types.h"
#include <cstddef>
#include <cstdint>
#include <span>

namespace Renderer { // NOLINT(readability-identifier-naming)

	// The batched vertex (VertexLayout::Uber), attributes 0 to 5 of uber.vert:
	//   0 position (vec2), 1 texCoord (vec2): UV for text and images, rect-local for
	//   shapes, arc-local for arcs; 2 color (vec4); 3 data1 (vec4): borderData for
	//   shapes, blur for shadows; 4 data2 (vec4): per-mode parameters, the render mode
	//   in w; 5 clipBounds (vec4): (minX, minY, maxX, maxY), or zero for no clip.
	struct UberVertex {
		Foundation::Vec2 position;	 // Screen-space position
		Foundation::Vec2 texCoord;	 // UV for text and images, rectLocalPos for shapes
		Foundation::Vec4 color;		 // Fill color RGBA, straight alpha
		Foundation::Vec4 data1;		 // Border: (color.rgb, width) for shapes, (blur, 0, 0, 0) for shadows
		Foundation::Vec4 data2;		 // xyz per mode, w the mode (BatchRenderer.h has the table). Shape: (halfW, halfH, cornerRadius,
									 // borderPos); text: (pixelRange, 0, 0, w); shadow: (halfW, halfH, cornerRadius, w);
									 // arc: (innerR, outerR, sweep / 2, w); image: (0, 0, 0, w)
		Foundation::Vec4 clipBounds; // Clip rect (minX, minY, maxX, maxY), or (0,0,0,0) for no clipping
	};
	static_assert(sizeof(UberVertex) == 80, "UberVertex is the 80-byte stride of the Uber layout");

	// An instanced mesh vertex (VertexLayout::InstancedMesh), attributes 0 and 2; the
	// per-instance InstanceData (primitives/InstanceData.h) supplies 6 and 7.
	struct InstancedMeshVertex {
		Foundation::Vec2  position; // Local mesh position, transformed by the instance
		Foundation::Color color;	// Vertex color, multiplied by the instance tint
	};

} // namespace Renderer

namespace Renderer::backend {

	// A geometry's vertex format, which fixes its index type: Uber is UberVertex with
	// uint32 indices; InstancedMesh is InstancedMeshVertex with uint16 indices, plus
	// an InstanceData per instance.
	enum class VertexLayout : uint8_t { Uber, InstancedMesh };

	// How a buffer is written after it is made. Static: never. Dynamic: in place, the
	// write ordered after every draw already submitted, which still reads the old
	// contents (drawInstanced rewrites one instance buffer between draws and relies on
	// it). Stream: whole, once per submission; a Stream write at offset 0 starts the
	// buffer over (what it held is discarded, never waited on) and grows it to fit.
	enum class BufferUsage : uint8_t { Static, Dynamic, Stream };

	// The GPU blend state a draw runs in. One: premultiplied "over", (ONE,
	// ONE_MINUS_SRC_ALPHA), which additive draws share by writing alpha 0 (see
	// BatchRenderer.h), so mixing the two never splits a draw.
	enum class BlendState : uint8_t { Alpha };

	// A pipeline's shader files under shaders/, which the backend loads and compiles.
	struct ShaderSource {
		const char* vertex = nullptr;
		const char* fragment = nullptr;
	};

	// Buffers to draw from in `layout`; `instances` only for InstancedMesh.
	struct GeometryDesc {
		VertexLayout layout = VertexLayout::Uber;
		BufferHandle vertices;
		BufferHandle indices;
		BufferHandle instances;
	};

	// One draw: `indexCount` indices from `indexOffset` (counted in indices) of the
	// geometry, triangles, `instanceCount` times when it is not zero. `texture` is
	// bound to unit 0, which u_atlas samples; an invalid one binds nothing.
	struct DrawCall {
		PipelineHandle pipeline;
		GeometryHandle geometry;
		BlendState	   blend = BlendState::Alpha;
		TextureHandle  texture;
		uint32_t	   indexOffset = 0;
		uint32_t	   indexCount = 0;
		uint32_t	   instanceCount = 0;
	};

	// The uniform block's name in the shaders.
	constexpr const char* kDrawUniformsBlock = "DrawUniforms";

	// The per-draw uniforms: the std140 block DrawUniforms in
	// shaders/includes/uniforms.glsl, member for member, padded to the block's 208
	// bytes. In this order std140 puts no padding between members (mat4 on 16 bytes,
	// vec2 on 8, scalars on 4); the offsets are asserted below and against the linked
	// program in GLBackend.test.cpp. Change the two together.
	struct DrawUniforms {
		Foundation::Mat4 projection{1.0F};
		Foundation::Mat4 transform{1.0F};
		Foundation::Vec2 cameraPosition{0.0F, 0.0F}; // world position at the viewport's centre
		Foundation::Vec2 viewportSize{0.0F, 0.0F};	 // logical pixels, for the world-space modes
		Foundation::Vec2 cursorWorld{0.0F, 0.0F};	 // groundcover interaction centre
		float			 cameraZoom = 0.0F;
		float			 pixelsPerMeter = 0.0F;
		float			 bakedAlpha = 1.0F;		// multiplies instanced and baked draws
		float			 viewportHeight = 0.0F; // framebuffer pixels, for the clip test's y flip
		float			 pixelRatio = 0.0F;		// framebuffer pixels per logical pixel
		int32_t			 instanced = 0;			// 0 batched, 1 instanced, 2 baked world space
		int32_t			 groundcoverMode = 0;	// 1 applies the groundcover deform
		float			 groundcoverOpenness = 0.0F;
		float			 groundcoverReach = 0.0F;
		float			 cursorRadius = 0.0F;
		float			 cursorStrength = 0.0F;
		float			 padding[3] = {}; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
	};
	static_assert(offsetof(DrawUniforms, projection) == 0);
	static_assert(offsetof(DrawUniforms, transform) == 64);
	static_assert(offsetof(DrawUniforms, cameraPosition) == 128);
	static_assert(offsetof(DrawUniforms, viewportSize) == 136);
	static_assert(offsetof(DrawUniforms, cursorWorld) == 144);
	static_assert(offsetof(DrawUniforms, cameraZoom) == 152);
	static_assert(offsetof(DrawUniforms, pixelsPerMeter) == 156);
	static_assert(offsetof(DrawUniforms, bakedAlpha) == 160);
	static_assert(offsetof(DrawUniforms, viewportHeight) == 164);
	static_assert(offsetof(DrawUniforms, pixelRatio) == 168);
	static_assert(offsetof(DrawUniforms, instanced) == 172);
	static_assert(offsetof(DrawUniforms, groundcoverMode) == 176);
	static_assert(offsetof(DrawUniforms, groundcoverOpenness) == 180);
	static_assert(offsetof(DrawUniforms, groundcoverReach) == 184);
	static_assert(offsetof(DrawUniforms, cursorRadius) == 188);
	static_assert(offsetof(DrawUniforms, cursorStrength) == 192);
	static_assert(sizeof(DrawUniforms) == 208);

	// One per GPU context. Every handle it returns is invalid when the object could
	// not be made (logged), and every call taking a stale or invalid handle does
	// nothing.
	//
	// State: depth test, culling and scissor are off for every draw, fixed state set
	// at creation, beginFrame() and invalidateState(). Bindings (pipeline, geometry,
	// texture, blend) are made by each draw() and assumed to survive nothing outside
	// it, because a game's own GPU code runs between the renderer's submissions. The
	// one exception is the uniforms: setUniforms() binds them to uniform binding point
	// 0, which is the renderer's, and draws read them there, so a game's GL that binds
	// a uniform buffer to point 0 calls invalidateState() before the renderer draws.
	class Backend {
	  public:
		Backend() = default;
		virtual ~Backend() = default;

		Backend(const Backend&) = delete;
		Backend& operator=(const Backend&) = delete;
		Backend(Backend&&) = delete;
		Backend& operator=(Backend&&) = delete;

		// --- Frame and state ---

		virtual void beginFrame() = 0;
		virtual void endFrame() = 0;

		// The window's framebuffer size in pixels: the viewport setRenderTarget({})
		// restores and the size readPixels({}) reads.
		virtual void setWindowSize(int width, int height) = 0;

		// Clear the bound target's colour to `color` exactly (the caller premultiplies).
		virtual void clear(const Foundation::Color& color) = 0;

		// GPU code outside the renderer changed the fixed state or uniform binding point
		// 0; set both again.
		virtual void invalidateState() = 0;

		// --- Textures ---

		virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
		// Replace the rectangle with `pixels` in the texture's format; a rectangle
		// outside the texture is refused.
		virtual void updateTexture(TextureHandle texture, int x, int y, int width, int height, const void* pixels) = 0;
		virtual void destroyTexture(TextureHandle texture) = 0;

		// --- Render targets ---

		// A target drawing into `colour`, which stays the caller's to destroy (after the target).
		virtual RenderTargetHandle createRenderTarget(TextureHandle colour) = 0;
		// Bind `target`, {} for the window, with a viewport over all of it.
		virtual void setRenderTarget(RenderTargetHandle target) = 0;
		// The target's colour ({} the window's) as RGBA8, bottom row first, into
		// `rgba`, which holds width * height * 4 bytes or nothing is read. Synchronous
		// by design: it waits for the GPU. The bound target stays bound.
		virtual void readPixels(RenderTargetHandle target, std::span<uint8_t> rgba) = 0;
		virtual void destroyRenderTarget(RenderTargetHandle target) = 0;

		// --- Buffers and geometry ---

		// `bytes` of storage, filled from `data` when it is given.
		virtual BufferHandle createBuffer(BufferUsage usage, size_t bytes, const void* data) = 0;
		// Write `bytes` of `data` at `offset`; see BufferUsage for Stream. Any other
		// write must fit the buffer.
		virtual void updateBuffer(BufferHandle buffer, size_t offset, size_t bytes, const void* data) = 0;
		virtual void destroyBuffer(BufferHandle buffer) = 0;

		// Destroy a geometry before its buffers.
		virtual GeometryHandle createGeometry(const GeometryDesc& desc) = 0;
		virtual void		   destroyGeometry(GeometryHandle geometry) = 0;

		// --- Pipelines and drawing ---

		// The program from `source`, its DrawUniforms block bound to the buffer
		// setUniforms() writes and u_atlas to unit 0. Uber is the only one.
		virtual PipelineHandle createPipeline(const ShaderSource& source) = 0;
		virtual void		   destroyPipeline(PipelineHandle pipeline) = 0;

		// The uniforms every draw reads from here on, until the next call. A draw still
		// in flight keeps the values it was given: new values take fresh storage, and a
		// repeat of the last values reuses theirs.
		virtual void setUniforms(const DrawUniforms& uniforms) = 0;

		// One submission, in order: bindings change only where consecutive calls
		// differ, and nothing bound is left behind but the pipeline and the uniforms on
		// point 0 (and, on GL, the generic buffer bindings its writes used).
		virtual void draw(std::span<const DrawCall> calls) = 0;

		// Bind `pipeline` in `blend` for draws the caller issues itself, from its own
		// vertex arrays, until endForeignDraws(): worldsim's baked chunk meshes under
		// the uber program (docs/renderer-boundary.md, question 2). setUniforms()
		// between those draws applies from the next one.
		virtual void beginForeignDraws(PipelineHandle pipeline, BlendState blend) = 0;
		virtual void endForeignDraws() = 0;

		// --- Timestamp queries ---

		virtual QueryHandle createTimestampQuery() = 0;
		// The GPU's clock once every command before this one has run.
		virtual void	 writeTimestamp(QueryHandle query) = 0;
		virtual bool	 timestampReady(QueryHandle query) = 0;
		virtual uint64_t timestampNs(QueryHandle query) = 0; // waits for the GPU unless timestampReady()
		virtual void	 destroyTimestampQuery(QueryHandle query) = 0;
	};

	// The backend Primitives::init() was given; null before init() and after
	// shutdown(). The renderer's own objects (RenderTarget, GPUTimer) reach it here.
	Backend* active();

} // namespace Renderer::backend
