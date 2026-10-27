# App API

The design of salal's `app` library for phase 2: the frame, the clock, device
input and the per-tick snapshot, the scene stack, settings, resource lookup,
the clipboard and the dev server, and the PRs that get there. Written
2026-10-27 from worldsim main @ `db604e68` (pinning salal `v0.8.1`), checked
again at `478b1baf` (pinning `v0.9.0`; the only change to the app slice between
them is a new 62-line `application/FontAtlases.test.cpp`), dual-deck-builder @
`afcd7b16` and asteroids-roguelike @ `176064c`.
[app-carve-out.md](app-carve-out.md) is the map of worldsim's code this starts
from, and [consumer-requirements.md](consumer-requirements.md) has what the
three games ask for; this document cites them rather than repeating them.
Reviewed and decided 2026-10-27; the decisions are listed at the end. All three
games are pre-release, which is why small, listed behaviour changes in worldsim
are acceptable.

## Goal and non-goals

`app` owns the window, the frame, device input, scenes, the settings file and
the dev server, behind headers that name no GLFW or GL type, and a frame that
runs headless in a test. Worldsim converts in one PR at the end and behaves
identically wherever a player or its tooling can see, with the exceptions
listed under "What worldsim keeps" and owned there one by one.

Not in phase 2:

- `ui`. It is extracted in phase 3, so in phase 2 `app` names no ui type. The
  focus manager, `UI::InputEvent`, `UI::IComponent`, the tree serialiser and
  the lint stay worldsim's, and worldsim keeps a bridge between them and `app`
  until phase 3.
