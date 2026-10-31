# Shader hooks

The renderer draws everything with one program, the base shader
(`libs/renderer/shaders/base.vert` and `base.frag` with their two includes). A
game that needs its own vertex math in that program, worldsim's grass parting
around the cursor for one, writes it as a hook: a GLSL file in the game's repo
that the game's build splices into the base shader's vertex stage. A game that
needs its own surface, worn paper for one, writes a fill hook, spliced into the
fragment stage and picked per rect ("Fill hooks", below). The GPU still sees one
program, the game's code stays in the game's repo, and nothing about the shader is read or compiled from files
at run time.

Added 2026-10-26, for `v0.6.0`. Fill hooks designed 2026-10-31, not yet built.

## The CMake function

```cmake
salal_base_shader(<target> [HOOKS <dir>] [NAMESPACE <name>] [HEADER <file>])
```

`cmake/SalalBaseShader.cmake`, included by salal's top-level `CMakeLists.txt`,
so a consumer has it after `FetchContent_MakeAvailable(salal)`. At build time it
assembles `base.vert` and `base.frag` with every include resolved and the hooks
in `<dir>` spliced into the vertex stage, checks the two stages compiled and
linked as one program with `glslangValidator`, and compiles the result into
`<target>` as a generated source. The header, `HEADER` (default
`BaseShader.h`; a relative path, folders allowed, `worldsim/BaseShader.h`
say), goes on `<target>`'s include path as PUBLIC, and declares everything in
namespace `NAMESPACE` (default `base_shader`, a plain C++ identifier). Without
`HOOKS` the hook chain is empty and the base shader is what salal ships. A
keyword given without a value fails at configure.

Call it once per set of hooks, on the library the executables share: worldsim
calls it on `engine`, and every executable that starts the renderer reaches the
same assembled source through it. `NAMESPACE` and `HEADER` are there so one
target can hold two (salal's own tests embed the bare base shader and one with a
test hook); two calls on one target that share a `HEADER` or a `NAMESPACE` fail
at configure. The generated source includes its header by a path from itself, so
nothing else on the include path can stand in for it. The target links `salal::renderer`, whose `backend/Backend.h` the
header includes. The step is its own target, `<target>_<name>`, which
`<target>` waits for and which builds alone.

It needs `glslangValidator`: the vcpkg port `glslang` with its `tools` feature,
as a host dependency, in the consumer's manifest (a FetchContent consumer's
manifest is the one vcpkg reads):

```json
{ "name": "glslang", "host": true, "features": ["tools"] }
```

The tool that does the splicing, `salal_shader_assembler`, builds with the
renderer, from `libs/renderer/assembler/`. It writes the header and source only
when what it generates changes, so a hook edit that leaves the header alone
recompiles nothing that includes it.

## Hook files

