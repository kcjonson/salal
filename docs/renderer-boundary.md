# Renderer backend boundary

The seam between `renderer` and OpenGL: what leaks today, what the public surface
becomes, what sits behind it, and the PRs that get there. Written 2026-10-26 from
worldsim @ `2cc7e40a` (the imported renderer and its callers); decision G in
[open-decisions.md](open-decisions.md) has the reasoning. All four PRs are in, and the
base shader's build-time hooks followed them ([shader-hooks.md](shader-hooks.md)). The
leak table under "The public surface today" and the table of what the renderer
asked of GL under "The backend seam" are the state it started from, kept as the
record of what moved.

## Goal and non-goals

GL types and GL calls stay inside `libs/renderer`; consumers (`ui`, `app`, the
games) see renderer-owned handles and value types. OpenGL 3.3 core is the first
and only backend; Vulkan is possible later and not scheduled.

A boundary, not a port: same shaders, same batching, same draw calls, worldsim's
screenshots do not change. Out of scope: the game's own GL code (worldsim's chunk
and planet renderers, `GlobeView` and the `SurfaceFieldGolden` test: 346 GL
calls, about 480 counting everything outside the renderer), worldsim's until it
chooses to come through the engine; and the `glClearColor`/`glClear` pair opening
about forty worldsim scenes, which include `<GL/glew.h>` themselves.

## The public surface today

Every place a renderer header hands GL or GLFW to an includer, with worldsim's
users at `2cc7e40a`. `libs/ui` makes no GL call of its own.

| header | symbol | leak | worldsim users |
|---|---|---|---|
| `primitives/BatchRenderer.h` | `#include <GL/glew.h>`, plus `gl/GLBuffer.h`, `gl/GLVertexArray.h`, `shader/Shader.h` for members | header include | 8 includers: `ui/shapes/Shapes.cpp`, `ui/components/TextInput/TextInput.cpp`, `engine/world/rendering/{BakedChunkRenderer,ChunkRenderer,InstancedEntityRenderer}.cpp` and the grass renderer beside them, `ui-sandbox/scenes/{GrassScene,SdfMinimalScene}.cpp` |
| | `getShaderProgram()` | returns `GLuint` | `BakedChunkRenderer.cpp:182`, the grass renderer's `:145`, `GrassScene.cpp:194`, each followed by `glUseProgram` and `glUniform*` |
| | `addTextQuad(..., GLuint atlasTexture = 0, ...)` | `GLuint` parameter | `Shapes.cpp:222`, `TextInput.cpp:583`, `:710`, `SdfMinimalScene.cpp:97` (all leave the default); `Primitives.cpp:387` passes the family atlas |
| | `setFontAtlas(GLuint, float)` | `GLuint` parameter | through `Primitives::setFontAtlas`, `AppLauncher.cpp:195` |
| | `uploadInstancedMesh`, `drawInstanced`, `releaseInstancedMesh` | take `InstancedMeshHandle`, a struct of GL wrappers | the grass renderer's `:75` and `:182`, `InstancedEntityRenderer.cpp:32`, `:121`, `GrassScene.cpp:151`, `:209` |
| `primitives/InstanceData.h` | `#include <GL/glew.h>`; `InstancedMeshHandle{GLVertexArray vao; GLBuffer meshVBO, meshIBO, instanceVBO}` | header include, GL members | the grass renderer's header (`:12`), `InstancedEntityRenderer.h:9`, `GrassScene.cpp:17` |
| `primitives/Primitives.h` | `getBatchRenderer()` | returns the GL-typed class | 14 call sites in the 8 files above |
| | `setFontAtlas(unsigned int, float)` | GL texture name as `unsigned` | `AppLauncher.cpp:195` |
| | `setTileAtlas(unsigned int, rects)`, `getTileAtlasTexture()` | GL texture name as `unsigned` | `world-sim/GameBootstrap.cpp:28`, `ChunkRenderer.cpp:537` |
| `font/FontRenderer.h` | `#include <GL/glew.h>` and an unused `shader/Shader.h`; `Atlas::texture` | header include, `GLuint` member | 20 includers: 12 in `libs/ui` (`Shapes`, `TextInput`, `Avatar`, `Badge`, `Button`, `Divider`, `KeyCap`, `ProgressBar`, `SegmentedControl`, `Slider`, `Stat`, `TabBar`), 7 in `apps`, `AppLauncher.cpp`; none uses GL, they measure text and build glyph quads |
| | `getAtlasTexture(FontFamily)` | returns `GLuint` | `AppLauncher.cpp:195`; `Primitives.cpp:352` |
| `shader/Shader.h` | `#include <GL/glew.h>`; `getProgram()` | `GLuint` | `ChunkRenderer.h:19`, `:134` (its own tile shader, game GL); `BatchRenderer.h` member |
| `shader/ShaderLoader.h` | `loadShaderProgram()`, `compileShader(GLenum, ...)` | `GLuint`, `GLenum` | `planet-view/PlanetRenderer.cpp:34`, `:40` (game GL) |
| `gl/GLBuffer.h`, `GLVertexArray.h`, `GLTexture.h`, `GLFramebuffer.h`, `GLQuery.h` | the RAII wrappers, each with `operator GLuint()` | GL by design | `ChunkRenderer.h:16-18`, `:70-142`, `BakedChunkRenderer.h:10-11`, `:81-83`, `ChunkRenderer.cpp:152` (game GL) |
| `resources/RenderToTexture.h` | `#include <GL/glew.h>`; `texture()`; `prevViewport_[4]`, `prevFbo_` | `GLuint` return, `GLint` members | `engine/assets/AssetRenderer.cpp:53` (`begin`/`end`, then its own `glReadPixels`) |
| `resources/TileTextureAtlas.h` | `#include <GL/glew.h>`; `texture()` | `GLuint` | through `TileAtlasBuilder` only |
| `resources/TileAtlasBuilder.h` | `#include <GL/glew.h>`; `texture()` | `GLuint` | `GameBootstrap.cpp:28` |
| `metrics/GPUTimer.h` | `#include "gl/GLQuery.h"`; `Span{GLQuery start, stop}` | header include, GL members | `world-sim/scenes/game/GameScene.cpp:33`, `:1978-1979` |
| `CoordinateSystem/CoordinateSystem.h` | `Initialize(GLFWwindow*)`, `GLFWwindow* window`; `setFullViewport()` calls `glViewport` | platform handle | `AppLauncher.cpp:168`; `:105` calls `updateWindowSize`, whose arguments are ignored; nobody calls `setFullViewport` |

