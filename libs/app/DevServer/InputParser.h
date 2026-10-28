#pragma once

// /api/input's events, parsed on the HTTP thread into the InputEvents the server queues for the
// accumulator. Each `ev` value is comma-separated; positions are logical pixels, the space the
// screenshot is in:
//   move,x,y                 down,x,y[,button]    up,x,y[,button]    click,x,y[,button] (move, down, up)
//   scroll,x,y,delta         keydown,<key>        keyup,<key>        char,<codepoint>
//   pad,vpad<n>,<button>,down|up                  axis,vpad<n>,<axis>,<value>
//   connect,vpad<n>          disconnect,vpad<n>
// A button is left, right or middle. A key is a letter, a digit, F1 to F12, or space, escape (esc),
// enter (return), tab, backspace, delete (del), insert, home, end, pageup, pagedown, up, down, left or
// right, without case. A codepoint is decimal, or hex after 0x or U+. Pad buttons and axes are
// foundation's GamepadButton and GamepadAxis names without case (a, leftbumper, dpadup; leftx,
// righttrigger), an axis value is in [-1, 1], and vpad0 to vpad3 are pads[16..19]. A virtual pad
// takes input only between its connect and its disconnect.

#include <input/InputTypes.h>

#include <array>
#include <span>
#include <string>
#include <vector>

namespace app::detail {

	// Which virtual pads are connected, by vpad number.
	using VirtualPads = std::array<bool, foundation::kMaxVirtualGamepads>;

	struct ParsedInput {
		std::vector<foundation::InputEvent> events; // unstamped; the app's clock stamps them as they join
		std::string							error;	// empty when every event parsed
		std::string							failed; // the event that didn't
	};

	// All or nothing: on an error there are no events and `pads` is as it was; otherwise `pads` is as
	// the events leave it.
	ParsedInput parseInputEvents(std::span<const std::string> evs, VirtualPads& pads);

} // namespace app::detail