A hooks folder holds hooks and nothing else. Each is `<name>.vert.glsl`, a
vertex hook, or `<name>.frag.glsl`, a fill hook, where `<name>` is a GLSL
identifier (letters, digits and underscores, not starting with a digit) that
makes no reserved name of `<name>_<point>`: not `gl`, no `gl_` prefix, no
trailing or double underscore. A name is one hook, so `sway.vert.glsl` beside
`sway.frag.glsl` fails the build. Anything else in the folder, a folder
included, fails the build. Entries whose
names start with a dot (`.DS_Store`, an editor's lock file or swap folder) are
left out before anything reads them, and a leading UTF-8 byte-order mark is
dropped.

A hook is spliced into the middle of a `#version 330 core` vertex stage, so it
brings no `#version` or `#extension` of its own (desktop GLSL wants an
extension directive before any code, which a hook never is), and no `binding`
layout qualifier: the build gives its block a point. All three fail the build.

`<name>` is the hook's prefix. Its hook-point functions are `<name>_<point>`,
and every other global it declares should carry the prefix too
(`sway_gust`, say); two hooks declaring the same name fail in
`glslangValidator`, as does a hook reusing one of the base shader's. A hook sees
the base shader's attributes and its `DrawUniforms` block. On the instanced-mesh
path, where hooks run, only `a_position` and `a_color` (0 and 2) carry data; the
batched quad's per-instance attributes (1, 3 to 5 and 8 to 12) read GL's
defaults there. It doesn't see the mesh's instance attributes `a_instanceData1`
and `a_instanceData2`, which
`includes/instancing.glsl` declares after the hooks; it should rely only on its
arguments and its own block. A `#define` in a hook stays live for the rest of the
vertex stage, `instancing.glsl` and `main` included (`#define scale 2.0` breaks
the instance transform; `#define u_cameraZoom 2.0` changes it quietly), so
`#undef` what you define, or use a `const`.

## Hook points

The vertex stage has two, both on the instanced path (`Primitives::drawInstanced`, an
`InstancedMesh` geometry): every instanced vertex runs both, in this order.
Batched shapes, text, images and the baked pass run neither.

| point | signature | where |
|---|---|---|
| `instanceLocal` | `vec2 <name>_instanceLocal(vec2 position, vec2 instancePosition)` | the vertex in mesh space, before the instance's rotation, scale and translation |
| `instanceWorld` | `vec2 <name>_instanceWorld(vec2 position, vec2 localPosition, vec2 instancePosition)` | the vertex in world space, after the instance transform and before world to screen |

Each returns the position it was given, moved or not. `instancePosition` is the
instance's world position (`InstanceData::worldPosition`); `localPosition` is
the mesh vertex as uploaded, before any `instanceLocal` hook. A hook defines
either point or both, and one that defines neither fails the build.

## The chain

The build generates the functions `base.vert` calls, through
`#include "hooks.glsl"` (which no file in the source tree answers: the build
does) and `includes/instancing.glsl`:

```glsl
vec2 applyInstanceLocalHooks(vec2 position, vec2 instancePosition) {
	position = sway_instanceLocal(position, instancePosition);
	return position;
}

vec2 applyInstanceWorldHooks(vec2 position, vec2 localPosition, vec2 instancePosition) {
	position = sway_instanceWorld(position, localPosition, instancePosition);
	return position;
}
```

Each point calls every hook that defines it, in file-name order (byte order of
the names), each taking the previous one's result. With no hooks both return
`position`. The chain runs every hook for every instanced vertex, so a hook
switches itself off from its own uniforms, and a draw that is not its concern
(worldsim's trees, under its grass hook) passes through untouched.

## Uniform blocks

A hook may declare one uniform block, and its uniforms go there: a uniform
outside a block, a second block, a block that is not `layout(std140)`, or an
array of blocks fails the build. An instance name equal to the hook's name keeps
the members out of the global scope:

```glsl
layout(std140) uniform SwayUniforms {
	vec2  wind;      //  0
	int   enabled;   //  8
	float stiffness; // 12
} sway;
```

The build gives the blocks binding points from 1 up, in file-name order, to the
hooks that have one; 0 is `DrawUniforms`. GL 3.3 core promises 12 uniform blocks
a stage, so at most 11 hooks may have one. GL 3.3 core has no
`layout(binding = N)` either, so the GL backend binds each block's index to its
point after link (`glUniformBlockBinding`), and `createPipeline` fails, logged,
when the linked block's size, padded to 16, or any member's offset is not what
the build generated; it checks every block before it makes anything, so a
pipeline that fails leaves no ring behind. A block the hook never reads is one
`glslangValidator` drops, and the build fails on it rather than generate a
constant for nothing. A block a driver reports inactive where glslang kept it (a
read that folds away, `x * 0.0`) is logged once and skipped: no ring, and writes
to its point are dropped, rather than no pipeline at all.

Every block starts zeroed, before the game first fills it, and a hook takes
zeros as off.

## The generated files

Under `<target>`'s binary directory, in `salal_base_shader/<target>/<name>/`:

- `base.vert` and `base.frag`, the two stages as checked, each hook's start and
  end marked. `glslangValidator`'s line numbers count in these.
- `reflection.txt`, `glslangValidator -q --reflect-all-block-variables` on the
  linked stages, where the block sizes and member offsets come from. Any
  warning from `glslangValidator` fails the build, as an error does.
- `include/<HEADER>` and `<HEADER stem>.cpp`, which `<target>` compiles: the
  stages as raw string literals, in pieces (MSVC refuses one literal much past
  16 KB).

The header, for a target with one hook, `sway.vert.glsl`, and its block:

```cpp
namespace base_shader {
	// What Primitives::init() takes.
	const Renderer::backend::ShaderSource& source();

	// sway.vert.glsl's uniform block SwayUniforms: binding point 1, 16 bytes, and its
	// members' std140 offsets and sizes.
	inline constexpr Renderer::backend::HookUniformMember kSwayWind{.name = "SwayUniforms.wind", .type = "vec2", .offset = 0, .size = 8};
	inline constexpr Renderer::backend::HookUniformMember kSwayEnabled{.name = "SwayUniforms.enabled", .type = "int", .offset = 8, .size = 4};
	inline constexpr Renderer::backend::HookUniformMember kSwayStiffness{.name = "SwayUniforms.stiffness", .type = "float", .offset = 12, .size = 4};
	inline constexpr Renderer::backend::HookUniformMember kSwayMembers[] = {kSwayWind, kSwayEnabled, kSwayStiffness};
	inline constexpr Renderer::backend::HookUniformBlock kSway{
		.hook = "sway",
		.block = "SwayUniforms",
		.binding = 1,
		.size = 16,
		.members = kSwayMembers,
	};
} // namespace base_shader
```

A hook's constant is `k` and its name in PascalCase (`wind_sway` is
`kWindSway`); a hook without a block has none. The size is std140's, padded to
16 bytes. Each member's constant adds the member's name in PascalCase
(`kSwayWind`), with its name as GL names it, its GLSL type, its std140 offset,
and its size in bytes, an array's elements at their std140 stride (`float[2]`
is 32); `k<Name>Members` lists them in offset order. Two constants that come
out the same fail the build, as does a member type outside the scalars,
vectors and square matrices.

