#include "ui/components/Panel.h"

#include "ui/Context.h"
#include "ui/theme/Tokens.h"

namespace ui {

	Panel::Panel(const Args& args)
		: title(args.title),
		  withActions(args.actions) {}

	StyleId Panel::themeStyle() const {
		return styles::pane.theme;
	}

	void Panel::onMount() {
		const Theme& theme = getContext()->getTheme();
		const float	 pad = theme.get(tokens::space::md);
		const float	 gap = theme.get(tokens::space::sm);
		Layout		 column = getLayout();
		column.display = Display::Flex;
		column.flexDirection = FlexDirection::Column;
		setLayout(column);
		if (!title.empty() || withActions) {
			Ref<Component> header = addChild<Component>(
				{.layout = {
					 .display = Display::Flex,
					 .gap = gap,
					 .padding = {pad, pad, 0.0F, pad},
					 .alignItems = AlignItems::Center,
				 }}
			);
			titleText = header.addChild<Text>({.layout = {.flexGrow = 1.0F}, .style = styles::text::title, .text = title});
			if (withActions) {
				actionsSlot =
					header.addChild<Component>({.layout = {.display = Display::Flex, .gap = gap, .alignItems = AlignItems::Center}});
			}
		}
		bodySlot = addChild<Component>({
			.layout = {
				.display = Display::Flex,
				.flexDirection = FlexDirection::Column,
				.gap = gap,
				.padding = pad,
				.alignItems = AlignItems::Start,
				.flexGrow = 1.0F
			},
		});
	}

	void Panel::setTitle(std::string value) {
		title = std::move(value);
		if (Text* shown = titleText.get()) {
			shown->setText(title);
		}
	}

} // namespace ui
