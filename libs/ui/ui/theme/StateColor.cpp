#include "ui/theme/StateColor.h"

#include "ui/Context.h"
#include "ui/theme/Tokens.h"

namespace ui {

	void StateColor::attach(Component& component, Foundation::Color color, const InteractionState& interaction) {
		owner = &component;
		base = color;
		state = interaction;
		const Foundation::Color start = target();
		tween = component.tween<Foundation::Color>({.from = start, .to = start});
	}

	void StateColor::setBase(Foundation::Color color) {
		base = color;
		setState(state);
	}

	void StateColor::setState(const InteractionState& interaction) {
		state = interaction;
		if (owner == nullptr || owner->getContext() == nullptr) {
			return;
		}
		const Theme& theme = owner->getContext()->getTheme();
		tween.retarget(target(), theme.get(tokens::motion::duration::fast), Easing(theme.get(tokens::motion::easing::standard)));
	}

	Foundation::Color StateColor::value() const {
		return owner != nullptr ? tween.value() : base;
	}

	Foundation::Color StateColor::target() const {
		const Context* context = owner != nullptr ? owner->getContext() : nullptr;
		return context != nullptr ? applyStateLayers(context->getTheme(), base, state) : base;
	}

} // namespace ui
