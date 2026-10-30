#include "ui/debug/LayoutLint.h"

#include "ui/Context.h"
#include "ui/detail/Snapshot.h"

#include <algorithm>

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

		bool overlapsBeyondEpsilon(const Foundation::Rect& a, const Foundation::Rect& b) {
			const float overlapWidth = std::min(a.right(), b.right()) - std::max(a.x, b.x);
			const float overlapHeight = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
			return overlapWidth > kEpsilon && overlapHeight > kEpsilon;
		}

		struct Sibling {
			const Component* component;
			std::string		 path;
			Foundation::Rect bounds; // in the viewport, untransformed
			int				 z;
		};

		class Linter {
		  public:
			Linter(const Context& linted, Foundation::Vec2 viewport)
				: context(linted),
				  viewportRect{0.0F, 0.0F, viewport.x, viewport.y} {}

			// One sibling group at `origin` in the viewport (the parent's top-left, untransformed), then
			// each visible member's children. `parent` is null for the roots, which only the viewport
			// contains. Within a stacking context an unset zIndex takes the parent's; under a new
			// context it starts from 0.
			void siblings(
				const std::vector<ComponentId>& ids,
				const Sibling*					parent,
				Foundation::Vec2				origin,
				int								inheritedZ,
				LintResult&						result
			) {
				std::vector<Sibling> visible;
				visible.reserve(ids.size());
				for (std::size_t i = 0; i < ids.size(); ++i) {
					const Component* component = context.find(ids[i]);
					if (component == nullptr || !component->isVisible()) {
						continue;
					}
					const Foundation::Rect local = component->getBounds();
					const Foundation::Rect bounds{origin.x + local.x, origin.y + local.y, local.width, local.height};
					const std::string	   path = joinPath(parent != nullptr ? parent->path : std::string{}, pathSegment(*component, i));
					if (bounds.width <= 0.0F || bounds.height <= 0.0F) {
						result.violations.push_back({LintRule::ZeroOrNegativeSize, path, bounds, "", {}});
					}
					if (parent != nullptr && !containsWithEpsilon(parent->bounds, bounds)) {
						result.violations.push_back({LintRule::ChildOutsideParent, path, bounds, parent->path, parent->bounds});
					}
					if (!containsWithEpsilon(viewportRect, bounds)) {
						result.violations.push_back({LintRule::OutsideViewport, path, bounds, "viewport", viewportRect});
					}
					visible.push_back({component, path, bounds, component->getZIndex().value_or(inheritedZ)});
				}

				// Roots are each a stacking context the size of the viewport, layered on purpose.
				for (std::size_t i = 0; parent != nullptr && i < visible.size(); ++i) {
					for (std::size_t j = i + 1; j < visible.size(); ++j) {
						if (visible[i].z == visible[j].z && overlapsBeyondEpsilon(visible[i].bounds, visible[j].bounds)) {
							result.violations.push_back(
								{LintRule::SiblingOverlap, visible[i].path, visible[i].bounds, visible[j].path, visible[j].bounds}
							);
						}
					}
				}

				for (const Sibling& member : visible) {
					const int childZ = member.component->getIsolation() ? 0 : member.z;
					siblings(member.component->getChildren(), &member, {member.bounds.x, member.bounds.y}, childZ, result);
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
		}
		return "unknown";
	}

	LintResult lint(const Context& context, Foundation::Vec2 viewport) {
		LintResult result;
		Linter	   linter(context, viewport);
		linter.siblings(context.getRoots(), nullptr, {0.0F, 0.0F}, 0, result);
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
