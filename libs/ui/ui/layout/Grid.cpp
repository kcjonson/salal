#include "Grid.h"

#include <algorithm>
#include <limits>

namespace ui {

	namespace {

		using detail::GridItemAxis;
		using detail::GridTrackState;
		using detail::TrackKind;

		constexpr float kInfinity = std::numeric_limits<float>::infinity();

		// The spanning steps of 12.5: what grows, toward which contribution.
		enum class Step : std::uint8_t { IntrinsicMinimums, FlexibleMinimums, IntrinsicMaximums, MaxContentMaximums };

		bool growsBase(Step step) {
			return step == Step::IntrinsicMinimums || step == Step::FlexibleMinimums;
		}

		bool affected(const GridTrackState& track, Step step) {
			switch (step) {
				case Step::IntrinsicMinimums:
					return track.minKind == TrackKind::Auto;
				case Step::FlexibleMinimums:
					return track.minKind == TrackKind::Auto && track.maxKind == TrackKind::Flex;
				case Step::IntrinsicMaximums:
				case Step::MaxContentMaximums:
					return track.maxKind == TrackKind::Auto;
			}
			return false;
		}

		float contribution(const GridItemAxis& item, Step step) {
			switch (step) {
				case Step::IntrinsicMinimums:
				case Step::FlexibleMinimums:
					return item.minimum;
				case Step::IntrinsicMaximums:
					return item.minContent;
				case Step::MaxContentMaximums:
					return item.maxContent;
			}
			return 0.0F;
		}

		// An infinite growth limit reads as the base size.
		float affectedSize(const GridTrackState& track, Step step) {
			return growsBase(step) || track.limit == kInfinity ? track.base : track.limit;
		}

		float sizeLimit(const GridTrackState& track, Step step) {
			if (growsBase(step)) {
				return track.limit;
			}
			return track.growable || track.limit == kInfinity ? kInfinity : track.limit;
		}

		GridTrackState initTrack(const GridTrack& track, std::optional<float> available) {
			const auto resolve = [available](const TrackBreadth& breadth, float& value) {
				switch (breadth.unit) {
					case TrackBreadth::Unit::Px:
						value = std::max(breadth.value, 0.0F);
						return TrackKind::Fixed;
					case TrackBreadth::Unit::Percent:
						if (!available) {
							return TrackKind::Auto;
						}
						value = std::max(*available * breadth.value / 100.0F, 0.0F);
						return TrackKind::Fixed;
					case TrackBreadth::Unit::Fr:
						value = std::max(breadth.value, 0.0F);
						return TrackKind::Flex;
					case TrackBreadth::Unit::Auto:
						break;
				}
				return TrackKind::Auto;
			};

			GridTrackState state;
			state.minKind = resolve(track.min, state.minValue);
			if (state.minKind == TrackKind::Flex) {
				state.minKind = TrackKind::Auto;
			}
			state.maxKind = resolve(track.max, state.maxValue);
			state.base = state.minKind == TrackKind::Fixed ? state.minValue : 0.0F;
			state.limit = state.maxKind == TrackKind::Fixed ? std::max(state.maxValue, state.base) : kInfinity;
			return state;
		}

		// Shares space equally over the unfrozen tracks, freezing each once room says it is full, and returns what is left.
		template <typename Room, typename Grow>
		float fillEqually(std::span<GridTrackState> tracks, float space, std::uint32_t open, Room room, Grow grow) {
			while (space > 0.0F && open > 0) {
				const float share = space / static_cast<float>(open);
				bool		capped = false;
				for (GridTrackState& track : tracks) {
					if (track.frozen) {
						continue;
					}
					const float left = room(track);
					if (left <= share) {
						const float grown = std::max(left, 0.0F);
						grow(track, grown);
						space -= grown;
						track.frozen = true;
						--open;
						capped = true;
					} else {
						grow(track, share);
						space -= share;
					}
				}
				if (!capped) {
					return 0.0F;
				}
			}
			return space;
		}

