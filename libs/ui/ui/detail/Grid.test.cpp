// Grid's placement and track sizing with no tree, from CSS Grid Layout Level 1's own examples where it
// has them (section numbers are the spec's), and from its algorithm's steps where it doesn't. The
// layout pass's grid tests are in Layout.test.cpp.

#include "ui/detail/Grid.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace ui::detail {

	namespace {

		constexpr float kTolerance = 0.01F;

		// An item from `start` over `count` tracks with these min-content and max-content sizes.
		TrackContribution content(std::uint32_t start, std::uint32_t count, float minContent, float maxContent) {
			return {.tracks = {.start = start, .count = count}, .minContent = minContent, .maxContent = maxContent};
		}

		// An item whose own size is definite.
		TrackContribution definite(std::uint32_t start, std::uint32_t count, float size) {
			return {.tracks = {.start = start, .count = count}, .minContent = size, .maxContent = size, .minimum = size};
		}

		std::vector<float> sized(
			const std::vector<TrackSizingFunction>& tracks,
			std::optional<float>					available,
			const std::vector<TrackContribution>&	items = {},
			float									gap = 0.0F
		) {
			std::vector<float> sizes;
			sizeGridTracks({.tracks = tracks, .available = available, .gap = gap, .items = items}, sizes);
			return sizes;
		}

		void expectSizes(const std::vector<float>& actual, const std::vector<float>& expected) {
			ASSERT_EQ(actual.size(), expected.size());
			for (std::size_t i = 0; i < actual.size(); ++i) {
				EXPECT_NEAR(actual[i], expected[i], kTolerance) << "track " << i;
			}
		}

		PlacedGrid placed(std::uint32_t columns, std::uint32_t rows, const std::vector<GridItemLines>& items) {
			PlacedGrid grid;
			placeGridItems({.explicitColumns = columns, .explicitRows = rows, .items = items}, grid);
			return grid;
		}

		// Each item's first column and row.
		void expectCells(const PlacedGrid& grid, const std::vector<std::pair<std::uint32_t, std::uint32_t>>& cells) {
			ASSERT_EQ(grid.areas.size(), cells.size());
			for (std::size_t i = 0; i < cells.size(); ++i) {
				EXPECT_EQ(grid.areas[i].column.start, cells[i].first) << "item " << i;
				EXPECT_EQ(grid.areas[i].row.start, cells[i].second) << "item " << i;
			}
		}

	} // namespace

	// --- Track sizing ---

	TEST(GridTracks, AFixedColumnAndAnFrColumnShareTheWidth) {
		// 3.2: 150px 1fr is 150 and 50 in a 200px grid, and 150 and 0 in a 100px one.
		expectSizes(sized({150.0F, fr(1)}, 200.0F), {150, 50});
		expectSizes(sized({150.0F, fr(1)}, 100.0F), {150, 0});
	}

	TEST(GridTracks, FrFactorsSummingUnderOneLeaveSpaceUnfilled) {
		// 7.2.4: three .25fr tracks take a quarter of the leftover each.
		expectSizes(sized({fr(0.25F), fr(0.25F), fr(0.25F)}, 400.0F), {100, 100, 100});
	}

	TEST(GridTracks, TheResolvedValueExample) {
		// 7.2.6: a 500px grid, `auto minmax(min-content, 1fr) 40px 40px` and five auto tracks, a 50px item
		// in the first and the ninth. The spec's min-content floor is auto here, the same for an empty
		// track.
		const std::vector<TrackSizingFunction> tracks{kAuto, minmax(kAuto, fr(1)), 40.0F, 40.0F, kAuto, kAuto, kAuto, kAuto, kAuto};
		expectSizes(sized(tracks, 500.0F, {definite(0, 1, 50.0F), definite(8, 1, 50.0F)}), {50, 320, 40, 40, 0, 0, 0, 0, 50});
	}

	TEST(GridTracks, FrTracksSplitTheFreeSpaceAndContentHoldsAFloor) {
		// 7.2.1's `100px 1fr max-content minmax(min-content, 1fr)`, with auto for the content keywords:
		// the third track is its item's max-content, and the fr tracks halve what's left...
		const std::vector<TrackSizingFunction> tracks{100.0F, fr(1), kAuto, minmax(kAuto, fr(1))};
		const std::vector<TrackContribution>   items{content(2, 1, 40.0F, 80.0F), content(3, 1, 30.0F, 120.0F)};
		expectSizes(sized(tracks, 500.0F, items), {100, 160, 80, 160});
		// ...until half is less than the fourth's item's minimum, which it keeps.
		expectSizes(sized(tracks, 230.0F, items), {100, 20, 80, 30});
	}

	TEST(GridTracks, AFixedFloorHoldsAnFrTrack) {
		expectSizes(sized({minmax(200.0F, fr(1)), fr(1)}, 300.0F), {200, 100});
		expectSizes(sized({minmax(200.0F, fr(1)), fr(1)}, 600.0F), {300, 300});
	}

	TEST(GridTracks, AutoTracksSizeToContentThenStretch) {
		const std::vector<TrackContribution> items{content(0, 1, 40.0F, 100.0F), content(1, 1, 25.0F, 50.0F)};
		// Sized to content: the max-content widths.
		expectSizes(sized({kAuto, kAuto}, std::nullopt, items), {100, 50});
		// With room: those, and what's left shared equally (11.8).
		expectSizes(sized({kAuto, kAuto}, 500.0F, items), {275, 225});
		// Short of room: from the min-content widths, the free space shared equally (11.6).
		expectSizes(sized({kAuto, kAuto}, 100.0F, items), {57.5F, 42.5F});
	}

	TEST(GridTracks, PercentTracksAreAutoUntilTheGridHasASize) {
		expectSizes(sized({percent(25), fr(1)}, 400.0F), {100, 300});
		expectSizes(sized({percent(25), 100.0F}, std::nullopt, {content(0, 1, 30.0F, 60.0F)}), {60, 100});
	}

	TEST(GridTracks, AnAutomaticMinimumStopsAtAFixedMaximumAndADefiniteSizeDoesNot) {
		// 6.6: content-based minimums are clamped to the fixed maximums spanned; a definite size isn't
		// an automatic minimum.
		expectSizes(sized({minmax(kAuto, 100.0F), fr(1)}, 400.0F, {content(0, 1, 150.0F, 150.0F)}), {100, 300});
		expectSizes(sized({minmax(kAuto, 100.0F), fr(1)}, 400.0F, {definite(0, 1, 150.0F)}), {150, 250});
	}

	TEST(GridTracks, AnIntrinsicMaximumKeepsTheTrackItsItemSized) {
		// 11.5 step 3's example: two auto tracks, a 10px item in the first, and an item 30px at its
		// narrowest and 100px at its widest across both: [10, 90], not [45, 55].
		const std::vector<TrackContribution> items{content(0, 1, 10.0F, 10.0F), content(0, 2, 30.0F, 100.0F)};
		expectSizes(sized({kAuto, kAuto}, std::nullopt, items), {10, 90});
		expectSizes(sized({kAuto, kAuto}, 100.0F, items), {10, 90});
	}

	TEST(GridTracks, ASpanCountsTheGapsItCrosses) {
		// 10.1: a gap is a fixed track the span crosses.
		expectSizes(sized({kAuto, kAuto}, std::nullopt, {content(0, 2, 210.0F, 210.0F)}, 10.0F), {100, 100});
	}

	TEST(GridTracks, AnItemAcrossFrTracksGrowsThemByFactor) {
		// 11.5 step 4: by factor, and factors summing under 1 take that much by factor and the rest
		// equally.
		expectSizes(sized({fr(1), fr(3)}, 100.0F, {definite(0, 2, 400.0F)}), {100, 300});
		expectSizes(sized({fr(0.5F), fr(0.25F)}, 0.0F, {definite(0, 2, 300.0F)}), {187.5F, 112.5F});
		// Only into the fr tracks with an auto minimum.
		expectSizes(sized({minmax(50.0F, fr(1)), fr(1)}, 100.0F, {definite(0, 2, 300.0F)}), {50, 250});
	}

	TEST(GridTracks, AContentItemSpanningAnFrTrackHasNoMinimum) {
		// 6.6: spanning several tracks, one of them fr, its automatic minimum is 0.
		const std::vector<TrackContribution> items{content(0, 1, 60.0F, 80.0F), content(0, 2, 300.0F, 500.0F)};
		expectSizes(sized({kAuto, fr(1)}, 400.0F, items), {80, 320});
	}

	TEST(GridTracks, FrTracksSizedToContentTakeTheLargestShare) {
		// 11.7 with no definite size: 1fr is the largest of each fr track's content over its factor.
		expectSizes(sized({fr(1), fr(2)}, std::nullopt, {content(0, 1, 60.0F, 60.0F), content(1, 1, 40.0F, 40.0F)}), {60, 120});
		expectSizes(sized({fr(1), fr(2)}, std::nullopt, {content(0, 2, 90.0F, 150.0F)}), {50, 100});
	}

	// --- Placement ---

	TEST(GridPlacement, AutoItemsFillTheRowsInOrder) {
		const PlacedGrid grid = placed(3, 0, std::vector<GridItemLines>(7));
		expectCells(grid, {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}, {0, 2}});
		EXPECT_EQ(grid.columns, 3U);
		EXPECT_EQ(grid.rows, 3U);
	}

	TEST(GridPlacement, TheSpecsLineAndSpanExamples) {
		// 8.1: `grid-row: 2 / span 5` and `grid-row: span 5 / 7` are both lines 2 to 7.
		for (const GridLines& row : {GridLines{line(2), span(5)}, GridLines{span(5), line(7)}}) {
			const PlacedGrid grid = placed(1, 0, {{.column = {}, .row = row}});
			EXPECT_EQ(grid.areas[0].row, (TrackRange{.start = 1, .count = 5}));
			EXPECT_EQ(grid.rows, 6U);
		}
		// 8.3, in eight columns: `4 / auto` is lines 4 to 5, `auto / 6` 5 to 6, and `8 / 8` 8 to 9.
		const std::vector<std::pair<GridLines, TrackRange>> cases{
			{GridLines{line(4)}, {.start = 3, .count = 1}},
			{GridLines{{}, line(6)}, {.start = 4, .count = 1}},
			{GridLines{line(8), line(8)}, {.start = 7, .count = 1}},
		};
		for (const auto& [column, range] : cases) {
			EXPECT_EQ(placed(8, 1, {{.column = column, .row = line(1)}}).areas[0].column, range);
		}
		// 2.2: `grid-area: 1 / 3 / -1` is column 3, every row.
		const PlacedGrid area = placed(4, 3, {{.column = line(3), .row = {line(1), line(-1)}}});
		EXPECT_EQ(area.areas[0].column, (TrackRange{.start = 2, .count = 1}));
		EXPECT_EQ(area.areas[0].row, (TrackRange{.start = 0, .count = 3}));
	}

	TEST(GridPlacement, LinesPastTheTemplateAddImplicitTracks) {
		// 8.5: in `repeat(5, 100px)`, `grid-column: 4 / span 3` needs a sixth column.
		const PlacedGrid after = placed(5, 0, {{.column = {line(4), span(3)}, .row = {}}});
		EXPECT_EQ(after.areas[0].column, (TrackRange{.start = 3, .count = 3}));
		EXPECT_EQ(after.columns, 6U);

		// A negative line past the start adds a track before the explicit grid, and auto placement
		// starts there.
		const PlacedGrid before = placed(2, 0, {{.column = line(-4), .row = {}}, {}});
		EXPECT_EQ(before.columnsBefore, 1U);
		EXPECT_EQ(before.columns, 3U);
		expectCells(before, {{0, 0}, {1, 0}});

		std::vector<TrackSizingFunction>	   tracks;
		const std::vector<TrackSizingFunction> columns{100.0F, fr(1)};
		gridTracks(columns, before.columnsBefore, before.columns, tracks);
		EXPECT_EQ(tracks, (std::vector<TrackSizingFunction>{kAuto, 100.0F, fr(1)}));
	}

	TEST(GridPlacement, AutoItemsFlowAroundAnExplicitItem) {
		const PlacedGrid grid = placed(3, 0, {{}, {.column = line(2), .row = line(1)}, {}, {}, {}});
		expectCells(grid, {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}});
	}

	TEST(GridPlacement, ADefiniteColumnMovesTheCursorOnward) {
		// 8.5 step 4, sparse: an item with a definite column takes the next row whose cell is free, a row
		// down when its column is behind the cursor.
		const PlacedGrid grid =
			placed(3, 0, {{}, {.column = line(1), .row = {}}, {}, {.column = line(3), .row = {}}, {.column = line(2), .row = {}}});
		expectCells(grid, {{0, 0}, {0, 1}, {1, 1}, {2, 1}, {1, 2}});
	}

	TEST(GridPlacement, SparsePlacementNeverGoesBack) {
		// A two-column span that doesn't fit at the end of the row leaves the cell it skipped empty.
		const PlacedGrid grid = placed(3, 0, {{}, {}, {.column = span(2), .row = {}}, {}});
		expectCells(grid, {{0, 0}, {1, 0}, {0, 1}, {2, 1}});
	}

	TEST(GridPlacement, RowSpansAndRowLockedItems) {
		// A row span pushes later items aside in the rows it covers.
		const PlacedGrid spans = placed(2, 0, {{.column = {}, .row = span(2)}, {}, {}, {}});
		expectCells(spans, {{0, 0}, {1, 0}, {1, 1}, {0, 2}});

		// 8.5 step 2: items locked to a row go after one another there, past the template if they must.
		const PlacedGrid locked =
			placed(2, 0, {{.column = {}, .row = line(1)}, {.column = {}, .row = line(1)}, {.column = {}, .row = line(1)}, {}});
		expectCells(locked, {{0, 0}, {1, 0}, {2, 0}, {0, 1}});
		EXPECT_EQ(locked.columns, 3U);
	}

} // namespace ui::detail
