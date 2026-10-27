# Carving app out of worldsim's libs/engine

The map of `worldsim/libs/engine` for the phase-2 carve-out: which files are
generic, where they knot into the game, ui and renderer, and what the proposed
`libs/app` slice weighs. Surveyed read-only against worldsim main @ db335a2b
on 2026-10-25. This document is the map. The design of the new app API (tick,
scene stack, config, the device layer that feeds the input accumulator) is
phase-2 work and is not written here; the input types themselves are settled
(decision D) and live in `foundation/input/`. What follows is meant to let that
design be written without re-reading worldsim.

Eight phase 0.5 PRs ([worldsim#291](https://github.com/kcjonson/worldsim/pull/291) through [worldsim#298](https://github.com/kcjonson/worldsim/pull/298)) merged 2026-10-25/26, taking
worldsim main to 870dd755. Counts, sizes and line numbers below are the survey
at db335a2b and stay as the record; rows that changed carry a trailing note.
Paths that moved: `libs/ui/font/FontRenderer.*` and the stb_image TU to
`libs/renderer/font` and `libs/renderer` ([worldsim#293](https://github.com/kcjonson/worldsim/pull/293)); `libs/foundation/debug/*`
to `libs/engine/debug`, `foundation/graphics/PngEncoder.*` to
`libs/engine/graphics`, and `foundation/metrics/PerformanceMetrics.h` to
`libs/renderer/metrics` ([worldsim#297](https://github.com/kcjonson/worldsim/pull/297)); `input/InputTypes.h` to `libs/foundation/input`
([worldsim#298](https://github.com/kcjonson/worldsim/pull/298)). `AppLauncher.cpp` line numbers predate
[worldsim#296](https://github.com/kcjonson/worldsim/pull/296) and [worldsim#297](https://github.com/kcjonson/worldsim/pull/297), which trimmed it.

`libs/engine` is 294 .h/.cpp files and 75,113 lines with tests and benches,
across two CMake targets: `engine` (236 files, 60,938 lines) and `assets` (58
files, 14,175 lines, `libs/engine/assets/CMakeLists.txt`). Tests and benches
are 29,873 of the 75,113 lines (40%).

## Classification

| directory | files | lines | class | why |
|---|---|---|---|---|
| `application/` | 6 | 1,231 | generic, body mixed | window, GL context, bootstrap, loop. `AppLauncher.cpp` carries four game knots. `Placeholder.cpp` is two dead lines |
| `scene/` | 4 | 530 | generic | `IScene`, `IOverlay`, `SceneManager`. The only outside types are forward-declared `UI::InputEvent` and `UI::IComponent` |
| `input/` | 4 | 1,032 | generic | keyboard, mouse and scroll over GLFW. No gamepad anywhere in worldsim |
| `clipboard/` | 2 | 109 | generic | GLFW clipboard wrapper, singleton |
| `assets/` (direct) | 42 | 10,107 | mixed, lands game | mechanism (XML defs, Lua generators, SVG tessellation, template cache, async load: AssetRegistry, AssetDefinition, IAssetGenerator, AssetRenderer, AssetBatcher, SvgPathNodes, MotionDef/Eval, AssetValidator) is engine-grade, but the schema is the colony sim: `AssetDefinition.h:23-110` Edible, Drinkable, Sleepable, Toilet, Waste, Carryable, Harvestable, Craftable, Storage; `ItemProperties.h`. ActionTypeRegistry, RecipeRegistry, TaskChainRegistry, WorkTypeRegistry, PriorityConfig, ConstructionRegistry, ConfigValidator are game |
| `assets/lua/` | 4 | 476 | mixed, lands game | sol2 host for asset generator scripts |
| `assets/placement/` | 12 | 3,592 | game | chunk placement; includes world/chunk/*, world/rendering/* |
| `ecs/` (direct) | 11 | 1,938 | mixed | 7 header-only core files are generic (689 lines). GoalTaskRegistry (576) and InventoryMass.h plus test (673) are game |
| `ecs/components/` | 38 | 5,230 | game | |
| `ecs/spatial/` | 3 | 179 | game | |
| `ecs/systems/` | 47 | 21,617 | game | TimeSystem is game time, not an engine clock |
| `ecs/systems/action/` | 5 | 1,773 | game | |
| `construction/` | 17 | 6,284 | game | |
| `nav/` | 4 | 1,783 | game | |
| `vision/` | 3 | 580 | game | |
| `world/` (direct) | 6 | 584 | game | `Biome.h:6` includes libs/world |
| `world/camera/` | 1 | 170 | game | WorldCamera is a 2D pan/zoom camera in concept but typed on WorldPosition and ChunkCoordinate. A candidate for a later generic Camera2D |
| `world/chunk/` | 46 | 13,467 | game | |
| `world/generation/` | 12 | 574 | game | |
| `world/rendering/` | 27 | 3,857 | game | |

Generic slice today: application + scene + input + clipboard, 16 files, 2,902
lines. Everything else, 278 files and about 72k lines, stays in worldsim.

Build-graph facts that shape the cut. `worldsim/libs/engine/CMakeLists.txt`
links PUBLIC foundation, geometry, renderer, ui, assets, world, nlohmann_json
and glfw, and no engine file includes nlohmann.
`libs/engine/assets/CMakeLists.txt` links PUBLIC engine; the real edges are
`assets/placement/AsyncChunkProcessor.h:17-22`, `PlacementExecutor.cpp:6-9`
and `PlacementExecutor.h:51-52` into world/chunk, world/rendering and
world/Biome.h, an engine <-> assets cycle. `libs/ui/CMakeLists.txt:47-54`
links PUBLIC engine. `libs/foundation/CMakeLists.txt:37` links OpenGL::GL
because DebugServer calls glReadPixels.
`libs/renderer/primitives/Primitives.h:35-36,73` forward-declares
`ui::FontRenderer`. [phase-0-audit.md](phase-0-audit.md) has the include
graph.

Since the survey: engine no longer links `nlohmann_json` and ui no longer links
`httplib` ([worldsim#295](https://github.com/kcjonson/worldsim/pull/295)), foundation links only glm ([worldsim#297](https://github.com/kcjonson/worldsim/pull/297)), and FontRenderer is
`Renderer::FontRenderer` in `libs/renderer/font`, so renderer no longer reaches
into ui ([worldsim#293](https://github.com/kcjonson/worldsim/pull/293)). ui no longer links engine: `input/InputTypes.h` moved to
`libs/foundation/input` ([worldsim#298](https://github.com/kcjonson/worldsim/pull/298)).

## Knots

### AppLauncher.cpp

The only generic file with game dependencies.

| line | include / use | layer | note |
|---|---|---|---|
| :8, :244, :251, :254 | `<assets/AssetRegistry.h>`; setSharedScriptsPath, beginLoadAsync, loadDefinitionsFromFolder | engine/assets (game schema) | bootstraps the game's asset pipeline inside the launcher (moved out, 2026-10-25/26) |
| :9, :261 | `<assets/RecipeRegistry.h>`; `loadRecipesFromFolder("assets/recipes")` | game (crafting) | (moved out, 2026-10-25/26) |
| :24, :211-217 | `"world/chunk/Chunk.h"`; Surface::Count, surfaceToString, Chunk::getSurfaceColor | game, transitively libs/world | builds a default tile atlas for terrain surfaces at startup (moved out, 2026-10-25/26) |
| :16, :38, :213-221 | `<resources/TileAtlasBuilder.h>` | renderer | `TileAtlasBuilder.cpp:64` hardcodes `assets/tiles/surfaces/<name>/pattern.svg` (moved out, 2026-10-25/26) |
| :231-264 | initializeAssetSystem | game paths | hardcodes `assets/shared/scripts` and `assets/recipes`; `AppConfig.h:46` defaults assetsRootPath to `assets/world` (moved out, 2026-10-25/26) |
| :11, :37, :195-205 | `<font/FontRenderer.h>`; `ui::FontRenderer` owned as g_fontRenderer | ui | the app layer constructing a ui object and wiring it into Primitives (moved out of ui, 2026-10-25/26: now `Renderer::FontRenderer`) |
| :12, :52, :66-80 | `<input/InputEvent.h>`; UI::InputEvent::mouseMove/Down/Up/scroll | ui | injected-input dispatch from the dev server |
| :7, :171-176 | CoordinateSystem (function-static instance) | renderer | |
| :15, :113, :189-205, :221, :279, :284, :401, :470, :475 | Renderer::Primitives::* | renderer | |
| :14, :96, :334, :397, :474-486 | Renderer::MetricsCollector | renderer/metrics | |
| :10, :42, :327-331, :405-465, :489-495, :525-528 | Foundation::DebugServer | foundation/debug | control, input, screenshot and metrics plumbing (now in `engine/debug`, 2026-10-25/26) |
| :18, :287, :323, :328, :342, :350, :526 | foundation::Logger::initialize/shutdown/setDebugServer | foundation | the logger knows the debug server (`Log.h:45`) (now a sink, `Logger::setSink`, 2026-10-25/26) |
| :19, :235, :242, :259 | Foundation::findResourceString | foundation | |
| :21-22, :99-166, :285-286, :456 | GLEW and GLFW direct | platform | window hints 3.3 core (:137-140), glewInit (:153) |

The TileAtlasBuilder bootstrap (`:209-226`) and the asset-system init
(`:231-264`) moved out of AppLauncher ([worldsim#296](https://github.com/kcjonson/worldsim/pull/296)). The init is
`engine::assets::initializeAssetSystem` in
`libs/engine/assets/AssetBootstrap.{h,cpp}`, the tile atlas is built in
`apps/world-sim/GameBootstrap.{h,cpp}`, and both stay in worldsim.

### Application.{h,cpp}

- `Application.h:3-4` has `<GL/glew.h>` and `<GLFW/glfw3.h>` in the public
  header; `:16-18`, `:121`, `:146` forward-declare, own and return
  `UI::FocusManager` through `getFocusManager()`.
- `Application.cpp:3`, `:48-85` include ui's `focus/FocusManager.h`:
  constructs the FocusManager, sets the singleton, wires Tab and Shift-Tab
  plus key and char routing.
- `Application.cpp:8`, `:163-199` include ui's `input/InputEvent.h` and
  build `UI::InputEvent` per frame.
- `Application.cpp:9`, `:147` foundation `math/Types.h`.
- `Application.cpp:16-21`, `:102-106`, `:309-311` windows.h and timeapi.h,
  `timeBeginPeriod(1)`.
- `Application.cpp:26-28` re-declares GLFW_MOD constants; `:240-241`
  hardcodes `glClearColor(0.1, 0.1, 0.15)` plus `glClear` inside the loop.

### scene/

- `Scene.h:7-10`, `:80`, `:85` forward-declare `UI::InputEvent` and
  `UI::IComponent`; `handleInput(UI::InputEvent&)`, `getUiRoots()`.
- `Scene.h:64-68` `exportState()` is pure virtual "for /api/scene/state", a
  route that no longer exists. Every scene implements dead API. Deleted during
  extraction; settled.
- `Overlay.h:3-5`, `:26`, `SceneManager.h:129`, `SceneManager.cpp:3` carry
  the same `UI::InputEvent` edge.

### input/ and clipboard/

- `InputManager.h:4` has GLFW in the public header (`GLFWwindow*`, six
  `GLFW*fun` members at `:121-126`, static callbacks at `:109-114`); `:6`
  glm.
- `InputManager.h:103-106`, `:156-159` pan, zoom and edge-pan fields "for
  future use by scenes": camera config in the input layer, unused, and not
  something that should travel to app.
- `InputTypes.h` has zero includes. Clean.
- `ClipboardManager.h:3` GLFW in the public header.

### The reverse knots, ui -> engine

Listed in full in [phase-0-audit.md](phase-0-audit.md). In short: five ui
headers plus Button.cpp and TabBar.cpp include `input/InputTypes.h` (83 `engine::Key` and 15
`engine::MouseButton` in non-test ui files); `TextInput.cpp:460,470,478` call
`engine::ClipboardManager::Get()`; `Button.cpp:10` and `TextInput.cpp:10`
include `InputManager.h` and never use it. Plan implication: Key and
MouseButton (133 lines, no deps) must move below ui, and TextInput needs a
clipboard interface that app implements.

Since the survey: the two dead `InputManager.h` includes are gone ([worldsim#292](https://github.com/kcjonson/worldsim/pull/292)),
and TextInput reaches the clipboard through `UI::IClipboard` in
`libs/ui/clipboard`, which `engine::ClipboardManager` implements ([worldsim#294](https://github.com/kcjonson/worldsim/pull/294)).
The seven `InputTypes.h` includes were what was left of ui -> engine; decision D
(settled 2026-10-26) put the file in `foundation/input/`, done in
[worldsim#298](https://github.com/kcjonson/worldsim/pull/298) (namespace now `foundation::`), which also dropped `engine` from
ui's link list.

## Subsystem notes

### Launcher and Application API

AppLauncher (`application/AppLauncher.{h,cpp}`) is a static-only class over
file-static globals (`AppLauncher.cpp:36-97`: g_coordinateSystem,
g_fontRenderer, g_tileAtlasBuilder, g_windowResizeCallback, g_debugServer,
g_metrics, g_app).

`AppContext initialize(int argc, char* argv[], const AppConfig&)` returns
`{GLFWwindow* window, Application* app, CoordinateSystem*, DebugServer*, MetricsCollector*, bool hasSceneArg}`.
It parses `--scene=<name>`, `--http-port <n>` and `--help` (`:300-320`);
starts the logger and debug server (`:323-331`); creates the window (`:340`),
rendering systems (`:347`), asset system (`:358`) and Application (`:362`);
calls `config.initializeScenes()` (`:366-368`); resolves the initial scene
through remapSceneName, getKeyForName and getDefaultSceneKey (`:371-393`);
installs the default pre-frame callback (debug-server control plus injected
input, `:396-467`), the overlay renderer (`Primitives::endFrame`, `:470`) and
the post-frame callback (metrics, screenshot, scene name, `:473-496`).
`void run(AppContext&)` is `ctx.app->run()` and blocks.
`int shutdown(AppContext&)`. `int launch(...)` is all three. Also
setWindowResizeCallback, getMetrics(), getDebugServer().

AppConfig (`AppConfig.h`): windowTitle, windowSizePercent, enableDebugServer,
debugServerPort, enableMetrics, initializeScenes, getDefaultSceneKey,
remapSceneName, assetsRootPath, loadAssetsAsync. [worldsim#296](https://github.com/kcjonson/worldsim/pull/296) removed
`assetsRootPath` and `loadAssetsAsync` and added two hooks,
`initializeResources(bool hasSceneArg)` and `shutdownResources()`; the app now
brings the asset system up and down through them instead of the launcher
doing it.

Application (`application/Application.{h,cpp}`): the constructor takes
`GLFWwindow*`, constructs InputManager, ClipboardManager and
`UI::FocusManager`, and sets all three singletons (`:38-50`). Methods: run(),
stop(), pause(), resume(), isPaused(), setOverlayRenderer,
setPreFrameCallback (returns bool; false exits), setPostFrameCallback,
getFPS(), getDeltaTime(), getFocusManager(), getFrameTimings().

Scene registration: the app's initializeScenes callback builds an
`engine::SceneRegistry`
(`unordered_map<SceneKey, function<unique_ptr<IScene>()>>`) plus a SceneKey
-> string name map and calls `SceneManager::Get().initialize(registry, names)`
(`SceneManager.h:48`; the pattern is `apps/ui-sandbox/SceneTypes.cpp:15-33`).
SceneKey is `std::size_t`. `switchTo(key)` is immediate when no scene is
active, otherwise deferred to the top of the next update()
(`SceneManager.cpp:34-50`, `:56-64`, `:100-103`). One current scene, no
stack; overlays are a `std::vector<IOverlay*>` (non-owning) that get input
first in reverse order and render last. Exit is `requestExit()` polled by the
loop. Users: `apps/world-sim/Main.cpp` (8 scenes), `apps/ui-sandbox/Main.cpp`,
`apps/asset-manager/Main.cpp`.

GL context: hints at `AppLauncher.cpp:137-140` (3.3, CORE_PROFILE,
FORWARD_COMPAT), glfwCreateWindow `:142`, glewInit `:154`. A second production
site, `apps/asset-cli/Main.cpp:134-149`, creates its own hidden 3.3 core
context and bypasses AppLauncher. Five test sites create contexts too:
`libs/engine/assets/AssetRenderer.test.cpp:66-81`,
`libs/engine/world/rendering/SurfaceFieldGolden.test.cpp:155-170`,
`libs/renderer/resources/RenderToTexture.test.cpp:13-25`,
`libs/ui/font/FontRenderer.test.cpp:29-39`,
`libs/renderer/CoordinateSystem/CoordinateSystem.test.cpp:16-24`
(GLFW_NO_API).

### Scene interface

IScene (`scene/Scene.h`): pure `onEnter()`, `update(float dt)`, `render()`,
`onExit()`, `std::string exportState()`, `const char* getName() const`;
virtual with defaults `bool handleInput(UI::InputEvent&)` (false) and
`std::vector<const UI::IComponent*> getUiRoots() const` (empty); non-virtual
`setSceneManager(SceneManager*)` injected before onEnter. IOverlay
(`scene/Overlay.h`): `handleEvent(UI::InputEvent&)`, `update(float)`,
`render()`, `onWindowResize()`. `exportState` is the dead route above.

### Main loop

`Application::run()`, `Application.cpp:94-314`. Variable timestep, no
accumulator, no fixed step anywhere in `libs/`.
`deltaTime = glfwGetTime() - lastTime`, capped at 0.25 s (`:113-122`). Loop
condition at `:111`:
`glfwWindowShouldClose(window) == 0 && isRunning && !SceneManager::Get().isExitRequested()`.

Frame order: glfwPollEvents (`:127`) -> `inputManager->update(dt)` (`:135`)
-> build ui mouse events from polled state and push them through
`SceneManager::handleInput` (`:145-202`) -> pre-frame callback ->
`SceneManager::update(dt)`, skipped when paused (`:225`) -> glClear
(`:240-241`) -> `SceneManager::render()`, which runs even when paused
(`:245`) -> overlay renderer -> glfwSwapBuffers (`:267`) -> pacing to a hard
120 FPS cap, `kTargetFrameMs = 1000/120`, `sleep_for` then a 2 ms yield spin
(`:276-287`) -> deltaTime and fps recomputed after the sleep (`:290-294`) ->
post-frame callback. `glfwSwapInterval(1)` at `AppLauncher.cpp:151`, and
`/api/control?action=vsync` can turn it off (`:456`). Windows takes
`timeBeginPeriod(1)` for the whole run (`:105`, `:310`). Every scene call is
wrapped in try/catch and logged.

The body `:112-306` is already one frame's work. The split is mechanical:
`bool Application::tick()` with the `break` at `:211` becoming
`return false`, and `run()` becoming the native `while (tick())`. The
fixed-step accumulator and the interpolated render are net-new.

### InputManager

`input/InputManager.{h,cpp}`, a singleton through setInstance and Get()
(throws if unset). Polled API: getMousePosition, getMouseDelta,
isMouseButtonDown/Pressed/Released, isDragging, getDragStartPosition,
getDragDelta, isKeyDown/Pressed/Released, getScrollDelta, consumeScrollDelta,
isCursorInWindow, getWindowSize. Edge detection uses previous-frame state maps
(`:323-369`). Callbacks: `setKeyInputCallback(Key, int action, int mods) -> bool`,
`setCharInputCallback(char32_t)`, setMouseButtonInputCallback,
setMouseMoveInputCallback, setScrollInputCallback (nobody sets the three mouse
ones). Synthetic: injectKey, injectMousePosition, static keyFromName (tested).
Installs GLFW callbacks at `InputManager.cpp:256-261` and chains any
previously installed ones. Gamepad: none; zero hits for gamepad, joystick,
glfwGetGamepadState, glfwJoystickPresent, GLFW_GAMEPAD or XInput anywhere in
worldsim.

Delivery to ui is two paths. Mouse: `Application::run` builds a
`UI::InputEvent` from polled state and calls `SceneManager::handleInput`,
which goes to overlays and then `IScene::handleInput`, and the scene forwards
to its component tree. Keyboard: the GLFW key callback -> the
keyInputCallback Application set (`:53-80`) -> `FocusManager::focusNext`,
`focusPrevious` or `routeKeyInput`; the char callback ->
`FocusManager::routeCharInput` (`:82-85`). Keys reach ui through the
FocusManager singleton, never through the scene. `UI::InputEvent.modifiers`
carries raw GLFW mod bits. Game code polls `InputManager::Get()` in 8 places
under `apps/`. A second input type set lives in
`libs/ui/input/InputEvent.{h,cpp}` (86 lines) and includes engine's
InputTypes.h.

### Config system

There is none. AppConfig is a C++ struct filled with designated initializers in
each main(). No settings file, no JSON reader; engine's CMake linked
nlohmann though no engine source included it (dropped in [worldsim#295](https://github.com/kcjonson/worldsim/pull/295)). What
worldsim calls config is XML
under `assets/config/{actions,construction,work}/*.xml`, parsed with pugixml
by game registries in engine/assets. JSON in worldsim appears only in
`libs/ui/debug/{UiTreeSerializer,LayoutLint}`,
`libs/ui/font/FontRenderer.cpp` (the MSDF atlas) and two CLIs. A generic
config loader for libs/app is net-new.

### Dev server

Now in `worldsim/libs/engine/debug` ([worldsim#297](https://github.com/kcjonson/worldsim/pull/297), 2026-10-25/26):
`DebugServer.{h,cpp,test.cpp}` (297 + 1,064 + 67 lines) and
`LockFreeRingBuffer.h` (71), with `PngEncoder.{h,cpp}` (65) in
`libs/engine/graphics` and `PerformanceMetrics.h` (73) in
`libs/renderer/metrics`, because renderer's `MetricsCollector` returns it from
its public API. All of it was in `libs/foundation` when surveyed, and the line
numbers below are from there. Namespaces stayed `Foundation::` for now. Still
enabled through `AppConfig::enableDebugServer` or `--http-port`. Started by
`AppLauncher.cpp:323-331`. Binds 127.0.0.1 only (`DebugServer.cpp:1052`).

| route | DebugServer.cpp line | what |
|---|---|---|
| GET `/api/health` | 574 | |
| GET `/api/metrics` | 588 | |
| GET `/api/ui/screenshot` | 595 | glReadPixels on the game thread, PNG through stb |
| GET `/api/input` | 628 | `ev=click\|move\|down\|up\|scroll\|keydown\|keyup,...` CSV, queued; pointer and keys only |
| GET `/api/dev/<verb>` | 678 | generic DevCommand bag; the request parks up to 2 s |
| GET `/api/state?what=` | 725 | synchronous requestState handshake |
| GET `/api/ui/tree` | 753 | the state channel with `what="ui.tree"` |
| GET `/api/ui/lint` | 754 | same, with `what="ui.lint"` |
| GET `/api/control` | 760 | `action=exit\|scene&scene=\|pause\|resume\|reload\|camera&x,y,zoom,panx,pany\|vsync&value=` |
| GET `/stream/metrics` | 898 | SSE |
| GET `/stream/logs` | 944 | SSE |
| GET `/` | 1000 | HTML index |

Dependencies: cpp-httplib, one `std::thread` (`:260`) plus mutexes and
condvars, GLEW for glReadPixels, PngEncoder. It does not include ui:
`/api/ui/tree` only parks the HTTP thread; the drain runs on the main thread
in each app (`apps/ui-sandbox/Main.cpp:27-52`,
`apps/world-sim/scenes/game/GameScene.cpp:819-835`,
`apps/world-sim/scenes/shared/UiStateDrain.cpp`) and calls
`UI::serializeUiTreeJson` and `lintUiTreeJson` from `libs/ui/debug`.
AppLauncher does the generic halves: `/api/input` dispatch (`:46-95`),
`/api/control` (`:414-465`), screenshot, metrics and scene name (`:489-495`).
Game residue in the server: CameraCommand (`DebugServer.h:44-53`),
consumed only by GameScene. `foundation::Logger::setDebugServer` (`Log.h:45`)
coupled the logger to it; [worldsim#297](https://github.com/kcjonson/worldsim/pull/297) replaced it with
`setSink(LogSinkFn, void* context)`, an atomic function pointer plus an atomic
context.

Compile flag: none. Zero `#if` in DebugServer.cpp/h; it is always compiled
into foundation (into engine since [worldsim#297](https://github.com/kcjonson/worldsim/pull/297)) and enabled at runtime by `AppConfig.enableDebugServer`
(default false) or `--http-port`. All three GUI apps enable it (world-sim
8081, ui-sandbox 8090, asset-manager 8070). DEVELOPMENT_BUILD gates log levels
only. The hard rule in CLAUDE.md ("behind a compile flag") describes salal and
nothing in worldsim today.

The route set above is what worldsim ships; architecture.md's five
(`/api/ui/tree`, `/api/ui/lint`, `/api/ui/screenshot`, `/api/input`,
`/api/control`) are the contract every app must have.

### Asset loading

Path resolution goes through `Foundation::findResource` and
`findResourceString` (`worldsim/libs/foundation/utils/ResourcePath.cpp:48-83`:
the exe directory via GetModuleFileNameW or _NSGetExecutablePath, Linux
unimplemented at `:44`, then cwd, then `cwd/build/apps/ui-sandbox`). Reads are
direct filesystem: pugixml `load_file` (`AssetRegistry.cpp:419, :492, :585, :1359`;
`ActionTypeRegistry.cpp:21`; `AssetValidator.cpp:100`),
`std::filesystem::recursive_directory_iterator` (`AssetRegistry.cpp:1107`,
`AssetValidator.cpp:75`, `WorkTypeRegistry.cpp:71`), `std::ifstream` for Lua
(`LuaEngine.cpp:228`), `renderer::loadSVG(path)` (`AssetRegistry.cpp:1359`).
Renderer and ui read directly too: `renderer/shader/Shader.cpp:41-42`,
`ShaderLoader.cpp:13`, `ShaderPreprocessor.cpp:28`,
`ui/font/FontRenderer.cpp:72-73, :141, :276` (stbi_load),
`renderer/resources/TileAtlasBuilder.cpp:64`.

No byte-provider or VFS interface exists; ResourcePath is the only seam. Async
load is a `std::thread` (`AssetRegistry.cpp:1179`) polled by
`SplashScene.cpp:141-143`;
`--scene` already forces the synchronous path (`AppLauncher.cpp:357`).

### ECS core

Generic and header-only: `ecs/EntityID.h` (30), `ComponentPool.h` (139,
sparse set), `Registry.h` (158), `View.h` (96), `ISystem.h` (36), `World.h`
(220), `ECS.h` umbrella. 689 lines, namespace `ecs`, std-only includes.
Everything else under ecs/ is worldsim. 31 files in apps/world-sim include the
core headers. Engine-grade and optional, and it stays in worldsim (decision E,
settled 2026-10-26).

### PRNG

`worldsim/libs/foundation/random/Pcg32.h` (PCG32 `pcg_oneseq_64_xsh_rr_32`,
8-byte state, constexpr, seeded via SplitMix64, nextUInt32, nextFloat,
nextDouble, nextRange with Lemire's bounded method; the header says "THE rng
for world-generation code" and "std::mt19937 is banned in worldgen", and
promises identical output across architectures), `SplitMix64.h` (`splitMix64`
and `deriveSeed(worldSeed, streamId)` for independent sub-streams),
`HashNoise.h`, plus `foundation/math/DeterministicMath.h`. Seeded,
deterministic, cross-platform, with seed derivation. No `fork()` on the
generator and no named-stream forking like DDB's `fork('run', n)`; forking is
by hand as `Pcg32(deriveSeed(seed, id))`. `std::mt19937` is still used
outside worldgen: in engine `IAssetGenerator.h`, `PlacementExecutor`,
`ActionSystem.h`, `AIDecisionSystem.h`; in apps GameScene, PlacementSystem,
PartySelectScene, and ui-sandbox Grass and VectorPerf. What was missing was
the fork API and the discipline. Decision A (settled 2026-10-26) put
dual-deck-builder's `Rng`, with its named-stream `fork`, beside these in
salal's `foundation/random/Rng.h`; Pcg32 and SplitMix64 stay as they are for
worldgen.

### The sandbox harness

`worldsim/apps/ui-sandbox/`: CMakeLists.txt, Main.cpp,
NavigationMenu.{h,cpp}, SceneTypes.{h,cpp}, 28 scene .cpp files, 5 demo files
not in CMake; 8,284 lines. Main.cpp builds
`engine::AppConfig{.windowTitle="UI Sandbox", .windowSizePercent=0.8, .enableDebugServer=true, .debugServerPort=8090, .enableMetrics=true, .initializeScenes=ui_sandbox::initializeSceneManager}`
with default scene Svg, calls `AppLauncher::initialize`, pushes a
NavigationMenu overlay when there is no `--scene=`, sets an overlay renderer
that calls `Primitives::endFrame()` then answers pending ui.tree and ui.lint
requests from the current scene's `getUiRoots()`, then run and shutdown. 27
scenes through an X-macro in SceneTypes.h (Arena, Handle, Button, TabBar,
TextInput, Grass, VectorPerf, Svg, Clip, Layer, TextShapes, SdfMinimal,
InputTest, Tree, Layout, Scroll, Icon, TreeView_, Dropdown, Toast_, Dialog_,
Tooltip_, ContextMenu_, TextWrap, Planet, Salvage, SalvageDialog);
SliderScene.cpp is on disk but in neither list.

It links (`CMakeLists.txt:42-51`) engine, ui, renderer, foundation, assets,
planet-view and world. PlanetScene.cpp includes planet-view/* and
world/worldgen/*; GrassScene and TreeScene include assets/*. Two scenes poll
GLFW directly (`ClipScene.cpp:89-102`, `VectorPerfScene.cpp:52`). The harness
and the pure-UI scenes move; roughly a third of the scenes (Planet, Grass,
Tree, Salvage, SalvageDialog) stay in worldsim. Settled.

## Proposed libs/app slice

Core, moves once the knots are cut (15 files, 2,900 lines):

| file | lines |
|---|---|
| `application/AppConfig.h` | 51 |
| `application/AppLauncher.h` | 107 |
| `application/AppLauncher.cpp` | 557 |
| `application/Application.h` | 155 |
| `application/Application.cpp` | 359 |
| `scene/Scene.h` | 94 |
| `scene/Overlay.h` | 40 |
| `scene/SceneManager.h` | 159 |
| `scene/SceneManager.cpp` | 237 |
| `input/InputTypes.h` | 133 (moved to `foundation/input/`, worldsim#298; decision D) |
| `input/InputManager.h` | 182 |
| `input/InputManager.cpp` | 628 |
| `input/InputManager.test.cpp` | 89 |
| `clipboard/ClipboardManager.h` | 46 |
| `clipboard/ClipboardManager.cpp` | 63 |

`application/Placeholder.cpp` is dropped. `AssetBootstrap`
(`libs/engine/assets/AssetBootstrap.{h,cpp}`, [worldsim#296](https://github.com/kcjonson/worldsim/pull/296)) is worldsim's asset
system, so it stays in worldsim and is not part of this slice.

App-layer code parked in foundation and renderer today that AppLauncher owns.
The first six rows (2,069 lines) move with app, or at least behind app's
compile flag; the last two stay put and change shape:

| file | lines | note |
|---|---|---|
| `foundation/debug/DebugServer.{h,cpp,test.cpp}` | 1,428 | now `engine/debug` |
| `foundation/debug/LockFreeRingBuffer.h` | 71 | now `engine/debug` |
| `foundation/metrics/PerformanceMetrics.h` | 73 | now `renderer/metrics`, next to the `MetricsCollector` that returns it |
| `foundation/graphics/PngEncoder.{h,cpp}` | 65 | now `engine/graphics` |
| `renderer/metrics/MetricsCollector.{h,cpp}` | 319 | |
| `foundation/utils/ResourcePath.{h,cpp}` | 113 | the seed of the asset indirection |
| `foundation/utils/Log.{h,cpp}` | 301 | stays in foundation; `setDebugServer` was inverted into a sink callback, `setSink(LogSinkFn, void* context)`, with an atomic function pointer and atomic context |
| `renderer/resources/TileAtlasBuilder` | 181 | stays in renderer; its bootstrap in `AppLauncher.cpp:209-226` moved to `apps/world-sim/GameBootstrap.{h,cpp}` |

[worldsim#297](https://github.com/kcjonson/worldsim/pull/297) moved the debug, metrics and PNG files and turned the logger's
coupling into a sink; [worldsim#296](https://github.com/kcjonson/worldsim/pull/296) moved the atlas bootstrap (both 2026-10-25/26).
DebugServer, LockFreeRingBuffer and PngEncoder are now in engine, so phase 2
moves them from engine into app. PerformanceMetrics is in renderer because
`MetricsCollector` returns it from its public API.

Not in the slice: the ECS core, 7 headers, 689 lines, which stays in worldsim
(decision E).

Totals: about 2.9k core, 5.0k with the dev server, metrics and resource path.
One test file exists for the whole slice (InputManager.test.cpp);
Application, SceneManager and AppLauncher have none.

## Net-new for salal

None of this exists in worldsim in any form:

- the fixed-step accumulator loop with interpolated render (architecture.md's
  frame contract)
- gamepad input, hot-plug and device-to-player-slot claiming
- a config file loader
- a byte-provider seam between asset lookup and file reads, only if a consumer
  needs one; today worldsim's `findResourceString` path search is enough
- a compile flag for the dev server, with game-registrable endpoints and pad
  and axis injection
- a named-stream PRNG fork API (decision A; landed ahead of phase 2 as
  `foundation/random/Rng.h`)
- a scene stack with typed payloads and return-to-caller
  ([consumer-requirements.md](consumer-requirements.md) has what asteroids
  needs)

## Frame-contract flags

Eleven things in the current code that the phase-2 design has to answer, in
the order they were found.

1. `Application::run()` owns the loop and blocks (`Application.cpp:111-307`);
   `AppLauncher::run` is just `ctx.app->run()`. The body `:112-306` is one
   frame's work, so the split is mechanical: `bool Application::tick()` with
   the break at `:211` becoming `return false`, and `run()` becoming the
   native `while (tick())`.
2. Frame pacing sleeps and spins on the main thread (`:276-287`) and takes
   `timeBeginPeriod` for the whole run (`:105`, `:310`). Native-only; vsync is
   on by default (`AppLauncher.cpp:151`) but `/api/control?action=vsync` can
   disable it (`:456`).
3. DebugServer runs its own `std::thread` (`DebugServer.cpp:260`) and parks
   HTTP threads on the game thread. No compile flag; foundation linked
   httplib and OpenGL for it until [worldsim#297](https://github.com/kcjonson/worldsim/pull/297) moved the server into engine.
4. `AssetRegistry::beginLoadAsync` is a `std::thread`
   (`AssetRegistry.cpp:1179`); the game side also uses `std::async`
   (`ChunkManager.cpp:108`, `NavigationSystem.cpp:501`,
   `AsyncChunkProcessor.h:115`).
5. GLFW types in public headers: `Application.h:3-4`, `InputManager.h:4`
   (members `:121-129`), `ClipboardManager.h:3`. The platform leaks into the
   API.
6. Static-lifetime singletons everywhere: `SceneManager::Get()`
   function-static; InputManager, ClipboardManager and FocusManager through
   setInstance; `AssetRegistry::Get()`; a function-static CoordinateSystem
   (`AppLauncher.cpp:171`); the g_* globals. Re-init and headless tests are
   awkward.
7. `glfwGetTime` is the clock (`Application.cpp:109, :113, :279, :285, :290`).
   A platform-called tick wants a Clock seam.
8. Three exit flags OR'd at `Application.cpp:111`; a `tick()` that returns
   `false` collapses them into one.
9. argv parsing and printf help (`AppLauncher.cpp:300-320`): a headless test
   constructs the app without argv, so defaults must stand alone.
10. The hardcoded clear colour and glClear live in the loop
    (`Application.cpp:240-241`); they belong to the scene or the renderer.
11. `IScene::exportState()` (`Scene.h:64-68`) is a pure-virtual contract for
    a route that no longer exists. Deleted during extraction rather than
    carried.