Nineteen leaks in sixteen header files. Behind them the renderer makes 182 GL
calls in twelve files, 134 of them in `BatchRenderer.cpp`, `Shader.cpp` and
`ShaderLoader.cpp`.

## The target surface

Declarations, not code. `Primitives` keeps its API except where it hands out GL.

```cpp
namespace Renderer {
// Tagged handle, 16-bit index + 16-bit generation as in resources/ResourceHandle.h, which stays only because worldsim's ui-sandbox HandleScene still exercises it with ResourceManager; both go with that scene.
template <typename Tag> struct Handle { uint32_t value = 0xFFFFFFFF; bool isValid() const; bool operator==(Handle) const = default; };
using TextureHandle = Handle<struct TextureTag>; // BufferHandle, GeometryHandle, RenderTargetHandle, PipelineHandle, QueryHandle likewise, PR 4
enum class TextureFormat : uint8_t { Rgba8, Rgb8, Rgba16F, Rgba32F };
struct TextureDesc { int width = 0; int height = 0; TextureFormat format = TextureFormat::Rgba8; bool linear = true; const void* pixels = nullptr; };
// Replaces Initialize(GLFWwindow*): app's resize callbacks feed it, nothing in renderer polls GLFW, the test needs no window.
struct Viewport { int logicalWidth = 0; int logicalHeight = 0; int framebufferWidth = 0; int framebufferHeight = 0; float pixelRatio() const; };
class CoordinateSystem { // the rest as today; updateWindowSize and setFullViewport go
	void setViewport(const Viewport&);
	const Viewport& viewport() const;
};
// The first backend object, replacing RenderToTexture: the renderer keeps its own stack of bound targets, so begin/end rebind from it, not glGetIntegerv.
class RenderTarget {
	static RenderTarget create(int width, int height, TextureFormat format = TextureFormat::Rgba8);
	void begin();
	void end();
	TextureHandle texture() const; // plus width() and height()
	void readPixels(std::span<uint8_t> out) const; // synchronous by design; what AssetRenderer and the screenshot route do by hand
};
// Move-only; its destructor calls Primitives::releaseInstancedMesh (GrassScene.cpp:292 never releases today). No GL in the header.
class InstancedMeshHandle;
class BatchRenderer { // renderer-internal after PR 1; getShaderProgram() deleted
	void addTextQuad(Vec2 pos, Vec2 size, Vec2 uvMin, Vec2 uvMax, Color color, Vec2 runOrigin, TextureHandle atlas = {}, float zIndex = 0.0F);
	void setFontAtlas(TextureHandle atlas, float pixelRange = 4.0F);
};
class FontRenderer { TextureHandle getAtlasTexture(FontFamily family = FontFamily::Default) const; }; // header drops <GL/glew.h> and the unused shader/Shader.h
class GPUTimer; // public API unchanged; the spans move behind a pimpl so the header drops gl/GLQuery.h
namespace Primitives {
	void setFontAtlas(TextureHandle atlas, float pixelRange = 4.0F); // the same three calls, handle type
	void setTileAtlas(TextureHandle atlas, const std::vector<glm::vec4>& rects);
	TextureHandle getTileAtlasTexture();
	TextureHandle createTexture(const TextureDesc&); // PR 2: every texture through here, and so through gl/GLTexture
	void updateTexture(TextureHandle, int x, int y, int width, int height, const void* pixels);
	void destroyTexture(TextureHandle);
	void clear(Color); // PR 1 adds it and migrates no caller; scenes and apps move to it as they are touched (AssetRenderer in PR 2)
	void invalidateState(); // PR 4: a foreign GL pass that leaves depth test, culling or scissor on, or binds uniform point 0, says so; the renderer never queries state (dual-deck-builder R2.15)
	void readBackbuffer(std::span<uint8_t> rgba, int& width, int& height); // PR 2: the screenshot route
	struct WorldCamera { Vec2 position; float zoom = 1.0F; float pixelsPerMeter = 1.0F; }; // from here down: what replaces getBatchRenderer() and getShaderProgram()
	void drawGlyphs(const FontRenderer::GlyphQuad* quads, size_t count, float zIndex = 0.0F, BlendMode blend = BlendMode::Alpha); // colour, atlas and the atlas's distance range from each quad; this sketch took a family too, until a caller drew one family's quads with another's atlas (v0.9.0), and PR 3 added the blend
	InstancedMeshHandle uploadInstancedMesh(const renderer::TessellatedMesh& mesh, uint32_t maxInstances = 10000);
	void releaseInstancedMesh(InstancedMeshHandle&);
	void drawInstanced(const InstancedMeshHandle&, const InstanceData* instances, uint32_t count, const WorldCamera&);
	void setHookUniforms(const backend::HookUniformBlock&, std::span<const std::byte>); // a hook's uniform block; this sketch had a setter for worldsim's grass deform until it became a hook (decision 4)
	void setBakedAlpha(float alpha); // u_bakedAlpha, also between baked draws
	void beginBakedPass(const WorldCamera&); // the base shader in mode 2, camera uniforms, projection from the setViewport size as today
	void endBakedPass(); // around BakedChunkRenderer's own VAO draws; stays until a world mesh API exists
}
}
```

