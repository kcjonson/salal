#pragma once

// The layout lint, /api/ui/lint (R13.25 to R13.28): a developer's helper for building screens, never a
// gate in CI (docs/ui-design.md, "Layout lint"). One walk over the boxes layout placed, untransformed,
// with a pairwise check among siblings:
//
//   sibling-overlap        two visible siblings with the same effective zIndex overlap by more than
//                          0.5 px on both axes; touching edges don't
//   child-outside-parent   a visible child's box leaves its parent's by more than 0.5 px
//   outside-viewport       a visible box leaves the viewport by more than 0.5 px
//   zero-or-negative-size  a visible box with no width or no height
//   text-overflow          a text whose laid-out extent passes its box by more than 0.5 px; its
//                          bounds are the text's, its other bounds the box
//   unreachable-interactive  a component that answers the pointer, clipped away where no scroller
//                          brings it back, or covered whole by a sibling painted over it that takes
//                          the pointer (its other path)
//
// A component whose own pointerEvents is None is decoration (R8.29): its owner paints it over or under
// the rest on purpose, a caret's strokes, a hairline under tabs, a slider's thumb or a focus ring, and
// the pointer never reaches it. It is exempt from sibling-overlap, on either side, from
// child-outside-parent, and from zero-or-negative-size, since a fill at 0 is empty on purpose; its
// children are judged by their own.
//
// Ink past a box (a shadow) is not overflow. Invisible subtrees are skipped. The roots are siblings,
// each the viewport's size and layered on purpose, so they don't overlap each other. Paths join ids,
// or Type[index] where there is none, with '/'; bounds are in the viewport. A child's bounds are its
// margin box, and it is contained by its parent's box, unless the parent clips (Overflow::Clip or
// Scroll), which declares it may run past; then only the part the clip shows must be in the viewport.

#include <graphics/Rect.h>
#include <math/Types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ui {

	class Context;

	enum class LintRule : std::uint8_t {
		SiblingOverlap,
		ChildOutsideParent,
		OutsideViewport,
		ZeroOrNegativeSize,
		TextOverflow,
		UnreachableInteractive,
	};

	[[nodiscard]] const char* lintRuleName(LintRule rule);

	struct LintViolation {
		LintRule		 rule;
		std::string		 path;
		Foundation::Rect bounds;
		std::string		 otherPath; // overlap: the sibling; containment: the parent, or "viewport"
		Foundation::Rect otherBounds;
	};

	struct LintResult {
		std::vector<LintViolation> violations;

		[[nodiscard]] bool clean() const { return violations.empty(); }
	};

	// Against `viewport`, which needn't be the context's, so a screen can be checked at a size the
	// window isn't.
	[[nodiscard]] LintResult lint(const Context& context, Foundation::Vec2 viewport);
	// `context` null: a scene without ui, `{"count":0,"violations":[]}`.
	[[nodiscard]] std::string lintJson(const Context* context, Foundation::Vec2 viewport);

} // namespace ui
