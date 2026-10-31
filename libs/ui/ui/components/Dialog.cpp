#include "ui/components/Dialog.h"

#include "ui/Context.h"
#include "ui/components/Control.h"
#include "ui/components/Divider.h"
#include "ui/components/FadeIn.h"
#include "ui/shapes/Line.h"
#include "ui/theme/Tokens.h"

namespace ui {

	namespace {

		constexpr float kPad = 16.0F;
		constexpr float kClose = 28.0F;
		constexpr float kCloseInset = 6.0F;

		// An X of two strokes.
		class DialogClose : public Control {
		  public:
			struct Args {
				UI_COMMON_ARGS
			};

			[[nodiscard]] const char* typeName() const override { return "DialogClose"; }

		  protected:
			void onMount() override {
				Control::onMount();
				const Style stroke{.color = getContext()->getTheme().get(tokens::color::text::secondary)};
				addChild<Line>(
					{.style = stroke,
					 .pointerEvents = PointerEvents::None,
					 .start = {9.0F, 9.0F},
					 .end = {19.0F, 19.0F},
					 .lineWidth = 1.5F,
					 .roundCaps = true}
				);
				addChild<Line>(
					{.style = stroke,
					 .pointerEvents = PointerEvents::None,
					 .start = {19.0F, 9.0F},
					 .end = {9.0F, 19.0F},
					 .lineWidth = 1.5F,
					 .roundCaps = true}
				);
			}

			void activate() override {
				for (Component* node = parent().get(); node != nullptr; node = node->parent().get()) {
					if (auto* dialog = dynamic_cast<Dialog*>(node)) {
						dialog->close();
						return;
					}
				}
			}
		};

	} // namespace

	Dialog::Dialog(const Args& args)
		: title(args.title),
		  kicker(args.kicker),
		  panelSize(args.panelSize),
		  modal(args.modal),
		  hasFooter(args.footer),
		  closeOnScrim(args.closeOnScrim) {}

	Ref<Dialog> Dialog::open(Component& opener, const Args& args, int player) {
		Ref<Dialog> dialog = opener.openOverlay<Dialog>(
			args,
			{.kind = args.modal ? OverlayKind::Dialog : OverlayKind::Panel,
			 .player = player,
			 .placement = Absolute{.cover = true},
			 .onDismiss = args.onClose}
		);
		if (Dialog* shown = dialog.get()) {
			shown->player = player;
			shown->appear = fadeIn(*shown);
			if (shown->modal) {
				shown->requestUpdate(); // the scope, once the opener has filled it
			}
		}
		return dialog;
	}

	StyleId Dialog::themeStyle() const {
		return modal ? styles::scrim.theme : StyleId{};
	}

	PointerEvents Dialog::defaultPointerEvents() const {
		return modal ? PointerEvents::Auto : PointerEvents::Passthrough;
	}

	void Dialog::onMount() {
		Layout centre = getLayout();
		centre.display = Display::Flex;
		centre.flexDirection = FlexDirection::Column;
		centre.alignItems = AlignItems::Center;
		centre.justifyContent = JustifyContent::Center;
		setLayout(centre);
		const Theme& theme = getContext()->getTheme();
		box = addChild<Rectangle>(
			{.size = panelSize, .layout = {.display = Display::Flex, .flexDirection = FlexDirection::Column}, .style = styles::overlay}
		);
		Ref<Component> header = box.addChild<Component>(
			{.layout = {
				 .display = Display::Flex,
				 .flexDirection = FlexDirection::Column,
				 .gap = 2.0F,
				 .padding = {kPad, kPad + kClose, 12.0F, kPad}
			 }}
		);
		if (!kicker.empty()) {
			header.addChild<Text>(
				{.style = styles::text::caption.extend({.textTransform = Foundation::TextTransform::Uppercase}), .text = kicker}
			);
		}
		titleText = header.addChild<Text>({.style = styles::text::title, .text = title});
		box.addChild<Divider>({});
		body = box.addChild<Component>(
			{.layout = {.display = Display::Flex, .flexDirection = FlexDirection::Column, .gap = 8.0F, .padding = kPad, .flexGrow = 1.0F}}
		);
		if (hasFooter) {
			box.addChild<Divider>({});
			actions = box.addChild<Component>(
				{.layout = {
					 .display = Display::Flex,
					 .gap = 8.0F,
					 .padding = {12.0F, kPad},
					 .alignItems = AlignItems::Center,
					 .justifyContent = JustifyContent::End,
				 }}
			);
		}
		// Last in tree order, so Tab reaches the content first. It sits over the header, whose padding
		// leaves it room, and its zIndex says so.
		box.addChild<DialogClose>({
			.size = {kClose, kClose},
			.layout = {.position = Position::Absolute, .inset = {.top = kCloseInset, .right = kCloseInset}},
			.style = {.backgroundColor = Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), .borderRadius = theme.get(tokens::radius::sm)},
			.zIndex = 1,
		});
	}

	void Dialog::update(float /*dt*/) {
		if (modal && !scoped) {
			scoped = true;
			getContext()->pushScope(self(), player);
		}
	}

	void Dialog::handlePointer(PointerEvent& event) {
		if (event.type == PointerEvent::Type::Down && event.target == this && modal && closeOnScrim) {
			close();
			event.consume();
		}
	}

	void Dialog::setTitle(std::string value) {
		title = std::move(value);
		if (Text* shown = titleText.get()) {
			shown->setText(title);
		}
	}

	void Dialog::close() {
		if (isOverlay()) {
			getContext()->dismissOverlay(self());
		}
	}

} // namespace ui
