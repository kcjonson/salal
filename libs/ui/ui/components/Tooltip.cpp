#include "ui/components/Tooltip.h"

#include "ui/Context.h"
#include "ui/components/FadeIn.h"
#include "ui/components/KeyCap.h"
#include "ui/shapes/Text.h"
#include "ui/theme/Tokens.h"

namespace ui {

	namespace {
		constexpr float kPad = 8.0F;
		constexpr float kGap = 4.0F;
		constexpr float kPointerGap = 16.0F;
	} // namespace

	Tooltip::Tooltip(const Args& args)
		: text(args.text),
		  maxWidth(args.maxWidth) {}

	Ref<Tooltip> Tooltip::open(Component& owner, Foundation::Vec2 at, const Args& args, const std::function<void(Tooltip&)>& build) {
		Ref<Tooltip> tip = owner.openOverlay<Tooltip>(
			args,
			{.kind = OverlayKind::Tooltip,
			 .placement = Anchored{.side = Side::Below, .align = Align::Start, .gap = kPointerGap, .point = at}}
		);
		if (Tooltip* shown = tip.get()) {
			if (build) {
				build(*shown);
			}
			shown->appear = fadeIn(*shown);
		}
		return tip;
	}

	StyleId Tooltip::themeStyle() const {
		return styles::tooltip.theme;
	}

	void Tooltip::onMount() {
		Layout column = getLayout();
		column.display = Display::Flex;
		column.flexDirection = FlexDirection::Column;
		column.alignItems = AlignItems::Start;
		column.gap = kGap;
		column.padding = kPad;
		setLayout(column);
		setMaxSize({maxWidth, kAuto});
		if (!text.title.empty()) {
			addChild<Text>({.style = styles::text::body, .text = text.title, .wrap = true});
		}
		if (!text.description.empty()) {
			addChild<Text>({.style = styles::text::caption, .text = text.description, .wrap = true});
		}
		if (!text.hotkey.empty()) {
			addChild<KeyCap>({.label = text.hotkey});
		}
	}

	Component::TooltipOpener tooltip(TooltipText text) {
		return [text = std::move(text)](Component& owner, Foundation::Vec2 at) -> Ref<Component> {
			return Tooltip::open(owner, at, {.text = text});
		};
	}

	Component::TooltipOpener tooltip(std::function<void(Tooltip&)> build) {
		return [build = std::move(build)](Component& owner, Foundation::Vec2 at) -> Ref<Component> {
			return Tooltip::open(owner, at, {}, build);
		};
	}

} // namespace ui
