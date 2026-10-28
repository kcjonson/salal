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
| salal `libs/app`, `apps/sandbox` | phase 2 PRs 2 and 3: the clock, the GLFW and headless platforms, the frame, the scene stack, overlays and the context object; the device layer, slots, key names, capture and the clipboard; a sandbox that runs one scene on GLFW; tests not counted | 3,502 (the sandbox 117 of them) |
| salal `libs/app/DevServer`, `libs/renderer/PngEncoder` | phase 2 PR 4: the dev server behind its flags, its registrations, the input grammar, and the PNG encoder; tests not counted | 1,914 |
| salal `libs/app/settings`, `saves`, `storage` | phase 2 PR 5: the settings file and `ActionMap` bindings through it, saves, the per-user directories, control names, and the safe write the settings and saves share; tests not counted | 2,460 |
| salal `apps/sandbox` | phase 2 PR 6: the picker, the six scenes and the atlas staging, in place of the one-scene sandbox above | 1,759 |
| salal `libs/foundation/input/` | the `namespace foundation` block under "Input": the types, `InputAccumulator` and `ActionMap` (phase 2 PR 1, axis edges PR 3) | 768 |

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
  the game's teardown, then ~App        fonts, Primitives, window, then the server (an exit request answers now)
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

class PlatformEvents { // what a platform reports while it polls; the App is the one listener, and the dev server injects through it too
public:
	virtual void inputEvent(const foundation::InputEvent&) = 0;           // arrival order, stamped with the platform's clock; not connections
	virtual void deviceConnected(const DeviceInfo&, double time) = 0;     // a pad with its name and model GUID; its DeviceConnected joins the events in order
	virtual void deviceDisconnected(foundation::DeviceId, double time) = 0;
	virtual void framebufferResized(const Renderer::Viewport&) = 0;
};
class Platform { // window, context, callbacks into Input, gamepad polling, clipboard, present, vsync, focus; no GLFW type in the header
public:
	virtual const Clock& clock() const = 0;
	virtual Renderer::Viewport viewport() const = 0;
	virtual std::unique_ptr<Renderer::backend::Backend> createBackend() = 0; // handed to Primitives; null, logged, on failure
	virtual void pollEvents(PlatformEvents&) = 0;
	virtual bool focused() const = 0;
	virtual bool closeRequested() const = 0;
	virtual void present() = 0;
	virtual void setVsync(bool) = 0;
	virtual void setFullscreen(std::optional<int> monitor) = 0; // the window system's monitor order; windowed, logged, for one that isn't there
	virtual std::optional<int> fullscreenMonitor() const = 0;
	virtual void setWindowSize(int width, int height) = 0;      // the windowed size: now while windowed, where fullscreen returns to otherwise
	virtual std::optional<std::string> keyLabel(foundation::Key) const = 0; // the layout's label on a key that prints; glfwGetKeyName
	virtual std::string clipboardText() const = 0;
	virtual void setClipboardText(const std::string&) = 0;
};
std::unique_ptr<Platform> createGlfwPlatform(const WindowConfig&); // GLBackend::create() inside
class HeadlessPlatform final : public Platform { // NullBackend, no window
public:
	explicit HeadlessPlatform(int width, int height);
	ManualClock& clock();
	void push(const foundation::InputEvent&); // arrives in the next tick's poll, as a callback would
	void resize(int width, int height);
	void setFocused(bool);
	void connectGamepad(const DeviceInfo&);            // pads are sampled once per poll and diffed, as GLFW's are, through the same GamepadTracker
	void disconnectGamepad(std::uint8_t index);
	void setGamepad(std::uint8_t index, const GamepadSample&);
	void setKeyLabel(foundation::Key, std::string);
	void addMonitor(int width, int height);            // what setFullscreen takes; the window resizes at once, its event in the next poll
	bool vsync() const;
};
struct GamepadSample { std::array<bool, 15> buttons{}; std::array<float, 6> axes{}; }; // one poll of a pad, triggers 0 to 1

struct WindowConfig {
	std::string title;
	float sizePercent = 0.8F;          // of the primary monitor's work area
	std::optional<int> fullscreenMonitor;
	bool vsync = true;
};

struct FrameConfig {
	double fixedStep = 1.0 / 60.0; // seconds per fixed tick; asteroids sets 1.0 / 120.0
	double maxFrameDelta = 0.25;   // a longer frame counts as this long (worldsim's clamp today)
	int maxTicksPerFrame = 8;      // past this the whole steps left are dropped, the part of a step kept for alpha: the sim runs slower than wall time instead of spiralling
	int frameCapHz = 0;            // 0 is vsync alone; worldsim sets 120 (its sleep-then-spin pacing, Windows timeBeginPeriod)
	bool freezeTicksOnFocusLoss = true;
};

struct AppConfig {
	std::string organization, applicationName; // name the per-user directories
	FrameConfig frame;
	Renderer::backend::ShaderSource baseShader;  // the game's base shader with its hooks (salal_base_shader)
	std::span<const Renderer::FontAtlasSource> fontAtlases; // empty: no text, logged once, not a failure
	Renderer::FontFamily defaultFont = Renderer::FontFamily::Default;
	Foundation::Color clearColor{0.1F, 0.1F, 0.15F, 1.0F}; // through Primitives::clear; the glClear in worldsim's loop goes
	std::vector<std::filesystem::path> resourceRoots;      // searched before the exe directory and cwd
	std::filesystem::path settingsFile;                     // relative: under userConfigDir(organization, applicationName); empty for none
	int settingsVersion = 1;                                // the game's settings format, saved as the file's "version"
	Settings::Migrate migrateSettings;                      // brings an older file up to settingsVersion
	std::filesystem::path savesDirectory;                   // relative: under userDataDir(organization, applicationName); empty for none
	SaveFormat saveFormat;                                  // the game's save version and its upgrade steps
	DevServerConfig devServer;                              // enabled, port, request timeout; a no-op where the server isn't compiled in
	bool metrics = false;
};

