# Phase 0 audit: worldsim's ui layering

Read-only audit of worldsim main @ db335a2b (2026-10-25, clean checkout; no
commit has touched `libs/` since 2026-09-28). extraction.md's phase 0 asked
whether `libs/ui` respects worldsim's documented dependency layering or
reaches into `libs/engine`. It reaches in, narrowly, and there is a second
breach in the other direction that the docs never mention.

## How it was measured

A script resolved every `#include` in `libs/` against each library's declared
CMake include root plus the including file's own directory, and counted only
includes that resolved to a real header. The tables below are header
resolutions, not grep hits on directory names. Production files only unless a
row says otherwise; `*.test.cpp` and `*.bench.cpp` were tallied separately.
Symbol usage (`engine::Key` and so on) was a second pass by grep over
`libs/ui`.

The resolver is `tools/resolve-includes.mjs` in worldsim, a Node script added
in [worldsim#291](https://github.com/kcjonson/worldsim/pull/291). Re-run it
after each untangling step until the ui -> engine and renderer -> ui cells
read zero.

## The documented rule

`worldsim/docs/technical/monorepo-structure.md:26-45` ("Dependency Rules"):
`engine -> game-systems, ui, world -> renderer, geometry -> foundation`, no
circular dependencies. ui may depend on renderer and foundation (its stated
deps near line 70); engine may depend on ui; renderer may not depend on ui.
`worldsim/README.md:166-168` repeats the rule, as do
`worldsim/docs/workflows.md:138-140` and
`worldsim/docs/technical/cpp-coding-standards.md:699`. Worldsim's CLAUDE.md
says to follow the hierarchy strictly and warns that existing code may violate
the documented standards. The docs never mention `assets` (a separate
`add_library(assets)` under `libs/engine/assets`) or `planet-view`.

## Declared graph (CMake)

| target | target_link_libraries | include dirs | notes |
|---|---|---|---|
| foundation (`libs/foundation/CMakeLists.txt:28-37`) | PUBLIC glm; PRIVATE httplib, OpenGL::GL | own dir | GL and httplib are for `debug/DebugServer.cpp` (glReadPixels screenshots) |
| geometry | PUBLIC foundation | own dir | clean |
| renderer (`libs/renderer/CMakeLists.txt:28-36, 41-52`) | PUBLIC foundation, OpenGL, GLEW, glfw, glm; PRIVATE NanoSVG | own dir, plus PRIVATE `../ui` ("Include ui headers for FontRenderer ... include-only, not a link dependency, to avoid circular deps") | an acknowledged renderer -> ui reach |
| ui (`libs/ui/CMakeLists.txt:47-54`) | PUBLIC foundation, renderer, engine, httplib, nlohmann_json | own dir | declares engine as a PUBLIC dep; httplib is vestigial (zero `httplib.h` includes in libs/ui) |
| world | PUBLIC foundation | own dir | clean |
| planet-view | PUBLIC foundation, renderer, world, GL, GLEW, glm | own dir and `..` | planet-view-tests must link ui because renderer's Primitives TU references `ui::FontRenderer` |
| game-systems | PUBLIC foundation, renderer, world | own dir | placeholder, one 2-line file |
| engine (`libs/engine/CMakeLists.txt:73-83`) | PUBLIC foundation, geometry, renderer, ui, assets, world, nlohmann_json, glfw; PRIVATE winmm | own dir | no engine file includes nlohmann; dead link |
| assets (`libs/engine/assets/CMakeLists.txt`) | PUBLIC foundation, renderer, engine, pugixml; PRIVATE Lua | `libs/engine` | "engine dependency is for world/TileGrid.h access" |

Two declared cycles: ui <-> engine and engine <-> assets. CMake tolerates both
for static libraries. The root CMakeLists adds libs/ui before libs/engine with
a "dependency order" comment. The apps (ui-sandbox, world-sim, asset-manager)
link `engine ui renderer foundation assets ...`.

## Actual include graph

Resolved `#include` counts, production files only. Rows are the including
library, columns the library the header resolved into.

| src \ dst | foundation | geometry | renderer | ui | world | planet-view | engine | engine/assets | external |
|---|---|---|---|---|---|---|---|---|---|
| foundation | 20 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 106 |
| geometry | 2 | 86 | 0 | 0 | 0 | 0 | 0 | 0 | 158 |
| renderer | 27 | 0 | 58 | 1 | 0 | 0 | 0 | 0 | 120 |
| ui | 92 | 0 | 34 | 168 | 0 | 0 | 10 | 0 | 127 |
| world | 34 | 0 | 0 | 0 | 118 | 0 | 0 | 0 | 149 |
| planet-view | 2 | 0 | 1 | 0 | 6 | 11 | 0 | 0 | 38 |
| engine | 48 | 86 | 25 | 5 | 13 | 0 | 466 | 70 | 505 |
| engine/assets | 22 | 3 | 9 | 0 | 0 | 0 | 12 | 51 | 156 |

With tests the ui row is 93 / 0 / 34 / 202 / 0 / 0 / 10 / 0 / 154: the same
ten engine includes, nothing new. ui's external includes are std, gtest, glm,
nlohmann, stb_image, GL/glew and GLFW. ui has zero includes into world,
geometry, planet-view, assets or `apps/`. foundation, geometry, world,
planet-view and game-systems have zero violations.

Two cells are wrong by the documented rule: ui -> engine (10) and
renderer -> ui (1). The engine/assets -> engine cell (12) is a third cycle the
docs never address; it stays in worldsim and is noted at the end.

## ui -> engine: ten includes in eight files

All ten resolve to two small engine files. Grouped by cause.

### Input enums live in engine (seven includes)

`worldsim/libs/engine/input/InputTypes.h` is 133 lines, has no includes of its
own, and declares `enum class Key` and `enum class MouseButton` in
`namespace engine`.

| file | line | note |
|---|---|---|
| `worldsim/libs/ui/input/InputEvent.h` | 11 | public header; `InputEvent::button` is `engine::MouseButton` (lines 27, 41, 42) |
| `worldsim/libs/ui/focus/Focusable.h` | 4 | public header; pure-virtual `handleKeyInput(engine::Key, ...)` at line 51 |
| `worldsim/libs/ui/focus/FocusManager.h` | 4 | `routeKeyInput(engine::Key, ...)` at line 134 |
| `worldsim/libs/ui/components/contextmenu/ContextMenu.h` | 23 | |
| `worldsim/libs/ui/components/list/ListRow.h` | 18 | |
| `worldsim/libs/ui/components/button/Button.cpp` | 11 | |
| `worldsim/libs/ui/components/tabbar/TabBar.cpp` | 11 | |

Symbol reach through these headers: `engine::Key` and `engine::MouseButton`
appear in 26 ui files, 98 occurrences (Button, ContextMenu, Dialog,
DropdownButton, ListRow, Select, Slider, TabBar, TextInput, Focusable,
FocusManager, InputEvent, plus tests; 83 `engine::Key` and 15
`engine::MouseButton` in non-test files). The enums are in ui's public API
through `Focusable::handleKeyInput` and `InputEvent::button`, so whatever
header they move to, ui's consumers see it.

### Dead includes of InputManager.h (two includes)

`worldsim/libs/engine/input/InputManager.h` includes `<GLFW/glfw3.h>`.

| file | line | note |
|---|---|---|
| `worldsim/libs/ui/components/button/Button.cpp` | 10 | no InputManager symbol used |
| `worldsim/libs/ui/components/TextInput/TextInput.cpp` | 10 | same |

### TextInput calls the clipboard singleton (one include)

`worldsim/libs/ui/components/TextInput/TextInput.cpp:9` includes
`clipboard/ClipboardManager.h`, a GLFW-backed singleton, and calls
`engine::ClipboardManager::Get().setText(...)` at lines 460 and 470 and
`.getText()` at 478.

### What ui does not touch

No forward declarations of engine types anywhere in ui. No references to
InputManager (beyond the two dead includes), Scene, SceneManager, Application,
AppLauncher, ECS or Entity, Chunk, World, Colonist, or any asset type.

Side findings, not violations, that extraction will meet:
`worldsim/libs/ui/font/FontRenderer.h:9` includes `<GL/glew.h>` and `:8`
includes `shader/Shader.h`, so ui owns GL texture and shader state directly.
`worldsim/libs/ui/input/InputEvent.h:29` documents `modifiers` as "GLFW
modifier flags", an untyped int with GLFW semantics. Namespace spelling is `UI`
in 185 places and `ui` in 6, the six being the three FontRenderer files.

## renderer -> ui: the FontRenderer knot

The direction the rule forbids outright, and the real structural blocker for
extracting renderer on its own.

- `worldsim/libs/renderer/primitives/Primitives.cpp:12` includes
  `<font/FontRenderer.h>`, resolving to `libs/ui/font/FontRenderer.h`; `:28`
  holds `static ui::FontRenderer* g_fontRenderer`; `:382-384` builds
  `std::vector<ui::FontRenderer::GlyphQuad>` and calls
  `generateGlyphQuads(...)`.
- `worldsim/libs/renderer/primitives/Primitives.h:35` forward-declares
  `namespace ui { class FontRenderer; }` and exposes
  `setFontRenderer(ui::FontRenderer*)` at `:73` and `getFontRenderer()` at
  `:82` in renderer's public API.
- `worldsim/libs/renderer/CMakeLists.txt` adds `../ui` as a PRIVATE include
  dir for exactly this.

It links only because every final executable links ui; planet-view-tests had
to add ui to link at all.

FontRenderer itself (`worldsim/libs/ui/font/FontRenderer.h/.cpp`) includes
only foundation (`graphics/PrimitiveStyles.h`, `utils/Log.h`,
`utils/ResourcePath.h`), renderer (`primitives/FontFamily.h`,
`shader/Shader.h`) and third-party headers (GL, glm, nlohmann, stb_image). It
uses nothing else from ui and is already lowercase `namespace ui`. Callers:
`AppLauncher.cpp:37,195`, renderer's Primitives, and ui components including
`font/FontRenderer.h` (13 includes).

## foundation's reach

Zero internal violations. Third-party reach that matters for a reusable
foundation:

| file | line | what |
|---|---|---|
| `worldsim/libs/foundation/debug/DebugServer.cpp` | 12 | `<httplib.h>` |
| `worldsim/libs/foundation/debug/DebugServer.cpp` | 20 | `<GL/glew.h>`, for glReadPixels and glGetIntegerv in screenshots |
| `worldsim/libs/foundation/debug/DebugServer.cpp` | 260 | `std::thread serverThread`, with 10 mutexes, 2 condvars and atomics |
| `worldsim/libs/foundation/threading/TaskPool.{h,cpp}` | | a thread pool |
| `worldsim/libs/foundation/graphics/PngEncoder.cpp` | 7 | `<stb_image_write.h>` |
| `worldsim/libs/foundation/math/Types.h` | 6-8 | glm, PUBLIC |
| `worldsim/libs/foundation/utils/ResourcePath.cpp` | 17 | `<windows.h>` |
| `worldsim/libs/foundation/utils/Log.h` | 45 | `Logger::setDebugServer`; the logger knows the server |

So foundation is neither GL-free nor headless: the HTTP dev server sits at the
bottom of the stack. There is no compile flag; zero `#if` in DebugServer.cpp/h.
The only gate is runtime, `AppConfig::enableDebugServer` (default false) and
`--http-port`. architecture.md assigns the dev server to `app`; that is the
target, and this is the current state.

## engine -> ui: the allowed direction, and the seam

Five include lines in three files, all in `application/` and `scene/`. Nothing
in ecs/, world/, construction/, nav/, vision/, input/, clipboard/ or assets/
touches ui.

| file | line | what |
|---|---|---|
| `worldsim/libs/engine/application/Application.cpp` | 3, 8 | `focus/FocusManager.h`, `input/InputEvent.h`; constructs `UI::FocusManager` (`:48-49`), builds `UI::InputEvent` from GLFW callbacks (`:163-194`), `getFocusManager()` (`:355`) |
| `worldsim/libs/engine/application/Application.h` | 16 | forward-declares `namespace UI { class FocusManager; }`; members at `:121`, `:146` |
| `worldsim/libs/engine/application/AppLauncher.cpp` | 11, 12 | `font/FontRenderer.h`, `input/InputEvent.h`; owns `std::unique_ptr<ui::FontRenderer>` (`:37`, `:195`); injects synthetic `UI::InputEvent`s from the dev server (`:52-80`) |
| `worldsim/libs/engine/scene/SceneManager.cpp` | 3 | `input/InputEvent.h`; `SceneManager::handleInput(UI::InputEvent&)` (`SceneManager.h:129`, `.cpp:215`) |
| `worldsim/libs/engine/scene/Scene.h` | 7 | forward declaration only; `Scene::handleInput(UI::InputEvent&)` `:80`, `Scene::getUiRoots() -> std::vector<const UI::IComponent*>` `:85` |
| `worldsim/libs/engine/scene/Overlay.h` | 3 | forward declaration only; `Overlay::handleEvent(UI::InputEvent&)` `:26` |

Legal under worldsim's rule and under salal's (app sits above ui). It matters
because it is the exact surface `app` will cut along: `Application.cpp`,
`SceneManager.*`, `InputManager.*` and `ClipboardManager.*` include only
engine-self, foundation, ui and GLFW. `AppLauncher.cpp` is the odd one in
application/: it also pulls `assets/AssetRegistry.h`,
`assets/RecipeRegistry.h`, engine's `world/chunk/Chunk.h`, renderer's
`resources/TileAtlasBuilder.h` and foundation's `debug/DebugServer.h`.
[app-carve-out.md](app-carve-out.md) has that file line by line.

## Verdict

libs/ui does not respect its documented layering today, but the breach is
narrow and shallow. Every one of the ten violating includes resolves to a
133-line include-free enum header or a GLFW clipboard singleton. ui never
reaches Scene, Application, AppLauncher, ECS, chunks, world, geometry,
planet-view, assets or apps. The declared CMake graph is worse than the real
include graph: ui PUBLIC-links all of engine, which drags geometry, world,
assets, nlohmann and glfw into ui-sandbox and into anything else that links
ui.

Two header moves (InputTypes below ui, FontRenderer into renderer), one
clipboard seam and CMake cleanup gate the extraction; ui's design does not,
and nothing requires an interface redesign of ui itself. The reverse knot
(renderer -> ui) and foundation's GL and httplib reach were outside the
question as asked and are the two findings that change the plan: both have to
be cut before phase 1 can extract renderer and foundation and leave worldsim
building.

## Fix list

Costs: trivial is a one-PR mechanical change with no API decision; moderate
needs a small interface or a namespace decision; hard would need a redesign.
Nothing here is hard. The order is the "untangle worldsim" step (0.5) in
[extraction.md](extraction.md); each item is its own small PR in worldsim that
leaves it building and running identically.

| step | change | cost | notes |
|---|---|---|---|
| a | Delete the two dead `InputManager.h` includes (`Button.cpp:10`, `TextInput.cpp:10`) | trivial | |
| b | Move `input/InputTypes.h` below ui; engine's `InputManager.h:3` includes it from the new place | trivial | foundation if decision D in [open-decisions.md](open-decisions.md) goes as proposed, `libs/ui/input/` otherwise. Leave the `engine::` namespace for the first pass; a rename to `UI::` touches ui 26 files / 98 uses, engine 2 / 15, apps/world-sim 13 / 54, apps/ui-sandbox 3 / 20, asset-manager 0 |
| c | Clipboard seam: a small interface or `std::function` hooks in ui that `Application.cpp:43-44` installs; TextInput stops calling `engine::ClipboardManager::Get()` | moderate | alternative: move ClipboardManager (one .h/.cpp; deps GLFW, string, Log) down as a platform service. ui already has GLFW transitively through renderer's PUBLIC link |
| d | Move FontRenderer from `libs/ui/font` to `libs/renderer/font`, namespace `Renderer`; drop renderer's PRIVATE `../ui` include dir | moderate | 13 includes plus `AppLauncher.cpp:37,195` and Primitives to retarget. `Renderer` rather than the standards doc's lowercase because FontFamily, Primitives, BatchRenderer, Shader, CoordinateSystem, metrics/, gl/ and resources/Tile* are all `namespace Renderer` (only vector/ and two resources/ files are lowercase); a lowercase FontRenderer would force `Renderer::FontFamily` qualifiers and a second namespace block in Primitives.h, the shape being removed. Alternative kept for the record: a text-provider interface in renderer that ui implements, the shape `setFontRenderer` injection already has |
| e | CMake: ui drops `engine` and `httplib`; engine drops `nlohmann_json`; re-run the resolver and confirm ui -> engine and renderer -> ui are both zero | trivial | planet-view-tests can then drop ui |
| f | Move DebugServer, LockFreeRingBuffer, PerformanceMetrics and PngEncoder out of foundation into `libs/engine` (the proto-app); invert `Logger::setDebugServer` into a generic sink callback; foundation drops OpenGL::GL and httplib | moderate | could land in phase 1 instead, but must land before foundation is extracted |

Not on the list: the engine <-> assets declared cycle (assets -> engine, 12
production includes, all in
`worldsim/libs/engine/assets/placement/{AsyncChunkProcessor.h:17-22, PlacementExecutor.cpp:6-9, PlacementExecutor.h:51-52}`
into engine's `world/chunk/*`, `world/rendering/*` and `world/Biome.h`). It is
irrelevant to ui and `assets` stays in worldsim, so it is worldsim's own
problem.

## What the audit did not check

- Which storage model ui's Layer actually uses. Worldsim's
  colonysim-integration doc records two contradictory decisions (shared_ptr on
  2025-10-29, "value semantics with std::variant from the start" on
  2025-11-03) and the code was not read; `libs/ui/layer` is 176 lines and
  components are 12,765. Open decision F.
- Layering in `apps/`. Game code polls `InputManager::Get()` in 8 places
  under apps/, and two sandbox scenes poll GLFW directly
  (`worldsim/apps/ui-sandbox/scenes/ClipScene.cpp:89-102`,
  `VectorPerfScene.cpp:52`); none of that was tallied against a rule because
  the docs state none for apps.
- The test-only include graph beyond the ui row.
- Whether the `UI` / `ui` namespace split, or the `Foundation` / `foundation`
  and `Renderer` / `renderer` splits the fact-check found, cost anything at
  link time. Spelling, not structure; left alone. One worldsim follow-up from
  it: settle renderer's lib-wide namespace case (`Renderer` vs `renderer`; the
  standards doc says lowercase, the code and now FontRenderer say `Renderer`).
- Whether msdfgen is a build-time or runtime dependency.
- Runtime behaviour of any kind. Nothing was built or run.
