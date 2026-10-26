# Renderer backend boundary

The seam between `renderer` and OpenGL: what leaks today, what the public surface
becomes, what sits behind it, and the PRs that get there. Written 2026-10-26 from
the imported renderer (worldsim @ `2cc7e40a`) and its callers at that commit;
decision G in [open-decisions.md](open-decisions.md) has the reasoning. Nothing
below is built.

## Goal and non-goals

GL types and GL calls stay inside `libs/renderer`. Consumers (`ui`, `app`, the
games) see renderer-owned handles and value types: `TextureHandle`,
`RenderTarget`, `Viewport`, the `Primitives` API. OpenGL 3.3 core is the first
and only backend; Vulkan is possible later and not scheduled.

A boundary, not a port: same shaders, same batching, same draw calls, worldsim's
screenshots do not change. Out of scope: the game's own GL code (worldsim's
`ChunkRenderer`, `BakedChunkRenderer`, `PlanetRenderer`, `PlanetMesh`,
`PlanetColorizer`, `GlobeView`, the `SurfaceFieldGolden` test, about 400 GL
calls), worldsim's until it chooses to come through the engine; and the
`glClearColor`/`glClear` pair opening about forty worldsim scenes, which include
`<GL/glew.h>` themselves (`Primitives::clear` below lets them stop).

## The public surface today

Every place a renderer header hands GL or GLFW to an includer, with worldsim's
users at `2cc7e40a`. `libs/ui` makes no GL call of its own; it gets GL only
through these headers.

| header | symbol | leak | worldsim users |
|---|---|---|---|
| `primitives/BatchRenderer.h` | `#include <GL/glew.h>`, plus `gl/GLBuffer.h`, `gl/GLVertexArray.h`, `shader/Shader.h` for members | header include | 8 includers: `ui/shapes/Shapes.cpp`, `ui/components/TextInput/TextInput.cpp`, `engine/world/rendering/{BakedChunkRenderer,ChunkRenderer,GroundcoverRenderer,InstancedEntityRenderer}.cpp`, `ui-sandbox/scenes/{GrassScene,SdfMinimalScene}.cpp` |
| | `getShaderProgram()` | returns `GLuint` | `BakedChunkRenderer.cpp:182`, `GroundcoverRenderer.cpp:145`, `GrassScene.cpp:194`, each followed by `glUseProgram` and `glUniform*` |
| | `addTextQuad(..., GLuint atlasTexture = 0, ...)` | `GLuint` parameter | `Shapes.cpp:222`, `TextInput.cpp:583`, `:710`, `SdfMinimalScene.cpp:97` (all leave the default); `Primitives.cpp:394` passes the family atlas |
| | `setFontAtlas(GLuint, float)` | `GLuint` parameter | through `Primitives::setFontAtlas`, `AppLauncher.cpp:195` |
| | `uploadInstancedMesh`, `drawInstanced`, `releaseInstancedMesh` | take `InstancedMeshHandle`, a struct of GL wrappers | `GroundcoverRenderer.cpp:75`, `:182`, `InstancedEntityRenderer.cpp:32`, `:121`, `GrassScene.cpp:151`, `:209` |
| `primitives/InstanceData.h` | `#include <GL/glew.h>`; `InstancedMeshHandle{GLVertexArray vao; GLBuffer meshVBO, meshIBO, instanceVBO}` | header include, GL members | `GroundcoverRenderer.h:12`, `InstancedEntityRenderer.h:9`, `GrassScene.cpp:17` |
| `primitives/Primitives.h` | `getBatchRenderer()` | returns the GL-typed class | 14 call sites in the 8 files above |
| | `setFontAtlas(unsigned int, float)` | GL texture name as `unsigned` | `AppLauncher.cpp:195` |
| | `setTileAtlas(unsigned int, rects)`, `getTileAtlasTexture()` | GL texture name as `unsigned` | `world-sim/GameBootstrap.cpp:28`, `ChunkRenderer.cpp:537` |
| `font/FontRenderer.h` | `#include <GL/glew.h>`; `Atlas::texture` | header include, `GLuint` member | 20 includers: 12 in `libs/ui` (`Shapes`, `TextInput`, `Avatar`, `Badge`, `Button`, `Divider`, `KeyCap`, `ProgressBar`, `SegmentedControl`, `Slider`, `Stat`, `TabBar`), 7 in `apps`, `AppLauncher.cpp`; none uses GL, they measure text and build glyph quads |
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
`ShaderLoader.cpp`. About a dozen worldsim files change when the leaks close (the
users above minus the game-GL rows and the twenty `FontRenderer.h` includers).

