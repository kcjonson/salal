// Grid layout cost per layout, the GridLayout reused between iterations as a container keeps it:
//   - Form: label and control columns, `auto 1fr`, a full-width heading every eighth row.
//   - Wall: four mixed columns, `auto 1fr 1fr minmax(80px, auto)`, every seventh item spanning two.

#include "Grid.h"

#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

using namespace ui::track;

static void BM_Grid_Form(benchmark::State& state) {
	const auto				  rows = static_cast<int>(state.range(0));
	const ui::GridTrack		  columns[] = {kAuto, fr(1)};
	std::vector<ui::GridItem> items;
	for (int row = 0; row < rows; ++row) {
		if (row % 8 == 0) {
			items.push_back({.gridColumn = {.start = 1, .span = 2}, .minContent = {120.0F, 24.0F}, .maxContent = {320.0F, 24.0F}});
		} else {
			items.push_back({.minContent = {40.0F, 20.0F}, .maxContent = {60.0F + static_cast<float>(row % 5) * 10.0F, 20.0F}});
			items.push_back({.minContent = {80.0F, 28.0F}, .maxContent = {200.0F, 28.0F}});
		}
	}
	const ui::GridContainer container{.width = 480.0F, .gridTemplateColumns = columns, .columnGap = 12.0F, .rowGap = 6.0F};
	ui::GridLayout			layout;
	for (auto _ : state) {
		ui::layoutGrid(container, items, layout);
		benchmark::DoNotOptimize(layout.rects.data());
	}
	state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(items.size()));
}
BENCHMARK(BM_Grid_Form)->Arg(16)->Arg(256)->Arg(4096);

static void BM_Grid_Wall(benchmark::State& state) {
	const auto				  count = static_cast<int>(state.range(0));
	const ui::GridTrack		  columns[] = {kAuto, fr(1), fr(1), minmax(px(80), kAuto)};
	std::vector<ui::GridItem> items;
	for (int i = 0; i < count; ++i) {
		const float width = 30.0F + static_cast<float>(i % 9) * 12.0F;
		items.push_back(
			{.gridColumn = {.start = 0, .span = static_cast<std::uint16_t>(i % 7 == 0 ? 2 : 1)},
			 .minContent = {width * 0.5F, 40.0F},
			 .maxContent = {width, 40.0F}}
		);
	}
	const ui::GridContainer container{.width = 640.0F, .gridTemplateColumns = columns, .columnGap = 8.0F, .rowGap = 8.0F};
	ui::GridLayout			layout;
	for (auto _ : state) {
		ui::layoutGrid(container, items, layout);
		benchmark::DoNotOptimize(layout.rects.data());
	}
	state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(items.size()));
}
BENCHMARK(BM_Grid_Wall)->Arg(16)->Arg(256)->Arg(4096);
