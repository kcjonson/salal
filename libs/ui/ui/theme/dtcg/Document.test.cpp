#include "ui/theme/dtcg/Document.h"
#include "ui/theme/dtcg/Resolver.h"

#include <gtest/gtest.h>

using namespace ui::dtcg;

namespace {

	using Files = std::map<std::string, std::string>;

	struct Loaded {
		ResolvedTokens			tokens;
		SalalExtensions			extensions;
		std::vector<Diagnostic> diagnostics;

		bool hasError(std::string_view path, std::string_view words) const {
			return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diagnostic& d) {
				return d.severity == Diagnostic::Severity::Error && d.path == path && d.message.find(words) != std::string::npos;
			});
		}

		std::string all() const {
			std::string text;
			for (const Diagnostic& d : diagnostics) {
				text += d.text() + "\n";
			}
			return text;
		}

		ColorValue color(const std::string& path) const { return std::get<ColorValue>(tokens.at(path).value); }
		float	   number(const std::string& path) const { return std::get<float>(tokens.at(path).value); }
	};

	// `files` as one layer: through `entry` if it's a resolver, else every file in name order.
	Loaded load(const Files& files, const std::string& entry = "", const ResolverInputs& inputs = {}) {
		Layer layer{.label = "", .entry = entry};
		for (const auto& [name, text] : files) {
			if (!name.ends_with(".resolver.json")) {
				layer.files.push_back(name);
			}
		}
		layer.read = [files](const std::string& name) -> std::optional<std::string> {
			const auto found = files.find(name);
			return found == files.end() ? std::nullopt : std::optional(found->second);
		};
		Loaded		loaded;
		const Layer layers[] = {layer};
		loaded.tokens = resolve(loadLayers(layers, inputs, loaded.diagnostics), {}, loaded.diagnostics, &loaded.extensions);
		return loaded;
	}

	constexpr const char* kTiers = R"({
		"base": { "$type": "color", "blue": { "$value": { "colorSpace": "srgb", "components": [0.2, 0.4, 0.8] } } },
		"color": { "accent": { "$value": "{base.blue}" } },
		"button": { "background": { "$value": "{color.accent}" } }
	})";

} // namespace

TEST(DesignTokens, AliasesResolveAcrossTiers) {
	const Loaded loaded = load({{"a.tokens.json", kTiers}});
	ASSERT_TRUE(loaded.diagnostics.empty()) << loaded.all();
	const ColorValue background = loaded.color("button.background");
	EXPECT_FLOAT_EQ(background.r, 0.2F);
	EXPECT_FLOAT_EQ(background.b, 0.8F);
	EXPECT_EQ(loaded.tokens.at("button.background").type, TokenType::Color); // from its alias, two hops down
	EXPECT_EQ(loaded.tokens.at("button.background").file, "a.tokens.json");
}

TEST(DesignTokens, JsonPointerReachesIntoAValue) {
	const Loaded loaded = load(
		{{"a.tokens.json", kTiers},
		 {"b.tokens.json", R"({ "blueness": { "$type": "number", "$value": { "$ref": "#/base/blue/$value/components/2" } } })"}}
	);
	ASSERT_FALSE(hasErrors(loaded.diagnostics)) << loaded.all();
	EXPECT_FLOAT_EQ(loaded.number("blueness"), 0.8F);
}

TEST(DesignTokens, LaterFilesOverrideAndAliasesSeeTheOverride) {
	const Loaded loaded = load(
		{{"a.tokens.json", kTiers},
		 {"b.tokens.json", R"({ "base": { "blue": { "$value": { "colorSpace": "srgb", "components": [0, 0, 1] } } } })"}}
	);
	ASSERT_FALSE(hasErrors(loaded.diagnostics)) << loaded.all();
	EXPECT_FLOAT_EQ(loaded.color("button.background").b, 1.0F);
	EXPECT_EQ(loaded.tokens.at("base.blue").file, "b.tokens.json");
}

TEST(DesignTokens, ResolverModeSelectsItsContextBeforeAliasesResolve) {
	const Files files{
		{"app.resolver.json", R"({
			"version": "2025.10",
			"sets": { "core": { "sources": [{ "$ref": "core.tokens.json" }] } },
			"modifiers": { "theme": { "contexts": { "dark": [], "light": [{ "$ref": "modes/light.tokens.json" }] }, "default": "dark" } },
			"resolutionOrder": [{ "$ref": "#/sets/core" }, { "$ref": "#/modifiers/theme" }]
		})"},
		{"core.tokens.json", kTiers},
		{"modes/light.tokens.json", R"({ "base": { "blue": { "$value": { "colorSpace": "srgb", "components": [0.9, 0.9, 1] } } } })"},
	};
	const Loaded dark = load(files, "app.resolver.json");
	const Loaded light = load(files, "app.resolver.json", {{"theme", "light"}});
	ASSERT_FALSE(hasErrors(dark.diagnostics)) << dark.all();
	ASSERT_FALSE(hasErrors(light.diagnostics)) << light.all();
	EXPECT_FLOAT_EQ(dark.color("button.background").r, 0.2F);
	EXPECT_FLOAT_EQ(light.color("button.background").r, 0.9F);
	EXPECT_EQ(light.tokens.at("base.blue").file, "modes/light.tokens.json");
	EXPECT_EQ(
		permutations(
			layerModifiers({.entry = "app.resolver.json", .read = [&](const std::string& n) { return std::optional(files.at(n)); }})
		).size(),
		2U
	);
}

TEST(DesignTokens, ResolverInputsAreChecked) {
	const Files files{
		{"app.resolver.json", R"({
			"version": "2025.10",
			"modifiers": { "theme": { "contexts": { "dark": [], "light": [] } } },
			"resolutionOrder": [{ "$ref": "#/modifiers/theme" }]
		})"},
	};
	auto has = [](const Loaded& loaded, std::string_view words) {
		return std::any_of(loaded.diagnostics.begin(), loaded.diagnostics.end(), [&](const Diagnostic& d) {
			return d.message.find(words) != std::string::npos;
		});
	};
	EXPECT_TRUE(has(load(files, "app.resolver.json"), "no input and no default"));
	EXPECT_TRUE(has(load(files, "app.resolver.json", {{"theme", "blue"}}), "no context blue"));
	EXPECT_TRUE(has(load(files, "app.resolver.json", {{"theme", "dark"}, {"size", "large"}}), "modifier size, which no resolver has"));
	EXPECT_FALSE(hasErrors(load(files, "app.resolver.json", {{"theme", "light"}}).diagnostics));
}

