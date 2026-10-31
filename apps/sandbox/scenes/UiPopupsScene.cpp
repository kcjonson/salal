// ui_popups: the popup stack. A menu opens below its button and closes on a press outside it; one of
// its rows opens a dialog over a scrim, and the dialog's own menu lands above the dialog. A button
// shows a tooltip above it while hovered, over everything. A toast arrives at the top right. A menu
// opened from inside a clipped list escapes the clip. The last action is written at the bottom.

#include "Scenes.h"

#include <application/App.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {

	constexpr const char* kSceneName = "ui_popups";

	const Foundation::Color kBackground(0.1F, 0.1F, 0.12F, 1.0F);
	const Foundation::Color kButton(0.25F, 0.45F, 0.7F, 1.0F);
	const Foundation::Color kPanel(0.18F, 0.2F, 0.25F, 1.0F);
	const Foundation::Color kRow(0.24F, 0.26F, 0.32F, 1.0F);
	const Foundation::Color kScrim(0.0F, 0.0F, 0.0F, 0.55F);
	const Foundation::Color kTip(0.95F, 0.9F, 0.6F, 1.0F);
	const Foundation::Color kLabel(0.85F, 0.85F, 0.6F, 1.0F);

	using ui::Component;
	using ui::PointerEvent;
	using ui::Rectangle;
	using ui::Ref;
	using ui::Text;

	class UiPopupsScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			root().get()->setLayout(
				{.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 24.0F, .padding = 32.0F}
			);
			Ref<Component> row = root().addChild<Component>({.layout = {.display = ui::Display::Flex, .gap = 16.0F}});
			Ref<Rectangle> menuButton = button(row, "menu_button", "Menu", kButton, {});
			menuButton.get()->setOnClick([this, menuButton](PointerEvent& /*event*/) { openMenu(menuButton); });
			Ref<Rectangle> hint = button(row, "tooltip_button", "Hover for a tooltip", kButton, {});
			hint.get()->setOnPointer(PointerEvent::Type::Enter, [this, hint](PointerEvent& /*event*/) { showTip(hint); });
			hint.get()->setOnPointer(PointerEvent::Type::Leave, [this](PointerEvent& /*event*/) { ui().closePopup(tip); });
			button(row, "toast_button", "Toast", kButton, [this] { toast(); });
			clippedList();
			status = root().addChild<Text>({.id = "status", .style = {.color = kLabel, .fontSize = 14.0F}, .text = "Nothing yet"});
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(kBackground); }

	  private:
		void show(const std::string& what) {
			if (Text* line = status.get()) {
				line->setText(what);
			}
		}

		Ref<Rectangle> button(
			Ref<Component>		  parent,
			const std::string&	  id,
			const std::string&	  label,
			Foundation::Color	  color,
			std::function<void()> onClick
		) {
			Ref<Rectangle> made = parent.addChild<Rectangle>({
				.id = id,
				.size = {180.0F, 40.0F},
				.style = {.backgroundColor = color, .borderRadius = 6.0F},
				.cursor = ui::Cursor::Pointer,
			});
			if (onClick) {
				made.get()->setOnClick([onClick](PointerEvent& /*event*/) { onClick(); });
			}
			made.addChild<Text>({
				.size = {ui::percent(100.0F), ui::percent(100.0F)},
				.style =
					{.fontSize = 14.0F,
					 .textAlign = Foundation::HorizontalAlign::Center,
					 .verticalAlign = Foundation::VerticalAlign::Middle},
				.pointerEvents = ui::PointerEvents::None,
				.text = label,
			});
			return made;
		}

		// A column of rows under `opener`, each running its action and closing the menu.
		Ref<Rectangle>
		menu(Ref<Component> opener, const std::string& id, const std::vector<std::pair<std::string, std::function<void()>>>& rows) {
			Ref<Rectangle> made = opener.get()->openPopup<Rectangle>(
				{.id = id,
				 .layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 4.0F, .padding = 6.0F},
				 .style = {.backgroundColor = kPanel, .borderRadius = 6.0F}},
				{.kind = ui::PopupKind::Menu, .placement = ui::Placement{}, .onDismiss = [this, id] {
					 show(id + " dismissed by a press outside");
				 }}
			);
			for (const auto& [label, action] : rows) {
				button(made, id + "_" + label, label, kRow, [this, made, action] {
					ui().closePopup(made);
					action();
				});
			}
			return made;
		}

		void openMenu(Ref<Component> opener) {
			menu(
				opener,
				"menu",
				{
					{"Dialog", [this] { openDialog(); }},
					{"Toast", [this] { toast(); }},
					{"Nothing", [this] { show("Picked nothing"); }},
				}
			);
			show("Menu open");
		}

		// A scrim over the viewport, the panel centred on it, modal for player 0.
		void openDialog() {
			Ref<Rectangle> scrim = root().get()->openPopup<Rectangle>(
				{.id = "dialog",
				 .layout =
					 {.display = ui::Display::Flex, .alignItems = ui::AlignItems::Center, .justifyContent = ui::JustifyContent::Center},
				 .style = {.backgroundColor = kScrim}},
				{.kind = ui::PopupKind::Dialog, .cover = true}
			);
			Ref<Rectangle> panel = scrim.addChild<Rectangle>({
				.id = "dialog_panel",
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 16.0F, .padding = 24.0F},
				.style = {.backgroundColor = kPanel, .borderRadius = 8.0F},
			});
			panel.addChild<Text>({.style = {.color = kLabel, .fontSize = 16.0F}, .text = "A dialog: the scene under it takes no pointer"});
			Ref<Component> actions = panel.addChild<Component>({.layout = {.display = ui::Display::Flex, .gap = 12.0F}});
			Ref<Rectangle> more = button(actions, "dialog_menu", "Its menu", kButton, {});
			more.get()->setOnClick([this, more](PointerEvent& /*event*/) {
				menu(more, "dialog_menu_open", {{"Above", [this] { show("The dialog's menu landed above it"); }}});
			});
			button(actions, "dialog_close", "Close", kButton, [this, scrim] {
				ui().closePopup(scrim);
				show("Dialog closed");
			});
		}

		void showTip(Ref<Component> anchor) {
			ui().closePopup(tip);
			tip = anchor.get()->openPopup<Rectangle>(
				{.id = "tooltip", .layout = {.padding = 6.0F}, .style = {.backgroundColor = kTip, .borderRadius = 4.0F}},
				{.kind = ui::PopupKind::Tooltip, .placement = ui::Placement{.side = ui::Side::Above}}
			);
			tip.addChild<Text>({.style = {.color = kBackground, .fontSize = 13.0F}, .text = "Above everything, and never hit"});
		}

		// Brought to the front as it arrives; a click closes it.
		void toast() {
			const std::string id = "toast_" + std::to_string(++toasts);
			Ref<Rectangle>	  made = root().get()->openPopup<Rectangle>(
				   {.id = id,
					.size = {220.0F, 48.0F},
					.style = {.backgroundColor = kRow, .borderRadius = 6.0F},
					.cursor = ui::Cursor::Pointer},
				   {.kind = ui::PopupKind::Toast, .placement = ui::Placement{.at = Foundation::Vec2{10000.0F, 16.0F}}}
			   );
			made.get()->setOnClick([this, made](PointerEvent& /*event*/) { ui().closePopup(made); });
			made.addChild<Text>(
				{.position = {12.0F, 14.0F},
				 .style = {.fontSize = 14.0F},
				 .pointerEvents = ui::PointerEvents::None,
				 .text = id + ": click to close"}
			);
			ui().bringToFront(made);
			show("Toast " + std::to_string(toasts));
		}

		// Its rows are clipped to the list; the menu a row opens isn't.
		void clippedList() {
			Ref<Component> list = root().addChild<Component>({
				.id = "clipped_list",
				.size = {220.0F, 120.0F},
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 4.0F},
				.overflow = ui::Overflow::Scroll,
			});
			for (int i = 0; i < 6; ++i) {
				Ref<Rectangle> rowButton = button(list, "list_row_" + std::to_string(i + 1), "Row " + std::to_string(i + 1), kRow, {});
				rowButton.get()->setOnClick([this, rowButton, i](PointerEvent& /*event*/) {
					menu(rowButton, "list_menu", {{"Escaped", [this, i] {
													   show("Row " + std::to_string(i + 1) + "'s menu escaped the clip");
												   }}});
				});
			}
		}

		Ref<Text>	   status;
		Ref<Rectangle> tip;
		int			   toasts = 0;
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiPopupsScene{.name = kSceneName, .make = makeScene<UiPopupsScene>};
} // namespace sandbox
