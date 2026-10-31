#include "ui/theme/Theme.h"

#include "ui/Context.h"
#include "ui/shapes/Rectangle.h"
#include "ui/shapes/Text.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/Variants.h"
#include "ui/theme/dtcg/Codegen.h"
#include "ui/theme/dtcg/Resolver.h"

#include <theme_test/Tokens.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

	std::string describe(const std::vector<ui::dtcg::Diagnostic>& diagnostics) {
		std::string text;
		for (const ui::dtcg::Diagnostic& d : diagnostics) {
			text += d.text() + "\n";
		}
		return text;
	}

	void expectColor(const Foundation::Color& actual, float r, float g, float b, float a = 1.0F) {
		EXPECT_NEAR(actual.r, r, 1e-4);
		EXPECT_NEAR(actual.g, g, 1e-4);
		EXPECT_NEAR(actual.b, b, 1e-4);
		EXPECT_NEAR(actual.a, a, 1e-4);
	}

	// A table built by hand over one inline tokens file, for what a generated one can't get wrong.
	struct HandTable {
		HandTable(std::string text, std::vector<ui::TokenEntry> entries)
			: text(std::move(text)),
			  entries(std::move(entries)) {
			pieces[0] = this->text;
			files[0] = {.name = "hand.tokens.json", .pieces = pieces};
			table = {.name = "hand", .tokens = this->entries, .styles = {}, .files = files, .entry = "", .base = &ui::tokenTable()};
		}

		std::string					text;
		std::vector<ui::TokenEntry> entries;
		std::string_view			pieces[1];
		ui::EmbeddedFile			files[1];
		ui::TokenTable				table{};
	};

} // namespace

TEST(Theme, UiDefaultsLoadOnTheirOwn) {
	const ui::ThemeLoad load = ui::Theme::load({});
	ASSERT_TRUE(load.diagnostics.empty()) << describe(load.diagnostics);
	const ui::Style& pane = load.theme.style(ui::styles::pane.theme);
	ASSERT_TRUE(pane.backgroundColor.has_value());
	// styles.pane.backgroundColor -> color.surface.raised -> base.color.gray850, #181c24
	expectColor(*pane.backgroundColor, 0.0941F, 0.1098F, 0.1412F);
	EXPECT_FLOAT_EQ(pane.borderRadius.value_or(-1.0F), 4.0F);
	EXPECT_FALSE(pane.fontSize.has_value());
	EXPECT_FLOAT_EQ(load.theme.style(ui::styles::text::body.theme).fontSize.value_or(-1.0F), 14.0F); // from its font
	EXPECT_FLOAT_EQ(load.theme.get(ui::tokens::motion::duration::normal), 200.0F);
}

TEST(Theme, AGameThemeOverridesUiAndSwitchesModes) {
	const ui::ThemeLoad dark = ui::Theme::load({.tokens = &theme_test::tokenTable()});
	const ui::ThemeLoad light = ui::Theme::load({.tokens = &theme_test::tokenTable(), .inputs = {{"theme", "light"}}});
	ASSERT_TRUE(dark.ok()) << describe(dark.diagnostics);
	ASSERT_TRUE(light.ok()) << describe(light.diagnostics);
	EXPECT_TRUE(dark.diagnostics.empty()) << describe(dark.diagnostics);

	// ui's pane, its background aliased by the game to its own palette, which the mode swaps.
	expectColor(*dark.theme.style(ui::styles::pane.theme).backgroundColor, 0.1F, 0.12F, 0.16F);
	expectColor(*light.theme.style(ui::styles::pane.theme).backgroundColor, 0.96F, 0.97F, 0.98F);
	// What the game leaves alone falls back to ui's.
	expectColor(*light.theme.style(ui::styles::pane.theme).borderColor, 1.0F, 1.0F, 1.0F, 0.12F);
	// The game's own style, its p3 colour drawn through its hex, its radius one of ui's.
	const ui::Style& fill = dark.theme.style(theme_test::styles::healthBar::fill.theme);
	expectColor(*fill.backgroundColor, 230.0F / 255.0F, 102.0F / 255.0F, 26.0F / 255.0F);
	EXPECT_FLOAT_EQ(fill.borderRadius.value_or(-1.0F), 2.0F);
	EXPECT_TRUE(dark.theme.style(theme_test::styles::healthBar::track.theme).backgroundColor.has_value());
	expectColor(light.theme.get(theme_test::tokens::palette::slate::_900), 0.96F, 0.97F, 0.98F);
}

