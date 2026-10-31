#include "ui/components/FadeIn.h"

#include "ui/Context.h"
#include "ui/theme/Tokens.h"

namespace ui {

	Tween<float> fadeIn(Component& component) {
		const Theme& theme = component.getContext()->getTheme();
		return component.tween<float>({
			.from = 0.0F,
			.to = 1.0F,
			.durationMs = theme.get(tokens::motion::duration::fast),
			.easing = Easing(theme.get(tokens::motion::easing::enter)),
			.onUpdate = [&component](const float& alpha) {
				Style faded = component.getDeclaredStyle();
				faded.opacity = alpha * faded.opacity.value_or(1.0F);
				component.setStyle(faded);
			},
		});
	}

} // namespace ui
