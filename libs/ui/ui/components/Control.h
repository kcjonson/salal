#pragma once

// What every pressable control shares (docs/ui-plan.md, 13b): it takes focus, shows the pointer
// cursor, answers the pointer as one unit, draws its background under the theme's state layers in
// R11.12's order (hover, pressed, active, disabled) with the focus ring outside its box while a
// player's focus shows one, and has one activation: a click or a player's activate action, while
// it's enabled. Button, ListRow, Checkbox, Toggle, a radio option, a segment and a tab are Controls.

#include "ui/shapes/Rectangle.h"

namespace ui {

	class Control : public Rectangle {
	  public:
		// Drawn with the active wash: open, selected or checked.
		[[nodiscard]] bool isActive() const { return active; }

		[[nodiscard]] bool handlesPointer() const override { return true; }

	  protected:
		void						onMount() override;
		void						render() override;
		void						handlePointer(PointerEvent& event) override;
		void						handleKey(KeyEvent& event) override;
		[[nodiscard]] PointerEvents defaultPointerEvents() const override { return PointerEvents::Unit; }

		// A click or the activate action; not called while disabled.
		virtual void activate() {}
		void		 setActive(bool value) { active = value; }

	  private:
		bool active = false;
	};

} // namespace ui
