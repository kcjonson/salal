#include "InputAccumulator.h"

#include <gtest/gtest.h>
#include <limits>
#include <vector>

using namespace foundation;
using Type = InputEvent::Type;

namespace {

	constexpr DeviceId kMouse{.kind = DeviceKind::Mouse};

	constexpr DeviceId pad(std::uint8_t index) {
		return {.kind = DeviceKind::Gamepad, .index = index};
	}

	InputEvent keyDown(Key key, bool repeat = false) {
		return {.type = Type::KeyDown, .repeat = repeat, .key = key};
	}

	InputEvent keyUp(Key key) {
		return {.type = Type::KeyUp, .key = key};
	}

	InputEvent pointer(Type type, glm::vec2 position, MouseButton button = MouseButton::Left) {
		return {.type = type, .device = kMouse, .mouseButton = button, .position = position};
	}

	InputEvent padEvent(Type type, std::uint8_t index) {
		return {.type = type, .device = pad(index)};
	}

	InputEvent padButton(Type type, std::uint8_t index, GamepadButton button) {
		return {.type = type, .device = pad(index), .button = button};
	}

	InputEvent padAxis(std::uint8_t index, GamepadAxis axis, float value) {
		return {.type = Type::Axis, .device = pad(index), .axis = axis, .value = value};
	}

	const ButtonState& keyOf(const InputSnapshot& snapshot, Key key) {
		return snapshot.keys[static_cast<std::size_t>(key)];
	}

	const ButtonState& mouseOf(const InputSnapshot& snapshot, MouseButton button) {
		return snapshot.mouse[static_cast<std::size_t>(button)];
	}

	const ButtonState& padButtonOf(const InputSnapshot& snapshot, std::uint8_t index, GamepadButton button) {
		return snapshot.pads[index].buttons[static_cast<std::size_t>(button)];
	}

	float padAxisOf(const InputSnapshot& snapshot, std::uint8_t index, GamepadAxis axis) {
		return snapshot.pads[index].axes[static_cast<std::size_t>(axis)];
	}

	void expectState(const ButtonState& state, bool down, int transitions) {
		EXPECT_EQ(state.down, down);
		EXPECT_EQ(int{state.transitions}, transitions);
	}

	// One frame as app's tick() runs it: the poll's events are already pushed, the frame reader is
	// consumed, then as many fixed ticks as the accumulated time allows.
	struct FrameLoop {
		InputAccumulator&		   input;
		double					   fixedStep = 0.0;
		double					   accumulated = 0.0;
		InputSnapshot			   frame{};
		std::vector<InputSnapshot> ticks{};

		void run(double realDelta) {
			frame = input.consumeFrame();
			ticks.clear();
			accumulated += realDelta;
			while (accumulated >= fixedStep) {
				ticks.push_back(input.consumeTick());
				accumulated -= fixedStep;
			}
		}
	};

	// The doc's three rates.

	TEST(InputAccumulator, PressAndReleaseInsideOneTickIsSeenByThatTick) {
		InputAccumulator input;
		FrameLoop		 loop{.input = input, .fixedStep = 1.0 / 120.0};

		input.push(keyDown(Key::Space));
		input.push(keyUp(Key::Space));
		loop.run(1.0 / 60.0);

		ASSERT_EQ(loop.ticks.size(), 2U);
		expectState(keyOf(loop.ticks[0], Key::Space), false, 2);
		EXPECT_TRUE(keyOf(loop.ticks[0], Key::Space).pressed());
		EXPECT_TRUE(keyOf(loop.ticks[0], Key::Space).released());
		expectState(keyOf(loop.ticks[1], Key::Space), false, 0);
		expectState(keyOf(loop.frame, Key::Space), false, 2);
	}

	TEST(InputAccumulator, FrameWithTwoTicksReportsOnePressOnce) {
		InputAccumulator input;
		FrameLoop		 loop{.input = input, .fixedStep = 1.0 / 120.0};

		input.push(keyDown(Key::W));
		loop.run(1.0 / 60.0);

		ASSERT_EQ(loop.ticks.size(), 2U);
		expectState(keyOf(loop.ticks[0], Key::W), true, 1);
		EXPECT_TRUE(keyOf(loop.ticks[0], Key::W).pressed());
		expectState(keyOf(loop.ticks[1], Key::W), true, 0);
		EXPECT_FALSE(keyOf(loop.ticks[1], Key::W).pressed());

		loop.run(1.0 / 60.0);
		ASSERT_EQ(loop.ticks.size(), 2U);
		expectState(keyOf(loop.ticks[0], Key::W), true, 0);
		expectState(keyOf(loop.frame, Key::W), true, 0);
	}

