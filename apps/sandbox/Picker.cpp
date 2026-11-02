#include "Picker.h"

#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>

#include <nlohmann/json.hpp>

#include <string_view>
#include <utility>

namespace sandbox {

	namespace {
		constexpr float kMargin = 10.0F; // from the window's edges
		constexpr float kTabWidth = 116.0F;
		constexpr float kGap = 6.0F; // between the list and the tab
		constexpr float kPanelWidth = 200.0F;
		constexpr float kInset = 4.0F;

		Foundation::Vec2 logicalSize(const Renderer::Viewport& viewport) {
			return {static_cast<float>(viewport.logicalWidth), static_cast<float>(viewport.logicalHeight)};
		}

		nlohmann::ordered_json bounds(const Foundation::Rect& rect) {
			return {{"x", rect.x}, {"y", rect.y}, {"width", rect.width}, {"height", rect.height}};
		}
	} // namespace

	Picker::Picker(app::App& owningApp, Args args)
		: owner(owningApp),
		  scenes(std::move(args.scenes)),
		  context(logicalSize(owningApp.viewport())),
		  state(owningApp.devServer().stateProvider("picker", [this] { return stateJson(); })) {
		context.setScale(owningApp.uiScale());
		ui::Ref<ui::Component> root = context.root();
		root.get()->setLayout({
			.display = ui::Display::Flex,
			.flexDirection = ui::FlexDirection::Column,
			.gap = kGap,
			.padding = kMargin,
			.alignItems = ui::AlignItems::End,
			.justifyContent = ui::JustifyContent::End,
		});
		panel = root.addChild<ui::Rectangle>({
			.id = "picker_panel",
			.visible = false,
			.size = {kPanelWidth, ui::kAuto},
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .padding = kInset},
			.style = ui::styles::overlay,
			.focusGroup = ui::FocusGroup{.orientation = ui::FocusGroup::Orientation::Vertical, .wrap = true},
		});
		panel.addChild<ui::Text>({.margin = {6.0F, 8.0F}, .style = ui::styles::label, .text = "SCENES"});
		for (std::size_t i = 0; i < scenes.size(); ++i) {
			rows.push_back(panel.addChild<ui::ListRow>({
				.id = "picker_" + scenes[i],
				.label = scenes[i],
				.onPress = [this, i] { choose(i); },
			}));
		}
		tab = root.addChild<ui::Button>({
			.id = "picker_tab",
			.size = {kTabWidth, ui::kAuto},
			.label = "F1 scenes",
			.controlSize = ui::ControlSize::Sm,
			.onPress = [this] { setOpen(!open); },
		});
		setOpen(args.open);
	}

	bool Picker::handleEvent(const foundation::InputEvent& event) {
		using Type = foundation::InputEvent::Type;
		const bool keyboard = event.type == Type::KeyDown || event.type == Type::KeyUp || event.type == Type::Char;
		if (keyboard && event.key == foundation::Key::F1) {
			if (event.type == Type::KeyDown && !event.repeat) {
				setOpen(!open);
			}
			return true;
		}
		if (keyboard) {
			if (!open) {
				return false;
			}
			if (event.type == Type::KeyDown && event.key == foundation::Key::Escape) {
				setOpen(false);
			} else {
				context.dispatch(event);
			}
			return true;
		}
		const bool took = context.dispatch(event);
		return took && event.type != Type::FocusLost;
	}

	void Picker::update(const app::FrameContext& frame) {
		const std::size_t running = current();
		for (std::size_t i = 0; i < rows.size(); ++i) {
			if (ui::ListRow* row = rows[i].get()) {
				row->setSelected(i == running);
			}
		}
		context.update(frame.dt);
	}

	void Picker::render() {
		context.layout();
		context.render();
	}

	void Picker::onResize(const Renderer::Viewport& viewport) {
		context.setViewport(logicalSize(viewport));
	}

	void Picker::setOpen(bool value) {
		open = value;
		panel.get()->setVisible(open);
		if (open) {
			const std::size_t running = current();
			if (running < rows.size()) {
				rows[running].get()->focus();
			}
		}
	}

	void Picker::choose(std::size_t index) {
		owner.scenes().switchTo(scenes[index]);
		setOpen(false);
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
		std::string			   selected;
		const ui::Component*   focused = const_cast<ui::Context&>(context).focused().get();
		for (std::size_t i = 0; i < scenes.size(); ++i) {
			const ui::ListRow* row = rows[i].get();
			list.push_back(
				{{"name", scenes[i]}, {"bounds", row != nullptr ? bounds(row->screenBounds()) : nlohmann::ordered_json(nullptr)}}
			);
			if (row != nullptr && row == focused) {
				selected = scenes[i];
			}
		}
		const std::size_t running = current();
		return nlohmann::ordered_json{
			{"open", open},
			{"current", running < scenes.size() ? nlohmann::ordered_json(scenes[running]) : nlohmann::ordered_json(nullptr)},
			{"selected", selected.empty() ? nlohmann::ordered_json(nullptr) : nlohmann::ordered_json(selected)},
			{"tab", bounds(tab.get()->screenBounds())},
			{"scenes", list},
		}
			.dump();
	}

} // namespace sandbox
