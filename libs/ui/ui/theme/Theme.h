#pragma once

// The typed theme a scene's root carries (docs/ui-design.md, "Theme"): ui's tokens with a game's
// over them, resolved for one mode at load, every generated style and token looked up once. A
// component never names it; its styles and its type's defaults come from it through the context.
//
// The load's rules: a wrong $type, a broken alias or a bad value fails it; a token no table names
// is a warning; a token a table names that the loaded files lack is an error in a development
// build and a warning otherwise, and reads as the type's default. A path ui names and a game's
// file leaves out falls back to ui's value, since ui's tokens are always the bottom layer. A ui
// style a game's layer sets a field of is an info naming the fields, since a game style sharing
// ui's name merges into ui's.

#include "ui/Style.h"
#include "ui/theme/TokenTable.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ui {

	// The name a tokens file's fontFamily uses for one of the game's families (FontRenderer::Initialize),
	// and that family's weight. Several families under one name are its faces: a display role with a
	// semibold and a bold atlas registers both, and a style draws in the face nearest its fontWeight.
	struct FontFamilyName {
		std::string_view	 name;
		Renderer::FontFamily family;
		float				 weight = 400.0F;
	};

	// Which of `weights` CSS's font matching picks for `desired` (CSS Fonts 4, 5.2): from 400 to 500,
	// the desired weight up to 500, then lighter ones heaviest first, then heavier past 500; under 400,
	// lighter or equal heaviest first, then heavier; over 500, heavier or equal lightest first, then
	// lighter. An index into `weights`; 0 when it's empty.
	std::size_t matchWeight(std::span<const float> weights, float desired);

	struct Typography {
		Renderer::FontFamily family = Renderer::FontFamily::Default;
		float				 fontSize = 16.0F;
		float				 fontWeight = 400.0F;
		float				 letterSpacing = 0.0F;
		float				 lineHeight = 1.2F; // a multiple of fontSize
	};

#ifdef DEVELOPMENT_BUILD
	inline constexpr bool kStrictThemeByDefault = true;
#else
	inline constexpr bool kStrictThemeByDefault = false;
#endif

	struct ThemeArgs {
		const TokenTable*				tokens = nullptr; // a game's tokenTable(), loaded over ui's; null loads ui's alone
		dtcg::ResolverInputs			inputs;			  // the mode: a context for each resolver modifier, e.g. {"theme", "light"}
		std::span<const FontFamilyName> fontFamilies;
		float							rootFontSize = 16.0F;			// px a rem is
		bool							strict = kStrictThemeByDefault; // a token missing from the files is an error, not a warning
	};

	struct ThemeLoad;

	class Theme {
	  public:
		// Every lookup answers its type's default until a load fills it.
		Theme() = default;

		static ThemeLoad load(const ThemeArgs& args);

		Foundation::Color	   get(ColorToken token) const;
		float				   get(DimensionToken token) const; // px
		float				   get(DurationToken token) const;	// ms
		float				   get(NumberToken token) const;
		float				   get(FontWeightToken token) const;
		Renderer::FontFamily   get(FontFamilyToken token) const;
		Typography			   get(TypographyToken token) const;
		dtcg::CubicBezierValue get(CubicBezierToken token) const;
		dtcg::ShadowValue	   get(ShadowToken token) const;
		dtcg::BorderValue	   get(BorderToken token) const;
		dtcg::TransitionValue  get(TransitionToken token) const;
		dtcg::GradientValue	   get(GradientToken token) const;
		dtcg::StrokeStyleValue get(StrokeStyleToken token) const;

		// A theme style's fields as its tokens set them, its `font` filling the font fields its own
		// don't, and its text transform from salal's extension on its group; all unset for a style of
		// a table the theme didn't load.
		const Style& style(StyleId id) const;

		// `declared` as a component of a type whose theme style is `typeStyle` draws it: that style,
		// then the one `declared` extends, then `declared`'s own set fields, its fontFamily the face of
		// that family's name nearest its fontWeight.
		Style resolve(const Style& declared, StyleId typeStyle) const;

		// The face of `family`'s name nearest `weight` (matchWeight); `family` itself when the game
		// registered it under no name, or no other face under its name.
		Renderer::FontFamily face(Renderer::FontFamily family, float weight) const;

	  private:
		struct Slot {
			std::optional<dtcg::Value> value;
			Renderer::FontFamily	   family = Renderer::FontFamily::Default; // a fontFamily's or typography's, mapped at load
		};

		struct Table {
			const TokenTable*  table = nullptr;
			std::vector<Slot>  tokens;
			std::vector<Style> styles;
		};

		// A family the game registered: which of the registered names it's under, and its weight.
		struct Face {
			std::size_t			 name = 0;
			Renderer::FontFamily family = Renderer::FontFamily::Default;
			float				 weight = 400.0F;
		};

		const Slot* slot(const TokenTable* table, std::uint32_t index) const;

		template <typename T>
		T valueOr(const TokenTable* table, std::uint32_t index, T fallback) const;

		std::vector<Table> tables;
		std::vector<Face>  faces;
	};

	struct ThemeLoad {
		Theme						  theme;
		std::vector<dtcg::Diagnostic> diagnostics;

		bool ok() const { return !dtcg::hasErrors(diagnostics); }
	};

} // namespace ui
