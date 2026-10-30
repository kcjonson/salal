#include "Grid.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

// Expected values are Chromium's for the same container and template, with each item's content built from
// inline blocks so its min-content and max-content widths are the numbers given here.

using namespace ui::track;

namespace {

	using ui::GridAlign;
	using ui::GridContainer;
	using ui::GridItem;
	using ui::GridLayout;
	using ui::GridTrack;

	constexpr float kTolerance = 0.01F;

	// min px wide at its narrowest, max px wide on one line, height px tall.
	GridItem sized(float min, float max, float height = 0.0F) {
		return {.minContent = {min, height}, .maxContent = {max, height}};
	}

	GridItem fixedWidth(float width) {
		return {.width = width};
	}

	GridItem fixedHeight(float height) {
		return {.height = height};
	}

	GridItem inColumn(std::int16_t start, std::uint16_t span, GridItem item = {}) {
		item.gridColumn = {.start = start, .span = span};
		return item;
	}

	GridLayout layout(const GridContainer& container, const std::vector<GridItem>& items) {
		GridLayout result;
		ui::layoutGrid(container, items, result);
		return result;
	}

	void expectSizes(const std::vector<float>& actual, const std::vector<float>& expected) {
		ASSERT_EQ(actual.size(), expected.size());
		for (std::size_t i = 0; i < actual.size(); ++i) {
			EXPECT_NEAR(actual[i], expected[i], kTolerance) << "track " << i;
		}
	}

	void expectCells(const GridLayout& result, const std::vector<std::pair<std::uint32_t, std::uint32_t>>& cells) {
		ASSERT_EQ(result.areas.size(), cells.size());
		for (std::size_t i = 0; i < cells.size(); ++i) {
			EXPECT_EQ(result.areas[i].column, cells[i].first) << "item " << i;
			EXPECT_EQ(result.areas[i].row, cells[i].second) << "item " << i;
		}
	}

	void expectRect(const Foundation::Rect& rect, float x, float y, float width, float height) {
		EXPECT_NEAR(rect.x, x, kTolerance);
		EXPECT_NEAR(rect.y, y, kTolerance);
		EXPECT_NEAR(rect.width, width, kTolerance);
		EXPECT_NEAR(rect.height, height, kTolerance);
	}

} // namespace

TEST(GridTracks, FixedTracksKeepTheirSize) {
	const GridTrack columns[] = {px(100), px(200), px(50)};
	expectSizes(layout({.width = 500.0F, .gridTemplateColumns = columns}, {{}, {}, {}}).columns, {100, 200, 50});
	expectSizes(layout({.width = 500.0F, .gridTemplateColumns = columns}, {}).columns, {100, 200, 50});
}

TEST(GridTracks, FrSharesTheLeftoverSpace) {
	const GridTrack columns[] = {px(100), fr(1), fr(2)};
	expectSizes(layout({.width = 400.0F, .gridTemplateColumns = columns}, {{}, {}, {}}).columns, {100, 100, 200});
}

TEST(GridTracks, FrFactorsUnderOneLeaveSpace) {
	const GridTrack columns[] = {fr(0.5F), fr(0.25F)};
	expectSizes(layout({.width = 400.0F, .gridTemplateColumns = columns}, {{}, {}}).columns, {200, 100});
}

TEST(GridTracks, MinmaxFloorHoldsAnFrTrack) {
	const GridTrack columns[] = {minmax(px(200), fr(1)), fr(1)};
	expectSizes(layout({.width = 300.0F, .gridTemplateColumns = columns}, {{}, {}}).columns, {200, 100});
	expectSizes(layout({.width = 600.0F, .gridTemplateColumns = columns}, {{}, {}}).columns, {300, 300});
}

TEST(GridTracks, FrTrackKeepsItsContentMinimum) {
	const GridTrack columns[] = {fr(1), fr(1)};
	expectSizes(layout({.width = 100.0F, .gridTemplateColumns = columns}, {sized(80, 120), {}}).columns, {80, 20});
	expectSizes(layout({.width = 100.0F, .gridTemplateColumns = columns}, {fixedWidth(80), {}}).columns, {80, 20});
}

TEST(GridTracks, AutoTracksSizeToContent) {
	const GridTrack columns[] = {kAuto, kAuto, fr(1)};
	expectSizes(layout({.width = 500.0F, .gridTemplateColumns = columns}, {sized(40, 100), sized(20, 60), {}}).columns, {100, 60, 340});
}