In PR 1 a `TextureHandle` is a name, not an owner: a registry maps it to a GL
texture name, `FontRenderer`, `TileTextureAtlas` and `RenderToTexture` register
the names they already create, and `backend::gl::name()` reads it. PR 2 adds the
texture table and makes the entries owned, which keeps PR 1 to headers and handles.

| caller | today | becomes |
|---|---|---|
| `Shapes.cpp:90,222`; `TextInput.cpp:553,583,678,710`; `SdfMinimalScene.cpp:36,97` | `getBatchRenderer()->addTextQuad` per glyph | `Primitives::drawGlyphs`; `ui` stops including `BatchRenderer.h`. `TextInput` passes no family: it shapes with `fontUi` (Barlow, `theme/Variants.h:21`) but samples atlas 0 today, a separate worldsim fix. That fix (worldsim#310) passes `fontUi` to its two `drawGlyphs` calls; since `v0.9.0` each quad carries its atlas, `drawGlyphs` takes no family, and TextInput drops `fontUi` from those calls on adopting it |
| `InstancedEntityRenderer.cpp:13,30,86`; the grass renderer's `:18,66`; `GrassScene.cpp:122,151` | `getBatchRenderer()->uploadInstancedMesh`, `releaseInstancedMesh` | `Primitives::uploadInstancedMesh`; release through the handle's destructor or `releaseInstancedMesh` |
| `InstancedEntityRenderer.cpp:121`; the grass renderer's `:182`; `GrassScene.cpp:209` | `drawInstanced(handle, data, n, pos, zoom, ppm)` | `Primitives::drawInstanced(handle, data, n, WorldCamera)` |
| the grass renderer's `:145-161,187-193`; `GrassScene.cpp:194-201,212-213` | `getShaderProgram()` + `glUniform*` | a setter for the grass deform (since a hook's block, `setHookUniforms`, decision 4), `setBakedAlpha`; `InstancingUniforms.h` deleted or kept as a GL-free carrier, that PR's call |
| `BakedChunkRenderer.cpp:153-200,238-242` | `flush`, `setViewport`, `getShaderProgram()`, eight uniforms, `u_bakedAlpha` per sub-chunk, own VAO draws | `Primitives::flush`, `beginBakedPass`, `setBakedAlpha` between draws, `endBakedPass`; its `glIsEnabled` save and restore goes |
| `ChunkRenderer.cpp:506-509,537`; `InstancedEntityRenderer.cpp:139,174` | `getBatchRenderer()->flush()`; `getTileAtlasTexture()` bound to its own sampler | `Primitives::flush`; `getTileAtlasTexture()` as a handle, plus `backend::gl::name(TextureHandle)` for the one game-GL bind |
| `AppLauncher.cpp:168,195` | `Initialize(window)`; `setFontAtlas(getAtlasTexture(), 4)` | `setViewport({...})` from the GLFW callbacks; the same `setFontAtlas` call with a handle |
| `GameBootstrap.cpp:28` | `setTileAtlas(builder->texture(), rects)` | same call, handle |