struct CommandLine { std::optional<std::string> scene; std::optional<int> httpPort; bool help = false; bool valid = true; };
CommandLine parseCommandLine(int argc, char** argv); // --http-port enables the server, as today

class App {
public:
	static std::unique_ptr<App> create(AppConfig, std::unique_ptr<Platform>); // also nullptr when a requested dev-server port is busy, or while another App lives (Primitives are module-static)
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
   they wait for resume, as today. When they change the top scene, the tick
   reader is consumed and discarded, so the press that opened a menu or popped
   one doesn't reach the new top's first tick. The frame reader isn't, so the
   new top's first update still sees that press in `FrameContext::input`, as
   worldsim's polled input does; a scene that should ignore it reacts in
   `fixedUpdate` or to events. A `resume()` asked for during
   the frame takes effect once the frame is over, so the frame that resumed
   stays paused and its press is dropped with the rest. A resume drops steps
   not run yet, deferred or not, and a `step` asked for after an in-frame
   resume is dropped at the frame's end, logged.
7. Fixed ticks, unless paused or (with `freezeTicksOnFocusLoss`) unfocused:
   `accumulator += realDelta`; while `accumulator >= fixedStep` and fewer than
   `maxTicksPerFrame` have run, the top scene's `fixedUpdate(TickContext)` with
   a snapshot from the tick reader, and `accumulator -= fixedStep`. The first
   tick of a frame sees every transition since the last tick ran, including a
   frame that had none; later ticks in the same frame see only held state. On
   a paused or frozen frame the tick reader is consumed and discarded, so a
   press made during a pause doesn't fire on resume. A tick that pauses ends
   the frame's ticks, and the whole steps left are dropped as past the cap.
8. `update(FrameContext)` on the top scene, then the overlays, with
   `realDelta`, unless paused (worldsim's order). Its input is the frame
   reader's, edges included, even for a scene that became top in step 6.
9. `Primitives::beginFrame`, `clear(clearColor)`, render the stack from the
   topmost opaque scene upward, then overlays, then `Primitives::endFrame`.
   The top scene renders with `alpha = accumulator / fixedStep`; a covered
   scene renders with the alpha it had when it was covered, so the arena under
   a pause menu holds still. `beginFrame` moves here from before update
   (`AppLauncher.cpp:380`); nothing in worldsim draws from `update`.
10. Dev server, post-render: screenshot readback, then tree, lint and state
    providers. Before present (decision 9).
11. `Platform::present()`.

The frame clock starts at the first `tick()`, whose delta is 0, so a game
that loads after `create` doesn't open on a burst of ticks.

A scene's `requestExit` (world-sim's Quit) finishes the frame, and `tick()`
returns false; `glfwWindowShouldClose` the same.

`run()` adds pacing after `tick()`: vsync through the platform, plus the
optional `frameCapHz` sleep-then-spin worldsim has today. Pacing is not in
`tick()`, so a test drives `tick()` with the headless platform's `ManualClock`
and never sleeps; nor does `run()` on a clock that doesn't run in real time.
A minimized window sleeps a little each frame instead of pacing. Metrics map
today's five sections onto the steps: poll (2), input (3 to 6), update (7 and
8), render (9 and 10), present (11 alone, so worldsim's client still reads it
as GPU time); pacing is in the frame's time and in no section.

A fixed tick reads no clock: `TickContext{tick, step, input}` carries the
scene's own tick count since its `onEnter`, from 0, and the step, which is all
a deterministic sim may use, and a replay starts from that count. Variable
update keeps `dt`, for animation and for worldsim, whose scenes are all
variable-step and stay so until it chooses otherwise. A scene that overrides
only `update` and `render` behaves exactly as today; the accumulator still runs
and costs nothing. A discarded tick reader drops release edges as well as
presses: a key held into a pause and let go during it reads up with no
`released()`, so a sim reads held state across a pause, not edges.

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
enum class Key : std::uint16_t { Unknown, /* GLFW's full token set in GLFW's order, dense */ Count }; // A..Z, Num0..Num9 and F1..F25 contiguous
enum class MouseButton : std::uint8_t { Left, Right, Middle, Button4, Button5, Button6, Button7, Button8, Count }; // GLFW's button numbers
enum class GamepadButton : std::uint8_t { A, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide,
	LeftThumb, RightThumb, DpadUp, DpadRight, DpadDown, DpadLeft, Count }; // GLFW's gamepad layout
enum class GamepadAxis : std::uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count }; // triggers 0 (released) to 1; the device layer maps GLFW's -1 to 1
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
struct GamepadState { bool connected = false; std::array<ButtonState, std::size_t(GamepadButton::Count)> buttons{}; std::array<float, std::size_t(GamepadAxis::Count)> axes{};
	std::array<float, std::size_t(GamepadAxis::Count)> startAxes{}; }; // axes at this reader's last read, so an axis read as a button has edges
struct InputSnapshot { // every device's state, about 1.9 KB
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
	Source source = Source::Key; std::uint16_t control = 0; // the enum value
	float scale = 1.0F;                    // -1 makes a key the negative half of an axis (two keys to one axis)
	float threshold = 0.5F;                // an axis read as a button: down while its value, scale applied, is past this, so -1 reads the negative half
	float deadzone = 0.0F;                 // per axis; sticks take a radial deadzone over both axes in the map
};
struct ActionState { ButtonState button; float value = 0.0F; }; // button transitions follow the reader the snapshot came from; an axis binding's, from startAxes to axes
class ActionMap {
public:
	void bind(std::string_view action, Binding);
	void setStickDeadzone(GamepadAxis x, GamepadAxis y, float radius);
	ActionState evaluate(std::string_view action, const InputSnapshot&, std::span<const DeviceId>) const;
};
}