TEST(GridTracks, AutoTracksStretchIntoFreeSpace) {
	const GridTrack columns[] = {kAuto, kAuto};
	expectSizes(layout({.width = 500.0F, .gridTemplateColumns = columns}, {sized(40, 100), sized(25, 50)}).columns, {275, 225});
}

TEST(GridTracks, AutoTracksShareAShortfallEqually) {
	const GridTrack columns[] = {kAuto, kAuto};
	expectSizes(layout({.width = 100.0F, .gridTemplateColumns = columns}, {sized(40, 100), sized(20, 60)}).columns, {60, 40});
}

TEST(GridTracks, FixedMaximumClampsAContentMinimumButNotAFixedSize) {
	const GridTrack columns[] = {minmax(kAuto, px(100)), fr(1)};
	expectSizes(layout({.width = 400.0F, .gridTemplateColumns = columns}, {sized(150, 150), {}}).columns, {100, 300});
	expectSizes(layout({.width = 400.0F, .gridTemplateColumns = columns}, {fixedWidth(150), {}}).columns, {150, 250});
}

TEST(GridTracks, FixedMinimumWithAutoMaximum) {
	const GridTrack beforeFr[] = {minmax(px(100), kAuto), fr(1)};
	expectSizes(layout({.width = 400.0F, .gridTemplateColumns = beforeFr}, {sized(50, 50), {}}).columns, {100, 300});
	const GridTrack beforeFixed[] = {minmax(px(100), kAuto), px(50)};
	expectSizes(layout({.width = 400.0F, .gridTemplateColumns = beforeFixed}, {sized(50, 50), {}}).columns, {350, 50});
}

TEST(GridTracks, PercentResolvesAgainstTheContainer) {
	const GridTrack columns[] = {percent(25), fr(1)};
	expectSizes(layout({.width = 400.0F, .gridTemplateColumns = columns}, {{}, {}}).columns, {100, 300});
}

TEST(GridTracks, PercentWithoutASizeIsAutoUntilTheContainerHasOne) {
	// CSS sizes the container with the percent as auto, then lays out again at the size that gave.
	const GridTrack columns[] = {px(100)};
	const GridTrack rows[] = {percent(50)};
	GridContainer	container{.width = 100.0F, .gridTemplateColumns = columns, .gridTemplateRows = rows};
	GridLayout		result = layout(container, {fixedHeight(30)});
	expectSizes(result.rows, {30});
	container.height = result.size.y;
	ui::layoutGrid(container, std::vector<GridItem>{fixedHeight(30)}, result);
	expectSizes(result.rows, {15});
}

TEST(GridTracks, RowsFillADefiniteHeight) {
	const GridTrack columns[] = {px(100)};
	const GridTrack rows[] = {px(40), fr(1)};
	expectSizes(
		layout({.width = 100.0F, .height = 200.0F, .gridTemplateColumns = columns, .gridTemplateRows = rows}, {{}, {}}).rows, {40, 160}
	);

	const GridLayout autoRows =
		layout({.width = 100.0F, .height = 100.0F, .gridTemplateColumns = columns}, {fixedHeight(20), fixedHeight(40)});
	expectSizes(autoRows.rows, {40, 60});
	expectRect(autoRows.rects[0], 0, 0, 100, 20);
	expectRect(autoRows.rects[1], 0, 40, 100, 40);
}

TEST(GridSpans, SpanAcrossAutoTracks) {
	const GridTrack		  columns[] = {kAuto, kAuto};
	std::vector<GridItem> items{sized(50, 50), inColumn(1, 2, sized(300, 400))};
	expectSizes(layout({.gridTemplateColumns = columns}, items).columns, {50, 350});
	expectSizes(layout({.width = 300.0F, .gridTemplateColumns = columns}, items).columns, {50, 250});
}

TEST(GridSpans, SpanAcrossFixedAndAutoTracks) {
	const GridTrack columns[] = {px(100), kAuto};
	expectSizes(layout({.gridTemplateColumns = columns}, {inColumn(1, 2, sized(250, 300))}).columns, {100, 200});
}

TEST(GridSpans, SpanAcrossAutoAndFrTracks) {
	// A content-sized item spanning an fr track has no minimum contribution; its max-content counts only
	// when the grid sizes to its content.
	const GridTrack		  columns[] = {kAuto, fr(1)};
	std::vector<GridItem> items{sized(60, 80), inColumn(1, 2, sized(300, 500))};
	expectSizes(layout({.width = 400.0F, .gridTemplateColumns = columns}, items).columns, {80, 320});
	expectSizes(layout({.gridTemplateColumns = columns}, items).columns, {80, 420});
}