		// 12.5.1 for one item: its increase to each spanned track, kept as the track's planned increase when it is the largest.
		void distributeItem(std::span<GridTrackState> spanned, float gaps, float itemContribution, Step step) {
			float space = itemContribution - gaps;
			for (const GridTrackState& track : spanned) {
				space -= affectedSize(track, step);
			}
			if (space <= 0.0F) {
				return;
			}

			std::uint32_t open = 0;
			float		  frSum = 0.0F;
			bool		  intrinsicMax = false;
			for (GridTrackState& track : spanned) {
				track.incurred = 0.0F;
				track.frozen = !affected(track, step);
				if (!track.frozen) {
					++open;
					frSum += track.maxValue;
					intrinsicMax = intrinsicMax || track.maxKind == TrackKind::Auto;
				}
			}
			if (open == 0) {
				return;
			}

			if (step == Step::FlexibleMinimums) {
				// By fr factor, equally when they are all 0. An fr track's growth limit is infinite, so none freezes.
				// The spec's draft splits factors summing under 1 partly equally; Chromium doesn't, and neither does this.
				for (GridTrackState& track : spanned) {
					if (!track.frozen) {
						track.incurred = frSum > 0.0F ? space * track.maxValue / frSum : space / static_cast<float>(open);
						track.planned = std::max(track.planned, track.incurred);
					}
				}
				return;
			}

			space = fillEqually(
				spanned,
				space,
				open,
				[step](const GridTrackState& track) { return sizeLimit(track, step) - affectedSize(track, step) - track.incurred; },
				[](GridTrackState& track, float grown) { track.incurred += grown; }
			);
			if (space > 0.0F) {
				// Past the limits: growing base sizes, the tracks with an intrinsic maximum if there are any;
				// growing limits, every affected track.
				const bool onlyIntrinsic = growsBase(step) && intrinsicMax;
				const auto beyond = [onlyIntrinsic, step](const GridTrackState& track) {
					return affected(track, step) && (!onlyIntrinsic || track.maxKind == TrackKind::Auto);
				};
				std::uint32_t count = 0;
				for (const GridTrackState& track : spanned) {
					count += beyond(track) ? 1 : 0;
				}
				const float share = space / static_cast<float>(count);
				for (GridTrackState& track : spanned) {
					if (beyond(track)) {
						track.incurred += share;
					}
				}
			}

			for (GridTrackState& track : spanned) {
				if (affected(track, step)) {
					track.planned = std::max(track.planned, track.incurred);
				}
			}
		}

		// One step over a set of items, then each affected track grows by its planned increase.
		void runStep(
			std::span<GridTrackState>	   tracks,
			std::span<const GridItemAxis>  items,
			std::span<const std::uint32_t> group,
			float						   gap,
			Step						   step
		) {
			for (const std::uint32_t index : group) {
				for (GridTrackState& track : tracks.subspan(items[index].start, items[index].span)) {
					track.planned = 0.0F;
					track.touched = true;
				}
			}
			for (const std::uint32_t index : group) {
				const GridItemAxis& item = items[index];
				distributeItem(
					tracks.subspan(item.start, item.span), gap * static_cast<float>(item.span - 1), contribution(item, step), step
				);
			}
			for (const std::uint32_t index : group) {
				for (GridTrackState& track : tracks.subspan(items[index].start, items[index].span)) {
					if (!track.touched) {
						continue;
					}
					track.touched = false;
					if (step == Step::MaxContentMaximums) {
						track.growable = false;
					}
					if (!affected(track, step)) {
						continue;
					}
					if (growsBase(step)) {
						track.base += track.planned;
						if (track.limit < track.base) {
							track.limit = track.base;
						}
					} else if (track.limit == kInfinity) {
						track.limit = track.base + track.planned;
						track.growable = step == Step::IntrinsicMaximums;
					} else {
						track.limit += track.planned;
					}
				}
			}
		}