- Audio (phase 2b, decision B). What a save, profile or leaderboard contains,
  which is game data (the mechanics of storing one are `app`'s, under "Game
  state and saves"). Particles. Screen transitions
  (dual-deck-builder's fade with input blocked, `ScreenManager.ts:144-173`),
  which a game draws as an overlay over the stack. A fixed-aspect virtual
  resolution with letterboxing, which is the game's over `onResize` and the
  renderer's viewport and render targets until a second game wants the same
  one.
- WebAssembly, dropped 2026-10-26. The board's tick-split item still asks for
  an `emscripten_set_main_loop` path and its byte-provider item for MEMFS;
  neither survives (see "Resources"). Asteroids' `tech-stack.md` (`:141-144`,
  `:202-206`, `:245-264`) still gives WebAssembly as the reason for both seams;
  the seams stand on their other reasons, and that file is the asteroids
  repo's to fix.

## What exists

| where | what | lines |
|---|---|---|
| worldsim `libs/engine/application` | `AppConfig` (12 fields), `AppLauncher` (static, over file-static globals), `Application` (owns the loop) | 1,221 (1,283 with the font test) |
| worldsim `libs/engine/scene` | `IScene`, `IOverlay`, `SceneManager` (function-static singleton, one current scene, deferred switch, non-owning overlays) | 530 |
| worldsim `libs/engine/input` | `InputManager` (singleton, six GLFW callbacks, per-frame edge maps) | 903 |
| worldsim `libs/engine/clipboard` | `ClipboardManager`, implementing `UI::IClipboard` over GLFW | 62 |
| worldsim `libs/engine/debug`, `graphics/PngEncoder` | `DebugServer`, twelve routes, `LockFreeRingBuffer`, PNG encoding | 1,505 |
| salal `libs/app`, `apps/sandbox` | placeholders: `app::layerDepth()`, a `main` that returns it | |
| salal `libs/foundation/input/InputTypes.h` | `Key` and `MouseButton` only; no snapshot, accumulator, `ActionMap` or gamepad types yet | 124 |

What changed since app-carve-out.md's survey, and matters here:

- `glewInit` is in `GLBackend::create()` (`salal/libs/renderer/backend/gl/GLBackend.cpp:124-132`),
  which `AppLauncher.cpp:184` calls. AppLauncher still includes GLEW for
  `glViewport` (:111) and two `glGetString` calls (:160-161) that run before
  GLEW is initialised.
- The screenshot route reads through `Primitives::readBackbuffer`
  (`DebugServer.cpp:307`, `:321`), with no GL of its own. It runs in the
  post-frame callback, after `glfwSwapBuffers` and pacing.
- `IScene::exportState()` is not dead. 40 registered scenes implement it, and
  world-sim answers `GET /api/state?what=scene` with it for six pre-game
  scenes (`apps/world-sim/scenes/shared/UiStateDrain.cpp:24-26`, since
  `e45d7247`). The route the comment names, `/api/scene/state`, never existed;
  the one that uses it does. It stays, optional (decision 1).
- No dev-server compile flag exists in either repo. A Release build serves the
  server; only the log sink and debug log levels drop out (`DEVELOPMENT_BUILD`).

## The shape

```
main()                                  the game's
  parseCommandLine(argc, argv)          --scene, --http-port, --help; separate so a test passes no argv
  createGlfwPlatform(window)            or createHeadlessPlatform(...) in a test
  App::create(config, platform)         nullptr on failure, logged; never exits the process
    Platform     window, GL context, event callbacks, gamepads, clipboard, present
    Clock        seconds, monotonic; the platform's, or a ManualClock
    Input        events per frame, InputSnapshot per tick and per frame, devices, player slots
    SceneStack   typed push, pop, replace; overlays above it
    context      one game-defined object every scene can reach
    Settings     the user settings file
    Saves        slots, safe writes, versioned upgrades
    DevServer    development builds only
  the game's own resources, its scenes, the first scene
  app->run()                            native: while (tick()) pace();
  app->shutdown()                       scenes and overlays exit; GL still alive for the game's teardown
  the game's teardown, then ~App        server, fonts, Primitives, window
```

The game's `main` owns the order, so worldsim's `initializeResources(hasSceneArg)`
and `shutdownResources` hooks become plain code there, `hasSceneArg` is
`CommandLine::scene.has_value()`, and its `"game"` to `"gameloading"` remap
(`apps/world-sim/Main.cpp:20-27`) runs before it names the first scene.

No singletons in `app`. Scenes reach everything through the `App&` they are
given; worldsim's `SceneManager::Get()`, `InputManager::Get()` and
`IClipboard::setInstance` go (their call sites are under "What worldsim
keeps"). `Primitives` stays module-static, the renderer's business, as does
the logger. Resource roots are process-global too, in foundation, since the
renderer's `Shader::LoadFromFile` resolves through the same free function.

```cpp
namespace app {

class Clock { // the seam the frame reads time through; nothing in app calls glfwGetTime or a std::chrono clock directly
public:
	virtual ~Clock() = default;
	virtual double now() const = 0; // seconds, monotonic
};
class ManualClock final : public Clock { public: double now() const override; void advance(double seconds); };

class Platform; // window, context, callbacks into Input, gamepad polling, clipboard, present, vsync, focus; no GLFW type in the header
std::unique_ptr<Platform> createGlfwPlatform(const WindowConfig&); // GLBackend::create() inside
class HeadlessPlatform final : public Platform { // NullBackend, no window
public:
	explicit HeadlessPlatform(int width, int height);
	ManualClock& clock();
	void push(const foundation::InputEvent&); // arrives in the next tick's poll, as a callback would
	void resize(int width, int height);
	void setFocused(bool);
};

struct WindowConfig {
	std::string title;
	float sizePercent = 0.8F;          // of the primary monitor's work area
	std::optional<int> fullscreenMonitor;
	bool vsync = true;
};

struct FrameConfig {
	double fixedStep = 1.0 / 60.0; // seconds per fixed tick; asteroids sets 1.0 / 120.0
	double maxFrameDelta = 0.25;   // a longer frame counts as this long (worldsim's clamp today)
	int maxTicksPerFrame = 8;      // past this the remainder is dropped: the sim runs slower than wall time instead of spiralling
	int frameCapHz = 0;            // 0 is vsync alone; worldsim sets 120 (its sleep-then-spin pacing, Windows timeBeginPeriod)
	bool freezeTicksOnFocusLoss = true;
};

struct AppConfig {
	std::string organization, applicationName; // the per-user settings directory
	FrameConfig frame;
	Renderer::backend::ShaderSource baseShader;  // the game's base shader with its hooks (salal_base_shader)
	std::span<const Renderer::FontAtlasSource> fontAtlases; // empty: no text, logged once, not a failure
	Renderer::FontFamily defaultFont = Renderer::FontFamily::Default;
	Foundation::Color clearColor{0.1F, 0.1F, 0.15F, 1.0F}; // through Primitives::clear; the glClear in worldsim's loop goes
	std::vector<std::filesystem::path> resourceRoots;      // searched before the exe directory and cwd
	std::filesystem::path settingsFile;                     // under the per-user directory; empty for none
	DevServerConfig devServer;                              // enabled, port; a no-op where the server isn't compiled in
	bool metrics = false;
};

struct CommandLine { std::optional<std::string> scene; std::optional<int> httpPort; bool help = false; bool valid = true; };
CommandLine parseCommandLine(int argc, char** argv); // --http-port enables the server, as today

class App {
public:
	static std::unique_ptr<App> create(AppConfig, std::unique_ptr<Platform>); // also nullptr when a requested dev-server port is busy
	bool tick();              // one frame (below); false once exit is requested or the window closes
	void run();               // the native runner: while (tick()) pace();
	void shutdown();          // scenes top-down, then overlays, onExit; GL and the server stay up until ~App
	void requestExit();
	void pause();             // fixed ticks and update stop for scenes and overlays; render continues
	void resume();
	bool paused() const;
	void step(int ticks);     // while paused: exactly `ticks` fixed ticks of the top scene, then one render
	void setDispatch(std::function<void(App&)>); // replaces step 5's default event dispatch (worldsim's bridge; salal's ui in phase 3)
	void setVsync(bool);
	void setFullscreen(std::optional<int> monitor);
	SceneStack& scenes();
	template <typename T> void setContext(T&); // the game's state, set once before the first scene
	template <typename T> T& context();        // checked against the type given to setContext
	Input& input();
	Settings& settings();
	SaveStore& saves();
	Clipboard& clipboard();
	DevServer& devServer();   // always there; its calls do nothing when the server is compiled out or off
	const Clock& clock() const;
	const Renderer::Viewport& viewport() const;
	Renderer::CoordinateSystem& coordinateSystem();
	Renderer::MetricsCollector* metrics();
	void addOverlay(std::unique_ptr<Overlay>); // persistent across scene changes, above the stack: dev menus, debug HUDs
};

}
```

## The frame

`tick()` is one frame, in this order:

1. Read the clock once. `realDelta = min(now - last, maxFrameDelta)`.
   Metrics close the previous frame and open this one, so a frame's time
   includes its present and pacing, as it does today.
2. `Platform::pollEvents()`. Callbacks push `InputEvent`s into the input
   accumulator in arrival order; gamepads are polled once here and diffed into
   events (GLFW has no gamepad callbacks). A framebuffer resize updates the
   coordinate system and `Primitives`' viewport inside the callback, as today,
   and once the poll returns overlays then every scene in the stack get
   `onResize`.
3. Dev server, pre-frame: control actions, then registered routes. Injected
   input joins the accumulator here, unless paused, when it stays queued in the
   server until resume, as today (`AppLauncher.cpp:384`). An exit request from
   the server ends the frame here and `tick()` returns false, as today's
   mid-frame break does (`Application.cpp:208-212`).
4. The frame reader is consumed: `input().frame()` and `input().events()` now
   hold this frame.
5. Dispatch. By default each event goes to the overlays top-down, then the top
   scene, each `handleEvent` returning whether it consumed it; while paused,
   only overlays receive them. A game can replace this step with
   `setDispatch`, which worldsim does (under "What worldsim keeps").
6. Scene-stack changes requested so far apply here, after dispatch and before
   any update, which is where worldsim's deferred switch applies today
   (`SceneManager.cpp:100-102`, after `Application.cpp:145-202`). While paused
   they wait for resume, as today.
7. Fixed ticks, unless paused or (with `freezeTicksOnFocusLoss`) unfocused:
   `accumulator += realDelta`; while `accumulator >= fixedStep` and fewer than
   `maxTicksPerFrame` have run, the top scene's `fixedUpdate(TickContext)` with
   a snapshot from the tick reader, and `accumulator -= fixedStep`. The first
   tick of a frame sees every transition since the last tick ran, including a
   frame that had none; later ticks in the same frame see only held state. On
   a paused or frozen frame the tick reader is consumed and discarded, so a
   press made during a pause doesn't fire on resume.
8. `update(FrameContext)` on overlays and the top scene with `realDelta`,
   unless paused.
9. `Primitives::beginFrame`, `clear(clearColor)`, render the stack from the
   topmost opaque scene upward, then overlays, then `Primitives::endFrame`.
   The top scene renders with `alpha = accumulator / fixedStep`; a covered
   scene renders with the alpha it had when it was covered, so the arena under
   a pause menu holds still. `beginFrame` moves here from before update
   (`AppLauncher.cpp:380`); nothing in worldsim draws from `update`.
10. Dev server, post-render: screenshot readback, then tree, lint and state
    providers. Before present (decision 9).
11. `Platform::present()`.

A scene's `requestExit` (world-sim's Quit) finishes the frame, and `tick()`
returns false; `glfwWindowShouldClose` the same.

`run()` adds pacing after `tick()`: vsync through the platform, plus the
optional `frameCapHz` sleep-then-spin worldsim has today. Pacing is not in
`tick()`, so a test drives `tick()` with the headless platform's `ManualClock`
and never sleeps. Metrics map today's five sections onto the steps: poll (2),
input (3 to 6), update (7 and 8), render (9 and 10), present (11, plus
pacing).

A fixed tick reads no clock: `TickContext{tick, step, input}` carries the
scene's own tick count since its `onEnter`, from 0, and the step, which is all
a deterministic sim may use, and a replay starts from that count. Variable
update keeps `dt`, for animation and for worldsim, whose scenes are all
variable-step and stay so until it chooses otherwise. A scene that overrides
only `update` and `render` behaves exactly as today; the accumulator still runs
and costs nothing.

Pause stops fixed ticks and update, for scenes and overlays, as worldsim's
pause skips `SceneManager::update` (and with it overlay update) today; render
keeps going. Focus loss freezes only the fixed accumulator, so a backgrounded
window doesn't replay ten seconds of ticks, and worldsim, which ticks nothing,
is unaffected. `step(n)` runs exactly `n` fixed steps whatever the wall clock
did, which is what a headless "run a seeded sortie N ticks" test and the dev
server's step both need; dual-deck-builder's `stepFrame` steps by wall-clock
time and can't do this.

This is the shape architecture.md's frame contract describes as `bool tick()`
with `run()` as the native `while (tick())`; its code block still shows
`tick(realDeltaSeconds)` and a separate `render(interpolationAlpha)` and is
amended in this change to match. The platform still owns the frame: a native
`main` calls `tick()` in a loop, a test calls it with a clock it advances.

## Input

Decision D put the types in `foundation/input/` and the device layer in `app`.
Phase 2 builds both.

```cpp
namespace foundation {
enum class Key : std::uint16_t { Unknown, /* GLFW's full token set, dense */ Count }; // today's enum lacks ; = [ ] \ ` the lock keys, PrintScreen, Pause, Menu and F13-F25
enum class MouseButton : std::uint8_t { /* as today */ Count };
enum class GamepadButton : std::uint8_t { A, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide,
	LeftThumb, RightThumb, DpadUp, DpadRight, DpadDown, DpadLeft, Count }; // GLFW's gamepad layout
enum class GamepadAxis : std::uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };
enum class DeviceKind : std::uint8_t { Keyboard, Mouse, Gamepad };
struct DeviceId { DeviceKind kind = DeviceKind::Keyboard; std::uint8_t index = 0; bool operator==(const DeviceId&) const = default; };
inline constexpr int kMaxRealGamepads = 16;    // GLFW joystick ids 0 to 15, pads[0..15]
inline constexpr int kMaxVirtualGamepads = 4;  // the dev server's vpad0 to vpad3, pads[16..19]
enum Mod : std::uint8_t { ModShift = 1, ModControl = 2, ModAlt = 4, ModSuper = 8 }; // GLFW's bit values

