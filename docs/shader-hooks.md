# Shader hooks

The renderer draws everything with one program, the base shader
(`libs/renderer/shaders/base.vert` and `base.frag` with their two includes). A
game that needs its own vertex math in that program, worldsim's grass parting
around the cursor for one, writes it as a hook: a GLSL file in the game's repo
that the game's build splices into the base shader's vertex stage. The GPU still
sees one program (dual-deck-builder's R5.1), the game's code stays in the game's
repo, and nothing about the shader is read or compiled from files at run time.

Added 2026-10-26, for `v0.6.0`.

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
`BaseShader.h`), goes on `<target>`'s include path as PUBLIC, and declares
everything in namespace `NAMESPACE` (default `base_shader`). Without `HOOKS` the
hook chain is empty and the base shader is what salal ships.

Call it once per set of hooks, on the library the executables share: worldsim
calls it on `engine`, and every executable that starts the renderer reaches the
same assembled source through it. `NAMESPACE` and `HEADER` are there so one
target can hold two (salal's own tests embed the bare base shader and one with a
test hook). The target links `salal::renderer`, whose `backend/Backend.h` the
header includes. The step is its own target, `<target>_<name>`, which
`<target>` waits for and which builds alone.

It needs `glslangValidator`: the vcpkg port `glslang` with its `tools` feature,
as a host dependency, in the consumer's manifest (a FetchContent consumer's
manifest is the one vcpkg reads):

```json
{ "name": "glslang", "host": true, "features": ["tools"] }
```

The tool that does the splicing, `salal_shader_assembler`, builds with the
renderer, from `libs/renderer/assembler/`.

## Hook files

A hooks folder holds hooks and nothing else. Each is `<name>.vert.glsl`, where
`<name>` is a GLSL identifier (letters, digits and underscores, not starting
with a digit) without a `gl_` prefix or a double underscore, the two GLSL
reserves. Only vertex hooks exist. Anything else in the folder, a folder
included, fails the build; files whose names start with a dot (`.DS_Store`)
are skipped.

`<name>` is the hook's prefix. Its hook-point functions are `<name>_<point>`,
and every other global it declares should carry the prefix too
(`sway_gust`, say); two hooks declaring the same name fail in
`glslangValidator`, as does a hook reusing one of the base shader's. A hook sees
the base shader's attributes and its `DrawUniforms` block, but should rely only
on its arguments and its own block.

## Hook points

Two, both on the instanced path (`Primitives::drawInstanced`, an
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
outside a block, a second block, or a block that is not `layout(std140)` fails
the build. An instance name equal to the hook's name keeps the members out of
the global scope:

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
when the linked block's size, padded to 16, is not the size the build
generated. A block the hook never reads is one `glslangValidator` drops, and the
build fails on it rather than generate a constant for nothing.

Every block starts zeroed, before the game first fills it, and a hook takes
zeros as off.

## The generated files

Under `<target>`'s binary directory, in `salal_base_shader/<target>/<name>/`:

- `base.vert` and `base.frag`, the two stages as checked, each hook's start and
  end marked. `glslangValidator`'s line numbers count in these.
- `reflection.txt`, `glslangValidator -q` on the linked stages, where the block
  sizes come from.
- `include/<HEADER>` and `<HEADER stem>.cpp`, which `<target>` compiles: the
  stages as raw string literals, in pieces (MSVC refuses one literal much past
  16 KB).

The header, for a target with one hook, `sway.vert.glsl`, and its block:

```cpp
namespace base_shader {
	// What Primitives::init() takes.
	const Renderer::backend::ShaderSource& source();

	// sway.vert.glsl's uniform block SwayUniforms: binding point 1, 16 bytes.
	inline constexpr Renderer::backend::HookUniformBlock kSway{
		.hook = "sway",
		.block = "SwayUniforms",
		.binding = 1,
		.size = 16,
	};
} // namespace base_shader
```

A hook's constant is `k` and its name in PascalCase (`wind_sway` is
`kWindSway`); a hook without a block has none. The size is std140's, padded to
16 bytes.

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
block's size and checked against the constant,

```cpp
struct SwayUniforms {
	Foundation::Vec2 wind{0.0F, 0.0F};
	int32_t			 enabled = 0;
	float			 stiffness = 0.0F;
};
static_assert(sizeof(SwayUniforms) == base_shader::kSway.size);

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

## What fails the build

At configure time: `HOOKS` that is not a folder, and no `glslangValidator`.
At build time, with the message on the build log:

- an entry in the hooks folder that is not `<name>.vert.glsl`, folders included;
- a `<name>` that is not a GLSL identifier, or is reserved;
- a hook that defines no hook point;
- a uniform outside a block, a second block, a block that is not std140, or a
  block name the base shader or another hook already has;
- more than 11 hooks with a block;
- a block the linked program does not have, because nothing reads it;
- either stage failing to compile, or the two failing to link, under
  `glslangValidator`. A hook point with the wrong parameters or return type is
  one of these: the chain's call to it does not match.

## Not covered

Fragment-stage hooks, any order but file names, SPIR-V and Vulkan. A game's own
passes (worldsim's tile shader, its planet renderer) still load their GLSL at
run time through `Shader` and `ShaderLoader`, and stage their own files.