## The C++ side

```cpp
Renderer::Primitives::init(std::move(backend), base_shader::source());
```

`Primitives::init` takes the assembled source with the backend; the text needs
to outlive only the call. `Backend::createPipeline` takes source text, and
`ShaderSource` carries the hook blocks with it.

A game fills a hook's block with

```cpp
void Primitives::setHookUniforms(const backend::HookUniformBlock& block, std::span<const std::byte> bytes);
void Primitives::setHookUniforms(std::string_view hook, std::span<const std::byte> bytes);
```

by the generated constant or by the hook's name. `bytes` is the block's std140
contents, exactly its size: a C++ struct laid out like the GLSL, padded to the
block's size and checked member by member against the constants, so a block
reordered or grown in the GLSL stops the C++ from compiling,

```cpp
struct SwayUniforms {
	Foundation::Vec2 wind{0.0F, 0.0F};
	int32_t			 enabled = 0;
	float			 stiffness = 0.0F;
};
static_assert(sizeof(SwayUniforms) == base_shader::kSway.size);
static_assert(std::size(base_shader::kSwayMembers) == 3);
static_assert(offsetof(SwayUniforms, wind) == base_shader::kSwayWind.offset);
static_assert(sizeof(SwayUniforms::wind) == base_shader::kSwayWind.size);
// ... and the same for enabled and stiffness

const SwayUniforms sway{.wind = {0.3F, 0.0F}, .enabled = 1, .stiffness = 0.8F};
Renderer::Primitives::setHookUniforms(base_shader::kSway, std::as_bytes(std::span(&sway, 1)));
```

The instanced draws issued after the call read the new values, and a draw
issued before keeps the ones it was given, the order `setUniforms` keeps for
`DrawUniforms`. A hook the base shader `init()` was given has no block for, a
constant from another assembled shader, or the wrong size is logged and
ignored.