	TEST(InputAccumulator, PressInAFrameWithNoTickWaitsForTheNextTick) {
		InputAccumulator input;
		FrameLoop		 loop{.input = input, .fixedStep = 1.0 / 60.0};
		constexpr double kFrame = 1.0 / 144.0;

		input.push(keyDown(Key::Enter));
		loop.run(kFrame);
		EXPECT_TRUE(loop.ticks.empty());
		expectState(keyOf(loop.frame, Key::Enter), true, 1);

		input.push(keyUp(Key::Enter));
		loop.run(kFrame);
		EXPECT_TRUE(loop.ticks.empty());
		expectState(keyOf(loop.frame, Key::Enter), false, 1);

		loop.run(kFrame);
		ASSERT_EQ(loop.ticks.size(), 1U);
		expectState(keyOf(loop.ticks[0], Key::Enter), false, 2);
		EXPECT_TRUE(keyOf(loop.ticks[0], Key::Enter).pressed());
		expectState(keyOf(loop.frame, Key::Enter), false, 0);

		loop.run(kFrame);
		EXPECT_TRUE(loop.ticks.empty());
		loop.run(kFrame);
		ASSERT_EQ(loop.ticks.size(), 1U);
		expectState(keyOf(loop.ticks[0], Key::Enter), false, 0);
	}

	TEST(InputAccumulator, ConsumingOneReaderLeavesTheOtherAlone) {
		InputAccumulator input;
		input.push(keyDown(Key::A));
		input.push(pointer(Type::PointerDown, {4.0F, 8.0F}, MouseButton::Right));

		const InputSnapshot frame = input.consumeFrame();
		expectState(keyOf(frame, Key::A), true, 1);
		expectState(mouseOf(frame, MouseButton::Right), true, 1);
		expectState(keyOf(input.consumeFrame(), Key::A), true, 0);

		const InputSnapshot tick = input.consumeTick();
		expectState(keyOf(tick, Key::A), true, 1);
		expectState(mouseOf(tick, MouseButton::Right), true, 1);
		expectState(keyOf(input.consumeTick(), Key::A), true, 0);

		input.push(keyUp(Key::A));
		expectState(keyOf(input.consumeTick(), Key::A), false, 1);
		expectState(keyOf(input.consumeTick(), Key::A), false, 0);
		expectState(keyOf(input.consumeFrame(), Key::A), false, 1);
	}

	// Choices the doc leaves open.

	TEST(InputAccumulator, RepeatAddsNoTransition) {
		InputAccumulator input;
		input.push(keyDown(Key::Backspace));
		input.push(keyDown(Key::Backspace, true));
		input.push(keyDown(Key::Backspace, true));

		const InputSnapshot frame = input.consumeFrame();
		expectState(keyOf(frame, Key::Backspace), true, 1);
		EXPECT_EQ(input.frameEvents().size(), 3U);
	}

	TEST(InputAccumulator, RepeatOnAReleasedKeyChangesNothing) {
		InputAccumulator input;
		input.push(keyDown(Key::Backspace, true));
		expectState(keyOf(input.consumeTick(), Key::Backspace), false, 0);
	}

	TEST(InputAccumulator, DuplicateDownOrUpAddsNoTransition) {
		InputAccumulator input;
		input.push(keyUp(Key::Q));
		input.push(keyDown(Key::Q));
		input.push(keyDown(Key::Q));
		input.push(pointer(Type::PointerUp, {}, MouseButton::Middle));
		expectState(keyOf(input.consumeTick(), Key::Q), true, 1);

		input.push(keyUp(Key::Q));
		input.push(keyUp(Key::Q));
		expectState(keyOf(input.consumeTick(), Key::Q), false, 1);
		expectState(mouseOf(input.consumeTick(), MouseButton::Middle), false, 0);
	}

	TEST(InputAccumulator, FocusLostReleasesHeldKeysAndMouseButtons) {
		InputAccumulator input;
		input.push(keyDown(Key::W));
		input.push(keyDown(Key::LeftShift));
		input.push(pointer(Type::PointerDown, {}, MouseButton::Left));
		input.consumeTick();

		input.push({.type = Type::FocusLost});
		// GLFW does send ups after its focus callback; they find the keys already up.
		input.push(keyUp(Key::W));
		const InputSnapshot tick = input.consumeTick();
		expectState(keyOf(tick, Key::W), false, 1);
		EXPECT_TRUE(keyOf(tick, Key::W).released());
		expectState(keyOf(tick, Key::LeftShift), false, 1);
		expectState(mouseOf(tick, MouseButton::Left), false, 1);
		EXPECT_EQ(tick.mods, 0);
	}

