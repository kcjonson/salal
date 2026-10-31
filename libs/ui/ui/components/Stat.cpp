#include "ui/components/Stat.h"

#include "ui/Context.h"
#include "ui/theme/Tokens.h"

namespace ui {

	namespace {
		constexpr float kGap = 2.0F;
		constexpr float kUnitScale = 0.62F; // of the value's size
	} // namespace

	Stat::Stat(const Args& args)
		: label(args.label),
		  value(args.value),
		  unit(args.unit),
		  tone(args.tone),
		  controlSize(args.controlSize) {}

	void Stat::onMount() {
		Layout column = getLayout();
		column.display = Display::Flex;
		column.flexDirection = FlexDirection::Column;
		column.alignItems = AlignItems::Start;
		column.gap = kGap;
		setLayout(column);
		if (!label.empty()) {
			addChild<Text>({.style = styles::label.extend({.textTransform = Foundation::TextTransform::Uppercase}), .text = label});
		}
		Ref<Component> row = addChild<Component>(
			{.layout = {.display = Display::Flex, .flexDirection = FlexDirection::Row, .gap = kGap, .alignItems = AlignItems::End}}
		);
		valueText = row.addChild<Text>({.text = value});
		if (!unit.empty()) {
			unitText = row.addChild<Text>({.text = unit});
		}
		restyleValue();
	}

	void Stat::setStyle(const Style& declared) {
		Component::setStyle(declared);
		restyleValue();
	}

	void Stat::setValue(std::string text) {
		value = std::move(text);
		if (Text* shown = valueText.get()) {
			shown->setText(value);
		}
	}

	void Stat::setTone(Tone toneValue) {
		tone = toneValue;
		restyleValue();
	}

	void Stat::restyleValue() {
		if (getContext() == nullptr) {
			return;
		}
		const Theme& theme = getContext()->getTheme();
		float		 size = theme.get(tokens::stat::valueSize::md);
		if (controlSize == ControlSize::Sm) {
			size = theme.get(tokens::stat::valueSize::sm);
		} else if (controlSize == ControlSize::Lg) {
			size = theme.get(tokens::stat::valueSize::lg);
		}
		Style valueStyle = styles::statValue.extend({.fontSize = size});
		if (tone != Tone::Default) {
			valueStyle.color = toneColors(theme, tone).color;
		}
		if (Text* shown = valueText.get()) {
			shown->setStyle(valueStyle);
		}
		if (Text* shown = unitText.get()) {
			shown->setStyle(styles::text::secondary.extend({.fontSize = size * kUnitScale}));
		}
	}

} // namespace ui
