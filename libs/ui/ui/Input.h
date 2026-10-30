#pragma once

// Pointer input as ui delivers it (docs/ui-design.md, "Services and input routing"). app hands the top
// scene's context its platform events; the context finds the topmost painted component under the
// pointer in the one sorted list painting reads, delivers the event there and bubbles it to each
// ancestor until one consumes it. Enter and leave are derived from successive hits, and hover is
// checked again whenever layout moves content under a still pointer.

#include <input/InputTypes.h>
#include <math/Types.h>

#include <cstdint>

namespace ui {

	class Component;

	// Which of a component and its children the pointer can hit (R8.29). Auto: its box and its
	// children; Passthrough: only its children, a container's default; Unit: its box, never its
	// children, for a widget whose parts are its own business; None: neither.
	enum class PointerEvents : std::uint8_t { Auto, Passthrough, Unit, None };

	// The window's cursor over a component, from GLFW's standard shapes. Unset, a component shows its
	// parent's.
	enum class Cursor : std::uint8_t { Default, Pointer, Text, Crosshair, ResizeHorizontal, ResizeVertical, NotAllowed };

	struct PointerEvent {
		enum class Type : std::uint8_t { Down, Up, Move, Enter, Leave, Cancel, Wheel, Click };

		Type					type = Type::Move;
		Foundation::Vec2		screen{0.0F, 0.0F}; // in the ui's logical px
		Foundation::Vec2		local{0.0F, 0.0F};	// in the box of the component it is delivered to now
		foundation::MouseButton button = foundation::MouseButton::Left;
		Foundation::Vec2		wheel{0.0F, 0.0F}; // px to scroll: positive y moves the view down the content
		std::uint8_t			mods = 0;		   // foundation::Mod bits
		double					time = 0.0;
		// For this dispatch only: what it was aimed at, and what is handling it now.
		Component* target = nullptr;
		Component* currentTarget = nullptr;
		bool	   consumed = false;

		// Stops it bubbling further; a consumed press still produces its click.
		void consume() { consumed = true; }
	};

} // namespace ui
