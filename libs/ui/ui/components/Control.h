#pragma once

// What every pressable control shares (docs/ui-plan.md, 13b): it takes focus, shows the pointer
// cursor, answers the pointer as one unit, draws its background under the theme's state layers in
// R11.12's order (hover, pressed, active) with the focus ring outside its box while a player's focus
// shows one, draws whole at state.disabled.opacity while disabled, and has one activation: a click
// or a player's activate action, while it's enabled, and never one from inside an overlay it
// opened. A key or pad activation shows it pressed for state.pressed.hold, so the pressed look is
// the same however it's activated. The state layers ease through the animator (a StateColor), so
// hover in and out fades. A subclass reads the state with interaction() and hears it change in
// onInteractionChanged, for a look the layers don't draw (a lift on hover, a drop on press).
// Button, ListRow, Checkbox, Toggle, a radio option, a segment, a tab, a menu item and a select are
// Controls.

#include "ui/shapes/Rectangle.h"
#include "ui/theme/StateColor.h"

namespace ui {

	class Control : public Rectangle {
	  public:
		// Drawn with the active wash: open, selected or checked.
		[[nodiscard]] bool isActive() const { return active; }

		[[nodiscard]] bool handlesPointer() const override { return true; }
		// Disabled, it fades whole, fill, border, label and icon, so a clear fill still reads as disabled.
		[[nodiscard]] float stateOpacity() const override;
		void				describe(nlohmann::json& node) const override;

	  protected:
		void						onMount() override;
		void						render() override;
		void						handlePointer(PointerEvent& event) override;
		void						handleKey(KeyEvent& event) override;
		[[nodiscard]] PointerEvents defaultPointerEvents() const override { return PointerEvents::Unit; }

		// A click or the activate action; not called while disabled.
		virtual void activate() {}
		void		 setActive(bool value);
		// Whose activation activate() is running: the action's player, or for a click the player whose
		// focus the press gave it.
		[[nodiscard]] int activatingPlayer() const { return activator; }

		// Its state as it draws: hovered, pressed (held, or just activated by a key or pad), active,
		// and a player's focus on it.
		[[nodiscard]] InteractionState interaction() const;
		// The state changed from `previous` to interaction(), called in the event that changed it, once
		// it's mounted.
		virtual void onInteractionChanged(const InteractionState& /*previous*/) {}

	  private:
		void interactionChanged() override;
		// The pressed look for a key or pad activation, which has no release to end it.
		void pressBriefly();

		bool			 active = false;
		bool			 keyPressed = false;
		int				 activator = 0;
		TimerId			 keyRelease;
		InteractionState reported; // what onInteractionChanged last said it is
		StateColor		 background;
	};

} // namespace ui
