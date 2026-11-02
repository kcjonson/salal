// border: a rect's border on some of its sides, and dashed. A tab on a panel, bordered on three
// sides, its top corners round and its bottom square; one side alone; two facing sides; three sides
// in each position, the bordered sides running straight on to the bare one's edge; then dashes,
// dots, round and square caps, and a dash on three sides, each a whole number of periods round.

#include "DesignCanvas.h"
#include "Scenes.h"

#include <graphics/Color.h>
#include <graphics/PrimitiveStyles.h>
#include <primitives/Primitives.h>

#include <string>

namespace {

	constexpr const char* kSceneName = "border";
	constexpr float		  kCanvasWidth = 2400.0F;
	constexpr float		  kCanvasHeight = 1800.0F;

	const Foundation::Color kBackground(0.08F, 0.08F, 0.11F, 1.0F);
	const Foundation::Color kLabel(0.75F, 0.75F, 0.8F, 1.0F);
	const Foundation::Color kSheet(0.2F, 0.22F, 0.28F, 1.0F);
	const Foundation::Color kRule(0.95F, 0.8F, 0.45F, 1.0F);
	const Foundation::Color kGuide(1.0F, 0.3F, 0.3F, 0.6F);

	namespace P = Renderer::Primitives;

	// A label centred on `centreX`, its top at `top`.
	void label(const std::string& text, float centreX, float top) {
		P::drawText(
			{.text = text,
			 .position = {centreX - 250.0F, top},
			 .scale = 2.0F,
			 .color = kLabel,
			 .hAlign = Foundation::HorizontalAlign::Center,
			 .boxWidth = 500.0F}
		);
	}

	// A sheet with a border of `width` on `sides`, and a thin guide on its box.
	void sheet(const Foundation::Rect& bounds, Foundation::BorderSides sides, Foundation::BorderPosition position, float radius = 0.0F) {
		P::drawRect(
			{.bounds = bounds,
			 .style = {
				 .fill = kSheet,
				 .border =
					 Foundation::BorderStyle{.color = kRule, .width = 10.0F, .cornerRadius = radius, .position = position, .sides = sides}
			 }}
		);
		P::drawRect(
			{.bounds = bounds,
			 .style = {
				 .fill = Foundation::Color::transparent(),
				 .border = Foundation::BorderStyle{.color = kGuide, .width = 1.0F, .position = Foundation::BorderPosition::Inside}
			 }}
		);
	}

	class BorderScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void render(float /*alpha*/) override {
			P::clear(kBackground);
			P::PushTransform(sandbox::designCanvas(kCanvasWidth, kCanvasHeight));
			P::drawText(
				{.text = "drawRect: borders on some sides, square where a side is bare, and dashed",
				 .position = {80.0F, 40.0F},
				 .scale = 2.75F,
				 .color = Foundation::Color::white()}
			);

			// A tab on its panel: its top corners round, its bottom square and open, its fill over the
			// panel's top rule where they meet.
			P::drawRect(
				{.bounds = {120.0F, 320.0F, 520.0F, 180.0F},
				 .style = {
					 .fill = kSheet,
					 .border = Foundation::BorderStyle{.color = kRule, .width = 6.0F, .position = Foundation::BorderPosition::Inside}
				 }}
			);
			P::drawRect(
				{.bounds = {160.0F, 230.0F, 200.0F, 96.0F},
				 .style = {
					 .fill = kSheet,
					 .border = Foundation::BorderStyle{
						 .color = kRule,
						 .width = 6.0F,
						 .cornerRadius = 18.0F,
						 .position = Foundation::BorderPosition::Inside,
						 .sides = {.bottom = false}
					 }
				 }}
			);
			label("tab on a panel", 380.0F, 540.0F);

			sheet(
				{820.0F, 230.0F, 260.0F, 260.0F}, {.top = false, .right = false, .bottom = false}, Foundation::BorderPosition::Inside, 24.0F
			);
			label("left only, radius 24", 950.0F, 540.0F);
			sheet({1300.0F, 230.0F, 260.0F, 260.0F}, {.right = false, .left = false}, Foundation::BorderPosition::Inside, 24.0F);
			label("top and bottom", 1430.0F, 540.0F);
			sheet({1780.0F, 230.0F, 260.0F, 260.0F}, {.top = false, .right = false, .left = false}, Foundation::BorderPosition::Inside);
			label("underline", 1910.0F, 540.0F);

			// Three sides in each position, round between bordered sides.
			sheet({300.0F, 760.0F, 300.0F, 260.0F}, {.bottom = false}, Foundation::BorderPosition::Inside, 30.0F);
			label("inside", 450.0F, 1060.0F);
			sheet({1050.0F, 760.0F, 300.0F, 260.0F}, {.bottom = false}, Foundation::BorderPosition::Center, 30.0F);
			label("centred", 1200.0F, 1060.0F);
			sheet({1800.0F, 760.0F, 300.0F, 260.0F}, {.bottom = false}, Foundation::BorderPosition::Outside, 30.0F);
			label("outside", 1950.0F, 1060.0F);

			// Dashes round a border's centre line, one centred on the top-left corner.
			const auto dashed =
				[](const Foundation::Rect& bounds, float radius, Foundation::DashPattern dash, Foundation::BorderSides sides = {}) {
					P::drawRect(
						{.bounds = bounds,
						 .style = {
							 .fill = kSheet,
							 .border = Foundation::BorderStyle{
								 .color = kRule,
								 .width = 8.0F,
								 .cornerRadius = radius,
								 .position = Foundation::BorderPosition::Center,
								 .sides = sides,
								 .dash = dash
							 }
						 }}
					);
				};
			using Foundation::DashCap;
			dashed({200.0F, 1260.0F, 300.0F, 220.0F}, 0.0F, {.dash = 24.0F, .gap = 14.0F});
			label("dashed", 350.0F, 1530.0F);
			dashed({700.0F, 1260.0F, 300.0F, 220.0F}, 40.0F, {.dash = 0.0F, .gap = 18.0F, .cap = DashCap::Round});
			label("dotted, round corners", 850.0F, 1530.0F);
			dashed({1200.0F, 1260.0F, 300.0F, 220.0F}, 20.0F, {.dash = 30.0F, .gap = 20.0F, .cap = DashCap::Round});
			label("round caps", 1350.0F, 1530.0F);
			dashed({1700.0F, 1260.0F, 300.0F, 220.0F}, 20.0F, {.dash = 20.0F, .gap = 24.0F, .cap = DashCap::Square}, {.bottom = false});
			label("square caps, three sides", 1850.0F, 1530.0F);
			P::PopTransform();
		}
	};

} // namespace

namespace sandbox {
	const SceneInfo kBorderScene{.name = kSceneName, .make = makeScene<BorderScene>};
} // namespace sandbox