## The target surface

Declarations, not code. `Primitives` keeps its API except where it hands out GL.

```cpp
namespace Renderer {
// ResourceHandle.h's 16-bit index + 16-bit generation layout as a distinct type in Renderer::, not its
// `using TextureHandle = ResourceHandle` alias; the GL backend resolves it through a ResourceManager<GLTexture>.
struct TextureHandle { uint32_t value = 0xFFFFFFFF; bool isValid() const; bool operator==(TextureHandle) const = default; };
enum class TextureFormat : uint8_t { Rgba8, Rgb8, Rgba16F, Rgba32F };
struct TextureDesc { int width = 0, height = 0; TextureFormat format = TextureFormat::Rgba8; bool linear = true; const void* pixels = nullptr; };
// Replaces Initialize(GLFWwindow*): app's resize callbacks feed it, nothing in renderer
// polls GLFW, CoordinateSystem.test.cpp needs no window; updateWindowSize and setFullViewport go.
struct Viewport { int logicalWidth = 0, logicalHeight = 0, framebufferWidth = 0, framebufferHeight = 0; float pixelRatio() const; };
class CoordinateSystem { void setViewport(const Viewport&); const Viewport& viewport() const; /* the rest as today */ };
// The first backend object, replacing RenderToTexture. The renderer keeps its own stack of bound targets, so
// begin/end rebind from that record, not glGetIntegerv; begin() flushes and sets viewport, projection and clip height.
class RenderTarget {
  public:
	static RenderTarget create(int width, int height, TextureFormat format = TextureFormat::Rgba8);
	void begin(); void end();
	TextureHandle texture() const; int width() const; int height() const;
	void readPixels(std::span<uint8_t> out) const; // what AssetRenderer and the screenshot route do by hand today
};
class BatchRenderer { // renderer-internal after PR 1; getShaderProgram() deleted
	void addTextQuad(Vec2 pos, Vec2 size, Vec2 uvMin, Vec2 uvMax, Color color, Vec2 runOrigin, TextureHandle atlas = {}, float zIndex = 0.0F);
	void setFontAtlas(TextureHandle atlas, float pixelRange = 4.0F);
};
class FontRenderer { TextureHandle getAtlasTexture(FontFamily family = FontFamily::Roboto) const; }; // header drops <GL/glew.h>
class GPUTimer; // public API unchanged; the spans move behind a pimpl so the header drops gl/GLQuery.h
namespace Primitives {
	void setFontAtlas(TextureHandle atlas, float pixelRange = 4.0F); // same three calls, handle type
	void setTileAtlas(TextureHandle atlas, const std::vector<glm::vec4>& rects);	TextureHandle getTileAtlasTexture();
	TextureHandle createTexture(const TextureDesc&); // every texture through here, and so through gl/GLTexture
	void updateTexture(TextureHandle, int x, int y, int width, int height, const void* pixels);	void destroyTexture(TextureHandle);
	void clear(Color);		// the glClearColor/glClear pair every scene opens with
	void invalidateState(); // a foreign GL pass says so; the renderer never queries state (dual-deck-builder R2.15)
	void readBackbuffer(std::span<uint8_t> rgba, int& width, int& height); // the screenshot route
	// replacements for getBatchRenderer() and getShaderProgram(), one per operation the callers need
	struct WorldCamera { Vec2 position; float zoom = 1.0F; float pixelsPerMeter = 1.0F; };
	struct GroundcoverParams { bool enabled = false; float openness = 1.0F, reach = 0.0F; Vec2 cursorWorld; float cursorRadius = 0.0F, cursorStrength = 0.0F; };
	void drawGlyphs(const FontRenderer::GlyphQuad* quads, size_t count, Color color, FontFamily family = FontFamily::Roboto, float zIndex = 0.0F);
	InstancedMeshHandle uploadInstancedMesh(const renderer::TessellatedMesh& mesh, uint32_t maxInstances = 10000); // handle becomes an opaque index
	void releaseInstancedMesh(InstancedMeshHandle&);
	void drawInstanced(const InstancedMeshHandle&, const InstanceData* instances, uint32_t count, const WorldCamera&);
	void setGroundcover(const GroundcoverParams&);	void setBakedAlpha(float alpha); // u_groundcover*, u_cursor*, u_bakedAlpha
	void beginBakedPass(const WorldCamera&, float alpha); // binds the uber program in mode 2 with the camera uniforms,
	void endBakedPass();								  // around BakedChunkRenderer's own VAO draws; open question 2
}
}
```

