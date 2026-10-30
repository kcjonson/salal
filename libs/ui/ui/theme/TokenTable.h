#pragma once

// What salal_tokens() generates against (docs/ui-design.md, "Theme styles"): one table per tokens
// folder, holding its token paths and types, its styles and its documents, and the typed handles
// code names (`ui::styles::pane`, `ui::tokens::color::text::primary`), each an index into the table.
// The theme resolves the documents at run time; the handles find their values in it.

#include "ui/theme/dtcg/Values.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace ui {

	struct TokenEntry {
		std::string_view path;
		dtcg::TokenType	 type;
	};

	// A tokens or resolver file compiled in: its name relative to its folder and its text in pieces,
	// since MSVC caps the length of one string literal.
	struct EmbeddedFile {
		std::string_view				  name;
		std::span<const std::string_view> pieces;
	};

	struct TokenTable {
		std::string_view				  name; // the namespace the table was generated into
		std::span<const TokenEntry>		  tokens;
		std::span<const std::string_view> styles; // each style's group path, `styles.pane`
		std::span<const EmbeddedFile>	  files;
		std::string_view				  entry; // the resolver file; empty merges every file in name order
		const TokenTable*				  base;	 // ui's table, under a game's; null for ui's own
	};

	struct StyleId {
		const TokenTable* table = nullptr;
		std::uint32_t	  index = 0;
	};

	template <dtcg::TokenType kType>
	struct TokenRef {
		const TokenTable* table = nullptr;
		std::uint32_t	  index = 0;
	};

	using ColorToken = TokenRef<dtcg::TokenType::Color>;
	using DimensionToken = TokenRef<dtcg::TokenType::Dimension>;
	using FontFamilyToken = TokenRef<dtcg::TokenType::FontFamily>;
	using FontWeightToken = TokenRef<dtcg::TokenType::FontWeight>;
	using DurationToken = TokenRef<dtcg::TokenType::Duration>;
	using CubicBezierToken = TokenRef<dtcg::TokenType::CubicBezier>;
	using NumberToken = TokenRef<dtcg::TokenType::Number>;
	using StrokeStyleToken = TokenRef<dtcg::TokenType::StrokeStyle>;
	using BorderToken = TokenRef<dtcg::TokenType::Border>;
	using TransitionToken = TokenRef<dtcg::TokenType::Transition>;
	using ShadowToken = TokenRef<dtcg::TokenType::Shadow>;
	using GradientToken = TokenRef<dtcg::TokenType::Gradient>;
	using TypographyToken = TokenRef<dtcg::TokenType::Typography>;

} // namespace ui