TEST(GridSpans, FrTracksSizedToContent) {
	const GridTrack columns[] = {fr(1), fr(2)};
	expectSizes(layout({.gridTemplateColumns = columns}, {inColumn(1, 2, sized(90, 150))}).columns, {50, 100});
	expectSizes(layout({.gridTemplateColumns = columns}, {sized(60, 60), sized(40, 40)}).columns, {60, 120});
}

TEST(GridSpans, FixedSizeAcrossFrTracksSplitsByFactor) {
	const GridTrack equal[] = {fr(1), fr(1)};
	expectSizes(layout({.width = 100.0F, .gridTemplateColumns = equal}, {inColumn(1, 2, fixedWidth(300))}).columns, {150, 150});
	const GridTrack weighted[] = {fr(1), fr(3)};
	expectSizes(layout({.width = 100.0F, .gridTemplateColumns = weighted}, {inColumn(1, 2, fixedWidth(400))}).columns, {100, 300});
	const GridTrack underOne[] = {fr(0.5F), fr(0.25F)};
	expectSizes(layout({.width = 0.0F, .gridTemplateColumns = underOne}, {inColumn(1, 2, fixedWidth(300))}).columns, {200, 100});
}

TEST(GridSpans, FixedSizeAcrossFrTracksGrowsOnlyAutoMinimums) {
	const GridTrack withAuto[] = {kAuto, fr(1)};
	expectSizes(
		layout({.width = 100.0F, .gridTemplateColumns = withAuto}, {sized(20, 20), inColumn(1, 2, fixedWidth(300))}).columns, {20, 280}
	);
	const GridTrack withFloor[] = {minmax(px(50), fr(1)), fr(1)};
	expectSizes(layout({.width = 100.0F, .gridTemplateColumns = withFloor}, {inColumn(1, 2, fixedWidth(300))}).columns, {50, 250});
}

TEST(GridSpans, FixedMaximumsClampAContentSpan) {
	const GridTrack columns[] = {minmax(kAuto, px(100)), minmax(kAuto, px(100))};
	expectSizes(layout({.width = 50.0F, .gridTemplateColumns = columns}, {inColumn(1, 2, sized(300, 300))}).columns, {100, 100});
	expectSizes(layout({.width = 50.0F, .gridTemplateColumns = columns}, {inColumn(1, 2, fixedWidth(300))}).columns, {150, 150});
}

TEST(GridSpans, SpansOfOneLengthGrowTogether) {
	const GridTrack columns[] = {kAuto, kAuto, kAuto};
	expectSizes(
		layout({.gridTemplateColumns = columns}, {inColumn(1, 2, sized(100, 200)), inColumn(2, 2, sized(60, 300))}).columns, {100, 160, 140}
	);
}

TEST(GridSpans, SpanCountsTheGapsItCrosses) {
	const GridTrack two[] = {kAuto, kAuto};
	expectSizes(layout({.gridTemplateColumns = two, .columnGap = 10.0F}, {inColumn(0, 2, sized(210, 210))}).columns, {100, 100});

	// The empty third column's growth limit is 0, so the span grows both auto columns past their limits.
	const GridTrack three[] = {kAuto, px(50), kAuto};
	expectSizes(
		layout({.gridTemplateColumns = three, .columnGap = 10.0F}, {sized(30, 30), {}, {}, inColumn(1, 3, sized(250, 250))}).columns,
		{105, 50, 75}
	);
}

TEST(GridSpans, RowSpanSharesWhatItNeeds) {
	const GridTrack columns[] = {px(100), px(100)};
	GridItem		tall = fixedHeight(30);
	tall.gridRow.span = 2;
	const GridLayout result =
		layout({.width = 200.0F, .gridTemplateColumns = columns}, {tall, fixedHeight(10), fixedHeight(10), fixedHeight(10)});
	expectCells(result, {{0, 0}, {1, 0}, {1, 1}, {0, 2}});
	expectSizes(result.rows, {15, 15, 10});
}

TEST(GridPlacement, AutoItemsFillRowsInOrder) {
	const GridTrack columns[] = {px(100), px(100), px(100)};
	expectCells(
		layout({.width = 300.0F, .gridTemplateColumns = columns}, std::vector<GridItem>(7)),
		{{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}, {0, 2}}
	);
}