	TEST(InputAccumulator, FocusLostLeavesPadsAlone) {
		InputAccumulator input;
		input.push(padEvent(Type::DeviceConnected, 0));
		input.push(padButton(Type::ButtonDown, 0, GamepadButton::A));
		input.push(padAxis(0, GamepadAxis::LeftX, 0.5F));
		input.consumeTick();

		input.push({.type = Type::FocusLost});
		const InputSnapshot tick = input.consumeTick();
		expectState(padButtonOf(tick, 0, GamepadButton::A), true, 0);
		EXPECT_EQ(padAxisOf(tick, 0, GamepadAxis::LeftX), 0.5F);
	}

	TEST(InputAccumulator, DisconnectReleasesThePadsButtonsAndZeroesItsAxes) {
		InputAccumulator input;
		input.push(padEvent(Type::DeviceConnected, 2));
		input.push(padButton(Type::ButtonDown, 2, GamepadButton::RightBumper));
		input.push(padAxis(2, GamepadAxis::RightTrigger, 0.75F));
		const InputSnapshot connected = input.consumeTick();
		EXPECT_TRUE(connected.pads[2].connected);
		expectState(padButtonOf(connected, 2, GamepadButton::RightBumper), true, 1);

		input.push(padEvent(Type::DeviceDisconnected, 2));
		const InputSnapshot gone = input.consumeTick();
		EXPECT_FALSE(gone.pads[2].connected);
		expectState(padButtonOf(gone, 2, GamepadButton::RightBumper), false, 1);
		EXPECT_TRUE(padButtonOf(gone, 2, GamepadButton::RightBumper).released());
		EXPECT_EQ(padAxisOf(gone, 2, GamepadAxis::RightTrigger), 0.0F);
	}

	TEST(InputAccumulator, PadInputWhileDisconnectedIsDropped) {
		InputAccumulator input;
		input.push(padButton(Type::ButtonDown, 1, GamepadButton::Start));
		input.push(padAxis(1, GamepadAxis::LeftY, -1.0F));

		const InputSnapshot frame = input.consumeFrame();
		EXPECT_FALSE(frame.pads[1].connected);
		expectState(padButtonOf(frame, 1, GamepadButton::Start), false, 0);
		EXPECT_EQ(padAxisOf(frame, 1, GamepadAxis::LeftY), 0.0F);
		EXPECT_TRUE(input.frameEvents().empty());
	}

	TEST(InputAccumulator, NonFiniteAxisValuesAreDropped) {
		InputAccumulator input;
		input.push(padEvent(Type::DeviceConnected, 0));
		input.push(padAxis(0, GamepadAxis::RightX, 0.5F));
		input.push(padAxis(0, GamepadAxis::RightX, std::numeric_limits<float>::quiet_NaN()));
		input.push(padAxis(0, GamepadAxis::RightX, std::numeric_limits<float>::infinity()));

		const InputSnapshot frame = input.consumeFrame();
		EXPECT_EQ(padAxisOf(frame, 0, GamepadAxis::RightX), 0.5F);
		EXPECT_EQ(input.frameEvents().size(), 2U);
	}

	TEST(InputAccumulator, VirtualPadsUseTheUpperSlots) {
		InputAccumulator   input;
		const std::uint8_t lastVirtual = kMaxRealGamepads + kMaxVirtualGamepads - 1;
		input.push(padEvent(Type::DeviceConnected, lastVirtual));
		input.push(padButton(Type::ButtonDown, lastVirtual, GamepadButton::Y));
		expectState(padButtonOf(input.consumeTick(), lastVirtual, GamepadButton::Y), true, 1);
	}

	TEST(InputAccumulator, CharChangesNoState) {
		InputAccumulator input;
		input.push({.type = Type::Char, .codepoint = U'x'});

		const InputSnapshot frame = input.consumeFrame();
		for (const ButtonState& key : frame.keys) {
			expectState(key, false, 0);
		}
		ASSERT_EQ(input.frameEvents().size(), 1U);
		EXPECT_EQ(input.frameEvents()[0].codepoint, U'x');
	}