TEST(Theme, FontRolesMapToTheGamesFamilies) {
	constexpr Renderer::FontFamily kBody{3};
	const ui::FontFamilyName	   names[] = {{.name = "body", .family = kBody}};
	const ui::ThemeLoad			   load = ui::Theme::load({.fontFamilies = names});
	ASSERT_TRUE(load.ok()) << describe(load.diagnostics);
	EXPECT_EQ(load.theme.style(ui::styles::text::body.theme).fontFamily, kBody);
	EXPECT_EQ(load.theme.get(ui::tokens::typography::caption).family, kBody);
	// display and mono name families the game didn't register: the default family, and a warning.
	EXPECT_EQ(load.theme.get(ui::tokens::font::family::display), Renderer::FontFamily::Default);
	EXPECT_FALSE(load.diagnostics.empty());
}

TEST(Theme, AnUnknownPathWarns) {
	const HandTable		hand(R"({ "color": { "surface": { "raisd": { "$type": "color", "$value": "{base.color.white}" } } } })", {});
	const ui::ThemeLoad load = ui::Theme::load({.tokens = &hand.table});
	EXPECT_TRUE(load.ok()) << describe(load.diagnostics);
	ASSERT_EQ(load.diagnostics.size(), 1U) << describe(load.diagnostics);
	EXPECT_EQ(load.diagnostics[0].severity, ui::dtcg::Diagnostic::Severity::Warning);
	EXPECT_EQ(load.diagnostics[0].path, "color.surface.raisd");
}

TEST(Theme, AMissingPathIsAnErrorWhenStrictAndADefaultEitherWay) {
	const HandTable		 hand("{}", {{.path = "hud.glow", .type = ui::dtcg::TokenType::Color}});
	const ui::ColorToken glow{.table = &hand.table, .index = 0};

	const ui::ThemeLoad strict = ui::Theme::load({.tokens = &hand.table, .strict = true});
	EXPECT_FALSE(strict.ok());
	ASSERT_EQ(strict.diagnostics.size(), 1U) << describe(strict.diagnostics);
	EXPECT_EQ(strict.diagnostics[0].path, "hud.glow");

	const ui::ThemeLoad lenient = ui::Theme::load({.tokens = &hand.table, .strict = false});
	EXPECT_TRUE(lenient.ok());
	expectColor(lenient.theme.get(glow), 0.0F, 0.0F, 0.0F);
}

