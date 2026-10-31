// perf_ui: ui's walk over 10,000 components with the overlay stack in use. 1,250 cards of eight nodes
// each, three deep: a rounded card holding a header (an icon and a title), a value, a meter's track
// (its fill) and a divider, one card in ten raised by zIndex. Over them, open from the start: a menu anchored to a card, a modal
// dialog over a scrim, two toasts and a tooltip. The stage moves with the tick, as perf_quads does, so
// every frame places every card again and the anchored menu and tooltip follow theirs. Fixed content
// from a fixed seed.

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
#include <optional>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "perf_ui";
	constexpr uint32_t	  kSeed = 0x5A1A1004;
	constexpr int		  kCards = 1250;
	constexpr int		  kNodesPerCard = 8;
	constexpr int		  kMenuRows = 8;
	constexpr int		  kDialogRows = 12;
	constexpr float		  kWidth = 1600.0F;
	constexpr float		  kHeight = 900.0F;
	constexpr float		  kCardWidth = 64.0F;
	constexpr float		  kCardHeight = 44.0F;

	const Foundation::Color kBackground(0.07F, 0.07F, 0.09F, 1.0F);
	const Foundation::Color kCard(0.16F, 0.16F, 0.2F);
	const Foundation::Color kTrack(0.1F, 0.1F, 0.12F);
	const Foundation::Color kPanel(0.18F, 0.2F, 0.25F, 1.0F);
	const Foundation::Color kRow(0.24F, 0.26F, 0.32F, 1.0F);
	const Foundation::Color kScrim(0.0F, 0.0F, 0.0F, 0.55F);
	const Foundation::Color kTip(0.95F, 0.9F, 0.6F, 1.0F);
	const Foundation::Color kInk(0.85F, 0.85F, 0.9F, 1.0F);

	using Foundation::Color;
	using ui::Component;
	using ui::Rectangle;
	using ui::Ref;
	using ui::Text;

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
				cards.push_back(card(rng, i));
			}
			openOverlays();
		}

		void onExit() override {
			cards.clear();
			stage = {};
		}

		void fixedUpdate(const app::TickContext& context) override {
			const auto t = static_cast<float>(context.tick);
			if (Component* moving = stage.get()) {
				moving->setPosition({40.0F + (40.0F * std::sin(t * 0.02F)), 30.0F + (30.0F * std::cos(t * 0.015F))});
			}
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(kBackground); }

		std::string exportState() const override {
			return R"({"cards":)" + std::to_string(cards.size()) + R"(,"nodes":)" + std::to_string(nodes) + R"(,"overlays":)" +
				   std::to_string(overlays) + "}";
		}

	  private:
		// One draw a statement, so the content doesn't depend on the order a call's arguments are evaluated.
		Ref<Rectangle> card(foundation::Rng& rng, int index) {
			const Color			 tint = colour(rng);
			const float			 x = (kWidth - kCardWidth) * rng.nextFloat();
			const float			 y = (kHeight - kCardHeight) * rng.nextFloat();
			const bool			 raised = rng.nextFloat() < 0.1F;
			const std::string	 value = std::to_string(rng.nextInt(0, 999));
			const float			 filled = rng.nextFloat();
			const Ref<Rectangle> made = stage.addChild<Rectangle>({
				.position = {x, y},
				.size = {kCardWidth, kCardHeight},
				.style = {.backgroundColor = kCard, .borderColor = tint, .borderWidth = 1.0F, .borderRadius = 4.0F},
				.zIndex = raised ? std::optional(1) : std::nullopt,
			});
			const Ref<Rectangle> header = made.addChild<Rectangle>(
				{.position = {2.0F, 2.0F}, .size = {60.0F, 16.0F}, .style = {.backgroundColor = shade(tint, 0.6F)}}
			);
			header.addChild<ui::Circle>({.position = {2.0F, 3.0F}, .size = {10.0F, 10.0F}, .style = {.backgroundColor = tint}});
			header.addChild<Text>(
				{.position = {15.0F, 1.0F}, .style = {.color = Color::white(), .fontSize = 11.0F}, .text = "#" + std::to_string(index)}
			);
			made.addChild<Text>({.position = {4.0F, 20.0F}, .style = {.color = tint, .fontSize = 11.0F}, .text = value});
			const Ref<Rectangle> track = made.addChild<Rectangle>(
				{.position = {4.0F, 36.0F}, .size = {56.0F, 6.0F}, .style = {.backgroundColor = kTrack, .borderRadius = 3.0F}}
			);
			track.addChild<Rectangle>(
				{.size = {56.0F * (0.05F + (0.95F * filled)), 6.0F}, .style = {.backgroundColor = tint, .borderRadius = 3.0F}}
			);
			made.addChild<ui::Line>({.style = {.color = shade(tint, 0.8F)}, .start = {34.0F, 27.0F}, .end = {60.0F, 27.0F}});
			nodes += kNodesPerCard;
			return made;
		}

		// A row of a menu or the dialog: a box with a label.
		void row(Ref<Component> parent, const std::string& label) {
			const Ref<Rectangle> made =
				parent.addChild<Rectangle>({.size = {200.0F, 24.0F}, .style = {.backgroundColor = kRow, .borderRadius = 4.0F}});
			made.addChild<Text>({.position = {8.0F, 5.0F}, .style = {.color = kInk, .fontSize = 13.0F}, .text = label});
			nodes += 2;
		}

		// Each kind the stack holds, open at once: a menu, a dialog over its scrim, two toasts, a tooltip.
		void openOverlays() {
			const ui::Layout column{.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 4.0F, .padding = 6.0F};
			const Ref<Rectangle> menu = cards[kCards / 3].get()->openOverlay<Rectangle>(
				{.id = "menu", .layout = column, .style = {.backgroundColor = kPanel, .borderRadius = 6.0F}},
				{.kind = ui::OverlayKind::Menu}
			);
			for (int i = 0; i < kMenuRows; ++i) {
				row(menu, "menu row " + std::to_string(i + 1));
			}

			const Ref<Rectangle> scrim = root().get()->openOverlay<Rectangle>(
				{.id = "dialog",
				 .layout =
					 {.display = ui::Display::Flex, .alignItems = ui::AlignItems::Center, .justifyContent = ui::JustifyContent::Center},
				 .style = {.backgroundColor = kScrim}},
				{.kind = ui::OverlayKind::Dialog, .placement = ui::Absolute{.cover = true}}
			);
			const Ref<Rectangle> panel = scrim.addChild<Rectangle>(
				{.id = "dialog_panel", .layout = column, .style = {.backgroundColor = kPanel, .borderRadius = 8.0F}}
			);
			for (int i = 0; i < kDialogRows; ++i) {
				row(panel, "dialog row " + std::to_string(i + 1));
			}

			for (int i = 0; i < 2; ++i) {
				const Ref<Rectangle> toast = root().get()->openOverlay<Rectangle>(
					{.id = "toast_" + std::to_string(i + 1),
					 .size = {220.0F, 48.0F},
					 .style = {.backgroundColor = kRow, .borderRadius = 6.0F}},
					{.kind = ui::OverlayKind::Toast,
					 .placement = ui::Absolute{.at = {10000.0F, 16.0F + (64.0F * static_cast<float>(i))}, .margin = 16.0F}}
				);
				toast.addChild<Text>(
					{.position = {12.0F, 14.0F}, .style = {.color = kInk, .fontSize = 14.0F}, .text = "toast " + std::to_string(i + 1)}
				);
			}

			const Ref<Rectangle> tip = cards[(2 * kCards) / 3].get()->openOverlay<Rectangle>(
				{.id = "tooltip", .layout = {.padding = 6.0F}, .style = {.backgroundColor = kTip, .borderRadius = 4.0F}},
				{.kind = ui::OverlayKind::Tooltip, .placement = ui::Anchored{.side = ui::Side::Above}}
			);
			tip.addChild<Text>({.style = {.color = kBackground, .fontSize = 13.0F}, .text = "a tooltip, above everything"});
			nodes += 3 + 4 + 2;
			overlays = 5;
		}

		Ref<Component>				stage;
		std::vector<Ref<Rectangle>> cards;
		int							nodes = 0;
		int							overlays = 0;
	};

} // namespace

namespace sandbox {
	const SceneInfo kPerfUiScene{.name = kSceneName, .make = makeScene<PerfUiScene>};
} // namespace sandbox
