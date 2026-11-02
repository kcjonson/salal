#pragma once

// A colour that follows a component's interaction state through the animator (R11.13): R11.12's
// state layers over a base, eased over motion.duration.fast with motion.easing.standard on each
// change, so hover in and out fades rather than snapping, and under reduced motion it snaps. A
// widget holds one for each part whose colour has states, and draws with value().

#include "ui/Animator.h"
#include "ui/theme/Variants.h"

namespace ui {

	class Component;

	class StateColor {
	  public:
		// At `base` under `state` from the start, its tween owned by `owner`, which is mounted and holds
		// this as a member.
		void attach(Component& owner, Foundation::Color base, const InteractionState& state = {});
		void setBase(Foundation::Color base);
		void setState(const InteractionState& state);

		[[nodiscard]] Foundation::Color value() const;

	  private:
		[[nodiscard]] Foundation::Color target() const;

		Component*				 owner = nullptr;
		Foundation::Color		 base;
		InteractionState		 state;
		Tween<Foundation::Color> tween;
	};

} // namespace ui