TEST(Theme, AWrongTypeFailsTheLoad) {
	const HandTable		hand(R"({ "hud": { "glow": { "$type": "colour", "$value": "#fff" } } })", {});
	const ui::ThemeLoad load = ui::Theme::load({.tokens = &hand.table});
	EXPECT_FALSE(load.ok());
	EXPECT_NE(describe(load.diagnostics).find("hand/hand.tokens.json: hud.glow: unknown $type \"colour\""), std::string::npos)
		<< describe(load.diagnostics);
}

TEST(Theme, StateLayersTonesAndSizesReadTheTokens) {
	const ui::Theme			theme = ui::Theme::load({}).theme;
	const Foundation::Color base{0.2F, 0.2F, 0.2F, 1.0F};
	// hover's wash is white at 0.06: 0.2 * 0.94 + 0.06
	expectColor(ui::applyStateLayers(theme, base, {.hovered = true}), 0.248F, 0.248F, 0.248F);
	expectColor(ui::applyStateLayers(theme, base, {.disabled = true}), 0.2F, 0.2F, 0.2F, 0.45F);
	expectColor(
		ui::applyStateLayers(theme, {0.0F, 0.0F, 0.0F, 0.0F}, {.hovered = true}), 1.0F, 1.0F, 1.0F, 0.06F
	); // a wash shows on a clear base

	EXPECT_EQ(ui::resolveTone(theme, ui::Tone::Auto, 0.1F), ui::Tone::Danger);
	EXPECT_EQ(ui::resolveTone(theme, ui::Tone::Auto, 0.4F), ui::Tone::Warning);
	EXPECT_EQ(ui::resolveTone(theme, ui::Tone::Auto, 0.9F), ui::Tone::Positive);
	EXPECT_EQ(ui::resolveTone(theme, ui::Tone::Info, 0.1F), ui::Tone::Info);
	expectColor(ui::toneColors(theme, ui::Tone::Danger).color, 0.8784F, 0.3255F, 0.2353F);

	EXPECT_FLOAT_EQ(ui::sizeMetrics(theme, ui::ControlSize::Lg).controlHeight, 40.0F);
	EXPECT_FLOAT_EQ(ui::sizeMetrics(theme, ui::ControlSize::Sm).fontSize, 12.0F);
	EXPECT_FLOAT_EQ(ui::focusRing(theme).width, 2.0F);
}

// A component resolves its style against its scene's theme when it mounts and again on a mode
// switch: its type's theme style, then the one its style extends, then its own fields.
TEST(Theme, MountedComponentsResolveAgainstTheScenesTheme) {
	ui::Context context({800.0F, 600.0F});
	const auto	pane =
		context.root().addChild<ui::Rectangle>({.size = {10.0F, 10.0F}, .style = ui::styles::pane.extend({.borderRadius = 9.0F})});
	const auto label = context.root().addChild<ui::Text>({.text = "hi"});
	expectColor(*pane.get()->getStyle().backgroundColor, 0.0941F, 0.1098F, 0.1412F);
	EXPECT_FLOAT_EQ(pane.get()->getStyle().borderRadius.value_or(-1.0F), 9.0F);
	EXPECT_FLOAT_EQ(label.get()->getStyle().fontSize.value_or(-1.0F), 14.0F); // Text's own theme style, text.body

	context.setTheme(ui::Theme::load({.tokens = &theme_test::tokenTable(), .inputs = {{"theme", "light"}}}).theme);
	expectColor(*pane.get()->getStyle().backgroundColor, 0.96F, 0.97F, 0.98F);
	EXPECT_FLOAT_EQ(pane.get()->getStyle().borderRadius.value_or(-1.0F), 9.0F);
	expectColor(*label.get()->getStyle().color, 0.08F, 0.09F, 0.11F);
}

// Every tokens folder in the repo, the way salal_tokens() loads it: over ui's tokens, in every mode
// its resolver has, with no errors and its styles following the rules.
TEST(Theme, EveryTokensFileInTheRepoLoads) {
	namespace fs = std::filesystem;
	const fs::path root = SALAL_SOURCE_DIR;
	const fs::path uiTokens = root / "libs/ui/tokens";

	std::map<fs::path, std::vector<std::string>> folders; // the folder holding each file -> file names
	for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
		const std::string name = it->path().filename().string();
		if (it->is_directory() && (name.starts_with('.') || name.starts_with("build") || name == "vcpkg_installed")) {
			it.disable_recursion_pending();
		} else if (name.ends_with(".tokens.json") || name.ends_with(".resolver.json")) {
			folders[it->path().parent_path()].push_back(name);
		}
	}
	ASSERT_TRUE(folders.contains(uiTokens));

	// A tokens folder is its resolver's folder, or for files without one, the folder they're in.
	const auto layerFor = [](const fs::path& dir) {
		ui::dtcg::Layer layer{.label = dir.generic_string()};
		for (auto it = fs::recursive_directory_iterator(dir); it != fs::recursive_directory_iterator(); ++it) {
			const std::string name = fs::relative(it->path(), dir).generic_string();
			if (name.ends_with(".resolver.json")) {
				layer.entry = name;
			} else if (name.ends_with(".tokens.json")) {
				layer.files.push_back(name);
			}
		}
		std::sort(layer.files.begin(), layer.files.end());
		layer.read = [dir](const std::string& name) -> std::optional<std::string> {
			std::ifstream	   file(dir / name, std::ios::binary);
			std::ostringstream text;
			text << file.rdbuf();
			return file ? std::optional(text.str()) : std::nullopt;
		};
		return layer;
	};

	size_t loaded = 0;
	for (const auto& [dir, names] : folders) {
		const bool hasResolver =
			std::any_of(names.begin(), names.end(), [](const std::string& n) { return n.ends_with(".resolver.json"); });
		const bool insideResolverFolder = std::any_of(folders.begin(), folders.end(), [&](const auto& other) {
			return other.first != dir && dir.generic_string().starts_with(other.first.generic_string() + "/") &&
				   std::any_of(other.second.begin(), other.second.end(), [](const std::string& n) {
					   return n.ends_with(".resolver.json");
				   });
		});
		if (insideResolverFolder && !hasResolver) {
			continue; // its resolver's folder loads it
		}
		std::vector<ui::dtcg::Layer> layers;
		if (dir != uiTokens) {
			layers.push_back(layerFor(uiTokens));
		}
		layers.push_back(layerFor(dir));
		std::vector<ui::dtcg::Modifier> modifiers;
		for (const ui::dtcg::Layer& layer : layers) {
			for (ui::dtcg::Modifier& modifier : ui::dtcg::layerModifiers(layer)) {
				modifiers.push_back(std::move(modifier));
			}
		}
		for (const ui::dtcg::ResolverInputs& inputs : ui::dtcg::permutations(modifiers)) {
			std::vector<ui::dtcg::Diagnostic> diagnostics;
			const ui::dtcg::ResolvedTokens resolved = ui::dtcg::resolve(ui::dtcg::loadLayers(layers, inputs, diagnostics), {}, diagnostics);
			ui::dtcg::catalog(resolved, {}, diagnostics);
			EXPECT_FALSE(ui::dtcg::hasErrors(diagnostics)) << dir.generic_string() << ":\n" << describe(diagnostics);
			EXPECT_FALSE(resolved.empty()) << dir.generic_string();
			++loaded;
		}
	}
	EXPECT_GE(loaded, 3U); // ui's, and the test theme's two modes
}
