#pragma once

// The layout pass, internal to ui; the context runs it. After worldsim's LayoutContainer
// (libs/ui/layout/LayoutContainer.cpp, a4226e17), reworked to CSS's names and to measure and place
// in one pass over the tree rather than inside each container's render:
//
//   measure  the size a component takes for the axes its parent knows, its auto axes from its
//            content: a leaf's own (a text), a Free one's children's boxes, a Flex's flow, a Grid's
//            tracks. Kept per component while nothing under it changes.
//   arrange  a component whose size is decided places its children and decides theirs, then
//            arranges each child that changed or holds a change.
//
// Flex: basis from the child's size or content, the leftover grown by flexGrow or the overflow taken
// by flexShrink weighted by basis, min and max clamped, then the distribution run once more with the
// clamped children fixed (R10.4). A text's automatic minimum in a row is its longest word, or its
// whole width when it doesn't wrap. A scroller's flex children don't shrink: it scrolls them instead.
// With flexWrap the children break into lines first, at the main size or, for an auto width, the
// room there is, each line distributed on its own; a line is as tall as its tallest child, a
// stretched child is stretched to its line, and the lines stack from the start.
//
// Grid places its children (ui/detail/Grid.h), sizes the columns from their widths, measures each
// at its columns' width for its height, then sizes the rows. A child's max-content width is its
// width measured unbounded; its min-content width is its own size when that's definite, or the
// narrowest a flex row takes it to (a text's longest word, a container's 0). A grid whose width is
// auto takes its columns' max-content widths, sized again into the room there is when they don't fit
// it, as a text wraps.

#include "ui/Component.h"
#include "ui/detail/Grid.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ui::detail {

	class LayoutEngine {
	  public:
		// `moved` collects the components the pass moved or resized, for their onLayout.
		LayoutEngine(const Registry& registry, std::uint32_t pass, std::vector<ComponentId>& moved);

		// `node`'s size is decided: lay out what's under it.
		void arrange(Component& node);
		// The size of `node`'s box, margin excluded. `width` and `height` are axes the caller has
		// decided; `parentBox` is what its percentages are of (an axis below 0: not known yet);
		// `availableWidth` bounds an auto width that wraps.
		Foundation::Vec2
		measure(Component& node, std::optional<float> width, std::optional<float> height, Foundation::Vec2 parentBox, float availableWidth);

	  private:
		struct FlexItem {
			Component*			 component;
			float				 basis;
			float				 main;
			float				 cross;
			float				 marginMain;
			float				 marginCross;
			float				 minMain;
			float				 maxMain;
			std::optional<float> stretchedCross;
			bool				 frozen;
		};

		// A run of `items` on one line, and the line's size across the main axis: its biggest margin box.
		struct FlexLine {
			std::size_t begin;
			std::size_t end;
			float		cross;
		};

		// A grid child, its area, and the box it takes there: a definite or stretched width or none,
		// which it wraps to its area's width.
		struct GridItem {
			Component*			 component;
			GridArea			 area;
			std::optional<float> width;
			Foundation::Vec2	 measured;
		};

		struct GridResult {
			std::vector<GridItem> items;
			std::vector<float>	  columns;
			std::vector<float>	  rows;
		};

		Foundation::Vec2 contentSize(Component& node, std::optional<float> width, std::optional<float> height, float availableWidth);
		Foundation::Vec2 freeContent(Component& node, std::optional<float> width, std::optional<float> height);
		// Each flowing child's main and cross size inside a content box whose known axes are given, and
		// the lines they're on, left in `items` and `lines`.
		void flex(Component& node, std::optional<float> contentWidth, std::optional<float> contentHeight, float availableWidth);
		// Grows or shrinks one line's children into `contentMain`.
		static void distribute(std::span<FlexItem> line, float contentMain, float gap, bool shrinks);
		// The grid's tracks and each flowing child's area inside a content box whose known axes are
		// given, left in `grid`.
		void layoutGrid(Component& node, std::optional<float> contentWidth, std::optional<float> contentHeight, float availableWidth);
		void arrangeFlex(Component& node);
		void arrangeGrid(Component& node);
		void arrangeFree(Component& node);
		void placeAbsoluteChildren(Component& node);
		void scrollExtent(Component& node);
		void placeAbsolute(Component& child, Foundation::Vec2 box);
		void place(Component& child, Foundation::Vec2 offset, Foundation::Vec2 size);

		const Registry&			  registry;
		std::uint32_t			  pass;
		std::vector<ComponentId>& moved;
		std::vector<FlexItem>	  items;
		std::vector<FlexLine>	  lines;
		GridResult				  grid;
	};

} // namespace ui::detail
