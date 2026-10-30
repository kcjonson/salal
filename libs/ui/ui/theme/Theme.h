#pragma once

// The typed theme a scene's root carries (docs/ui-design.md, "Theme"): ui's tokens with a game's
// over them, resolved for one mode at load, every generated style and token looked up once. A
// component never names it; its styles and its type's defaults come from it through the context.
//
// The load's rules: a wrong $type, a broken alias or a bad value fails it; a token no table names
// is a warning; a token a table names that the loaded files lack is an error in a development
// build and a warning otherwise, and reads as the type's default. A path ui names and a game's
// file leaves out falls back to ui's value, since ui's tokens are always the bottom layer.

#include "ui/theme/TokenTable.h"

#include <graphics/Color.h>
#include <graphics/PrimitiveStyles.h>
#include <primitives/FontFamily.h>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ui {

	// The name a tokens file's fontFamily uses for one of the game's families (FontRenderer::Initialize).
	struct FontFamilyName {
		std::string_view	 name;
		Renderer::FontFamily family;
	};

	struct Typography {
		Renderer::FontFamily family = Renderer::FontFamily::Default;
		float				 fontSize = 16.0F;
		float				 fontWeight = 400.0F;
		float				 letterSpacing = 0.0F;
		float				 lineHeight = 1.2F; // a multiple of fontSize
	};

	// A style's fields as the theme resolved them; unset where its tokens don't set them. A style's
	// `font` fills the font fields its own font fields don't.
	struct ThemeStyle {
		std::optional<Foundation::Color>	 backgroundColor;
		std::optional<Foundation::Color>	 borderColor;
		std::optional<float>				 borderWidth;
		std::optional<float>				 borderRadius;
		std::optional<Foundation::BoxShadow> boxShadow;
		std::optional<float>				 opacity;
		std::optional<Foundation::Color>	 color;
		std::optional<Renderer::FontFamily>	 fontFamily;
		std::optional<float>				 fontSize;
		std::optional<float>				 fontWeight;
		std::optional<float>				 lineHeight;
		std::optional<float>				 letterSpacing;
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

		const ThemeStyle& style(StyleId id) const;

	  private:
		struct Slot {
			std::optional<dtcg::Value> value;
			Renderer::FontFamily	   family = Renderer::FontFamily::Default; // a fontFamily's or typography's, mapped at load
		};

		struct Table {
			const TokenTable*		table = nullptr;
			std::vector<Slot>		tokens;
			std::vector<ThemeStyle> styles;
		};

		const Slot* slot(const TokenTable* table, std::uint32_t index) const;

		template <typename T>
		T valueOr(const TokenTable* table, std::uint32_t index, T fallback) const;

		std::vector<Table> tables;
	};

	struct ThemeLoad {
		Theme						  theme;
		std::vector<dtcg::Diagnostic> diagnostics;

		bool ok() const { return !dtcg::hasErrors(diagnostics); }
	};

} // namespace ui
