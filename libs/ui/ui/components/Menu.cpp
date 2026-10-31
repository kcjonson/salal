#include "ui/components/Menu.h"

#include "ui/Context.h"
#include "ui/components/FadeIn.h"
#include "ui/shapes/Icon.h"
#include "ui/shapes/Text.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/Variants.h"

namespace ui {

	namespace {

		constexpr float kPad = 4.0F;
		constexpr float kPadX = 8.0F;
		constexpr float kGap = 8.0F;
		constexpr float kIcon = 14.0F;
		constexpr float kMinWidth = 160.0F;

		class MenuRow : public Control {
		  public:
			struct Args {
				UI_COMMON_ARGS
				MenuItem	item;
				std::size_t index = 0;
			};

			explicit MenuRow(const Args& args)
				: item(args.item),
				  index(args.index) {}

			[[nodiscard]] const char* typeName() const override { return "MenuItem"; }
			[[nodiscard]] StyleId	  themeStyle() const override { return styles::menuItem.theme; }

		  protected:
			void onMount() override {
				Control::onMount();
				setLayout({.display = Display::Flex, .gap = kGap, .padding = {0.0F, kPadX, 0.0F, kPadX}, .alignItems = AlignItems::Center});
				setSize({kAuto, sizeMetrics(getContext()->getTheme(), ControlSize::Sm).controlHeight});
				setEnabled(item.enabled);
				setActive(item.selected);
				const Style text = item.enabled ? styles::text::body : styles::text::secondary;
				if (!item.icon.empty()) {
					addChild<Icon>({.size = {kIcon, kIcon}, .style = item.enabled ? Style{} : styles::text::secondary, .name = item.icon});
				}
				addChild<Text>({.layout = {.flexGrow = 1.0F}, .style = text, .text = item.label});
				if (!item.hint.empty()) {
					addChild<Text>({.style = styles::label, .text = item.hint});
				}
			}

			void activate() override {
				if (auto* menu = dynamic_cast<Menu*>(parent().get())) {
					menu->choose(index);
				}
			}

		  private:
			MenuItem	item;
			std::size_t index;
		};

	} // namespace

	Menu::Menu(const Args& args)
		: items(args.items) {}

	Ref<Menu> Menu::open(Component& opener, const Args& args, Anchored placement, int player) {
		Ref<Menu> menu = opener.openOverlay<Menu>(args, {.kind = OverlayKind::Menu, .player = player, .placement = placement});
		if (Menu* shown = menu.get()) {
			shown->opened(player);
		}
		return menu;
	}

	void Menu::opened(int player) {
		appear = fadeIn(*this);
		focusItem(player);
	}

	StyleId Menu::themeStyle() const {
		return styles::overlay.theme;
	}

	void Menu::onMount() {
		Layout column = getLayout();
		column.display = Display::Flex;
		column.flexDirection = FlexDirection::Column;
		column.padding = kPad;
		column.alignItems = AlignItems::Stretch;
		setLayout(column);
		if (getSizeSpec().width.isAuto()) {
			setMinSize({kMinWidth, kAuto});
		}
		if (!getFocusGroup()) {
			setFocusGroup(FocusGroup{.orientation = FocusGroup::Orientation::Vertical, .wrap = true});
		}
		build();
	}

	void Menu::setItems(std::vector<MenuItem> value) {
		items = std::move(value);
		build();
	}

	void Menu::build() {
		for (const Ref<Control>& old : rows) {
			removeChild(old);
		}
		rows.clear();
		for (std::size_t i = 0; i < items.size(); ++i) {
			rows.push_back(addChild<MenuRow>({.item = items[i], .index = i}));
		}
	}

	Ref<Control> Menu::row(std::size_t index) const {
		return index < rows.size() ? rows[index] : Ref<Control>();
	}

	void Menu::choose(std::size_t index) {
		if (index >= items.size() || !items[index].enabled) {
			return;
		}
		const std::function<void()> onSelect = items[index].onSelect;
		if (isOverlay()) {
			getContext()->closeOverlay(self());
		}
		if (onSelect) {
			onSelect();
		}
	}

	void Menu::focusItem(int player) {
		const auto pick = [&](bool selectedOnly) {
			for (std::size_t i = 0; i < items.size(); ++i) {
				if (items[i].enabled && (!selectedOnly || items[i].selected)) {
					return getContext()->focus(rows[i], player);
				}
			}
			return false;
		};
		if (!pick(true)) {
			pick(false);
		}
	}

} // namespace ui