		// 12.7.1: the size of 1fr filling space across the tracks, with any fr track that would fall under its
		// base size taken as fixed.
		float frSize(std::span<GridTrackState> tracks, float space, float gap) {
			if (tracks.empty()) {
				return 0.0F;
			}
			float leftover = space - (gap * static_cast<float>(tracks.size() - 1));
			for (GridTrackState& track : tracks) {
				track.frozen = track.maxKind != TrackKind::Flex;
				if (track.frozen) {
					leftover -= track.base;
				}
			}
			while (true) {
				float frSum = 0.0F;
				for (const GridTrackState& track : tracks) {
					if (!track.frozen) {
						frSum += track.maxValue;
					}
				}
				const float size = leftover / std::max(frSum, 1.0F);
				bool		restart = false;
				for (GridTrackState& track : tracks) {
					if (!track.frozen && size * track.maxValue < track.base) {
						track.frozen = true;
						leftover -= track.base;
						restart = true;
					}
				}
				if (!restart) {
					return size;
				}
			}
		}

		float usedSpace(std::span<const GridTrackState> tracks, float gap) {
			float used = tracks.empty() ? 0.0F : gap * static_cast<float>(tracks.size() - 1);
			for (const GridTrackState& track : tracks) {
				used += track.base;
			}
			return used;
		}

		struct Aligned {
			float position;
			float size;
		};

		Aligned alignInArea(float start, float extent, float minContent, float maxContent, std::optional<float> fixed, GridAlign align) {
			float size = extent;
			if (fixed) {
				size = std::max(*fixed, 0.0F);
			} else if (align != GridAlign::Stretch) {
				size = std::min(std::max(maxContent, minContent), std::max(minContent, extent));
			}
			switch (align) {
				case GridAlign::End:
					return {.position = start + extent - size, .size = size};
				case GridAlign::Center:
					return {.position = start + ((extent - size) * 0.5F), .size = size};
				case GridAlign::Start:
				case GridAlign::Stretch:
					break;
			}
			return {.position = start, .size = size};
		}

		float trackOffsets(std::span<const float> sizes, float gap, std::vector<float>& offsets) {
			offsets.resize(sizes.size());
			float at = 0.0F;
			for (std::size_t i = 0; i < sizes.size(); ++i) {
				offsets[i] = at;
				at += sizes[i] + gap;
			}
			return sizes.empty() ? 0.0F : at - gap;
		}

	} // namespace

