#include "ui/components/DropdownButton.h"

#include "ui/Context.h"

#include <algorithm>

namespace ui {

	namespace {
		constexpr float kMinMenuWidth = 160.0F;
	} // namespace

	DropdownButton::DropdownButton(const Args& args)
		: Button(Button::Args{.label = args.label, .icon = args.icon, .variant = args.variant, .controlSize = args.controlSize}),
		  items(args.items),
		  side(args.side) {}

	void DropdownButton::onMount() {
		Button::onMount();
		caret = addChild<Chevron>({.style = {.color = getStyle().color}});
	}

	void DropdownButton::setStyle(const Style& declared) {
		Button::setStyle(declared);
		if (Chevron* shown = caret.get()) {
			shown->setStyle({.color = getStyle().color});
		}
	}

	void DropdownButton::openMenu(int player) {
		if (isOpen() || items.empty()) {
			return;
		}
		menu = Menu::open(*this, {.size = {std::max(getSize().x, kMinMenuWidth), kAuto}, .items = items}, Anchored{.side = side}, player);
	}

	void DropdownButton::closeMenu() {
		if (isOpen()) {
			getContext()->closeOverlay(menu);
		}
	}

	void DropdownButton::setItems(std::vector<MenuItem> value) {
		items = std::move(value);
		closeMenu();
	}

	void DropdownButton::activate() {
		if (isOpen()) {
			closeMenu();
		} else {
			openMenu(activatingPlayer());
		}
	}

} // namespace ui