Under it is `Backend::setHookUniforms(uint32_t binding, std::span<const std::byte>)`.
The GL backend keeps a uniform ring per binding point, the machinery
`DrawUniforms` has: 512 slots, the next one written for each new set of values
and bound with `glBindBufferRange`, orphaned when full, and a repeat of the last
values bound again rather than written. `invalidateState()` rebinds every point
the renderer owns, 0 and up, so a game's GL that binds a uniform buffer to one
of them calls it first. The null backend records each write
(`hookUniformWrites()`) and the values each draw read
(`RecordedDraw::hookBlock(binding)`).

## Fill hooks

Designed 2026-10-31. A fill hook is a game's surface: a function of the pixel's
place in a rect that gives the rect's colour there. Worn paper is the case it
was designed for: grime at the edges and corners over a flat reading window,
seeded per surface, which would otherwise be baked into nine-sliced raster art
that salal doesn't draw (ui-design.md, "Remaining rules"). Noise computed where it is
drawn is what an SVG filter such as `feTurbulence` does, without the offscreen
pass a filter needs, so a hooked rect is one more quad in the batch, with no
texture and no split, and is sharp at any pixel ratio.

### The hook point

`<name>.frag.glsl` defines one function,

```glsl
vec4 <name>_fill(vec4 color, vec2 local, vec2 halfSize, uvec3 params, float pixelSize);
```

and returns the rect's premultiplied colour at the pixel. `color` is the rect's
fill, premultiplied; `local` is the pixel from the rect's centre in its local
units, y down, the point the rect's distance is measured from; `halfSize` is
the rect's half size in the same units; `params` is the draw's twelve bytes;
`pixelSize` is one screen pixel in local units, for a hook that softens its own
edges. A hook that defines no `<name>_fill`, or defines it with other
parameters or another return type, fails the build.

What the hook returns stands where the rect's flat fill stood: the rect mode
still covers the box by its signed distance, rounds its corners, lays its
border over it and applies the clip. A hook changes the colour of the surface,
never its shape; a pixel it wants empty returns alpha 0.

### Picking a hook

Vertex hooks chain, every hook running for every vertex. A fill hook is picked:
a rect names one, or none, and only that one runs. The build gives the fill
hooks indexes from 1 up, in file-name order, and generates the function
`base.frag`'s rect mode calls:

```glsl
vec4 applyFillHook(uint fill, vec4 color, vec2 local, vec2 halfSize, uvec3 params, float pixelSize) {
	switch (fill) {
		case 1u: return paper_fill(color, local, halfSize, params, pixelSize);
	}
	return color;
}
```

With no fill hooks it returns `color`, and a plain rect, index 0, pays one
comparison a pixel. At most 255 fill hooks.

### Where the index and the params ride

In the 104-byte instance (ui-design.md, "Per-draw data"), with no field added:

- The index is the texture-slot byte, which the rect mode doesn't read.
- The params are corners 1 to 3 of the premultiplied fill. A hooked rect has one
  fill colour, corner 0's, which the vertex stage hands every vertex; it hands
  the other three corners to the fragment stage untouched, as a flat `uvec3`
  varying, `v_fillParams`. That is one more flat varying for every mode, 32 of
  GL 3.3's 64 vertex output components in use after it.

A rect's linear gradient is the four corner colours, so a hooked rect can't
have one; the batcher logs a gradient on a hooked rect once per `id` and draws
the hook over the style's `fill`.

### Rules the fragment stage adds

A fill hook follows the vertex hooks' rules (no `#version`, `#extension` or
`binding`, its globals carrying its prefix, a `#define` live for the rest of the
stage, so `#undef` it), plus three for where it runs:

- **No derivatives.** It runs inside the rect mode's branch, after the clip's
  `discard`, where `dFdx`, `dFdy` and `fwidth` are undefined. A hook that names
  any of them fails the build; `pixelSize` is taken before the branch for it.
- **No textures.** It can't name `u_textures`, the build fails if it does, and a
  sampler can't be declared outside a uniform block, which already fails.