Who calls what:

| caller | today | becomes |
|---|---|---|
| `Shapes.cpp:90,222`; `TextInput.cpp:553,583,678,710`; `SdfMinimalScene.cpp:36,97` | `getBatchRenderer()->addTextQuad` per glyph | `Primitives::drawGlyphs`; `ui` stops including `BatchRenderer.h` |
| `InstancedEntityRenderer.cpp:13,30,86`; `GroundcoverRenderer.cpp:18,66`; `GrassScene.cpp:122,151` | `getBatchRenderer()->uploadInstancedMesh`, `releaseInstancedMesh` | `Primitives::uploadInstancedMesh`, `releaseInstancedMesh` |
| `InstancedEntityRenderer.cpp:121`; `GroundcoverRenderer.cpp:182`; `GrassScene.cpp:209` | `drawInstanced(handle, data, n, pos, zoom, ppm)` | `Primitives::drawInstanced(handle, data, n, WorldCamera)` |
| `GroundcoverRenderer.cpp:145-161,187-193`; `GrassScene.cpp:194-201,212-213` | `getShaderProgram()` + `glUniform*` | `setGroundcover`, `setBakedAlpha`; `InstancingUniforms.h` deleted |
| `BakedChunkRenderer.cpp:153-200` | `flush`, `setViewport`, `getShaderProgram()`, eight uniforms, own VAO draws | `Primitives::flush`, `beginBakedPass`/`endBakedPass` around the draws; its `glIsEnabled` save and restore goes |
| `ChunkRenderer.cpp:506-509,537`; `InstancedEntityRenderer.cpp:115` | `getBatchRenderer()->flush()`; `getTileAtlasTexture()` bound to its own sampler | `Primitives::flush`; `getTileAtlasTexture()` as a handle, plus `backend::gl::name(TextureHandle)` for the one game-GL bind |
| `AppLauncher.cpp:168,195` | `Initialize(window)`; `setFontAtlas(getAtlasTexture(), 4)` | `setViewport({...})` from the GLFW callbacks; the same `setFontAtlas` call with a handle |
| `GameBootstrap.cpp:28` | `setTileAtlas(builder->texture(), rects)` | same call, handle |

## The backend seam

What `BatchRenderer`, `Shader`, `RenderToTexture` and `GPUTimer` ask of GL today:

