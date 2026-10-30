#include "ui/Component.h"

#include "ui/Context.h"

#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <utility>

namespace ui {

	namespace detail {
		void logGoneParent(const char* type) {
			LOG_ERROR(UI, "addChild<%s> through a Ref whose component has been removed; nothing added", type);
		}
	} // namespace detail

	void Component::setCommon(const Args& args) {
		id = args.id;
		visible = args.visible;
		enabled = args.enabled;
		position = args.position;
		size = args.size;
		transform = args.transform;
		declaredStyle = args.style;
		style = args.style;
		zIndex = args.zIndex;
		isolation = args.isolation;
	}

	void Component::setStyle(const Style& value) {
		declaredStyle = value;
		style = context != nullptr ? context->getTheme().resolve(value, themeStyle()) : value;
	}

	ComponentId Component::adopt(std::unique_ptr<Component> child) {
		if (context == nullptr) {
			LOG_ERROR(UI, "%s: addChild before it is mounted; add children in onMount, not the constructor", typeName());
			return {};
		}
		if (context->drawing) {
			LOG_ERROR(UI, "%s: addChild while the ui draws; nothing added", typeName());
			return {};
		}
		Component&		  added = *child;
		const ComponentId childId = context->registry.insert(std::move(child));
		added.selfId = childId;
		added.parentId = selfId;
		added.context = context;
		children.push_back(childId);
		context->geometryDirty = true;
		context->orderDirty = true;
		added.setStyle(added.declaredStyle);
		{
			const Registry::DispatchScope scope(context->registry);
			added.onMount();
		}
		return childId;
	}

	void Component::removeChild(Ref<Component> child) {
		Component* removed = child.get();
		if (removed == nullptr || removed->parentId != selfId || context == nullptr) {
			LOG_WARNING(UI, "%s: removeChild of a component that isn't its child; ignored", typeName());
			return;
		}
		if (context->drawing) {
			LOG_ERROR(UI, "%s: removeChild while the ui draws; ignored", typeName());
			return;
		}
		std::erase(children, removed->selfId);
		context->orderDirty = true;
		context->registry.release(removed->selfId);
	}

	Ref<Component> Component::parent() const {
		return context != nullptr ? Ref<Component>(parentId, &context->registry) : Ref<Component>();
	}

	Ref<Component> Component::self() const {
		return context != nullptr ? Ref<Component>(selfId, &context->registry) : Ref<Component>();
	}

	void Component::setVisible(bool value) {
		if (visible != value) {
			visible = value;
			geometryChanged();
			orderChanged();
		}
	}

	void Component::setPosition(Foundation::Vec2 value) {
		if (position != value) {
			position = value;
			geometryChanged();
		}
	}

	void Component::setSize(Foundation::Vec2 value) {
		if (size != value) {
			size = value;
			geometryChanged();
		}
	}

	void Component::setTransform(const Transform& value) {
		transform = value;
		geometryChanged();
	}

	void Component::setZIndex(std::optional<int> value) {
		if (zIndex != value) {
			zIndex = value;
			orderChanged();
		}
	}

	void Component::setIsolation(bool value) {
		if (isolation != value) {
			isolation = value;
			orderChanged();
		}
	}

	Foundation::Rect Component::screenBounds() const {
		if (screen.isTranslation()) {
			return {screen.tx, screen.ty, size.x, size.y};
		}
		const std::array<Foundation::Vec2, 4> corners{
			screen.apply({0.0F, 0.0F}),
			screen.apply({size.x, 0.0F}),
			screen.apply({0.0F, size.y}),
			screen.apply({size.x, size.y}),
		};
		Foundation::Vec2 low = corners[0];
		Foundation::Vec2 high = corners[0];
		for (const Foundation::Vec2& corner : corners) {
			low = glm::min(low, corner);
			high = glm::max(high, corner);
		}
		return {low.x, low.y, high.x - low.x, high.y - low.y};
	}

	Foundation::Vec2 Component::localToScreen(Foundation::Vec2 point) const {
		return screen.apply(point);
	}

	Foundation::Vec2 Component::screenToLocal(Foundation::Vec2 point) const {
		return screen.inverse().apply(point);
	}

	void Component::geometryChanged() const {
		if (context != nullptr) {
			context->geometryDirty = true;
		}
	}

	void Component::orderChanged() const {
		if (context != nullptr) {
			context->orderDirty = true;
		}
	}

} // namespace ui