	TEST(InputAccumulator, PointerDeltaAndScrollResetPerReader) {
		InputAccumulator input;
		input.push(pointer(Type::PointerMove, {10.0F, 20.0F}));
		input.push(pointer(Type::PointerMove, {15.0F, 26.0F}));
		input.push({.type = Type::Scroll, .device = kMouse, .position = {15.0F, 26.0F}, .scroll = {0.0F, 1.0F}});
		input.push({.type = Type::Scroll, .device = kMouse, .position = {15.0F, 26.0F}, .scroll = {0.5F, 2.0F}});

		const InputSnapshot frame = input.consumeFrame();
		EXPECT_EQ(frame.pointer, glm::vec2(15.0F, 26.0F));
		EXPECT_EQ(frame.pointerDelta, glm::vec2(5.0F, 6.0F));
		EXPECT_EQ(frame.scroll, glm::vec2(0.5F, 3.0F));

		const InputSnapshot nextFrame = input.consumeFrame();
		EXPECT_EQ(nextFrame.pointer, glm::vec2(15.0F, 26.0F));
		EXPECT_EQ(nextFrame.pointerDelta, glm::vec2(0.0F));
		EXPECT_EQ(nextFrame.scroll, glm::vec2(0.0F));

		input.push(pointer(Type::PointerMove, {16.0F, 24.0F}));
		const InputSnapshot tick = input.consumeTick();
		EXPECT_EQ(tick.pointerDelta, glm::vec2(6.0F, 4.0F));
		EXPECT_EQ(tick.scroll, glm::vec2(0.5F, 3.0F));
		EXPECT_EQ(input.consumeFrame().pointerDelta, glm::vec2(1.0F, -2.0F));
	}

	TEST(InputAccumulator, FirstPointerPositionAddsNoDelta) {
		InputAccumulator input;
		input.push(pointer(Type::PointerMove, {300.0F, 200.0F}));
		const InputSnapshot tick = input.consumeTick();
		EXPECT_EQ(tick.pointer, glm::vec2(300.0F, 200.0F));
		EXPECT_EQ(tick.pointerDelta, glm::vec2(0.0F));
	}

	TEST(InputAccumulator, PointerButtonsCarryThePointer) {
		InputAccumulator input;
		input.push(pointer(Type::PointerMove, {10.0F, 10.0F}));
		input.push(pointer(Type::PointerDown, {30.0F, 40.0F}));
		const InputSnapshot tick = input.consumeTick();
		EXPECT_EQ(tick.pointer, glm::vec2(30.0F, 40.0F));
		EXPECT_EQ(tick.pointerDelta, glm::vec2(20.0F, 30.0F));
		expectState(mouseOf(tick, MouseButton::Left), true, 1);
	}

	TEST(InputAccumulator, PointerUpAndScrollCarryThePointer) {
		InputAccumulator input;
		input.push(pointer(Type::PointerMove, {10.0F, 10.0F}));
		input.push(pointer(Type::PointerDown, {10.0F, 10.0F}));
		input.consumeTick();

		input.push(pointer(Type::PointerUp, {50.0F, 30.0F}));
		InputSnapshot tick = input.consumeTick();
		EXPECT_EQ(tick.pointer, glm::vec2(50.0F, 30.0F));
		EXPECT_EQ(tick.pointerDelta, glm::vec2(40.0F, 20.0F));
		expectState(mouseOf(tick, MouseButton::Left), false, 1);

		input.push({.type = Type::Scroll, .device = kMouse, .position = {45.0F, 60.0F}, .scroll = {0.0F, -1.0F}});
		tick = input.consumeTick();
		EXPECT_EQ(tick.pointer, glm::vec2(45.0F, 60.0F));
		EXPECT_EQ(tick.pointerDelta, glm::vec2(-5.0F, 30.0F));
		EXPECT_EQ(tick.scroll, glm::vec2(0.0F, -1.0F));
	}

	TEST(InputAccumulator, EnterAndLeaveSetPointerInWindowOnly) {
		InputAccumulator input;
		input.push(pointer(Type::PointerMove, {5.0F, 5.0F}));
		input.push({.type = Type::PointerEnter, .device = kMouse});
		EXPECT_TRUE(input.consumeTick().pointerInWindow);

		input.push({.type = Type::PointerLeave, .device = kMouse});
		const InputSnapshot tick = input.consumeTick();
		EXPECT_FALSE(tick.pointerInWindow);
		EXPECT_EQ(tick.pointer, glm::vec2(5.0F, 5.0F));
	}

