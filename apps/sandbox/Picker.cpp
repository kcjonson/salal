#include "Picker.h"

#include <graphics/Color.h>
#include <graphics/PrimitiveStyles.h>
#include <primitives/Primitives.h>

#include <nlohmann/json.hpp>

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
		constexpr float kTextInset = 12.0F; // text from a row's left edge
		constexpr float kCorner = 6.0F;

		const Foundation::Color kPanelFill(0.11F, 0.12F, 0.16F, 0.96F);
		const Foundation::Color kTabOpenFill(0.22F, 0.4F, 0.7F, 1.0F);
		const Foundation::Color kBorder(0.32F, 0.36F, 0.46F, 1.0F);
		const Foundation::Color kHeaderText(0.6F, 0.63F, 0.7F, 1.0F);
		const Foundation::Color kRowText(0.85F, 0.87F, 0.9F, 1.0F);
		const Foundation::Color kSelectedFill(0.22F, 0.26F, 0.36F, 1.0F);
		const Foundation::Color kCurrentMark(0.35F, 0.65F, 1.0F, 1.0F);

		// `text` in `box`, centred on its height, and on its width or kTextInset in from its left.
		void
		label(const std::string& text, const Foundation::Rect& box, const Foundation::Color& color, Foundation::HorizontalAlign align) {
			const bool centred = align == Foundation::HorizontalAlign::Center;
			Renderer::Primitives::drawText(
				{.text = text,
				 .position = {centred ? box.x : box.x + kTextInset, box.y},
				 .color = color,
				 .hAlign = align,
				 .vAlign = Foundation::VerticalAlign::Middle,
				 .boxWidth = centred ? box.width : 0.0F,
				 .boxHeight = box.height}
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
		  state(owningApp.devServer().stateProvider("picker", [this] { return stateJson(); })) {}

	bool Picker::handleEvent(const foundation::InputEvent& event) {
		using Type = foundation::InputEvent::Type;
		switch (event.type) {
			case Type::KeyDown:
				return handleKey(event);
			case Type::PointerMove:
			case Type::PointerDown:
			case Type::PointerUp:
				return handlePointer(event);
			default:
				return false;
		}
	}

	void Picker::render() {
		namespace P = Renderer::Primitives;
		const Layout where = layout();

		P::drawRect(
			{.bounds = where.tab,
			 .style = {
				 .fill = open ? kTabOpenFill : kPanelFill,
				 .border = Foundation::BorderStyle{.color = kBorder, .width = 1.0F, .cornerRadius = kCorner}
			 }}
		);
		label("F1 scenes", where.tab, kRowText, Foundation::HorizontalAlign::Center);
		if (!open) {
			return;
		}

		P::drawRect(
			{.bounds = where.panel,
			 .style = {.fill = kPanelFill, .border = Foundation::BorderStyle{.color = kBorder, .width = 1.0F, .cornerRadius = kCorner}}}
		);
		label("scenes", {where.panel.x, where.panel.y, where.panel.width, kHeaderHeight}, kHeaderText, Foundation::HorizontalAlign::Left);
		const std::size_t running = current();
		for (std::size_t i = 0; i < scenes.size(); ++i) {
			const Foundation::Rect& row = where.rows[i];
			if (i == selected) {
				P::drawRect({.bounds = row, .style = {.fill = kSelectedFill}});
			}
			if (i == running) {
				P::drawRect({.bounds = {row.x, row.y + 5.0F, 3.0F, row.height - 10.0F}, .style = {.fill = kCurrentMark}});
			}
			label(scenes[i], row, i == running ? Foundation::Color::white() : kRowText, Foundation::HorizontalAlign::Left);
		}
	}

	Picker::Layout Picker::layout() const {
		const Renderer::Viewport& window = owner.viewport();
		const float				  width = static_cast<float>(window.logicalWidth);
		const float				  height = static_cast<float>(window.logicalHeight);
		const float				  panelHeight = kHeaderHeight + (static_cast<float>(scenes.size()) * kRowHeight) + kInset;

		Layout where{
			.tab = {width - kMargin - kTabWidth, height - kMargin - kTabHeight, kTabWidth, kTabHeight},
			.panel = {width - kMargin - kPanelWidth, height - kMargin - kTabHeight - kGap - panelHeight, kPanelWidth, panelHeight},
		};
		where.rows.reserve(scenes.size());
		for (std::size_t i = 0; i < scenes.size(); ++i) {
			where.rows.emplace_back(
				where.panel.x + kInset,
				where.panel.y + kHeaderHeight + (static_cast<float>(i) * kRowHeight),
				kPanelWidth - (2.0F * kInset),
				kRowHeight
			);
		}
		return where;
	}

	bool Picker::handleKey(const foundation::InputEvent& event) {
		using foundation::Key;
		if (event.key == Key::F1) {
			if (!event.repeat) {
				setOpen(!open);
			}
			return true;
		}
		if (!open || scenes.empty()) {
			return false;
		}
		switch (event.key) {
			case Key::Up:
				selected = (selected + scenes.size() - 1) % scenes.size();
				return true;
			case Key::Down:
				selected = (selected + 1) % scenes.size();
				return true;
			case Key::Enter:
			case Key::KpEnter:
				choose(selected);
				return true;
			case Key::Escape:
				setOpen(false);
				return true;
			default:
				return false;
		}
	}

	bool Picker::handlePointer(const foundation::InputEvent& event) {
		const Layout where = layout();
		const bool	 overTab = where.tab.contains(event.position);
		const bool	 overPanel = open && where.panel.contains(event.position);
		std::size_t	 row = scenes.size();
		for (std::size_t i = 0; overPanel && i < scenes.size(); ++i) {
			if (where.rows[i].contains(event.position)) {
				row = i;
				break;
			}
		}

		using Type = foundation::InputEvent::Type;
		if (event.type == Type::PointerMove) {
			if (row < scenes.size()) {
				selected = row;
			}
			return false;
		}
		if (event.type == Type::PointerDown && event.mouseButton == foundation::MouseButton::Left) {
			if (overTab) {
				setOpen(!open);
			} else if (row < scenes.size()) {
				choose(row);
			}
		}
		return overTab || overPanel;
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
		const Layout		   where = layout();
		nlohmann::ordered_json list = nlohmann::ordered_json::array();
		for (std::size_t i = 0; i < scenes.size(); ++i) {
			list.push_back({{"name", scenes[i]}, {"bounds", bounds(where.rows[i])}});
		}
		const std::size_t running = current();
		return nlohmann::ordered_json{
			{"open", open},
			{"current", running < scenes.size() ? nlohmann::ordered_json(scenes[running]) : nlohmann::ordered_json(nullptr)},
			{"selected", selected < scenes.size() ? nlohmann::ordered_json(scenes[selected]) : nlohmann::ordered_json(nullptr)},
			{"tab", bounds(where.tab)},
			{"scenes", list},
		}
			.dump();
	}

} // namespace sandbox