namespace app {
struct DeviceInfo { foundation::DeviceId id; std::string name; std::string modelGuid; }; // GLFW's GUID names a model, not a unit
inline constexpr int kMaxPlayerSlots = 8;
class Input {
public:
	const foundation::InputSnapshot& frame() const;           // this frame's state; replaces InputManager's polled API
	std::span<const foundation::InputEvent> events() const;   // this frame's events, in order
	std::span<const DeviceInfo> devices() const;              // connected now: keyboard, mouse, then pads by id
	std::optional<foundation::DeviceId> pressedDevice() const; // the first press this frame, in event order, from a device not backing its share of slots: "press a button to join"
	void claim(int slot, foundation::DeviceId);               // a keyboard can back two slots, a mouse or pad one; refused, logged, past that
	void release(int slot);
	std::optional<foundation::DeviceId> deviceForSlot(int slot) const; // a departed pad's id until another device takes it
	bool slotConnected(int slot) const;
	std::string keyName(foundation::Key) const;               // the layout's label for the main block's printable keys, capitalised; a fixed English table for the rest
	std::optional<Binding> captureNext();                     // the next press this frame not yet returned, for a rebinding screen; an axis past half travel binds its half
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
- Slots live in `app::Input`, not in the snapshot, which holds every device's
  state; decision D's text says the snapshot carries the slot and is amended in
  this change. A claimed pad that disconnects keeps its slot, `slotConnected`
  goes false, and the game decides whether to pause. A pad that connects while
  a slot is waiting on the same model GUID takes that slot; anything else
  arrives unclaimed. Two slots waiting on one model take its pads lowest slot
  first; a pad with no GUID is taken back by nothing. While a slot waits,
  `deviceForSlot` still names the departed pad's id, which the accumulator has
  released and zeroed, so a sim reading it sees the release; once another
  device connects at that id it's empty. A pad that leaves and comes back at
  another id in one poll moves the slot before a tick reads the release, so a
  sim sees a second press with no release between. Releasing a waiting slot
  ends the wait, and a claim on a claimed slot replaces its device.
- `pressedDevice` counts a key going down (not a repeat) as the keyboard's,
  whatever device the event names, a mouse button as the mouse's and a pad
  button as its pad's. It skips a device that backs its share of slots already,
  so a joined player's press doesn't hide a new one's. Sticks and triggers
  don't press.
- `captureNext` binds a stick or trigger crossing half travel away from rest to
  that half: scale 1, or -1 for a stick's negative half.
- An axis read as a button has edges: each reader's snapshot holds the axes at
  its last read (`startAxes`) beside their values now, and `ActionMap` folds
  an axis binding's crossing with its button bindings' transitions, reporting
  the most transitions some order of the bindings' changes gives. Pads are
  sampled once a frame, so the frame reader sees every crossing but one: a pad
  that disconnects and reconnects in one poll with a trigger held gives no edge
  (a held button gives a release and a press). The tick reader's axis edges are
  sampled at frame rate as well, so a tick spanning frames with no tick sees
  the net crossing, and a trigger tap shorter than the gap between ticks can be
  lost at low tick rates; several virtual pad events in one frame give the net
  one too.
- A trigger the pad's mapping lacks, which GLFW leaves at 0, reads released: a
  trigger reads 0 until GLFW has reported it below 0, where mapped ones rest.
- Connections come through `PlatformEvents::deviceConnected` and
  `deviceDisconnected`, with the name and model GUID an `InputEvent` can't
  carry; the dev server registers its virtual pads (`pads[16..19]`) through
  the same calls.
- Keys are positions, as decision D settled; `keyName` shows the layout's
  label, with a fallback because GLFW names only printable keys. The label is
  capitalised by codepoint, by range: ASCII, the Latin-1 Supplement (U+00E0 to
  U+00FE but U+00F7), Greek and Cyrillic. A key the keyboard lacks has no
  label.
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
	template <typename S> void add(std::string name);                         // the same, for a complete type whose Args default (built with Args{})
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

class Overlay { /* handleEvent, update(FrameContext), render(), onResize: today's IOverlay, owned by the App; onExit at shutdown */ };
}
```

- Changes apply at step 6, after dispatch and before update, so a click that
  switches scene switches in the same frame, as today. A `replace`, `reset` or
  `switchTo` discards the changes queued before it in that frame, so two
  switches in one frame build and enter only the last scene, as today
  (`SceneManager.cpp:48`); pushes and pops queue in order. A change asked
  for while changes apply (from an `onEnter` or a result callback, say) waits
  for the next frame, as a switch from worldsim's `onEnter` does today, so a
  callback that pushes leaves its pusher uncovered, updated and rendered for
  one frame before the next scene covers it. The first scene, pushed before
  the first frame, enters in that frame's step 6, where worldsim enters it
  inside `AppLauncher::initialize` (`:371`).
- Only the top scene receives events, fixed ticks and update. Covered scenes
  render, from the topmost opaque one up.
- Arguments and results are typed per scene and checked at compile time:
  `finish(R)` is how a scene returns, and the caller's callback runs before
  its `onUncovered`, with the finished scene off the stack but not yet
  destroyed, so a result that points into it is good in the callback. The
  stack type-erases them itself, so `Args`, `Result` and the callback may be
  move-only (a `std::unique_ptr` in an `Args`), which `std::function` and
  `std::any` couldn't hold. A push with a callback remembers the scene on top
  when it was asked for, whoever asks (an overlay or game code too), and is
  dropped, logged, if that scene has left the stack by the time it applies (a
  scene stays on top until step 6, so it can ask to leave and then push), so a
  callback never outlives its pusher. A push without a callback queues in
  order regardless, so it can land over a scene that replaced the one that
  asked.
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
  name has a factory with no arguments, or is a type whose `Args` default,
  built with `Args{}`. A scene pushed with arguments has no name, and `reload`
  refuses it, so it is never rebuilt without them.
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
	Settings();                                                                      // in memory, no file
	static Settings load(const std::filesystem::path&, int version, Migrate = {}); // missing: empty; unreadable or corrupt: logged, moved aside, empty
	template <typename T> auto get(std::string_view path, T fallback) const;        // "video.vsync"; a std::string for a literal fallback
	template <typename T> void set(std::string_view path, T value);                 // an empty std::optional sets null; a non-finite float is refused
	bool contains(std::string_view path) const;                                      // null counts
	const nlohmann::json* find(std::string_view path) const;                         // a read that makes nothing; null when absent
	nlohmann::json& section(std::string_view path);                                  // a game-owned subtree, made when absent
	bool save();                                                                     // temp file, flushed, then renamed; unknown keys kept
	const std::filesystem::path& file() const;                                       // empty in memory
	int version() const;                                                             // what save writes
};
bool validFileName(std::string_view);                                                // the names userConfigDir and userDataDir take
bool validSlotName(std::string_view);                                                // a valid file name in lower case
std::filesystem::path userConfigDir(std::string_view organization, std::string_view application);
void writeBindings(const foundation::ActionMap&, nlohmann::json& section);
void readBindings(const nlohmann::json& section, foundation::ActionMap&);
}
```

- Location: `%APPDATA%\<org>\<app>` on Windows, `~/Library/Application Support/<org>/<app>`
  on macOS, `$XDG_CONFIG_HOME/<org>/<app>` (else `~/.config/...`) on Linux,
  read from the environment (an `XDG_` variable that isn't an absolute path is
  ignored, as the spec says), else from the system (the known folder on Windows,
  the password database's home on POSIX) for a process whose environment was
  scrubbed, and made by the first save. `AppConfig::settingsFile`
  is under it when relative, and used as it is when absolute (tests). Both names
  have to pass `validFileName` (under saves, below); otherwise there's no
  directory, logged, and the settings live in memory.
- Unknown keys survive a save, which is how dual-deck-builder keeps older and
  newer builds compatible; a `version` field plus the game's `Migrate` covers
  the rest. A file newer than the build loads as it is, isn't migrated down,
  and keeps its version on save. The top-level `version` is the Settings' own,
  taken out at load and written by `save`, and no path may start with it (refused
  like an invalid path); `settingsVersion` below 1 fails `App::create`. A file
  without a version, or with one that isn't a whole number of 0 or more, reads
  as version 0. `Migrate` runs once,
  with the file's version, and the file is at the build's version after it (with
  no `Migrate`, as it is). The file changes when the game saves, not at load.
- A file that can't be read, isn't JSON, or is JSON but not an object is moved
  aside to `<file>.corrupt` (`.corrupt.1` and on when that's taken, so an earlier
  copy is never replaced), and the settings start empty; so do they when
  `Migrate` throws or leaves something other than an object. When the file can't
  be moved, the settings are that run's alone and `save` refuses to write over it.
- Paths are keys joined by dots; one with an empty key reads and sets nothing,
  logged, and its section isn't saved. `get` gives the fallback for nothing
  there or a value of another type, logged: a bool only from a bool, an integer
  only from an integer in the type's range, a float from any number, a string
  from a string; a string literal fallback reads as a `std::string`. `set` makes the
  objects along its path and replaces, logged, anything on the way that isn't one,
  so a set is always read back; `section` does the same for the subtree itself.
  `set` refuses a float that isn't finite, which JSON can't hold. Keys are object
  keys, numeric-looking ones included; an array isn't indexed by path, and a key
  with a dot in it is reached only through its parent's `find` or `section`.
  `find` reads without making anything, which is how to read a section
  (`if (const auto* input = settings.find("input")) readBindings(*input, map);`).
- `app` reads its own keys at create, `video.vsync`, `video.fullscreenMonitor`
  (null for windowed) and the windowed size as `video.windowWidth` and
  `video.windowHeight`, and applies them through the platform, the monitor
  before the size; a key that isn't there, holds the wrong type, or names a
  monitor that isn't connected, and a size with only one half, leave the window
  as its `WindowConfig` made it. The settings load after the platform has made
  and shown the window, so a saved fullscreen or size is a visible switch at
  launch, and the file overrides `WindowConfig`. `setVsync` and `setFullscreen`
  apply a change and keep it in the settings, the monitor as asked, so one
  unplugged now is tried again at the next launch (`App::fullscreenMonitor()`
  says where the window actually is); a windowed resize keeps the size, a
  minimized one doesn't. The dev server's `vsync` control goes through
  `setVsync`, so it lands in the settings too, and is saved if the game saves.
  The App never saves the file; the game does, from its settings screen or
  before shutdown. Everything else is the game's.
- `setFullscreen` goes through three platform calls, `setFullscreen`,
  `fullscreenMonitor` and `setWindowSize`; the GLFW platform remembers where
  the window was before it went fullscreen and puts it back there, and keeps a
  windowed size on the work area of the monitor it's on, shrinking and moving the
  window as it must. A monitor is its position in the list of those connected
  now, so unplugging one can change which monitor an index names, and one that
  isn't connected changes nothing, fullscreen or not. GLFW resizes
  the window inside the call on Windows and macOS, outside any poll, so the App
  takes the platform's new size at once, the coordinate system and `Primitives`
  with it, and the frame that asked draws at the size GL's viewport already has;
  scenes and overlays hear of it through `onResize` after the next poll, as of
  any resize. The GLFW platform's relay keeps a held resize in its place among
  the events held around it (the latest only), where it used to deliver it first.
- `ActionMap` bindings round-trip through a section as
  `{"actions": {"<action>": [{"key": "Space", "scale": 1, "threshold": 0.5, "deadzone": 0}, ...]}, "sticks": [{"x": "LeftX", "y": "LeftY", "radius": 0.2}]}`.
  A binding names its control under its source (`key`, `mouse`, `button`,
  `axis`) by foundation's enumerator name, read in any case (`controlName`
  and `controlNamed`, in `input/ControlNames.h`), since the enum values
  follow GLFW's order; every field is written, so a change to `Binding`'s
  defaults doesn't change a saved binding. Reading goes over a map's defaults
  action by action: an action in the section replaces that action's bindings
  (`[]` clears it), one that isn't there keeps its default, and what this
  build can't read is skipped, logged; an action whose list holds nothing this
  build can read keeps its bindings, since that's a newer build's list, not a
  player clearing it. A skipped entry in an action that is read isn't kept: the
  next write writes the action as the map has it. Writing replaces the map's
  actions and sticks in the section and keeps the rest, a newer build's actions
  included. Writing pins every action the map has, its defaults included, at the
  first save, so changing a default later reaches existing players only through a
  `settingsVersion` bump whose `Migrate` drops the section (or that action). The
  name tables are checked at compile time against the enum values, so an
  enumerator added mid-enum can't move saved bindings.
  `foundation` stays free of JSON; `ActionMap` gained `actions()`,
  `bindings(action)`, `stickDeadzones()` and `unbind(action)`, which clears
  an action and keeps it listed, so a cleared action is written as `[]` and
  stays cleared over the defaults.
- `nlohmann::json` is in `app`'s public API, so `app` links it PUBLIC, in
  every configuration, where the renderer keeps it private.

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
	int version = 1;                                                 // the game's format version, not salal's; 1 or more
	std::vector<std::function<bool(std::vector<std::byte>&)>> upgrades; // upgrades[i] takes version i+1 to i+2; the game writes each step
};
struct SlotInfo { std::string name; int version = 0; std::uint64_t writes = 0; std::chrono::system_clock::time_point written; bool intact = false; };
template <typename T, typename E> class Expected; // std::expected's has_value, value, error, operator*, ->, bool and value_or; Unexpected(e) for an error
class BadExpectedAccess;                          // what value() throws on an error, as std::bad_expected_access
class SaveStore {
public:
	enum class LoadError { Missing, Corrupt, NewerThanBuild, UpgradeFailed };
	SaveStore(std::filesystem::path directory, SaveFormat);                          // the directory is made by the first write
	bool write(std::string_view slot, std::span<const std::byte>);                   // temp file, flushed, then renamed; the previous save kept as the backup
	Expected<std::vector<std::byte>, LoadError> read(std::string_view slot);         // runs every upgrade needed, in order, before returning
	bool writeJson(std::string_view slot, const nlohmann::json&);                    // the same, for games that save JSON, as its text
	Expected<nlohmann::json, LoadError> readJson(std::string_view slot);
	std::vector<SlotInfo> slots() const;                                              // by name: version, writes, time written, intact
	bool restoreBackup(std::string_view slot);                                        // swaps the save and its backup, through a staged third file
	bool remove(std::string_view slot);                                               // every file of the slot
};
std::filesystem::path userDataDir(std::string_view organization, std::string_view application); // beside, not inside, the settings directory
}
```

The design first wrote `std::expected`, which is C++23; salal is C++20 and
stays so, with no dependency added, so `read` and `readJson` return
`app::Expected` (`saves/Expected.h`), the part of `std::expected` they need
under its member names, and moving to the standard type is a respelling.

A slot is `<slot>.save` in the store's directory, with `<slot>.save.bak` beside
it. Each file is an envelope, little-endian:

| offset | size | field |
|---|---|---|
| 0 | 8 | magic, ASCII `SALALSAV` |
| 8 | 4 | envelope version, u32, 1 |
| 12 | 4 | the game's format version, i32, 1 or more |
| 16 | 8 | write counter, u64, from 1 |
| 24 | 8 | time written, i64, microseconds since 1970-01-01 UTC |
| 32 | 8 | payload length n, u64 |
| 40 | n | the game's bytes |
| 40+n | 4 | CRC-32 (zlib's) of everything before it |

A later envelope version (2 to 8) keeps the first 16 bytes where they are, so an
older build can still say which version a save is, and reports it
`NewerThanBuild`; past 8, which is what most flipped bits there give, the header
is damaged.
The time is in the envelope rather than taken from the file, whose modification
time a copy may not keep.

- Each save is an envelope: the game's format version, a write counter, and
  the game's bytes. Bytes rather than JSON, so a game with large binary data
  isn't forced through JSON; the JSON pair covers the rest.
- On load the store runs the game's upgrade steps from the save's version to
  the build's, in order, once, and keeps the pre-upgrade file as the backup
  until the upgraded save is written. A save newer than the build isn't loaded
  and isn't touched. As landed: once every step has succeeded the upgraded save
  is written back at the build's version, the pre-upgrade file becoming its
  backup, so the steps don't run on every load (if that write fails the bytes
  are still returned, logged, and the next read upgrades again). A step that
  returns false or throws, or a version the format has no step for (past the end
  of `upgrades`, or an empty function), is `UpgradeFailed`, and every file is
  as it was.
- A write copies the save it replaces over the backup, then writes the new save,
  each through a temp file of its own flushed to the disk and renamed over its
  target, so at every moment the save is whole, the last good one or the new one.
  What isn't one of this build's good saves never goes over the backup: a save
  damaged some other way (a disk that lost a write: a short file, a bad CRC) is
  `Corrupt` to `read` and, when written over, copied aside to `<slot>.save.corrupt`
  (`.corrupt.1` and on after that); one that can't be read at all is hard-linked
  there, so its name holds a save until the new one's rename (renamed there
  where it can't be linked: a directory, a filesystem without links); and a
  newer build's save is kept as `<slot>.save.v<its version>`, which only a copy
  of that version written later (a higher write counter) replaces, so a player
  who tried a beta and went back doesn't lose the beta's latest save to the
  stable build's autosaves. A newer build's backup is renamed to its own name
  the same way before it's replaced, unless a later copy is there. No read looks
  at a `.v<N>` file; it's there to be recovered by hand.
- The safe write: each write's temp file is `<target>.<pid>.<n>.tmp`, created
  exclusively, so two writers never share one, and a later write of the target
  sweeps up ones more than ten minutes old. It's flushed with `F_FULLFSYNC` on
  Apple, where `fsync` leaves the data in the drive's cache (`fsync` where a volume
  can't take it), `fsync` elsewhere on POSIX and `FlushFileBuffers` on Windows. On
  Windows the rename is a POSIX-semantics one (`FileRenameInfoEx`), which replaces
  a target a scanner, indexer or sync client has open, with `MoveFileExW` where the
  filesystem can't do it, and a rename refused because a file is held is tried
  again for about a second. On POSIX the directory is flushed after the rename,
  and after it's made, so the rename itself survives a power cut.
- `restoreBackup` swaps the save and its backup, so a second call undoes it. The
  save being replaced is staged as `<slot>.save.swap` first, then the backup is
  renamed over the save, then the swap file over the backup, so the replaced
  save is on disk under some name throughout. The call is true once the save is
  the backup's: when only the last rename fails, logged, the swap file stays,
  and the next call on the slot (read, write, restoreBackup) makes it the backup
  when there's none, drops it when it matches the save, and otherwise keeps it,
  logged, for `remove`. A restoreBackup that finds and finishes an interrupted
  restore is true without swapping again, so a retry doesn't undo what it
  retries. With no save the backup is renamed into place; a damaged save is
  copied aside, one that can't be read linked aside, and the backup renamed
  straight over it, so there's never a moment with no save. A missing or damaged backup is refused with nothing changed. A backup
  alone isn't `read` (`Missing`); `slots()` shows its slot, not intact.
- The write counter goes on from the higher of the save's and the backup's, so
  it never repeats in a slot, a restore included. Nothing on load chooses by it;
  it's in `SlotInfo` to tell which of two copies is the later.
- `slots()` lists every slot with a save or a backup, by name in byte order,
  from each save's header alone (its CRC is `read`'s to check), a time past
  what the clock holds read as zero. `remove` takes every file of the slot: the
  save, the backup, a staged swap, the copies set aside (`.corrupt`, `.corrupt.N`,
  `.vN`) and temp files, and is true for a slot that had none.
- Slot names become file names, so they, and the organization and application
  names, have to pass `validFileName`: 1 to 64 characters from ASCII letters,
  digits, space, `-`, `_` and `.`, not starting or ending with a space or a dot,
  and not a Windows device name (CON, PRN, AUX, NUL, COM0 to COM9, LPT0 to LPT9,
  any case, alone or before a dot, spaces before the dot included). Slot names
  are lower case besides (`validSlotName`), so two slots that differ only in case
  can't be one file on Windows and macOS and two on Linux. A refused slot is
  `Missing` to `read` and false everywhere else, logged, and no file is touched.
- `userDataDir` is `%LOCALAPPDATA%\<org>\<app>` on Windows (saves can be large,
  and Local doesn't roam), `~/Library/Application Support/<org>/<app> Data` on
  macOS, which has one root for both, and `$XDG_DATA_HOME/<org>/<app>` (else
  `~/.local/share/...`) on Linux, with the same fallbacks to the system as
  `userConfigDir`. `AppConfig::savesDirectory` is under it when relative.
- A store isn't thread-safe. Two on one directory (two processes, say) don't
  tear each other's files, since every write is its own temp file renamed into
  place, but don't coordinate: the last write wins, and the counter goes on from
  what's on disk.
- Dual-deck-builder built all of this by hand (A/B slots, a recovery copy, a
  `{version, sequence, campaign}` envelope, upgrades from 8 to 12); asteroids
  asks for Continue, profiles, leaderboards and "save migrations". The
  deterministic games save seeds and fork names and counters, never a PRNG's
  state mid-stream, which is content and so theirs.

## Resources

No byte-provider in phase 2. WebAssembly was its reason; worldsim's
`findResource` path search is enough for three native games; and nobody packs
assets. What phase 2 fixes instead, in `foundation/utils/ResourcePath`:

- The executable's directory is found on Linux too, from `/proc/self/exe`, and
  on macOS it resolves links as Linux's does.
- `cwd/build/apps/ui-sandbox`, a worldsim path, left the search order.
  `Foundation::setResourceRoots` (process-global, set from
  `AppConfig::resourceRoots`, which replace any set before `create` and are
  cleared by `~App`) come first, then the exe directory, then cwd.
- `TileAtlasBuilder` takes each surface's pattern from its caller, a resource
  path it finds through `findResource`, where it hardcoded worldsim's
  `assets/tiles/surfaces/<name>/pattern.svg`; a path it can't find is a
  warning, and the surface gets the checker.

A byte-provider comes back when a consumer packs assets into an archive.

## Clipboard

`app::Clipboard` is concrete: GLFW's clipboard, or an in-memory one on the
headless platform, through `Platform::clipboardText` and `setClipboardText`.
Its `getText` and `setText` are `UI::IClipboard`'s names. Phase 2 `app` can't
implement `UI::IClipboard`, which is worldsim's until phase 3, so worldsim keeps
a ten-line adapter. In phase 3 the interface moves into salal's `ui` and `app`
implements it directly.

## Dev server

Compiled in where `SALAL_DEV_SERVER` is defined: Debug and RelWithDebInfo, the
configurations that already define `DEVELOPMENT_BUILD`, when the CMake option
`SALAL_WITH_DEV_SERVER` is on (the default), and Release too only when
`SALAL_DEV_SERVER_IN_RELEASE` is turned on, for a profiling session (off by
default); MinSizeRel never, nor a configuration a game defines for itself (a
"Profile", say), which gets neither the server nor `DEVELOPMENT_BUILD`. A
multi-config generator builds every
configuration from one configure, so the choice is a generator expression,
`SALAL_DEV_SERVER_GENEX`, and the define, the server's sources and the
`cpp-httplib` link all carry it; `salal_check_layering` fails a configure
where an `app` link names httplib without it, a lower layer links it, or a
public header reaches `httplib.h`, a tripwire on the link rather than a proof. The definition is PUBLIC, so a game can
`#if` on it, but needn't: `DevServer`'s API is always declared, and where the
server is compiled out its methods are inline no-ops, so a game's registration
code links in every configuration. A shipping build links no `cpp-httplib` and
opens no socket. A Release build made for profiling serves with an empty log
stream, since the logger's sink, like `LOG_INFO`, is `DEVELOPMENT_BUILD`'s. It
binds `127.0.0.1` only, as today, and starts after the window, so startup logs
before it aren't streamed. A requested port that is busy fails `App::create`,
logged, where today the server thread calls `std::exit(1)`
(`DebugServer.cpp:1016`); port 0 takes any free one, which `port()` reports.
Enabled in a configuration it isn't compiled into, `start` logs one ERROR, so
a profiling run says why nothing listens; a game that wants the server on by
default enables it with `kDevServerCompiledIn`, or for `--http-port`, as the
sandbox does, so a shipped build's log stays clean.

