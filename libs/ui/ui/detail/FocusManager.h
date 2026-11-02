#pragma once

// A context's focus, internal to ui (ui/Focus.h has the contract): each player's focused component,
// their scopes and where focus goes back to, the devices and navigation map that drive them, and the
// input mode.

#include "ui/Component.h"

#include <input/ActionMap.h>
#include <input/InputTypes.h>

#include <array>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace ui {
	class Context;
}

namespace ui::detail {

	class FocusManager {
	  public:
		explicit FocusManager(Context& owner);

		// A key, character, pad button or axis event; true when the ui took it.
		bool dispatch(const foundation::InputEvent& event);
		// The mouse pressed `hit`, none for empty space: its player's focus goes to the nearest focusable
		// at or above it, with no ring, or off the ui. Inside a scope a press can't take focus out, and a
		// press in an overlay with nothing to focus leaves focus where it is.
		void pointerPressed(ComponentId hit);
		// The player holding the mouse, else player 0.
		[[nodiscard]] int mousePlayer() const;

		bool		focus(ComponentId target, int player);
		void		clear(int player);
		ComponentId focused(int player) const;
		bool		next(int player, bool backward);
		bool		direction(int player, Direction direction);
		void		pushScope(ComponentId root, int player);
		void		popScope(int player);
		void		setNavigation(int player, foundation::ActionMap map);
		void		setDevices(int player, std::span<const foundation::DeviceId> devices);
		InputMode	inputMode() const { return mode; }

		// `root` and its subtree are about to go: focus in it is dropped with no callbacks (R9.21), and
		// a scope rooted in it closes.
		void removing(ComponentId root);
		// After the layout pass: focus that can't be held any more moves on (R9.28), and focus that
		// arrived by keys, a pad or code scrolls into view (R9.22).
		void afterLayout();

	  private:
		enum class Action : std::uint8_t { None, Activate, Cancel, Up, Down, Left, Right, Next, Previous };

		struct Scope {
			ComponentId root;
			ComponentId restore; // the focus before it opened
		};
		struct Player {
			ComponentId							 focused;
			bool								 visible = false;
			bool								 lost = false; // its focus went with a removal; afterLayout puts it back
			ComponentId							 restore;	   // where afterLayout puts it back to
			ComponentId							 reveal;	   // to scroll into view after the layout
			std::vector<Scope>					 scopes;
			std::optional<foundation::ActionMap> navigation; // defaultNavigation() while unset
			std::vector<foundation::DeviceId>	 devices;
		};
		struct AxisValue {
			foundation::DeviceId	device;
			foundation::GamepadAxis axis;
			float					value;
		};

		bool keyDown(const foundation::InputEvent& event);
		bool keyOther(const foundation::InputEvent& event);
		bool padAction(const foundation::InputEvent& event, float previous);
		// The action, its event delivered to the focused component first; a Navigate nobody consumed
		// moves focus.
		bool act(int player, Action action, const KeyEvent& base);
		// To the target, then up through its ancestors until one consumes it.
		bool deliver(ComponentId target, KeyEvent& event);
		// Blur on the old, focus on the new; a focus group above the new remembers it.
		void move(int player, ComponentId to, bool visible, bool reveal);

		// The player an event drives and its action: among the players holding the device, the first
		// whose map binds it, else the first. -1 when nobody holds it.
		[[nodiscard]] std::pair<int, Action> route(const foundation::InputEvent& event, float previous) const;
		[[nodiscard]] bool					 holds(int player, foundation::DeviceId device) const;
		[[nodiscard]] Action actionOf(const foundation::ActionMap& map, const foundation::InputEvent& event, float previous) const;
		float&				 axisValue(foundation::DeviceId device, foundation::GamepadAxis axis);

		[[nodiscard]] Component*  find(ComponentId id) const;
		[[nodiscard]] bool		  within(ComponentId id, ComponentId root) const;
		[[nodiscard]] ComponentId scopeRoot(int player) const;
		[[nodiscard]] bool		  canFocus(const Component& component, int player) const;
		// The Tab stops in the player's scope, in Tab order: a focus group is one, its member focused
		// last or its first. `blocked`: under a descendantsFocusable false, where only overlays count.
		[[nodiscard]] std::vector<Component*> stops(int player) const;
		void					 collectStops(Component& node, std::vector<Component*>& out, bool self, bool blocked = false) const;
		[[nodiscard]] Component* groupStop(Component& group) const;
		// Every focusable that takes Tab, groups' members included, for directional focus.
		void					 collectFocusable(Component& node, std::vector<Component*>& out, bool blocked = false) const;
		[[nodiscard]] Component* groupOf(const Component& component, int player) const;
		// A step inside `group`, along its orientation; none at an end without wrap, or off its axis.
		[[nodiscard]] Component* stepInGroup(Component& group, Component& from, Direction direction) const;
		// Lowest score that way on screen: the gap along the direction, twice the offset across it, less
		// the overlap across it.
		[[nodiscard]] Component* nearest(const Component& from, Direction direction, std::span<Component* const> candidates) const;
		// Entering a focus group from outside lands on its member focused last.
		[[nodiscard]] Component* resolve(Component& candidate, const Component& from) const;
		void					 scrollIntoView(Component& target);

		Context&						context;
		std::array<Player, kMaxPlayers> players;
		foundation::ActionMap			defaults;
		std::vector<AxisValue>			axes;
		InputMode						mode = InputMode::Pointer;
	};

} // namespace ui::detail