## The backend seam

What `BatchRenderer`, `Shader`, `RenderToTexture` and `GPUTimer` ask of GL today:

| resource | today |
|---|---|
| buffers | one dynamic VBO + IBO re-uploaded with `glBufferData` each flush; per instanced mesh a static VBO, static `uint16` IBO and a dynamic instance VBO (`glBufferSubData` per draw) |
| vertex layouts | two: `BatchVertex` (80 bytes, attributes 0-5, `uint32` indices) and `InstancedMeshVertex` + `InstanceData` (attributes 0, 2 and 6, 7 with divisor 1, `uint16` indices) |
| program | one: `base.vert`, `base.frag`, `includes/instancing.glsl`, GLSL 330 through `ShaderPreprocessor` at startup; 17 uniforms (`u_projection`, `u_transform`, `u_atlas`, `u_bakedAlpha`, `u_viewportHeight`, `u_pixelRatio`, `u_cameraPosition`, `u_cameraZoom`, `u_pixelsPerMeter`, `u_viewportSize`, `u_instanced`, and six for worldsim's grass deform and its cursor). Since the hooks it is still one program, assembled at build time from the base shader and the game's hooks, checked by `glslangValidator` and compiled from embedded text ([shader-hooks.md](shader-hooks.md)): `u_atlas`, the ten others in the `DrawUniforms` block, and a std140 block per hook; the grass deform is a worldsim hook (decision 4) |
| textures, framebuffers | 2D, RGBA8 or RGB8, linear, clamp to edge, one unit; `glTexSubImage2D` updates; a draw run split per atlas; one FBO with one RGBA8 colour attachment, float formats allowed |
| pipeline state | blend on, `ONE, ONE_MINUS_SRC_ALPHA` over premultiplied output (`SRC_ALPHA, ONE_MINUS_SRC_ALPHA` until PR 3), off again after each flush; depth test off; cull off; no scissor (clip is per-vertex in the shader) |
| draws | `glDrawElements(TRIANGLES, uint32)` per run, cut where the texture or the GL blend state changes; `glDrawElementsInstanced(TRIANGLES, uint16)` per instance batch |
| queries | `GL_TIMESTAMP` pairs, availability polled, `uint64` result |
| context | `glewInit` after the GLFW context (`AppLauncher.cpp:149`, app's; becomes the backend's `create`) |

The interface follows dual-deck-builder's `DrawBackend`
(`src/renderer/engine/draw/DrawBackend.ts`: `beginFrame`, `submit`, `endFrame`,
`invalidateState`, `textures`, `loadFontAtlas`) one layer lower, at its R15.38
seam. That design puts the batcher behind the seam because the vertex format is
the backend's; salal has one program and one format and keeps `BatchRenderer` in
front, building `BatchVertex` as plain data the backend uploads. The GL
implementation is the old code moved into `backend/gl/GLBackend.{h,cpp}` (the GL
halves of `BatchRenderer.cpp`, `RenderTarget.cpp`, `TileTextureAtlas.cpp` and
`GPUTimer.cpp`, PR 2's texture table, and `glewInit`), with the `gl/*.h` wrappers
and `Shader` its internals. `Shader`, `ShaderLoader` and the wrappers stay
includable for a game's own GL. As landed in PR 4, with the hooks' changes since:

```cpp
namespace Renderer::backend {
enum class VertexLayout : uint8_t { Batched, InstancedMesh }; // implies the index type: uint32 for Batched, uint16 for InstancedMesh
enum class BufferUsage : uint8_t { Static, Dynamic, Stream }; // a Stream write at offset 0 starts the buffer over and grows it to fit
enum class BlendState : uint8_t { Alpha }; // premultiplied over, which additive draws share since PR 3
struct HookUniformBlock { const char* hook; const char* block; uint32_t binding; uint32_t size; }; // one per hook block, from the generated header
struct ShaderSource { const char* vertex; const char* fragment; std::span<const HookUniformBlock> hookBlocks; }; // GLSL text, assembled at build time; file names under shaders/ until the hooks
struct GeometryDesc { VertexLayout layout; BufferHandle vertices, indices, instances; };
struct DrawCall { PipelineHandle pipeline; GeometryHandle geometry; BlendState blend; TextureHandle texture; uint32_t indexOffset; uint32_t indexCount; uint32_t instanceCount; };
struct DrawUniforms { /* the 10 non-sampler uniforms, the std140 block in shaders/includes/uniforms.glsl: 176 bytes (16 and 208 until the grass deform left), every offset static_asserted */ };
class Backend { // one per context; depth off, cull off and no scissor are fixed state, set at creation, beginFrame and invalidateState
	virtual void beginFrame() = 0;
	virtual void endFrame() = 0;
	virtual void setWindowSize(int width, int height) = 0; // what setRenderTarget({}) restores and readPixels({}) reads
	virtual void clear(const Color&) = 0;
	virtual void invalidateState() = 0;
	virtual TextureHandle createTexture(const TextureDesc&) = 0; // plus updateTexture
	virtual RenderTargetHandle createRenderTarget(TextureHandle colour) = 0; // the texture stays the caller's
	virtual void setRenderTarget(RenderTargetHandle) = 0; // {} is the window
	virtual void readPixels(RenderTargetHandle, std::span<uint8_t>) = 0; // synchronous, stalls by design
	virtual BufferHandle createBuffer(BufferUsage, size_t bytes, const void* data) = 0;
	virtual void updateBuffer(BufferHandle, size_t offset, size_t bytes, const void* data) = 0;
	virtual GeometryHandle createGeometry(const GeometryDesc&) = 0;
	virtual PipelineHandle createPipeline(const ShaderSource&) = 0; // the base shader is the only pipeline until the bloom blur
	virtual void setUniforms(const DrawUniforms&) = 0; // new values take fresh storage, so a draw in flight keeps its own
	virtual void setHookUniforms(uint32_t binding, std::span<const std::byte>) = 0; // a hook's block, the same way, on its binding point
	virtual void draw(std::span<const DrawCall>) = 0; // one submission
	virtual void beginForeignDraws(PipelineHandle, BlendState) = 0; // the baked pass's own VAO draws, until endForeignDraws()
	virtual void endForeignDraws() = 0;
	virtual QueryHandle createTimestampQuery() = 0; // plus writeTimestamp, timestampReady, timestampNs, and destroy* per handle type
};
Backend* active(); // the one Primitives::init was given, for RenderTarget and GPUTimer
}
namespace Renderer::Primitives { void init(std::unique_ptr<backend::Backend>, const backend::ShaderSource&); } // backend::gl::GLBackend::create(), which runs glewInit, or a backend::NullBackend; the consumer's assembled base shader
```

Where it differs from the sketch PR 4 started from, and why. `draw` takes a span,
one per flush, because the GL backend binds once per submission and assumes
nothing between submissions (below). `beginFrame` takes no `Viewport`: the
window's size comes through `setWindowSize` when it changes, since
`RenderTarget::end` and `readBackbuffer` need it between frames.
`createRenderTarget` takes the colour texture rather than a size and format, so
nothing has to hand a target's texture back. `BlendMode { None, Alpha, Additive }`
is `BlendState { Alpha }`: PR 3 put additive in alpha's state and nothing draws
unblended. `beginForeignDraws` and `endForeignDraws` are new, for
`beginBakedPass` and `endBakedPass` (question 2). `createGeometry` takes a desc,
three handles of one type being easy to swap, and `active()` is new, for the
renderer's objects that are not `BatchRenderer`.

Bindings are not cached across submissions. Each `draw` binds the program,
vertex array, blend state and unit-0 texture it uses and leaves nothing bound but
the program, as the batch always did, because a game's GL runs between flushes
and moves bindings: worldsim's `ChunkRenderer` leaves its tile program and the
tile atlas on unit 0, and `PlanetMesh`, `PlanetColorizer` and
`BakedChunkRenderer` uploads bind buffers and textures. Buffers are written
through `GL_COPY_WRITE_BUFFER`, which no vertex array records. Fixed state is
set once and trusted, and every worldsim pass that touches it (`ChunkRenderer`,
`PlanetRenderer`) restores it, so worldsim calls `invalidateState` nowhere. The
uniforms are the one binding that outlives a call: uniform binding point 0 is the
renderer's, and since the hooks points 1 up as well, one per hook block, and
`invalidateState` rebinds them for a pass that bound its own buffer there (no
worldsim shader declares a block).

Storage. The batch's vertex and index buffers are `Stream`: each flush writes
from offset 0, and the GL backend respecifies with `glBufferData(nullptr)`
(orphans) before `glBufferSubData`, doubling the size when it grows. A ring
would need `glFenceSync` to know when a region is free, or would have to be big
enough never to lap the GPU, and a flush's size is unbounded; the orphan is what
the old per-flush `glBufferData` already was, and needs no base vertex. The
uniforms are a ring, 512 slots of 176 bytes (208 before the grass deform left)
rounded up to `GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT`, written with
`glBufferSubData` and bound with `glBindBufferRange`, orphaned when full, and
each hook block has a ring of its own the same way. No slot is written twice between
orphans, so nothing in flight is overwritten and nothing waits, and a repeat of
the last values (every instanced draw of a pass shares its camera) binds their
slot again rather than writing a new one: each write into a buffer the GPU is
drawing from costs GPU time, 0.77 ms a frame in worldsim's game scene until the
repeats were skipped.

`clear` and `setRenderTarget` fold into one `beginPass(target, optional clear)`
and `endPass()` on Vulkan; GL is unaffected. One shader source is possible but
not free: Vulkan GLSL rejects loose `uniform float` and wants `#version 450`,
so the non-sampler uniforms are a std140 block, as each hook's are (GL 3.3 core has them;
`u_atlas` stays a sampler, set to unit 0 at `createPipeline`). The
preprocessor does not swap the version line: the Vulkan variant also needs
`set` and `binding` qualifiers that GLSL 330 cannot spell, so the swap is not
the one line it looked like, and it waits for that backend. A second backend then
provides the virtuals, the vertex layouts as vertex-input descriptions, and the
clip test's y flip (`u_viewportHeight` exists because `gl_FragCoord` is
bottom-left and Vulkan's is top-left: a shader-variant fact). The null backend
records what reaches it, so the renderer's tests run the z sort, the blend and
atlas splits and the clip bounds through `flush` headless, not only through
`BatchRenderer::plan()`.

## Decision G, folded in

Blend state in the sort key: a per-group key that splits, not sorts. `DrawGroup`
gets a `BlendMode` next to `zIndex`; emit order is kept, and the run walk
(`BatchRenderer::plan`, which `flush` submits, GL-free so the batch tests run
headless) starts a new run where a group's GL blend state changes, as it already
does for atlases. Premultiplied alpha decides the cost: with it, additive is
over's blend state (`ONE, ONE_MINUS_SRC_ALPHA`, alpha zeroed in the shader,
dual-deck-builder R5.22a) and never splits a draw; without it, additive is
`SRC_ALPHA, ONE` and splits. The per-vertex flag rides in `data2.w`, since
`BatchVertex` has no spare field for shapes: border position 0 to 2 plus 4 for
additive, and every negative mode (text and shadow, and the arc and image modes
PR 3 added) 10 lower when additive; `BatchRenderer.h` and `base.frag` carry the
same table. Decided: premultiplied, in the same PR, also because compositing a
straight-alpha render target over the scene (bloom) gives dark fringes. Nothing
the base shader samples carries alpha in PR 3 (font atlases are RGB8; the tile
atlas is sampled only by worldsim's tile shader, `ChunkRenderer.cpp:537`,
blending off, and is left alone), so the costs are `base.frag` multiplying at
output, `drawImage` taking its texels as premultiplied (a `RenderTarget`'s
already are; `createTexture` is unchanged, so a caller with straight-alpha
pixels premultiplies them before upload), and screenshots moving by rounding.
One readback does change: a draw over a transparent target stores alpha `a`
where straight blending stored `a * a`.