Routes:

| route | answered | change |
|---|---|---|
| `/api/ui/tree`, `/api/ui/lint` | step 10 | by providers the game registers (`"ui.tree"`, `"ui.lint"`) until salal's `ui` registers its own in phase 3; with none registered, `{"viewport":{"width":w,"height":h},"roots":[]}` at the logical viewport and `{"count":0,"violations":[]}`, so an app with no ui (the sandbox, the asteroids prototype) still meets the inspector contract |
| `/api/ui/screenshot` | step 10 | read before present; one readback answers every screenshot parked for the frame; 500 when there is no backbuffer to read |
| `/api/input` | queued at once, joins at step 3 | grows `char,<codepoint>`, `pad,<device>,<button>,down\|up`, `axis,<device>,<axis>,<value>`, `connect,vpad<n>`, `disconnect,vpad<n>`; a request's events all queue or none do, and a malformed one is a 400 naming it; injected events go through the accumulator, so injected keys reach the ui the way typed ones do (today they skip the focus manager, `InputManager.cpp:371-375`) |
| `/api/control` | step 3 | `exit`, `scene`, `pause`, `resume`, `reload`, `vsync` as today, plus `step&ticks=<n>` (one tick without `ticks`, 409 unless paused); games register further actions, which is how world-sim keeps `camera` for `scripts/perf-capture.ps1:23` and `docs/testing/README.md:132`; an action answers once it has run, though `scene` and `reload` queue their change, which applies at step 6 (after resume while paused), so they answer before it takes effect, as today; an unknown action is a 404 at once (today a 400), a `scene` nothing was added under a 404 and a `reload` of a scene pushed with arguments a 409 |
| `/api/state?what=` | step 10 | `scene` answered by the server itself from the stack's `exportState()`; anything else by registered providers, 404 at once for an unregistered `what`, where today it is 503 after 2 s; `what` still defaults to `summary` |
| `/api/dev/<verb>` | step 3 | the game's registered routes; 404 for an unregistered verb, where today it is queued and answered with an ack |
| `/api/health`, `/api/metrics`, `/stream/metrics`, `/stream/logs`, `/` | at once | as today; the index page lists every route, without worldsim's developer-client notes |

