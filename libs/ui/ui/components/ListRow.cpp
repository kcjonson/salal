#include "ui/components/ListRow.h"

#include "ui/Context.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/Variants.h"

namespace ui {

	namespace {
		constexpr float kPadX = 8.0F;
		constexpr float kGap = 8.0F;
		constexpr float kIcon = 14.0F;
	} // namespace

	ListRow::ListRow(const Args& args)
		: label(args.label),
		  trailing(args.trailing),
		  iconName(args.icon),
		  dim(args.dim),
		  indent(args.indent),
		  selectedAtStart(args.selected),
		  onPress(args.onPress) {}

	StyleId ListRow::themeStyle() const {
		return styles::listRow.theme;
	}

	void ListRow::onMount() {
		Control::onMount();
		const Theme& theme = getContext()->getTheme();
		Layout		 row = getLayout();
		row.display = Display::Flex;
		row.flexDirection = FlexDirection::Row;
		row.alignItems = AlignItems::Center;
		row.gap = kGap;
		row.padding = {0.0F, kPadX, 0.0F, kPadX + indent};
		setLayout(row);
		if (getSizeSpec().height.isAuto()) {
			setSize({getSizeSpec().width, sizeMetrics(theme, ControlSize::Sm).controlHeight});
		}
		const float hairline = theme.get(tokens::lineWidth::thin);
		addChild<Rectangle>({
			.size = {kAuto, hairline},
			.layout = {.position = Position::Absolute, .inset = {.right = 0.0F, .bottom = 0.0F, .left = 0.0F}},
			.style = styles::divider,
		});
		bar = addChild<Rectangle>({
			.size = {theme.get(tokens::lineWidth::thick), kAuto},
			.layout = {.position = Position::Absolute, .inset = {.top = 0.0F, .bottom = 0.0F, .left = 0.0F}},
			.style = {.backgroundColor = theme.get(tokens::tone::accent::color)},
		});
		if (!iconName.empty()) {
			shownIcon = addChild<Icon>({.size = {kIcon, kIcon}, .style = dim ? styles::text::secondary : Style{}, .name = iconName});
		}
		text = addChild<Text>({.layout = {.flexGrow = 1.0F}, .style = dim ? styles::text::secondary : styles::text::body, .text = label});
		trailingText = addChild<Text>({.style = styles::label, .text = trailing});
		setSelected(selectedAtStart);
	}

	void ListRow::setSelected(bool value) {
		setActive(value);
		if (Rectangle* shown = bar.get()) {
			shown->setVisible(value);
		}
	}

	void ListRow::setLabel(std::string value) {
		label = std::move(value);
		if (Text* shown = text.get()) {
			shown->setText(label);
		}
	}

	void ListRow::setDim(bool value) {
		if (value == dim) {
			return;
		}
		dim = value;
		if (Icon* shown = shownIcon.get()) {
			shown->setStyle(dim ? styles::text::secondary : Style{});
		}
		if (Text* shown = text.get()) {
			shown->setStyle(dim ? styles::text::secondary : styles::text::body);
		}
	}

	void ListRow::setTrailing(std::string value) {
		trailing = std::move(value);
		if (Text* shown = trailingText.get()) {
			shown->setText(trailing);
		}
	}

	void ListRow::activate() {
		if (onPress) {
			onPress();
		}
	}

} // namespace ui
