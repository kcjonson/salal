#include "ui/components/Toggle.h"

#include "ui/Context.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/Variants.h"

namespace ui {

	namespace {
		constexpr float kTrackWidth = 32.0F;
		constexpr float kTrackHeight = 18.0F;
		constexpr float kInset = 2.0F;
		constexpr float kKnob = kTrackHeight - 2.0F * kInset;
		constexpr float kGap = 8.0F;
	} // namespace

	Toggle::Toggle(const Args& args)
		: label(args.label),
		  on(args.on),
		  onChange(args.onChange) {}

	StyleId Toggle::themeStyle() const {
		return styles::listRow.theme; // the row itself has no fill; the track carries the look
	}

	void Toggle::onMount() {
		Control::onMount();
		Layout row = getLayout();
		row.display = Display::Flex;
		row.flexDirection = FlexDirection::Row;
		row.alignItems = AlignItems::Center;
		row.gap = kGap;
		setLayout(row);
		track = addChild<Rectangle>({.size = {kTrackWidth, kTrackHeight}});
		knob = track.addChild<Circle>({.position = {kInset, kInset}, .size = {kKnob, kKnob}});
		if (!label.empty()) {
			addChild<Text>({.style = styles::text::body, .text = label});
		}
		show();
	}

	void Toggle::setStyle(const Style& declared) {
		Control::setStyle(declared);
		show();
	}

	void Toggle::setOn(bool value) {
		on = value;
		show();
	}

	void Toggle::activate() {
		setOn(!on);
		if (onChange) {
			onChange(on);
		}
	}

	void Toggle::show() {
		Rectangle* shownTrack = track.get();
		Circle*	   shownKnob = knob.get();
		if (shownTrack == nullptr || shownKnob == nullptr || getContext() == nullptr) {
			return;
		}
		const Theme&	 theme = getContext()->getTheme();
		const ToneColors accent = toneColors(theme, Tone::Accent);
		shownTrack->setStyle(
			on ? styles::toggleTrack.extend({.backgroundColor = accent.color, .borderColor = accent.color}) : styles::toggleTrack
		);
		shownKnob->setStyle({.backgroundColor = on ? accent.onColor : theme.get(tokens::color::text::secondary)});
		shownKnob->setPosition({on ? kTrackWidth - kInset - kKnob : kInset, kInset});
	}

} // namespace ui