```cpp
namespace app {
inline constexpr bool kDevServerCompiledIn = /* SALAL_DEV_SERVER is defined */;
struct DevServerConfig { bool enabled = false; int port = 0; double requestTimeout = 10.0; }; // port 0: any free port
struct DevRequest { std::string name; std::map<std::string, std::string> params; };            // the first of a repeated key
struct DevResponse { int status = 200; std::string contentType = "application/json"; std::string body; };
class DevServer {
public:
	using Handler = std::function<DevResponse(const DevRequest&)>;
	using Provider = std::function<std::string()>; // JSON
	class Registration { public: void reset(); bool active() const; }; // move-only; unregisters when destroyed, so a scene can register in onEnter and hold it
	[[nodiscard]] Registration route(std::string verb, Handler);          // /api/dev/<verb>, step 3
	[[nodiscard]] Registration control(std::string action, Handler);      // /api/control?action=<action>, step 3
	[[nodiscard]] Registration stateProvider(std::string what, Provider); // /api/state?what=, /api/ui/tree, /api/ui/lint, step 10
	bool serving() const;
	int port() const;                   // the bound port; 0 when not serving
	std::size_t parkedRequests() const; // waiting for the frame now
};
}
```

A request parks its HTTP thread until the game thread answers it in the
frame's drain, as today, so handlers never race the game. Asteroids' "sim
state, step N ticks, set the seed" are a `stateProvider`, `step`, and a route.
`/api/input` doesn't park: injected input waits in the server while the app is
paused, so a request that waited for it would time out.

