#include "ui/components/Select.h"

#include "ui/Context.h"
#include "ui/detail/Snapshot.h"
#include "ui/theme/Tokens.h"

namespace ui {

	namespace {
		constexpr float kPadX = 10.0F;
		constexpr float kGap = 8.0F;
		constexpr float kWidth = 160.0F;
	} // namespace

	Select::Select(const Args& args)
		: options(args.options),
		  value(args.value),
		  placeholder(args.placeholder),
		  controlSize(args.controlSize),
		  onChange(args.onChange) {}

	void Select::describe(nlohmann::json& node) const {
		Control::describe(node);
		node["state"]["open"] = isOpen();
		node["value"] = value;
	}

	StyleId Select::themeStyle() const {
		return styles::select.theme;
	}

	void Select::onMount() {
		Control::onMount();
		const SizeMetrics metrics = sizeMetrics(getContext()->getTheme(), controlSize);
		Layout			  row = getLayout();
		row.display = Display::Flex;
		row.flexDirection = FlexDirection::Row;
		row.alignItems = AlignItems::Center;
		row.gap = kGap;
		row.padding = {0.0F, kPadX};
		setLayout(row);
		setSize(
			{getSizeSpec().width.isAuto() ? Length(kWidth) : getSizeSpec().width,
			 getSizeSpec().height.isAuto() ? Length(metrics.controlHeight) : getSizeSpec().height}
		);
		text = addChild<Text>({.layout = {.flexGrow = 1.0F}});
		caret = addChild<Chevron>({});
		show();
	}

	void Select::setStyle(const Style& declared) {
		Control::setStyle(declared);
		show();
	}

	void Select::setValue(const std::string& newValue) {
		value = newValue;
		show();
	}

	void Select::setOptions(std::vector<SelectOption> value) {
		options = std::move(value);
		if (isOpen()) {
			getContext()->closeOverlay(menu);
		}
		show();
	}

	std::string Select::getSelectedLabel() const {
		for (const SelectOption& option : options) {
			if (option.value == value) {
				return option.label;
			}
		}
		return placeholder;
	}

	void Select::show() {
		Text* shown = text.get();
		if (shown == nullptr) {
			return;
		}
		const bool chosen = std::ranges::any_of(options, [&](const SelectOption& option) { return option.value == value; });
		shown->setText(getSelectedLabel());
		const Theme& theme = getContext()->getTheme();
		shown->setStyle({
			.color = chosen ? getStyle().color : std::optional(theme.get(tokens::color::text::secondary)),
			.fontFamily = getStyle().fontFamily,
			.fontSize = sizeMetrics(theme, controlSize).fontSize,
			.letterSpacing = getStyle().letterSpacing,
			.textTransform = getStyle().textTransform,
		});
	}

	void Select::activate() {
		if (isOpen()) {
			getContext()->closeOverlay(menu);
			return;
		}
		if (options.empty()) {
			return;
		}
		std::vector<MenuItem> items;
		items.reserve(options.size());
		for (const SelectOption& option : options) {
			items.push_back(
				{.label = option.label, .onSelect = [this, chosen = option.value] { choose(chosen); }, .selected = option.value == value}
			);
		}
		menu = Menu::open(*this, {.size = {getSize().x, kAuto}, .items = std::move(items)}, Anchored{}, activatingPlayer());
	}

	void Select::choose(const std::string& chosen) {
		const bool changed = chosen != value;
		value = chosen;
		show();
		if (changed && onChange) {
			onChange(value);
		}
	}

} // namespace ui
