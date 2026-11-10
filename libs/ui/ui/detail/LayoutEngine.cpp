#include "ui/detail/LayoutEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ui::detail {

	namespace {
		constexpr float kUnbounded = std::numeric_limits<float>::infinity();

		bool sameMeasure(std::optional<float> a, std::optional<float> b) {
			return a.has_value() == b.has_value() && (!a || *a == *b);
		}

		// The padding a component's declared sizes leave out, on each axis: all of it for a content box,
		// none for a border box.
		Foundation::Vec2 outsidePadding(const Component& node) {
			const Layout& layout = node.getLayout();
			return layout.boxSizing == BoxSizing::ContentBox ? layout.padding.total() : Foundation::Vec2{0.0F, 0.0F};
		}

		// A declared size, minSize or maxSize as the whole box it sets, `outside` the padding a content
		// box adds; none for auto.
		std::optional<float> boxSize(const Length& length, float base, float outside) {
			const std::optional<float> value = length.resolve(base);
			return value ? std::optional(*value + outside) : std::nullopt;
		}

		float clampAxis(float value, const Length& minimum, const Length& maximum, float base, float outside) {
			const float low = std::max(boxSize(minimum, base, outside).value_or(0.0F), 0.0F);
			const float high = boxSize(maximum, base, outside).value_or(kUnbounded);
			return std::max(std::min(value, high), low);
		}

		// A grid child's minimum contribution on one axis, margins in, where it isn't CSS's automatic
		// minimum: its size when that's definite, or its minSize.
		std::optional<float> gridMinimum(bool definite, float size, const Length& minimum, float base, float outside, float margins) {
			if (definite) {
				return size + margins;
			}
			if (!minimum.isAuto()) {
				return std::max(boxSize(minimum, base, outside).value_or(0.0F), 0.0F) + margins;
			}
			return std::nullopt;
		}

		// Where each track starts, gaps between them.
		std::vector<float> trackStarts(const std::vector<float>& sizes, float gap) {
			std::vector<float> starts(sizes.size());
			float			   at = 0.0F;
			for (std::size_t i = 0; i < sizes.size(); ++i) {
				starts[i] = at;
				at += sizes[i] + gap;
			}
			return starts;
		}

		float areaExtent(const std::vector<float>& sizes, TrackRange range, float gap) {
			float extent = gap * static_cast<float>(range.count - 1);
			for (std::uint32_t i = range.start; i < range.end(); ++i) {
				extent += sizes[i];
			}
			return extent;
		}

		// Where a margin box `size` long starts in `extent`; one that doesn't fit starts.
		float alignedIn(float extent, float size, AlignItems align) {
			switch (align) {
				case AlignItems::Center:
					return std::max((extent - size) * 0.5F, 0.0F);
				case AlignItems::End:
					return std::max(extent - size, 0.0F);
				case AlignItems::Start:
				case AlignItems::Stretch:
					break;
			}
			return 0.0F;
		}

		// A flex child with an aspect ratio and a declared main size takes its cross size from the ratio,
		// so it isn't stretched across the line.
		bool ratioSetsCross(const Component& child, bool row) {
			return child.getLayout().aspectRatio > 0.0F && !(row ? child.getSizeSpec().width : child.getSizeSpec().height).isAuto();
		}

		// A relatively positioned child's inset nudges it from where the flow put it, a percentage of `box`.
		Foundation::Vec2 nudged(Foundation::Vec2 at, const Inset& inset, Foundation::Vec2 box) {
			const std::optional<float> left = inset.left.resolve(box.x);
			const std::optional<float> top = inset.top.resolve(box.y);
			at.x += left ? *left : -inset.right.resolve(box.x).value_or(0.0F);
			at.y += top ? *top : -inset.bottom.resolve(box.y).value_or(0.0F);
			return at;
		}
	} // namespace

	LayoutEngine::LayoutEngine(const Registry& owner, std::uint32_t layoutPass, std::vector<ComponentId>& movedComponents)
		: registry(owner),
		  pass(layoutPass),
		  moved(movedComponents) {}

	// --- Measuring ---

	Foundation::Vec2 LayoutEngine::measure(
		Component&			 node,
		std::optional<float> width,
		std::optional<float> height,
		Foundation::Vec2	 parentBox,
		float				 availableWidth
	) {
		if (!node.isShown()) {
			return {0.0F, 0.0F};
		}
		for (const Component::Measured& kept : node.measured) {
			if (kept.valid && (kept.pass == pass || !node.layoutDirty) && sameMeasure(kept.width, width) &&
				sameMeasure(kept.height, height) && kept.parentBox == parentBox && kept.availableWidth == availableWidth) {
				return kept.result;
			}
		}

		const Foundation::Vec2 outside = outsidePadding(node); // what a content box adds to its declared sizes

		const auto clampWidth = [&](float value) {
			return clampAxis(value, node.minSpec.width, node.maxSpec.width, parentBox.x, outside.x);
		};
		const auto clampHeight = [&](float value) {
			return clampAxis(value, node.minSpec.height, node.maxSpec.height, parentBox.y, outside.y);
		};
		std::optional<float> resolvedWidth = width ? width : boxSize(node.sizeSpec.width, parentBox.x, outside.x);
		std::optional<float> resolvedHeight = height ? height : boxSize(node.sizeSpec.height, parentBox.y, outside.y);
		if (resolvedWidth) {
			resolvedWidth = clampWidth(*resolvedWidth);
		}
		if (resolvedHeight) {
			resolvedHeight = clampHeight(*resolvedHeight);
		}
		// An aspect ratio gives the auto axis from the other, in the box boxSizing names; a clamp on
		// the axis it gave wins over it. With both auto the width is the content's and the height
		// follows, and a clamp on that height takes the width back with it.
		const float ratio = node.layout.aspectRatio;
		if (ratio > 0.0F) {
			const auto heightFor = [&](float boxWidth) {
				return (std::max(boxWidth - outside.x, 0.0F) / ratio) + outside.y;
			};
			const auto widthFor = [&](float boxHeight) {
				return (std::max(boxHeight - outside.y, 0.0F) * ratio) + outside.x;
			};
			if (resolvedWidth && !resolvedHeight) {
				resolvedHeight = clampHeight(heightFor(*resolvedWidth));
			} else if (resolvedHeight && !resolvedWidth) {
				resolvedWidth = clampWidth(widthFor(*resolvedHeight));
			} else if (!resolvedWidth && !resolvedHeight) {
				const float room = std::min(
					std::max(availableWidth - node.margin.horizontal(), 0.0F),
					boxSize(node.maxSpec.width, parentBox.x, outside.x).value_or(kUnbounded)
				);
				float		boxWidth = clampWidth(contentSize(node, std::nullopt, std::nullopt, room).x);
				const float wanted = heightFor(boxWidth);
				const float boxHeight = clampHeight(wanted);
				if (boxHeight != wanted) {
					boxWidth = clampWidth(widthFor(boxHeight));
				}
				resolvedWidth = boxWidth;
				resolvedHeight = boxHeight;
			}
		}
		if (!resolvedWidth || !resolvedHeight) {
			const float		 room = resolvedWidth ? *resolvedWidth
												  : std::min(
													std::max(availableWidth - node.margin.horizontal(), 0.0F),
													boxSize(node.maxSpec.width, parentBox.x, outside.x).value_or(kUnbounded)
												);
			Foundation::Vec2 content = contentSize(node, resolvedWidth, resolvedHeight, room);
			if (!resolvedWidth) {
				const float clamped = clampWidth(content.x);
				// Its height may hang on its width (a wrapping text), so a clamp means measuring again.
				if (clamped != content.x && !resolvedHeight) {
					content = contentSize(node, clamped, std::nullopt, room);
				}
				resolvedWidth = clamped;
			}
			if (!resolvedHeight) {
				resolvedHeight = clampHeight(content.y);
			}
		}

		const Foundation::Vec2 result{*resolvedWidth, *resolvedHeight};
		node.measured[1] = node.measured[0];
		node.measured[0] = {
			.width = width,
			.height = height,
			.parentBox = parentBox,
			.availableWidth = availableWidth,
			.result = result,
			.pass = pass,
			.valid = true,
		};
		return result;
	}

	Foundation::Vec2
	LayoutEngine::contentSize(Component& node, std::optional<float> width, std::optional<float> height, float availableWidth) {
		if (const std::optional<Foundation::Vec2> own = node.intrinsicSize(width, availableWidth)) {
			return *own;
		}
		if (node.layout.display != Display::Flex && node.layout.display != Display::Grid) {
			return freeContent(node, width, height);
		}
		const Edges&		 padding = node.layout.padding;
		std::optional<float> contentWidth = width ? std::optional(std::max(*width - padding.horizontal(), 0.0F)) : std::nullopt;
		std::optional<float> contentHeight = height ? std::optional(std::max(*height - padding.vertical(), 0.0F)) : std::nullopt;
		const float			 room = std::max(availableWidth - padding.horizontal(), 0.0F);
		if (node.layout.display == Display::Grid) {
			layoutGrid(node, contentWidth, contentHeight, room);
			const float gap = std::max(node.layout.gap, 0.0F);
			return {gridExtent(grid.columns, gap) + padding.horizontal(), gridExtent(grid.rows, gap) + padding.vertical()};
		}
		flex(node, contentWidth, contentHeight, room);
		// The longest line along, the lines across, a gap between each.
		float main = 0.0F;
		float cross = 0.0F;
		for (const FlexLine& line : lines) {
			float along = 0.0F;
			for (std::size_t i = line.begin; i < line.end; ++i) {
				along += items[i].main + items[i].marginMain;
			}
			along += node.layout.gap * static_cast<float>(line.end - line.begin - 1);
			main = std::max(main, along);
			cross += line.cross;
		}
		if (lines.size() > 1) {
			cross += std::max(node.layout.gap, 0.0F) * static_cast<float>(lines.size() - 1);
		}
		const bool row = node.layout.flexDirection == FlexDirection::Row;
		return row ? Foundation::Vec2{main + padding.horizontal(), cross + padding.vertical()}
				   : Foundation::Vec2{cross + padding.horizontal(), main + padding.vertical()};
	}

	// A Free component hugs its children where they sit; absolutely positioned ones are out of it.
	Foundation::Vec2 LayoutEngine::freeContent(Component& node, std::optional<float> width, std::optional<float> height) {
		const Foundation::Vec2 box{width.value_or(-1.0F), height.value_or(-1.0F)};
		Foundation::Vec2	   extent{0.0F, 0.0F};
		for (const ComponentId childId : node.children) {
			Component* child = registry.find(childId);
			if (child == nullptr || !child->isShown() || child->layout.position == Position::Absolute) {
				continue;
			}
			const float			   room = width ? std::max(*width - child->position.x, 0.0F) : kUnbounded;
			const Foundation::Vec2 childSize = measure(*child, std::nullopt, std::nullopt, box, room);
			extent = glm::max(extent, child->position + childSize + child->margin.total());
		}
		return extent;
	}

	// Leaves `items` holding each flowing child's sizes and `lines` the runs of them on each line. A
	// nested measure runs flex() too, so this works on local lists and swaps them in only at the end.
	void LayoutEngine::flex(Component& node, std::optional<float> contentWidth, std::optional<float> contentHeight, float availableWidth) {
		const bool			   row = node.layout.flexDirection == FlexDirection::Row;
		const bool			   wraps = node.layout.flexWrap == FlexWrap::Wrap;
		std::optional<float>   contentMain = row ? contentWidth : contentHeight;
		std::optional<float>   contentCross = row ? contentHeight : contentWidth;
		const Foundation::Vec2 box{contentWidth.value_or(-1.0F), contentHeight.value_or(-1.0F)};
		const float			   mainBase = row ? box.x : box.y;
		const float			   gap = node.layout.gap;
		// A row measures its children unbounded (their max-content basis); a column bounds them by its width.
		const float room = row ? kUnbounded : (contentCross ? *contentCross : availableWidth);
		const auto	measureAt = [&](Component& child, std::optional<float> main, std::optional<float> cross) {
			 return row ? measure(child, main, cross, box, room) : measure(child, cross, main, box, room);
		};

		std::vector<FlexItem> flowing;
		for (const ComponentId childId : node.children) {
			Component* child = registry.find(childId);
			if (child == nullptr || !child->isShown() || child->layout.position == Position::Absolute) {
				continue;
			}
			const AlignItems	 align = child->layout.alignSelf.value_or(node.layout.alignItems);
			const Length&		 crossSpec = row ? child->sizeSpec.height : child->sizeSpec.width;
			const float			 marginMain = row ? child->margin.horizontal() : child->margin.vertical();
			const float			 marginCross = row ? child->margin.vertical() : child->margin.horizontal();
			std::optional<float> stretched;
			// One line is as big across as the container; a wrapping child stretches to its own line, below.
			if (!wraps && align == AlignItems::Stretch && crossSpec.isAuto() && contentCross && !ratioSetsCross(*child, row)) {
				stretched = std::max(*contentCross - marginCross, 0.0F);
			}
			const Foundation::Vec2 measured = measureAt(*child, std::nullopt, stretched);
			const float			   basis = row ? measured.x : measured.y;
			const Length&		   minSpec = row ? child->minSpec.width : child->minSpec.height;
			const Length&		   maxSpec = row ? child->maxSpec.width : child->maxSpec.height;
			const float			   automaticMin = row ? std::min(child->minContentWidth(), basis) : 0.0F;
			const float			   outsideMain = row ? outsidePadding(*child).x : outsidePadding(*child).y;
			flowing.push_back({
				.component = child,
				.basis = basis,
				.main = basis,
				.cross = row ? measured.y : measured.x,
				.marginMain = marginMain,
				.marginCross = marginCross,
				.minMain = minSpec.isAuto() ? automaticMin : boxSize(minSpec, mainBase, outsideMain).value_or(0.0F),
				.maxMain = boxSize(maxSpec, mainBase, outsideMain).value_or(kUnbounded),
				.stretchedCross = stretched,
				.frozen = false,
			});
		}

		// The lines: one, or a new one where the next margin box, at its clamped basis, would pass the
		// main size, or for an auto width the room there is, as a text wraps.
		std::optional<float> limit = contentMain;
		if (!limit && row && std::isfinite(availableWidth)) {
			limit = availableWidth;
		}
		std::vector<FlexLine> broken;
		float				  used = 0.0F;
		for (std::size_t i = 0; i < flowing.size(); ++i) {
			const FlexItem& item = flowing[i];
			const float		outer = std::max(std::min(item.basis, item.maxMain), item.minMain) + item.marginMain;
			if (broken.empty() || (wraps && limit && used + gap + outer > *limit)) {
				broken.push_back({.begin = i, .end = i, .cross = 0.0F});
				used = outer;
			} else {
				used += gap + outer;
			}
			broken.back().end = i + 1;
		}

		for (FlexLine& line : broken) {
			const std::span<FlexItem> run(flowing.data() + line.begin, line.end - line.begin);
			if (contentMain) {
				distribute(run, *contentMain, gap, node.overflow != Overflow::Scroll);
			}
			// A child whose main size moved is measured again across it: a text that wraps grows taller.
			for (FlexItem& item : run) {
				if (item.main != item.basis && !item.stretchedCross) {
					const Foundation::Vec2 measured = measureAt(*item.component, item.main, std::nullopt);
					item.cross = row ? measured.y : measured.x;
				}
				line.cross = std::max(line.cross, item.cross + item.marginCross);
			}
			if (!wraps) {
				continue;
			}
			for (FlexItem& item : run) {
				const Component& child = *item.component;
				const AlignItems align = child.layout.alignSelf.value_or(node.layout.alignItems);
				if (align == AlignItems::Stretch && (row ? child.sizeSpec.height : child.sizeSpec.width).isAuto() &&
					!ratioSetsCross(child, row)) {
					item.stretchedCross = std::max(line.cross - item.marginCross, 0.0F);
					const Foundation::Vec2 measured = measureAt(*item.component, item.main, item.stretchedCross);
					item.cross = row ? measured.y : measured.x;
				}
			}
		}
		items = std::move(flowing);
		lines = std::move(broken);
	}

	// Once, then again with whatever a min or max clamped held fixed (CSS's frozen items).
	void LayoutEngine::distribute(std::span<FlexItem> line, float contentMain, float gap, bool shrinks) {
		const float gaps = line.size() > 1 ? gap * static_cast<float>(line.size() - 1) : 0.0F;
		for (int round = 0; round < 2; ++round) {
			float used = gaps;
			float grow = 0.0F;
			float shrink = 0.0F;
			for (const FlexItem& item : line) {
				used += (item.frozen ? item.main : item.basis) + item.marginMain;
				if (!item.frozen) {
					grow += std::max(item.component->layout.flexGrow, 0.0F);
					shrink += std::max(item.component->layout.flexShrink, 0.0F) * item.basis;
				}
			}
			const float free = contentMain - used;
			bool		clamped = false;
			for (FlexItem& item : line) {
				if (item.frozen) {
					continue;
				}
				float main = item.basis;
				if (free > 0.0F && grow > 0.0F) {
					main += free * std::max(item.component->layout.flexGrow, 0.0F) / grow;
				} else if (free < 0.0F && shrink > 0.0F && shrinks) {
					main += free * std::max(item.component->layout.flexShrink, 0.0F) * item.basis / shrink;
				}
				const float held = std::max(std::min(main, item.maxMain), item.minMain);
				if (held != main) {
					clamped = true;
					item.frozen = round == 0;
				}
				item.main = std::max(held, 0.0F);
			}
			if (!clamped) {
				break;
			}
		}
	}

	// Leaves `grid` holding the tracks and each flowing child's area, decided width and measure. A
	// nested measure lays out grids too, so this works on a local result and moves it in at the end.
	void
	LayoutEngine::layoutGrid(Component& node, std::optional<float> contentWidth, std::optional<float> contentHeight, float availableWidth) {
		const Layout&		   layout = node.layout;
		const float			   gap = std::max(layout.gap, 0.0F);
		const Foundation::Vec2 box{contentWidth.value_or(-1.0F), contentHeight.value_or(-1.0F)};

		GridResult				   result;
		std::vector<GridItemLines> lines;
		for (const ComponentId childId : node.children) {
			Component* child = registry.find(childId);
			if (child == nullptr || !child->isShown() || child->layout.position == Position::Absolute) {
				continue;
			}
			result.items.push_back({.component = child, .area = {}, .width = std::nullopt, .measured = {0.0F, 0.0F}});
			lines.push_back({.column = child->layout.gridColumn, .row = child->layout.gridRow});
		}
		PlacedGrid placed;
		placeGridItems(
			{.explicitColumns = static_cast<std::uint32_t>(layout.gridTemplateColumns.size()),
			 .explicitRows = static_cast<std::uint32_t>(layout.gridTemplateRows.size()),
			 .items = lines},
			placed
		);

		// Columns, from each child's width unbounded and the narrowest a flex row takes it to.
		std::vector<TrackSizingFunction> tracks;
		std::vector<TrackContribution>	 contributions(result.items.size());
		gridTracks(layout.gridTemplateColumns, placed.columnsBefore, placed.columns, tracks);
		for (std::size_t i = 0; i < result.items.size(); ++i) {
			Component& child = *result.items[i].component;
			result.items[i].area = placed.areas[i];
			const bool	definite = child.sizeSpec.width.resolve(box.x).has_value();
			const float widest = measure(child, std::nullopt, std::nullopt, box, kUnbounded).x;
			const float outside = outsidePadding(child).x;
			const float narrowest =
				definite ? widest
						 : std::min(clampAxis(child.minContentWidth(), child.minSpec.width, child.maxSpec.width, box.x, outside), widest);
			const float margins = child.margin.horizontal();
			contributions[i] = {
				.tracks = placed.areas[i].column,
				.minContent = narrowest + margins,
				.maxContent = widest + margins,
				.minimum = gridMinimum(definite, widest, child.minSpec.width, box.x, outside, margins),
			};
		}
		sizeGridTracks({.tracks = tracks, .available = contentWidth, .gap = gap, .items = contributions}, result.columns);
		if (!contentWidth && std::isfinite(availableWidth) && gridExtent(result.columns, gap) > availableWidth) {
			sizeGridTracks({.tracks = tracks, .available = availableWidth, .gap = gap, .items = contributions}, result.columns);
		}

		// Rows, from each child's height at its columns' width: stretched to it when its width is auto,
		// wrapped to it otherwise.
		gridTracks(layout.gridTemplateRows, placed.rowsBefore, placed.rows, tracks);
		for (std::size_t i = 0; i < result.items.size(); ++i) {
			GridItem&		 item = result.items[i];
			Component&		 child = *item.component;
			const float		 areaWidth = areaExtent(result.columns, item.area.column, gap);
			const AlignItems justify = child.layout.justifySelf.value_or(layout.justifyItems);
			if (justify == AlignItems::Stretch && child.sizeSpec.width.isAuto()) {
				item.width = std::max(areaWidth - child.margin.horizontal(), 0.0F);
			}
			item.measured = measure(child, item.width, std::nullopt, box, areaWidth);
			const bool	definite = child.sizeSpec.height.resolve(box.y).has_value();
			const float margins = child.margin.vertical();
			contributions[i] = {
				.tracks = item.area.row,
				.minContent = item.measured.y + margins,
				.maxContent = item.measured.y + margins,
				.minimum = gridMinimum(definite, item.measured.y, child.minSpec.height, box.y, outsidePadding(child).y, margins),
			};
		}
		sizeGridTracks({.tracks = tracks, .available = contentHeight, .gap = gap, .items = contributions}, result.rows);
		grid = std::move(result);
	}

	// --- Arranging ---

	void LayoutEngine::arrange(Component& node) {
		node.layoutDirty = false;
		if (!node.isShown()) {
			return;
		}
		if (node.layout.display == Display::Flex) {
			arrangeFlex(node);
		} else if (node.layout.display == Display::Grid) {
			arrangeGrid(node);
		} else {
			arrangeFree(node);
		}
		if (node.overflow == Overflow::Scroll) {
			scrollExtent(node);
		}
	}

	// How far a scroller scrolls: its children's margin boxes and its padding past its box (R4.13).
	void LayoutEngine::scrollExtent(Component& node) {
		Foundation::Vec2 content{0.0F, 0.0F};
		for (const ComponentId childId : node.children) {
			const Component* child = registry.find(childId);
			if (child != nullptr && child->isShown() && !child->isOverlay()) {
				content = glm::max(content, child->offset + child->size + child->margin.total());
			}
		}
		content += Foundation::Vec2{node.layout.padding.right, node.layout.padding.bottom};
		node.scrollMax = glm::max(content - node.size, Foundation::Vec2{0.0F, 0.0F});
		node.scroll = glm::clamp(node.scroll, Foundation::Vec2{0.0F, 0.0F}, node.scrollMax);
	}

	void LayoutEngine::arrangeFree(Component& node) {
		for (const ComponentId childId : node.children) {
			Component* child = registry.find(childId);
			if (child == nullptr || !child->isShown()) {
				continue;
			}
			if (child->layout.position == Position::Absolute) {
				placeAbsolute(*child, node.size);
				continue;
			}
			const Foundation::Vec2 childSize =
				measure(*child, std::nullopt, std::nullopt, node.size, std::max(node.size.x - child->position.x, 0.0F));
			place(*child, child->position, childSize);
		}
	}

	void LayoutEngine::arrangeFlex(Component& node) {
		const Edges& padding = node.layout.padding;
		const float	 contentWidth = std::max(node.size.x - padding.horizontal(), 0.0F);
		const float	 contentHeight = std::max(node.size.y - padding.vertical(), 0.0F);
		flex(node, contentWidth, contentHeight, contentWidth);
		const std::vector<FlexItem> flowing = std::move(items);
		const std::vector<FlexLine> laid = std::move(lines);

		const bool	row = node.layout.flexDirection == FlexDirection::Row;
		const bool	wraps = node.layout.flexWrap == FlexWrap::Wrap;
		const float contentMain = row ? contentWidth : contentHeight;
		const float contentCross = row ? contentHeight : contentWidth;
		const float gap = node.layout.gap;
		float		lineAt = 0.0F;
		for (const FlexLine& line : laid) {
			const std::span<const FlexItem> run(flowing.data() + line.begin, line.end - line.begin);
			// One line is the container's size across; a wrapped one is its biggest child's.
			const float lineCross = wraps ? line.cross : contentCross;
			float		total = 0.0F;
			for (const FlexItem& item : run) {
				total += item.main + item.marginMain;
			}
			const auto	count = static_cast<float>(run.size());
			const float gaps = run.size() > 1 ? gap * (count - 1.0F) : 0.0F;
			const float leftover = std::max(contentMain - total - gaps, 0.0F);

			// R10.9's distribution, on top of the gap.
			float start = 0.0F;
			float between = 0.0F;
			switch (node.layout.justifyContent) {
				case JustifyContent::Start:
					break;
				case JustifyContent::Center:
					start = leftover * 0.5F;
					break;
				case JustifyContent::End:
					start = leftover;
					break;
				case JustifyContent::SpaceBetween:
					between = run.size() > 1 ? leftover / (count - 1.0F) : 0.0F;
					break;
				case JustifyContent::SpaceAround:
					between = count > 0.0F ? leftover / count : 0.0F;
					start = between * 0.5F;
					break;
				case JustifyContent::SpaceEvenly:
					between = leftover / (count + 1.0F);
					start = between;
					break;
			}

			float cursor = start;
			for (const FlexItem& item : run) {
				Component&		 child = *item.component;
				const AlignItems align = child.layout.alignSelf.value_or(node.layout.alignItems);
				const float		 crossBox = item.cross + item.marginCross;
				float			 crossAt = lineAt;
				if (align == AlignItems::Center) {
					crossAt += std::max((lineCross - crossBox) * 0.5F, 0.0F);
				} else if (align == AlignItems::End) {
					crossAt += std::max(lineCross - crossBox, 0.0F);
				}
				const Foundation::Vec2 at =
					padding.topLeft() + (row ? Foundation::Vec2{cursor, crossAt} : Foundation::Vec2{crossAt, cursor});
				place(
					child,
					nudged(at, child.layout.inset, {contentWidth, contentHeight}),
					row ? Foundation::Vec2{item.main, item.cross} : Foundation::Vec2{item.cross, item.main}
				);
				cursor += item.main + item.marginMain + gap + between;
			}
			lineAt += line.cross + std::max(gap, 0.0F);
		}
		placeAbsoluteChildren(node);
	}

	void LayoutEngine::arrangeGrid(Component& node) {
		const Edges& padding = node.layout.padding;
		const float	 contentWidth = std::max(node.size.x - padding.horizontal(), 0.0F);
		const float	 contentHeight = std::max(node.size.y - padding.vertical(), 0.0F);
		layoutGrid(node, contentWidth, contentHeight, contentWidth);
		const GridResult laid = std::move(grid);

		const Foundation::Vec2	 box{contentWidth, contentHeight};
		const float				 gap = std::max(node.layout.gap, 0.0F);
		const std::vector<float> columnStarts = trackStarts(laid.columns, gap);
		const std::vector<float> rowStarts = trackStarts(laid.rows, gap);
		for (const GridItem& item : laid.items) {
			Component&		 child = *item.component;
			const float		 areaWidth = areaExtent(laid.columns, item.area.column, gap);
			const float		 areaHeight = areaExtent(laid.rows, item.area.row, gap);
			const AlignItems align = child.layout.alignSelf.value_or(node.layout.alignItems);
			const AlignItems justify = child.layout.justifySelf.value_or(node.layout.justifyItems);
			Foundation::Vec2 childSize = item.measured;
			const bool		 ratioSetsHeight = child.layout.aspectRatio > 0.0F && (item.width || !child.sizeSpec.width.isAuto());
			if (align == AlignItems::Stretch && child.sizeSpec.height.isAuto() && !ratioSetsHeight) {
				childSize = measure(child, item.width, std::max(areaHeight - child.margin.vertical(), 0.0F), box, areaWidth);
			}
			const Foundation::Vec2 margins = child.margin.total();
			const Foundation::Vec2 at{
				padding.left + columnStarts[item.area.column.start] + alignedIn(areaWidth, childSize.x + margins.x, justify),
				padding.top + rowStarts[item.area.row.start] + alignedIn(areaHeight, childSize.y + margins.y, align),
			};
			place(child, nudged(at, child.layout.inset, {areaWidth, areaHeight}), childSize);
		}
		placeAbsoluteChildren(node);
	}

	void LayoutEngine::placeAbsoluteChildren(Component& node) {
		for (const ComponentId childId : node.children) {
			Component* child = registry.find(childId);
			if (child != nullptr && child->isShown() && child->layout.position == Position::Absolute) {
				placeAbsolute(*child, node.size);
			}
		}
	}

	// Against the parent's whole box, its padding included: an inset edge anchors that side, both of a
	// pair stretch an auto size between them, and none falls back to `position`; a percentage is of the
	// box's width or height. An overlay entry goes on screen where its placement puts it, not in its
	// opener's box, so only its own maxSize bounds the width it fills.
	void LayoutEngine::placeAbsolute(Component& child, Foundation::Vec2 box) {
		const Inset&			   inset = child.layout.inset;
		const std::optional<float> top = inset.top.resolve(box.y);
		const std::optional<float> right = inset.right.resolve(box.x);
		const std::optional<float> bottom = inset.bottom.resolve(box.y);
		const std::optional<float> left = inset.left.resolve(box.x);
		std::optional<float>	   width;
		std::optional<float>	   height;
		if (child.sizeSpec.width.isAuto() && left && right) {
			width = std::max(box.x - *left - *right - child.margin.horizontal(), 0.0F);
		}
		if (child.sizeSpec.height.isAuto() && top && bottom) {
			height = std::max(box.y - *top - *bottom - child.margin.vertical(), 0.0F);
		}
		const float			   from = left.value_or(right ? 0.0F : child.position.x);
		const float			   available = child.isOverlay() ? kUnbounded : std::max(box.x - from, 0.0F);
		const Foundation::Vec2 childSize = measure(child, width, height, box, available);
		const Foundation::Vec2 margins = child.margin.total();
		const float			   x = left ? *left : (right ? box.x - *right - childSize.x - margins.x : child.position.x);
		const float			   y = top ? *top : (bottom ? box.y - *bottom - childSize.y - margins.y : child.position.y);
		place(child, {x, y}, childSize);
	}

	void LayoutEngine::place(Component& child, Foundation::Vec2 at, Foundation::Vec2 childSize) {
		const bool resized = child.size != childSize;
		if (resized || child.offset != at || !child.laidOut) {
			moved.push_back(child.selfId);
		}
		child.laidOut = true;
		child.offset = at;
		child.size = childSize;
		if (resized || child.layoutDirty) {
			arrange(child);
		}
	}

} // namespace ui::detail
