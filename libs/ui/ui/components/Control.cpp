#include "ui/components/Control.h"

#include "ui/Context.h"
#include "ui/theme/Variants.h"

#include <primitives/Primitives.h>

namespace ui {

	void Control::onMount() {
		setFocusable(true);
		if (!getCursor()) {
			setCursor(Cursor::Pointer);
		}
	}

	void Control::render() {
		const Theme&			theme = getContext()->getTheme();
		const InteractionState	state{.hovered = isHovered(), .pressed = isPressed(), .active = active, .disabled = !isEnabledInTree()};
		const Foundation::Color base = style.backgroundColor.value_or(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F));
		const std::optional<Foundation::Color> declared = style.backgroundColor;
		style.backgroundColor = applyStateLayers(theme, base, state);
		Rectangle::render();
		style.backgroundColor = declared;

		if (!isFocusVisible()) {
			return;
		}
		const FocusRing ring = focusRing(theme);
		const float		reach = ring.offset + ring.width;
		Renderer::Primitives::drawRect({
			.bounds = {-reach, -reach, size.x + 2.0F * reach, size.y + 2.0F * reach},
			.style =
				{.fill = Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F),
				 .border =
					 Foundation::BorderStyle{
						 .color = ring.color,
						 .width = ring.width,
						 .cornerRadius = style.borderRadius.value_or(0.0F) + reach,
						 .position = Foundation::BorderPosition::Inside,
					 }},
			.key = drawKey(),
		});
	}

	void Control::handlePointer(PointerEvent& event) {
		if (event.type == PointerEvent::Type::Click && event.button == foundation::MouseButton::Left && isEnabledInTree()) {
			activate();
			event.consume();
		}
	}

	void Control::handleKey(KeyEvent& event) {
		if (event.type == KeyEvent::Type::Activate && isEnabledInTree()) {
			activate();
			event.consume();
		}
	}

} // namespace ui
