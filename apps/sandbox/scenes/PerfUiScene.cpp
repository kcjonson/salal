// perf_ui: 10,000 ui components, the cost of ui's walk (R13.31): 1,250 cards of eight nodes each, a
// rounded card with a header, an icon, a title, a value, a meter's track and fill and a divider, one
// card in ten raised by zIndex. The stage moves with the tick, as perf_quads does, so every frame
// recomputes every screen transform; the paint order stays as it was built.

#include "Scenes.h"

#include <application/App.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <random/Rng.h>
#include <ui/shapes/Circle.h>
#include <ui/shapes/Line.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <cmath>
#include <cstdint>
#include <string>

namespace {

	constexpr const char* kSceneName = "perf_ui";
	constexpr uint32_t	  kSeed = 0x5A1A1009;
	constexpr int		  kCards = 1250;
	constexpr float		  kWidth = 1600.0F;
	constexpr float		  kHeight = 900.0F;
	constexpr float		  kCardWidth = 64.0F;
	constexpr float		  kCardHeight = 44.0F;

	const Foundation::Color kBackground(0.07F, 0.07F, 0.09F, 1.0F);

	using Foundation::Color;
	using ui::Component;
	using ui::Ref;

	Color shade(Color color, float amount) {
		return {color.r * amount, color.g * amount, color.b * amount, color.a};
	}

	Color colour(foundation::Rng& rng) {
		return {0.25F + (0.6F * rng.nextFloat()), 0.25F + (0.6F * rng.nextFloat()), 0.25F + (0.6F * rng.nextFloat()), 1.0F};
	}

	class PerfUiScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			foundation::Rng rng(kSeed);
			stage = root().addChild<Component>({.id = "stage", .size = {kWidth, kHeight}});
			for (int i = 0; i < kCards; ++i) {
				const Color		   tint = colour(rng);
				Ref<ui::Rectangle> card = stage.addChild<ui::Rectangle>({
					.position = {(kWidth - kCardWidth) * rng.nextFloat(), (kHeight - kCardHeight) * rng.nextFloat()},
					.size = {kCardWidth, kCardHeight},
					.style = {.backgroundColor = Color(0.16F, 0.16F, 0.2F), .borderColor = tint, .borderWidth = 1.0F, .borderRadius = 4.0F},
					.zIndex = rng.nextFloat() < 0.1F ? std::optional(1) : std::nullopt,
				});
				card.addChild<ui::Rectangle>(
					{.position = {2.0F, 2.0F}, .size = {60.0F, 12.0F}, .style = {.backgroundColor = shade(tint, 0.6F)}}
				);
				card.addChild<ui::Circle>({.position = {4.0F, 3.0F}, .size = {10.0F, 10.0F}, .style = {.backgroundColor = tint}});
				card.addChild<ui::Text>({
					.position = {17.0F, 2.0F},
					.style = {.color = Color::white(), .fontSize = 11.0F},
					.text = "#" + std::to_string(i),
				});
				card.addChild<ui::Text>({
					.position = {4.0F, 16.0F},
					.style = {.color = tint, .fontSize = 11.0F},
					.text = std::to_string(rng.nextInt(0, 999)),
				});
				const float filled = rng.nextFloat();
				card.addChild<ui::Rectangle>(
					{.position = {4.0F, 32.0F},
					 .size = {56.0F, 6.0F},
					 .style = {.backgroundColor = Color(0.1F, 0.1F, 0.12F), .borderRadius = 3.0F}}
				);
				card.addChild<ui::Rectangle>(
					{.position = {4.0F, 32.0F}, .size = {56.0F * filled, 6.0F}, .style = {.backgroundColor = tint, .borderRadius = 3.0F}}
				);
				card.addChild<ui::Line>({.style = {.color = shade(tint, 0.8F)}, .start = {34.0F, 18.0F}, .end = {60.0F, 18.0F}});
			}
		}

		void fixedUpdate(const app::TickContext& context) override {
			const auto t = static_cast<float>(context.tick);
			if (Component* moving = stage.get()) {
				moving->setPosition({40.0F * std::sin(t * 0.02F), 30.0F * std::cos(t * 0.015F)});
			}
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(kBackground); }

		std::string exportState() const override { return R"({"cards":)" + std::to_string(kCards) + "}"; }

	  private:
		Ref<Component> stage;
	};

} // namespace

namespace sandbox {
	const SceneInfo kPerfUiScene{.name = kSceneName, .make = makeScene<PerfUiScene>};
} // namespace sandbox