	void placeGridItems(const GridContainer& container, std::span<const GridItem> items, GridLayout& layout) {
		std::vector<GridArea>& areas = layout.areas;
		areas.resize(items.size());

		// Columns for every item with a definite column and the widest auto span; rows for the definite rows. A
		// row-locked item (8.5 step 2) can push past the columns by at most its span, which bounds the stride.
		auto		  columns = static_cast<std::uint32_t>(container.gridTemplateColumns.size());
		auto		  rows = static_cast<std::uint32_t>(container.gridTemplateRows.size());
		std::uint32_t rowLocked = 0;
		for (std::size_t i = 0; i < items.size(); ++i) {
			const GridItem& item = items[i];
			GridArea&		area = areas[i];
			area.columnSpan = std::max<std::uint32_t>(item.gridColumn.span, 1);
			area.rowSpan = std::max<std::uint32_t>(item.gridRow.span, 1);
			if (item.gridColumn.start > 0) {
				area.column = static_cast<std::uint32_t>(item.gridColumn.start - 1);
				columns = std::max(columns, area.column + area.columnSpan);
			} else {
				columns = std::max(columns, area.columnSpan);
				if (item.gridRow.start > 0) {
					rowLocked += area.columnSpan;
				}
			}
			if (item.gridRow.start > 0) {
				area.row = static_cast<std::uint32_t>(item.gridRow.start - 1);
				rows = std::max(rows, area.row + area.rowSpan);
			}
		}

		const std::uint32_t		   stride = columns + rowLocked;
		std::vector<std::uint8_t>& occupied = layout.scratch.occupied;
		occupied.assign(static_cast<std::size_t>(stride) * rows, 0);

		const auto fits = [&](std::uint32_t column, std::uint32_t row, const GridArea& area) {
			const std::uint32_t rowEnd = std::min(row + area.rowSpan, rows);
			for (std::uint32_t r = row; r < rowEnd; ++r) {
				for (std::uint32_t c = column; c < column + area.columnSpan; ++c) {
					if (occupied[(static_cast<std::size_t>(r) * stride) + c] != 0) {
						return false;
					}
				}
			}
			return true;
		};
		const auto occupy = [&](const GridArea& area) {
			if (area.row + area.rowSpan > rows) {
				rows = area.row + area.rowSpan;
				occupied.resize(static_cast<std::size_t>(stride) * rows, 0);
			}
			for (std::uint32_t r = area.row; r < area.row + area.rowSpan; ++r) {
				for (std::uint32_t c = area.column; c < area.column + area.columnSpan; ++c) {
					occupied[(static_cast<std::size_t>(r) * stride) + c] = 1;
				}
			}
		};

		for (std::size_t i = 0; i < items.size(); ++i) {
			if (items[i].gridColumn.start > 0 && items[i].gridRow.start > 0) {
				occupy(areas[i]);
			}
		}

		if (rowLocked > 0) {
			// Sparse: each row-locked item goes past the ones this step already put in its row.
			std::vector<std::uint32_t>& cursor = layout.scratch.rowCursor;
			cursor.assign(rows, 0);
			for (std::size_t i = 0; i < items.size(); ++i) {
				if (items[i].gridColumn.start > 0 || items[i].gridRow.start <= 0) {
					continue;
				}
				GridArea&	  area = areas[i];
				std::uint32_t column = cursor[area.row];
				while (column + area.columnSpan < stride && !fits(column, area.row, area)) {
					++column;
				}
				area.column = column;
				cursor[area.row] = column + area.columnSpan;
				columns = std::max(columns, column + area.columnSpan);
				occupy(area);
			}
		}

		std::uint32_t cursorRow = 0;
		std::uint32_t cursorColumn = 0;
		for (std::size_t i = 0; i < items.size(); ++i) {
			const GridItem& item = items[i];
			if (item.gridRow.start > 0) {
				continue;
			}
			GridArea& area = areas[i];
			if (item.gridColumn.start > 0) {
				if (area.column < cursorColumn) {
					++cursorRow;
				}
				cursorColumn = area.column;
				while (!fits(area.column, cursorRow, area)) {
					++cursorRow;
				}
			} else {
				while (true) {
					if (cursorColumn + area.columnSpan > columns) {
						++cursorRow;
						cursorColumn = 0;
					} else if (fits(cursorColumn, cursorRow, area)) {
						break;
					} else {
						++cursorColumn;
					}
				}
				area.column = cursorColumn;
			}
			area.row = cursorRow;
			occupy(area);
		}

		layout.columns.assign(columns, 0.0F);
		layout.rows.assign(rows, 0.0F);
	}