TEST(DesignTokens, ResolverStructureIsChecked) {
	auto errorsOf = [](const std::string& resolver) {
		return load({{"app.resolver.json", resolver}, {"a.tokens.json", "{}"}}, "app.resolver.json").all();
	};
	EXPECT_NE(errorsOf(R"({ "version": "2024.10", "resolutionOrder": [] })").find("version"), std::string::npos);
	EXPECT_NE(
		errorsOf(R"({ "version": "2025.10", "modifiers": { "m": { "contexts": { "a": [], "b": [] }, "default": "a" } },
			"sets": { "s": { "sources": [{ "$ref": "#/modifiers/m" }] } }, "resolutionOrder": [{ "$ref": "#/sets/s" }] })")
			.find("only the resolution order may name a modifier"),
		std::string::npos
	);
	EXPECT_NE(
		errorsOf(
			R"({ "version": "2025.10", "sets": { "a": { "sources": [{ "$ref": "#/sets/b" }] }, "b": { "sources": [{ "$ref": "#/sets/a" }] } },
			"resolutionOrder": [{ "$ref": "#/sets/a" }] })"
		)
			.find("includes itself"),
		std::string::npos
	);
	EXPECT_NE(
		errorsOf(R"({ "version": "2025.10", "resolutionOrder": [{ "$ref": "missing.tokens.json" }] })").find("names no set"),
		std::string::npos
	);
}

TEST(DesignTokens, AWrongTypeFailsTheLoad) {
	const Loaded unknown = load({{"a.tokens.json", R"({ "gap": { "$type": "size", "$value": { "value": 4, "unit": "px" } } })"}});
	EXPECT_TRUE(unknown.hasError("gap", "unknown $type \"size\"")) << unknown.all();
	EXPECT_TRUE(unknown.tokens.empty()); // a file with a structural error is left out whole

	const Loaded mismatched = load({{"a.tokens.json", R"({ "gap": { "$type": "dimension", "$value": { "value": 4, "unit": "em" } } })"}});
	EXPECT_TRUE(mismatched.hasError("gap", "unit \"em\" is not px or rem")) << mismatched.all();

	const Loaded aliasMismatch =
		load({{"a.tokens.json", R"({ "a": { "$type": "number", "$value": 2 }, "b": { "$type": "color", "$value": "{a}" } })"}});
	EXPECT_TRUE(aliasMismatch.hasError("b", "is a color but aliases a number")) << aliasMismatch.all();

	const Loaded untyped = load({{"a.tokens.json", R"({ "loose": { "$value": 3 } })"}});
	EXPECT_TRUE(untyped.hasError("loose", "has no type")) << untyped.all();
}

