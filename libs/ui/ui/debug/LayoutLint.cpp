#include "ui/debug/LayoutLint.h"

#include "ui/Context.h"
#include "ui/detail/Snapshot.h"
#include "ui/shapes/Text.h"

#include <algorithm>
#include <optional>

namespace ui {

	namespace {

		constexpr float kEpsilon = 0.5F;

		std::string pathSegment(const Component& component, std::size_t index) {
			if (!component.getId().empty()) {
				return component.getId();
			}
			return std::string(component.typeName()) + "[" + std::to_string(index) + "]";
		}

		std::string joinPath(const std::string& parentPath, const std::string& segment) {
			return parentPath.empty() ? segment : parentPath + "/" + segment;
		}

		bool containsWithEpsilon(const Foundation::Rect& outer, const Foundation::Rect& inner) {
			return inner.x >= outer.x - kEpsilon && inner.y >= outer.y - kEpsilon && inner.right() <= outer.right() + kEpsilon &&
				   inner.bottom() <= outer.bottom() + kEpsilon;
		}

		// Touching edges aren't overlap, nor is up to `allowed` on an axis.
		bool overlapsBeyondEpsilon(const Foundation::Rect& a, const Foundation::Rect& b, Foundation::Vec2 allowed) {
			const float overlapWidth = std::min(a.right(), b.right()) - std::max(a.x, b.x);
			const float overlapHeight = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
			return overlapWidth > allowed.x + kEpsilon && overlapHeight > allowed.y + kEpsilon;
		}

		// What two rects share; none when it has no area.
		std::optional<Foundation::Rect> intersection(const Foundation::Rect& a, const Foundation::Rect& b) {
			const float left = std::max(a.x, b.x);
			const float top = std::max(a.y, b.y);
			const float right = std::min(a.right(), b.right());
			const float bottom = std::min(a.bottom(), b.bottom());
			if (right <= left || bottom <= top) {
				return std::nullopt;
			}
			return Foundation::Rect{left, top, right - left, bottom - top};
		}

		bool clips(const Component& component) {
			return component.getOverflow() == Overflow::Clip || component.getOverflow() == Overflow::Scroll;
		}

		// Something a player is meant to reach: it takes focus, or it answers the pointer and its box can
		// be hit.
		bool interactive(const Component& component) {
			const PointerEvents pointer = component.getPointerEvents();
			return component.isFocusable() ||
				   (component.handlesPointer() && (pointer == PointerEvents::Auto || pointer == PointerEvents::Unit));
		}

		struct Sibling {
			const Component* component;
			std::string		 path;
			Foundation::Rect bounds; // in the viewport, untransformed
			int				 z;
			bool			 decoration = false;
		};

		class Linter {
		  public:
			Linter(const Context& linted, Foundation::Vec2 viewport)
				: context(linted),
				  viewportRect{0.0F, 0.0F, viewport.x, viewport.y} {}