struct InputEvent {   // one per platform callback, in order; positions in GLFW window coordinates, which are logical pixels already
	enum class Type : std::uint8_t { KeyDown, KeyUp, Char, PointerMove, PointerDown, PointerUp, Scroll,
		PointerEnter, PointerLeave, ButtonDown, ButtonUp, Axis, DeviceConnected, DeviceDisconnected, FocusLost, FocusGained };
	Type type = Type::KeyDown; DeviceId device{}; double time = 0.0; // the app clock when pushed
	std::uint8_t mods = 0; bool repeat = false; Key key = Key::Unknown; char32_t codepoint = 0;
	MouseButton mouseButton = MouseButton::Left; GamepadButton button = GamepadButton::A; GamepadAxis axis = GamepadAxis::LeftX;
	float value = 0.0F; glm::vec2 position{}; glm::vec2 scroll{};
};

struct ButtonState {  // Handmade Hero's halfTransitionCount
	bool down = false; std::uint8_t transitions = 0;
	bool pressed() const;  // went down at least once since the last read
	bool released() const;
};
struct GamepadState { bool connected = false; std::array<ButtonState, std::size_t(GamepadButton::Count)> buttons{}; std::array<float, std::size_t(GamepadAxis::Count)> axes{}; };
struct InputSnapshot { // per device, about 1 KB
	std::array<ButtonState, std::size_t(Key::Count)> keys{};
	std::array<ButtonState, std::size_t(MouseButton::Count)> mouse{};
	glm::vec2 pointer{}, pointerDelta{}, scroll{};
	bool pointerInWindow = false;
	std::uint8_t mods = 0;
	std::array<GamepadState, kMaxRealGamepads + kMaxVirtualGamepads> pads{};
};

