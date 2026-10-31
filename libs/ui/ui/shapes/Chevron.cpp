#include "ui/shapes/Chevron.h"

#include "ui/Context.h"
#include "ui/theme/Tokens.h"

namespace ui {

	namespace {
		constexpr float kBox = 10.0F;
		constexpr float kStroke = 1.5F;
	} // namespace

	Chevron::Chevron(const Args& args)
		: pointing(args.pointing) {}

	void Chevron::onMount() {
		Component::setSize(Size(Foundation::Vec2{kBox, kBox}));
		first = addChild<Line>({.lineWidth = kStroke, .roundCaps = true});
		second = addChild<Line>({.lineWidth = kStroke, .roundCaps = true});
		show();
	}

	void Chevron::setPointing(Pointing value) {
		pointing = value;
		show();
	}

	void Chevron::setStyle(const Style& declared) {
		Component::setStyle(declared);
		show();
	}

	void Chevron::show() {
		Line* a = first.get();
		Line* b = second.get();
		if (a == nullptr || b == nullptr) {
			return;
		}
		const Style stroke{.color = getStyle().color.value_or(getContext()->getTheme().get(tokens::color::text::secondary))};
		a->setStyle(stroke);
		b->setStyle(stroke);
		switch (pointing) {
			case Pointing::Down:
				a->setEndpoints({1.0F, 3.0F}, {5.0F, 7.0F});
				b->setEndpoints({5.0F, 7.0F}, {9.0F, 3.0F});
				break;
			case Pointing::Up:
				a->setEndpoints({1.0F, 7.0F}, {5.0F, 3.0F});
				b->setEndpoints({5.0F, 3.0F}, {9.0F, 7.0F});
				break;
			case Pointing::Right:
				a->setEndpoints({3.0F, 1.0F}, {7.0F, 5.0F});
				b->setEndpoints({7.0F, 5.0F}, {3.0F, 9.0F});
				break;
			case Pointing::Left:
				a->setEndpoints({7.0F, 1.0F}, {3.0F, 5.0F});
				b->setEndpoints({3.0F, 5.0F}, {7.0F, 9.0F});
				break;
		}
	}

} // namespace ui
