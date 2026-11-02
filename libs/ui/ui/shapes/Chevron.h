#pragma once

// A caret: two strokes meeting at a point in a 10 px box, pointing down, up, right or left, in
// style.color (the theme's secondary text unset). Lines, so ui needs no icon atlas of its own; they
// are decoration, and the box takes the pointer, as a leaf shape's does.

#include "ui/Component.h"
#include "ui/shapes/Line.h"

#include <cstdint>

namespace ui {

	class Chevron : public Component {
	  public:
		enum class Pointing : std::uint8_t { Down, Up, Right, Left };

		struct Args {
			UI_COMMON_ARGS
			Pointing pointing = Pointing::Down;
		};

		explicit Chevron(const Args& args);

		[[nodiscard]] Pointing getPointing() const { return pointing; }
		void				   setPointing(Pointing value);
		void				   setSize(const Size& /*value*/) override {}
		void				   setStyle(const Style& declared) override;

		[[nodiscard]] const char*	typeName() const override { return "Chevron"; }
		[[nodiscard]] PointerEvents defaultPointerEvents() const override { return PointerEvents::Auto; }

	  protected:
		void onMount() override;

	  private:
		void show();

		Pointing  pointing;
		Ref<Line> first;
		Ref<Line> second;
	};

} // namespace ui