			// One sibling group at `origin` in the viewport (the parent's top-left, untransformed), then
			// each visible member's children. `parent` is null for the roots, which only the viewport
			// contains. Within a stacking context an unset zIndex takes the parent's; under a new
			// context it starts from 0. A parent that clips declares its children may run past it, so
			// they aren't outside it, and only what its clip shows is held to the viewport.
			void siblings(
				const std::vector<ComponentId>&		   ids,
				const Sibling*						   parent,
				Foundation::Vec2					   origin,
				int									   inheritedZ,
				const std::optional<Foundation::Rect>& clip,
				bool								   inScroller,
				LintResult&							   result,
				const std::string&					   prefix = {}, // the path an entry's group hangs from
				bool								   entryGroup = false
			) {
				const bool				 parentClips = parent != nullptr && clips(*parent->component);
				std::vector<std::size_t> entries; // checked as roots of their own, after the rest
				std::vector<Sibling>	 visible;
				visible.reserve(ids.size());
				for (std::size_t i = 0; i < ids.size(); ++i) {
					const Component* component = context.find(ids[i]);
					if (!entryGroup && component != nullptr && component->isShown() && component->isOverlay()) {
						entries.push_back(i);
						continue;
					}
					if (component == nullptr || !component->isShown()) {
						continue;
					}
					const Foundation::Rect local = component->getBounds();
					const Foundation::Rect bounds{origin.x + local.x, origin.y + local.y, local.width, local.height};
					const std::string	   path = joinPath(parent != nullptr ? parent->path : prefix, pathSegment(*component, i));
					const bool			   decoration = component->getPointerEvents() == PointerEvents::None;
					if (!decoration && (bounds.width <= 0.0F || bounds.height <= 0.0F)) {
						result.violations.push_back({LintRule::ZeroOrNegativeSize, path, bounds, "", {}});
					}
					if (parent != nullptr && !parentClips && !decoration && !containsWithEpsilon(parent->bounds, bounds)) {
						result.violations.push_back({LintRule::ChildOutsideParent, path, bounds, parent->path, parent->bounds});
					}
					const std::optional<Foundation::Rect> shown = clip ? intersection(bounds, *clip) : std::optional(bounds);
					if (shown && !containsWithEpsilon(viewportRect, *shown)) {
						result.violations.push_back({LintRule::OutsideViewport, path, bounds, "viewport", viewportRect});
					}
					if (const auto* text = dynamic_cast<const Text*>(component)) {
						const Foundation::Vec2 extent = text->measureExtent().size;
						const Foundation::Vec2 box = component->getSize();
						if (extent.x > box.x + kEpsilon || extent.y > box.y + kEpsilon) {
							const Foundation::Vec2 at{bounds.x + component->getMargin().left, bounds.y + component->getMargin().top};
							result.violations.push_back(
								{LintRule::TextOverflow, path, {at.x, at.y, extent.x, extent.y}, "", {at.x, at.y, box.x, box.y}}
							);
						}
					}
					// Clipped away where no scroll brings it back.
					if (interactive(*component) && clip && !inScroller && !intersection(bounds, *clip)) {
						result.violations.push_back({LintRule::UnreachableInteractive, path, bounds, "clip", *clip});
					}
					visible.push_back({component, path, bounds, component->getZIndex().value_or(inheritedZ), decoration});
				}

				// Covered whole by a sibling painted over it that takes the pointer.
				for (const Sibling& under : visible) {
					if (!interactive(*under.component)) {
						continue;
					}
					for (const Sibling& over : visible) {
						const PointerEvents pointer = over.component->getPointerEvents();
						if (over.component->drawKey() > under.component->drawKey() &&
							(pointer == PointerEvents::Auto || pointer == PointerEvents::Unit) &&
							containsWithEpsilon(over.bounds, under.bounds)) {
							result.violations.push_back(
								{LintRule::UnreachableInteractive, under.path, under.bounds, over.path, over.bounds}
							);
							break;
						}
					}
				}

				// Roots are each a stacking context the size of the viewport, layered on purpose. A flex
				// parent's negative gap overlaps its children by that much on purpose too.
				Foundation::Vec2 allowed{0.0F, 0.0F};
				if (parent != nullptr && parent->component->getLayout().display == Display::Flex &&
					parent->component->getLayout().gap < 0.0F) {
					const float gap = -parent->component->getLayout().gap;
					allowed = parent->component->getLayout().flexDirection == FlexDirection::Row ? Foundation::Vec2{gap, 0.0F}
																								 : Foundation::Vec2{0.0F, gap};
				}
				for (std::size_t i = 0; parent != nullptr && i < visible.size(); ++i) {
					for (std::size_t j = i + 1; j < visible.size(); ++j) {
						if (visible[i].decoration || visible[j].decoration) {
							continue;
						}
						if (visible[i].z == visible[j].z && overlapsBeyondEpsilon(visible[i].bounds, visible[j].bounds, allowed)) {
							result.violations.push_back(
								{LintRule::SiblingOverlap, visible[i].path, visible[i].bounds, visible[j].path, visible[j].bounds}
							);
						}
					}
				}

				// A child's space starts at its parent's box, inside the parent's margin.
				for (const Sibling& member : visible) {
					const int			   childZ = member.component->getIsolation() ? 0 : member.z;
					const Edges&		   margin = member.component->getMargin();
					const Foundation::Vec2 boxSize = member.component->getSize();
					const Sibling		   box{
						 member.component,
						 member.path,
								 {member.bounds.x + margin.left, member.bounds.y + margin.top, boxSize.x, boxSize.y},
						 member.z
					 };
					std::optional<Foundation::Rect> childClip = clip;
					if (clips(*member.component)) {
						childClip = clip ? intersection(box.bounds, *clip) : std::optional(box.bounds);
						if (!childClip) {
							childClip = Foundation::Rect{box.bounds.x, box.bounds.y, 0.0F, 0.0F};
						}
					}
					const bool scrolls = inScroller || member.component->getOverflow() == Overflow::Scroll;
					siblings(member.component->getChildren(), &box, {box.bounds.x, box.bounds.y}, childZ, childClip, scrolls, result);
				}

				// An overlay entry is out of its opener's box and clips on purpose: a root of its own, where
				// it is on screen, held to the viewport alone.
				for (const std::size_t index : entries) {
					const Component*	   entry = context.find(ids[index]);
					const Foundation::Rect screen = entry->screenBounds();
					const Foundation::Rect local = entry->getBounds();
					const Foundation::Vec2 at{screen.x - entry->getMargin().left - local.x, screen.y - entry->getMargin().top - local.y};
					const std::vector<ComponentId> one{ids[index]};
					siblings(one, nullptr, at, 0, std::nullopt, false, result, parent != nullptr ? parent->path : prefix, true);
				}
			}

		  private:
			const Context&	 context;
			Foundation::Rect viewportRect;
		};

	} // namespace

	const char* lintRuleName(LintRule rule) {
		switch (rule) {
			case LintRule::SiblingOverlap:
				return "sibling-overlap";
			case LintRule::ChildOutsideParent:
				return "child-outside-parent";
			case LintRule::OutsideViewport:
				return "outside-viewport";
			case LintRule::ZeroOrNegativeSize:
				return "zero-or-negative-size";
			case LintRule::TextOverflow:
				return "text-overflow";
			case LintRule::UnreachableInteractive:
				return "unreachable-interactive";
		}
		return "unknown";
	}

	LintResult lint(const Context& context, Foundation::Vec2 viewport) {
		LintResult result;
		Linter	   linter(context, viewport);
		linter.siblings(context.getRoots(), nullptr, {0.0F, 0.0F}, 0, std::nullopt, false, result);
		return result;
	}

	std::string lintJson(const Context* context, Foundation::Vec2 viewport) {
		const LintResult result = context != nullptr ? lint(*context, viewport) : LintResult{};
		nlohmann::json	 violations = nlohmann::json::array();
		for (const LintViolation& violation : result.violations) {
			nlohmann::json entry;
			entry["rule"] = lintRuleName(violation.rule);
			entry["path"] = violation.path;
			entry["bounds"] = detail::rectJson(violation.bounds);
			if (!violation.otherPath.empty()) {
				entry["otherPath"] = violation.otherPath;
				entry["otherBounds"] = detail::rectJson(violation.otherBounds);
			}
			violations.push_back(std::move(entry));
		}
		nlohmann::json out;
		out["count"] = result.violations.size();
		out["violations"] = std::move(violations);
		return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	}

} // namespace ui
