#include "ui/components/Toast.h"

#include "ui/Context.h"
#include "ui/components/Control.h"
#include "ui/components/FadeIn.h"
#include "ui/shapes/Icon.h"
#include "ui/shapes/Line.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/Variants.h"

#include <algorithm>
#include <string>

namespace ui {

	namespace {

		constexpr float kPad = 12.0F;
		constexpr float kGap = 10.0F;
		constexpr float kStripe = 3.0F;
		constexpr float kIcon = 20.0F;
		constexpr float kClose = 20.0F;

		Tone toneOf(ToastSeverity severity) {
			switch (severity) {
				case ToastSeverity::Warning:
					return Tone::Warning;
				case ToastSeverity::Critical:
					return Tone::Danger;
				case ToastSeverity::Info:
					break;
			}
			return Tone::Info;
		}

		// An X of two strokes; not a Tab stop, so toasts never take keyboard focus.
		class ToastClose : public Control {
		  public:
			struct Args {
				UI_COMMON_ARGS
			};

			[[nodiscard]] const char* typeName() const override { return "ToastClose"; }

		  protected:
			void onMount() override {
				Control::onMount();
				setFocusable(false);
				const Style stroke{.color = getContext()->getTheme().get(tokens::color::text::secondary)};
				addChild<Line>(
					{.style = stroke,
					 .pointerEvents = PointerEvents::None,
					 .start = {6.0F, 6.0F},
					 .end = {14.0F, 14.0F},
					 .lineWidth = 1.5F,
					 .roundCaps = true}
				);
				addChild<Line>(
					{.style = stroke,
					 .pointerEvents = PointerEvents::None,
					 .start = {14.0F, 6.0F},
					 .end = {6.0F, 14.0F},
					 .lineWidth = 1.5F,
					 .roundCaps = true}
				);
			}

			void activate() override {
				if (auto* toast = dynamic_cast<Toast*>(parent().get())) {
					toast->dismiss();
				}
			}
		};

	} // namespace

	// --- Toast ---

	Toast::Toast(const Args& args)
		: title(args.title),
		  message(args.message),
		  severity(args.severity),
		  autoDismissMs(args.autoDismissMs),
		  iconName(args.icon),
		  onPress(args.onPress),
		  onDismiss(args.onDismiss) {}

	StyleId Toast::themeStyle() const {
		return styles::toast.theme;
	}

	void Toast::onMount() {
		const Theme&	 theme = getContext()->getTheme();
		const ToneColors tone = toneColors(theme, toneOf(severity));
		Layout			 row = getLayout();
		row.display = Display::Flex;
		row.flexDirection = FlexDirection::Row;
		row.alignItems = AlignItems::Start;
		row.gap = kGap;
		row.padding = {kPad, kPad, kPad, kPad + kStripe};
		setLayout(row);
		addChild<Rectangle>({
			.size = {kStripe, kAuto},
			.layout = {.position = Position::Absolute, .inset = {.top = 0.0F, .bottom = 0.0F, .left = 0.0F}},
			.style = {.backgroundColor = tone.color},
			.pointerEvents = PointerEvents::None,
		});
		if (!iconName.empty()) {
			addChild<Icon>({.size = {kIcon, kIcon}, .style = {.color = tone.color}, .name = iconName});
		}
		Ref<Component> words = addChild<Component>(
			{.layout = {.display = Display::Flex, .flexDirection = FlexDirection::Column, .gap = 4.0F, .flexGrow = 1.0F}}
		);
		words.addChild<Text>({.style = styles::text::body.extend({.color = tone.color}), .text = title});
		if (!message.empty()) {
			words.addChild<Text>({.style = styles::text::secondary, .text = message, .wrap = true});
		}
		addChild<ToastClose>({.size = {kClose, kClose}, .style = {.backgroundColor = Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F)}});
		if (autoDismissMs > 0.0) {
			after(autoDismissMs, [this] { dismiss(); });
		}
		appear = fadeIn(*this);
	}

	void Toast::handlePointer(PointerEvent& event) {
		if (event.type != PointerEvent::Type::Click || event.button != foundation::MouseButton::Left) {
			return;
		}
		event.consume();
		if (onPress) {
			onPress();
		}
		dismiss();
	}

	void Toast::dismiss() {
		Component* owner = parent().get();
		if (owner == nullptr) {
			return; // gone already
		}
		const std::function<void()> heard = onDismiss;
		if (auto* stack = dynamic_cast<ToastStack*>(owner)) {
			stack->removed(*this);
		} else {
			owner->removeChild(self());
		}
		if (heard) {
			heard();
		}
	}

	// --- ToastStack ---

	ToastStack::ToastStack(const Args& args)
		: anchor(args.anchor),
		  spacing(args.spacing),
		  inset(args.inset),
		  maxToasts(std::max<std::size_t>(args.maxToasts, 1)),
		  toastWidth(args.toastWidth) {}

	Ref<ToastStack> ToastStack::open(Component& opener, const Args& args) {
		return opener.openOverlay<ToastStack>(args, {.kind = OverlayKind::Toast, .placement = Absolute{.cover = true}});
	}

	void ToastStack::onMount() {
		const bool top = anchor == ToastAnchor::TopRight || anchor == ToastAnchor::TopLeft;
		const bool right = anchor == ToastAnchor::TopRight || anchor == ToastAnchor::BottomRight;
		setLayout({
			.display = Display::Flex,
			.flexDirection = FlexDirection::Column,
			.gap = spacing,
			.padding = inset,
			.alignItems = right ? AlignItems::End : AlignItems::Start,
			.justifyContent = top ? JustifyContent::Start : JustifyContent::End,
		});
	}

	Ref<Toast> ToastStack::add(Toast::Args args) {
		if (args.size.width.isAuto()) {
			args.size = {toastWidth, args.size.height};
		}
		while (entries.size() >= maxToasts) {
			if (Toast* oldest = entries.front().toast.get()) {
				oldest->dismiss();
			} else {
				entries.erase(entries.begin());
			}
		}
		entries.push_back({.key = "toast" + std::to_string(nextKey++), .args = std::move(args)});
		arrange();
		if (isOverlay()) {
			getContext()->bringToFront(self());
		}
		return entries.back().toast;
	}

	void ToastStack::dismissAll() {
		for (const Ref<Toast>& toast : toasts()) {
			if (Toast* shown = toast.get()) {
				shown->dismiss();
			}
		}
	}

	std::vector<Ref<Toast>> ToastStack::toasts() const {
		std::vector<Ref<Toast>> out;
		out.reserve(entries.size());
		for (const Entry& entry : entries) {
			out.push_back(entry.toast);
		}
		return out;
	}

	void ToastStack::removed(const Toast& toast) {
		std::erase_if(entries, [&](const Entry& entry) { return entry.toast.get() == &toast; });
		arrange();
	}

	// Newest nearest the corner: last in the column at a bottom corner, first at a top one.
	void ToastStack::arrange() {
		std::vector<std::size_t> order(entries.size());
		for (std::size_t i = 0; i < order.size(); ++i) {
			order[i] = i;
		}
		if (anchor == ToastAnchor::TopRight || anchor == ToastAnchor::TopLeft) {
			std::ranges::reverse(order);
		}
		reconcileChildren(
			order,
			[&](std::size_t i) { return entries[i].key; },
			[&](std::size_t i) -> Ref<Component> {
				entries[i].toast = addChild<Toast>(entries[i].args);
				return entries[i].toast;
			},
			[](Component& /*existing*/, std::size_t /*i*/) {}
		);
	}

} // namespace ui
