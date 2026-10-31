#include "ui/components/ProgressBar.h"

#include "ui/Context.h"
#include "ui/theme/Tokens.h"

#include <algorithm>

namespace ui {

	namespace {
		constexpr float kHeaderGap = 4.0F;
		constexpr float kInlinePadX = 8.0F;
		constexpr float kInlineWash = 0.22F; // the fill's alpha behind an inline label

		float trackHeight(ControlSize size, bool inlineLabel) {
			if (inlineLabel) {
				return size == ControlSize::Sm ? 16.0F : 18.0F;
			}
			return size == ControlSize::Sm ? 5.0F : 8.0F;
		}
	} // namespace

	ProgressBar::ProgressBar(const Args& args)
		: value(std::clamp(args.value, 0.0F, 1.0F)),
		  tone(args.tone),
		  label(args.label),
		  valueText(args.valueText),
		  controlSize(args.controlSize),
		  inlineLabel(args.inlineLabel) {}

	void ProgressBar::onMount() {
		Layout column = getLayout();
		column.display = Display::Flex;
		column.flexDirection = FlexDirection::Column;
		column.gap = kHeaderGap;
		setLayout(column);
		if (getSizeSpec().width.isAuto()) {
			setSize({percent(100.0F), getSizeSpec().height});
		}
		const Style	   labelStyle = styles::label.extend({.textTransform = Foundation::TextTransform::Uppercase});
		const bool	   header = !inlineLabel && (!label.empty() || !valueText.empty());
		Ref<Component> textRow;
		if (header) {
			textRow = addChild<Component>({.layout = {.display = Display::Flex, .justifyContent = JustifyContent::SpaceBetween}});
		}

		const float height = trackHeight(controlSize, inlineLabel);
		const float radius = inlineLabel ? getContext()->getTheme().get(tokens::radius::sm) : height * 0.5F;
		Layout		trackLayout;
		if (inlineLabel) {
			trackLayout = {
				.display = Display::Flex,
				.padding = {0.0F, kInlinePadX},
				.alignItems = AlignItems::Center,
				.justifyContent = JustifyContent::SpaceBetween,
			};
		}
		Ref<Rectangle> track = addChild<Rectangle>(
			{.size = {kAuto, height}, .layout = trackLayout, .style = styles::progressTrack.extend({.borderRadius = radius})}
		);
		// First in the track, so the inline label and value draw over it.
		bar = track.addChild<Rectangle>({
			.size = {percent(value * 100.0F), percent(100.0F)},
			.layout = {.position = Position::Absolute, .inset = {.top = 0.0F, .left = 0.0F}},
			.style = {.borderRadius = radius},
		});
		if (inlineLabel) {
			textRow = track;
		}
		if (textRow.get() != nullptr) {
			shownLabel = textRow.addChild<Text>({.style = labelStyle, .text = label});
			shownValue = textRow.addChild<Text>({.text = valueText});
		}
		fill();
	}

	void ProgressBar::setStyle(const Style& declared) {
		Component::setStyle(declared);
		fill();
	}

	void ProgressBar::setValue(float fraction) {
		value = std::clamp(fraction, 0.0F, 1.0F);
		fill();
	}

	void ProgressBar::setTone(Tone toneValue) {
		tone = toneValue;
		fill();
	}

	void ProgressBar::setValueText(std::string text) {
		valueText = std::move(text);
		if (Text* shown = shownValue.get()) {
			shown->setText(valueText);
		}
	}

	void ProgressBar::setLabel(std::string text) {
		label = std::move(text);
		if (Text* shown = shownLabel.get()) {
			shown->setText(label);
		}
	}

	void ProgressBar::fill() {
		Rectangle* shown = bar.get();
		if (shown == nullptr || getContext() == nullptr) {
			return;
		}
		Foundation::Color color = toneColors(getContext()->getTheme(), tone, value).color;
		if (Text* text = shownValue.get()) {
			text->setStyle(styles::label.extend({.color = color}));
		}
		if (inlineLabel) {
			color.a *= kInlineWash;
		}
		shown->setStyle(shown->getDeclaredStyle().extend({.backgroundColor = color}));
		shown->setSize({percent(value * 100.0F), percent(100.0F)});
	}

} // namespace ui
