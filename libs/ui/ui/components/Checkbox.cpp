#include "ui/components/Checkbox.h"

#include "ui/Context.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/Variants.h"

namespace ui {

	namespace {
		constexpr float kBox = 16.0F;
		constexpr float kGap = 8.0F;
		constexpr float kTick = 2.0F;
	} // namespace

	Checkbox::Checkbox(const Args& args)
		: label(args.label),
		  checked(args.checked),
		  onChange(args.onChange) {}

	StyleId Checkbox::themeStyle() const {
		return styles::listRow.theme; // the row itself has no fill; the box carries the look
	}

	void Checkbox::onMount() {
		Control::onMount();
		Layout row = getLayout();
		row.display = Display::Flex;
		row.flexDirection = FlexDirection::Row;
		row.alignItems = AlignItems::Center;
		row.gap = kGap;
		setLayout(row);
		box = addChild<Rectangle>({.size = {kBox, kBox}});
		tickShort = box.addChild<Line>({.start = {3.5F, 8.0F}, .end = {6.5F, 11.0F}, .lineWidth = kTick, .roundCaps = true});
		tickLong = box.addChild<Line>({.start = {6.5F, 11.0F}, .end = {12.5F, 4.5F}, .lineWidth = kTick, .roundCaps = true});
		if (!label.empty()) {
			addChild<Text>({.style = styles::text::body, .text = label});
		}
		show();
	}

	void Checkbox::setStyle(const Style& declared) {
		Control::setStyle(declared);
		show();
	}

	void Checkbox::setChecked(bool value) {
		checked = value;
		show();
	}

	void Checkbox::activate() {
		setChecked(!checked);
		if (onChange) {
			onChange(checked);
		}
	}

	void Checkbox::show() {
		Rectangle* shown = box.get();
		if (shown == nullptr || getContext() == nullptr) {
			return;
		}
		const ToneColors accent = toneColors(getContext()->getTheme(), Tone::Accent);
		shown->setStyle(checked ? styles::check.extend({.backgroundColor = accent.color, .borderColor = accent.color}) : styles::check);
		for (Line* tick : {tickShort.get(), tickLong.get()}) {
			if (tick != nullptr) {
				tick->setVisible(checked);
				tick->setStyle({.color = accent.onColor});
			}
		}
	}

} // namespace ui