TEST(GridPlacement, AutoItemsFlowAroundAnExplicitItem) {
	const GridTrack columns[] = {px(100), px(100), px(100)};
	GridItem		explicitItem;
	explicitItem.gridColumn.start = 2;
	explicitItem.gridRow.start = 1;
	expectCells(
		layout({.width = 300.0F, .gridTemplateColumns = columns}, {{}, explicitItem, {}, {}, {}}), {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}}
	);
}

TEST(GridPlacement, SpanThatDoesNotFitLeavesAHole) {
	const GridTrack columns[] = {px(100), px(100), px(100)};
	expectCells(layout({.width = 300.0F, .gridTemplateColumns = columns}, {{}, inColumn(0, 2), {}}), {{0, 0}, {1, 0}, {0, 1}});
	expectCells(layout({.width = 300.0F, .gridTemplateColumns = columns}, {{}, {}, inColumn(0, 2), {}}), {{0, 0}, {1, 0}, {0, 1}, {2, 1}});
}

TEST(GridPlacement, RowLockedItemsAddColumns) {
	const GridTrack columns[] = {px(100), px(100)};
	GridItem		inFirstRow = sized(30, 30);
	inFirstRow.gridRow.start = 1;
	const GridLayout result = layout({.width = 300.0F, .gridTemplateColumns = columns}, {inFirstRow, inFirstRow, inFirstRow, {}});
	expectCells(result, {{0, 0}, {1, 0}, {2, 0}, {0, 1}});
	expectSizes(result.columns, {100, 100, 100});
}

TEST(GridPlacement, DefiniteColumnMovesTheCursor) {
	const GridTrack columns[] = {px(100), px(100), px(100)};
	expectCells(
		layout({.width = 300.0F, .gridTemplateColumns = columns}, {{}, inColumn(1, 1), {}, inColumn(3, 1), inColumn(2, 1)}),
		{{0, 0}, {0, 1}, {1, 1}, {2, 1}, {1, 2}}
	);
}

TEST(GridPlacement, LinesPastTheTemplateAddImplicitTracks) {
	const GridTrack	 columns[] = {px(100), px(100), px(100)};
	const GridLayout result = layout(
		{.width = 500.0F, .gridTemplateColumns = columns, .gridAutoColumns = px(50)}, {inColumn(5, 1, fixedHeight(10)), fixedHeight(10)}
	);
	expectCells(result, {{4, 0}, {0, 1}});
	expectSizes(result.columns, {100, 100, 100, 50, 50});
	expectRect(result.rects[0], 350, 0, 50, 10);
}

TEST(GridPlacement, ImplicitRowsTakeGridAutoRows) {
	const GridTrack	 columns[] = {px(100), px(100)};
	const GridLayout result = layout({.width = 200.0F, .gridTemplateColumns = columns, .gridAutoRows = px(30)}, std::vector<GridItem>(3));
	expectSizes(result.rows, {30, 30});
	expectRect(result.rects[2], 0, 30, 100, 30);
}

TEST(GridGaps, GapsSeparateTracks) {
	const GridTrack	 columns[] = {fr(1), fr(1), fr(1)};
	const GridLayout result = layout(
		{.width = 320.0F, .gridTemplateColumns = columns, .columnGap = 10.0F, .rowGap = 5.0F}, std::vector<GridItem>(6, fixedHeight(20))
	);
	expectSizes(result.columns, {100, 100, 100});
	expectSizes(result.rows, {20, 20});
	expectRect(result.rects[1], 110, 0, 100, 20);
	expectRect(result.rects[5], 220, 25, 100, 20);
	EXPECT_NEAR(result.size.x, 320, kTolerance);
	EXPECT_NEAR(result.size.y, 45, kTolerance);
}

namespace {
	// One 100 by 100 cell holding content 20 to 40 px wide and 30 px tall.
	Foundation::Rect alignedInCell(GridAlign justifyItems, GridAlign alignItems, GridItem item = sized(20, 40, 30)) {
		static const GridTrack cell[] = {px(100)};
		return layout(
				   {.width = 100.0F,
					.gridTemplateColumns = cell,
					.gridTemplateRows = cell,
					.justifyItems = justifyItems,
					.alignItems = alignItems},
				   {item}
		)
			.rects[0];
	}
} // namespace

