#include "ui/components/Slider.h"

#include "ui/Context.h"
#include "ui/detail/Snapshot.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/Variants.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui {

	namespace {
		constexpr float kTrack = 4.0F;
		constexpr float kThumb = 14.0F;
		constexpr float kGap = 10.0F;
		constexpr float kKeySteps = 50.0F; // a key's move without a step: a fiftieth of the range
	} // namespace

	Slider::Slider(const Args& args)
		: min(args.min),
		  max(args.max > args.min ? args.max : args.min + 1.0F),
		  step(args.step),
		  value(args.value),
		  label(args.label),
		  showValue(args.showValue),
		  format(args.format),
		  onChange(args.onChange) {
		value = snapped(value);
	}

	void Slider::describe(nlohmann::json& node) const {
		Control::describe(node);
		node["value"] = value;
	}

	StyleId Slider::themeStyle() const {
		return styles::listRow.theme; // the row itself has no fill; the track carries the look
	}

	void Slider::onMount() {
		Control::onMount();
		Layout row = getLayout();
		row.display = Display::Flex;
		row.flexDirection = FlexDirection::Row;
		row.alignItems = AlignItems::Center;
		row.gap = kGap;
		setLayout(row);
		if (getSizeSpec().width.isAuto()) {
			setSize({200.0F, getSizeSpec().height});
		}
		if (!label.empty()) {
			addChild<Text>({.style = styles::text::body, .text = label});
		}
		trackArea = addChild<Component>({.size = {kAuto, kThumb}, .layout = {.flexGrow = 1.0F}});
		Ref<Rectangle> track = trackArea.addChild<Rectangle>({
			.position = {0.0F, (kThumb - kTrack) * 0.5F},
			.size = {percent(100.0F), kTrack},
			.style = styles::progressTrack.extend({.borderRadius = kTrack * 0.5F}),
		});
		fill = track.addChild<Rectangle>(
			{.size = {kAuto, percent(100.0F)}, .style = {.borderRadius = kTrack * 0.5F}, .pointerEvents = PointerEvents::None}
		);
		thumb = fill.addChild<Circle>({
			.size = {kThumb, kThumb},
			.layout = {.position = Position::Absolute, .inset = {.top = (kTrack - kThumb) * 0.5F, .right = -kThumb * 0.5F}},
			.pointerEvents = PointerEvents::None,
		});
		if (showValue) {
			valueText = addChild<Text>({.style = styles::label});
		}
		show();
	}

	void Slider::setStyle(const Style& declared) {
		Control::setStyle(declared);
		show();
	}

	float Slider::snapped(float raw) const {
		float clamped = std::clamp(raw, min, max);
		if (step > 0.0F) {
			clamped = std::clamp(min + std::round((clamped - min) / step) * step, min, max);
		}
		return clamped;
	}

	void Slider::setValue(float newValue) {
		value = snapped(newValue);
		show();
	}

	void Slider::change(float raw) {
		const float next = snapped(raw);
		if (next == value) {
			return;
		}
		value = next;
		show();
		if (onChange) {
			onChange(value);
		}
	}

	void Slider::handlePointer(PointerEvent& event) {
		const Component* area = trackArea.get();
		if (area == nullptr || !isEnabledInTree()) {
			return;
		}
		const auto toValue = [&] {
			const Foundation::Rect box = area->getBounds();
			const float			   fraction = box.width > 0.0F ? (event.local.x - box.x) / box.width : 0.0F;
			change(min + std::clamp(fraction, 0.0F, 1.0F) * (max - min));
		};
		if (event.type == PointerEvent::Type::Down && event.button == foundation::MouseButton::Left) {
			capturePointer();
			toValue();
			event.consume();
		} else if (event.type == PointerEvent::Type::Move && hasPointerCapture()) {
			toValue();
			event.consume();
		} else if (event.type == PointerEvent::Type::Up && hasPointerCapture()) {
			releasePointer();
			event.consume();
		}
	}

	void Slider::handleKey(KeyEvent& event) {
		if (event.type != KeyEvent::Type::Navigate || !isEnabledInTree()) {
			return;
		}
		if (event.direction != Direction::Left && event.direction != Direction::Right) {
			return;
		}
		const float delta = step > 0.0F ? step : (max - min) / kKeySteps;
		change(value + (event.direction == Direction::Right ? delta : -delta));
		event.consume();
	}

	void Slider::show() {
		if (getContext() == nullptr) {
			return;
		}
		const Theme&	 theme = getContext()->getTheme();
		const ToneColors accent = toneColors(theme, Tone::Accent);
		const float		 fraction = (value - min) / (max - min);
		if (Rectangle* shown = fill.get()) {
			shown->setSize({percent(fraction * 100.0F), percent(100.0F)});
			shown->setStyle({.backgroundColor = accent.color, .borderRadius = kTrack * 0.5F});
		}
		if (Circle* shown = thumb.get()) {
			shown->setStyle({.backgroundColor = theme.get(tokens::color::text::primary), .borderColor = accent.color, .borderWidth = 2.0F});
		}
		if (Text* shown = valueText.get()) {
			if (format) {
				shown->setText(format(value));
			} else {
				char text[32];
				std::snprintf(text, sizeof(text), "%.2f", static_cast<double>(value));
				shown->setText(text);
			}
		}
	}

} // namespace ui
