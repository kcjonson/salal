#include "ui/components/TabBar.h"

#include "ui/Context.h"
#include "ui/shapes/Text.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/Variants.h"

namespace ui {

	namespace {

		constexpr float kPadX = 12.0F;

		class TabControl : public Control {
		  public:
			struct Args {
				UI_COMMON_ARGS
				Tab tab;
			};

			explicit TabControl(const Args& args)
				: tab(args.tab) {}

			[[nodiscard]] const std::string& tabId() const { return tab.id; }
			[[nodiscard]] const char*		 typeName() const override { return "Tab"; }
			[[nodiscard]] StyleId			 themeStyle() const override { return styles::tab.theme; }

			void show(bool chosen, const Theme& theme) {
				setActive(chosen);
				setStyle(chosen ? Style{.color = theme.get(tokens::color::text::primary)} : Style{});
				if (Text* shown = text.get()) {
					shown->setStyle({.color = getStyle().color, .fontFamily = getStyle().fontFamily, .fontSize = getStyle().fontSize});
				}
				if (Rectangle* shown = underline.get()) {
					shown->setVisible(chosen);
					shown->setStyle({.backgroundColor = theme.get(tokens::tone::accent::color)});
				}
			}

		  protected:
			void onMount() override {
				Control::onMount();
				const Theme& theme = getContext()->getTheme();
				setLayout({.display = Display::Flex, .padding = {0.0F, kPadX}, .alignItems = AlignItems::Center});
				setSize({kAuto, sizeMetrics(theme, ControlSize::Md).controlHeight});
				underline = addChild<Rectangle>({
					.size = {kAuto, theme.get(tokens::lineWidth::thick)},
					.layout = {.position = Position::Absolute, .inset = {.right = 0.0F, .bottom = 0.0F, .left = 0.0F}},
				});
				text = addChild<Text>({.text = tab.label});
			}

			void activate() override {
				if (auto* bar = dynamic_cast<TabBar*>(parent().get())) {
					bar->choose(tab.id);
				}
			}

			// A selected tab shows by its underline and its label, not a wash.
			void render() override {
				const bool chosen = isActive();
				setActive(false);
				Control::render();
				setActive(chosen);
			}

		  private:
			Tab			   tab;
			Ref<Text>	   text;
			Ref<Rectangle> underline;
		};

	} // namespace

	TabBar::TabBar(const Args& args)
		: tabs(args.tabs),
		  selectedId(args.selectedId.empty() && !args.tabs.empty() ? args.tabs.front().id : args.selectedId),
		  onSelect(args.onSelect) {}

	void TabBar::onMount() {
		const Theme& theme = getContext()->getTheme();
		Layout		 row = getLayout();
		row.display = Display::Flex;
		row.flexDirection = FlexDirection::Row;
		row.alignItems = AlignItems::End;
		setLayout(row);
		setFocusGroup(FocusGroup{.orientation = FocusGroup::Orientation::Horizontal, .wrap = true});
		addChild<Rectangle>({
			.size = {kAuto, theme.get(tokens::lineWidth::thin)},
			.layout = {.position = Position::Absolute, .inset = {.right = 0.0F, .bottom = 0.0F, .left = 0.0F}},
			.style = styles::divider,
		});
		for (const Tab& tab : tabs) {
			tabControls.push_back(addChild<TabControl>({.id = "tab_" + tab.id, .tab = tab}));
		}
		show();
	}

	void TabBar::setStyle(const Style& declared) {
		Component::setStyle(declared);
		show();
	}

	void TabBar::setSelectedId(std::string id) {
		selectedId = std::move(id);
		show();
	}

	void TabBar::choose(const std::string& id) {
		if (id == selectedId) {
			return;
		}
		setSelectedId(id);
		if (onSelect) {
			onSelect(selectedId);
		}
	}

	void TabBar::show() {
		if (getContext() == nullptr) {
			return;
		}
		for (const Ref<Control>& ref : tabControls) {
			if (auto* tab = static_cast<TabControl*>(ref.get())) {
				tab->show(tab->tabId() == selectedId, getContext()->getTheme());
			}
		}
	}

} // namespace ui