	TEST(InputAccumulator, FrameEventsHoldWhatTheLastConsumeFrameCovered) {
		InputAccumulator input;
		input.push(keyDown(Key::A));
		input.push({.type = Type::Char, .codepoint = U'a'});
		EXPECT_TRUE(input.frameEvents().empty());

		input.consumeFrame();
		ASSERT_EQ(input.frameEvents().size(), 2U);
		EXPECT_EQ(input.frameEvents()[0].type, Type::KeyDown);
		EXPECT_EQ(input.frameEvents()[1].type, Type::Char);

		input.push(keyUp(Key::A));
		input.consumeTick();
		EXPECT_EQ(input.frameEvents().size(), 2U);

		input.consumeFrame();
		ASSERT_EQ(input.frameEvents().size(), 1U);
		EXPECT_EQ(input.frameEvents()[0].type, Type::KeyUp);

		input.consumeFrame();
		EXPECT_TRUE(input.frameEvents().empty());
	}

	TEST(InputAccumulator, EventsNamingNoValidControlOrPadAreDropped) {
		InputAccumulator   input;
		const std::uint8_t pastLastPad = kMaxRealGamepads + kMaxVirtualGamepads;
		input.push(keyDown(Key::Unknown));
		input.push(keyDown(Key::Count));
		input.push(pointer(Type::PointerDown, {}, MouseButton::Count));
		input.push(padEvent(Type::DeviceConnected, pastLastPad));
		input.push(padButton(Type::ButtonDown, pastLastPad, GamepadButton::A));
		input.push(padButton(Type::ButtonDown, 0, GamepadButton::Count));
		input.push(padAxis(0, GamepadAxis::Count, 1.0F));
		input.push({.type = Type::ButtonDown, .device = {.kind = DeviceKind::Keyboard}, .button = GamepadButton::A});

		const InputSnapshot frame = input.consumeFrame();
		EXPECT_TRUE(input.frameEvents().empty());
		expectState(keyOf(frame, Key::Unknown), false, 0);
		for (const GamepadState& state : frame.pads) {
			EXPECT_FALSE(state.connected);
		}
	}

	TEST(InputAccumulator, ModsFollowTheHeldModifierKeys) {
		InputAccumulator input;
		input.push({.type = Type::KeyDown, .mods = ModAlt, .key = Key::A});
		EXPECT_EQ(input.consumeTick().mods, 0);

		struct Modifier {
			Key			 key;
			std::uint8_t bit;
		};
		const Modifier modifiers[] = {
			{.key = Key::LeftShift, .bit = ModShift},
			{.key = Key::RightShift, .bit = ModShift},
			{.key = Key::LeftControl, .bit = ModControl},
			{.key = Key::RightControl, .bit = ModControl},
			{.key = Key::LeftAlt, .bit = ModAlt},
			{.key = Key::RightAlt, .bit = ModAlt},
			{.key = Key::LeftSuper, .bit = ModSuper},
			{.key = Key::RightSuper, .bit = ModSuper},
		};
		for (const Modifier& modifier : modifiers) {
			input.push(keyDown(modifier.key));
			EXPECT_EQ(input.consumeTick().mods, modifier.bit) << "key " << static_cast<int>(modifier.key);
			input.push(keyUp(modifier.key));
			EXPECT_EQ(input.consumeTick().mods, 0) << "key " << static_cast<int>(modifier.key);
		}

		input.push(keyDown(Key::LeftShift));
		input.push(keyDown(Key::RightControl));
		input.push(keyDown(Key::LeftSuper));
		EXPECT_EQ(input.consumeTick().mods, ModShift | ModControl | ModSuper);
		input.push(keyDown(Key::RightShift));
		input.push(keyUp(Key::LeftShift));
		EXPECT_EQ(input.consumeTick().mods, ModShift | ModControl | ModSuper);
		input.push(keyUp(Key::RightShift));
		EXPECT_EQ(input.consumeTick().mods, ModControl | ModSuper);
	}

	TEST(InputAccumulator, AxisValuesClampToTheUnitRange) {
		InputAccumulator input;
		input.push(padEvent(Type::DeviceConnected, 0));
		input.push(padAxis(0, GamepadAxis::LeftX, 1.5F));
		input.push(padAxis(0, GamepadAxis::LeftY, -3.0F));
		const InputSnapshot tick = input.consumeTick();
		EXPECT_EQ(padAxisOf(tick, 0, GamepadAxis::LeftX), 1.0F);
		EXPECT_EQ(padAxisOf(tick, 0, GamepadAxis::LeftY), -1.0F);
	}

	TEST(InputAccumulator, TransitionsSaturate) {
		InputAccumulator input;
		for (int tap = 0; tap < 200; ++tap) {
			input.push(keyDown(Key::Space));
			input.push(keyUp(Key::Space));
		}
		const ButtonState space = keyOf(input.consumeTick(), Key::Space);
		expectState(space, false, 255);
		EXPECT_TRUE(space.pressed());
		EXPECT_TRUE(space.released());
	}

} // namespace