	void sizeGridTracks(GridAxis axis, const GridContainer& container, std::span<const GridItem> items, GridLayout& layout) {
		const bool						 isColumn = axis == GridAxis::Column;
		std::vector<float>&				 sizes = isColumn ? layout.columns : layout.rows;
		const std::span<const GridTrack> explicitTracks = isColumn ? container.gridTemplateColumns : container.gridTemplateRows;
		const GridTrack&				 implicitTrack = isColumn ? container.gridAutoColumns : container.gridAutoRows;
		std::optional<float>			 available = isColumn ? container.width : container.height;
		if (available) {
			available = std::max(*available, 0.0F);
		}
		const float gap = std::max(isColumn ? container.columnGap : container.rowGap, 0.0F);

		// 12.4
		std::vector<GridTrackState>& tracks = layout.scratch.tracks;
		tracks.resize(sizes.size());
		for (std::size_t i = 0; i < tracks.size(); ++i) {
			tracks[i] = initTrack(i < explicitTracks.size() ? explicitTracks[i] : implicitTrack, available);
		}

		// Each item's span and contributions on this axis. Its minimum contribution is content based only when it
		// spans an auto minimum and, spanning more than one track, no fr track (6.6); when every track it spans
		// has a fixed maximum, it is clamped to their sum.
		std::vector<GridItemAxis>& itemAxes = layout.scratch.items;
		itemAxes.resize(items.size());
		for (std::size_t i = 0; i < items.size(); ++i) {
			const GridItem& item = items[i];
			const GridArea& area = layout.areas[i];
			GridItemAxis&	axisItem = itemAxes[i];
			axisItem.start = isColumn ? area.column : area.row;
			axisItem.span = isColumn ? area.columnSpan : area.rowSpan;

			const std::optional<float> fixed = isColumn ? item.width : item.height;
			const float				   minContent = std::max(isColumn ? item.minContent.x : item.minContent.y, 0.0F);
			const float				   maxContent = std::max(isColumn ? item.maxContent.x : item.maxContent.y, minContent);
			axisItem.minContent = fixed ? std::max(*fixed, 0.0F) : minContent;
			axisItem.maxContent = fixed ? axisItem.minContent : maxContent;

			bool  autoMinimum = false;
			bool  fixedMaximums = true;
			float fixedSum = gap * static_cast<float>(axisItem.span - 1);
			axisItem.flexible = false;
			for (const GridTrackState& track : std::span(tracks).subspan(axisItem.start, axisItem.span)) {
				autoMinimum = autoMinimum || track.minKind == TrackKind::Auto;
				axisItem.flexible = axisItem.flexible || track.maxKind == TrackKind::Flex;
				fixedMaximums = fixedMaximums && track.maxKind == TrackKind::Fixed;
				fixedSum += track.maxKind == TrackKind::Fixed ? track.maxValue : 0.0F;
			}
			const bool contentBased = autoMinimum && (axisItem.span == 1 || !axisItem.flexible);
			if (fixed) {
				axisItem.minimum = axisItem.minContent;
			} else if (contentBased) {
				axisItem.minimum = fixedMaximums ? std::min(minContent, fixedSum) : minContent;
			} else {
				axisItem.minimum = 0.0F;
			}
		}

		// 12.5 step 2: items in one track, fr tracks aside.
		for (const GridItemAxis& axisItem : itemAxes) {
			if (axisItem.span != 1 || axisItem.flexible) {
				continue;
			}
			GridTrackState& track = tracks[axisItem.start];
			if (track.minKind == TrackKind::Auto) {
				track.base = std::max(track.base, axisItem.minimum);
			}
			if (track.maxKind == TrackKind::Auto) {
				track.limit = track.limit == kInfinity ? axisItem.maxContent : std::max(track.limit, axisItem.maxContent);
			}
		}
		for (GridTrackState& track : tracks) {
			track.limit = std::max(track.limit, track.base);
		}

		// Step 3: spanning items clear of fr tracks, by span, shortest first.
		std::vector<std::uint32_t>& order = layout.scratch.order;
		order.clear();
		for (std::size_t i = 0; i < itemAxes.size(); ++i) {
			if (itemAxes[i].span > 1 && !itemAxes[i].flexible) {
				order.push_back(static_cast<std::uint32_t>(i));
			}
		}
		std::sort(order.begin(), order.end(), [&itemAxes](std::uint32_t a, std::uint32_t b) {
			return itemAxes[a].span < itemAxes[b].span;
		});
		for (std::size_t first = 0; first < order.size();) {
			std::size_t last = first + 1;
			while (last < order.size() && itemAxes[order[last]].span == itemAxes[order[first]].span) {
				++last;
			}
			const std::span<const std::uint32_t> group = std::span(order).subspan(first, last - first);
			runStep(tracks, itemAxes, group, gap, Step::IntrinsicMinimums);
			runStep(tracks, itemAxes, group, gap, Step::IntrinsicMaximums);
			runStep(tracks, itemAxes, group, gap, Step::MaxContentMaximums);
			first = last;
		}

		// Step 4: every item crossing an fr track at once, into the fr tracks' base sizes.
		order.clear();
		for (std::size_t i = 0; i < itemAxes.size(); ++i) {
			if (itemAxes[i].flexible) {
				order.push_back(static_cast<std::uint32_t>(i));
			}
		}
		if (!order.empty()) {
			runStep(tracks, itemAxes, order, gap, Step::FlexibleMinimums);
		}

		// Step 5.
		for (GridTrackState& track : tracks) {
			if (track.limit == kInfinity) {
				track.limit = track.base;
			}
		}

		// 12.6: free space grows base sizes up to the growth limits; content sizing grows them all the way.
		if (available) {
			std::uint32_t open = 0;
			for (GridTrackState& track : tracks) {
				track.frozen = track.base >= track.limit;
				open += track.frozen ? 0 : 1;
			}
			fillEqually(
				tracks,
				*available - usedSpace(tracks, gap),
				open,
				[](const GridTrackState& track) { return track.limit - track.base; },
				[](GridTrackState& track, float grown) { track.base += grown; }
			);
		} else {
			for (GridTrackState& track : tracks) {
				track.base = track.limit;
			}
		}

		// 12.7
		const bool anyFlexible =
			std::any_of(tracks.begin(), tracks.end(), [](const GridTrackState& track) { return track.maxKind == TrackKind::Flex; });
		if (anyFlexible) {
			float fraction = 0.0F;
			if (available) {
				if (*available - usedSpace(tracks, gap) > 0.0F) {
					fraction = frSize(tracks, *available, gap);
				}
			} else {
				for (const GridTrackState& track : tracks) {
					if (track.maxKind == TrackKind::Flex) {
						fraction = std::max(fraction, track.maxValue > 1.0F ? track.base / track.maxValue : track.base);
					}
				}
				for (const GridItemAxis& axisItem : itemAxes) {
					if (axisItem.flexible) {
						fraction =
							std::max(fraction, frSize(std::span(tracks).subspan(axisItem.start, axisItem.span), axisItem.maxContent, gap));
					}
				}
			}
			for (GridTrackState& track : tracks) {
				if (track.maxKind == TrackKind::Flex) {
					track.base = std::max(track.base, fraction * track.maxValue);
				}
			}
		}

		// 12.8: what is left goes to the auto tracks.
		if (available) {
			const float	  free = *available - usedSpace(tracks, gap);
			std::uint32_t autoTracks = 0;
			for (const GridTrackState& track : tracks) {
				autoTracks += track.maxKind == TrackKind::Auto ? 1 : 0;
			}
			if (free > 0.0F && autoTracks > 0) {
				const float share = free / static_cast<float>(autoTracks);
				for (GridTrackState& track : tracks) {
					if (track.maxKind == TrackKind::Auto) {
						track.base += share;
					}
				}
			}
		}

		for (std::size_t i = 0; i < tracks.size(); ++i) {
			sizes[i] = tracks[i].base;
		}
	}