| resource | today |
|---|---|
| buffers | one dynamic VBO + IBO re-uploaded with `glBufferData` each flush; per instanced mesh a static VBO, static `uint16` IBO and a dynamic instance VBO (`glBufferSubData` per draw) |
| vertex layouts | two: `UberVertex` (80 bytes, attributes 0-5) and `InstancedMeshVertex` + `InstanceData` (attributes 0, 2 and 6, 7 with divisor 1) |
| program | one: `uber.vert`, `uber.frag`, `includes/instancing.glsl`, GLSL 330 through `ShaderPreprocessor`; 17 uniforms (`u_projection`, `u_transform`, `u_atlas`, `u_bakedAlpha`, `u_viewportHeight`, `u_pixelRatio`, `u_cameraPosition`, `u_cameraZoom`, `u_pixelsPerMeter`, `u_viewportSize`, `u_instanced`, and six groundcover and cursor uniforms) |
| textures | 2D, RGBA8 or RGB8, linear, clamp to edge, one unit; `glTexSubImage2D` updates; a draw run split per atlas |
| framebuffers | one colour attachment, RGBA8; float formats allowed |
| pipeline state | blend on, `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`, off again after each flush; depth test off; cull off; no scissor (clip is per-vertex in the shader) |
| draws | `glDrawElements(TRIANGLES, uint32)` per atlas run; `glDrawElementsInstanced(TRIANGLES, uint16)` per instance batch |
| queries | `GL_TIMESTAMP` pairs, availability polled, `uint64` result |
| context | `glewInit` after the GLFW context (`AppLauncher.cpp:149`, app's; becomes the backend's `create`) |

The interface, in the spirit of dual-deck-builder's `DrawBackend`
(`src/renderer/engine/draw/DrawBackend.ts`: `beginFrame`, `submit`, `endFrame`,
`invalidateState`, `textures`, `loadFontAtlas`) but one layer lower, at its
R15.38 seam. Dual-deck-builder puts the batcher behind the seam because the
vertex format is the backend's; salal has one program and one format and keeps
`BatchRenderer` in front, building `UberVertex` as plain data the backend uploads.
The GL implementation is today's code moved into `renderer/backend/gl/`: the GL
halves of `BatchRenderer.cpp`, `Shader.cpp`, `ShaderLoader.cpp`,
`RenderToTexture.cpp`, `TileTextureAtlas.cpp`, `GPUTimer.cpp` and
`FontRenderer.cpp:289-299`, with the `gl/*.h` wrappers as its internals.

```cpp
namespace Renderer::backend {
enum class VertexLayout : uint8_t { Uber, InstancedMesh };	enum class BlendMode : uint8_t { None, Alpha, Additive };
struct DrawCall { GeometryHandle geometry; BlendMode blend; TextureHandle texture; uint32_t indexOffset, indexCount, instanceCount; };
struct FrameUniforms { /* the 17, as one struct; the backend uploads what changed */ };
class Backend { // one per context; depth off, cull off and no scissor are fixed state, set at beginFrame and after invalidateState
  public:
	virtual ~Backend() = default;	virtual void beginFrame(const Viewport&) = 0;	virtual void endFrame() = 0;
	virtual void clear(Color) = 0;					virtual void invalidateState() = 0;
	virtual TextureHandle createTexture(const TextureDesc&) = 0;	virtual void updateTexture(TextureHandle, int x, int y, int w, int h, const void*) = 0;
	virtual RenderTargetHandle createRenderTarget(int w, int h, TextureFormat) = 0;
	virtual void setRenderTarget(RenderTargetHandle) = 0;	virtual void readPixels(RenderTargetHandle, std::span<uint8_t>) = 0; // {} is the window
	virtual BufferHandle createBuffer(BufferUsage, size_t bytes, const void* data) = 0;	virtual void updateBuffer(BufferHandle, size_t offset, size_t bytes, const void* data) = 0;
	virtual GeometryHandle createGeometry(VertexLayout, BufferHandle vertices, BufferHandle indices, BufferHandle instances = {}) = 0;
	virtual void setUniforms(const FrameUniforms&) = 0;		virtual void draw(const DrawCall&) = 0;
	virtual QueryHandle createTimestampQuery() = 0; // plus writeTimestamp, timestampReady, timestampNs, and destroy* per handle type
};
}
```