What the first version of this design left open, as landed:

- A parked request waits `requestTimeout`, 10 s by default (today's
  screenshot wait) and held to an hour, whatever its route. One the frame
  hasn't taken by then is answered 503 and dropped; one it has taken is
  running, and answers when it finishes. An exit waits up to 30 s for `~App`,
  as today, and says `"shutdown":"timed out"` past that. Requests still parked
  when the App goes are answered 503, exits `"shutdown":"complete"`, and one
  taken and never answered (something threw out of a drain) 500.
- Once exit has been asked for at step 3, or before it in the frame, the
  actions and routes left in that step are answered 503 without running, and
  injected input stays queued. Only an exit asked for at step 3 ends the frame
  there; one from an `onResize` at step 2 finishes it, as a scene's does.
- Several registrations may hold one name; the newest answers, and when it
  goes the one before it answers again, so a scene pushed over another can
  register the verb the other holds and hand it back when it pops.
- A registration, or a handle going, takes effect at once for every request,
  parked or not, from inside a drain too: an action that registers a route
  answers a request for it in that frame's routes, and one that drops a route
  turns a request parked for it into a 404, since actions run first.
- Refused, logged, as an inactive handle: an empty handler, a verb with
  anything but letters, digits, `-` and `_` (verbs match without case, as
  today), an action or `what` with anything but those and `.`, a built-in
  action, and `scene`. A handle that outlives the App does nothing when it
  goes.
