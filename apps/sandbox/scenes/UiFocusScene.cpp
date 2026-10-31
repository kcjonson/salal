// ui_focus: focus for two players on a split keyboard. Player 1 is the arrows, Enter, Escape and Tab;
// player 2 is WASD, E to activate, Q to cancel and R for next. A pad that connects takes the first
// keyboard half.
// A menu down the left is a vertical focus group, the tiles beside it are found by direction, and the
// list on the right is a wrapping group in a scroller, so focus scrolls it. Each control draws a ring
// for each player on it; activating counts. "Dialog" opens a scope for whoever activated it, which its
// cancel (Escape, Q or a pad's B) closes, handing focus back.

#include "Scenes.h"

#include <application/App.h>
#include <graphics/Color.h>
#include <input/Input.h>
#include <primitives/Primitives.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <array>
#include <string>

namespace {

	constexpr const char* kSceneName = "ui_focus";
	constexpr float		  kRing = 3.0F;

	const Foundation::Color				   kBackground(0.1F, 0.1F, 0.12F, 1.0F);
	const Foundation::Color				   kControl(0.25F, 0.3F, 0.38F, 1.0F);
	const Foundation::Color				   kLabel(0.85F, 0.85F, 0.6F, 1.0F);
	const std::array<Foundation::Color, 2> kPlayerRing{
		Foundation::Color(0.35F, 0.75F, 1.0F, 1.0F),
		Foundation::Color(1.0F, 0.55F, 0.3F, 1.0F),
	};

	using ui::Component;
	using ui::KeyEvent;
	using ui::Ref;
	using ui::Text;

	// A box that rings itself, outside its edge, in each focusing player's colour; the second ring
	// sits outside the first.
	class Control final : public ui::Rectangle {
	  public:
		[[nodiscard]] float inkExtent() const override { return kRing * 2.0F; }

	  protected:
		void render() override {
			Rectangle::render();
			for (int player = 0; player < 2; ++player) {
				if (!isFocusedBy(player)) {
					continue;
				}
				const float out = kRing * static_cast<float>(player + 1);
				Renderer::Primitives::drawRect({
					.bounds = {-out, -out, getSize().x + (out * 2.0F), getSize().y + (out * 2.0F)},
					.style =
						{.fill = {0.0F, 0.0F, 0.0F, 0.0F},
						 .border = Foundation::BorderStyle{.color = kPlayerRing[player], .width = kRing, .cornerRadius = 6.0F + out}},
					.key = drawKey(),
				});
			}
		}
	};

	foundation::ActionMap wasd() {
		using foundation::Key;
		foundation::ActionMap map;
		const auto			  bind = [&](std::string_view action, Key key) {
			   map.bind(action, {.source = foundation::Binding::Source::Key, .control = static_cast<std::uint16_t>(key)});
		};
		bind(ui::actions::kUp, Key::W);
		bind(ui::actions::kDown, Key::S);
		bind(ui::actions::kLeft, Key::A);
		bind(ui::actions::kRight, Key::D);
		bind(ui::actions::kActivate, Key::E);
		bind(ui::actions::kCancel, Key::Q);
		bind(ui::actions::kNext, Key::R);
		return map;
	}

