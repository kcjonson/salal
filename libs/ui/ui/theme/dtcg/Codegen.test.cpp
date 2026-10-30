#include "ui/theme/dtcg/Codegen.h"
#include "ui/theme/dtcg/Resolver.h"

#include <gtest/gtest.h>

using namespace ui::dtcg;

namespace {

	ResolvedTokens resolveText(const std::string& text, std::vector<Diagnostic>& diagnostics) {
		TokenSet set;
		set.merge(parseJson(text, "t.tokens.json", diagnostics), "t.tokens.json", diagnostics);
		return resolve(set, {}, diagnostics);
	}

	bool mentions(const std::vector<Diagnostic>& diagnostics, std::string_view path, std::string_view words) {
		return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diagnostic& d) {
			return d.path == path && d.message.find(words) != std::string::npos;
		});
	}

} // namespace

TEST(TokensCodegen, NamesBecomeIdentifiers) {
	EXPECT_EQ(identifierFor("surface"), "surface");
	EXPECT_EQ(identifierFor("gray-100"), "gray100");
	EXPECT_EQ(identifierFor("on surface"), "onSurface");
	EXPECT_EQ(identifierFor("12"), "_12");
	EXPECT_EQ(identifierFor("default"), "default_");
	EXPECT_EQ(identifierFor("$root"), "root");
	EXPECT_EQ(identifierFor("50%"), std::nullopt);
}

TEST(TokensCodegen, StylesAreGroupsOfStyleFields) {
	std::vector<Diagnostic> diagnostics;
	const ResolvedTokens	resolved = resolveText(
		   R"({
		"space": { "$type": "dimension", "md": { "$value": { "value": 12, "unit": "px" } } },
		"styles": {
			"pane": { "borderRadius": { "$value": "{space.md}" } },
			"hud": { "health": { "backgroundColor": { "$type": "color", "$value": { "colorSpace": "srgb", "components": [1, 0, 0] } } } }
		}
	})",
		   diagnostics
	   );
	const Catalog catalogue = catalog(resolved, {}, diagnostics);
	ASSERT_FALSE(hasErrors(diagnostics));
	ASSERT_EQ(catalogue.styles.size(), 2U);
	EXPECT_EQ(catalogue.styles[0].path, "styles.hud.health");
	EXPECT_EQ(catalogue.styles[1].path, "styles.pane");
	ASSERT_EQ(catalogue.tokens.size(), 1U); // a style's fields aren't tokens of their own
	EXPECT_EQ(catalogue.tokens[0].path, "space.md");

	const CodegenOutput out = generate(catalogue, {.nameSpace = "game", .header = "Tokens.h", .origin = "tokens"}, diagnostics);
	EXPECT_NE(
		out.header.find("inline constexpr ::ui::StyleId pane{.table = &::game::tokens_detail::kTable, .index = 1};"), std::string::npos
	);
	EXPECT_NE(out.header.find("namespace hud {"), std::string::npos);
	EXPECT_NE(out.header.find("inline constexpr ::ui::DimensionToken md{"), std::string::npos);
	EXPECT_NE(out.source.find(".base = &::ui::tokens_detail::kTable"), std::string::npos);
}

TEST(TokensCodegen, StyleRulesAreChecked) {
	std::vector<Diagnostic> diagnostics;
	const ResolvedTokens	resolved = resolveText(
		   R"({
		"styles": {
			"$type": "color",
			"a": { "backgroundColour": { "$value": { "colorSpace": "srgb", "components": [1, 0, 0] } } },
			"b": { "borderWidth": { "$value": { "colorSpace": "srgb", "components": [1, 0, 0] } } },
			"c": {
				"color": { "$value": { "colorSpace": "srgb", "components": [1, 0, 0] } },
				"hover": { "color": { "$value": { "colorSpace": "srgb", "components": [1, 1, 0] } } }
			},
			"loose": { "$value": { "colorSpace": "srgb", "components": [1, 0, 0] } }
		}
	})",
		   diagnostics
	   );
	catalog(resolved, {}, diagnostics);
	EXPECT_TRUE(mentions(diagnostics, "styles.a.backgroundColour", "is not a style field"));
	EXPECT_TRUE(mentions(diagnostics, "styles.b.borderWidth", "a style's borderWidth is a dimension"));
	EXPECT_TRUE(mentions(diagnostics, "styles.c", "a style holds no groups"));
	EXPECT_TRUE(mentions(diagnostics, "styles.loose", "directly under styles"));
}

TEST(TokensCodegen, InheritedPathsStayInTheirOwnTable) {
	std::vector<Diagnostic> diagnostics;
	const ResolvedTokens	resolved = resolveText(
		   R"({ "$type": "number", "ui": { "$value": 1 }, "mine": { "$value": 2 }, "styles": { "pane": { "opacity": { "$value": 0.5 } } } })",
		   diagnostics
	   );
	const Catalog catalogue = catalog(resolved, {"ui", "styles.pane.opacity"}, diagnostics);
	ASSERT_EQ(catalogue.tokens.size(), 1U);
	EXPECT_EQ(catalogue.tokens[0].path, "mine");
	EXPECT_TRUE(catalogue.styles.empty()); // a game overriding ui's pane doesn't get a pane of its own
}

TEST(TokensCodegen, NamesThatCollideFail) {
	std::vector<Diagnostic> diagnostics;
	const ResolvedTokens	resolved =
		resolveText(R"({ "$type": "number", "gray-100": { "$value": 1 }, "gray100": { "$value": 2 } })", diagnostics);
	generate(catalog(resolved, {}, diagnostics), {.nameSpace = "game"}, diagnostics);
	EXPECT_TRUE(mentions(diagnostics, "gray100", "gets the same C++ name as gray-100"));
}
