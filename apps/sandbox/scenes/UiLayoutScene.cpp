// ui_layout: Flex and Free placement, after worldsim's ui-sandbox layout scene (apps/ui-sandbox/scenes/
// LayoutScene.cpp, a4226e17). Six columns, one per justifyContent; four per alignItems; a row of a
// fixed box and two growing ones at 1x and 2x; a column whose body grows; a nested card; a wrapping
// text in a column that hugs it; and absolutely placed badges in a card's corners. The stage is a
// full-viewport column, so a resize lays it all out again.

#include "Scenes.h"

#include <application/App.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <array>
#include <string>
#include <utility>

namespace {

	constexpr const char* kSceneName = "ui_layout";

	const Foundation::Color kBackground(0.1F, 0.1F, 0.13F, 1.0F);
	const Foundation::Color kFrame(0.17F, 0.17F, 0.22F, 1.0F);
	const Foundation::Color kSlate(0.204F, 0.596F, 0.859F);
	const Foundation::Color kRust(0.906F, 0.298F, 0.235F);
	const Foundation::Color kMoss(0.180F, 0.800F, 0.443F);
	const Foundation::Color kSand(0.945F, 0.769F, 0.059F);
	const Foundation::Color kLabel(0.85F, 0.85F, 0.6F, 1.0F);

	using ui::AlignItems;
	using ui::Component;
	using ui::Display;
	using ui::FlexDirection;
	using ui::JustifyContent;
	using ui::Rectangle;
	using ui::Ref;
	using ui::Text;

	ui::Layout flex(FlexDirection direction, float gap = 0.0F, ui::Edges padding = {}) {
		return {.display = Display::Flex, .flexDirection = direction, .gap = gap, .padding = padding};
	}

	// A captioned frame: the caption, then the frame a demo lays out in.
	Ref<Rectangle> captioned(Ref<Component> parent, const std::string& caption, ui::Size size, ui::Layout layout) {
		Ref<Component> cell = parent.addChild<Component>({.layout = flex(FlexDirection::Column, 4.0F)});
		cell.addChild<Text>({.style = {.color = kLabel, .fontSize = 12.0F}, .text = caption});
		return cell.addChild<Rectangle>({.id = caption, .size = size, .layout = layout, .style = {.backgroundColor = kFrame}});
	}