	void alignGridItems(const GridContainer& container, std::span<const GridItem> items, GridLayout& layout) {
		const float				  columnGap = std::max(container.columnGap, 0.0F);
		const float				  rowGap = std::max(container.rowGap, 0.0F);
		const std::vector<float>& columnOffsets = layout.scratch.columnOffsets;
		const std::vector<float>& rowOffsets = layout.scratch.rowOffsets;
		layout.size = {
			trackOffsets(layout.columns, columnGap, layout.scratch.columnOffsets),
			trackOffsets(layout.rows, rowGap, layout.scratch.rowOffsets)
		};

		layout.rects.resize(items.size());
		for (std::size_t i = 0; i < items.size(); ++i) {
			const GridItem&		item = items[i];
			const GridArea&		area = layout.areas[i];
			const std::uint32_t lastColumn = area.column + area.columnSpan - 1;
			const std::uint32_t lastRow = area.row + area.rowSpan - 1;
			const float			x = columnOffsets[area.column];
			const float			y = rowOffsets[area.row];
			const Aligned		horizontal = alignInArea(
				  x,
				  columnOffsets[lastColumn] + layout.columns[lastColumn] - x,
				  item.minContent.x,
				  item.maxContent.x,
				  item.width,
				  item.justifySelf.value_or(container.justifyItems)
			  );
			const Aligned vertical = alignInArea(
				y,
				rowOffsets[lastRow] + layout.rows[lastRow] - y,
				item.minContent.y,
				item.maxContent.y,
				item.height,
				item.alignSelf.value_or(container.alignItems)
			);
			layout.rects[i] = Foundation::Rect(horizontal.position, vertical.position, horizontal.size, vertical.size);
		}
	}

	void layoutGrid(const GridContainer& container, std::span<const GridItem> items, GridLayout& layout) {
		placeGridItems(container, items, layout);
		sizeGridTracks(GridAxis::Column, container, items, layout);
		sizeGridTracks(GridAxis::Row, container, items, layout);
		alignGridItems(container, items, layout);
	}

} // namespace ui