A second backend provides those virtuals, the two vertex layouts as vertex-input
descriptions, the uber shader in its language (GLSL 330 to SPIR-V through
glslang keeps one source), the uniform struct as a uniform block, and the clip
test's y convention (`u_viewportHeight` exists because `gl_FragCoord` is
bottom-left and Vulkan's is top-left: a shader-variant fact, not the batcher's).
A null backend for headless tests of the z sort, blend split and atlas split
comes with it; `BatchRenderer.test.cpp` has two trivial tests today for want of a context.

This interface is step 3 and may wait. PRs 1 to 3 need none of it; they move GL
code into `backend/gl/` as it is and change only headers and handles.

## Decision G, folded in

Blend state in the sort key. `DrawGroup` gets a `BlendMode` next to `zIndex`;
the emit walk in `flush` starts a new run where it changes, as it already does
for atlases, so order stays exact. Premultiplied alpha decides the cost: with it,
additive is the same blend state as over (`ONE, ONE_MINUS_SRC_ALPHA`, alpha
zeroed in the shader, dual-deck-builder R5.22a) and never splits a draw; without
it, additive is `SRC_ALPHA, ONE` and splits. Recommendation: premultiplied, in
the same PR, also because compositing a straight-alpha render target back over
the scene (bloom) gives dark fringes. Costs: the tile atlas premultiplied on
upload (nanosvg output is straight), `uber.frag` multiplying at output, and
screenshots moving by rounding, not by eye. Open question 1.

Offscreen render target as the first backend object: `RenderTarget` above,
`Rgba16F` for bloom, in PR 2. The screenshot route and `AssetRenderer` read back
through it. The primitives asteroids needs, and whether each is a new batch entry:

| need | how | new batch entry? |
|---|---|---|
| filled polygon with a per-frame transform | `drawPolygon({vertices, style, zIndex})` under `PushTransform`: tessellate (`vector/Tessellator`, cached per silhouette) into `addTriangles` | no |
| arc or ring with partial fill | one quad in a new SDF mode, `data2 = (innerR, outerR, startAngle, sweep)`, next to text and shadow in `uber.frag` | yes: one render-mode constant and about twenty lines of GLSL; a 64-segment fan (`drawCircle` today) facets at HUD radii |
| line with width, polyline | `drawLine` is already a rotated quad; `drawPolyline({points, width, cap, join})` is quads plus round caps, dual-deck-builder's shape | no |
| silhouette stroke, rim light | `drawPolygon` with `style.border`: the outline as a polyline, offset outward by half the width | no |
| clip by silhouette, hold-fill | the fill rises, so it is the polygon under a rect clip, which the fast per-vertex path already does; the stencil path clip stays unbuilt | no |
| transform stack for wrap ghosts | exists: `PushTransform`, baked at add time; draw an entity up to four times | no |
| fixed-aspect viewport with letterboxing | a scale-and-translate transform plus a rect clip pushed at frame start; the backend viewport stays full | no |
| bloom, additive glow | `RenderTarget` + additive blend + `drawImage(texture, rect)` as a plain-sample render mode; the blur is a second program, the first thing after the uber program the backend has to manage, Quick Run or later | `drawImage` yes; the blur later |

Particles stay game-side over `Primitives`: a game-owned pool calling
`drawTriangles` or `drawCircle` with `blend = Additive`, which the sort key
batches with everything else; an engine pool when a second game wants one.

## Sequencing

Four PRs in salal, each followed by a tag worldsim pins; worldsim changes only
where its callers move. Behaviour is identical through PR 2, identical to
rounding after PR 3 if premultiplied lands. The tag after PR 1 is the one that
matters: the first API change worldsim takes through a pin, and the last before
a second consumer depends on the renderer.