	class UiLayoutScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			ui::Layout stage = flex(FlexDirection::Column, 24.0F, 24.0F);
			stage.alignItems = AlignItems::Start;
			root().get()->setLayout(stage);
			root().addChild<Text>({.id = "title", .style = {.color = Foundation::Color::white(), .fontSize = 20.0F}, .text = "Layout"});
			distributions(root().addChild<Component>({.layout = flex(FlexDirection::Row, 16.0F)}));
			alignments(root().addChild<Component>({.layout = flex(FlexDirection::Row, 16.0F)}));
			Ref<Component> lastRow = root().addChild<Component>({.layout = flex(FlexDirection::Row, 16.0F)});
			growRow(lastRow);
			growColumn(lastRow);
			nested(lastRow);
			wrapping(lastRow);
			corners(lastRow);
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(kBackground); }

	  private:
		static void distributions(Ref<Component> row) {
			constexpr std::array<std::pair<JustifyContent, const char*>, 6> kModes{{
				{JustifyContent::Start, "start"},
				{JustifyContent::Center, "center"},
				{JustifyContent::End, "end"},
				{JustifyContent::SpaceBetween, "between"},
				{JustifyContent::SpaceAround, "around"},
				{JustifyContent::SpaceEvenly, "evenly"},
			}};
			for (const auto& [mode, name] : kModes) {
				ui::Layout layout = flex(FlexDirection::Column, 4.0F);
				layout.justifyContent = mode;
				layout.alignItems = AlignItems::Center;
				Ref<Rectangle> frame = captioned(row, name, {90.0F, 150.0F}, layout);
				for (const Foundation::Color& colour : {kSlate, kRust, kMoss}) {
					frame.addChild<Rectangle>({.size = {60.0F, 24.0F}, .style = {.backgroundColor = colour}});
				}
			}
		}

		static void alignments(Ref<Component> row) {
			constexpr std::array<std::pair<AlignItems, const char*>, 4> kModes{{
				{AlignItems::Start, "align start"},
				{AlignItems::Center, "align center"},
				{AlignItems::End, "align end"},
				{AlignItems::Stretch, "align stretch"},
			}};
			for (const auto& [mode, name] : kModes) {
				ui::Layout layout = flex(FlexDirection::Column, 4.0F, 4.0F);
				layout.alignItems = mode;
				Ref<Rectangle>						   frame = captioned(row, name, {120.0F, 100.0F}, layout);
				const std::array<float, 3>			   widths{40.0F, 70.0F, 55.0F};
				const std::array<Foundation::Color, 3> colours{kSlate, kRust, kMoss};
				for (std::size_t i = 0; i < 3; ++i) {
					// Under Stretch an auto width fills the column; elsewhere it's this.
					frame.addChild<Rectangle>(
						{.size = {mode == AlignItems::Stretch ? ui::kAuto : ui::Length(widths[i]), 22.0F},
						 .style = {.backgroundColor = colours[i]}}
					);
				}
			}
		}

		static void growRow(Ref<Component> row) {
			ui::Layout layout = flex(FlexDirection::Row, 6.0F, 8.0F);
			layout.alignItems = AlignItems::Center;
			Ref<Rectangle> frame = captioned(row, "fixed, grow 1, grow 2", {360.0F, 52.0F}, layout);
			frame.addChild<Rectangle>({.id = "fixed_rect", .size = {90.0F, 30.0F}, .style = {.backgroundColor = kSlate}});
			frame.addChild<Rectangle>(
				{.id = "grow_1", .size = {0.0F, 30.0F}, .layout = {.flexGrow = 1.0F}, .style = {.backgroundColor = kMoss}}
			);
			frame.addChild<Rectangle>(
				{.id = "grow_2", .size = {0.0F, 30.0F}, .layout = {.flexGrow = 2.0F}, .style = {.backgroundColor = kSand}}
			);
		}

		static void growColumn(Ref<Component> row) {
			Ref<Rectangle> frame = captioned(row, "growing body", {120.0F, 150.0F}, flex(FlexDirection::Column, 4.0F));
			frame.addChild<Rectangle>({.id = "column_header", .size = {ui::kAuto, 24.0F}, .style = {.backgroundColor = kRust}});
			frame.addChild<Rectangle>({.id = "column_body", .layout = {.flexGrow = 1.0F}, .style = {.backgroundColor = kSlate}});
		}

		static void nested(Ref<Component> row) {
			Ref<Rectangle> frame = captioned(row, "nested", {220.0F, 150.0F}, flex(FlexDirection::Column, 6.0F, 8.0F));
			frame.addChild<Rectangle>({.id = "nested_header", .size = {ui::kAuto, 22.0F}, .style = {.backgroundColor = kMoss}});
			ui::Layout inner = flex(FlexDirection::Row, 6.0F);
			inner.justifyContent = JustifyContent::SpaceBetween;
			inner.flexGrow = 1.0F;
			Ref<Component> innerRow = frame.addChild<Component>({.id = "nested_row", .layout = inner});
			for (const Foundation::Color& colour : {kSlate, kRust, kSand}) {
				innerRow.addChild<Rectangle>({.size = {50.0F, ui::kAuto}, .style = {.backgroundColor = colour}});
			}
		}

		static void wrapping(Ref<Component> row) {
			Ref<Rectangle> frame = captioned(row, "wrapping, hugged", {200.0F, ui::kAuto}, flex(FlexDirection::Column, 4.0F, 8.0F));
			frame.addChild<Text>({
				.id = "wrapped",
				.style = {.color = Foundation::Color::white(), .fontSize = 13.0F},
				.text = "A text in a column wraps at the column's width, and the column grows with it.",
				.wrap = true,
			});
		}

		static void corners(Ref<Component> row) {
			Ref<Rectangle> frame = captioned(row, "inset corners", {160.0F, 120.0F}, {});
			const auto	   badge = [&](const char* id, ui::Inset inset) {
				frame.addChild<Rectangle>({
						.id = id,
						.size = {20.0F, 20.0F},
						.layout = {.position = ui::Position::Absolute, .inset = inset},
						.style = {.backgroundColor = kSand, .borderRadius = 10.0F},
				});
			};
			badge("top_left", {.top = 6.0F, .left = 6.0F});
			badge("top_right", {.top = 6.0F, .right = 6.0F});
			badge("bottom_left", {.bottom = 6.0F, .left = 6.0F});
			badge("bottom_right", {.right = 6.0F, .bottom = 6.0F});
		}
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiLayoutScene{.name = kSceneName, .make = makeScene<UiLayoutScene>};
} // namespace sandbox