TEST(GridAlignment, StretchFillsTheArea) {
	expectRect(alignedInCell(GridAlign::Stretch, GridAlign::Stretch), 0, 0, 100, 100);
}

TEST(GridAlignment, StartEndAndCenterSizeToContent) {
	expectRect(alignedInCell(GridAlign::Start, GridAlign::Start), 0, 0, 40, 30);
	expectRect(alignedInCell(GridAlign::End, GridAlign::End), 60, 70, 40, 30);
	expectRect(alignedInCell(GridAlign::Center, GridAlign::Center), 30, 35, 40, 30);
}

TEST(GridAlignment, SelfOverridesTheContainer) {
	GridItem item = sized(20, 40, 30);
	item.justifySelf = GridAlign::End;
	item.alignSelf = GridAlign::Center;
	expectRect(alignedInCell(GridAlign::Start, GridAlign::Start, item), 60, 35, 40, 30);
}

TEST(GridAlignment, FixedSizeIsNeverStretched) {
	GridItem item = sized(20, 40, 30);
	item.width = 50.0F;
	expectRect(alignedInCell(GridAlign::Stretch, GridAlign::Stretch, item), 0, 0, 50, 100);
}

TEST(GridAlignment, ContentNarrowerThanItsMaximumFitsTheArea) {
	// At 30 px the content wraps to two lines, which the caller measures as 60 px.
	const GridTrack	 column[] = {px(30)};
	const GridTrack	 row[] = {px(100)};
	const GridLayout result = layout(
		{.width = 30.0F,
		 .gridTemplateColumns = column,
		 .gridTemplateRows = row,
		 .justifyItems = GridAlign::Start,
		 .alignItems = GridAlign::Start},
		{sized(20, 40, 60)}
	);
	expectRect(result.rects[0], 0, 0, 30, 60);
}

TEST(GridAlignment, SpannedAreaIncludesItsGaps) {
	const GridTrack columns[] = {px(50), px(50)};
	const GridTrack rows[] = {px(20), px(20)};
	GridItem		item = inColumn(1, 2, sized(30, 30, 10));
	item.gridRow = {.start = 1, .span = 2};
	const GridLayout result = layout(
		{.width = 100.0F,
		 .gridTemplateColumns = columns,
		 .gridTemplateRows = rows,
		 .columnGap = 10.0F,
		 .rowGap = 4.0F,
		 .justifyItems = GridAlign::Center},
		{item}
	);
	expectRect(result.rects[0], 40, 0, 30, 44);
}

TEST(GridLayoutSteps, RowsSizeFromHeightsMeasuredAtTheColumnWidths) {
	// Text 1000 px square wraps to 1000 / width px tall, so its height is known only once the column is.
	const GridTrack		  columns[] = {fr(1), fr(3)};
	const GridContainer	  container{.width = 200.0F, .gridTemplateColumns = columns};
	std::vector<GridItem> items{sized(10, 1000), sized(10, 1000)};
	GridLayout			  result;
	ui::placeGridItems(container, items, result);
	ui::sizeGridTracks(ui::GridAxis::Column, container, items, result);
	for (std::size_t i = 0; i < items.size(); ++i) {
		const float height = 1000.0F / result.columns[result.areas[i].column];
		items[i].minContent.y = height;
		items[i].maxContent.y = height;
	}
	ui::sizeGridTracks(ui::GridAxis::Row, container, items, result);
	ui::alignGridItems(container, items, result);
	expectSizes(result.columns, {50, 150});
	expectSizes(result.rows, {20});
}

TEST(GridLayoutSteps, ALaidOutGridKeepsItsBuffers) {
	const GridTrack				columns[] = {kAuto, fr(1), minmax(px(40), kAuto)};
	const GridContainer			container{.width = 400.0F, .gridTemplateColumns = columns, .columnGap = 4.0F};
	const std::vector<GridItem> items{sized(10, 50), inColumn(1, 2, sized(100, 200)), {}, sized(5, 5), inColumn(2, 2, fixedWidth(90))};
	GridLayout					result;
	ui::layoutGrid(container, items, result);
	const auto* rects = result.rects.data();
	const auto* tracks = result.scratch.tracks.data();
	const auto* occupied = result.scratch.occupied.data();
	ui::layoutGrid(container, items, result);
	EXPECT_EQ(result.rects.data(), rects);
	EXPECT_EQ(result.scratch.tracks.data(), tracks);
	EXPECT_EQ(result.scratch.occupied.data(), occupied);
}