class InputAccumulator { // folds events into state; two readers, each with its own transition counts
public:
	void push(const InputEvent&);
	InputSnapshot consumeTick();   // what a fixed tick reads
	InputSnapshot consumeFrame();  // what dispatch, update and the ui read
	std::span<const InputEvent> frameEvents() const;
};

// Actions. A binding names a device kind and a control; a map holds bindings per action and is evaluated against a snapshot
// for one set of devices, so split keyboard is two maps over the keyboard and co-op is one map per claimed pad.
struct Binding {
	enum class Source : std::uint8_t { Key, MouseButton, GamepadButton, GamepadAxis };
	Source source; std::uint16_t control;  // the enum value
	float scale = 1.0F;                    // -1 makes a key the negative half of an axis (two keys to one axis)
	float threshold = 0.5F;                // an axis read as a button: |value| past this is down
	float deadzone = 0.0F;                 // per axis; sticks take a radial deadzone over both axes in the map
};
struct ActionState { ButtonState button; float value = 0.0F; }; // button transitions follow the reader the snapshot came from
class ActionMap {
public:
	void bind(std::string_view action, Binding);
	void setStickDeadzone(GamepadAxis x, GamepadAxis y, float radius);
	ActionState evaluate(std::string_view action, const InputSnapshot&, std::span<const DeviceId>) const;
};
}

namespace app {
struct DeviceInfo { foundation::DeviceId id; std::string name; std::string modelGuid; }; // GLFW's GUID names a model, not a unit
class Input {
public:
	const foundation::InputSnapshot& frame() const;           // this frame's state; replaces InputManager's polled API
	std::span<const foundation::InputEvent> events() const;   // this frame's events, in order
	std::span<const DeviceInfo> devices() const;              // connected now
	std::optional<foundation::DeviceId> pressedDevice() const; // the first device with a press this frame: "press a button to join"
	void claim(int slot, foundation::DeviceId);               // a keyboard can back two slots
	void release(int slot);
	std::optional<foundation::DeviceId> deviceForSlot(int slot) const;
	bool slotConnected(int slot) const;
	std::string keyName(foundation::Key) const;               // glfwGetKeyName for printable keys, a fixed English table for the rest
	std::optional<Binding> captureNext();                     // the first press of any control this frame, for a rebinding screen
};
}
```

The rules this carries:

- Transition counts, not per-frame "just pressed" flags, with separate tick
  and frame readers. At 120 Hz ticks and 60 Hz frames a press and release
  inside one tick is seen by that tick, a frame with two ticks doesn't report
  one press twice, and at 60 Hz ticks and 144 Hz frames a press in a frame
  with no tick waits for the next one. Worldsim's maps collapse a press and
  release inside one poll to a release (`handleKeyInput`,
  `InputManager.cpp:573-582`; `handleMouseButton`, `:590-612`); the readers
  don't.
- Gamepads come from `glfwGetGamepadState`, polled once per frame and diffed,
  with `glfwSetJoystickCallback` for hot-plug. GLFW's built-in SDL mapping
  database is used as is. Pads are sampled at the frame rate, since ticks
  within a frame run back to back with nothing between them to sample.
- Slots live in `app::Input`, not in the snapshot, which is per device;
  decision D's text says the snapshot carries the slot and is amended in this
  change. A claimed pad that disconnects keeps its slot, `slotConnected` goes
  false, and the game decides whether to pause. A pad that connects while a
  slot is waiting on the same model GUID takes that slot; anything else
  arrives unclaimed.
- Keys are positions, as decision D settled; `keyName` shows the layout's
  label, with a fallback because GLFW names only printable keys.
- A sim never reads `InputSnapshot`. It takes the game's own action struct,
  built from `ActionMap` in the game's `fixedUpdate` with `deviceForSlot`, and
  that struct is the replay log.
- Headless: the accumulator and `ActionMap` are pure, so tests feed events and
  read snapshots with no window; `HeadlessPlatform::push` and the dev server's
  `/api/input` go through the same accumulator.

## Scenes

```cpp
namespace app {
struct TickContext { std::uint64_t tick; double step; const foundation::InputSnapshot& input; };
struct FrameContext { double dt; const foundation::InputSnapshot& input; };

class Scene {
public:
	virtual ~Scene() = default;
	virtual const char* name() const = 0;
	virtual void onEnter(App&) {}
	virtual void onExit() {}
	virtual void onCovered() {}        // another scene was pushed over this one
	virtual void onUncovered() {}      // on top again; a result, if any, has already been delivered
	virtual bool handleEvent(const foundation::InputEvent&) { return false; }
	virtual void fixedUpdate(const TickContext&) {}
	virtual void update(const FrameContext&) {}
	virtual void render(float alpha) = 0;
	virtual void onResize(const Renderer::Viewport&) {}
	virtual bool opaque() const { return true; } // false: the scenes below keep rendering (a pause menu over the arena)
	virtual std::string exportState() const { return {}; } // JSON for /api/state?what=scene; only the dev server calls it
};

struct NoArgs {}; struct NoResult {};
template <typename A = NoArgs, typename R = NoResult>
class TypedScene : public Scene {
public:
	using Args = A; using Result = R; // a scene with Args takes them in its constructor; a NoArgs scene is built as S()
protected:
	void finish(R result);            // pop this scene and hand `result` to whoever pushed it; checked at compile time
};

class SceneStack {
public:
	void add(std::string name, std::function<std::unique_ptr<Scene>()> factory); // by name: what worldsim registers (its scene classes live in .cpp files)
	template <typename S> void add(std::string name);                         // the same, for a complete type whose Args default
	template <typename S> void push(typename S::Args = {});
	template <typename S, typename F> void push(typename S::Args, F onResult); // F(typename S::Result), run on the caller when S finishes
	template <typename S> void replace(typename S::Args = {});
	template <typename S> void reset(typename S::Args = {});      // clear the stack, then push
	void pop();                                                    // no result; the caller's callback doesn't run
	bool switchTo(std::string_view name);                          // reset to a named scene
	bool reload();                                                 // rebuild the top scene by its name; false for an unnamed one
	Scene* top();
	std::string_view topName() const;
};

class Overlay { /* handleEvent, update(FrameContext), render(), onResize: today's IOverlay, owned by the App */ };
}
```

- Changes apply at step 6, after dispatch and before update, so a click that
  switches scene switches in the same frame, as today. A `replace`, `reset` or
  `switchTo` discards the changes queued before it in that frame, so two
  switches in one frame build and enter only the last scene, as today
  (`SceneManager.cpp:48`); pushes and pops queue in order.
- Only the top scene receives events, fixed ticks and update. Covered scenes
  render, from the topmost opaque one up.
- Arguments and results are typed per scene and checked at compile time:
  `finish(R)` is how a scene returns, and the caller's callback runs before
  its `onUncovered`. The stack type-erases them itself, so `Args`, `Result`
  and the callback may be move-only (a `std::unique_ptr` in an `Args`), which
  `std::function` and `std::any` couldn't hold.
- Asteroids' Arena args (`{mode, players[], arenaSeed, spawnTable, pressureCurve, routeEffects?}`)
  and Player setup args become `Args` structs. Dual-deck-builder's four
  continuation shapes (`next`, `returnTo`/`returnData`, `onChoose`/`onDone`,
  `prepare`/`back`/`onEnded`) map onto push with a result or replace; its
  untyped `data?: unknown` is what this replaces. Its screens that enter cold
  by loading the save when no campaign is handed in keep doing so with an
  optional field in their `Args`.
- State that outlives a scene (asteroids' ore and purchases across the wave
  shop round trip, dual-deck-builder's live `Campaign`, world-sim's
  `NewGameSetup`) lives in the game's context object (under "Game state and
  saves"); `Args` carry what one navigation hands over.
- Names are for the command line, the dev server and reload: a scene added by
  name has a factory with no arguments. A scene that needs arguments isn't
  reachable by name, and `reload` refuses it.
- `exportState` stays, optional. Scenes are the engine's and so is the dev
  server, so every game gets scene state for debugging with no setup: the
  server answers `/api/state?what=scene` with each stacked scene's name and
  `exportState()`, top first. Nothing calls it in a build without the server;
  a game that wants the overrides out of the binary too wraps them in
  `#if SALAL_DEV_SERVER`.
