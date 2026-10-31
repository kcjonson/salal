// ui_overlay_components: ui's overlays, after worldsim's ui-sandbox context menu, dialog, dropdown,
// toast and tooltip scenes (apps/ui-sandbox/scenes/, a4226e17): a select, two dropdown buttons (one
// opening upward), a modal dialog with a select of its own, whose menu lands above it, and a
// non-modal one; toasts of each severity at the top right; buttons with tooltips; and an area that
// opens a context menu where it's right-clicked. What each last did shows in a status line, and
// /api/state?what=scene has it too.

#include "Scenes.h"

#include <application/App.h>
#include <primitives/Primitives.h>
#include <ui/components/Button.h>
#include <ui/components/ContextMenu.h>
#include <ui/components/Dialog.h>
#include <ui/components/DropdownButton.h>
#include <ui/components/Select.h>
#include <ui/components/Toast.h>
#include <ui/components/Tooltip.h>
#include <ui/theme/Tokens.h>

#include <nlohmann/json.hpp>

#include <string>

namespace {

	constexpr const char* kSceneName = "ui_overlay_components";

	using namespace ui;

	Layout flex(FlexDirection direction, float gap) {
		return {.display = Display::Flex, .flexDirection = direction, .gap = gap, .alignItems = AlignItems::Start};
	}