TEST(DesignTokens, BrokenReferencesFail) {
	const Loaded missing = load({{"a.tokens.json", R"({ "a": { "$type": "number", "$value": "{nowhere.at.all}" } })"}});
	EXPECT_TRUE(missing.hasError("a", "references {nowhere.at.all}, which is not a token")) << missing.all();

	const Loaded group = load({{"a.tokens.json", R"({ "g": { "x": { "$type": "number", "$value": 1 } }, "a": { "$value": "{g}" } })"}});
	EXPECT_TRUE(group.hasError("a", "which is not a token")) << group.all();

	const Loaded cycle =
		load({{"a.tokens.json", R"({ "$type": "number", "a": { "$value": "{b}" }, "b": { "$value": "{c}" }, "c": { "$value": "{a}" } })"}});
	EXPECT_TRUE(cycle.tokens.empty());
	EXPECT_TRUE(cycle.hasError("c", "circular reference")) << cycle.all();
	EXPECT_TRUE(cycle.hasError("a", "which has errors")) << cycle.all();

	const Loaded pointer = load(
		{{"a.tokens.json", R"({ "a": { "$type": "number", "$value": { "$ref": "#/b/$type" } }, "b": { "$type": "number", "$value": 1 } })"}}
	);
	EXPECT_TRUE(pointer.hasError("a", "only $value and what it holds")) << pointer.all();
}

TEST(DesignTokens, StructureIsChecked) {
	EXPECT_TRUE(load({{"a.tokens.json", R"({ "a.b": { "$type": "number", "$value": 1 } })"}}).hasError("a.b", "can't contain {, } or ."));
	EXPECT_TRUE(load({{"a.tokens.json", R"({ "a": { "$type": "number", "$value": 1, "child": { "$value": 2 } } })"}})
					.hasError("a", "a token can't hold"));
	const Loaded clash = load(
		{{"a.tokens.json", R"({ "x": { "y": { "$type": "number", "$value": 1 } } })"},
		 {"b.tokens.json", R"({ "x": { "$type": "number", "$value": 2 } })"}}
	);
	EXPECT_TRUE(clash.hasError("x", "is a token here and a group in an earlier file")) << clash.all();
	const Loaded json = load({{"a.tokens.json", "{ \"a\": "}});
	EXPECT_EQ(json.diagnostics.size(), 1U);
	EXPECT_NE(json.all().find("invalid JSON"), std::string::npos);
}

TEST(DesignTokens, OtherColourSpacesNeedTheirHexFallback) {
	const Loaded withHex = load(
		{{"a.tokens.json",
		  R"({ "c": { "$type": "color", "$value": { "colorSpace": "oklch", "components": [0.7, 0.1, 30], "hex": "#ff8000" } } })"}}
	);
	ASSERT_FALSE(hasErrors(withHex.diagnostics)) << withHex.all();
	EXPECT_FLOAT_EQ(withHex.color("c").r, 1.0F);
	EXPECT_NEAR(withHex.color("c").g, 128.0F / 255.0F, 1e-6);

	const Loaded without =
		load({{"a.tokens.json", R"({ "c": { "$type": "color", "$value": { "colorSpace": "oklch", "components": [0.7, 0.1, 30] } } })"}});
	EXPECT_TRUE(without.hasError("c", "has no hex fallback")) << without.all();
}

TEST(DesignTokens, CompositesTakeReferencesInside) {
	const Loaded loaded = load({{"a.tokens.json", R"({
		"black": { "$type": "color", "$value": { "colorSpace": "srgb", "components": [0, 0, 0], "alpha": 0.5 } },
		"blur": { "$type": "dimension", "$value": { "value": 1, "unit": "rem" } },
		"soft": { "$type": "shadow", "$value": { "color": "{black}", "offsetX": { "value": 0, "unit": "px" }, "offsetY": { "value": 2, "unit": "px" }, "blur": "{blur}", "spread": { "value": 0, "unit": "px" } } },
		"layered": { "$type": "shadow", "$value": ["{soft}", { "color": "{black}", "offsetX": { "value": 1, "unit": "px" }, "offsetY": { "value": 1, "unit": "px" }, "blur": { "value": 0, "unit": "px" }, "spread": { "value": 0, "unit": "px" }, "inset": true }] },
		"body": { "$type": "typography", "$value": { "fontFamily": ["Inter", "sans-serif"], "fontSize": { "value": 14, "unit": "px" }, "fontWeight": "semi-bold", "letterSpacing": { "value": 0, "unit": "px" }, "lineHeight": 1.4 } },
		"ease": { "$type": "cubicBezier", "$value": [0.2, 0, 0, 1] },
		"fast": { "$type": "duration", "$value": { "value": 0.15, "unit": "s" } }
	})"}});
	ASSERT_FALSE(hasErrors(loaded.diagnostics)) << loaded.all();
	const auto& soft = std::get<ShadowValue>(loaded.tokens.at("soft").value);
	ASSERT_EQ(soft.layers.size(), 1U);
	EXPECT_FLOAT_EQ(soft.layers[0].blur, 16.0F); // a rem is the root font size, 16 px
	EXPECT_FLOAT_EQ(soft.layers[0].color.a, 0.5F);
	const auto& layered = std::get<ShadowValue>(loaded.tokens.at("layered").value);
	ASSERT_EQ(layered.layers.size(), 2U); // the referenced shadow's layers spliced in
	EXPECT_TRUE(layered.layers[1].inset);
	const auto& body = std::get<TypographyValue>(loaded.tokens.at("body").value);
	EXPECT_EQ(body.fontFamily.names.front(), "Inter");
	EXPECT_FLOAT_EQ(body.fontWeight, 600.0F);
	EXPECT_FLOAT_EQ(loaded.number("fast"), 150.0F);
	EXPECT_FLOAT_EQ(std::get<CubicBezierValue>(loaded.tokens.at("ease").value).x1, 0.2F);

	const Loaded extra =
		load({{"a.tokens.json", R"({ "d": { "$type": "dimension", "$value": { "value": 1, "unit": "px", "scale": 2 } } })"}});
	EXPECT_TRUE(extra.hasError("d", "unexpected property \"scale\"")) << extra.all();
}

TEST(DesignTokens, GroupsExtendOtherGroups) {
	const Loaded loaded = load({{"a.tokens.json", R"({
		"button": { "$type": "number", "height": { "$value": 32 }, "pad": { "$value": 8 } },
		"bigButton": { "$extends": "{button}", "height": { "$type": "number", "$value": 48 } }
	})"}});
	ASSERT_FALSE(hasErrors(loaded.diagnostics)) << loaded.all();
	EXPECT_FLOAT_EQ(loaded.number("bigButton.height"), 48.0F);
	EXPECT_FLOAT_EQ(loaded.number("bigButton.pad"), 8.0F); // inherited, typed by the copied group's $type

	const Loaded cycle = load({{"a.tokens.json", R"({ "a": { "$extends": "{b}" }, "b": { "$extends": "{a}" } })"}});
	EXPECT_NE(cycle.all().find("circles back"), std::string::npos) << cycle.all();
}

// The format has no type for a text transform, so a style's group carries one in salal's extension,
// checked with the document's structure: a bad one leaves the file out, as an unknown $type does.
TEST(DesignTokens, SalalsExtensionGivesAGroupItsTextTransform) {
	const Loaded loaded = load(
		{{"a.tokens.json", R"({ "styles": {
			"button": { "$extensions": { "salal": { "textTransform": "uppercase" } }, "fontSize": { "$type": "dimension", "$value": { "value": 14, "unit": "px" } } },
			"bigButton": { "$extends": "{styles.button}" },
			"tab": { "$extensions": { "salal": { "textTransform": "uppercase" } } }
		} })"},
		 {"b.tokens.json", R"({ "styles": { "tab": { "$extensions": { "salal": { "textTransform": "none" } } } } })"}}
	);
	ASSERT_FALSE(hasErrors(loaded.diagnostics)) << loaded.all();
	EXPECT_EQ(loaded.extensions.at("styles.button").textTransform, TextTransform::Uppercase);
	EXPECT_EQ(loaded.extensions.at("styles.bigButton").textTransform, TextTransform::Uppercase); // through $extends
	EXPECT_EQ(loaded.extensions.at("styles.tab").textTransform, TextTransform::None);			 // the later file's
	EXPECT_EQ(loaded.extensions.at("styles.tab").file, "b.tokens.json");
	EXPECT_FALSE(loaded.extensions.contains("styles")); // nothing inherits one down the groups

	const auto rejected = [](const std::string& button) {
		return load({{"a.tokens.json", R"({ "styles": { "button": )" + button + " } }"}});
	};
	const Loaded shouty = rejected(R"({ "$extensions": { "salal": { "textTransform": "shouty" } } })");
	EXPECT_TRUE(shouty.hasError("styles.button", "$extensions.salal.textTransform \"shouty\" is not none or uppercase")) << shouty.all();
	EXPECT_TRUE(shouty.extensions.empty()); // its file left out whole

	const Loaded misspelt = rejected(R"({ "$extensions": { "salal": { "textTransfrom": "uppercase" } } })");
	EXPECT_TRUE(misspelt.hasError("styles.button", "has no property \"textTransfrom\"")) << misspelt.all();

	const Loaded onAToken = rejected(R"({ "fontSize": { "$type": "dimension", "$value": { "value": 14, "unit": "px" },
		"$extensions": { "salal": { "textTransform": "uppercase" } } } })");
	EXPECT_TRUE(onAToken.hasError("styles.button.fontSize", "goes on a style's group, not on a token")) << onAToken.all();
}