A rect's border alpha rides in `data2.w` too, added later: its transparency in
the shape mode's fraction, 0 to 0.25, so an opaque border keeps the whole number.

Offscreen render target as the first backend object: `RenderTarget` above,
`Rgba16F` for bloom, in PR 2. The primitives asteroids needs, and whether each is
a new batch entry:

| need | how | new batch entry? |
|---|---|---|
| filled polygon with a per-frame transform | `drawPolygon({vertices, style, zIndex, blend})` under `PushTransform`: tessellate (`vector/Tessellator`, cached per outline, keyed by its vertex bits and emptied whole at 1024 entries) into `addTriangles`, its boundary loops found with it; the fill gets a ring one device pixel wide outside them, the fill colour fading to transparent across it, so the edge is anti-aliased without a new mode (dual-deck-builder R5.17); two translucent fills that share an edge show a band about a pixel wide along it, each ring over the other's fill, and a fill whose boundary doesn't close into loops (an outline touching itself) stays aliased | no |
| arc or ring with partial fill | one quad in a new SDF mode, `data2 = (innerR, outerR, sweep / 2, mode)`, the start angle folded into the quad's local coordinates (turned so the arc's middle lies along +x), next to text and shadow in `base.frag` | yes: one render-mode constant and about twenty lines of GLSL; `drawCircle` draws its disk and border in this mode too, where a 64-segment fan faceted at HUD radii |
| line with width, polyline | `drawLine` is already a rotated quad; `drawPolyline({points, width, color, cap, join, closed})` is a convex SDF piece per segment, a capsule or a box run on to the miter point, cut along each joint's bisector so neighbours share the cut's two end points as the same floats and the rasteriser gives each pixel to one piece; joins round or miter (exact up to a right-angle turn, round past it), caps butt, round or square; a translucent stroke covers each pixel once except where it crosses itself, at a segment too short for its joints' cuts, and within about 8.6 degrees of a hairpin; points within a quarter of a device pixel of the line drawn without them are dropped first, so sub-pixel input noise leaves no such short segments | no |
| silhouette stroke, rim light | `drawPolygon` with `style.border`: a closed stroke with miter joins, its outer edge the border's width outside the outline and its inner edge a pixel inside, over the fill's edge; it is drawPolyline's stroke, so its pieces overlap where drawPolyline's do; a notch is mitered into its corner, except beside an edge too short for its cut, where it is round with a flat piece into the corner | no |
| clip by silhouette, hold-fill | the fill rises, so it is the polygon under a rect clip, which the fast per-vertex path already does; the stencil path clip stays unbuilt | no |
| transform stack for wrap ghosts | exists: `PushTransform`, baked at add time; draw an entity up to four times | no |
| fixed-aspect viewport with letterboxing | a scale-and-translate transform plus a rect clip pushed at frame start; the backend viewport stays full | no |
| bloom, additive glow | `RenderTarget` + additive blend + `drawImage(texture, rect)` as a plain-sample render mode; the blur is a second pipeline, the first after the base shader, its source built in at build time as the base shader's is (`createPipeline` takes text since the hooks), Quick Run or later | `drawImage` yes; the blur later |

