#include "ui/components/Badge.h"

#include "ui/Context.h"
#include "ui/theme/Tokens.h"

namespace ui {

	namespace {
		constexpr float kHeight = 20.0F;
		constexpr float kPadX = 8.0F;
		constexpr float kDot = 6.0F;
		constexpr float kDotGap = 5.0F;
		constexpr float kBorderAlpha = 0.45F; // the tone's colour, faded, edges its wash
	} // namespace

	Badge::Badge(const Args& args)
		: label(args.label),
		  tone(args.tone),
		  dot(args.dot) {}

	StyleId Badge::themeStyle() const {
		return styles::badge.theme;
	}

	void Badge::onMount() {
		Layout row = getLayout();
		row.display = Display::Flex;
		row.flexDirection = FlexDirection::Row;
		row.alignItems = AlignItems::Center;
		row.gap = kDotGap;
		row.padding = {0.0F, kPadX};
		setLayout(row);
		if (getSizeSpec().height.isAuto()) {
			setSize({getSizeSpec().width, kHeight});
		}
		if (dot) {
			marker = addChild<Circle>({.size = {kDot, kDot}});
		}
		text = addChild<Text>({.text = label});
		tint();
	}

	void Badge::setStyle(const Style& value) {
		Rectangle::setStyle(value);
		tint();
	}

	void Badge::setLabel(std::string value) {
		label = std::move(value);
		if (Text* shown = text.get()) {
			shown->setText(label);
		}
	}

	void Badge::setTone(Tone value) {
		tone = value;
		tint();
	}

	void Badge::tint() {
		if (getContext() == nullptr) {
			return;
		}
		const ToneColors colors = toneColors(getContext()->getTheme(), tone);
		if (!getDeclaredStyle().backgroundColor) {
			style.backgroundColor = colors.subtle;
		}
		if (!getDeclaredStyle().borderColor) {
			style.borderColor = Foundation::Color(colors.color.r, colors.color.g, colors.color.b, colors.color.a * kBorderAlpha);
		}
		if (Circle* shown = marker.get()) {
			shown->setStyle({.backgroundColor = colors.color});
		}
		if (Text* shown = text.get()) {
			shown->setStyle(styles::label.extend({.color = colors.color, .textTransform = Foundation::TextTransform::Uppercase}));
		}
	}

} // namespace ui