- A handler or provider that throws answers 500 with what it threw, logged as
  every call into game code is, and the frame goes on.
- `/api/state?what=scene` is `{"scenes":[{"name":...,"state":...}, ...]}`,
  top first: each scene's `name()` and its `exportState()` as it gave it,
  `null` for none, and `null` beside an `"error"` when it threw or isn't JSON.
- `/api/input`: a codepoint is decimal, or hex after `0x` or `U+`; pad buttons
  and axes go by `GamepadButton` and `GamepadAxis` names without case, and an
  axis value is -1 to 1. `pad` and `axis` take `vpad0` to `vpad3` only, since
  real pads are the device layer's. A vpad connects and disconnects through
  `PlatformEvents::deviceConnected` and `deviceDisconnected`, as `vpad<n>`
  with the model GUID `salal-dev-server-vpad<n>`, so `Input::devices()` lists
  it, a slot can claim it, and a slot waiting on a real pad's model never takes
  it; its buttons and axes go through `inputEvent`. The server tracks which
  it connected, so input to one that isn't connected, a second connect, a
  disconnect of one that isn't connected and a vpad past 3 are 400s, as is an
  unknown key (a 200 and a warning today). Injected events carry no modifier bits, as today.
  At most 1024 events wait for the frame; past that, which only a paused or
  stalled app reaches, a request is a 429 and queues nothing. cpp-httplib
  keeps one of a repeated `key=value` pair, so two identical events in one
  request are one, as today; send a double click as two requests.