- **One uniform block, under the vertex hooks' rules.** Its block is in the
  fragment stage, and fill and vertex hooks share the binding points 1 to 11 and
  the limit of 11 hooks with a block, in file-name order across both. The C++
  side fills it as any hook's block, `Primitives::setHookUniforms`, and a
  hooked rect drawn after the call reads the new values.

A fill hook runs per pixel in floats, and two GPUs can differ in its last bits.
A game holds a surface steady by seeding it, never by expecting a bit pattern;
there are no stored images to compare it to (testing/README.md).

### The C++ side

The generated header has a constant for each fill hook, the hook's name in
PascalCase with `Fill` after it:

```cpp
// paper.frag.glsl, fill index 1.
inline constexpr Renderer::backend::FillHook kPaperFill{.hook = "paper", .index = 1};
```

`ShaderSource` carries the fill hooks beside the hook blocks, and `RectArgs`
takes one, in a field after `style`:

```cpp
struct Fill {
	const backend::FillHook* hook = nullptr; // none: the style's flat fill
	std::array<uint32_t, 3>	 params{};		 // the hook's, packed as it reads them
};

Renderer::Primitives::drawRect({
	.bounds = sheet,
	.style = {.fill = paperColour, .border = inkRule},
	.fill = {.hook = &base_shader::kPaperFill, .params = packPaper(seed, wear)},
});
```

A hook from another assembled shader is logged and drawn as a plain rect,
as `setHookUniforms` treats a constant it wasn't given. The null backend records
the index and params with each instance, so a game's test checks what it asked
for without a GPU.

ui grows nothing for it. A game's own component draws a hooked rect through
`Primitives::drawRect` in its render, as any component draws.

### Building it

One PR, for the next minor version:

- the assembler: `.frag.glsl` files, the `fill` point, `applyFillHook`, the
  three rules above and the failures below, and `FillHook` constants in the
  header;
- `base.vert` and `base.frag`: the index and `v_fillParams`, and the call in
  the rect mode;
- `RectArgs::fill`, the batcher's packing, and the null backend's record;
- a sandbox scene, `fill_hook`, with a test hook that stripes a rect from its
  params, on rects with borders, rounded corners and a clip, at 0 on the lint;
- tests: the assembler's rules and the generated chain, a GL readback of the
  test hook against the value its params give, the null backend's record;
- the perf suite against `main` (`node scripts/perf.mjs compare`), since it
  changes the shader and the batcher: plain rects pay a comparison and a
  varying, and must hold the gate.

## What fails the build

At configure time: `HOOKS` that is not a folder, a keyword without a value, a
`NAMESPACE` that is not a plain identifier, a `HEADER` outside the include
folder, a second call on a target with the same `HEADER` or `NAMESPACE`, and no
`glslangValidator`. At build time, with the message on the build log:

- an entry in the hooks folder that is not `<name>.vert.glsl` or
  `<name>.frag.glsl`, folders included (dot entries aside);
- a `<name>` that is not a GLSL identifier, or makes a reserved name, or is
  both a vertex and a fill hook;
- a vertex hook that defines no hook point, or a fill hook that defines no
  `<name>_fill`;
- a fill hook that names `dFdx`, `dFdy`, `fwidth` or `u_textures`, or more than
  255 fill hooks;
- `#version`, `#extension` or a `binding` qualifier in a hook;
- a uniform outside a block, a second block, a block that is not std140, an
  array of blocks, or a block name the base shader or another hook already has;
- more than 11 hooks with a block;
- a block the linked program does not have, because nothing reads it, or a
  member the generated header can't describe or name;
- either stage failing to compile, the two failing to link, or any warning,
  under `glslangValidator`. A hook point with the wrong parameters or return
  type is one of these: the chain's call to it does not match.
- `glslangValidator` gone from where configure found it.

## Not covered

Fragment hooks other than a rect's fill (on text, images, arcs or polygons, or
a hook that changes coverage), a gradient under a fill hook, any order but file
names, SPIR-V and Vulkan. A game's own
passes (worldsim's tile shader, its planet renderer) still load their GLSL at
run time through `Shader` and `ShaderLoader`, and stage their own files.
