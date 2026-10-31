#include "ui/components/Button.h"

#include "ui/Context.h"
#include "ui/theme/Tokens.h"

namespace ui {

	namespace {
		constexpr float kGap = 6.0F;
		constexpr float kPadX = 12.0F;
		constexpr float kIconGrow = 2.0F; // an icon a little larger than the label's font
	} // namespace

	Button::Button(const Args& args)
		: label(args.label),
		  iconName(args.icon),
		  variant(args.variant),
		  controlSize(args.controlSize),
		  onPress(args.onPress) {}

	StyleId Button::themeStyle() const {
		return styles::button.theme;
	}

	void Button::onMount() {
		Control::onMount();
		const SizeMetrics metrics = sizeMetrics(getContext()->getTheme(), controlSize);
		Layout			  row = getLayout();
		row.display = Display::Flex;
		row.flexDirection = FlexDirection::Row;
		row.alignItems = AlignItems::Center;
		row.justifyContent = JustifyContent::Center;
		row.gap = kGap;
		row.padding = {0.0F, kPadX};
		setLayout(row);
		if (getSizeSpec().height.isAuto()) {
			setSize({getSizeSpec().width, metrics.controlHeight});
		}
		if (!iconName.empty()) {
			const float side = metrics.fontSize + kIconGrow;
			icon = addChild<Icon>({.size = {side, side}, .name = iconName});
		}
		if (!label.empty()) {
			text = addChild<Text>({.text = label});
		}
		tint();
	}

	void Button::setStyle(const Style& declared) {
		Control::setStyle(declared);
		tint();
	}

	void Button::setLabel(std::string value) {
		label = std::move(value);
		if (Text* shown = text.get()) {
			shown->setText(label);
		}
	}

	void Button::activate() {
		if (onPress) {
			onPress();
		}
	}

	void Button::tint() {
		if (getContext() == nullptr) {
			return;
		}
		const Theme& theme = getContext()->getTheme();
		const Style& given = getDeclaredStyle();
		if (variant == ButtonVariant::Primary || variant == ButtonVariant::Danger) {
			const ToneColors tone = toneColors(theme, variant == ButtonVariant::Primary ? Tone::Accent : Tone::Danger);
			if (!given.backgroundColor) {
				style.backgroundColor = tone.color;
			}
			if (!given.borderColor) {
				style.borderColor = tone.color;
			}
			if (!given.color) {
				style.color = tone.onColor;
			}
		} else if (variant == ButtonVariant::Ghost) {
			if (!given.backgroundColor) {
				style.backgroundColor = Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F);
			}
			if (!given.borderWidth) {
				style.borderWidth = 0.0F;
			}
		}
		// A label is its own node, so it takes the button's text fields.
		const Style labelStyle{
			.color = style.color,
			.fontFamily = style.fontFamily,
			.fontSize = sizeMetrics(theme, controlSize).fontSize,
			.letterSpacing = style.letterSpacing,
		};
		if (Text* shown = text.get()) {
			shown->setStyle(labelStyle);
		}
		if (Icon* shown = icon.get()) {
			shown->setStyle({.color = style.color});
		}
	}

} // namespace ui
