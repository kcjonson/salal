#include "Picker.h"

#include <graphics/Color.h>
#include <graphics/PrimitiveStyles.h>
#include <primitives/Primitives.h>

#include <nlohmann/json.hpp>

#include <string_view>
#include <utility>

namespace sandbox {

	namespace {

		constexpr float kMargin = 10.0F; // from the window's edges
		constexpr float kTabWidth = 116.0F;
		constexpr float kTabHeight = 28.0F;
		constexpr float kGap = 6.0F; // between the tab and the list
		constexpr float kPanelWidth = 200.0F;
		constexpr float kHeaderHeight = 32.0F;
		constexpr float kRowHeight = 26.0F;
		constexpr float kInset = 4.0F;		// rows from the panel's sides and bottom
		constexpr float kTextInset = 12.0F; // left-aligned text from its box's left edge
		constexpr float kCorner = 6.0F;

		const Foundation::Color kPanelFill(0.11F, 0.12F, 0.16F, 0.96F);
		const Foundation::Color kTabOpenFill(0.22F, 0.4F, 0.7F, 1.0F);
		const Foundation::Color kBorder(0.32F, 0.36F, 0.46F, 1.0F);
		const Foundation::Color kHeaderText(0.6F, 0.63F, 0.7F, 1.0F);
		const Foundation::Color kRowText(0.85F, 0.87F, 0.9F, 1.0F);
		const Foundation::Color kSelectedFill(0.22F, 0.26F, 0.36F, 1.0F);
		const Foundation::Color kCurrentMark(0.35F, 0.65F, 1.0F, 1.0F);

		struct LabelArgs {
			std::string					text;
			Foundation::Rect			box;
			Foundation::Color			color;
			Foundation::HorizontalAlign align = Foundation::HorizontalAlign::Left;
		};

		// Text in its box, centred on the box's height, and on its width or kTextInset in from its left.
		void label(const LabelArgs& args) {
			const bool centred = args.align == Foundation::HorizontalAlign::Center;
			Renderer::Primitives::drawText(
				{.text = args.text,
				 .position = {centred ? args.box.x : args.box.x + kTextInset, args.box.y},
				 .color = args.color,
				 .hAlign = args.align,
				 .vAlign = Foundation::VerticalAlign::Middle,
				 .boxWidth = centred ? args.box.width : 0.0F,
				 .boxHeight = args.box.height}
			);
		}

		nlohmann::ordered_json bounds(const Foundation::Rect& rect) {
			return {{"x", rect.x}, {"y", rect.y}, {"width", rect.width}, {"height", rect.height}};
		}

	} // namespace

	Picker::Picker(app::App& owningApp, Args args)
		: owner(owningApp),
		  scenes(std::move(args.scenes)),
		  open(args.open),
		  pressedRow(scenes.size()),
		  state(owningApp.devServer().stateProvider("picker", [this] { return stateJson(); })) {}

	bool Picker::handleEvent(const foundation::InputEvent& event) {
		using Type = foundation::InputEvent::Type;
		switch (event.type) {
			case Type::KeyDown:
				return keyDown(event);
			case Type::KeyUp:
				return keyUp(event);
			case Type::PointerDown:
				return pointerDown(event);
			case Type::PointerUp:
				return pointerUp(event);
			case Type::PointerMove:
				if (const std::size_t row = rowAt(event.position); row < scenes.size()) {
					selected = row;
				}
				return false;
			case Type::Scroll:
				return over(event.position);
			case Type::FocusLost: // what was held is released without its events
				heldKeys.reset();
				heldButtons.reset();
				pressedTab = false;
				pressedRow = scenes.size();
				return false;
			default:
				return false;
		}
	}

	void Picker::render() {
		namespace P = Renderer::Primitives;
		const Foundation::Rect tab = tabBounds();

		P::drawRect(
			{.bounds = tab,
			 .style = {
				 .fill = open ? kTabOpenFill : kPanelFill,
				 .border = Foundation::BorderStyle{.color = kBorder, .width = 1.0F, .cornerRadius = kCorner}
			 }}
		);
		label({.text = "F1 scenes", .box = tab, .color = kRowText, .align = Foundation::HorizontalAlign::Center});
		if (!open) {
			return;
		}

		const Foundation::Rect panel = panelBounds();
		P::drawRect(
			{.bounds = panel,
			 .style = {.fill = kPanelFill, .border = Foundation::BorderStyle{.color = kBorder, .width = 1.0F, .cornerRadius = kCorner}}}
		);
		label({.text = "scenes", .box = {panel.x, panel.y, panel.width, kHeaderHeight}, .color = kHeaderText});
		const std::size_t running = current();
		for (std::size_t i = 0; i < scenes.size(); ++i) {
			const Foundation::Rect row = rowBounds(i);
			if (i == selected) {
				P::drawRect({.bounds = row, .style = {.fill = kSelectedFill}});
			}
			if (i == running) {
				P::drawRect({.bounds = {row.x, row.y + 5.0F, 3.0F, row.height - 10.0F}, .style = {.fill = kCurrentMark}});
			}
			label({.text = scenes[i], .box = row, .color = i == running ? Foundation::Color::white() : kRowText});
		}
	}