- Scenes get `onResize`; today only overlays are told (`SceneManager.cpp:231-235`).
- Every scene call is wrapped and logged as today, including `handleEvent` and
  the first `onEnter`, which aren't today (`Application.cpp:164-195`,
  `AppLauncher.cpp:350-372`).

## Settings

The board's "config file loader" is read here as the user settings file,
because that is the only config any consumer has: asteroids' Settings screen
(audio, video, input binding, accessibility; `screens.md:17, 89-92`),
dual-deck-builder's `dual-deckbuilder.settings` key, and world-sim's
`UserSettings` singleton. Neither game reads a startup config file, flag or
environment variable today, and `AppConfig` stays a struct in code.

```cpp
namespace app {
class Settings {
public:
	using Migrate = std::function<void(nlohmann::json&, int fromVersion)>;
	static Settings load(const std::filesystem::path&, int version, Migrate = {}); // missing: empty; unreadable: logged, moved aside, empty
	template <typename T> T get(std::string_view path, T fallback) const;           // "video.vsync"
	template <typename T> void set(std::string_view path, T value);
	nlohmann::json& section(std::string_view name);                                  // a game-owned subtree
	bool save();                                                                     // temp file, then rename; unknown keys kept
};
std::filesystem::path userConfigDir(std::string_view organization, std::string_view application);
}
```

- Location: `%APPDATA%\<org>\<app>` on Windows, `~/Library/Application Support/<org>/<app>`
  on macOS, `$XDG_CONFIG_HOME/<org>/<app>` (else `~/.config/...`) on Linux.
- Unknown keys survive a save, which is how dual-deck-builder keeps older and
  newer builds compatible; a `version` field plus the game's `Migrate` covers
  the rest. A file newer than the build loads as it is, isn't migrated down,
  and keeps its version on save.
- `app` reads its own keys (`video.vsync`, `video.fullscreenMonitor`, the
  window size) at create and applies changes through `setVsync` and
  `setFullscreen`; `ActionMap` bindings round-trip through a section.
  Everything else is the game's.
- `nlohmann::json` is in `app`'s public API, so `app` links it PUBLIC where
  the renderer keeps it private.

## Game state and saves

