// ui_grid: `display: Grid`. CSS Grid's game layout (its 1.1.1: title, stats and score down an auto
// column, the board over two rows of an fr column, the controls centred under it); a row of each track
// kind, `100px 1fr auto minmax(60px, 1fr) 15%`; tiles flowed row by row around one placed at line 4
// of row 1, one spanning two columns and one two rows; the four alignments across and down fixed
// cells, each over a marker filling its area; and a form, `auto 1fr`, under a heading from line 1 to
// -1. The stage is a full-viewport column, so a resize lays it all out again.

#include "Scenes.h"

#include <application/App.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace {

	constexpr const char* kSceneName = "ui_grid";

	const Foundation::Color kBackground(0.1F, 0.1F, 0.13F, 1.0F);
	const Foundation::Color kFrame(0.17F, 0.17F, 0.22F, 1.0F);
	const Foundation::Color kMarker(0.24F, 0.24F, 0.3F, 1.0F);
	const Foundation::Color kSlate(0.204F, 0.596F, 0.859F);
	const Foundation::Color kRust(0.906F, 0.298F, 0.235F);
	const Foundation::Color kMoss(0.180F, 0.800F, 0.443F);
	const Foundation::Color kSand(0.945F, 0.769F, 0.059F);
	const Foundation::Color kLabel(0.85F, 0.85F, 0.6F, 1.0F);
	const Foundation::Color kInk(0.08F, 0.08F, 0.1F, 1.0F);

	using ui::AlignItems;
	using ui::Component;
	using ui::Display;
	using ui::FlexDirection;
	using ui::Rectangle;
	using ui::Ref;
	using ui::Text;

	ui::Layout flex(FlexDirection direction, float gap = 0.0F, ui::Edges padding = {}) {
		return {.display = Display::Flex, .flexDirection = direction, .gap = gap, .padding = padding};
	}

	ui::Layout grid(std::vector<ui::TrackSizingFunction> columns, std::vector<ui::TrackSizingFunction> rows = {}) {
		return {
			.display = Display::Grid,
			.gap = 6.0F,
			.padding = 8.0F,
			.gridTemplateColumns = std::move(columns),
			.gridTemplateRows = std::move(rows),
		};
	}

	// A captioned frame: the caption, then the frame a demo lays out in.
	Ref<Rectangle> captioned(Ref<Component> parent, const std::string& caption, const std::string& id, ui::Size size, ui::Layout layout) {
		Ref<Component> cell = parent.addChild<Component>({.layout = flex(FlexDirection::Column, 4.0F)});
		cell.addChild<Text>({.style = {.color = kLabel, .fontSize = 12.0F}, .text = caption});
		return cell.addChild<Rectangle>({.id = id, .size = size, .layout = std::move(layout), .style = {.backgroundColor = kFrame}});
	}

	// A labelled block: its label centred in it, its auto size the label's and a margin of padding.
	// `placed` is where it goes in its grid; its display and padding are the block's own.
	Ref<Rectangle>
	block(Ref<Component> parent, const std::string& id, const std::string& label, Foundation::Color colour, ui::Layout placed = {}) {
		placed.display = Display::Flex;
		placed.padding = {4.0F, 8.0F};
		placed.alignItems = AlignItems::Center;
		placed.justifyContent = ui::JustifyContent::Center;
		Ref<Rectangle> made = parent.addChild<Rectangle>({.id = id, .layout = std::move(placed), .style = {.backgroundColor = colour}});
		made.addChild<Text>({.style = {.color = kInk, .fontSize = 12.0F}, .text = label});
		return made;
	}

	class UiGridScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			ui::Layout stage = flex(FlexDirection::Column, 20.0F, 24.0F);
			stage.alignItems = AlignItems::Start;
			root().get()->setLayout(stage);
			root().addChild<Text>({.id = "title", .style = {.color = Foundation::Color::white(), .fontSize = 20.0F}, .text = "Grid"});
			Ref<Component> upper = root().addChild<Component>({.layout = flex(FlexDirection::Row, 24.0F)});
			gameLayout(upper);
			trackKinds(upper);
			Ref<Component> lower = root().addChild<Component>({.layout = flex(FlexDirection::Row, 24.0F)});
			autoFlow(lower);
			alignments(lower);
			form(lower);
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(kBackground); }

	  private:
		static void gameLayout(Ref<Component> row) {
			Ref<Rectangle> frame =
				captioned(row, "game layout", "game", {420.0F, 260.0F}, grid({ui::kAuto, ui::fr(1)}, {ui::kAuto, ui::fr(1), ui::kAuto}));
			block(frame, "game_title", "title", kSlate, {.gridColumn = ui::line(1), .gridRow = ui::line(1)});
			block(frame, "game_score", "score", kMoss, {.gridColumn = ui::line(1), .gridRow = ui::line(3)});
			block(frame, "game_stats", "stats", kSand, {.alignSelf = AlignItems::Start, .gridColumn = ui::line(1), .gridRow = ui::line(2)});
			block(frame, "game_board", "board", kSlate, {.gridColumn = ui::line(2), .gridRow = {ui::line(1), ui::span(2)}});
			block(
				frame,
				"game_controls",
				"controls",
				kRust,
				{.justifySelf = AlignItems::Center, .gridColumn = ui::line(2), .gridRow = ui::line(3)}
			);
		}

		static void trackKinds(Ref<Component> row) {
			Ref<Rectangle> frame = captioned(
				row,
				"100px 1fr auto minmax(60px, 1fr) 15%",
				"tracks",
				{560.0F, ui::kAuto},
				grid({100.0F, ui::fr(1), ui::kAuto, ui::minmax(60.0F, ui::fr(1)), ui::percent(15.0F)})
			);
			const std::array<std::pair<const char*, Foundation::Color>, 5> kTracks{{
				{"100px", kSlate},
				{"1fr", kMoss},
				{"auto", kSand},
				{"minmax", kMoss},
				{"15%", kRust},
			}};
			for (const auto& [label, colour] : kTracks) {
				block(frame, std::string("track_") + label, label, colour);
			}
		}

		static void autoFlow(Ref<Component> row) {
			Ref<Rectangle> frame =
				captioned(row, "auto flow, spans", "flow", {400.0F, ui::kAuto}, grid({ui::fr(1), ui::fr(1), ui::fr(1), ui::fr(1)}));
			for (int i = 1; i <= 9; ++i) {
				ui::Layout	placed;
				std::string label = std::to_string(i);
				if (i == 2) {
					placed.gridColumn = ui::span(2);
					label += ", 2 wide";
				} else if (i == 5) {
					placed.gridColumn = ui::line(4);
					placed.gridRow = ui::line(1);
					label += " at 4/1";
				} else if (i == 7) {
					placed.gridRow = ui::span(2);
					label += ", 2 tall";
				}
				block(frame, "tile_" + std::to_string(i), label, i == 5 ? kRust : (i % 2 == 0 ? kSlate : kMoss), std::move(placed));
			}
		}

		static void alignments(Ref<Component> row) {
			Ref<Rectangle> frame =
				captioned(row, "justify / align", "align", {ui::kAuto, ui::kAuto}, grid({100.0F, 100.0F, 100.0F}, {56.0F, 56.0F}));
			constexpr std::array<std::pair<AlignItems, const char*>, 4> kModes{{
				{AlignItems::Start, "start"},
				{AlignItems::Center, "center"},
				{AlignItems::End, "end"},
				{AlignItems::Stretch, "stretch"},
			}};
			// Justify then align, by kModes' index, a cell each, three to a row.
			constexpr std::array<std::pair<std::size_t, std::size_t>, 6> kCells{{{0, 0}, {1, 1}, {2, 2}, {3, 3}, {0, 2}, {2, 0}}};
			for (std::size_t i = 0; i < kCells.size(); ++i) {
				const auto& [justify, justifyName] = kModes[kCells[i].first];
				const auto& [align, alignName] = kModes[kCells[i].second];
				const ui::GridLines column = ui::line(static_cast<int>(i % 3) + 1);
				const ui::GridLines cellRow = ui::line(static_cast<int>(i / 3) + 1);
				// The area, under the aligned block; a decoration, so it overlaps nothing.
				frame.addChild<Rectangle>({
					.layout = {.gridColumn = column, .gridRow = cellRow},
					.style = {.backgroundColor = kMarker},
					.pointerEvents = ui::PointerEvents::None,
				});
				const std::string label = justify == align ? std::string(justifyName) : std::string(justifyName) + " " + alignName;
				block(
					frame,
					"align_" + std::to_string(i),
					label,
					kSand,
					{.alignSelf = align, .justifySelf = justify, .gridColumn = column, .gridRow = cellRow}
				);
			}
		}

		static void form(Ref<Component> row) {
			Ref<Rectangle> frame = captioned(row, "form, auto 1fr", "form", {260.0F, ui::kAuto}, grid({ui::kAuto, ui::fr(1)}));
			frame.addChild<Text>({
				.id = "form_heading",
				.layout = {.gridColumn = {ui::line(1), ui::line(-1)}},
				.style = {.color = Foundation::Color::white(), .fontSize = 14.0F},
				.text = "New crew member",
			});
			const std::array<std::pair<const char*, const char*>, 3> kFields{{
				{"name", "Name"},
				{"callsign", "Callsign"},
				{"port", "Home port"},
			}};
			for (const auto& [id, label] : kFields) {
				frame.addChild<Text>({.id = std::string("label_") + id, .style = {.color = kLabel, .fontSize = 12.0F}, .text = label});
				frame.addChild<Rectangle>(
					{.id = std::string("field_") + id, .size = {ui::kAuto, 20.0F}, .style = {.backgroundColor = kMarker}}
				);
			}
		}
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiGridScene{.name = kSceneName, .make = makeScene<UiGridScene>};
} // namespace sandbox