Particles stay game-side: a pool over `drawTriangles` or `drawCircle` with
`blend = Additive`, batched by the sort key; an engine pool when a second game wants one.

## Sequencing

Four PRs in salal, each followed by a tag worldsim pins, and the hooks after them;
worldsim changes only where its callers move, and PR 1 presumes worldsim already
consumes `v0.1.0`.
Behaviour is identical through PR 2. After PR 3 what is drawn is identical to
rounding, but a target holds premultiplied colour: a draw over transparent reads
back alpha `a` where it read `a * a`, and `clear` stores its colour premultiplied.

| PR | salal | worldsim | verified by |
|---|---|---|---|
| 1. GL off the public surface (minor bump, `v0.2.0`) | `BatchRenderer.{h,cpp}`, `InstanceData.h`, `Primitives.{h,cpp}`, `FontRenderer.{h,cpp}`, `CoordinateSystem.{h,cpp,test.cpp}`, `RenderToTexture.h`, `TileTextureAtlas.h`, `TileAtlasBuilder.h`, `GPUTimer.{h,cpp}`, new `Handle.h`, the handle-to-name registry with `backend::gl::name()` in new `backend/gl/GLNames.h` (the only file under `backend/` until PR 2), `Primitives::clear` (no callers migrated), the four GL test fixtures, `CMakeLists.txt` (`OpenGL::GL` and `GLEW::GLEW` PRIVATE on `salal_renderer`, `glfw` off it, since nothing in the library touches GLFW once `CoordinateSystem` is fed its sizes; the GL test fixtures link all three), the layering check gaining "no GL, GLES, OpenGL, GLFW or glad header reachable from a public renderer header", public meaning every header under `libs/renderer` outside `gl/`, `shader/` and `backend/`, which is how `gl/` and `shader/` stay where they are until PR 2 moves them. About 25 files | about 20 files: the dozen in the table above, the `render(ctx, InstancingUniforms&, stats)` signatures across the entity renderers, `GrassScene`'s release, `libs/engine/CMakeLists.txt` linking OpenGL, GLEW and glfw PUBLIC (the apps' clear pairs and `GrassScene`'s 20 GL calls reach GL through `engine`) | `salal_renderer_tests` (CoordinateSystem now headless); worldsim full Windows build and ctest; ui-sandbox `text_shapes`, `sdf_minimal`, `svg`, `button`, `grass` and the game scene through the dev server, screenshots identical |
| 2. Textures through `GLTexture`, `RenderTarget` (`v0.3.0`) | `FontRenderer.cpp:289-299` (the one raw `glGenTextures` left in the renderer) via `createTexture`, `RenderToTexture.*` becomes `RenderTarget`, `TileTextureAtlas` on the texture table, the registry becomes the table (`backend/gl/GLTextures.h`, entries owned, `name()` kept), the stack of bound targets in `backend/gl/GLTargets.h` (the backbuffer at the bottom, its size refreshed by `Primitives::setViewport` while no target is open: the coordinate system's framebuffer size when one is set, else the size given), new public `Texture.h` (`TextureFormat`, `TextureDesc`), `Primitives` gains `createTexture`, `updateTexture`, `destroyTexture`, `readBackbuffer`; `gl/*.h`, `Shader.h`, `ShaderLoader.h`, `ShaderPreprocessor.h` move to `backend/gl/`, the layering check exempts `backend/` alone | `AssetRenderer.cpp` (drops its `glClear` and `glReadPixels`), `DebugServer.cpp` screenshot (optional), the include paths in `ChunkRenderer.h:16-19`, `BakedChunkRenderer.h:10-11`, `PlanetRenderer.cpp:7` | `RenderTarget` test (clear, read back); `asset-cli` output PNG identical; `FontRenderer` test |
| 3. Blend in the sort key, premultiplied alpha, new primitives (`v0.4.0`) | `BatchRenderer` (`DrawGroup.blend`, the run walk out of `flush` into GL-free `plan()`, split on texture and GL blend state, `addArc`, `addImageQuad`, `addStrokePieces`), `base.frag` (premultiplied output in every branch, the `data2.w` flag, arc and image modes), the three blend-func sites on `ONE, ONE_MINUS_SRC_ALPHA`, `Primitives::clear` premultiplying its colour, `Primitives` (`BlendMode`, `LineCap`, `LineJoin`, `PolygonArgs`, `PolylineArgs`, `ArcArgs`, `ImageArgs`, `blend` on the existing Args and `drawGlyphs`), new `primitives/Stroke.{h,cpp}`, `PolygonStyle` in foundation, and `Tessellator`'s convex fast path no longer taking a pentagram; headless tests of the sort and split, the mode table, arcs and strokes, and GL tests reading premultiplied output back from a `RenderTarget` | no caller moves, since every new parameter defaults to `Alpha`; four new ui-sandbox scenes, `polygon`, `arc`, `polyline` and `blend`; `AssetRenderer` un-premultiplies its readback for straight-alpha PNGs | sandbox scenes for polygon, arc, polyline and blend; existing scenes diffed by hand against PR 2's shots, within rounding, no baseline committed |
| 4. The backend interface (`v0.5.0`) | `backend/Backend.h` (the interface, `BatchVertex` and `InstancedMeshVertex`, `DrawUniforms` with every std140 offset asserted), `backend/SlotTable.h`, `backend/NullBackend.{h,cpp}`, `backend/gl/GLBackend.{h,cpp}` absorbing PR 2's `GLTextures.{h,cpp}` (the table and `name()`), the GL halves of `BatchRenderer.cpp`, `RenderTarget.cpp` and `GPUTimer.cpp`, and `glewInit`; `GLTargets.{h,cpp}` gone, the stack of begun targets in `RenderTarget.cpp`; new `shaders/includes/uniforms.glsl`, the block both stages include; `Primitives::init(std::unique_ptr<backend::Backend>)` and `Primitives::invalidateState`; `TileTextureAtlas` and `FontRenderer` through `Primitives::createTexture`; the layering check exempting `backend/gl/` alone | `AppLauncher.cpp`, `asset-cli` and `AssetRenderer.test.cpp` hand `Primitives::init` a `GLBackend::create()` and drop their `glewInit`; `ChunkRenderer.cpp` includes `backend/gl/GLBackend.h` for `name()`; no `invalidateState` calls | renderer tests headless against the null backend (z sort, blend split, atlas split, clip bounds, instanced and baked uniforms, all through `flush`); GL tests of the block's offsets against the linked program and of 1,024 instanced draws twice round the uniform ring; sandbox and game screenshots and asset-cli PNGs against `v0.4.0` |
| 5. Shader hooks (`v0.6.0`) | the base shader renamed from its old name (`base.vert`, `base.frag`, `BatchVertex`, `VertexLayout::Batched`); `cmake/SalalBaseShader.cmake` (`salal_base_shader()`) and `cmake/SalalGlslang.cmake`, the tool in `assembler/`, `glslang[tools]` as a host dependency; `#include "hooks.glsl"` in `base.vert` and the two hook points in `instancing.glsl`; `createPipeline` from source text, `ShaderSource::hookBlocks`, `Backend::setHookUniforms` and a uniform ring per binding point, each hook block's size and member offsets checked at link; `Primitives::init` taking the source, `Primitives::setHookUniforms`; the grass deform's six uniforms, its setter and its shader branch gone, `DrawUniforms` 176 bytes; no shader staging | the grass deform as a hook in `libs/engine/world/rendering/hooks/`, its block's struct checked member by member against the generated `HookUniformMember` constants, `salal_base_shader(engine ...)`; the three `Primitives::init` callers pass `base_shader::source()`; asset-cli's shader check and the test's shader-locating helper, and asset-cli's and asset-manager's shader mirrors, gone; asset-cli links `engine` | the assembler headless (order, refusals, empty folder, the generated header); hook writes through the null backend in order with draws; a test hook drawn on GL, on and off; sandbox, game and asset-cli output against `v0.5.0`, with the deform on |

## Questions, decided

1. Premultiplied alpha in PR 3: yes (2026-10-26). The alternative keeps
   additive as a run split and leaves bloom compositing wrong.
2. `BakedChunkRenderer` draws its own VAOs with the base shader in mode 2; the
   `beginBakedPass`/`endBakedPass` pair, shipped in PR 1, stays until a
   renderer-owned world mesh API exists (2026-10-26), which would turn 29 game
   GL calls into API.
3. `backend/gl/` stays includable by consumers (2026-10-26): an explicit opt-in
   path outside the layering check, dropped the day a second backend exists.
   `gl/` and `shader/` move there in PR 2.
4. Worldsim's grass deform and its cursor uniforms moved out of the base
   shader into a worldsim hook (2026-10-26). The base shader takes hooks,
   assembled into it at build time ([shader-hooks.md](shader-hooks.md)), so the
   GPU still sees one program and the game's code lives in the game's repo.

The handle type is settled, not open: a distinct tagged `Handle<Tag>`, since
nothing in `renderer` uses `ResourceHandle` or `ResourceManager` outside their
tests; worldsim's ui-sandbox `HandleScene` does, which is why they are still here.