State that outlives a scene is the game's, in one object the game defines and
hands to the App once (`setContext`), which any scene reaches with
`app.context<T>()`. It replaces the singletons and statics all three games use
for this today (world-sim's `GameStartConfig`, `NewGameSetup` and
`UserSettings`; dual-deck-builder's `GameSettings.shared` and
`CampaignStore.shared`), and a test builds a fresh one. `app` never looks
inside it.

Saving writes some of that state to disk. What a save contains is the game's;
storing one safely is the same for every game, so `app` does that, sharing the
settings file's safe write and versioning:

```cpp
namespace app {
struct SaveFormat {
	int version;                                                     // the game's format version, not salal's
	std::vector<std::function<bool(std::vector<std::byte>&)>> upgrades; // upgrades[i] takes version i+1 to i+2; the game writes each step
};
class SaveStore {
public:
	enum class LoadError { Missing, Corrupt, NewerThanBuild, UpgradeFailed };
	bool write(std::string_view slot, std::span<const std::byte>);                   // temp file, then rename; the previous save kept as the backup
	std::expected<std::vector<std::byte>, LoadError> read(std::string_view slot);     // runs every upgrade needed, in order, before returning
	bool writeJson(std::string_view slot, const nlohmann::json&);                    // the same, for games that save JSON
	std::expected<nlohmann::json, LoadError> readJson(std::string_view slot);
	std::vector<SlotInfo> slots() const;                                              // name, version, time written
	bool restoreBackup(std::string_view slot);
	bool remove(std::string_view slot);
};
std::filesystem::path userDataDir(std::string_view organization, std::string_view application); // beside, not inside, the settings directory
}
```

- Each save is an envelope: the game's format version, a write counter, and
  the game's bytes. Bytes rather than JSON, so a game with large binary data
  isn't forced through JSON; the JSON pair covers the rest.
- On load the store runs the game's upgrade steps from the save's version to
  the build's, in order, once, and keeps the pre-upgrade file as the backup
  until the upgraded save is written. A save newer than the build isn't loaded
  and isn't touched.
- Dual-deck-builder built all of this by hand (A/B slots, a recovery copy, a
  `{version, sequence, campaign}` envelope, upgrades from 8 to 12); asteroids
  asks for Continue, profiles, leaderboards and "save migrations". The
  deterministic games save seeds and fork names and counters, never a PRNG's
  state mid-stream, which is content and so theirs.

## Resources

No byte-provider in phase 2. WebAssembly was its reason; worldsim's
`findResource` path search is enough for three native games; and nobody packs
assets. What phase 2 fixes instead, in `foundation/utils/ResourcePath`:

- Linux has no executable directory (`ResourcePath.cpp:43-45`, a TODO); it
  reads `/proc/self/exe`.
- `cwd/build/apps/ui-sandbox`, a worldsim path, leaves the search order.
  `Foundation::setResourceRoots` (process-global, set from
  `AppConfig::resourceRoots`) come first, then the exe directory, then cwd.
- `TileAtlasBuilder` (`resources/TileAtlasBuilder.cpp:63-64`) hardcodes
  `assets/tiles/surfaces/<name>/pattern.svg`, a worldsim path inside salal; it
  takes the path from its caller.

A byte-provider comes back when a consumer packs assets into an archive.

## Clipboard

`app::Clipboard` is concrete: GLFW's clipboard, or an in-memory one on the
headless platform. Phase 2 `app` can't implement `UI::IClipboard`, which is
worldsim's until phase 3, so worldsim keeps a ten-line adapter. In phase 3 the
interface moves into salal's `ui` and `app` implements it directly.

## Dev server

Compiled in where `SALAL_DEV_SERVER` is defined: Debug and RelWithDebInfo, the
configurations that already define `DEVELOPMENT_BUILD`, when the CMake option
`SALAL_WITH_DEV_SERVER` is on (the default), and Release too only when
`SALAL_DEV_SERVER_IN_RELEASE` is turned on, for a profiling session (off by
default). The definition is PUBLIC, so a game can `#if` on it, but needn't:
`DevServer`'s API is always declared, and where the server is compiled out its
methods are inline no-ops, so a game's registration code links in every
configuration. A shipping build links no `cpp-httplib` and opens no socket. It
binds `127.0.0.1` only, as
today, and starts after the window, so startup logs before it aren't streamed.
A requested port that is busy fails `App::create`, logged, where today the
server thread calls `std::exit(1)` (`DebugServer.cpp:1016`).

Routes:

| route | change |
|---|---|
| `/api/ui/tree`, `/api/ui/lint` | answered by providers the game registers (`"ui.tree"`, `"ui.lint"`) until salal's `ui` registers its own in phase 3; with none registered, an empty tree and a zero-count lint, so an app with no ui (the sandbox, the asteroids prototype) still meets the inspector contract |
| `/api/ui/screenshot` | read before present |
| `/api/input` | grows `char,<codepoint>`, `pad,<device>,<button>,down\|up`, `axis,<device>,<axis>,<value>`, `connect,vpad<n>`, `disconnect,vpad<n>`; injected events go through the accumulator, so injected keys reach the ui the way typed ones do (today they skip the focus manager, `InputManager.cpp:371-375`) |
| `/api/control` | `exit`, `scene`, `pause`, `resume`, `reload`, `vsync` as today, plus `step&ticks=<n>`; games register further actions, which is how world-sim keeps `camera` for `scripts/perf-capture.ps1:23` and `docs/testing/README.md:132` |
| `/api/state?what=` | `scene` answered by the server itself from the stack's `exportState()`; anything else by registered providers, 404 at once for an unregistered `what`, where today it is 503 after 2 s |
| `/api/dev/<verb>` | the game's registered routes; 404 for an unregistered verb, where today it is queued and answered with an ack |
| `/api/health`, `/api/metrics`, `/stream/metrics`, `/stream/logs`, `/` | as today |

