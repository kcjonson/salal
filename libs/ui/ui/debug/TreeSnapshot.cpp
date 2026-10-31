#include "ui/debug/TreeSnapshot.h"

#include "ui/Context.h"
#include "ui/detail/Snapshot.h"

#include <cmath>

namespace ui {

	namespace {

		nlohmann::json transformJson(const Transform& transform) {
			if (transform.isIdentity()) {
				return nullptr;
			}
			return {
				{"rotate", transform.rotate},
				{"scale", {transform.scale.x, transform.scale.y}},
				{"translate", {transform.translate.x, transform.translate.y}},
				{"origin", {transform.origin.x, transform.origin.y}},
			};
		}

		// What the component draws over in the viewport: its screen box grown by its ink extent.
		Foundation::Rect inkBounds(const Component& component) {
			const Foundation::Rect box = component.screenBounds();
			const float			   ink = component.inkExtent();
			return {box.x - ink, box.y - ink, box.width + ink * 2.0F, box.height + ink * 2.0F};
		}

		// The common fields, then the type's own; opacity is the effective value the walk computes, as
		// of the last frame drawn, so it reads from the parent's.
		nlohmann::json node(const Context& context, const Component& component, float parentOpacity) {
			const float	   opacity = component.isVisible() ? parentOpacity * component.getStyle().opacity.value_or(1.0F) : 0.0F;
			nlohmann::json out;
			out["id"] = component.getId().empty() ? nlohmann::json(nullptr) : nlohmann::json(component.getId());
			out["type"] = component.typeName();
			out["bounds"] = detail::rectJson(component.getBounds());
			out["screenBounds"] = detail::rectJson(component.screenBounds());
			out["margin"] = {{"top", 0.0F}, {"right", 0.0F}, {"bottom", 0.0F}, {"left", 0.0F}};
			const std::optional<int> zIndex = component.getZIndex();
			out["zIndex"] = zIndex ? nlohmann::json(*zIndex) : nlohmann::json(nullptr);
			out["isolation"] = component.getIsolation();
			out["key"] = component.isVisible() ? nlohmann::json(component.drawKey()) : nlohmann::json(nullptr);
			out["visible"] = component.isVisible();
			out["enabled"] = component.isEnabled();
			out["opacity"] = opacity;
			out["clip"] = nullptr;
			out["contentOffset"] = nullptr;
			out["transform"] = transformJson(component.getTransform());
			out["focusable"] = false;
			out["pointerEvents"] = nullptr;
			out["handlesPointer"] = false;
			out["state"] = nullptr;
			out["text"] = nullptr;
			out["value"] = nullptr;
			const Style& style = component.getStyle();
			out["style"] = detail::styleJson(style.backgroundColor, style.color, style.borderColor);
			out["inkBounds"] = detail::rectJson(inkBounds(component));
			component.describe(out);

			nlohmann::json children = nlohmann::json::array();
			for (const ComponentId childId : component.getChildren()) {
				if (const Component* child = context.find(childId)) {
					children.push_back(node(context, *child, opacity));
				}
			}
			out["children"] = std::move(children);
			return out;
		}

	} // namespace

	std::string treeJson(const Context* context, std::string_view scene, Foundation::Vec2 viewport) {
		nlohmann::json out;
		out["scene"] = scene.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::string(scene));
		out["viewport"] = {{"width", std::lround(viewport.x)}, {"height", std::lround(viewport.y)}};
		nlohmann::json roots = nlohmann::json::array();
		if (context != nullptr) {
			for (const ComponentId rootId : context->getRoots()) {
				if (const Component* root = context->find(rootId)) {
					roots.push_back(node(*context, *root, 1.0F));
				}
			}
		}
		out["roots"] = std::move(roots);
		return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	}

} // namespace ui
