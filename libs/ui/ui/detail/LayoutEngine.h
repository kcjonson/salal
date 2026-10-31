#pragma once

// The layout pass, internal to ui; the context runs it. After worldsim's LayoutContainer
// (libs/ui/layout/LayoutContainer.cpp, a4226e17), reworked to CSS's names and to measure and place
// in one pass over the tree rather than inside each container's render:
//
//   measure  the size a component takes for the axes its parent knows, its auto axes from its
//            content: a leaf's own (a text), a Free one's children's boxes, a Flex's flow. Kept per
//            component while nothing under it changes.
//   arrange  a component whose size is decided places its children and decides theirs, then
//            arranges each child that changed or holds a change.
//
// Flex is single-line: basis from the child's size or content, the leftover grown by flexGrow or
// the overflow taken by flexShrink weighted by basis, min and max clamped, then the distribution run
// once more with the clamped children fixed (R10.4). A text's automatic minimum in a row is its
// longest word, or its whole width when it doesn't wrap. A scroller's flex children don't shrink: it
// scrolls them instead.

#include "ui/Component.h"

#include <cstdint>
#include <optional>
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

		Foundation::Vec2 contentSize(Component& node, std::optional<float> width, std::optional<float> height, float availableWidth);
		Foundation::Vec2 freeContent(Component& node, std::optional<float> width, std::optional<float> height);
		// Each flowing child's main and cross size inside a content box whose known axes are given,
		// left in `items`.
		void flex(Component& node, std::optional<float> contentWidth, std::optional<float> contentHeight, float availableWidth);
		void arrangeFlex(Component& node);
		void arrangeFree(Component& node);
		void scrollExtent(Component& node);
		void placeAbsolute(Component& child, Foundation::Vec2 box);
		void place(Component& child, Foundation::Vec2 offset, Foundation::Vec2 size);

		const Registry&			  registry;
		std::uint32_t			  pass;
		std::vector<ComponentId>& moved;
		std::vector<FlexItem>	  items;
	};

} // namespace ui::detail