| PR | salal | worldsim | verified by |
|---|---|---|---|
| 1. GL off the public surface (minor bump, `v0.2.0`) | `BatchRenderer.{h,cpp}`, `InstanceData.h`, `Primitives.{h,cpp}`, `FontRenderer.{h,cpp}`, `CoordinateSystem.{h,cpp,test.cpp}`, `RenderToTexture.h`, `TileTextureAtlas.h`, `TileAtlasBuilder.h`, `GPUTimer.{h,cpp}`, `Shader.h`, `ShaderLoader.h`, `gl/*.h` to `backend/gl/`, new `TextureHandle.h`; `OpenGL::GL`, `GLEW::GLEW` and `glfw` become PRIVATE on `salal_renderer`; the configure-time layering check gains "no `GL/glew.h` or `GLFW/glfw3.h` in a renderer header outside `backend/gl/`". About 15 files | the dozen files in the table above, `InstancingUniforms.h` deleted, `libs/engine` finds OpenGL, GLEW and glfw itself | `salal_renderer_tests` (CoordinateSystem now headless); worldsim full Windows build and ctest; ui-sandbox `text_shapes`, `sdf_minimal`, `svg`, `button`, `grass` and the game scene through the dev server, screenshots identical |
| 2. Textures through `GLTexture`, `RenderTarget` (`v0.3.0`) | `FontRenderer.cpp:289-299` (the one raw `glGenTextures` left in the renderer) via `createTexture`, `RenderToTexture.*` becomes `RenderTarget`, `TileTextureAtlas` on the texture table, `Primitives` gains `createTexture`, `updateTexture`, `destroyTexture`, `readBackbuffer` | `AssetRenderer.cpp` (drops its `glClear` and `glReadPixels`), `DebugServer.cpp` screenshot (optional) | `RenderTarget` test (clear, read back); `asset-cli` output PNG identical; `FontRenderer` test |
| 3. Blend in the sort key, premultiplied alpha, new primitives (`v0.4.0`) | `BatchRenderer` (`DrawGroup.blend`, run split), `uber.frag` (premultiplied output, arc and image modes), `Primitives` (`PolygonArgs`, `PolylineArgs`, `ArcArgs`, `ImageArgs`, `blend` on the existing Args), `TileAtlasBuilder` premultiply; a batch sort and split unit test | none expected; `BakedChunkRenderer`'s own blend func already went with PR 1 | sandbox scenes for polygon, arc, polyline and blend; existing scenes within one LSB of PR 2's screenshots |
| 4. The backend interface (`v0.5.0`) | `backend/Backend.h`, `backend/gl/GLBackend.{h,cpp}` absorbing the GL halves listed above, `NullBackend` for tests | none | renderer tests headless against the null backend (sort, blend split, atlas split, clip bounds); sandbox screenshots identical |

## Open questions

1. Premultiplied alpha in PR 3. Recommendation: yes; the alternative keeps
   additive as a run split and leaves bloom compositing wrong, for the price of a
   tile-atlas premultiply and a one-LSB tolerance on the screenshot check.
2. `BakedChunkRenderer` draws its own VAOs with the uber program in mode 2.
   `beginBakedPass`/`endBakedPass` keeps the program private but admits a game
   draws under it; the alternative is renderer-owned world meshes, 29 game GL
   calls becoming API. Recommendation: the pass pair in PR 1, the mesh API when
   worldsim brings the chunk path through the engine.
3. Whether `backend/gl/` (the wrappers, `Shader`, `ShaderLoader`,
   `name(TextureHandle)`) stays includable by consumers; worldsim's chunk and
   planet renderers include them today. Recommendation: yes, an explicit opt-in
   path outside the layering check, dropped the day a second backend exists; the
   alternative is worldsim copying five headers now.
4. The groundcover and cursor uniforms are a worldsim feature inside salal's
   uber shader, and `setGroundcover` exists only for them. Recommendation: keep
   them through PR 1 under the behaviour-identical rule and file their move into
   a game-supplied shader variant separately.
5. `TextureHandle` as a distinct `Renderer::` struct rather than
   `renderer::ResourceHandle`'s alias. Recommendation: distinct; the alias cannot
   tell a texture from a mesh, and the namespace-case split is settled per type.
