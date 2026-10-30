// ui_order: the paint order (docs/ui-design.md, "Ordering") as a tree of ui components, after
// worldsim's ui-sandbox layer scene (apps/ui-sandbox/scenes/LayerScene.cpp, a4226e17). Four panels:
// a hierarchy with ascending and descending stacks, each isolated; a hovered card, scaled up and
// raised by zIndex, over the resource bar beside its hand; targets over a targeting scrim; and a
// zIndex of 100 in the scene's root under a panel in a second root.

#include "Scenes.h"

#include <application/App.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <string>

namespace {

	constexpr const char* kSceneName = "ui_order";

	const Foundation::Color kBackground(0.08F, 0.08F, 0.11F, 1.0F);
	const Foundation::Color kPanel(0.14F, 0.14F, 0.19F, 1.0F);
	const Foundation::Color kLine(0.35F, 0.35F, 0.45F, 1.0F);
	const Foundation::Color kLabel(0.75F, 0.75F, 0.8F, 1.0F);

	using Foundation::Color;
	using ui::Component;
	using ui::Rectangle;
	using ui::Ref;
	using ui::Style;
	using ui::Text;

	Ref<Rectangle> panel(Ref<Component> parent, const char* id, Foundation::Vec2 position, const char* title) {
		Ref<Rectangle> made = parent.addChild<Rectangle>({
			.id = id,
			.position = position,
			.size = {480.0F, 300.0F},
			.style = {.backgroundColor = kPanel, .borderColor = kLine, .borderWidth = 1.0F, .borderRadius = 6.0F},
		});
		made.addChild<Text>({.position = {16.0F, 12.0F}, .style = {.color = kLabel, .fontSize = 18.0F}, .text = title});
		return made;
	}

	// A box with a label filling it, centred.
	Ref<Rectangle> labelled(Ref<Component> parent, Component::Args args, const std::string& label, float fontSize = 14.0F) {
		const Foundation::Vec2 size = args.size;
		Ref<Rectangle>		   box = parent.addChild<Rectangle>(args);
		box.addChild<Text>({
			.size = size,
			.style =
				{.color = Color::white(),
				 .fontSize = fontSize,
				 .textAlign = Foundation::HorizontalAlign::Center,
				 .verticalAlign = Foundation::VerticalAlign::Middle},
			.text = label,
		});
		return box;
	}