```cpp
namespace app {
struct DevRequest { std::string name; std::map<std::string, std::string> params; };
struct DevResponse { int status = 200; std::string contentType = "application/json"; std::string body; };
class DevServer {
public:
	class Registration; // move-only; unregisters when destroyed, so a scene can register in onEnter and hold it
	[[nodiscard]] Registration route(std::string verb, std::function<DevResponse(const DevRequest&)>);   // /api/dev/<verb>, step 3
	[[nodiscard]] Registration control(std::string action, std::function<DevResponse(const DevRequest&)>); // /api/control?action=<action>, step 3
	[[nodiscard]] Registration stateProvider(std::string what, std::function<std::string()>);          // /api/state?what=, /api/ui/tree, /api/ui/lint, step 10
};
}
```

A request parks its HTTP thread until the game thread answers it in the
frame's drain, as today, so handlers never race the game. Asteroids' "sim
state, step N ticks, set the seed" are a `stateProvider`, `step`, and a route.

Fixed on the way across, all found in the survey:

- The busy port above.
- The log ring is single-writer (`LockFreeRingBuffer.h:5`) but the sink is
  called from every thread that logs (`salal/libs/foundation/utils/Log.cpp:83-88`);
  it takes a lock.
- A dev command whose request timed out stays queued and runs whenever a
  scene next drains (`DebugServer.cpp:665-667`); it is dropped.
- `signalShutdownComplete` (`AppLauncher.cpp:226`, through `stopDebugServer()`
  at `:503`) fires before scene exit and GL teardown; it moves to `~App`,
  after both.

`PngEncoder` moves with it, but unconditionally and into `renderer` beside
`stb_image`, since worldsim's `AssetRenderer` (`libs/engine/assets/AssetRenderer.cpp:92`,
`:100`) and so asset-cli use it in every configuration, and two
`STB_IMAGE_WRITE_IMPLEMENTATION` sites can't both link.

## What worldsim keeps, and what changes there

Worldsim's three GUI apps convert in one PR once the salal side is in. It fills
`AppConfig` with `frameCapHz = 120`, vsync on, its ports and its clear colour,
and no scene of its needs a fixed tick.

The bridge is `setDispatch`: worldsim installs a function that does exactly
what `Application.cpp:53-85` and `:145-202` do now, from `app.input()`: keys
and chars from the frame's events to the focus manager first (Tab and
Shift-Tab, then `routeKeyInput`, then `routeCharInput`), whether paused or
not; then, unless paused, a mouse move every frame from the polled position
(hover under a still cursor depends on it, `AppLauncher.cpp:61-63`), Left and
Right edges only, modifiers rebuilt from held Shift, Control and Alt with no
Super bit, and a scroll that is overwritten by each wheel event and
re-dispatched every frame until something consumes it, through overlays then
the top scene's `UI::InputEvent` handler. GameScene's zoom reads that
consumable scroll from the bridge, not the snapshot, so scrolling a list in the
game ui doesn't also zoom the camera (`Application.cpp:195-199`,
`GameScene.cpp:856`). The bridge goes in phase 3, when salal's ui brings its
own dispatch.

| today | becomes |
|---|---|
| `AppLauncher::initialize/run/shutdown`, `AppConfig` (`libs/engine/application`) | each `Main.cpp`: `parseCommandLine`, the platform, `App::create`, its resources, its scenes, `run`, `shutdown`, its teardown |
| `Application`'s `UI::FocusManager` and its key, char and mouse routing | the bridge above |
| `IScene` (40 registered): `update(float)`, `render()`, `handleInput(UI::InputEvent&)`, `getUiRoots()`, `exportState()` | `app::Scene` through a worldsim `UiScene` base that keeps `handleInput` and `getUiRoots`; `exportState` stays an override, now optional |
| `SceneManager::Get()`, 18 `sceneManager->switchTo` sites and `apps/ui-sandbox/Main.cpp:86` | `app.scenes().switchTo`, scenes added by name with their `SceneInfo` factories; the static handoffs (`GameStartConfig`, `GameWorldState`, `NewGameSetup`) and `UserSettings` can move to `Args` and the context object later, not in this PR |
| `InputManager::Get()`, 10 sites | `app.input().frame()`, plus the bridge's scroll for GameScene |
| `ClipboardManager` | `app::Clipboard` plus the `UI::IClipboard` adapter |
| `DebugServer` and its drains: `UiStateDrain` in six scenes, ui-sandbox's overlay renderer, GameScene's dev verbs, camera and state | `app::DevServer` registrations: the tree and lint providers once at startup per app (asset-manager gains them; it has no drain today), GameScene's verbs, `camera` control and other state in its `onEnter`; `what=scene` needs nothing, the server answers it |
| `NavigationMenu` and the two `glfwGetKey` scenes (`ClipScene.cpp:85-102`, `VectorPerfScene.cpp:52`) | an overlay taking `app.coordinateSystem()`; `app.input()` |
| the `glClear` in the loop (`Application.cpp:240-241`) | `AppConfig::clearColor`; the 37 scene files that clear for themselves keep doing it until touched |
| `libs/engine/graphics/PngEncoder` | salal's, in `renderer` |

What changes where a player or worldsim's tooling can see, owned:

- A press and release inside one frame arrive as a down then an up; today the
  host's arrive as a lone up, while injected ones already arrive as both.
- Keys injected through `/api/input` reach the focus manager.
- `/api/state` and `/api/dev` answer 404 at once for what nothing registered,
  instead of 503 after 2 s or a queued ack; asset-manager answers tree and lint.
- A Release build has no dev server unless built for profiling (decision 4).
- The screenshot is read before present (decision 9).
- `/api/control?action=exit` signals completion after scenes exit and GL is
  torn down, not before.
