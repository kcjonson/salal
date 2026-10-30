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

		float clampAxis(float value, const Length& minimum, const Length& maximum, float base) {
			const float low = std::max(minimum.resolve(base).value_or(0.0F), 0.0F);
			const float high = maximum.resolve(base).value_or(kUnbounded);
			return std::max(std::min(value, high), low);
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

		const auto clampWidth = [&](float value) {
			return clampAxis(value, node.minSpec.width, node.maxSpec.width, parentBox.x);
		};
		const auto clampHeight = [&](float value) {
			return clampAxis(value, node.minSpec.height, node.maxSpec.height, parentBox.y);
		};
		std::optional<float> resolvedWidth = width ? width : node.sizeSpec.width.resolve(parentBox.x);
		std::optional<float> resolvedHeight = height ? height : node.sizeSpec.height.resolve(parentBox.y);
		if (resolvedWidth) {
			resolvedWidth = clampWidth(*resolvedWidth);
		}
		if (resolvedHeight) {
			resolvedHeight = clampHeight(*resolvedHeight);
		}
		if (!resolvedWidth || !resolvedHeight) {
			const float		 room = resolvedWidth ? *resolvedWidth
												  : std::min(
													std::max(availableWidth - node.margin.horizontal(), 0.0F),
													node.maxSpec.width.resolve(parentBox.x).value_or(kUnbounded)
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
		if (node.layout.display != Display::Flex) {
			return blockContent(node, width, height);
		}
		const Edges&		 padding = node.layout.padding;
		std::optional<float> contentWidth = width ? std::optional(std::max(*width - padding.horizontal(), 0.0F)) : std::nullopt;
		std::optional<float> contentHeight = height ? std::optional(std::max(*height - padding.vertical(), 0.0F)) : std::nullopt;
		flex(node, contentWidth, contentHeight, std::max(availableWidth - padding.horizontal(), 0.0F));
		float main = 0.0F;
		float cross = 0.0F;
		for (const FlexItem& item : items) {
			main += item.main + item.marginMain;
			cross = std::max(cross, item.cross + item.marginCross);
		}
		if (items.size() > 1) {
			main += node.layout.gap * static_cast<float>(items.size() - 1);
		}
		main = std::max(main, 0.0F);
		const bool row = node.layout.flexDirection == FlexDirection::Row;
		return row ? Foundation::Vec2{main + padding.horizontal(), cross + padding.vertical()}
				   : Foundation::Vec2{cross + padding.horizontal(), main + padding.vertical()};
	}

	// A Block hugs its children where they sit; absolutely positioned ones are out of its flow.
	Foundation::Vec2 LayoutEngine::blockContent(Component& node, std::optional<float> width, std::optional<float> height) {
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

	// Leaves `items` holding each flowing child's sizes. A nested measure runs flex() too, so this
	// works on a local list and swaps it in only at the end.
	void LayoutEngine::flex(Component& node, std::optional<float> contentWidth, std::optional<float> contentHeight, float availableWidth) {
		const bool			   row = node.layout.flexDirection == FlexDirection::Row;
		std::optional<float>   contentMain = row ? contentWidth : contentHeight;
		std::optional<float>   contentCross = row ? contentHeight : contentWidth;
		const Foundation::Vec2 box{contentWidth.value_or(-1.0F), contentHeight.value_or(-1.0F)};
		const float			   mainBase = row ? box.x : box.y;

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
			if (align == AlignItems::Stretch && crossSpec.isAuto() && contentCross) {
				stretched = std::max(*contentCross - marginCross, 0.0F);
			}
			// A row measures its children unbounded (their max-content basis); a column bounds them by its width.
			const float			   room = row ? kUnbounded : (contentCross ? *contentCross : availableWidth);
			const Foundation::Vec2 measured =
				row ? measure(*child, std::nullopt, stretched, box, room) : measure(*child, stretched, std::nullopt, box, room);
			const float	  basis = row ? measured.x : measured.y;
			const Length& minSpec = row ? child->minSpec.width : child->minSpec.height;
			const Length& maxSpec = row ? child->maxSpec.width : child->maxSpec.height;
			const float	  automaticMin = row ? std::min(child->minContentWidth(), basis) : 0.0F;
			flowing.push_back({
				.component = child,
				.basis = basis,
				.main = basis,
				.cross = row ? measured.y : measured.x,
				.marginMain = marginMain,
				.marginCross = marginCross,
				.minMain = minSpec.isAuto() ? automaticMin : minSpec.resolve(mainBase).value_or(0.0F),
				.maxMain = maxSpec.resolve(mainBase).value_or(kUnbounded),
				.stretchedCross = stretched,
				.frozen = false,
			});
		}

		if (contentMain) {
			const float gaps = flowing.size() > 1 ? node.layout.gap * static_cast<float>(flowing.size() - 1) : 0.0F;
			// Once, then again with whatever a min or max clamped held fixed (CSS's frozen items).
			for (int round = 0; round < 2; ++round) {
				float used = gaps;
				float grow = 0.0F;
				float shrink = 0.0F;
				for (const FlexItem& item : flowing) {
					used += (item.frozen ? item.main : item.basis) + item.marginMain;
					if (!item.frozen) {
						grow += std::max(item.component->layout.flexGrow, 0.0F);
						shrink += std::max(item.component->layout.flexShrink, 0.0F) * item.basis;
					}
				}
				const float free = *contentMain - used;
				bool		clamped = false;
				for (FlexItem& item : flowing) {
					if (item.frozen) {
						continue;
					}
					float main = item.basis;
					if (free > 0.0F && grow > 0.0F) {
						main += free * std::max(item.component->layout.flexGrow, 0.0F) / grow;
					} else if (free < 0.0F && shrink > 0.0F) {
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

		// A child whose main size moved is measured again across it: a text that wraps grows taller.
		for (FlexItem& item : flowing) {
			if (item.main == item.basis || item.stretchedCross) {
				continue;
			}
			const float			   room = row ? kUnbounded : (contentCross ? *contentCross : availableWidth);
			const Foundation::Vec2 measured = row ? measure(*item.component, item.main, std::nullopt, box, room)
												  : measure(*item.component, std::nullopt, item.main, box, room);
			item.cross = row ? measured.y : measured.x;
		}
		items = std::move(flowing);
	}

	// --- Arranging ---

	void LayoutEngine::arrange(Component& node) {
		node.layoutDirty = false;
		if (!node.isShown()) {
			return;
		}
		if (node.layout.display == Display::Flex) {
			arrangeFlex(node);
		} else {
			arrangeBlock(node);
		}
	}

	void LayoutEngine::arrangeBlock(Component& node) {
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

		const bool	row = node.layout.flexDirection == FlexDirection::Row;
		const float contentMain = row ? contentWidth : contentHeight;
		const float contentCross = row ? contentHeight : contentWidth;
		const float gap = node.layout.gap;
		float		total = 0.0F;
		for (const FlexItem& item : flowing) {
			total += item.main + item.marginMain;
		}
		const auto	count = static_cast<float>(flowing.size());
		const float gaps = flowing.size() > 1 ? gap * (count - 1.0F) : 0.0F;
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
				between = flowing.size() > 1 ? leftover / (count - 1.0F) : 0.0F;
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
		for (const FlexItem& item : flowing) {
			Component&		 child = *item.component;
			const AlignItems align = child.layout.alignSelf.value_or(node.layout.alignItems);
			const float		 crossBox = item.cross + item.marginCross;
			float			 crossAt = 0.0F;
			if (align == AlignItems::Center) {
				crossAt = std::max((contentCross - crossBox) * 0.5F, 0.0F);
			} else if (align == AlignItems::End) {
				crossAt = std::max(contentCross - crossBox, 0.0F);
			}
			Foundation::Vec2 at = padding.topLeft() + (row ? Foundation::Vec2{cursor, crossAt} : Foundation::Vec2{crossAt, cursor});
			const Inset&	 inset = child.layout.inset;
			at.x += inset.left ? *inset.left : (inset.right ? -*inset.right : 0.0F);
			at.y += inset.top ? *inset.top : (inset.bottom ? -*inset.bottom : 0.0F);
			place(child, at, row ? Foundation::Vec2{item.main, item.cross} : Foundation::Vec2{item.cross, item.main});
			cursor += item.main + item.marginMain + gap + between;
		}

		for (const ComponentId childId : node.children) {
			Component* child = registry.find(childId);
			if (child != nullptr && child->isShown() && child->layout.position == Position::Absolute) {
				placeAbsolute(*child, node.size);
			}
		}
	}

	// Against the parent's whole box: an inset edge anchors that side, both a pair stretch an auto
	// size between them, and none falls back to `position`.
	void LayoutEngine::placeAbsolute(Component& child, Foundation::Vec2 box) {
		const Inset&		 inset = child.layout.inset;
		std::optional<float> width;
		std::optional<float> height;
		if (child.sizeSpec.width.isAuto() && inset.left && inset.right) {
			width = std::max(box.x - *inset.left - *inset.right - child.margin.horizontal(), 0.0F);
		}
		if (child.sizeSpec.height.isAuto() && inset.top && inset.bottom) {
			height = std::max(box.y - *inset.top - *inset.bottom - child.margin.vertical(), 0.0F);
		}
		const float			   left = inset.left.value_or(inset.right ? 0.0F : child.position.x);
		const Foundation::Vec2 childSize = measure(child, width, height, box, std::max(box.x - left, 0.0F));
		const Foundation::Vec2 margins = child.margin.total();
		const float x = inset.left ? *inset.left : (inset.right ? box.x - *inset.right - childSize.x - margins.x : child.position.x);
		const float y = inset.top ? *inset.top : (inset.bottom ? box.y - *inset.bottom - childSize.y - margins.y : child.position.y);
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
