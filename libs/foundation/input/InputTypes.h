#pragma once

// Platform-independent input types. No GLFW here: whatever polls the platform maps its codes
// onto these, and everything above reads them.

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/vec2.hpp>

namespace foundation {

	// Physical key positions, GLFW's token set in GLFW's order. A..Z, Num0..Num9 and F1..F25 are
	// contiguous, and consumers compute keys from those runs.
	enum class Key : std::uint16_t {
		Unknown,

		Space,
		Apostrophe, // '
		Comma,		// ,
		Minus,		// -
		Period,		// .
		Slash,		// /

		Num0,
		Num1,
		Num2,
		Num3,
		Num4,
		Num5,
		Num6,
		Num7,
		Num8,
		Num9,

		Semicolon, // ;
		Equal,	   // =

		A,
		B,
		C,
		D,
		E,
		F,
		G,
		H,
		I,
		J,
		K,
		L,
		M,
		N,
		O,
		P,
		Q,
		R,
		S,
		T,
		U,
		V,
		W,
		X,
		Y,
		Z,

		LeftBracket,  // [
		Backslash,	  // backslash
		RightBracket, // ]
		GraveAccent,  // `
		World1,		  // non-US #1
		World2,		  // non-US #2

		Escape,
		Enter,
		Tab,
		Backspace,
		Insert,
		Delete,
		Right,
		Left,
		Down,
		Up,
		PageUp,
		PageDown,
		Home,
		End,
		CapsLock,
		ScrollLock,
		NumLock,
		PrintScreen,
		Pause,

		F1,
		F2,
		F3,
		F4,
		F5,
		F6,
		F7,
		F8,
		F9,
		F10,
		F11,
		F12,
		F13,
		F14,
		F15,
		F16,
		F17,
		F18,
		F19,
		F20,
		F21,
		F22,
		F23,
		F24,
		F25,

		Kp0,
		Kp1,
		Kp2,
		Kp3,
		Kp4,
		Kp5,
		Kp6,
		Kp7,
		Kp8,
		Kp9,
		KpDecimal,
		KpDivide,
		KpMultiply,
		KpSubtract,
		KpAdd,
		KpEnter,
		KpEqual,

		LeftShift,
		LeftControl,
		LeftAlt,
		LeftSuper,
		RightShift,
		RightControl,
		RightAlt,
		RightSuper,
		Menu,

		Count
	};

	// Values are GLFW's button numbers, so a GLFW button casts straight across.
	enum class MouseButton : std::uint8_t { Left, Right, Middle, Button4, Button5, Button6, Button7, Button8, Count };

	// GLFW's gamepad layout, in GLFW's order.
	enum class GamepadButton : std::uint8_t {
		A,
		B,
		X,
		Y,
		LeftBumper,
		RightBumper,
		Back,
		Start,
		Guide,
		LeftThumb,
		RightThumb,
		DpadUp,
		DpadRight,
		DpadDown,
		DpadLeft,
		Count
	};

	enum class GamepadAxis : std::uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

	enum class DeviceKind : std::uint8_t { Keyboard, Mouse, Gamepad };

	struct DeviceId {
		DeviceKind	 kind = DeviceKind::Keyboard;
		std::uint8_t index = 0;
		bool		 operator==(const DeviceId&) const = default;
	};

	inline constexpr int kMaxRealGamepads = 16;	  // GLFW joystick ids 0 to 15, pads[0..15]
	inline constexpr int kMaxVirtualGamepads = 4; // the dev server's vpad0 to vpad3, pads[16..19]

	// GLFW's bit values.
	enum Mod : std::uint8_t { ModShift = 1, ModControl = 2, ModAlt = 4, ModSuper = 8 };

	// One per platform callback, in order. Positions are in GLFW window coordinates, which are
	// logical pixels already; PointerMove, PointerDown, PointerUp and Scroll carry the pointer's.
	struct InputEvent {
		enum class Type : std::uint8_t {
			KeyDown,
			KeyUp,
			Char,
			PointerMove,
			PointerDown,
			PointerUp,
			Scroll,
			PointerEnter,
			PointerLeave,
			ButtonDown,
			ButtonUp,
			Axis,
			DeviceConnected,
			DeviceDisconnected,
			FocusLost,
			FocusGained
		};
		Type		  type = Type::KeyDown;
		DeviceId	  device{};
		double		  time = 0.0; // the app clock when pushed
		std::uint8_t  mods = 0;
		bool		  repeat = false;
		Key			  key = Key::Unknown;
		char32_t	  codepoint = 0;
		MouseButton	  mouseButton = MouseButton::Left;
		GamepadButton button = GamepadButton::A;
		GamepadAxis	  axis = GamepadAxis::LeftX;
		float		  value = 0.0F;
		glm::vec2	  position{};
		glm::vec2	  scroll{};
	};

	// Handmade Hero's halfTransitionCount: how many times the control changed since the last read,
	// saturating at 255.
	struct ButtonState {
		bool		 down = false;
		std::uint8_t transitions = 0;

		// Went down at least once since the last read.
		bool pressed() const { return transitions >= 2 || (transitions == 1 && down); }
		// Went up at least once since the last read.
		bool released() const { return transitions >= 2 || (transitions == 1 && !down); }
	};

	struct GamepadState {
		bool																	connected = false;
		std::array<ButtonState, static_cast<std::size_t>(GamepadButton::Count)> buttons{};
		std::array<float, static_cast<std::size_t>(GamepadAxis::Count)>			axes{};
	};

	// Every device's state, about 1.4 KB.
	struct InputSnapshot {
		std::array<ButtonState, static_cast<std::size_t>(Key::Count)>		  keys{};
		std::array<ButtonState, static_cast<std::size_t>(MouseButton::Count)> mouse{};
		glm::vec2															  pointer{};
		glm::vec2															  pointerDelta{};
		glm::vec2															  scroll{};
		bool																  pointerInWindow = false;
		std::uint8_t														  mods = 0;
		std::array<GamepadState, kMaxRealGamepads + kMaxVirtualGamepads>	  pads{};
	};

} // namespace foundation