- Startup logs before the window aren't on `/stream/logs`.

## Sequencing

Each salal PR is a minor tag; worldsim pins once, at the end. Moving half the
slice would leave worldsim with two loops.

| PR | salal | worldsim | verified by |
|---|---|---|---|
| 1. Input types (`v0.10.0`) | `foundation/input/`: `Key` completed to GLFW's token set, dense, with `Unknown` and `Count`; `MouseButton` typed; the gamepad enums, `DeviceId`, `InputEvent`, `ButtonState`, `InputSnapshot`, `InputAccumulator`, `Binding`, `ActionMap` | builds at the new tag, no change | headless tests: transitions inside one tick at 120 Hz under 60 Hz frames, two ticks in a frame, a frame with no tick, the two readers apart, axis thresholds, two keys to an axis, radial and per-axis deadzones, split keyboard as two maps |
| 2. The frame and scenes (`v0.11.0`) | `Clock`, `ManualClock`, `Platform` with the GLFW and headless implementations, `App::create/tick/run/shutdown`, the accumulator and alpha, pause, focus freeze, `step`, `setDispatch`, `setVsync`, `SceneStack` with typed args and results, overlays, the context object, `parseCommandLine`, metrics sections, empty font atlases as no text, the `ResourcePath` fixes | none | headless tests of exact tick counts, the clamp and the cap, pause, step, a covered scene's frozen alpha, change order within a frame, push, `finish` with a move-only result, replace, reset, by-name switching, resize, the context's type check; the sandbox stub runs one scene on GLFW |
| 3. Devices (`v0.12.0`) | the GLFW device layer: keyboard, mouse, gamepads, hot-plug, slots and reconnects, key names, capture, `Clipboard` | none | headless tests through the accumulator; a pad and a hot-plug by hand |
| 4. Dev server (`v0.13.0`) | from worldsim into `app`, behind the flags; registrations with handles, control actions, providers and the empty defaults, `what=scene` from the stack; the new `/api/input` events; `step`; the fixes; `PngEncoder` into `renderer` | none | route tests against an `App` on the headless platform; a Release build links no httplib and opens no socket, and one built with `SALAL_DEV_SERVER_IN_RELEASE` serves |
| 5. Settings and saves (`v0.14.0`) | `Settings`, `userConfigDir`, migration, `video.*` applied by `App`, `setFullscreen`, `ActionMap` bindings; `SaveStore`, `userDataDir`, envelopes, backups, upgrade chains | none | tests: round trip, unknown keys kept, corrupt file moved aside, migration, a newer file; a save upgraded across three versions, a failed upgrade leaving the original, a newer save refused untouched, a crash between temp and rename |
| 6. Sandbox (`v0.15.0`) | the harness: a scene picker drawn with `Primitives`, scene by name, the dev server, a committed atlas (decision 8); the renderer-level scenes from worldsim's ui-sandbox (`sdf_minimal`, `polygon`, `arc`, `polyline`, `blend`, and `input_test` rewritten on snapshots, drag derived from them) | none | screenshots of each scene through the dev server, the picker's state through a registered provider |
| 7. Worldsim converts | pin | delete `libs/engine/{application,scene,input,clipboard,debug}`, `graphics/PngEncoder`, and its copies of the six scenes the sandbox took; the table above | every scene through the dev server, screenshots identical; scripted `/api/input` runs before and after: a click that switches scene, Tab and typing into a field, scrolling a list in the game ui (no zoom), input while paused, the camera action; asset-cli PNGs; ctest; the three ports |

PR 5 depends on nothing after PR 2 and can go anywhere after it; PR 6 needs
PRs 3 and 4. When this merges the board's items are retitled to match: the
tick split loses its emscripten path, the config loader becomes settings and
saves, the byte-provider item becomes the `ResourcePath` fixes in PR 2, the
sandbox item becomes PR 6's renderer-level scenes, and the epic's description
drops MEMFS and the pure-UI scenes.

## Decisions

Reviewed with Kevin, 2026-10-27.

1. `exportState` stays on `app::Scene`, optional, and the dev server answers
   `/api/state?what=scene` from the stack. Scenes are the engine's, so scene
   state for debugging is for every game; it never runs in a build without
   the server.
2. The config loader is the user settings file: JSON in the per-user config
   directory, unknown keys kept, versioned with the game's migration.
3. No byte-provider; `ResourcePath` is fixed instead. It comes back when a
   game packs its assets.
4. The dev server is compiled into Debug and RelWithDebInfo, out of Release,
   with `SALAL_WITH_DEV_SERVER` to drop it everywhere and
   `SALAL_DEV_SERVER_IN_RELEASE` to force it into a Release build for
   profiling.
5. Scene arguments and results are typed at compile time; scenes with no
   arguments are reachable by name. State that outlives a scene lives in one
   game-defined context object the App holds. Saves are `app`'s mechanics
   (slots, safe writes, backups, versioned upgrade chains the game supplies)
   over the game's bytes.
6. Phase 2 `app` names no ui type; worldsim bridges its focus manager and
   `UI::InputEvent` through `setDispatch` until phase 3. Less work overall
   than moving the focus manager early, which decision C may replace.
7. The clipboard is concrete in `app`, with a worldsim adapter until phase 3.
8. The phase 2 sandbox has renderer-level scenes and a picker drawn with
   `Primitives`; the ui scenes and the harness's ui follow in phase 3. Its
   font atlas is JetBrains Mono, generated by worldsim's tool and committed
   with the font's licence text.
9. The screenshot is read before present.
10. The worldsim differences listed under "What worldsim keeps" are accepted.
11. Pause freezes fixed ticks and update for scenes and overlays; focus loss
    freezes fixed ticks only.
