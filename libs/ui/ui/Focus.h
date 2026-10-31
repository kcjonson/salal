#pragma once

// Focus, controller first (docs/ui-design.md, "Services and input routing"). Each player has one
// focused component in each scene, and that player's devices drive it: keys go to it and bubble
// (R9.15, R9.24), and the navigation actions (activate, cancel, the four directions, next and
// previous) are read through the player's foundation::ActionMap, so they rebind per device and
// nothing checks a key by name (R9.27). Tab order comes from the tree (R9.18); a direction picks the
// nearest focusable that way on screen (R9.26); a scope keeps navigation inside a subtree (R9.20); a
// focus group is one Tab stop whose arrows move among its children (R9.29).

#include <input/ActionMap.h>
#include <input/InputTypes.h>

#include <cstdint>
#include <string_view>

namespace ui {

	class Component;

	// app's player slots: one focus per slot.
	inline constexpr int kMaxPlayers = 8;

	enum class Direction : std::uint8_t { Up, Down, Left, Right };

	// What drove input last (R9.27): a component may show key hints, or hide what only hover shows.
	enum class InputMode : std::uint8_t { Pointer, Keyboard, Controller };

	// A focus group's arrows: along one axis, or all four for a grid. Without wrap an arrow at an end
	// is left to directional focus.
	struct FocusGroup {
		enum class Orientation : std::uint8_t { Horizontal, Vertical, Both };
		Orientation orientation = Orientation::Both;
		bool		wrap = false;
	};

	// The navigation actions, by their names in a player's ActionMap.
	namespace actions {
		inline constexpr std::string_view kActivate = "ui.activate";
		inline constexpr std::string_view kCancel = "ui.cancel";
		inline constexpr std::string_view kUp = "ui.up";
		inline constexpr std::string_view kDown = "ui.down";
		inline constexpr std::string_view kLeft = "ui.left";
		inline constexpr std::string_view kRight = "ui.right";
		inline constexpr std::string_view kNext = "ui.next"; // with Shift held, previous
		inline constexpr std::string_view kPrevious = "ui.previous";
	} // namespace actions

	// The map every player starts with: the arrows, Enter and Space, Escape, Tab; a pad's D-pad and
	// left stick, A and B, and its bumpers for previous and next.
	foundation::ActionMap defaultNavigation();

	// A key, a character or an action, delivered to the focused component and bubbled through its
	// ancestors until consumed. Activate, Cancel and Navigate are the actions; a component that moves
	// along an axis consumes its own Navigate directions and leaves the others to directional focus.
	struct KeyEvent {
		enum class Type : std::uint8_t { Down, Up, Char, Activate, Cancel, Navigate };

		Type			type = Type::Down;
		foundation::Key key = foundation::Key::Unknown;
		char32_t		codepoint = 0;
		std::uint8_t	mods = 0;
		bool			repeat = false;
		Direction		direction = Direction::Up; // Navigate's
		int				player = 0;
		Component*		target = nullptr;
		Component*		currentTarget = nullptr;
		bool			consumed = false;

		void consume() { consumed = true; }
	};

} // namespace ui
