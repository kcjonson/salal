#include "ActionMap.h"
#include "InputAccumulator.h"

#include <array>
#include <cmath>
#include <gtest/gtest.h>

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

		setAxis(snapshot, 0, GamepadAxis::RightTrigger, 0.625F);
		fire = map.evaluate("fire", snapshot, kPad0Only);
		expectButton(fire, true, 0);
		EXPECT_EQ(fire.value, 0.625F);
		// A snapshot holds no axis history, so an axis gives no edge.
		EXPECT_FALSE(fire.button.pressed());
	}

	TEST(ActionMap, NegativeScaleReadsTheNegativeHalfOfAnAxisAsAButton) {
		ActionMap map;
		map.bind("menuLeft", padAxis(GamepadAxis::LeftX, -1.0F));
		InputSnapshot snapshot;

		setAxis(snapshot, 0, GamepadAxis::LeftX, -0.75F);
		ActionState left = map.evaluate("menuLeft", snapshot, kPad0Only);
		expectButton(left, true, 0);
		EXPECT_EQ(left.value, 0.75F);

		setAxis(snapshot, 0, GamepadAxis::LeftX, 0.75F);
		left = map.evaluate("menuLeft", snapshot, kPad0Only);
		expectButton(left, false, 0);
		EXPECT_EQ(left.value, -0.75F);
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

} // namespace
