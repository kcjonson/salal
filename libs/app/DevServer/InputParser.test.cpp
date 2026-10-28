// /api/input's events parsed into InputEvents, and the virtual pads' connections. No sockets.

#ifdef SALAL_DEV_SERVER

#include "DevServer/InputParser.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace app::detail {

	namespace {

		using foundation::DeviceId;
		using foundation::DeviceKind;
		using foundation::GamepadAxis;
		using foundation::GamepadButton;
		using foundation::InputEvent;
		using foundation::Key;
		using foundation::MouseButton;
		using Type = InputEvent::Type;

		constexpr DeviceId kMouse{.kind = DeviceKind::Mouse};
		constexpr DeviceId kKeyboard{.kind = DeviceKind::Keyboard};
		constexpr DeviceId kVpad0{.kind = DeviceKind::Gamepad, .index = 16};
		constexpr DeviceId kVpad3{.kind = DeviceKind::Gamepad, .index = 19};

		ParsedInput parse(std::vector<std::string> evs, VirtualPads& pads) {
			return parseInputEvents(evs, pads);
		}

		ParsedInput parse(std::vector<std::string> evs) {
			VirtualPads pads{};
			return parseInputEvents(evs, pads);
		}

		std::string errorOf(const std::string& ev) {
			return parse({ev}).error;
		}

		void expectPointer(const InputEvent& event, Type type, float x, float y, MouseButton button = MouseButton::Left) {
			EXPECT_EQ(event.type, type);
			EXPECT_EQ(event.device, kMouse);
			EXPECT_EQ(event.position.x, x);
			EXPECT_EQ(event.position.y, y);
			EXPECT_EQ(event.mouseButton, button);
		}

	} // namespace

	TEST(InputParserTest, PointerEventsCarryTheirPositionAndButton) {
		const ParsedInput parsed = parse({"move,10,20", "down,1.5,2.5,right", "up,3,4,middle", "scroll,5,6,-2"});
		ASSERT_TRUE(parsed.error.empty()) << parsed.error;
		ASSERT_EQ(parsed.events.size(), 4U);
		expectPointer(parsed.events[0], Type::PointerMove, 10.0F, 20.0F);
		expectPointer(parsed.events[1], Type::PointerDown, 1.5F, 2.5F, MouseButton::Right);
		expectPointer(parsed.events[2], Type::PointerUp, 3.0F, 4.0F, MouseButton::Middle);
		expectPointer(parsed.events[3], Type::Scroll, 5.0F, 6.0F);
		EXPECT_EQ(parsed.events[3].scroll.x, 0.0F);
		EXPECT_EQ(parsed.events[3].scroll.y, -2.0F);
	}

	TEST(InputParserTest, AClickIsAMoveADownAndAnUp) {
		const ParsedInput parsed = parse({"click,7,8"});
		ASSERT_EQ(parsed.events.size(), 3U);
		expectPointer(parsed.events[0], Type::PointerMove, 7.0F, 8.0F);
		expectPointer(parsed.events[1], Type::PointerDown, 7.0F, 8.0F);
		expectPointer(parsed.events[2], Type::PointerUp, 7.0F, 8.0F);
	}

	TEST(InputParserTest, KeysByLetterDigitFunctionOrName) {
		const ParsedInput parsed =
			parse({"keydown,r", "keyup,R", "keydown,7", "keydown,F1", "keydown,f12", "keyup,Escape", "keyup,esc", "keydown,PageDown"});
		ASSERT_TRUE(parsed.error.empty()) << parsed.error;
		const std::vector<std::pair<Type, Key>> expected{
			{Type::KeyDown, Key::R},
			{Type::KeyUp, Key::R},
			{Type::KeyDown, Key::Num7},
			{Type::KeyDown, Key::F1},
			{Type::KeyDown, Key::F12},
			{Type::KeyUp, Key::Escape},
			{Type::KeyUp, Key::Escape},
			{Type::KeyDown, Key::PageDown},
		};
		ASSERT_EQ(parsed.events.size(), expected.size());
		for (std::size_t i = 0; i < expected.size(); ++i) {
			EXPECT_EQ(parsed.events[i].type, expected[i].first) << i;
			EXPECT_EQ(parsed.events[i].key, expected[i].second) << i;
			EXPECT_EQ(parsed.events[i].device, kKeyboard) << i;
		}
	}

	TEST(InputParserTest, ACharIsAUnicodeScalarValue) {
		const ParsedInput parsed = parse({"char,65", "char,0x1F600", "char,U+e9"});
		ASSERT_TRUE(parsed.error.empty()) << parsed.error;
		ASSERT_EQ(parsed.events.size(), 3U);
		EXPECT_EQ(parsed.events[0].type, Type::Char);
		EXPECT_EQ(parsed.events[0].device, kKeyboard);
		EXPECT_EQ(parsed.events[0].codepoint, U'A');
		EXPECT_EQ(parsed.events[1].codepoint, U'\U0001F600');
		EXPECT_EQ(parsed.events[2].codepoint, U'é');

		const std::string error = "a codepoint is a Unicode scalar value, decimal or hex after 0x or U+";
		EXPECT_EQ(errorOf("char,0"), error);
		EXPECT_EQ(errorOf("char,0xD800"), error);
		EXPECT_EQ(errorOf("char,0x110000"), error);
		EXPECT_EQ(errorOf("char,A"), error);
		EXPECT_EQ(errorOf("char,"), error);
		EXPECT_EQ(errorOf("char,65,66"), "expected char,<codepoint>");
	}

	TEST(InputParserTest, VirtualPadsConnectTakeInputAndDisconnect) {
		VirtualPads		  pads{};
		const ParsedInput parsed = parse(
			{"connect,vpad0",
			 "pad,vpad0,A,down",
			 "pad,vpad0,dpadleft,up",
			 "axis,vpad0,RightTrigger,0.75",
			 "disconnect,vpad0",
			 "connect,vpad3"},
			pads
		);
		ASSERT_TRUE(parsed.error.empty()) << parsed.error;
		ASSERT_EQ(parsed.events.size(), 6U);
		EXPECT_EQ(parsed.events[0].type, Type::DeviceConnected);
		EXPECT_EQ(parsed.events[0].device, kVpad0);
		EXPECT_EQ(parsed.events[1].type, Type::ButtonDown);
		EXPECT_EQ(parsed.events[1].button, GamepadButton::A);
		EXPECT_EQ(parsed.events[1].device, kVpad0);
		EXPECT_EQ(parsed.events[2].type, Type::ButtonUp);
		EXPECT_EQ(parsed.events[2].button, GamepadButton::DpadLeft);
		EXPECT_EQ(parsed.events[3].type, Type::Axis);
		EXPECT_EQ(parsed.events[3].axis, GamepadAxis::RightTrigger);
		EXPECT_EQ(parsed.events[3].value, 0.75F);
		EXPECT_EQ(parsed.events[4].type, Type::DeviceDisconnected);
		EXPECT_EQ(parsed.events[4].device, kVpad0);
		EXPECT_EQ(parsed.events[5].type, Type::DeviceConnected);
		EXPECT_EQ(parsed.events[5].device, kVpad3);
		EXPECT_EQ(pads, (VirtualPads{false, false, false, true}));
	}

	TEST(InputParserTest, APadTakesInputOnlyWhileConnected) {
		VirtualPads pads{};
		EXPECT_EQ(parse({"pad,vpad1,a,down"}, pads).error, "vpad1 isn't connected (connect,vpad1 first)");
		EXPECT_EQ(parse({"axis,vpad1,leftx,0"}, pads).error, "vpad1 isn't connected (connect,vpad1 first)");
		EXPECT_EQ(parse({"disconnect,vpad1"}, pads).error, "vpad1 isn't connected");
		ASSERT_TRUE(parse({"connect,vpad1"}, pads).error.empty());
		EXPECT_EQ(parse({"connect,vpad1"}, pads).error, "vpad1 is already connected");
		EXPECT_TRUE(parse({"pad,vpad1,a,down"}, pads).error.empty());
	}

	TEST(InputParserTest, VirtualPadsAreVpad0ToVpad3) {
		const std::string error = "unknown device 'vpad4' (expected vpad0 to vpad3)";
		EXPECT_EQ(errorOf("connect,vpad4"), error);
		EXPECT_EQ(errorOf("connect,vpad-1"), "unknown device 'vpad-1' (expected vpad0 to vpad3)");
		EXPECT_EQ(errorOf("connect,0"), "unknown device '0' (expected vpad0 to vpad3)");
		EXPECT_EQ(errorOf("connect,vpad"), "unknown device 'vpad' (expected vpad0 to vpad3)");
		EXPECT_EQ(errorOf("pad,vpad9,a,down"), "unknown device 'vpad9' (expected vpad0 to vpad3)");
	}

	TEST(InputParserTest, PadButtonsAndAxesByName) {
		VirtualPads pads{true, false, false, false};
		EXPECT_EQ(parse({"pad,vpad0,select,down"}, pads).error, "unknown pad button 'select'");
		EXPECT_EQ(parse({"pad,vpad0,a,press"}, pads).error, "a pad button goes down or up");
		EXPECT_EQ(parse({"axis,vpad0,leftz,0"}, pads).error, "unknown pad axis 'leftz'");
		EXPECT_EQ(parse({"axis,vpad0,leftx,1.5"}, pads).error, "an axis value is a number from -1 to 1");
		EXPECT_EQ(parse({"axis,vpad0,leftx,nan"}, pads).error, "an axis value is a number from -1 to 1");
		EXPECT_EQ(parse({"axis,vpad0,leftx,-1"}, pads).events.at(0).value, -1.0F);
	}

	TEST(InputParserTest, AllOrNothing) {
		VirtualPads		  pads{};
		const ParsedInput parsed = parse({"connect,vpad2", "keydown,A", "bogus"}, pads);
		EXPECT_EQ(parsed.error, "unknown event type (expected move|down|up|click|scroll|keydown|keyup|char|pad|axis|connect|disconnect)");
		EXPECT_EQ(parsed.failed, "bogus");
		EXPECT_TRUE(parsed.events.empty());
		EXPECT_EQ(pads, VirtualPads{});
	}

	TEST(InputParserTest, MalformedEvents) {
		EXPECT_EQ(errorOf(""), "empty event");
		EXPECT_EQ(errorOf("move,1"), "expected type,x,y with an optional button");
		EXPECT_EQ(errorOf("move,1,2,left"), "expected type,x,y with an optional button");
		EXPECT_EQ(errorOf("click,a,2"), "non-numeric coordinates");
		EXPECT_EQ(errorOf("click,1,2x"), "non-numeric coordinates");
		EXPECT_EQ(errorOf("down,1,2,back"), "unknown button (expected left|right|middle)");
		EXPECT_EQ(errorOf("scroll,1,2"), "expected scroll,x,y,delta");
		EXPECT_EQ(errorOf("scroll,1,2,up"), "non-numeric scroll delta");
		EXPECT_EQ(errorOf("keydown"), "key event requires a key name (e.g. keydown,R)");
		EXPECT_EQ(errorOf("keydown,"), "key event requires a key name (e.g. keydown,R)");
		EXPECT_EQ(errorOf("keydown,F13"), "unknown key 'F13'");
		EXPECT_EQ(errorOf("keydown,shift"), "unknown key 'shift'");
		EXPECT_EQ(errorOf("keydown,a,b"), "expected keydown,<key> or keyup,<key>");
	}

} // namespace app::detail

#endif
