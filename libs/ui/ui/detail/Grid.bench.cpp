// Grid's placement and track sizing per layout, with no tree, the measures already taken:
//   - Form: `auto 1fr`, a label and a control a row, a heading across both every eighth row.
//   - Wall: `auto 1fr 1fr minmax(80px, auto)`, every seventh item spanning two columns.

#include "ui/detail/Grid.h"

#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

	using ui::detail::GridItemLines;
	using ui::detail::TrackContribution;

	struct Measured {
		float minWidth;
		float maxWidth;
		float height;
	};

	void layOut(
		benchmark::State&							state,
		const std::vector<ui::TrackSizingFunction>& columns,
		float										width,
		const std::vector<GridItemLines>&			lines,
		const std::vector<Measured>&				measured
	) {
		ui::detail::PlacedGrid				 placed;
		std::vector<ui::TrackSizingFunction> tracks;
		std::vector<TrackContribution>		 contributions(lines.size());
		std::vector<float>					 columnSizes;
		std::vector<float>					 rowSizes;
		for (auto _ : state) {
			ui::detail::placeGridItems({.explicitColumns = static_cast<std::uint32_t>(columns.size()), .items = lines}, placed);
			ui::detail::gridTracks(columns, placed.columnsBefore, placed.columns, tracks);
			for (std::size_t i = 0; i < lines.size(); ++i) {
				contributions[i] = {
					.tracks = placed.areas[i].column, .minContent = measured[i].minWidth, .maxContent = measured[i].maxWidth
				};
			}
			ui::detail::sizeGridTracks({.tracks = tracks, .available = width, .gap = 8.0F, .items = contributions}, columnSizes);
			ui::detail::gridTracks({}, placed.rowsBefore, placed.rows, tracks);
			for (std::size_t i = 0; i < lines.size(); ++i) {
				contributions[i] = {.tracks = placed.areas[i].row, .minContent = measured[i].height, .maxContent = measured[i].height};
			}
			ui::detail::sizeGridTracks({.tracks = tracks, .gap = 6.0F, .items = contributions}, rowSizes);
			benchmark::DoNotOptimize(rowSizes.data());
		}
		state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(lines.size()));
	}

	void BM_Grid_Form(benchmark::State& state) {
		std::vector<GridItemLines> lines;
		std::vector<Measured>	   measured;
		for (int row = 0; row < state.range(0); ++row) {
			if (row % 8 == 0) {
				lines.push_back({.column = {ui::line(1), ui::span(2)}, .row = {}});
				measured.push_back({120.0F, 320.0F, 24.0F});
			} else {
				lines.push_back({});
				measured.push_back({40.0F, 60.0F + static_cast<float>(row % 5) * 10.0F, 20.0F});
				lines.push_back({});
				measured.push_back({80.0F, 200.0F, 28.0F});
			}
		}
		layOut(state, {ui::kAuto, ui::fr(1)}, 480.0F, lines, measured);
	}
	BENCHMARK(BM_Grid_Form)->Arg(16)->Arg(256)->Arg(4096);

	void BM_Grid_Wall(benchmark::State& state) {
		std::vector<GridItemLines> lines;
		std::vector<Measured>	   measured;
		for (int i = 0; i < state.range(0); ++i) {
			const float width = 30.0F + static_cast<float>(i % 9) * 12.0F;
			lines.push_back({.column = i % 7 == 0 ? ui::span(2) : ui::GridPlacement{}, .row = {}});
			measured.push_back({width * 0.5F, width, 40.0F});
		}
		layOut(state, {ui::kAuto, ui::fr(1), ui::fr(1), ui::minmax(80.0F, ui::kAuto)}, 640.0F, lines, measured);
	}
	BENCHMARK(BM_Grid_Wall)->Arg(16)->Arg(256)->Arg(4096);

} // namespace
