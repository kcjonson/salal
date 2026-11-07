#include "ui/components/Control.h"

#include "ui/Context.h"
#include "ui/detail/Snapshot.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/Variants.h"

#include <primitives/Primitives.h>

#include <utility>

namespace ui {

	namespace {
		// An event from inside an overlay the control opened isn't its own: a click on a menu's padding,
		// or an activate a dialog's content didn't take.
		bool fromOverlay(const Component& control, const Component* target) {
			for (const Component* node = target; node != nullptr && node != &control; node = node->parent().get()) {
				if (node->isOverlay()) {
					return true;
				}
			}
			return false;
		}
	} // namespace

	void Control::describe(nlohmann::json& node) const {
		Rectangle::describe(node);
		node["state"]["active"] = active;
		node["state"]["pressed"] = interaction().pressed; // a key's moment of press too
	}

	void Control::onMount() {
		setFocusable(true);
		if (!getCursor()) {
			setCursor(Cursor::Pointer);
		}
		reported = interaction();
		background.attach(*this, style.backgroundColor.value_or(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F)), reported);
	}

	InteractionState Control::interaction() const {
		return {
			.hovered = isHovered(),
			.pressed = isPressed() || keyPressed,
			.active = active,
			.focused = isFocused(),
			.focusVisible = isFocusVisible(),
			.dragging = isDragging(),
			.dropActive = isDropActive(),
		};
	}

	void Control::setActive(bool value) {
		if (active != value) {
			active = value;
			interactionChanged();
		}
	}

	void Control::interactionChanged() {
		const InteractionState now = interaction();
		if (getContext() != nullptr && now != reported) {
			const InteractionState previous = std::exchange(reported, now);
			onInteractionChanged(previous);
		}
	}

	void Control::pressBriefly() {
		Context* owner = getContext();
		if (owner == nullptr) {
			return;
		}
		owner->getAnimator().cancel(keyRelease);
		keyPressed = true;
		interactionChanged();
		keyRelease = after(owner->getTheme().get(tokens::state::pressed::hold), [this] {
			keyPressed = false;
			interactionChanged();
		});
	}

	float Control::stateOpacity() const {
		return isEnabledInTree() ? 1.0F : getContext()->getTheme().get(tokens::state::disabled::opacity);
	}

	void Control::render() {
		const Theme& theme = getContext()->getTheme();
		// Retargeted as it draws, which is a no-op while nothing changed; the tween ticks in the next update.
		background.setBase(style.backgroundColor.value_or(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F)));
		background.setState(interaction());
		const std::optional<Foundation::Color> declared = style.backgroundColor;
		style.backgroundColor = background.value();
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
		if (event.type == PointerEvent::Type::Click && event.button == foundation::MouseButton::Left && isEnabledInTree() &&
			!fromOverlay(*this, event.target)) {
			activator = 0;
			for (int player = 0; player < kMaxPlayers; ++player) {
				if (isFocusedBy(player)) {
					activator = player;
					break;
				}
			}
			activate();
			event.consume();
		}
	}

	void Control::handleKey(KeyEvent& event) {
		if (event.type == KeyEvent::Type::Activate && isEnabledInTree() && !fromOverlay(*this, event.target)) {
			activator = event.player;
			pressBriefly();
			activate();
			event.consume();
		}
	}

} // namespace ui