	class UiOrderScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			Ref<Component> stage = root().addChild<Component>({.id = "stage", .position = {20.0F, 20.0F}, .size = {1000.0F, 640.0F}});
			hierarchy(stage);
			hand(stage);
			scrim(stage);
			roots(stage);
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(kBackground); }

	  private:
		// Children paint over their parents with no zIndex at all; within each isolated stack the
		// zIndex orders the squares whatever order they were added in.
		static void hierarchy(Ref<Component> stage) {
			Ref<Rectangle> area = panel(stage, "hierarchy", {0.0F, 0.0F}, "Hierarchy and stacks");
			Ref<Rectangle> green = area.addChild<Rectangle>({
				.id = "green_panel",
				.position = {16.0F, 50.0F},
				.size = {268.0F, 100.0F},
				.style = {.backgroundColor = Color(0.15F, 0.55F, 0.25F), .borderRadius = 4.0F},
			});
			green.addChild<Text>({
				.position = {12.0F, 12.0F},
				.style = {.color = Color::yellow(), .fontSize = 20.0F},
				.text = "Component hierarchy",
			});
			Ref<Rectangle> sidebar = area.addChild<Rectangle>({
				.id = "sidebar",
				.position = {300.0F, 50.0F},
				.size = {160.0F, 230.0F},
				.style = {.backgroundColor = Color(0.3F, 0.3F, 0.35F), .borderColor = Color::cyan(), .borderWidth = 2.0F},
			});
			for (int i = 0; i < 4; ++i) {
				labelled(
					sidebar,
					{.id = "button_" + std::to_string(i + 1),
					 .position = {10.0F, 10.0F + (static_cast<float>(i) * 55.0F)},
					 .size = {140.0F, 45.0F},
					 .style = {.backgroundColor = Color(0.4F, 0.4F, 0.45F), .borderColor = Color::white(), .borderWidth = 1.0F}},
					"Button " + std::to_string(i + 1)
				);
			}
			stack(area, "ascending", {16.0F, 165.0F}, false);
			stack(area, "descending", {154.0F, 165.0F}, true);
		}

		static void stack(Ref<Component> area, const char* id, Foundation::Vec2 position, bool descending) {
			Ref<Component> group = area.addChild<Component>({.id = id, .position = position, .size = {130.0F, 125.0F}, .isolation = true});
			for (int i = 0; i < 4; ++i) {
				const float offset = static_cast<float>(i) * 20.0F;
				const float hue = static_cast<float>(i) / 4.0F;
				group.addChild<Rectangle>({
					.position = {offset, offset},
					.size = {60.0F, 60.0F},
					.style =
						{.backgroundColor = descending ? Color(hue, 0.5F, 1.0F - hue, 0.85F) : Color(hue, 1.0F - hue, 0.5F, 0.85F),
						 .borderColor = Color::white(),
						 .borderWidth = 2.0F},
					.zIndex = descending ? 3 - i : i,
				});
			}
		}

		// The hovered card's zIndex lifts it out of its hand; its scale (a transform, which layout and
		// the lint don't see) carries it over the bar beside the hand, and its label goes with it.
		static void hand(Ref<Component> stage) {
			Ref<Rectangle> area = panel(stage, "hand_case", {500.0F, 0.0F}, "A hovered card over the resource bar");
			Ref<Component> cards = area.addChild<Component>({.id = "hand", .position = {16.0F, 120.0F}, .size = {330.0F, 140.0F}});
			for (int i = 0; i < 4; ++i) {
				const bool hovered = i == 3;
				labelled(
					cards,
					{.id = "card_" + std::to_string(i + 1),
					 .position = {10.0F + (static_cast<float>(i) * 78.0F), 10.0F},
					 .size = {70.0F, 120.0F},
					 .transform = hovered ? ui::Transform{.scale = {1.6F, 1.6F}, .translate = {24.0F, -24.0F}} : ui::Transform{},
					 .style =
						 {.backgroundColor = hovered ? Color(0.85F, 0.6F, 0.2F) : Color(0.55F, 0.4F, 0.2F),
						  .borderColor = Color(1.0F, 0.9F, 0.6F),
						  .borderWidth = 2.0F,
						  .borderRadius = 6.0F},
					 .zIndex = hovered ? std::optional(1) : std::nullopt},
					"Card " + std::to_string(i + 1)
				);
			}
			Ref<Rectangle> bar = area.addChild<Rectangle>({
				.id = "resources",
				.position = {352.0F, 120.0F},
				.size = {112.0F, 140.0F},
				.style = {.backgroundColor = Color(0.2F, 0.35F, 0.6F), .borderColor = kLine, .borderWidth = 1.0F, .borderRadius = 4.0F},
			});
			labelled(
				bar,
				{.id = "gold", .position = {8.0F, 10.0F}, .size = {96.0F, 36.0F}, .style = {.backgroundColor = Color(0.6F, 0.5F, 0.1F)}},
				"Gold 12"
			);
			labelled(
				bar,
				{.id = "fuel", .position = {8.0F, 52.0F}, .size = {96.0F, 36.0F}, .style = {.backgroundColor = Color(0.3F, 0.5F, 0.3F)}},
				"Fuel 4"
			);
			labelled(
				bar,
				{.id = "scrap", .position = {8.0F, 94.0F}, .size = {96.0F, 36.0F}, .style = {.backgroundColor = Color(0.45F, 0.3F, 0.3F)}},
				"Scrap 7"
			);
		}

		// The scrim at zIndex 1 covers the board; the two targets inside it, at 2, paint over it.
		static void scrim(Ref<Component> stage) {
			Ref<Rectangle> area = panel(stage, "scrim_case", {0.0F, 320.0F}, "Targets over a targeting scrim");
			Ref<Component> board = area.addChild<Component>({.id = "board", .position = {16.0F, 50.0F}, .size = {448.0F, 230.0F}});
			for (int row = 0; row < 3; ++row) {
				for (int column = 0; column < 6; ++column) {
					const bool target = (row == 1 && column == 1) || (row == 2 && column == 4);
					labelled(
						board,
						{.id = target ? "target_" + std::to_string(row) + "_" + std::to_string(column) : std::string{},
						 .position = {8.0F + (static_cast<float>(column) * 72.0F), 8.0F + (static_cast<float>(row) * 72.0F)},
						 .size = {64.0F, 64.0F},
						 .style =
							 {.backgroundColor = target ? Color(0.8F, 0.25F, 0.25F) : Color(0.3F, 0.32F, 0.38F),
							  .borderColor = target ? Color(1.0F, 0.8F, 0.4F) : kLine,
							  .borderWidth = target ? 3.0F : 1.0F,
							  .borderRadius = 4.0F},
						 .zIndex = target ? std::optional(2) : std::nullopt},
						target ? "Target" : "Tile",
						12.0F
					);
				}
			}
			area.addChild<Rectangle>({
				.id = "scrim",
				.position = {16.0F, 50.0F},
				.size = {448.0F, 230.0F},
				.style = {.backgroundColor = Color(0.0F, 0.0F, 0.0F, 0.65F)},
				.zIndex = 1,
			});
		}

		// A zIndex of 100 orders inside the scene's root only; the second root paints over all of it.
		void roots(Ref<Component> stage) {
			Ref<Rectangle> area = panel(stage, "roots_case", {500.0F, 320.0F}, "A zIndex in one root, under another root");
			labelled(
				area,
				{.id = "raised",
				 .position = {16.0F, 50.0F},
				 .size = {300.0F, 200.0F},
				 .style = {.backgroundColor = Color(0.6F, 0.2F, 0.4F), .borderRadius = 6.0F},
				 .zIndex = 100},
				"zIndex 100 in the root"
			);
			Ref<Component> hud = ui().addRoot<Component>({.id = "hud"});
			labelled(
				hud,
				{.id = "hud_panel",
				 .position = {690.0F, 470.0F},
				 .size = {300.0F, 150.0F},
				 .style =
					 {.backgroundColor = Color(0.15F, 0.45F, 0.5F, 0.95F),
					  .borderColor = Color::white(),
					  .borderWidth = 2.0F,
					  .borderRadius = 6.0F}},
				"A second root, no zIndex"
			);
		}
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiOrderScene{.name = kSceneName, .make = makeScene<UiOrderScene>};
} // namespace sandbox