	class UiFocusScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& app) override {
			host = &app;
			const foundation::DeviceId keyboard{.kind = foundation::DeviceKind::Keyboard};
			app.input().claim(0, keyboard);
			app.input().claim(1, keyboard);
			ui().setNavigation(1, wasd());

			root().get()->setLayout(
				{.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 20.0F, .padding = 24.0F}
			);
			root().addChild<Text>({
				.style = {.color = kLabel, .fontSize = 14.0F},
				.text = "Player 1: arrows, Enter, Tab. Player 2: WASD, E, Q, R. A pad takes a keyboard half.",
			});
			Ref<Component> body = root().addChild<Component>({.layout = {.display = ui::Display::Flex, .gap = 32.0F}});
			menu(body);
			tiles(body);
			list(body);
			status = root().addChild<Text>({.id = "status", .style = {.color = kLabel, .fontSize = 14.0F}, .text = "Nothing yet"});
		}

		void onExit() override {
			host->input().release(0);
			host->input().release(1);
		}

		void update(const app::FrameContext& /*frame*/) override { claimPads(); }

		void render(float /*alpha*/) override { Renderer::Primitives::clear(kBackground); }

	  private:
		void show(const std::string& what) {
			if (Text* line = status.get()) {
				line->setText(what);
			}
		}

		Ref<Control> control(Ref<Component> parent, const std::string& id, const std::string& label, ui::Size size) {
			Ref<Control> made = parent.addChild<Control>({
				.id = id,
				.size = size,
				.style = {.backgroundColor = kControl, .borderRadius = 6.0F},
				.focusable = true,
				.onKeyDown = [this, label, count = 0](KeyEvent& event) mutable {
					if (event.type == KeyEvent::Type::Activate) {
						++count;
						show("Player " + std::to_string(event.player + 1) + " activated " + label + " (" + std::to_string(count) + ")");
						event.consume();
					}
				},
			});
			made.addChild<Text>({
				.size = {ui::percent(100.0F), ui::percent(100.0F)},
				.style =
					{.fontSize = 13.0F,
					 .textAlign = Foundation::HorizontalAlign::Center,
					 .verticalAlign = Foundation::VerticalAlign::Middle},
				.pointerEvents = ui::PointerEvents::None,
				.text = label,
			});
			return made;
		}

		void menu(Ref<Component> body) {
			Ref<Component> group = body.addChild<Component>({
				.id = "menu",
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 12.0F},
				.focusGroup = ui::FocusGroup{.orientation = ui::FocusGroup::Orientation::Vertical},
			});
			for (const char* label : {"Play", "Options", "Credits"}) {
				control(group, std::string("menu_") + label, label, {140.0F, 40.0F});
			}
			Ref<Control> open = control(group, "menu_dialog", "Dialog", {140.0F, 40.0F});
			open.get()->setOnKeyDown([this](KeyEvent& event) {
				if (event.type == KeyEvent::Type::Activate) {
					openDialog(event.player);
					event.consume();
				}
			});
		}

		void tiles(Ref<Component> body) {
			Ref<Component> grid = body.addChild<Component>({.id = "tiles", .size = {3 * 90.0F, 3 * 70.0F}});
			for (int i = 0; i < 9; ++i) {
				Ref<Control> tile = control(grid, "tile_" + std::to_string(i + 1), "Tile " + std::to_string(i + 1), {80.0F, 60.0F});
				tile.get()->setPosition({static_cast<float>(i % 3) * 90.0F, static_cast<float>(i / 3) * 70.0F});
			}
		}

		void list(Ref<Component> body) {
			Ref<Component> scroller = body.addChild<Component>({
				.id = "list",
				.size = {180.0F, 220.0F},
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 8.0F, .padding = 8.0F},
				.overflow = ui::Overflow::Scroll,
				.focusGroup = ui::FocusGroup{.orientation = ui::FocusGroup::Orientation::Vertical, .wrap = true},
			});
			for (int i = 0; i < 12; ++i) {
				control(scroller, "row_" + std::to_string(i + 1), "Row " + std::to_string(i + 1), {150.0F, 36.0F});
			}
		}

		// A scope for `player`: their navigation stays in it, and its cancel closes it.
		void openDialog(int player) {
			if (dialog.get() != nullptr) {
				return;
			}
			dialog = ui().addRoot<ui::Rectangle>({
				.id = "dialog",
				.layout =
					{.display = ui::Display::Flex, .alignItems = ui::AlignItems::Center, .justifyContent = ui::JustifyContent::Center},
				.style = {.backgroundColor = Foundation::Color(0.0F, 0.0F, 0.0F, 0.5F)},
				.zIndex = 10,
			});
			Ref<Control> panel = dialog.addChild<Control>({
				.id = "dialog_panel",
				.size = {320.0F, 140.0F},
				.layout = {.display = ui::Display::Flex, .gap = 16.0F, .padding = 24.0F, .alignItems = ui::AlignItems::End},
				.style = {.backgroundColor = Foundation::Color(0.18F, 0.2F, 0.25F, 1.0F), .borderRadius = 8.0F},
				.onKeyDown = [this](KeyEvent& event) {
					if (event.type == KeyEvent::Type::Cancel) {
						show("Dialog cancelled");
						closeDialog();
						event.consume();
					}
				},
			});
			for (const char* label : {"OK", "Cancel"}) {
				Ref<Control> choice = control(panel, std::string("dialog_") + label, label, {120.0F, 40.0F});
				choice.get()->setOnKeyDown([this, label = std::string(label)](KeyEvent& event) {
					if (event.type == KeyEvent::Type::Activate) {
						show("Dialog: " + label);
						closeDialog();
						event.consume();
					}
				});
			}
			ui().pushScope(dialog, player);
			show("Dialog open for player " + std::to_string(player + 1));
		}

		// Removing the scope's root closes it, and the layout hands focus back.
		void closeDialog() {
			if (dialog.get() != nullptr) {
				ui().removeRoot(dialog);
				dialog = {};
			}
		}

		// A pad that isn't a player's yet takes the first slot still on the keyboard.
		void claimPads() {
			app::Input& input = host->input();
			for (const app::DeviceInfo& device : input.devices()) {
				if (device.id.kind != foundation::DeviceKind::Gamepad) {
					continue;
				}
				bool held = false;
				for (int slot = 0; slot < app::kMaxPlayerSlots; ++slot) {
					held = held || input.deviceForSlot(slot) == device.id;
				}
				for (int slot = 0; slot < 2 && !held; ++slot) {
					if (input.deviceForSlot(slot) == foundation::DeviceId{.kind = foundation::DeviceKind::Keyboard}) {
						input.claim(slot, device.id);
						show("Pad " + std::to_string(device.id.index) + " is player " + std::to_string(slot + 1));
						held = true;
					}
				}
			}
		}

		app::App*	   host = nullptr;
		Ref<Text>	   status;
		Ref<Component> dialog;
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiFocusScene{.name = kSceneName, .make = makeScene<UiFocusScene>};
} // namespace sandbox
