// app::Input through an App on the headless platform: the device list, player slots and reconnects,
// the first press, key names, capture for rebinding, and axis edges reaching ticks and frames.

#include "input/Input.h"
#include "tests/AppHarness.h"

#include <backend/NullBackend.h>
#include <input/ActionMap.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace app::testing {

	namespace {

		inline constexpr char kPlay[] = "Play";
		using PlayScene = Recording<kPlay>;

		using foundation::Binding;
		using foundation::DeviceId;
		using foundation::DeviceKind;
		using foundation::GamepadAxis;
		using foundation::GamepadButton;
		using foundation::InputEvent;
		using foundation::Key;
		using foundation::MouseButton;

		DeviceInfo pad(std::uint8_t index, std::string guid, std::string name = "Pad") {
			return {.id = padId(index), .name = std::move(name), .modelGuid = std::move(guid)};
		}

		GamepadSample holding(GamepadButton button) {
			GamepadSample sample;
			sample.buttons[static_cast<std::size_t>(button)] = true;
			return sample;
		}

		GamepadSample tilted(GamepadAxis axis, float value) {
			GamepadSample sample;
			sample.axes[static_cast<std::size_t>(axis)] = value;
			return sample;
		}

		std::vector<DeviceId> ids(std::span<const DeviceInfo> devices) {
			std::vector<DeviceId> found;
			for (const DeviceInfo& device : devices) {
				found.push_back(device.id);
			}
			return found;
		}

		void expectBinding(const std::optional<Binding>& binding, Binding::Source source, std::uint16_t control, float scale = 1.0F) {
			ASSERT_TRUE(binding.has_value());
			EXPECT_EQ(binding->source, source);
			EXPECT_EQ(binding->control, control);
			EXPECT_EQ(binding->scale, scale);
			EXPECT_EQ(binding->threshold, 0.5F);
			EXPECT_EQ(binding->deadzone, 0.0F);
		}

		// A platform that makes the PlatformEvents calls a test scripts, as the dev server will for its
		// virtual pads, which no polled platform reports.
		class ScriptedPlatform final : public Platform {
		  public:
			const Clock&								clock() const override { return time; }
			Renderer::Viewport							viewport() const override { return {800, 600, 800, 600}; }
			std::unique_ptr<Renderer::backend::Backend> createBackend() override {
				return std::make_unique<Renderer::backend::NullBackend>();
			}
			void pollEvents(PlatformEvents& events) override {
				for (const std::function<void(PlatformEvents&)>& step : std::exchange(script, {})) {
					step(events);
				}
			}
			bool					   focused() const override { return true; }
			bool					   closeRequested() const override { return false; }
			void					   present() override {}
			void					   setVsync(bool /*enabled*/) override {}
			std::optional<std::string> keyLabel(Key /*key*/) const override { return std::nullopt; }
			std::string				   clipboardText() const override { return {}; }
			void					   setClipboardText(const std::string& /*text*/) override {}

			ManualClock										  time;
			std::vector<std::function<void(PlatformEvents&)>> script;
		};

		class InputTest : public AppTest {
		  protected:
			Input& input() { return app->input(); }
		};

	} // namespace

	// --- Devices ---

	TEST_F(InputTest, TheKeyboardAndMouseAreAlwaysThereAndPadsComeAndGoInIdOrder) {
		start();
		EXPECT_EQ(ids(input().devices()), (std::vector<DeviceId>{kKeyboard, kMouse}));
		EXPECT_EQ(input().devices()[0].name, "Keyboard");
		EXPECT_EQ(input().devices()[1].name, "Mouse");

		platform->connectGamepad(pad(4, "03004", "Second"));
		platform->connectGamepad(pad(1, "03001", "First"));
		frame();
		EXPECT_EQ(ids(input().devices()), (std::vector<DeviceId>{kKeyboard, kMouse, padId(1), padId(4)}));
		EXPECT_EQ(input().devices()[2].name, "First");
		EXPECT_EQ(input().devices()[2].modelGuid, "03001");
		EXPECT_EQ(input().devices()[3].name, "Second");
		EXPECT_TRUE(input().frame().pads[1].connected);
		ASSERT_EQ(input().events().size(), 2U);
		EXPECT_EQ(input().events()[0].type, InputEvent::Type::DeviceConnected);
		EXPECT_EQ(input().events()[0].device, padId(4));

		platform->disconnectGamepad(1);
		frame();
		EXPECT_EQ(ids(input().devices()), (std::vector<DeviceId>{kKeyboard, kMouse, padId(4)}));
		EXPECT_FALSE(input().frame().pads[1].connected);
		ASSERT_EQ(input().events().size(), 1U);
		EXPECT_EQ(input().events()[0].type, InputEvent::Type::DeviceDisconnected);
	}

	// A connection names its pad, so one through inputEvent, with no name or model, is dropped, and so
	// is a disconnection, which would otherwise zero a pad that Input still lists.
	TEST_F(InputTest, AConnectionEventPushedAsInputIsDropped) {
		start();
		platform->push({.type = InputEvent::Type::DeviceConnected, .device = padId(0)});
		platform->push({.type = InputEvent::Type::ButtonDown, .device = padId(0), .button = GamepadButton::A});
		frame();
		EXPECT_TRUE(input().events().empty());
		EXPECT_FALSE(input().frame().pads[0].connected);
		EXPECT_EQ(input().devices().size(), 2U);

		platform->connectGamepad(pad(1, "0301"));
		platform->setGamepad(1, holding(GamepadButton::B));
		frame();
		input().claim(0, padId(1));
		platform->push({.type = InputEvent::Type::DeviceDisconnected, .device = padId(1)});
		frame();
		EXPECT_TRUE(input().events().empty());
		EXPECT_TRUE(input().frame().pads[1].connected);
		EXPECT_TRUE(input().frame().pads[1].buttons[static_cast<std::size_t>(GamepadButton::B)].down);
		EXPECT_EQ(ids(input().devices()), (std::vector<DeviceId>{kKeyboard, kMouse, padId(1)}));
		EXPECT_TRUE(input().slotConnected(0));
	}

	// Connections straight through PlatformEvents, the dev server's way in: a virtual pad's index is
	// accepted like a real one's, and a duplicate, a keyboard, an index past pads[], a disconnection of
	// a pad never connected and one of the mouse are dropped.
	TEST(InputConnectionTest, PlatformEventsTakeVirtualPadsAndDropWhatIsNotAPad) {
		auto					   owned = std::make_unique<ScriptedPlatform>();
		ScriptedPlatform&		   platform = *owned;
		const std::unique_ptr<App> app = App::create({}, std::move(owned));
		ASSERT_NE(app, nullptr);
		Input& input = app->input();

		platform.script.emplace_back([](PlatformEvents& events) {
			events.deviceConnected({.id = padId(16), .name = "vpad0", .modelGuid = "virtual"}, 0.0);
			events.deviceConnected({.id = padId(16), .name = "again", .modelGuid = "virtual"}, 0.0);
			events.deviceConnected({.id = kKeyboard, .name = "Second keyboard"}, 0.0);
			events.deviceConnected({.id = padId(20), .name = "past pads[]"}, 0.0);
			events.inputEvent({.type = InputEvent::Type::ButtonDown, .device = padId(16), .button = GamepadButton::X});
		});
		app->tick();
		EXPECT_EQ(ids(input.devices()), (std::vector<DeviceId>{kKeyboard, kMouse, padId(16)}));
		EXPECT_EQ(input.devices()[2].name, "vpad0");
		ASSERT_EQ(input.events().size(), 2U);
		EXPECT_EQ(input.events()[0].type, InputEvent::Type::DeviceConnected);
		EXPECT_EQ(input.events()[1].type, InputEvent::Type::ButtonDown);
		EXPECT_TRUE(input.frame().pads[16].buttons[static_cast<std::size_t>(GamepadButton::X)].down);
		EXPECT_EQ(input.pressedDevice(), padId(16));
		input.claim(0, padId(16));
		EXPECT_EQ(input.deviceForSlot(0), padId(16));

		platform.script.emplace_back([](PlatformEvents& events) {
			events.deviceDisconnected(padId(3), 1.0);
			events.deviceDisconnected(kMouse, 1.0);
			events.deviceDisconnected(padId(16), 1.0);
		});
		app->tick();
		EXPECT_EQ(ids(input.devices()), (std::vector<DeviceId>{kKeyboard, kMouse}));
		ASSERT_EQ(input.events().size(), 1U);
		EXPECT_EQ(input.events()[0].type, InputEvent::Type::DeviceDisconnected);
		EXPECT_EQ(input.events()[0].device, padId(16));
		EXPECT_FALSE(input.slotConnected(0));

		// The same model back as another virtual pad takes the slot.
		platform.script.emplace_back([](PlatformEvents& events) {
			events.deviceConnected({.id = padId(17), .name = "vpad1", .modelGuid = "virtual"}, 2.0);
		});
		app->tick();
		EXPECT_TRUE(input.slotConnected(0));
		EXPECT_EQ(input.deviceForSlot(0), padId(17));
	}

	// --- The first press ---

	TEST_F(InputTest, PressedDeviceIsTheFirstPressThisFrameInEventOrder) {
		start();
		platform->connectGamepad(pad(2, "0302"));
		frame();
		EXPECT_EQ(input().pressedDevice(), std::nullopt);

		// A pad sampled after the queue: the key went down first.
		platform->setGamepad(2, holding(GamepadButton::Start));
		platform->push({.type = InputEvent::Type::PointerMove, .position = {5.0F, 5.0F}});
		platform->push({.type = InputEvent::Type::KeyDown, .device = padId(9), .key = Key::Enter});
		frame();
		EXPECT_EQ(input().pressedDevice(), kKeyboard); // key events are the keyboard's whatever they say

		platform->setGamepad(2, {});
		platform->push({.type = InputEvent::Type::KeyDown, .repeat = true, .key = Key::Enter});
		platform->push({.type = InputEvent::Type::KeyUp, .key = Key::Enter});
		frame();
		EXPECT_EQ(input().pressedDevice(), std::nullopt); // a repeat and a release aren't presses

		platform->setGamepad(2, holding(GamepadButton::A));
		frame();
		EXPECT_EQ(input().pressedDevice(), padId(2));

		platform->push({.type = InputEvent::Type::PointerDown, .mouseButton = MouseButton::Right});
		frame();
		EXPECT_EQ(input().pressedDevice(), kMouse);

		// A trigger pulled all the way isn't a press.
		platform->setGamepad(2, tilted(GamepadAxis::RightTrigger, 1.0F));
		frame();
		EXPECT_EQ(input().pressedDevice(), std::nullopt);
	}

	// A device backing its share of slots is a joined player; its press doesn't hide a new one's.
	TEST_F(InputTest, PressedDeviceSkipsDevicesThatBackTheirShareOfSlots) {
		start();
		platform->connectGamepad(pad(0, "0300"));
		platform->connectGamepad(pad(1, "0301"));
		frame();
		input().claim(0, padId(0));

		platform->setGamepad(0, holding(GamepadButton::A));
		platform->setGamepad(1, holding(GamepadButton::A));
		frame();
		EXPECT_EQ(input().pressedDevice(), padId(1));

		// The keyboard is two players: one half claimed still joins, both claimed doesn't.
		input().claim(1, kKeyboard);
		platform->push({.type = InputEvent::Type::KeyDown, .key = Key::Enter});
		frame();
		EXPECT_EQ(input().pressedDevice(), kKeyboard);
		input().claim(2, kKeyboard);
		platform->push({.type = InputEvent::Type::KeyDown, .key = Key::Space});
		frame();
		EXPECT_EQ(input().pressedDevice(), std::nullopt);
	}

	// --- Slots ---

	TEST_F(InputTest, AClaimedSlotGivesItsDeviceUntilReleased) {
		start();
		platform->connectGamepad(pad(0, "0300"));
		frame();
		EXPECT_EQ(input().deviceForSlot(0), std::nullopt);
		EXPECT_FALSE(input().slotConnected(0));

		input().claim(0, padId(0));
		input().claim(1, kKeyboard);
		EXPECT_EQ(input().deviceForSlot(0), padId(0));
		EXPECT_TRUE(input().slotConnected(0));
		EXPECT_EQ(input().deviceForSlot(1), kKeyboard);

		input().release(0);
		EXPECT_EQ(input().deviceForSlot(0), std::nullopt);
		EXPECT_FALSE(input().slotConnected(0));
		EXPECT_EQ(input().deviceForSlot(1), kKeyboard);
	}

	TEST_F(InputTest, TheKeyboardBacksTwoSlotsAndAPadOne) {
		start();
		platform->connectGamepad(pad(0, "0300"));
		frame();

		input().claim(0, kKeyboard);
		input().claim(1, kKeyboard);
		input().claim(2, kKeyboard); // a third half of the keyboard: refused
		EXPECT_EQ(input().deviceForSlot(0), kKeyboard);
		EXPECT_EQ(input().deviceForSlot(1), kKeyboard);
		EXPECT_EQ(input().deviceForSlot(2), std::nullopt);

		input().claim(3, padId(0));
		input().claim(4, padId(0)); // the pad backs slot 3 already: refused
		EXPECT_EQ(input().deviceForSlot(3), padId(0));
		EXPECT_EQ(input().deviceForSlot(4), std::nullopt);
		input().claim(5, kMouse);
		input().claim(6, kMouse);
		EXPECT_EQ(input().deviceForSlot(5), kMouse);
		EXPECT_EQ(input().deviceForSlot(6), std::nullopt);

		// Claiming the same device for its own slot again changes nothing.
		input().claim(3, padId(0));
		EXPECT_EQ(input().deviceForSlot(3), padId(0));
	}

	TEST_F(InputTest, ClaimRefusesASlotOutOfRangeAndADeviceNotConnected) {
		start();
		input().claim(-1, kKeyboard);
		input().claim(kMaxPlayerSlots, kKeyboard);
		input().claim(0, padId(3));
		input().claim(1, {.kind = DeviceKind::Keyboard, .index = 1});
		EXPECT_EQ(input().deviceForSlot(-1), std::nullopt);
		EXPECT_EQ(input().deviceForSlot(kMaxPlayerSlots), std::nullopt);
		EXPECT_EQ(input().deviceForSlot(0), std::nullopt);
		EXPECT_EQ(input().deviceForSlot(1), std::nullopt);
		input().release(kMaxPlayerSlots); // nothing to release; no harm

		// A claim refused leaves the slot with what it had.
		input().claim(2, kMouse);
		input().claim(2, padId(3));
		EXPECT_EQ(input().deviceForSlot(2), kMouse);
	}

	TEST_F(InputTest, ClaimingAClaimedSlotReplacesItsDevice) {
		start();
		platform->connectGamepad(pad(0, "0300"));
		frame();
		input().claim(0, padId(0));
		input().claim(0, kKeyboard);
		EXPECT_EQ(input().deviceForSlot(0), kKeyboard);

		// The pad it gave up is free for another slot.
		input().claim(1, padId(0));
		EXPECT_EQ(input().deviceForSlot(1), padId(0));
	}

	TEST_F(InputTest, AClaimedPadThatDisconnectsKeepsItsSlotUntilItsModelReturns) {
		start();
		platform->connectGamepad(pad(0, "03xbox"));
		frame();
		input().claim(1, padId(0));

		// Away, the slot still names the pad's id, which reads as released.
		platform->disconnectGamepad(0);
		frame();
		EXPECT_FALSE(input().slotConnected(1));
		EXPECT_EQ(input().deviceForSlot(1), padId(0));

		// A different model takes the old joystick id and arrives unclaimed; the slot names nothing now.
		platform->connectGamepad(pad(0, "03other"));
		frame();
		EXPECT_FALSE(input().slotConnected(1));
		EXPECT_EQ(input().deviceForSlot(1), std::nullopt);

		// The same model, at another id, takes the slot back.
		platform->connectGamepad(pad(3, "03xbox"));
		frame();
		EXPECT_TRUE(input().slotConnected(1));
		EXPECT_EQ(input().deviceForSlot(1), padId(3));

		// The unclaimed one can be claimed by another slot.
		input().claim(2, padId(0));
		EXPECT_EQ(input().deviceForSlot(2), padId(0));
	}

	// A sim reading its slot's pad through an ActionMap sees the release when the pad is pulled with a
	// button and a trigger held, then nothing.
	TEST_F(InputTest, ASlotsPadPulledWhileHeldGivesTheSimItsRelease) {
		start();
		foundation::ActionMap map;
		map.bind("fire", {.source = Binding::Source::GamepadButton, .control = static_cast<std::uint16_t>(GamepadButton::A)});
		map.bind("throttle", {.source = Binding::Source::GamepadAxis, .control = static_cast<std::uint16_t>(GamepadAxis::RightTrigger)});
		std::vector<std::string> seen;
		const auto				 describe = [](const foundation::ActionState& state) {
			  return std::string(state.button.down ? "down " : "up ") + std::to_string(state.button.transitions);
		};
		hooks()[kPlay].fixedUpdate = [&](App& owner, const TickContext& tick) {
			const std::optional<DeviceId>	device = owner.input().deviceForSlot(0);
			const std::span<const DeviceId> devices = device ? std::span(&*device, 1) : std::span<const DeviceId>{};
			seen.push_back(
				describe(map.evaluate("fire", tick.input, devices)) + " / " + describe(map.evaluate("throttle", tick.input, devices))
			);
		};
		app->scenes().push<PlayScene>();
		platform->connectGamepad(pad(0, "0300"));
		frame(); // the first frame runs no tick
		input().claim(0, padId(0));
		GamepadSample held = holding(GamepadButton::A);
		held.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] = 1.0F;
		platform->setGamepad(0, held);
		frame();
		platform->disconnectGamepad(0);
		frame();
		frame();
		EXPECT_EQ(seen, (std::vector<std::string>{"down 1 / down 1", "up 1 / up 1", "up 0 / up 0"}));
	}

	TEST_F(InputTest, TwoSlotsWaitingOnOneModelTakeItsPadsLowestSlotFirst) {
		start();
		platform->connectGamepad(pad(0, "03xbox"));
		platform->connectGamepad(pad(1, "03xbox"));
		frame();
		input().claim(5, padId(0));
		input().claim(2, padId(1));
		platform->disconnectGamepad(0);
		platform->disconnectGamepad(1);
		frame();

		platform->connectGamepad(pad(7, "03xbox"));
		frame();
		EXPECT_EQ(input().deviceForSlot(2), padId(7));
		EXPECT_FALSE(input().slotConnected(5));

		platform->connectGamepad(pad(8, "03xbox"));
		frame();
		EXPECT_EQ(input().deviceForSlot(5), padId(8));
	}

	TEST_F(InputTest, ReleasingAWaitingSlotLetsItsPadReturnUnclaimed) {
		start();
		platform->connectGamepad(pad(0, "03xbox"));
		frame();
		input().claim(0, padId(0));
		platform->disconnectGamepad(0);
		frame();
		input().release(0);

		platform->connectGamepad(pad(0, "03xbox"));
		frame();
		EXPECT_FALSE(input().slotConnected(0));
		EXPECT_EQ(input().deviceForSlot(0), std::nullopt);
	}

	TEST_F(InputTest, APadWithNoModelGuidIsTakenBackByNothing) {
		start();
		platform->connectGamepad(pad(0, ""));
		frame();
		input().claim(0, padId(0));
		platform->disconnectGamepad(0);
		frame();
		platform->connectGamepad(pad(0, ""));
		frame();
		EXPECT_FALSE(input().slotConnected(0));
	}

	// A slot's pad reads through an ActionMap from a fixed tick, as a sim builds its actions.
	TEST_F(InputTest, ATickReadsTheSlotsPadThroughAnActionMap) {
		start();
		foundation::ActionMap map;
		map.bind("fire", {.source = Binding::Source::GamepadButton, .control = static_cast<std::uint16_t>(GamepadButton::RightBumper)});
		std::vector<bool> fired;
		hooks()[kPlay].fixedUpdate = [&](App& owner, const TickContext& tick) {
			const std::optional<DeviceId> device = owner.input().deviceForSlot(0);
			fired.push_back(device && map.evaluate("fire", tick.input, std::span(&*device, 1)).button.pressed());
		};
		app->scenes().push<PlayScene>();
		platform->connectGamepad(pad(6, "0306"));
		frame(); // the first frame runs no tick
		input().claim(0, padId(6));
		frame();

		platform->setGamepad(6, holding(GamepadButton::RightBumper));
		frame();
		frame();
		EXPECT_EQ(fired, (std::vector<bool>{false, true, false}));
	}

	// --- Key names ---

	TEST_F(InputTest, KeyNamesFallBackToAFixedEnglishTable) {
		start();
		const std::vector<std::pair<Key, std::string>> expected{
			{Key::A, "A"},
			{Key::Num7, "7"},
			{Key::Slash, "/"},
			{Key::Backslash, "\\"},
			{Key::World1, "World 1"},
			{Key::Space, "Space"},
			{Key::Escape, "Escape"},
			{Key::PageDown, "Page Down"},
			{Key::PrintScreen, "Print Screen"},
			{Key::Up, "Up"},
			{Key::F1, "F1"},
			{Key::F25, "F25"},
			{Key::Kp0, "Keypad 0"},
			{Key::KpDecimal, "Keypad ."},
			{Key::KpEnter, "Keypad Enter"},
			{Key::LeftShift, "Left Shift"},
			{Key::RightSuper, "Right Super"},
			{Key::Menu, "Menu"},
			{Key::Unknown, "Unknown"},
			{Key::Count, "Unknown"},
		};
		for (const auto& [key, name] : expected) {
			EXPECT_EQ(input().keyName(key), name) << "Key " << static_cast<int>(key);
		}
	}

	// The layout's label wins for the main block, small letters capitalised; Space and the keypad keep
	// their names, and an empty label falls back.
	TEST_F(InputTest, TheLayoutsLabelNamesTheMainBlock) {
		start();
		platform->setKeyLabel(Key::Q, "a");				   // AZERTY
		platform->setKeyLabel(Key::Semicolon, "\xC3\xB6"); // German o umlaut, U+00F6
		platform->setKeyLabel(Key::Num2, "\xC3\xA9");	   // AZERTY e acute, U+00E9
		platform->setKeyLabel(Key::A, "\xD1\x84");		   // Cyrillic ef, U+0444
		platform->setKeyLabel(Key::W, "\xCF\x82");		   // Greek final sigma, U+03C2
		platform->setKeyLabel(Key::Equal, "\xC3\xB7");	   // division sign, U+00F7, no capital
		platform->setKeyLabel(Key::Kp1, "1");
		platform->setKeyLabel(Key::Space, " ");
		platform->setKeyLabel(Key::Minus, "");
		EXPECT_EQ(input().keyName(Key::Q), "A");
		EXPECT_EQ(input().keyName(Key::Semicolon), "\xC3\x96"); // U+00D6
		EXPECT_EQ(input().keyName(Key::Num2), "\xC3\x89");		// U+00C9
		EXPECT_EQ(input().keyName(Key::A), "\xD0\xA4");			// U+0424
		EXPECT_EQ(input().keyName(Key::W), "\xCE\xA3");			// U+03A3
		EXPECT_EQ(input().keyName(Key::Equal), "\xC3\xB7");
		EXPECT_EQ(input().keyName(Key::Kp1), "Keypad 1");
		EXPECT_EQ(input().keyName(Key::Space), "Space");
		EXPECT_EQ(input().keyName(Key::Minus), "-");
		EXPECT_EQ(input().keyName(Key::E), "E");
	}

	// --- Capture ---

	TEST_F(InputTest, CaptureTakesEachPressThisFrameOnceInOrder) {
		start();
		platform->connectGamepad(pad(1, "0301"));
		frame();
		EXPECT_EQ(input().captureNext(), std::nullopt);

		platform->push({.type = InputEvent::Type::KeyDown, .key = Key::Enter});
		platform->push({.type = InputEvent::Type::KeyDown, .repeat = true, .key = Key::Enter});
		platform->push({.type = InputEvent::Type::PointerDown, .mouseButton = MouseButton::Middle});
		platform->setGamepad(1, holding(GamepadButton::Y));
		frame();
		expectBinding(input().captureNext(), Binding::Source::Key, static_cast<std::uint16_t>(Key::Enter));
		expectBinding(input().captureNext(), Binding::Source::MouseButton, static_cast<std::uint16_t>(MouseButton::Middle));
		expectBinding(input().captureNext(), Binding::Source::GamepadButton, static_cast<std::uint16_t>(GamepadButton::Y));
		EXPECT_EQ(input().captureNext(), std::nullopt);
		EXPECT_EQ(input().captureNext(), std::nullopt);

		// The next frame starts over with its own events; a held button isn't pressed again.
		frame();
		EXPECT_EQ(input().captureNext(), std::nullopt);
	}

	// An axis is captured as it crosses half travel away from rest, binding the half it went to.
	TEST_F(InputTest, CaptureTakesAStickOrTriggerCrossingHalfTravel) {
		start();
		platform->connectGamepad(pad(0, "0300"));
		frame();

		platform->setGamepad(0, tilted(GamepadAxis::LeftX, 0.375F));
		frame();
		EXPECT_EQ(input().captureNext(), std::nullopt); // not far enough

		platform->setGamepad(0, tilted(GamepadAxis::LeftX, 0.75F));
		frame();
		expectBinding(input().captureNext(), Binding::Source::GamepadAxis, static_cast<std::uint16_t>(GamepadAxis::LeftX));

		// Held there, nothing more; flicked through to the other side, its negative half.
		frame();
		EXPECT_EQ(input().captureNext(), std::nullopt);
		platform->setGamepad(0, tilted(GamepadAxis::LeftX, -0.75F));
		frame();
		expectBinding(input().captureNext(), Binding::Source::GamepadAxis, static_cast<std::uint16_t>(GamepadAxis::LeftX), -1.0F);

		// Coming back toward rest isn't a press.
		platform->setGamepad(0, tilted(GamepadAxis::LeftX, -0.25F));
		frame();
		EXPECT_EQ(input().captureNext(), std::nullopt);

		platform->setGamepad(0, tilted(GamepadAxis::RightTrigger, 0.625F));
		frame();
		expectBinding(input().captureNext(), Binding::Source::GamepadAxis, static_cast<std::uint16_t>(GamepadAxis::RightTrigger));
	}

	// --- Axis edges ---

	// A trigger pulled past its threshold in a frame that runs two ticks: the first tick and the frame
	// see the press, the second tick holds it; let go in a frame with no tick, the next tick still sees
	// the release.
	TEST_F(InputTest, AnAxisEdgeReachesTheFirstTickAndTheFrameOnce) {
		start();
		foundation::ActionMap map;
		map.bind("fire", {.source = Binding::Source::GamepadAxis, .control = static_cast<std::uint16_t>(GamepadAxis::RightTrigger)});
		constexpr std::array	 kPad{padId(0)};
		std::vector<std::string> seen;
		const auto				 describe = [](const foundation::ActionState& state) {
			  return std::string(state.button.pressed() ? "pressed" : "") + (state.button.released() ? "released" : "") +
					 (state.button.down ? " down" : " up");
		};
		hooks()[kPlay].fixedUpdate = [&](App& /*owner*/, const TickContext& tick) {
			seen.push_back("tick " + describe(map.evaluate("fire", tick.input, kPad)));
		};
		hooks()[kPlay].update = [&](App& /*owner*/, const FrameContext& frameContext) {
			seen.push_back("frame " + describe(map.evaluate("fire", frameContext.input, kPad)));
		};
		app->scenes().push<PlayScene>();
		platform->connectGamepad(pad(0, "0300"));
		frame();
		seen.clear();

		platform->setGamepad(0, tilted(GamepadAxis::RightTrigger, 0.75F));
		frame(2 * kStep);
		EXPECT_EQ(seen, (std::vector<std::string>{"tick pressed down", "tick  down", "frame pressed down"}));
		seen.clear();

		platform->setGamepad(0, {});
		frame(kStep / 2);
		frame(kStep / 2);
		EXPECT_EQ(seen, (std::vector<std::string>{"frame released up", "tick released up", "frame  up"}));
	}

} // namespace app::testing
