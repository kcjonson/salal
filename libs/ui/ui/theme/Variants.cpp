#include "ui/theme/Variants.h"

#include "ui/theme/Tokens.h"

namespace ui {

	namespace {

		// Straight-alpha source-over.
		Foundation::Color over(Foundation::Color base, Foundation::Color wash) {
			const float alpha = wash.a + base.a * (1.0F - wash.a);
			if (alpha <= 0.0F) {
				return {0.0F, 0.0F, 0.0F, 0.0F};
			}
			const auto channel = [&](float washChannel, float baseChannel) {
				return (washChannel * wash.a + baseChannel * base.a * (1.0F - wash.a)) / alpha;
			};
			return {channel(wash.r, base.r), channel(wash.g, base.g), channel(wash.b, base.b), alpha};
		}

	} // namespace

	Tone resolveTone(const Theme& theme, Tone tone, float value) {
		if (tone != Tone::Auto) {
			return tone;
		}
		if (value < theme.get(tokens::tone::auto_::dangerBelow)) {
			return Tone::Danger;
		}
		return value < theme.get(tokens::tone::auto_::warningBelow) ? Tone::Warning : Tone::Positive;
	}

	ToneColors toneColors(const Theme& theme, Tone tone, float value) {
		const auto colors = [&](ColorToken color, ColorToken subtle, ColorToken onColor) {
			return ToneColors{.color = theme.get(color), .subtle = theme.get(subtle), .onColor = theme.get(onColor)};
		};
		namespace t = tokens::tone;
		switch (resolveTone(theme, tone, value)) {
			case Tone::Accent:
				return colors(t::accent::color, t::accent::subtle, t::accent::onColor);
			case Tone::Info:
				return colors(t::info::color, t::info::subtle, t::info::onColor);
			case Tone::Positive:
				return colors(t::positive::color, t::positive::subtle, t::positive::onColor);
			case Tone::Warning:
				return colors(t::warning::color, t::warning::subtle, t::warning::onColor);
			case Tone::Danger:
				return colors(t::danger::color, t::danger::subtle, t::danger::onColor);
			case Tone::Default:
			case Tone::Auto:
				break;
		}
		return colors(t::default_::color, t::default_::subtle, t::default_::onColor);
	}

	SizeMetrics sizeMetrics(const Theme& theme, ControlSize size) {
		switch (size) {
			case ControlSize::Sm:
				return {.fontSize = theme.get(tokens::size::sm::fontSize), .controlHeight = theme.get(tokens::size::sm::controlHeight)};
			case ControlSize::Lg:
				return {.fontSize = theme.get(tokens::size::lg::fontSize), .controlHeight = theme.get(tokens::size::lg::controlHeight)};
			case ControlSize::Md:
				break;
		}
		return {.fontSize = theme.get(tokens::size::md::fontSize), .controlHeight = theme.get(tokens::size::md::controlHeight)};
	}

	Foundation::Color applyStateLayers(const Theme& theme, Foundation::Color base, const InteractionState& state) {
		Foundation::Color color = base;
		if (state.hovered) {
			color = over(color, theme.get(tokens::state::hover::wash));
		}
		if (state.pressed) {
			color = over(color, theme.get(tokens::state::pressed::wash));
		}
		if (state.active) {
			color = over(color, theme.get(tokens::state::active::wash));
		}
		if (state.disabled) {
			color.a *= theme.get(tokens::state::disabled::opacity);
		}
		return color;
	}

	FocusRing focusRing(const Theme& theme) {
		return {
			.color = theme.get(tokens::focusRing::color),
			.width = theme.get(tokens::focusRing::width),
			.offset = theme.get(tokens::focusRing::offset),
		};
	}

} // namespace ui
