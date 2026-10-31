#include "ui/components/Toggle.h"

#include "ui/Context.h"
#include "ui/detail/Snapshot.h"
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

	void Toggle::describe(nlohmann::json& node) const {
		Control::describe(node);
		node["state"]["selected"] = on;
		node["value"] = on;
	}

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
		const float start = on ? kTrackWidth - kInset - kKnob : kInset;
		knob = track.addChild<Circle>({.position = {start, kInset}, .size = {kKnob, kKnob}});
		slide = tween<float>({.from = start, .to = start, .onUpdate = [this](const float& x) {
								  if (Circle* shown = knob.get()) {
									  shown->setPosition({x, kInset});
								  }
							  }});
		const Foundation::Color fill = trackFill();
		fade = tween<Foundation::Color>(
			{.from = fill, .to = fill, .onUpdate = [this](const Foundation::Color& fill) {
				 if (Rectangle* shown = track.get()) {
					 shown->setStyle(
						 styles::toggleTrack.extend({.backgroundColor = fill, .borderColor = on ? std::optional(fill) : std::nullopt})
					 );
				 }
			 }}
		);
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

	Foundation::Color Toggle::trackFill() const {
		const Theme& theme = getContext()->getTheme();
		return on ? toneColors(theme, Tone::Accent).color
				  : theme.style(styles::toggleTrack.theme).backgroundColor.value_or(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F));
	}

	void Toggle::show() {
		Rectangle* shownTrack = track.get();
		Circle*	   shownKnob = knob.get();
		if (shownTrack == nullptr || shownKnob == nullptr || getContext() == nullptr) {
			return;
		}
		const Theme&	 theme = getContext()->getTheme();
		const ToneColors accent = toneColors(theme, Tone::Accent);
		const float		 fast = theme.get(tokens::motion::duration::fast);
		fade.retarget(trackFill(), fast);
		shownKnob->setStyle({.backgroundColor = on ? accent.onColor : theme.get(tokens::color::text::secondary)});
		slide.retarget(on ? kTrackWidth - kInset - kKnob : kInset, fast);
	}

} // namespace ui