	Foundation::Rect Picker::tabBounds() const {
		const Renderer::Viewport& window = owner.viewport();
		return {
			static_cast<float>(window.logicalWidth) - kMargin - kTabWidth,
			static_cast<float>(window.logicalHeight) - kMargin - kTabHeight,
			kTabWidth,
			kTabHeight
		};
	}

	Foundation::Rect Picker::panelBounds() const {
		const Foundation::Rect tab = tabBounds();
		const float			   height = kHeaderHeight + (static_cast<float>(scenes.size()) * kRowHeight) + kInset;
		return {tab.right() - kPanelWidth, tab.y - kGap - height, kPanelWidth, height};
	}

	Foundation::Rect Picker::rowBounds(std::size_t index) const {
		const Foundation::Rect panel = panelBounds();
		return {
			panel.x + kInset, panel.y + kHeaderHeight + (static_cast<float>(index) * kRowHeight), kPanelWidth - (2.0F * kInset), kRowHeight
		};
	}

	std::size_t Picker::rowAt(const Foundation::Vec2& point) const {
		if (!open || !panelBounds().contains(point)) {
			return scenes.size();
		}
		for (std::size_t i = 0; i < scenes.size(); ++i) {
			if (rowBounds(i).contains(point)) {
				return i;
			}
		}
		return scenes.size();
	}

	bool Picker::over(const Foundation::Vec2& point) const {
		return tabBounds().contains(point) || (open && panelBounds().contains(point));
	}

	bool Picker::keyDown(const foundation::InputEvent& event) {
		const auto index = static_cast<std::size_t>(event.key);
		if (index >= heldKeys.size()) {
			return false;
		}
		if (event.repeat) {
			if (!heldKeys.test(index)) {
				return false; // the scene has this key
			}
			if (open && (event.key == foundation::Key::Up || event.key == foundation::Key::Down)) {
				moveSelection(event.key == foundation::Key::Up);
			}
			return true;
		}
		const bool taken = pressKey(event.key);
		heldKeys.set(index, taken);
		return taken;
	}

	bool Picker::keyUp(const foundation::InputEvent& event) {
		const auto index = static_cast<std::size_t>(event.key);
		if (index >= heldKeys.size() || !heldKeys.test(index)) {
			return false;
		}
		heldKeys.reset(index);
		return true;
	}

	bool Picker::pointerDown(const foundation::InputEvent& event) {
		const auto button = static_cast<std::size_t>(event.mouseButton);
		if (button >= heldButtons.size()) {
			return false;
		}
		const bool taken = over(event.position);
		heldButtons.set(button, taken);
		if (taken && event.mouseButton == foundation::MouseButton::Left) {
			pressedTab = tabBounds().contains(event.position);
			pressedRow = pressedTab ? scenes.size() : rowAt(event.position);
		}
		return taken;
	}

	bool Picker::pointerUp(const foundation::InputEvent& event) {
		const auto button = static_cast<std::size_t>(event.mouseButton);
		if (button >= heldButtons.size() || !heldButtons.test(button)) {
			return false;
		}
		heldButtons.reset(button);
		if (event.mouseButton == foundation::MouseButton::Left) {
			if (pressedTab && tabBounds().contains(event.position)) {
				setOpen(!open);
			} else if (pressedRow < scenes.size() && rowAt(event.position) == pressedRow) {
				choose(pressedRow);
			}
			pressedTab = false;
			pressedRow = scenes.size();
		}
		return true;
	}

	bool Picker::pressKey(foundation::Key key) {
		using foundation::Key;
		if (key == Key::F1) {
			setOpen(!open);
			return true;
		}
		if (!open) {
			return false;
		}
		switch (key) {
			case Key::Up:
			case Key::Down:
				moveSelection(key == Key::Up);
				return true;
			case Key::Enter:
			case Key::KpEnter:
				if (selected < scenes.size()) {
					choose(selected);
				}
				return true;
			case Key::Escape:
				setOpen(false);
				return true;
			default:
				return false;
		}
	}

	void Picker::moveSelection(bool up) {
		if (scenes.empty()) {
			return;
		}
		selected = up ? (selected + scenes.size() - 1) % scenes.size() : (selected + 1) % scenes.size();
	}

	void Picker::setOpen(bool value) {
		open = value;
		if (open) {
			const std::size_t running = current();
			selected = running < scenes.size() ? running : 0;
		}
	}

	void Picker::choose(std::size_t index) {
		owner.scenes().switchTo(scenes[index]);
		open = false;
	}

	std::size_t Picker::current() const {
		const std::string_view top = owner.scenes().topName();
		for (std::size_t i = 0; i < scenes.size(); ++i) {
			if (scenes[i] == top) {
				return i;
			}
		}
		return scenes.size();
	}

	std::string Picker::stateJson() const {
		nlohmann::ordered_json list = nlohmann::ordered_json::array();
		for (std::size_t i = 0; i < scenes.size(); ++i) {
			list.push_back({{"name", scenes[i]}, {"bounds", bounds(rowBounds(i))}});
		}
		const std::size_t running = current();
		return nlohmann::ordered_json{
			{"open", open},
			{"current", running < scenes.size() ? nlohmann::ordered_json(scenes[running]) : nlohmann::ordered_json(nullptr)},
			{"selected", selected < scenes.size() ? nlohmann::ordered_json(scenes[selected]) : nlohmann::ordered_json(nullptr)},
			{"tab", bounds(tabBounds())},
			{"scenes", list},
		}
			.dump();
	}

} // namespace sandbox
