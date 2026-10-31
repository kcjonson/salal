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

	// Until the first layout, `size` holds the px a declared size gives, 0 for auto or a percentage.
	void Component::setCommon(const Args& args) {
		id = args.id;
		visible = args.visible;
		enabled = args.enabled;
		position = args.position;
		sizeSpec = args.size;
		minSpec = args.minSize;
		maxSpec = args.maxSize;
		margin = args.margin;
		size = {
			sizeSpec.width.unit == Length::Unit::Px ? sizeSpec.width.value : 0.0F,
			sizeSpec.height.unit == Length::Unit::Px ? sizeSpec.height.value : 0.0F,
		};
		transform = args.transform;
		layout = args.layout;
		declaredStyle = args.style;
		style = args.style;
		overflow = args.overflow;
		zIndex = args.zIndex;
		isolation = args.isolation;
		pointerEvents = args.pointerEvents;
		focusable = args.focusable;
		tabIndex = args.tabIndex;
		focusGroup = args.focusGroup;
		cursor = args.cursor;
		const auto take = [this](const std::function<void(PointerEvent&)>& from, std::function<void(PointerEvent&)> Handlers::* to) {
			if (from) {
				handlersMade().*to = from;
			}
		};
		take(args.onClick, &Handlers::onClick);
		take(args.onPointerDown, &Handlers::onPointerDown);
		take(args.onPointerUp, &Handlers::onPointerUp);
		take(args.onPointerMove, &Handlers::onPointerMove);
		take(args.onPointerEnter, &Handlers::onPointerEnter);
		take(args.onPointerLeave, &Handlers::onPointerLeave);
		take(args.onWheel, &Handlers::onWheel);
		if (args.onKeyDown) {
			setOnKeyDown(args.onKeyDown);
		}
		if (args.onKeyUp) {
			setOnKeyUp(args.onKeyUp);
		}
		if (args.onFocus) {
			setOnFocus(args.onFocus);
		}
		if (args.onBlur) {
			setOnBlur(args.onBlur);
		}
		if (args.onLayout) {
			setOnLayout(args.onLayout);
		}
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
		layoutChanged(false);
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
		layoutChanged(false);
		context->orderDirty = true;
		context->removing(removed->selfId);
		context->registry.release(removed->selfId);
	}

	Ref<Component> Component::parent() const {
		return context != nullptr ? Ref<Component>(parentId, &context->registry) : Ref<Component>();
	}

	Ref<Component> Component::self() const {
		return context != nullptr ? Ref<Component>(selfId, &context->registry) : Ref<Component>();
	}

	Component* Component::parentComponent() const {
		return context != nullptr ? context->registry.find(parentId) : nullptr;
	}

	void Component::setEnabled(bool value) {
		if (enabled != value) {
			enabled = value;
			if (context != nullptr) {
				context->geometryDirty = true;
			}
		}
	}

	void Component::setVisible(bool value) {
		if (visible != value) {
			visible = value;
			layoutChanged(true);
			orderChanged();
		}
	}

	void Component::setPosition(Foundation::Vec2 value) {
		if (position != value) {
			position = value;
			layoutChanged(true);
		}
	}

	void Component::setSize(const Size& value) {
		sizeSpec = value;
		layoutChanged(true);
	}

	void Component::setMinSize(const Size& value) {
		minSpec = value;
		layoutChanged(true);
	}

	void Component::setMaxSize(const Size& value) {
		maxSpec = value;
		layoutChanged(true);
	}

	void Component::setMargin(const Edges& value) {
		if (margin != value) {
			margin = value;
			layoutChanged(true);
		}
	}

	void Component::setLayout(const Layout& value) {
		if (layout != value) {
			layout = value;
			layoutChanged(true);
		}
	}

	void Component::setOverflow(Overflow value) {
		overflow = value;
		if (context != nullptr) {
			context->geometryDirty = true;
		}
	}

	void Component::setTransform(const Transform& value) {
		transform = value;
		if (context != nullptr) {
			context->geometryDirty = true;
		}
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

	Component::Handlers& Component::handlersMade() {
		if (!handlers) {
			handlers = std::make_unique<Handlers>();
		}
		return *handlers;
	}

	void Component::setOnLayout(std::function<void(Component&)> callback) {
		handlersMade().onLayout = std::move(callback);
	}

	void Component::setOnClick(std::function<void(PointerEvent&)> callback) {
		handlersMade().onClick = std::move(callback);
	}

	void Component::setOnPointer(PointerEvent::Type type, std::function<void(PointerEvent&)> callback) {
		Handlers& made = handlersMade();
		switch (type) {
			case PointerEvent::Type::Down:
				made.onPointerDown = std::move(callback);
				break;
			case PointerEvent::Type::Up:
				made.onPointerUp = std::move(callback);
				break;
			case PointerEvent::Type::Move:
				made.onPointerMove = std::move(callback);
				break;
			case PointerEvent::Type::Enter:
				made.onPointerEnter = std::move(callback);
				break;
			case PointerEvent::Type::Leave:
				made.onPointerLeave = std::move(callback);
				break;
			case PointerEvent::Type::Wheel:
				made.onWheel = std::move(callback);
				break;
			case PointerEvent::Type::Click:
				made.onClick = std::move(callback);
				break;
			case PointerEvent::Type::Cancel:
				break;
		}
	}

	const std::function<void(PointerEvent&)>* Component::callbackFor(PointerEvent::Type type) const {
		if (!handlers) {
			return nullptr;
		}
		const std::function<void(PointerEvent&)>* callback = nullptr;
		switch (type) {
			case PointerEvent::Type::Down:
				callback = &handlers->onPointerDown;
				break;
			case PointerEvent::Type::Up:
				callback = &handlers->onPointerUp;
				break;
			case PointerEvent::Type::Move:
				callback = &handlers->onPointerMove;
				break;
			case PointerEvent::Type::Enter:
				callback = &handlers->onPointerEnter;
				break;
			case PointerEvent::Type::Leave:
				callback = &handlers->onPointerLeave;
				break;
			case PointerEvent::Type::Wheel:
				callback = &handlers->onWheel;
				break;
			case PointerEvent::Type::Click:
				callback = &handlers->onClick;
				break;
			case PointerEvent::Type::Cancel:
				break;
		}
		return callback != nullptr && *callback ? callback : nullptr;
	}

	bool Component::handlesPointer() const {
		return handlers && (handlers->onClick || handlers->onPointerDown || handlers->onPointerUp || handlers->onPointerMove ||
							handlers->onPointerEnter || handlers->onPointerLeave || handlers->onWheel);
	}

	void Component::setOnKeyDown(std::function<void(KeyEvent&)> callback) {
		handlersMade().onKeyDown = std::move(callback);
	}

	void Component::setOnKeyUp(std::function<void(KeyEvent&)> callback) {
		handlersMade().onKeyUp = std::move(callback);
	}

	void Component::setOnFocus(std::function<void(Component&)> callback) {
		handlersMade().onFocus = std::move(callback);
	}

	void Component::setOnBlur(std::function<void(Component&)> callback) {
		handlersMade().onBlur = std::move(callback);
	}

	void Component::setNeighbour(Direction direction, Ref<Component> neighbour) {
		neighbours[static_cast<int>(direction)] = neighbour.componentId();
	}

	bool Component::focus(int player) {
		return context != nullptr && context->focus(self(), player);
	}

	bool Component::canReceiveFocus() const {
		if (!focusable || context == nullptr || context->registry.find(selfId) != this) {
			return false;
		}
		for (const Component* node = this; node != nullptr; node = node->parentComponent()) {
			if (!node->isShown() || !node->enabled) {
				return false;
			}
		}
		return true;
	}

	Component* Component::keyedChild(const std::string& childKey) const {
		for (const ComponentId childId : children) {
			Component* child = context->registry.find(childId);
			if (child != nullptr && !child->reconcileKey.empty() && child->reconcileKey == childKey) {
				return child;
			}
		}
		return nullptr;
	}

	// Removes the children not kept, then puts the rest in the items' order.
	void Component::finishReconcile(const std::vector<ComponentId>& kept) {
		const std::vector<ComponentId> before = children;
		for (const ComponentId childId : before) {
			if (std::ranges::find(kept, childId) == kept.end()) {
				removeChild(Ref<Component>(childId, &context->registry));
			}
		}
		if (children != kept) {
			children = kept;
			layoutChanged(false);
			orderChanged();
		}
	}

	void Component::setPointerEvents(std::optional<PointerEvents> value) {
		pointerEvents = value;
		if (context != nullptr) {
			context->geometryDirty = true;
		}
	}

	bool Component::containsPoint(Foundation::Vec2 local) const {
		return local.x >= 0.0F && local.y >= 0.0F && local.x < size.x && local.y < size.y;
	}

	void Component::capturePointer() {
		if (context != nullptr) {
			context->capture(selfId);
		}
	}

	void Component::releasePointer() {
		if (context != nullptr && hasPointerCapture()) {
			context->capture({});
		}
	}

	bool Component::hasPointerCapture() const {
		return context != nullptr && context->captor() == selfId;
	}

	void Component::scrollTo(Foundation::Vec2 value) {
		const Foundation::Vec2 clamped = glm::clamp(value, Foundation::Vec2{0.0F, 0.0F}, scrollMax);
		if (clamped != scroll) {
			scroll = clamped;
			if (context != nullptr) {
				context->geometryDirty = true;
				context->hoverStale = true;
			}
		}
	}

	Foundation::Rect Component::getBounds() const {
		return {offset.x, offset.y, size.x + margin.horizontal(), size.y + margin.vertical()};
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

	void Component::contentChanged() {
		layoutChanged(false);
	}

	bool Component::isLayoutBoundary() const {
		return !parentId.valid() || (!sizeSpec.width.isAuto() && !sizeSpec.height.isAuto());
	}

	// A change to its own box is its parent's to lay out again; a change to what it holds is its own,
	// and its ancestors' while their size hangs on it. The walk stops at the first boundary, which the
	// context lays out from on the next pass.
	void Component::layoutChanged(bool ownBox) {
		layoutDirty = true;
		if (context == nullptr) {
			return;
		}
		context->geometryDirty = true;
		Component* node = ownBox ? parentComponent() : this;
		if (node == nullptr) {
			context->dirtyBoundaries.push_back(selfId);
			return;
		}
		while (node != nullptr) {
			node->layoutDirty = true;
			if (node->isLayoutBoundary()) {
				context->dirtyBoundaries.push_back(node->selfId);
				return;
			}
			node = node->parentComponent();
		}
	}

	void Component::orderChanged() const {
		if (context != nullptr) {
			context->orderDirty = true;
		}
	}

} // namespace ui