	class UiOverlayComponentsScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			Layout stage = flex(FlexDirection::Column, 24.0F);
			stage.padding = 24.0F;
			root().get()->setLayout(stage);
			root().addChild<Text>({.id = "title", .style = styles::text::display, .text = "Overlays"});
			status = root().addChild<Text>({.id = "status", .style = styles::text::caption, .text = "Nothing yet"});
			toasts = ToastStack::open(*root().get(), {.id = "toasts", .anchor = ToastAnchor::TopRight});
			menus(root());
			dialogs(root());
			toastButtons(root());
			tooltips(root());
			contextArea(root());
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(ui().getTheme().get(tokens::color::surface::base)); }

		std::string exportState() const override { return nlohmann::json{{"last", last}}.dump(); }

	  private:
		void did(std::string what) {
			last = std::move(what);
			if (Text* shown = status.get()) {
				shown->setText(last);
			}
		}

		void menus(Ref<Component> column) {
			Ref<Component> row = column.addChild<Component>({.layout = flex(FlexDirection::Row, 12.0F)});
			row.addChild<Select>({
				.id = "difficulty",
				.options = {{.label = "Easy", .value = "easy"}, {.label = "Normal", .value = "normal"}, {.label = "Hard", .value = "hard"}},
				.value = "normal",
				.onChange = [this](const std::string& value) { did("difficulty " + value); },
			});
			row.addChild<Select>({
				.id = "ship",
				.options = {{.label = "Kestrel", .value = "kestrel"}, {.label = "Osprey", .value = "osprey"}},
				.placeholder = "Pick a ship",
			});
			row.addChild<DropdownButton>({
				.id = "actions",
				.label = "Actions",
				.items = {
					{.label = "Inspect", .onSelect = [this] { did("inspect"); }, .hint = "I"},
					{.label = "Repair", .onSelect = [this] { did("repair"); }},
					{.label = "Scrap", .onSelect = [this] { did("scrap"); }, .enabled = false}
				},
			});
		}

		void dialogs(Ref<Component> column) {
			Ref<Component> row = column.addChild<Component>({.layout = flex(FlexDirection::Row, 12.0F)});
			Ref<Button>	   modal = row.addChild<Button>({.id = "open_dialog", .label = "Abandon run..."});
			modal.get()->setOnPress([this, modal] { abandonDialog(*modal.get()); });
			Ref<Button> panel = row.addChild<Button>({.id = "open_panel", .label = "Crew panel"});
			panel.get()->setOnPress([this, panel] {
				Ref<Dialog> crew = Dialog::open(
					*panel.get(), {.id = "crew_panel", .title = "Crew", .panelSize = {280.0F, kAuto}, .modal = false, .onClose = [this] {
									   did("closed crew");
								   }}
				);
				crew.get()->content().addChild<Text>({.style = styles::text::body, .text = "Ada, Brook, Cass"});
				did("opened crew");
			});
			row.addChild<DropdownButton>({
				.id = "build",
				.label = "Build",
				.items = {{.label = "Wall", .onSelect = [this] { did("wall"); }}, {.label = "Door", .onSelect = [this] { did("door"); }}},
				.side = Side::Above,
			});
		}

		void abandonDialog(Component& opener) {
			Ref<Dialog> dialog = Dialog::open(
				opener,
				{
					.id = "abandon",
					.title = "Abandon run?",
					.kicker = "Confirm",
					.panelSize = {420.0F, kAuto},
					.footer = true,
					.onClose = [this] { did("kept playing"); },
				}
			);
			Ref<Component> body = dialog.get()->content();
			body.addChild<Text>({.style = styles::text::body, .text = "The crew and the cargo are lost.", .wrap = true});
			body.addChild<Select>({
				.id = "reason",
				.options =
					{{.label = "Too hard", .value = "hard"}, {.label = "Bored", .value = "bored"}, {.label = "Other", .value = "other"}},
				.placeholder = "Why?",
			});
			dialog.get()->footer().addChild<Button>({.label = "Keep playing", .onPress = [dialog] {
														 if (Dialog* shown = dialog.get()) {
															 shown->close();
														 }
													 }});
			dialog.get()->footer().addChild<Button>({.label = "Abandon", .variant = ButtonVariant::Danger, .onPress = [this, dialog] {
														 did("abandoned");
														 if (Dialog* shown = dialog.get()) {
															 shown->getContext()->closeOverlay(dialog);
														 }
													 }});
			did("opened abandon");
		}

		void toastButtons(Ref<Component> column) {
			Ref<Component> row = column.addChild<Component>({.layout = flex(FlexDirection::Row, 12.0F)});
			row.addChild<Button>({.id = "toast_info", .label = "Info toast", .onPress = [this] {
									  toasts.get()->add({.title = "Docked", .message = "Repairs are free while docked."});
								  }});
			row.addChild<Button>(
				{.id = "toast_warning", .label = "Warning toast", .onPress = [this] {
					 toasts.get()->add({.title = "Low fuel", .message = "Two jumps left.", .severity = ToastSeverity::Warning});
				 }}
			);
			row.addChild<Button>({.id = "toast_critical", .label = "Critical toast", .onPress = [this] {
									  toasts.get()->add({
										  .title = "Hull breach",
										  .message = "Press to see where.",
										  .severity = ToastSeverity::Critical,
										  .autoDismissMs = 0.0,
										  .onPress = [this] { did("pressed breach"); },
									  });
								  }});
		}

		void tooltips(Ref<Component> column) {
			Ref<Component> row = column.addChild<Component>({.layout = flex(FlexDirection::Row, 12.0F)});
			Ref<Button>	   save = row.addChild<Button>({.id = "save", .label = "Save"});
			save.get()->setTooltip(tooltip({.title = "Save", .description = "Writes the run to its slot.", .hotkey = "Ctrl+S"}));
			Ref<Button> scan = row.addChild<Button>({.id = "scan", .label = "Scan"});
			scan.get()->setTooltip(tooltip(
				TooltipText{
					.title = "Long-range scan",
					.description = "Shows every ship within three sectors for one turn, and tells them where you are.",
				}
			));
		}

		void contextArea(Ref<Component> column) {
			Ref<Rectangle> area = column.addChild<Rectangle>({
				.id = "context_area",
				.size = {480.0F, 120.0F},
				.layout = {.display = Display::Flex, .alignItems = AlignItems::Center, .justifyContent = JustifyContent::Center},
				.style = styles::inset,
			});
			area.addChild<Text>({.style = styles::text::secondary, .text = "Right-click here"});
			area.get()->setOnPointer(PointerEvent::Type::Down, [this, area](PointerEvent& event) {
				if (event.button != foundation::MouseButton::Right) {
					return;
				}
				ContextMenu::open(
					*area.get(),
					event.local,
					{.id = "context_menu",
					 .items = {
						 {.label = "Cut", .onSelect = [this] { did("cut"); }, .hint = "Ctrl+X"},
						 {.label = "Copy", .onSelect = [this] { did("copy"); }, .hint = "Ctrl+C"},
						 {.label = "Paste", .onSelect = [this] { did("paste"); }, .enabled = false, .hint = "Ctrl+V"},
					 }}
				);
				event.consume();
			});
		}

		Ref<Text>		status;
		Ref<ToastStack> toasts;
		std::string		last;
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiOverlayComponentsScene{.name = kSceneName, .make = makeScene<UiOverlayComponentsScene>};
} // namespace sandbox