The log stream needs no singleton of its own: the logger's one sink slot
(`foundation::Logger::setSink`, module-static as the logger is) holds the
running server, set once it listens and cleared before it stops. Clearing it
waits for calls already inside the sink, so a thread still logging as the App
goes never writes into a server that's gone.

Fixed on the way across, all found in the survey:

- The busy port above.
- The log ring was single-writer (`LockFreeRingBuffer.h:5`) but the sink is
  called from every thread that logs (`salal/libs/foundation/utils/Log.cpp:83-88`);
  it takes a lock.
- A dev command whose request timed out stayed queued and ran whenever a scene
  next drained (`DebugServer.cpp:665-667`); it is dropped.
- `signalShutdownComplete` (`AppLauncher.cpp:226`, through `stopDebugServer()`
  at `:503`) fired before scene exit and GL teardown; `~App` answers the exit
  after both.

`PngEncoder` moves with it, but unconditionally and into `renderer` beside
`stb_image`, as `<PngEncoder.h>` in namespace `Renderer`, since worldsim's
`AssetRenderer` (`libs/engine/assets/AssetRenderer.cpp:92`, `:100`) and so
asset-cli use it in every configuration, and two
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
- Control actions answer once they have run, at the next frame's step 3,
  not as they are queued; an unknown action is a 404, not a 400, and a
  `scene` nothing was added under a 404, not a 200 and a log line.
- A request the frame doesn't reach in 10 s is a 503 and dropped, whatever
  its route.
- Malformed `/api/input` events are 400s: an unknown key, which is a 200 and
  a warning today; fields past the ones an event takes; a number that doesn't
  parse whole (`click,10px,20`, which `std::stof` took as 10); and a trailing
  comma, which is an empty field where today it was dropped (`keydown,R,`).
  `scene` with an empty `scene=` is a 400.
- `/api/state?what=scene` is `{"scenes":[{"name":...,"state":...}, ...]}`,
  top first, where today it is the one scene's `exportState()`; a script
  reads `scenes[0].state`.
- Startup logs before the window aren't on `/stream/logs`.

## Sequencing

Each salal PR is a minor tag; worldsim pins once, at the end. Moving half the
slice would leave worldsim with two loops.

| PR | salal | worldsim | verified by |
|---|---|---|---|
| 1. Input types (`v0.10.0`) | `foundation/input/`: `Key` completed to GLFW's token set, dense, with `Unknown` and `Count`; `MouseButton` typed; the gamepad enums, `DeviceId`, `InputEvent`, `ButtonState`, `InputSnapshot`, `InputAccumulator`, `Binding`, `ActionMap` | builds at the new tag, no change | headless tests: transitions inside one tick at 120 Hz under 60 Hz frames, two ticks in a frame, a frame with no tick, the two readers apart, axis thresholds, two keys to an axis, radial and per-axis deadzones, split keyboard as two maps |
| 2. The frame and scenes (`v0.11.0`) | `Clock`, `ManualClock`, `Platform` with the GLFW and headless implementations, `App::create/tick/run/shutdown`, the accumulator and alpha, pause, focus freeze, `step`, `setDispatch`, `setVsync`, `SceneStack` with typed args and results, overlays, the context object, `parseCommandLine`, metrics sections, empty font atlases as no text, the `ResourcePath` fixes | none | headless tests of exact tick counts, the clamp and the cap, pause, step, a covered scene's frozen alpha, change order within a frame, push, `finish` with a move-only result, replace, reset, by-name switching, resize, the context's type check; the sandbox stub runs one scene on GLFW |
| 3. Devices (`v0.12.0`) | the GLFW device layer: keyboard, mouse, gamepads, hot-plug, slots and reconnects, key names, capture, axis edges in the snapshot and `ActionMap`, `Clipboard` | none | headless tests through the accumulator; a pad and a hot-plug by hand |
| 4. Dev server (`v0.13.0`) | from worldsim into `app`, behind the flags; registrations with handles, control actions, providers and the empty defaults, `what=scene` from the stack; the new `/api/input` events; `step`; the fixes; `PngEncoder` into `renderer` | none | route tests against an `App` on the headless platform; a Release build links no httplib and opens no socket, and one built with `SALAL_DEV_SERVER_IN_RELEASE` serves |
| 5. Settings and saves (`v0.14.0`) | `Settings`, `userConfigDir`, migration, `video.*` applied by `App`, `setFullscreen`, `ActionMap` bindings; `SaveStore`, `userDataDir`, envelopes, backups, upgrade chains | none | tests: round trip, unknown keys kept, corrupt file moved aside, migration, a newer file; a save upgraded across three versions, a failed upgrade leaving the original, a newer save refused untouched, a crash between temp and rename |
| 6. Sandbox (`v0.15.0`) | the harness: a scene picker drawn with `Primitives`, scene by name, the dev server, a committed atlas (decision 8); the renderer-level scenes from worldsim's ui-sandbox (`sdf_minimal`, `polygon`, `arc`, `polyline`, `blend`, and `input_test` rewritten on snapshots, the drag's start from the frame's events and its end from the snapshot) | none | screenshots of each scene through the dev server, the picker's state through a registered provider |
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
