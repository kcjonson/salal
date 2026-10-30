#pragma once

// The theme's shared vocabulary (docs/ui-design.md, "Theme"): tones, sizes, state layers and the
// focus ring, read from ui's tokens, so a game restyles all of them through its tokens file.

#include "ui/theme/Theme.h"

namespace ui {

	enum class Tone : std::uint8_t { Default, Accent, Info, Positive, Warning, Danger, Auto };
	enum class Size : std::uint8_t { Sm, Md, Lg };

	struct ToneColors {
		Foundation::Color color;   // the tone itself, for text and indicators
		Foundation::Color subtle;  // a fill wash
		Foundation::Color onColor; // text on a solid fill of the tone
	};

	// Auto picks by `value`, from 0 to 1: under tone.auto.dangerBelow danger, under
	// tone.auto.warningBelow warning, else positive. Any other tone is itself.
	Tone	   resolveTone(const Theme& theme, Tone tone, float value = 1.0F);
	ToneColors toneColors(const Theme& theme, Tone tone, float value = 1.0F);

	struct SizeMetrics {
		float fontSize = 0.0F;
		float controlHeight = 0.0F;
	};

	SizeMetrics sizeMetrics(const Theme& theme, Size size);

	struct InteractionState {
		bool hovered = false;
		bool pressed = false;
		bool active = false; // open or selected
		bool disabled = false;
	};

	// `base` under its state layers in R11.12's order: the hover, pressed and active washes
	// composited over it, then the disabled opacity. The focus ring comes last, drawn apart.
	Foundation::Color applyStateLayers(const Theme& theme, Foundation::Color base, const InteractionState& state);

	struct FocusRing {
		Foundation::Color color;
		float			  width = 0.0F;
		float			  offset = 0.0F; // outside the component's box
	};

	FocusRing focusRing(const Theme& theme);

} // namespace ui
