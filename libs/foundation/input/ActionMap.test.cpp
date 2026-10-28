#include "ActionMap.h"
#include "InputAccumulator.h"

#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

using namespace foundation;

namespace {

	constexpr DeviceId kKeyboard{.kind = DeviceKind::Keyboard};
	constexpr DeviceId kMouse{.kind = DeviceKind::Mouse};
	constexpr DeviceId kPad0{.kind = DeviceKind::Gamepad, .index = 0};
	constexpr DeviceId kPad1{.kind = DeviceKind::Gamepad, .index = 1};

	constexpr std::array kKeyboardOnly{kKeyboard};
	constexpr std::array kPad0Only{kPad0};

	Binding key(Key control, float scale = 1.0F) {
		return {.source = Binding::Source::Key, .control = static_cast<std::uint16_t>(control), .scale = scale};
	}

	Binding padButton(GamepadButton control) {
		return {.source = Binding::Source::GamepadButton, .control = static_cast<std::uint16_t>(control)};
	}

	Binding padAxis(GamepadAxis control, float scale = 1.0F, float deadzone = 0.0F) {
		return {
			.source = Binding::Source::GamepadAxis, .control = static_cast<std::uint16_t>(control), .scale = scale, .deadzone = deadzone
		};
	}

	ButtonState& keyOf(InputSnapshot& snapshot, Key control) {
		return snapshot.keys[static_cast<std::size_t>(control)];
	}

	void setAxis(InputSnapshot& snapshot, std::uint8_t pad, GamepadAxis axis, float value) {
		snapshot.pads[pad].connected = true;
		snapshot.pads[pad].axes[static_cast<std::size_t>(axis)] = value;
	}

	// Where the axis was at the reader's last read.
	void setStartAxis(InputSnapshot& snapshot, std::uint8_t pad, GamepadAxis axis, float value) {
		snapshot.pads[pad].startAxes[static_cast<std::size_t>(axis)] = value;
	}

	// At value since the reader's last read.
	void holdAxis(InputSnapshot& snapshot, std::uint8_t pad, GamepadAxis axis, float value) {
		setAxis(snapshot, pad, axis, value);
		setStartAxis(snapshot, pad, axis, value);
	}

	void setStick(InputSnapshot& snapshot, float x, float y) {
		setAxis(snapshot, 0, GamepadAxis::LeftX, x);
		setAxis(snapshot, 0, GamepadAxis::LeftY, y);
	}

	void expectButton(const ActionState& state, bool down, int transitions) {
		EXPECT_EQ(state.button.down, down);
		EXPECT_EQ(int{state.button.transitions}, transitions);
	}

	TEST(ActionMap, AxisReadAsAButtonIsDownPastTheThreshold) {
		ActionMap map;
		map.bind("fire", padAxis(GamepadAxis::RightTrigger));
		InputSnapshot snapshot;

		setAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.5F);
		ActionState fire = map.evaluate("fire", snapshot, kPad0Only);
		expectButton(fire, false, 0);
		EXPECT_EQ(fire.value, 0.5F);

		holdAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.625F);
		fire = map.evaluate("fire", snapshot, kPad0Only);
		expectButton(fire, true, 0);
		EXPECT_EQ(fire.value, 0.625F);
	}

	TEST(ActionMap, NegativeScaleReadsTheNegativeHalfOfAnAxisAsAButton) {
		ActionMap map;
		map.bind("menuLeft", padAxis(GamepadAxis::LeftX, -1.0F));
		InputSnapshot snapshot;

		holdAxis(snapshot, 0, GamepadAxis::LeftX, -0.75F);
		ActionState left = map.evaluate("menuLeft", snapshot, kPad0Only);
		expectButton(left, true, 0);
		EXPECT_EQ(left.value, 0.75F);

		holdAxis(snapshot, 0, GamepadAxis::LeftX, 0.75F);
		left = map.evaluate("menuLeft", snapshot, kPad0Only);
		expectButton(left, false, 0);
		EXPECT_EQ(left.value, -0.75F);
	}

	TEST(ActionMap, AnAxisCrossingItsThresholdSinceTheLastReadIsAnEdge) {
		ActionMap map;
		map.bind("fire", padAxis(GamepadAxis::RightTrigger));
		InputSnapshot snapshot;

		setStartAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.25F);
		setAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.75F);
		const ActionState pulled = map.evaluate("fire", snapshot, kPad0Only);
		expectButton(pulled, true, 1);
		EXPECT_TRUE(pulled.button.pressed());
		EXPECT_FALSE(pulled.button.released());

		setStartAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.75F);
		setAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.5F);
		const ActionState letGo = map.evaluate("fire", snapshot, kPad0Only);
		expectButton(letGo, false, 1);
		EXPECT_FALSE(letGo.button.pressed());
		EXPECT_TRUE(letGo.button.released());

		// Moving on one side of the threshold is no edge.
		setStartAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.0F);
		setAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.5F);
		expectButton(map.evaluate("fire", snapshot, kPad0Only), false, 0);
		setStartAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.625F);
		setAxis(snapshot, 0, GamepadAxis::RightTrigger, 1.0F);
		expectButton(map.evaluate("fire", snapshot, kPad0Only), true, 0);
	}

	// The start of the window reads through the same scale, deadzones and threshold as the end.
	TEST(ActionMap, AxisEdgesReadTheStartThroughTheSameBinding) {
		ActionMap map;
		map.bind(
			"menuUp",
			{.source = Binding::Source::GamepadAxis,
			 .control = static_cast<std::uint16_t>(GamepadAxis::LeftY),
			 .scale = -1.0F,
			 .threshold = 0.25F,
			 .deadzone = 0.5F}
		);
		map.bind("aimX", padAxis(GamepadAxis::RightX));
		map.setStickDeadzone(GamepadAxis::RightX, GamepadAxis::RightY, 0.25F);
		InputSnapshot snapshot;

		// -0.5 is at the per-axis deadzone, 0, and -0.75 past it, 0.5, past the threshold.
		setStartAxis(snapshot, 0, GamepadAxis::LeftY, -0.5F);
		setAxis(snapshot, 0, GamepadAxis::LeftY, -0.75F);
		expectButton(map.evaluate("menuUp", snapshot, kPad0Only), true, 1);
		// The positive half reads under the threshold at both ends.
		setAxis(snapshot, 0, GamepadAxis::LeftY, 0.75F);
		expectButton(map.evaluate("menuUp", snapshot, kPad0Only), false, 0);

		// The radial deadzone reads both of the start's axes: (0.5, 0.5) gives X 0.43 (magnitude 0.71
		// rescaled to 0.61), under the threshold, and the end's (0.75, 0) gives 0.67, past it.
		setStartAxis(snapshot, 0, GamepadAxis::RightX, 0.5F);
		setStartAxis(snapshot, 0, GamepadAxis::RightY, 0.5F);
		setAxis(snapshot, 0, GamepadAxis::RightX, 0.75F);
		setAxis(snapshot, 0, GamepadAxis::RightY, 0.0F);
		expectButton(map.evaluate("aimX", snapshot, kPad0Only), true, 1);
		// (0.6, 0.8) has magnitude 1 and gives X 0.6, past it, so the stick was held; the start's X beside
		// the end's Y would give 0.47.
		setStartAxis(snapshot, 0, GamepadAxis::RightX, 0.6F);
		setStartAxis(snapshot, 0, GamepadAxis::RightY, 0.8F);
		expectButton(map.evaluate("aimX", snapshot, kPad0Only), true, 0);
	}

	// Through the accumulator: each reader sees the crossing once, measured from its own last read.
	TEST(ActionMap, AxisEdgesFollowEachReader) {
		ActionMap map;
		map.bind("fire", padAxis(GamepadAxis::LeftTrigger));
		InputAccumulator input;
		input.push({.type = InputEvent::Type::DeviceConnected, .device = kPad0});
		input.consumeTick();
		input.consumeFrame();

		// A frame with two ticks: the first tick and the frame see the pull, the second tick holds.
		input.push({.type = InputEvent::Type::Axis, .device = kPad0, .axis = GamepadAxis::LeftTrigger, .value = 0.75F});
		const ActionState frame = map.evaluate("fire", input.consumeFrame(), kPad0Only);
		const ActionState firstTick = map.evaluate("fire", input.consumeTick(), kPad0Only);
		const ActionState secondTick = map.evaluate("fire", input.consumeTick(), kPad0Only);
		expectButton(frame, true, 1);
		expectButton(firstTick, true, 1);
		expectButton(secondTick, true, 0);

		// A frame with no tick: the frame sees the let-go, and the next frame's tick still does.
		input.push({.type = InputEvent::Type::Axis, .device = kPad0, .axis = GamepadAxis::LeftTrigger, .value = 0.0F});
		expectButton(map.evaluate("fire", input.consumeFrame(), kPad0Only), false, 1);
		expectButton(map.evaluate("fire", input.consumeFrame(), kPad0Only), false, 0);
		expectButton(map.evaluate("fire", input.consumeTick(), kPad0Only), false, 1);

		// A pull and let-go in two frames with no tick between come back to where the tick last read, so
		// that tick sees neither: the snapshot keeps an axis's ends, not its path.
		input.push({.type = InputEvent::Type::Axis, .device = kPad0, .axis = GamepadAxis::LeftTrigger, .value = 1.0F});
		expectButton(map.evaluate("fire", input.consumeFrame(), kPad0Only), true, 1);
		input.push({.type = InputEvent::Type::Axis, .device = kPad0, .axis = GamepadAxis::LeftTrigger, .value = 0.0F});
		expectButton(map.evaluate("fire", input.consumeFrame(), kPad0Only), false, 1);
		expectButton(map.evaluate("fire", input.consumeTick(), kPad0Only), false, 0);

		// A disconnect zeroes the axis, which lets go of a held trigger.
		input.push({.type = InputEvent::Type::Axis, .device = kPad0, .axis = GamepadAxis::LeftTrigger, .value = 1.0F});
		input.consumeTick();
		input.push({.type = InputEvent::Type::DeviceDisconnected, .device = kPad0});
		expectButton(map.evaluate("fire", input.consumeTick(), kPad0Only), false, 1);
	}

	TEST(ActionMap, TwoKeysToOneAxis) {
		ActionMap map;
		map.bind("moveX", key(Key::A, -1.0F));
		map.bind("moveX", key(Key::D));
		InputSnapshot snapshot;

		EXPECT_EQ(map.evaluate("moveX", snapshot, kKeyboardOnly).value, 0.0F);
		keyOf(snapshot, Key::A).down = true;
		EXPECT_EQ(map.evaluate("moveX", snapshot, kKeyboardOnly).value, -1.0F);
		keyOf(snapshot, Key::D).down = true;
		const ActionState both = map.evaluate("moveX", snapshot, kKeyboardOnly);
		EXPECT_EQ(both.value, 0.0F);
		EXPECT_TRUE(both.button.down);
		keyOf(snapshot, Key::A).down = false;
		EXPECT_EQ(map.evaluate("moveX", snapshot, kKeyboardOnly).value, 1.0F);
	}

	TEST(ActionMap, RadialStickDeadzone) {
		ActionMap map;
		map.bind("moveX", padAxis(GamepadAxis::LeftX));
		map.bind("moveY", padAxis(GamepadAxis::LeftY));
		map.setStickDeadzone(GamepadAxis::LeftX, GamepadAxis::LeftY, 0.25F);
		InputSnapshot snapshot;

		setStick(snapshot, 0.0F, 0.25F);
		EXPECT_EQ(map.evaluate("moveY", snapshot, kPad0Only).value, 0.0F);
		setStick(snapshot, 0.125F, 0.125F);
		EXPECT_EQ(map.evaluate("moveX", snapshot, kPad0Only).value, 0.0F);
		EXPECT_EQ(map.evaluate("moveY", snapshot, kPad0Only).value, 0.0F);

		// Magnitude 0.625 rescales to 0.5 along the same direction.
		setStick(snapshot, 0.375F, 0.5F);
		EXPECT_FLOAT_EQ(map.evaluate("moveX", snapshot, kPad0Only).value, 0.3F);
		EXPECT_FLOAT_EQ(map.evaluate("moveY", snapshot, kPad0Only).value, 0.4F);

		setStick(snapshot, 0.0F, -1.0F);
		EXPECT_EQ(map.evaluate("moveX", snapshot, kPad0Only).value, 0.0F);
		EXPECT_EQ(map.evaluate("moveY", snapshot, kPad0Only).value, -1.0F);

		// A square gate's corner is past full tilt; the magnitude clamps to 1.
		setStick(snapshot, 1.0F, 1.0F);
		EXPECT_FLOAT_EQ(map.evaluate("moveX", snapshot, kPad0Only).value, std::sqrt(0.5F));
		EXPECT_FLOAT_EQ(map.evaluate("moveY", snapshot, kPad0Only).value, std::sqrt(0.5F));

		map.setStickDeadzone(GamepadAxis::LeftX, GamepadAxis::LeftY, 0.5F);
		setStick(snapshot, 0.0F, 0.75F);
		EXPECT_EQ(map.evaluate("moveY", snapshot, kPad0Only).value, 0.5F);
	}

	TEST(ActionMap, PerAxisDeadzone) {
		ActionMap map;
		map.bind("throttle", padAxis(GamepadAxis::RightTrigger, 1.0F, 0.25F));
		InputSnapshot snapshot;

		setAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.25F);
		EXPECT_EQ(map.evaluate("throttle", snapshot, kPad0Only).value, 0.0F);
		setAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.625F);
		EXPECT_EQ(map.evaluate("throttle", snapshot, kPad0Only).value, 0.5F);
		setAxis(snapshot, 0, GamepadAxis::RightTrigger, -0.625F);
		EXPECT_EQ(map.evaluate("throttle", snapshot, kPad0Only).value, -0.5F);
		setAxis(snapshot, 0, GamepadAxis::RightTrigger, 1.0F);
		EXPECT_EQ(map.evaluate("throttle", snapshot, kPad0Only).value, 1.0F);
	}

	TEST(ActionMap, PerAxisDeadzoneIsPerAxisAndAppliesAfterTheRadialOne) {
		ActionMap map;
		map.bind("perAxisX", padAxis(GamepadAxis::LeftX, 1.0F, 0.5F));
		map.bind("radialX", padAxis(GamepadAxis::RightX));
		map.bind("bothY", padAxis(GamepadAxis::RightY, 1.0F, 0.25F));
		map.setStickDeadzone(GamepadAxis::RightX, GamepadAxis::RightY, 0.25F);
		InputSnapshot snapshot;

		setAxis(snapshot, 0, GamepadAxis::LeftX, 0.375F);
		setAxis(snapshot, 0, GamepadAxis::LeftY, 0.5F);
		setAxis(snapshot, 0, GamepadAxis::RightX, 0.375F);
		setAxis(snapshot, 0, GamepadAxis::RightY, 0.5F);
		EXPECT_EQ(map.evaluate("perAxisX", snapshot, kPad0Only).value, 0.0F);
		EXPECT_FLOAT_EQ(map.evaluate("radialX", snapshot, kPad0Only).value, 0.3F);
		// Radial gives 0.4, then (0.4 - 0.25) / 0.75.
		EXPECT_NEAR(map.evaluate("bothY", snapshot, kPad0Only).value, 0.2F, 1e-6F);
	}

	TEST(ActionMap, SplitKeyboardIsTwoMapsOverOneKeyboard) {
		ActionMap left;
		left.bind("moveX", key(Key::A, -1.0F));
		left.bind("moveX", key(Key::D));
		left.bind("fire", key(Key::Space));
		ActionMap right;
		right.bind("moveX", key(Key::Left, -1.0F));
		right.bind("moveX", key(Key::Right));
		right.bind("fire", key(Key::Enter));

		InputSnapshot snapshot;
		keyOf(snapshot, Key::D).down = true;
		keyOf(snapshot, Key::Left).down = true;
		keyOf(snapshot, Key::Enter) = {.down = true, .transitions = 1};

		EXPECT_EQ(left.evaluate("moveX", snapshot, kKeyboardOnly).value, 1.0F);
		EXPECT_EQ(right.evaluate("moveX", snapshot, kKeyboardOnly).value, -1.0F);
		expectButton(left.evaluate("fire", snapshot, kKeyboardOnly), false, 0);
		expectButton(right.evaluate("fire", snapshot, kKeyboardOnly), true, 1);
	}

	TEST(ActionMap, CoOpIsOneMapPerPad) {
		ActionMap map;
		map.bind("moveX", padAxis(GamepadAxis::LeftX));
		InputSnapshot snapshot;
		setAxis(snapshot, 0, GamepadAxis::LeftX, 0.5F);
		setAxis(snapshot, 1, GamepadAxis::LeftX, -0.25F);

		EXPECT_EQ(map.evaluate("moveX", snapshot, kPad0Only).value, 0.5F);
		EXPECT_EQ(map.evaluate("moveX", snapshot, std::array{kPad1}).value, -0.25F);
	}

	TEST(ActionMap, NothingBoundOrNoDevicesIsZero) {
		ActionMap map;
		map.bind("jump", key(Key::Space));
		InputSnapshot snapshot;
		keyOf(snapshot, Key::Space) = {.down = true, .transitions = 1};

		const ActionState unbound = map.evaluate("crouch", snapshot, kKeyboardOnly);
		expectButton(unbound, false, 0);
		EXPECT_EQ(unbound.value, 0.0F);
		const ActionState noDevices = map.evaluate("jump", snapshot, {});
		expectButton(noDevices, false, 0);
		EXPECT_EQ(noDevices.value, 0.0F);
	}

	TEST(ActionMap, BindingsReadOnlyTheirDeviceKind) {
		ActionMap map;
		map.bind("jump", key(Key::Space));
		map.bind("jump", padButton(GamepadButton::A));
		map.bind("jump", {.source = Binding::Source::MouseButton, .control = static_cast<std::uint16_t>(MouseButton::Right)});
		InputSnapshot snapshot;
		keyOf(snapshot, Key::Space).down = true;

		EXPECT_TRUE(map.evaluate("jump", snapshot, kKeyboardOnly).button.down);
		EXPECT_FALSE(map.evaluate("jump", snapshot, kPad0Only).button.down);
		EXPECT_FALSE(map.evaluate("jump", snapshot, std::array{kMouse}).button.down);

		snapshot.mouse[static_cast<std::size_t>(MouseButton::Right)].down = true;
		EXPECT_TRUE(map.evaluate("jump", snapshot, std::array{kMouse}).button.down);
	}

	TEST(ActionMap, ControlsOutOfRangeContributeNothing) {
		ActionMap map;
		map.bind("broken", {.source = Binding::Source::Key, .control = 999});
		map.bind("broken", {.source = Binding::Source::GamepadAxis, .control = 6});
		InputSnapshot snapshot;
		setAxis(snapshot, 0, GamepadAxis::LeftX, 1.0F);

		const ActionState broken = map.evaluate("broken", snapshot, std::array{kKeyboard, kPad0});
		expectButton(broken, false, 0);
		EXPECT_EQ(broken.value, 0.0F);
	}

	TEST(ActionMap, ValuesSumWithinADeviceAndTheLargestWinsAcrossDevices) {
		ActionMap map;
		map.bind("moveX", key(Key::D));
		map.bind("moveX", key(Key::Right));
		map.bind("moveX", padAxis(GamepadAxis::LeftX));
		InputSnapshot				  snapshot;
		const std::array<DeviceId, 3> everything{kKeyboard, kPad0, kPad1};

		// Two keys on one device sum, clamped.
		keyOf(snapshot, Key::D).down = true;
		keyOf(snapshot, Key::Right).down = true;
		EXPECT_EQ(map.evaluate("moveX", snapshot, kKeyboardOnly).value, 1.0F);

		// Two pads pushing the same way don't add up.
		keyOf(snapshot, Key::D).down = false;
		keyOf(snapshot, Key::Right).down = false;
		setAxis(snapshot, 0, GamepadAxis::LeftX, 0.5F);
		setAxis(snapshot, 1, GamepadAxis::LeftX, 0.75F);
		EXPECT_EQ(map.evaluate("moveX", snapshot, everything).value, 0.75F);

		setAxis(snapshot, 0, GamepadAxis::LeftX, -0.75F);
		setAxis(snapshot, 1, GamepadAxis::LeftX, 0.5F);
		EXPECT_EQ(map.evaluate("moveX", snapshot, everything).value, -0.75F);

		// A key and a stick on different devices: the larger, not their sum.
		keyOf(snapshot, Key::D).down = true;
		EXPECT_EQ(map.evaluate("moveX", snapshot, everything).value, 1.0F);

		// A tie goes to the first device in the span.
		keyOf(snapshot, Key::D).down = false;
		setAxis(snapshot, 0, GamepadAxis::LeftX, 0.5F);
		setAxis(snapshot, 1, GamepadAxis::LeftX, -0.5F);
		EXPECT_EQ(map.evaluate("moveX", snapshot, everything).value, 0.5F);
		EXPECT_EQ(map.evaluate("moveX", snapshot, std::array{kPad1, kPad0}).value, -0.5F);
	}

	TEST(ActionMap, ButtonTransitionsCombineAcrossBindings) {
		ActionMap map;
		map.bind("jump", key(Key::Space));
		map.bind("jump", key(Key::Enter));
		map.bind("jump", padButton(GamepadButton::A));
		const std::array<DeviceId, 2> devices{kKeyboard, kPad0};

		const auto evaluate = [&](ButtonState space, ButtonState enter, ButtonState padA) {
			InputSnapshot snapshot;
			keyOf(snapshot, Key::Space) = space;
			keyOf(snapshot, Key::Enter) = enter;
			snapshot.pads[0].connected = true;
			snapshot.pads[0].buttons[static_cast<std::size_t>(GamepadButton::A)] = padA;
			return map.evaluate("jump", snapshot, devices);
		};
		const ButtonState up{};
		const ButtonState held{.down = true};
		const ButtonState pressed{.down = true, .transitions = 1};
		const ButtonState released{.down = false, .transitions = 1};
		const ButtonState tapped{.down = false, .transitions = 2};

		expectButton(evaluate(tapped, up, up), false, 2);
		// Held on the pad the whole time: tapping or letting go of a key is no edge.
		expectButton(evaluate(tapped, up, held), true, 0);
		expectButton(evaluate(released, up, held), true, 0);
		// Two keys going down together are one press, and coming up together one release.
		expectButton(evaluate(pressed, pressed, up), true, 1);
		expectButton(evaluate(released, released, up), false, 1);
		// A hand-off from one key to the other may have dipped in between.
		expectButton(evaluate(released, pressed, up), true, 2);
	}

	// Of the bindings that start down only the last to let go can release the action, and of those that
	// end down only the first to make its last press can press it, so the sum counts too many.
	TEST(ActionMap, ButtonTransitionsAreTheMostAnOrderGives) {
		ActionMap map;
		map.bind("fire", key(Key::A));
		map.bind("fire", padAxis(GamepadAxis::LeftTrigger));
		map.bind("fire", padAxis(GamepadAxis::RightTrigger));
		const std::array<DeviceId, 2> devices{kKeyboard, kPad0};
		InputSnapshot				  snapshot;

		// A let go and pressed again while both triggers are let go: orders give 0 or 2, never 4.
		keyOf(snapshot, Key::A) = {.down = true, .transitions = 2};
		setStartAxis(snapshot, 0, GamepadAxis::LeftTrigger, 0.75F);
		setAxis(snapshot, 0, GamepadAxis::LeftTrigger, 0.25F);
		setStartAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.75F);
		setAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.25F);
		expectButton(map.evaluate("fire", snapshot, devices), true, 2);

		// Two keys each let go and pressed again: 2, not 4.
		ActionMap twoKeys;
		twoKeys.bind("fire", key(Key::A));
		twoKeys.bind("fire", key(Key::B));
		keyOf(snapshot, Key::B) = {.down = true, .transitions = 2};
		expectButton(twoKeys.evaluate("fire", snapshot, kKeyboardOnly), true, 2);

		// Three keys tapped from up: each tap can fall in a gap the others leave, so 6.
		ActionMap threeKeys;
		threeKeys.bind("fire", key(Key::A));
		threeKeys.bind("fire", key(Key::B));
		threeKeys.bind("fire", key(Key::C));
		for (const Key tapped : {Key::A, Key::B, Key::C}) {
			keyOf(snapshot, tapped) = {.down = false, .transitions = 2};
		}
		expectButton(threeKeys.evaluate("fire", snapshot, kKeyboardOnly), false, 6);
	}

	TEST(ActionMap, TransitionsFollowTheReaderTheSnapshotCameFrom) {
		ActionMap map;
		map.bind("jump", key(Key::Space));
		InputAccumulator input;
		input.push({.type = InputEvent::Type::KeyDown, .key = Key::Space});

		const InputSnapshot firstTick = input.consumeTick();
		const InputSnapshot secondTick = input.consumeTick();
		const InputSnapshot frame = input.consumeFrame();
		EXPECT_TRUE(map.evaluate("jump", firstTick, kKeyboardOnly).button.pressed());
		expectButton(map.evaluate("jump", secondTick, kKeyboardOnly), true, 0);
		EXPECT_TRUE(map.evaluate("jump", frame, kKeyboardOnly).button.pressed());
	}

	TEST(ActionMap, ThresholdIsTheBindings) {
		ActionMap map;
		map.bind(
			"fire",
			{.source = Binding::Source::GamepadAxis, .control = static_cast<std::uint16_t>(GamepadAxis::RightTrigger), .threshold = 0.25F}
		);
		map.bind(
			"menuLeft",
			{.source = Binding::Source::GamepadAxis,
			 .control = static_cast<std::uint16_t>(GamepadAxis::LeftX),
			 .scale = -1.0F,
			 .threshold = 0.25F}
		);
		InputSnapshot snapshot;

		setAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.25F);
		EXPECT_FALSE(map.evaluate("fire", snapshot, kPad0Only).button.down);
		setAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.375F);
		EXPECT_TRUE(map.evaluate("fire", snapshot, kPad0Only).button.down);

		setAxis(snapshot, 0, GamepadAxis::LeftX, -0.25F);
		EXPECT_FALSE(map.evaluate("menuLeft", snapshot, kPad0Only).button.down);
		setAxis(snapshot, 0, GamepadAxis::LeftX, -0.375F);
		const ActionState left = map.evaluate("menuLeft", snapshot, kPad0Only);
		EXPECT_TRUE(left.button.down);
		EXPECT_EQ(left.value, 0.375F);
		setAxis(snapshot, 0, GamepadAxis::LeftX, 0.375F);
		EXPECT_FALSE(map.evaluate("menuLeft", snapshot, kPad0Only).button.down);
	}

	// Pads are sampled once a frame, so three players letting go of A land in one poll. The action
	// came up once; three transitions would read as a press too.
	TEST(ActionMap, ThreePadsMovingTogetherMoveTheActionOnce) {
		ActionMap map;
		map.bind("confirm", padButton(GamepadButton::A));
		const std::array<DeviceId, 3> pads{kPad0, kPad1, DeviceId{.kind = DeviceKind::Gamepad, .index = 2}};
		InputAccumulator			  input;
		for (const DeviceId& pad : pads) {
			input.push({.type = InputEvent::Type::DeviceConnected, .device = pad});
		}
		input.consumeTick();

		for (const DeviceId& pad : pads) {
			input.push({.type = InputEvent::Type::ButtonDown, .device = pad, .button = GamepadButton::A});
		}
		const ActionState pressed = map.evaluate("confirm", input.consumeTick(), pads);
		expectButton(pressed, true, 1);
		EXPECT_TRUE(pressed.button.pressed());
		EXPECT_FALSE(pressed.button.released());

		for (const DeviceId& pad : pads) {
			input.push({.type = InputEvent::Type::ButtonUp, .device = pad, .button = GamepadButton::A});
		}
		const ActionState released = map.evaluate("confirm", input.consumeTick(), pads);
		expectButton(released, false, 1);
		EXPECT_FALSE(released.button.pressed());
		EXPECT_TRUE(released.button.released());
	}

	// An axis past its threshold at both ends of the window held the action down throughout, as a held
	// button does, so a key's press, tap or release under it is no edge.
	TEST(ActionMap, AnAxisHeldThroughoutHoldsTheActionAsAButtonWould) {
		ActionMap map;
		map.bind("jump", key(Key::Space));
		map.bind("jump", padAxis(GamepadAxis::LeftY, -1.0F));
		const std::array<DeviceId, 2> devices{kKeyboard, kPad0};
		InputSnapshot				  snapshot;
		holdAxis(snapshot, 0, GamepadAxis::LeftY, -0.75F);
		expectButton(map.evaluate("jump", snapshot, devices), true, 0);

		keyOf(snapshot, Key::Space) = {.down = true, .transitions = 1};
		expectButton(map.evaluate("jump", snapshot, devices), true, 0);
		keyOf(snapshot, Key::Space) = {.down = false, .transitions = 2};
		expectButton(map.evaluate("jump", snapshot, devices), true, 0);
		keyOf(snapshot, Key::Space) = {.down = false, .transitions = 1};
		expectButton(map.evaluate("jump", snapshot, devices), true, 0);
	}

	TEST(ActionMap, AnAxisEdgeFoldsWithAButtonsAsAnotherButtonWould) {
		ActionMap map;
		map.bind("jump", key(Key::Space));
		map.bind("jump", padAxis(GamepadAxis::LeftY, -1.0F));
		const std::array<DeviceId, 2> devices{kKeyboard, kPad0};
		InputSnapshot				  snapshot;

		// Space let go as the stick went up: the action may have dipped in between.
		keyOf(snapshot, Key::Space) = {.down = false, .transitions = 1};
		setAxis(snapshot, 0, GamepadAxis::LeftY, -0.75F);
		expectButton(map.evaluate("jump", snapshot, devices), true, 2);

		// Both going down together are one press.
		keyOf(snapshot, Key::Space) = {.down = true, .transitions = 1};
		expectButton(map.evaluate("jump", snapshot, devices), true, 1);

		// The stick coming back while Space is held throughout moves nothing.
		keyOf(snapshot, Key::Space) = {.down = true, .transitions = 0};
		setStartAxis(snapshot, 0, GamepadAxis::LeftY, -0.75F);
		setAxis(snapshot, 0, GamepadAxis::LeftY, 0.0F);
		expectButton(map.evaluate("jump", snapshot, devices), true, 0);

		// Both coming up together are one release.
		keyOf(snapshot, Key::Space) = {.down = false, .transitions = 1};
		expectButton(map.evaluate("jump", snapshot, devices), false, 1);
	}

	TEST(ActionMap, ActionTransitionsSaturate) {
		ActionMap map;
		map.bind("jump", key(Key::Space));
		map.bind("jump", key(Key::Enter));
		InputSnapshot snapshot;

		keyOf(snapshot, Key::Space) = {.down = true, .transitions = 201};
		keyOf(snapshot, Key::Enter) = {.down = false, .transitions = 200};
		expectButton(map.evaluate("jump", snapshot, kKeyboardOnly), true, 255);

		// 400 saturates to 255, then parity: the action started and ended up.
		keyOf(snapshot, Key::Space) = {.down = false, .transitions = 200};
		expectButton(map.evaluate("jump", snapshot, kKeyboardOnly), false, 254);
	}

	TEST(ActionMap, StickDeadzonePairsMatchInEitherOrder) {
		ActionMap map;
		map.bind("moveX", padAxis(GamepadAxis::LeftX));
		map.setStickDeadzone(GamepadAxis::LeftX, GamepadAxis::LeftY, 0.0F);
		map.setStickDeadzone(GamepadAxis::LeftY, GamepadAxis::LeftX, 0.5F);
		InputSnapshot snapshot;
		setStick(snapshot, 0.75F, 0.0F);
		EXPECT_EQ(map.evaluate("moveX", snapshot, kPad0Only).value, 0.5F);
	}

	TEST(ActionMap, NaNDeadzoneReadsZero) {
		constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
		ActionMap		map;
		map.bind("perAxis", padAxis(GamepadAxis::LeftX, 1.0F, kNaN));
		map.bind("radial", padAxis(GamepadAxis::RightX));
		map.setStickDeadzone(GamepadAxis::RightX, GamepadAxis::RightY, kNaN);
		InputSnapshot snapshot;

		setAxis(snapshot, 0, GamepadAxis::LeftX, 0.0F);
		setAxis(snapshot, 0, GamepadAxis::RightX, 0.0F);
		EXPECT_EQ(map.evaluate("perAxis", snapshot, kPad0Only).value, 0.0F);
		EXPECT_EQ(map.evaluate("radial", snapshot, kPad0Only).value, 0.0F);
		setAxis(snapshot, 0, GamepadAxis::LeftX, 1.0F);
		setAxis(snapshot, 0, GamepadAxis::RightX, 1.0F);
		EXPECT_EQ(map.evaluate("perAxis", snapshot, kPad0Only).value, 0.0F);
		EXPECT_EQ(map.evaluate("radial", snapshot, kPad0Only).value, 0.0F);
	}

	TEST(ActionMap, WhatTheMapHoldsReadsBackActionsInNameOrderAndBindingsInTheOrderBound) {
		ActionMap map;
		EXPECT_TRUE(map.actions().empty());
		map.bind("moveX", {.source = Binding::Source::Key, .control = static_cast<std::uint16_t>(Key::D)});
		map.bind("jump", {.source = Binding::Source::Key, .control = static_cast<std::uint16_t>(Key::Space)});
		map.bind("moveX", {.source = Binding::Source::Key, .control = static_cast<std::uint16_t>(Key::A), .scale = -1.0F});
		map.setStickDeadzone(GamepadAxis::RightX, GamepadAxis::RightY, 0.3F);
		map.setStickDeadzone(GamepadAxis::LeftX, GamepadAxis::LeftY, 0.2F);
		map.setStickDeadzone(GamepadAxis::RightY, GamepadAxis::RightX, 0.4F);

		EXPECT_EQ(map.actions(), (std::vector<std::string_view>{"jump", "moveX"}));
		const std::span<const Binding> moveX = map.bindings("moveX");
		ASSERT_EQ(moveX.size(), 2U);
		EXPECT_EQ(moveX[0].control, static_cast<std::uint16_t>(Key::D));
		EXPECT_EQ(moveX[1].control, static_cast<std::uint16_t>(Key::A));
		EXPECT_EQ(moveX[1].scale, -1.0F);
		EXPECT_TRUE(map.bindings("never").empty());

		const std::span<const StickDeadzone> sticks = map.stickDeadzones();
		ASSERT_EQ(sticks.size(), 2U);
		EXPECT_EQ(sticks[0].x, GamepadAxis::RightX);
		EXPECT_EQ(sticks[0].y, GamepadAxis::RightY);
		EXPECT_EQ(sticks[0].radius, 0.4F);
		EXPECT_EQ(sticks[1].x, GamepadAxis::LeftX);
		EXPECT_EQ(sticks[1].radius, 0.2F);
	}

	TEST(ActionMap, UnbindClearsAnActionAndKeepsItListed) {
		ActionMap map;
		map.bind("jump", {.source = Binding::Source::Key, .control = static_cast<std::uint16_t>(Key::Space)});
		map.unbind("jump");
		map.unbind("never");
		EXPECT_EQ(map.actions(), (std::vector<std::string_view>{"jump", "never"}));
		EXPECT_TRUE(map.bindings("jump").empty());

		InputSnapshot snapshot;
		snapshot.keys[static_cast<std::size_t>(Key::Space)] = {.down = true, .transitions = 1};
		EXPECT_FALSE(map.evaluate("jump", snapshot, kKeyboardOnly).button.down);

		map.bind("jump", {.source = Binding::Source::Key, .control = static_cast<std::uint16_t>(Key::Space)});
		EXPECT_TRUE(map.evaluate("jump", snapshot, kKeyboardOnly).button.down);
	}

} // namespace
