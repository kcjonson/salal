// FontRenderer on the null backend, with atlases written to a temporary folder.
//
// MeasureText and generateGlyphQuads must agree on advances (one shared
// iteration), or centered/right-aligned text drifts against its measured box.
// The key assertion renders a doubled string (s+s): the second copy's quads
// must sit exactly MeasureText(s) + letterSpacing to the right of the first
// copy's, which proves measured width == rendered pen advance, including the
// fallback for characters missing from the atlas.
//
// Initialize takes its atlases from the caller: the contract tests check what it
// refuses, that a refusal leaves nothing loaded, and what a family that is not
// loaded draws with.
//
// A glyph quad's UVs index one family's atlas, so each quad carries that atlas and
// drawGlyphs samples it: there is no second place to name a family, and so no way
// to draw one family's quads with another's atlas.
//
// A quad reads its atlas half a texel past the glyph's atlasBounds, and no glyph's
// read may cross another's; loading checks it, and crampedGlyphs says what it found.
//
// The atlases are synthetic, in the JSON msdf-atlas-gen writes: printable ASCII,
// three characters past it (two, three and four bytes of UTF-8) and optionally
// U+FFFD, each glyph's box 32 texels square, packed 16 to a row from the atlas's
// bottom so neighbouring boxes touch, its atlasBounds inset half a texel from the
// box, each glyph with an advance of its own, and a blank PNG of the atlas's size.
// These tests read metrics, not outlines, so no font goes into them.

#include "font/FontRenderer.h"
#include "BaseShader.h"
#include "PngEncoder.h"
#include "backend/NullBackend.h"
#include "primitives/Primitives.h"
#include "utils/Log.h"

#include <glm/gtc/packing.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Renderer {

	namespace {

		constexpr FontFamily kBody{0};
		constexpr FontFamily kMono{3};

		constexpr float kBox = 32.0F; // Texels a glyph's box, and an em
		constexpr int	kAtlasWidth = 512;
		constexpr int	kAtlasHeight = 256;
		// Every glyph's plane bounds, in em: an em less a texel at each edge, as its box is.
		constexpr float kPlaneLeft = -0.1F;
		constexpr float kPlaneBottom = -0.25F;
		constexpr float kPlaneExtent = (kBox - 1.0F) / kBox;

		struct KerningPair {
			char32_t left;
			char32_t right;
			float	 advance; // In em
		};

		struct AtlasMetrics {
			float						  ascender = 0.9F;
			float						  lineHeight = 1.2F;
			float						  advanceScale = 1.0F;	// Every advance
			float						  distanceRange = 4.0F; // Texels
			bool						  yOriginTop = false;
			bool						  replacement = false; // U+FFFD among the glyphs
			std::vector<KerningPair>	  kerning;
			std::map<char32_t, glm::vec2> shift; // Texels a glyph's atlasBounds move, right and up
		};

		// The glyphs in their boxes' order: space has a box it doesn't use.
		std::vector<char32_t> codePoints(bool replacement) {
			std::vector<char32_t> glyphs;
			for (char32_t c = U' '; c <= U'~'; ++c) {
				glyphs.push_back(c);
			}
			glyphs.insert(glyphs.end(), {0xE9, 0x20AC, 0x1F600}); // é, €, 😀
			if (replacement) {
				glyphs.push_back(0xFFFD);
			}
			return glyphs;
		}

		size_t boxOf(char32_t codePoint) {
			const std::vector<char32_t> glyphs = codePoints(true);
			return static_cast<size_t>(std::ranges::find(glyphs, codePoint) - glyphs.begin());
		}

		// The bottom-left corner of a glyph's box, in texels from the atlas's bottom-left.
		glm::vec2 boxCorner(size_t box) {
			return {static_cast<float>(box % 16) * kBox, static_cast<float>(box / 16) * kBox};
		}

		float advanceOf(size_t box, float scale) {
			return (0.45F + 0.03F * static_cast<float>(box % 9)) * scale;
		}

		std::string number(float value) {
			return std::to_string(value);
		}

		std::string atlasJson(const AtlasMetrics& metrics) {
			// A top-origin file says the same glyphs with y pointing down: plane bounds and the
			// ascender negated, atlas rows counted from the image's top.
			const float up = metrics.yOriginTop ? -1.0F : 1.0F;
			const auto	row = [&](float texels) {
				 return number(metrics.yOriginTop ? static_cast<float>(kAtlasHeight) - texels : texels);
			};
			std::string json = R"({"atlas":{"type":"msdf","distanceRange":)" + number(metrics.distanceRange) +
							   R"(,"distanceRangeMiddle":0,"size":32,"width":512,"height":256,"yOrigin":")" +
							   (metrics.yOriginTop ? "top" : "bottom") + R"("},)";
			json += R"("name":"Test","metrics":{"emSize":1,"lineHeight":)" + number(metrics.lineHeight) + R"(,"ascender":)" +
					number(up * metrics.ascender) + R"(,"descender":)" + number(up * -0.25F) + R"(},"glyphs":[)";
			const std::vector<char32_t> glyphs = codePoints(metrics.replacement);
			for (size_t box = 0; box < glyphs.size(); ++box) {
				json += box == 0 ? "{" : ",{";
				json += R"("unicode":)" + std::to_string(static_cast<unsigned>(glyphs[box])) + R"(,"advance":)" +
						number(advanceOf(box, metrics.advanceScale));
				if (glyphs[box] != U' ') {
					const auto		shifted = metrics.shift.find(glyphs[box]);
					const glm::vec2 offset = shifted != metrics.shift.end() ? shifted->second : glm::vec2(0.0F);
					const glm::vec2 low = boxCorner(box) + 0.5F + offset;
					const glm::vec2 high = boxCorner(box) + (kBox - 0.5F) + offset;
					json += R"(,"planeBounds":{"left":)" + number(kPlaneLeft) + R"(,"bottom":)" + number(up * kPlaneBottom) +
							R"(,"right":)" + number(kPlaneLeft + kPlaneExtent) + R"(,"top":)" + number(up * (kPlaneBottom + kPlaneExtent)) +
							"}";
					json += R"(,"atlasBounds":{"left":)" + number(low.x) + R"(,"bottom":)" + row(low.y) + R"(,"right":)" + number(high.x) +
							R"(,"top":)" + row(high.y) + "}";
				}
				json += "}";
			}
			json += R"(],"kerning":[)";
			for (size_t i = 0; i < metrics.kerning.size(); ++i) {
				const KerningPair& pair = metrics.kerning[i];
				json += (i == 0 ? R"({"unicode1":)" : R"(,{"unicode1":)") + std::to_string(static_cast<unsigned>(pair.left)) +
						R"(,"unicode2":)" + std::to_string(static_cast<unsigned>(pair.right)) + R"(,"advance":)" + number(pair.advance) +
						"}";
			}
			json += "]}";
			return json;
		}

		// One glyph, 'A', in a 64 x 48 atlas, written as msdf-atlas-gen writes a game's atlas
		// (top origin, so the ascender and the plane's tops are negative and atlas rows count
		// down from the image's top), every length in em times `em`, the file's emSize. 'A' fills
		// 8.5 to 40.5 texels across and 4.5 to 36.5 down; its plane is an em square, 0.125 em left
		// of the pen and reaching 0.75 em above the baseline.
		std::string oneGlyphJson(double em = 1.0) {
			const auto lengths = [em](double value) {
				return std::to_string(value * em);
			};
			std::string json = R"({"atlas":{"type":"mtsdf","distanceRange":8,"distanceRangeMiddle":0,"size":48,"width":64,"height":48,)";
			json += R"("yOrigin":"top"},"metrics":{"emSize":)" + lengths(1.0) + R"(,"lineHeight":)" + lengths(1.25) + R"(,"ascender":)" +
					lengths(-1.0) + R"(,"descender":)" + lengths(0.25) + R"(},"glyphs":[)";
			json += R"({"unicode":32,"advance":)" + lengths(0.25) + "},";
			json += R"({"unicode":65,"advance":)" + lengths(0.5) + R"(,"planeBounds":{"left":)" + lengths(-0.125) + R"(,"top":)" +
					lengths(-0.75) + R"(,"right":)" + lengths(0.875) + R"(,"bottom":)" + lengths(0.25) +
					R"(},"atlasBounds":{"left":8.5,"top":4.5,"right":40.5,"bottom":36.5}}],"kerning":[]})";
			return json;
		}

		std::string replaced(std::string text, const std::string& from, const std::string& to) {
			const size_t at = text.find(from);
			EXPECT_NE(at, std::string::npos) << from;
			return at == std::string::npos ? text : text.replace(at, from.size(), to);
		}

		// A folder of its own per test, removed with it.
		class AtlasFolder {
		  public:
			AtlasFolder()
				: path(
					  std::filesystem::temp_directory_path() /
					  ("salal-font-atlases-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
					   std::to_string(std::random_device{}()))
				  ) {
				std::filesystem::create_directories(path);
			}
			~AtlasFolder() {
				std::error_code ignored;
				std::filesystem::remove_all(path, ignored);
			}
			AtlasFolder(const AtlasFolder&) = delete;
			AtlasFolder& operator=(const AtlasFolder&) = delete;

			FontAtlasSource write(FontFamily family, const std::string& name, const AtlasMetrics& metrics = {}) const {
				return writeJson(family, name, atlasJson(metrics), kAtlasWidth, kAtlasHeight);
			}

			// The JSON as given, and a blank PNG of the size given.
			FontAtlasSource writeJson(FontFamily family, const std::string& name, const std::string& json, int width, int height) const {
				const FontAtlasSource source{.family = family, .pngPath = file(name + ".png"), .jsonPath = file(name + ".json")};
				std::ofstream(source.jsonPath, std::ios::binary) << json;
				const std::vector<unsigned char> blank(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
				EXPECT_TRUE(writePngToFile(blank.data(), width, height, source.pngPath));
				return source;
			}

			// A path in the folder, which may name nothing.
			std::string file(const std::string& name) const { return (path / name).string(); }

		  private:
			std::filesystem::path path;
		};

		// The UI records logged at one level while it lives.
		class UiLog {
		  public:
			explicit UiLog(foundation::LogLevel level)
				: level(level) {
				foundation::Logger::setSink(
					[](void* context, const foundation::LogRecord& record) {
						auto* log = static_cast<UiLog*>(context);
						if (record.category == foundation::LogCategory::UI && record.level == log->level) {
							log->messages.emplace_back(record.message);
						}
					},
					this
				);
			}
			~UiLog() { foundation::Logger::setSink(nullptr, nullptr); }

			UiLog(const UiLog&) = delete;
			UiLog& operator=(const UiLog&) = delete;
			UiLog(UiLog&&) = delete;
			UiLog& operator=(UiLog&&) = delete;

			std::vector<std::string> messages;

		  private:
			foundation::LogLevel level;
		};

		class FontRendererFixture : public ::testing::Test {
		  protected:
			void SetUp() override {
				auto owned = std::make_unique<backend::NullBackend>();
				nullBackend = owned.get();
				Primitives::init(std::move(owned), base_shader::source());
				texturesBefore = nullBackend->live().textures;
				texturesMadeBefore = nullBackend->texturesMade();
				fontRenderer = std::make_unique<FontRenderer>();
			}

			void TearDown() override {
				fontRenderer.reset(); // Destroy its textures while the backend is there
				Primitives::shutdown();
			}

			// Atlas textures the font renderer holds.
			size_t atlasTextures() const { return nullBackend->live().textures - texturesBefore; }

			// Atlas textures it has made since the test began, released or not.
			int atlasTexturesMade() const { return nullBackend->texturesMade() - texturesMadeBefore; }

			std::vector<FontRenderer::GlyphQuad>
			quadsFor(const std::string& text, float scale = 1.0F, float letterSpacing = 0.0F, FontFamily family = kBody) const {
				std::vector<FontRenderer::GlyphQuad> quads;
				fontRenderer->generateGlyphQuads(text, glm::vec2(0.0F, 0.0F), scale, glm::vec4(1.0F), quads, family, letterSpacing);
				return quads;
			}

			// Two strings draw alike: the same width, and quads in the same places sampling the same texels.
			void expectDrawnAlike(const std::string& text, const std::string& like, FontFamily family = kBody) const {
				EXPECT_FLOAT_EQ(fontRenderer->MeasureText(text, 1.0F, family).x, fontRenderer->MeasureText(like, 1.0F, family).x)
					<< testing::PrintToString(text);
				const auto quads = quadsFor(text, 1.0F, 0.0F, family);
				const auto expected = quadsFor(like, 1.0F, 0.0F, family);
				ASSERT_EQ(quads.size(), expected.size()) << testing::PrintToString(text);
				for (size_t i = 0; i < quads.size(); ++i) {
					EXPECT_EQ(quads[i].position, expected[i].position) << testing::PrintToString(text) << " glyph " << i;
					EXPECT_EQ(quads[i].uvMin, expected[i].uvMin) << testing::PrintToString(text) << " glyph " << i;
					EXPECT_EQ(quads[i].uvMax, expected[i].uvMax) << testing::PrintToString(text) << " glyph " << i;
				}
			}

			AtlasFolder					  folder;
			backend::NullBackend*		  nullBackend = nullptr;
			size_t						  texturesBefore = 0;
			int							  texturesMadeBefore = 0;
			std::unique_ptr<FontRenderer> fontRenderer;
		};

		class FontRendererTest : public FontRendererFixture {
		  protected:
			void SetUp() override {
				FontRendererFixture::SetUp();
				ASSERT_TRUE(fontRenderer->Initialize(std::array{folder.write(kBody, "body")}, kBody));
			}
		};

		class FontRendererInitializeTest : public FontRendererFixture {
		  protected:
			// What a renderer holding nothing measures and samples.
			void expectNothingLoaded() const {
				EXPECT_EQ(atlasTextures(), 0U);
				EXPECT_FALSE(fontRenderer->getAtlasTexture().isValid());
				EXPECT_FALSE(fontRenderer->getAtlasTexture(kBody).isValid());
				EXPECT_EQ(fontRenderer->MeasureText("text", 1.0F), glm::vec2(0.0F, 0.0F));
				std::vector<FontRenderer::GlyphQuad> quads;
				fontRenderer->generateGlyphQuads("text", glm::vec2(0.0F), 1.0F, glm::vec4(1.0F), quads);
				EXPECT_TRUE(quads.empty());
			}
		};

	} // namespace

	TEST_F(FontRendererTest, MeasuredWidthMatchesRenderedAdvance) {
		// Includes '\x01', a character absent from the atlas (exercises the '?'
		// fallback inside the doubled-string advance check), and characters of two to four bytes.
		const std::string missingChar = std::string("mi") + '\x01' + "ssing";
		const std::string testStrings[] = {
			"Hello, World!",
			"iiiillll",
			"A B C",
			missingChar,
			"caf\xC3\xA9 \xE2\x82\xAC"
			"5 \xF0\x9F\x98\x80"
		};
		const float letterSpacings[] = {0.0F, 2.5F};
		const float scales[] = {1.0F, 1.25F};

		for (const std::string& text : testStrings) {
			for (float letterSpacing : letterSpacings) {
				for (float scale : scales) {
					const float measured = fontRenderer->MeasureText(text, scale, kBody, letterSpacing).x;

					const auto quadsSingle = quadsFor(text, scale, letterSpacing);
					const auto quadsDoubled = quadsFor(text + text, scale, letterSpacing);
					ASSERT_EQ(quadsDoubled.size(), quadsSingle.size() * 2)
						<< "text=" << text << " ls=" << letterSpacing << " scale=" << scale;

					// The second copy starts one measured width (plus the gap's
					// letter-spacing) after the first.
					const float expectedOffset = measured + letterSpacing;
					for (size_t i = 0; i < quadsSingle.size(); ++i) {
						const auto& first = quadsSingle[i];
						const auto& second = quadsDoubled[i + quadsSingle.size()];
						EXPECT_NEAR(second.position.x - first.position.x, expectedOffset, 1e-3F)
							<< "glyph " << i << " text=" << text << " ls=" << letterSpacing << " scale=" << scale;
						EXPECT_FLOAT_EQ(second.position.y, first.position.y);
						EXPECT_FLOAT_EQ(second.size.x, first.size.x);
						EXPECT_FLOAT_EQ(second.size.y, first.size.y);
					}
				}
			}
		}
	}

	TEST_F(FontRendererTest, CharactersPastAsciiLayOutByCodePoint) {
		// é, € and 😀: two, three and four bytes, one glyph each, from their own boxes.
		const std::string			  text = "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
		const std::array<char32_t, 3> expected{0xE9, 0x20AC, 0x1F600};
		const auto					  quads = quadsFor(text);
		ASSERT_EQ(quads.size(), expected.size());
		float width = 0.0F;
		for (size_t i = 0; i < expected.size(); ++i) {
			const size_t box = boxOf(expected[i]);
			EXPECT_FLOAT_EQ(quads[i].uvMin.x, (boxCorner(box).x + 0.5F) / static_cast<float>(kAtlasWidth)) << "glyph " << i;
			EXPECT_FLOAT_EQ(quads[i].uvMin.y, (boxCorner(box).y + 0.5F) / static_cast<float>(kAtlasHeight)) << "glyph " << i;
			EXPECT_NEAR(quads[i].position.x, width + kPlaneLeft * 16.0F, 1e-4F) << "glyph " << i;
			width += advanceOf(box, 1.0F) * 16.0F;
		}
		EXPECT_NEAR(fontRenderer->MeasureText(text, 1.0F).x, width, 1e-4F);
	}

	TEST_F(FontRendererTest, MissingGlyphMeasuresAndRendersAsFallback) {
		// This atlas has no U+FFFD, so what it lacks draws as '?': a byte it has no glyph for, a
		// character past ASCII (世), and each ill-formed sequence.
		expectDrawnAlike(std::string("a") + '\x01' + "b", "a?b");
		expectDrawnAlike(
			"a\xE4\xB8\x96"
			"b",
			"a?b"
		);
		expectDrawnAlike(
			"a\xE2\x82"
			"b\x80\x80",
			"a?b??"
		);
	}

	TEST_F(FontRendererInitializeTest, WithAReplacementGlyphWhatTheAtlasLacksDrawsAsIt) {
		ASSERT_TRUE(fontRenderer->Initialize(std::array{folder.write(kBody, "body", {.replacement = true})}, kBody));
		const std::string replacement = "\xEF\xBF\xBD";
		expectDrawnAlike(std::string("a") + '\x01' + "b", "a" + replacement + "b");
		expectDrawnAlike(
			"a\xE4\xB8\x96"
			"b",
			"a" + replacement + "b"
		);
		// One replacement a bad sequence: three bytes cut short, then two lone continuation bytes, then
		// four cut short by the end.
		expectDrawnAlike(
			"a\xE2\x82"
			"b\x80\x80"
			"c\xF0\x9F\x98",
			"a" + replacement + "b" + replacement + replacement + "c" + replacement
		);
	}

	TEST_F(FontRendererTest, LetterSpacingSitsBetweenGlyphsOnly) {
		// Between characters, not bytes: six characters in nine bytes, five gaps.
		const std::string text = "caf\xC3\xA9 \xE2\x82\xAC";
		const float		  letterSpacing = 3.0F;
		const float		  base = fontRenderer->MeasureText(text, 1.0F).x;
		const float		  spaced = fontRenderer->MeasureText(text, 1.0F, kBody, letterSpacing).x;
		EXPECT_NEAR(spaced - base, letterSpacing * 5.0F, 1e-3F);

		EXPECT_FLOAT_EQ(fontRenderer->MeasureText("", 1.0F).x, 0.0F);
	}

	TEST_F(FontRendererTest, GlyphQuadsCarryBaselinePenOrigin) {
		// runOrigin is the pen origin at the baseline: box position + font ascent.
		const float							 ascent = fontRenderer->getAscent(1.0F);
		const glm::vec2						 origin(13.25F, 7.5F);
		std::vector<FontRenderer::GlyphQuad> quads;
		fontRenderer->generateGlyphQuads("Run", origin, 1.0F, glm::vec4(1.0F), quads);
		ASSERT_FALSE(quads.empty());
		for (const auto& quad : quads) {
			EXPECT_FLOAT_EQ(quad.runOrigin.x, origin.x);
			EXPECT_FLOAT_EQ(quad.runOrigin.y, origin.y + ascent);
		}

		// Second call is served from the glyph-quad cache; the origin must still
		// be rewritten per call.
		const glm::vec2						 origin2(101.75F, 44.0F);
		std::vector<FontRenderer::GlyphQuad> cachedQuads;
		fontRenderer->generateGlyphQuads("Run", origin2, 1.0F, glm::vec4(1.0F), cachedQuads);
		ASSERT_EQ(cachedQuads.size(), quads.size());
		for (const auto& quad : cachedQuads) {
			EXPECT_FLOAT_EQ(quad.runOrigin.x, origin2.x);
			EXPECT_FLOAT_EQ(quad.runOrigin.y, origin2.y + ascent);
		}
	}

	TEST_F(FontRendererTest, WrappedTextLaysOutAtTheScaleItWasWrappedAt) {
		// The result carries the scale, so glyph sizes, alignment and line height all follow it: at a
		// scale of 1 and at another, each line comes out as generateGlyphQuads draws it directly there.
		const float		  atlasLineHeight = 1.2F; // The synthetic atlas's, in ems
		const std::string text = "wrap these words";
		for (const float scale : {1.0F, 2.5F}) {
			const float maxWidth = 100.0F * scale;
			const float containerWidth = 200.0F * scale;
			const float lineHeight = atlasLineHeight * 16.0F * scale;

			const FontRenderer::WrappedTextResult& wrapped = fontRenderer->wrapText(text, scale, maxWidth, kBody);
			ASSERT_GE(wrapped.lines.size(), 2U) << "scale=" << scale;
			EXPECT_FLOAT_EQ(wrapped.scale, scale);
			EXPECT_FLOAT_EQ(wrapped.lineHeight, lineHeight);

			std::vector<FontRenderer::GlyphQuad> quads;
			fontRenderer->generateWrappedGlyphQuads(
				wrapped, glm::vec2(0.0F), glm::vec4(1.0F), Foundation::HorizontalAlign::Right, containerWidth, quads
			);

			std::vector<FontRenderer::GlyphQuad> expected;
			for (size_t i = 0; i < wrapped.lines.size(); ++i) {
				const float		lineWidth = fontRenderer->MeasureText(wrapped.lines[i], scale, kBody).x;
				const glm::vec2 lineOrigin(containerWidth - lineWidth, static_cast<float>(i) * lineHeight);
				fontRenderer->generateGlyphQuads(wrapped.lines[i], lineOrigin, scale, glm::vec4(1.0F), expected, kBody);
			}
			ASSERT_EQ(quads.size(), expected.size()) << "scale=" << scale;
			for (size_t i = 0; i < quads.size(); ++i) {
				EXPECT_NEAR(quads[i].position.x, expected[i].position.x, 1e-3F) << "glyph " << i << " scale=" << scale;
				EXPECT_NEAR(quads[i].position.y, expected[i].position.y, 1e-3F) << "glyph " << i << " scale=" << scale;
				EXPECT_NEAR(quads[i].size.x, expected[i].size.x, 1e-3F) << "glyph " << i << " scale=" << scale;
				EXPECT_NEAR(quads[i].size.y, expected[i].size.y, 1e-3F) << "glyph " << i << " scale=" << scale;
			}
		}
	}

	// --- The atlas format ---

	TEST_F(FontRendererInitializeTest, AGlyphsQuadIsItsPlaneBoundsSamplingItsAtlasBounds) {
		// The same glyph in an em of 1 and of 2 lays out the same.
		for (const double em : {1.0, 2.0}) {
			ASSERT_TRUE(fontRenderer->Initialize(std::array{folder.writeJson(kBody, "one", oneGlyphJson(em), 64, 48)}, kBody)) << em;
			std::vector<FontRenderer::GlyphQuad> quads;
			fontRenderer->generateGlyphQuads("A A", glm::vec2(10.0F, 20.0F), 1.0F, glm::vec4(1.0F), quads);
			ASSERT_EQ(quads.size(), 2U) << em;

			// An em is 16 px at a scale of 1, and the ascender of an em puts the baseline 16 px down.
			EXPECT_FLOAT_EQ(fontRenderer->getAscent(1.0F), 16.0F) << em;
			EXPECT_FLOAT_EQ(quads[0].position.x, 10.0F - 0.125F * 16.0F) << em;
			EXPECT_FLOAT_EQ(quads[0].position.y, 20.0F + 16.0F - 0.75F * 16.0F) << em;
			EXPECT_FLOAT_EQ(quads[0].size.x, 16.0F) << em;
			EXPECT_FLOAT_EQ(quads[0].size.y, 16.0F) << em;
			// Texels over the atlas's size, with y up in the atlas as uploaded: rows 36.5 and 4.5 down
			// from the image's top are 11.5 and 43.5 up from its bottom.
			EXPECT_FLOAT_EQ(quads[0].uvMin.x, 8.5F / 64.0F) << em;
			EXPECT_FLOAT_EQ(quads[0].uvMax.x, 40.5F / 64.0F) << em;
			EXPECT_FLOAT_EQ(quads[0].uvMin.y, 11.5F / 48.0F) << em;
			EXPECT_FLOAT_EQ(quads[0].uvMax.y, 43.5F / 48.0F) << em;
			EXPECT_FLOAT_EQ(quads[0].pixelRange, 8.0F) << em;

			// The space has only an advance: the second A is an A's and a space's advance on.
			EXPECT_FLOAT_EQ(quads[1].position.x - quads[0].position.x, (0.5F + 0.25F) * 16.0F) << em;
			EXPECT_EQ(fontRenderer->MeasureText("A A", 1.0F), glm::vec2((0.5F + 0.25F + 0.5F) * 16.0F, 1.25F * 16.0F)) << em;
		}
	}

	TEST_F(FontRendererInitializeTest, BothYOriginsLoadTheSameGlyphs) {
		const std::array sources{folder.write(kBody, "bottom"), folder.write(kMono, "top", {.yOriginTop = true})};
		ASSERT_TRUE(fontRenderer->Initialize(sources, kBody));
		EXPECT_FLOAT_EQ(fontRenderer->getAscent(1.0F, kMono), fontRenderer->getAscent(1.0F, kBody));
		const std::string text = "Hello, caf\xC3\xA9 \xF0\x9F\x98\x80";
		EXPECT_EQ(fontRenderer->MeasureText(text, 1.0F, kMono), fontRenderer->MeasureText(text, 1.0F, kBody));
		const auto top = quadsFor(text, 1.0F, 0.0F, kMono);
		const auto bottom = quadsFor(text, 1.0F, 0.0F, kBody);
		ASSERT_EQ(top.size(), bottom.size());
		for (size_t i = 0; i < top.size(); ++i) {
			EXPECT_EQ(top[i].position, bottom[i].position) << "glyph " << i;
			EXPECT_EQ(top[i].size, bottom[i].size) << "glyph " << i;
			EXPECT_EQ(top[i].uvMin, bottom[i].uvMin) << "glyph " << i;
			EXPECT_EQ(top[i].uvMax, bottom[i].uvMax) << "glyph " << i;
		}
	}

	TEST_F(FontRendererInitializeTest, KerningAppliesInMeasuringLayingOutAndWrapping) {
		// A then V closes up a tenth of an em, and V then a space a twentieth; V then A is unkerned.
		const AtlasMetrics kerned{
			.kerning = {{.left = U'A', .right = U'V', .advance = -0.1F}, {.left = U'V', .right = U' ', .advance = -0.05F}}
		};
		ASSERT_TRUE(fontRenderer->Initialize(std::array{folder.write(kBody, "body", kerned), folder.write(kMono, "plain")}, kBody));
		const float a = advanceOf(boxOf(U'A'), 1.0F) * 16.0F;
		const float v = advanceOf(boxOf(U'V'), 1.0F) * 16.0F;
		const float space = advanceOf(boxOf(U' '), 1.0F) * 16.0F;

		EXPECT_NEAR(fontRenderer->MeasureText("AV", 1.0F).x, a + v - 1.6F, 1e-4F);
		EXPECT_NEAR(fontRenderer->MeasureText("VA", 1.0F).x, a + v, 1e-4F);
		EXPECT_NEAR(fontRenderer->MeasureText("AV", 1.0F, kMono).x, a + v, 1e-4F);
		const auto quads = quadsFor("AV");
		ASSERT_EQ(quads.size(), 2U);
		EXPECT_NEAR(quads[1].position.x - quads[0].position.x, a - 1.6F, 1e-4F);
		// With letter spacing too, both go between the glyphs.
		EXPECT_NEAR(fontRenderer->MeasureText("AV", 1.0F, kBody, 2.0F).x, a + v - 1.6F + 2.0F, 1e-4F);

		// "AV AV" is 4 px narrower kerned (two A-Vs and a V-space), so it fits a width the
		// sum of its words and the space doesn't, and one line measures what it draws.
		const float kernedWidth = fontRenderer->MeasureText("AV AV", 1.0F).x;
		EXPECT_NEAR(kernedWidth, 2.0F * (a + v) + space - 4.0F, 1e-4F);
		const FontRenderer::WrappedTextResult& wrapped = fontRenderer->wrapText("AV AV", 1.0F, kernedWidth + 0.5F);
		ASSERT_EQ(wrapped.lines.size(), 1U);
		EXPECT_FLOAT_EQ(wrapped.totalWidth, kernedWidth);
		EXPECT_EQ(fontRenderer->wrapText("AV AV", 1.0F, kernedWidth - 0.5F).lines.size(), 2U);
	}

	TEST_F(FontRendererInitializeTest, ABadAtlasFailsToLoadWithItsReasonLogged) {
		// Each a file msdf-atlas-gen didn't write, or one it wrote that this can't draw, or a PNG that
		// isn't the JSON's; each fails Initialize, as the default, naming the file and why.
		const std::string json = oneGlyphJson();
		const std::string glyphA = R"({"unicode":65,)";
		struct Bad {
			std::string json;
			int			pngWidth;
			std::string reason;
		};
		const Bad bad[] = {
			{.json = R"({"atlas":{"type":"msdf","distanceRange":4,"size":32,"width":64,"height":48},"metrics":{"emSize":1,"ascender":1,)"
					 R"("descender":-0.3,"lineHeight":1.3},"glyphs":{"A":{"advance":0.5}}})",
			 .pngWidth = 64,
			 .reason = "scripts/convert-font-atlas.mjs"},
			{.json = replaced(json, glyphA, R"({"index":36,)"), .pngWidth = 64, .reason = "glyph index"},
			{.json = replaced(json, R"("type":"mtsdf")", R"("type":"hardmask")"), .pngWidth = 64, .reason = "atlas.type"},
			{.json = replaced(json, R"("yOrigin":"top")", R"("yOrigin":"middle")"), .pngWidth = 64, .reason = "atlas.yOrigin"},
			{.json = replaced(json, R"("distanceRangeMiddle":0)", R"("distanceRangeMiddle":0.5)"),
			 .pngWidth = 64,
			 .reason = "distanceRangeMiddle"},
			{.json = replaced(json, R"(,"atlasBounds":{"left":8.5,"top":4.5,"right":40.5,"bottom":36.5})", ""),
			 .pngWidth = 64,
			 .reason = "U+0041 has planeBounds and no atlasBounds"},
			{.json = replaced(json, R"("right":40.5)", R"("right":64.5)"), .pngWidth = 64, .reason = "U+0041's atlasBounds"},
			{.json = replaced(json, R"({"unicode":32,)", R"({"unicode":65,)"), .pngWidth = 64, .reason = "U+0041 is given twice"},
			{.json = replaced(json, R"("glyphs":[)", R"("variants":[],"glyphs":[)"), .pngWidth = 64, .reason = "several fonts"},
			{.json = json, .pngWidth = 32, .reason = "is for a 64x48 atlas, and its PNG is 32x48"},
		};
		for (const Bad& sample : bad) {
			const UiLog errors(foundation::LogLevel::Error);
			EXPECT_FALSE(fontRenderer->Initialize(std::array{folder.writeJson(kBody, "bad", sample.json, sample.pngWidth, 48)}, kBody))
				<< sample.reason;
			expectNothingLoaded();
			ASSERT_FALSE(errors.messages.empty()) << sample.reason;
			EXPECT_TRUE(errors.messages[0].starts_with("SDF atlas bad.json: ")) << errors.messages[0];
			EXPECT_NE(errors.messages[0].find(sample.reason), std::string::npos) << errors.messages[0];
		}
		ASSERT_TRUE(fontRenderer->Initialize(std::array{folder.writeJson(kBody, "good", json, 64, 48)}, kBody));
	}

	// --- Initialize's contract ---

	TEST_F(FontRendererInitializeTest, NoSourcesFail) {
		EXPECT_FALSE(fontRenderer->Initialize({}, kBody));
		expectNothingLoaded();
		EXPECT_EQ(atlasTexturesMade(), 0);
	}

	TEST_F(FontRendererInitializeTest, AFamilyGivenTwiceFailsBeforeAnythingLoads) {
		const std::array sources{folder.write(kBody, "body"), folder.write(kMono, "mono"), folder.write(kBody, "body-again")};
		EXPECT_FALSE(fontRenderer->Initialize(sources, kBody));
		expectNothingLoaded();
		EXPECT_EQ(atlasTexturesMade(), 0);
	}

	TEST_F(FontRendererInitializeTest, TheDefaultMustBeOneOfTheSources) {
		const std::array sources{folder.write(kBody, "body")};
		EXPECT_FALSE(fontRenderer->Initialize(sources, kMono));
		expectNothingLoaded();
		EXPECT_FALSE(fontRenderer->Initialize(sources, FontFamily::Default));
		expectNothingLoaded();
		EXPECT_EQ(atlasTexturesMade(), 0);
	}

	TEST_F(FontRendererInitializeTest, FontFamilyDefaultIsNotAFamilyToLoad) {
		const std::array sources{folder.write(kBody, "body"), folder.write(FontFamily::Default, "default")};
		EXPECT_FALSE(fontRenderer->Initialize(sources, kBody));
		expectNothingLoaded();
		EXPECT_EQ(atlasTexturesMade(), 0);
	}

	TEST_F(FontRendererInitializeTest, ADefaultThatDoesNotLoadFailsAndReleasesTheRest) {
		const FontAtlasSource mono = folder.write(kMono, "mono");
		const FontAtlasSource body = folder.write(kBody, "body");
		std::ofstream(folder.file("broken.json")) << R"({"atlas":{"distanceRange":4},"metrics":)";

		const FontAtlasSource broken[] = {
			{.family = kBody, .pngPath = folder.file("absent.png"), .jsonPath = folder.file("absent.json")},
			{.family = kBody, .pngPath = folder.file("absent.png"), .jsonPath = body.jsonPath},
			{.family = kBody, .pngPath = body.pngPath, .jsonPath = folder.file("broken.json")},
		};
		for (const FontAtlasSource& source : broken) {
			// The default loads last, after mono has made its texture.
			const int madeBefore = atlasTexturesMade();
			EXPECT_FALSE(fontRenderer->Initialize(std::array{mono, source}, kBody)) << source.pngPath << " " << source.jsonPath;
			EXPECT_EQ(atlasTexturesMade() - madeBefore, 1);
			expectNothingLoaded();
			EXPECT_FALSE(fontRenderer->getAtlasTexture(kMono).isValid());
		}
	}

	TEST_F(FontRendererInitializeTest, ADefaultThatDoesNotLoadStopsTheLoading) {
		const std::array sources{
			FontAtlasSource{.family = kBody, .pngPath = folder.file("absent.png"), .jsonPath = folder.file("absent.json")},
			folder.write(kMono, "mono"),
		};
		EXPECT_FALSE(fontRenderer->Initialize(sources, kBody));
		EXPECT_EQ(atlasTexturesMade(), 0); // mono, after the default, never loads
		expectNothingLoaded();
	}

	TEST_F(FontRendererInitializeTest, AFamilyThatDoesNotLoadDrawsWithTheDefault) {
		const std::array sources{
			folder.write(kBody, "body"),
			FontAtlasSource{.family = kMono, .pngPath = folder.file("absent.png"), .jsonPath = folder.file("absent.json")},
		};
		ASSERT_TRUE(fontRenderer->Initialize(sources, kBody));
		EXPECT_EQ(atlasTextures(), 1U);

		const TextureHandle body = fontRenderer->getAtlasTexture(kBody);
		ASSERT_TRUE(body.isValid());
		EXPECT_EQ(fontRenderer->getAtlasTexture(kMono), body);
		EXPECT_EQ(fontRenderer->getAtlasTexture(FontFamily{7}), body); // never given
		EXPECT_EQ(fontRenderer->getAtlasTexture(), body);
		EXPECT_EQ(fontRenderer->MeasureText("Hello", 1.0F, kMono), fontRenderer->MeasureText("Hello", 1.0F, kBody));
	}

	TEST_F(FontRendererInitializeTest, EveryNameForOneAtlasSharesItsCacheEntries) {
		const std::array sources{
			folder.write(kBody, "body"),
			FontAtlasSource{.family = kMono, .pngPath = folder.file("absent.png"), .jsonPath = folder.file("absent.json")},
		};
		ASSERT_TRUE(fontRenderer->Initialize(sources, kBody));

		// The default, the default by id, a family that did not load and one never given.
		for (const FontFamily family : {FontFamily::Default, kBody, kMono, FontFamily{9}}) {
			std::vector<FontRenderer::GlyphQuad> quads;
			fontRenderer->generateGlyphQuads("Hello", glm::vec2(0.0F), 1.0F, glm::vec4(1.0F), quads, family);
			EXPECT_EQ(quads.size(), 5U);
		}
		EXPECT_EQ(fontRenderer->getGlyphQuadCacheSize(), 1U);
	}

	// --- Glyph quads and the atlas they sample ---

	TEST_F(FontRendererInitializeTest, GlyphQuadsCarryTheAtlasTheirFamilyLaidThemOutIn) {
		ASSERT_TRUE(fontRenderer->Initialize(std::array{folder.write(kBody, "body"), folder.write(kMono, "mono")}, kBody));
		const TextureHandle body = fontRenderer->getAtlasTexture(kBody);
		const TextureHandle mono = fontRenderer->getAtlasTexture(kMono);
		ASSERT_NE(body, mono);

		// The second pass comes from the glyph-quad cache. A family never given lays out in the default's atlas.
		for (int pass = 0; pass < 2; ++pass) {
			for (const auto& [family, atlas] :
				 {std::pair{kMono, mono}, std::pair{kBody, body}, std::pair{FontFamily::Default, body}, std::pair{FontFamily{9}, body}}) {
				std::vector<FontRenderer::GlyphQuad> quads;
				fontRenderer->generateGlyphQuads("Hello", glm::vec2(0.0F), 1.0F, glm::vec4(1.0F), quads, family);
				ASSERT_EQ(quads.size(), 5U);
				for (const FontRenderer::GlyphQuad& quad : quads) {
					EXPECT_EQ(quad.atlas, atlas) << "family " << static_cast<int>(family) << " pass " << pass;
				}
			}
		}
	}

	TEST_F(FontRendererInitializeTest, OneDrawGlyphsCallSamplesEachQuadsOwnAtlas) {
		ASSERT_TRUE(fontRenderer->Initialize(std::array{folder.write(kBody, "body"), folder.write(kMono, "mono")}, kBody));
		const TextureHandle body = fontRenderer->getAtlasTexture(kBody);
		const TextureHandle mono = fontRenderer->getAtlasTexture(kMono);
		Primitives::setFontAtlas(body);

		// Two families in one call: nothing to name but the quads.
		std::vector<FontRenderer::GlyphQuad> quads;
		fontRenderer->generateGlyphQuads("Mono", glm::vec2(0.0F), 1.0F, glm::vec4(1.0F), quads, kMono);
		fontRenderer->generateGlyphQuads("Body", glm::vec2(0.0F, 20.0F), 1.0F, glm::vec4(1.0F), quads, kBody);
		ASSERT_EQ(quads.size(), 8U);

		Primitives::setViewport(320, 200);
		Primitives::beginFrame();
		Primitives::drawGlyphs(quads.data(), quads.size());
		Primitives::endFrame();

		// One draw with both atlases bound, each glyph sampling its own.
		const std::vector<backend::NullBackend::RecordedDraw>& draws = nullBackend->draws();
		ASSERT_EQ(draws.size(), 1U);
		EXPECT_EQ(draws[0].call.textures[0], mono);
		EXPECT_EQ(draws[0].call.textures[1], body);
		EXPECT_EQ(draws[0].call.instanceCount, 8U); // eight glyphs, a quad each
	}

	TEST_F(FontRendererInitializeTest, EachQuadDrawsAtItsOwnAtlassDistanceRange) {
		// Body at a range of 4, mono at 2, and setFontAtlas's 6 for a quad with no atlas.
		ASSERT_TRUE(
			fontRenderer->Initialize(std::array{folder.write(kBody, "body"), folder.write(kMono, "mono", {.distanceRange = 2.0F})}, kBody)
		);
		Primitives::setFontAtlas(fontRenderer->getAtlasTexture(kBody), 6.0F);

		std::vector<FontRenderer::GlyphQuad> quads;
		fontRenderer->generateGlyphQuads("Mo", glm::vec2(0.0F), 1.0F, glm::vec4(1.0F), quads, kMono);
		fontRenderer->generateGlyphQuads("Bo", glm::vec2(0.0F, 20.0F), 1.0F, glm::vec4(1.0F), quads, kBody);
		ASSERT_EQ(quads.size(), 4U);
		EXPECT_EQ(quads[0].pixelRange, 2.0F);
		EXPECT_EQ(quads[2].pixelRange, 4.0F);
		FontRenderer::GlyphQuad handBuilt = quads.back();
		handBuilt.atlas = {};
		quads.push_back(handBuilt);

		Primitives::setViewport(320, 200);
		Primitives::beginFrame();
		Primitives::drawGlyphs(quads.data(), quads.size());
		Primitives::endFrame();

		// One upload; its quads run in submission order, each with its range in shape.z.
		const std::vector<backend::NullBackend::RecordedDraw>& draws = nullBackend->draws();
		ASSERT_FALSE(draws.empty());
		const std::span<const uint8_t> bytes = nullBackend->bufferContents(nullBackend->geometry(draws[0].call.geometry)->vertices);
		std::vector<BatchInstance>	   glyphs(bytes.size() / sizeof(BatchInstance));
		std::memcpy(glyphs.data(), bytes.data(), glyphs.size() * sizeof(BatchInstance));
		ASSERT_EQ(glyphs.size(), 5U);
		const float expected[] = {2.0F, 2.0F, 4.0F, 4.0F, 6.0F};
		for (size_t i = 0; i < glyphs.size(); ++i) {
			EXPECT_EQ(glm::unpackHalf2x16(glyphs[i].shape[1]).x, expected[i]) << "glyph " << i;
		}
	}

	TEST_F(FontRendererInitializeTest, WrappedTextLaysOutInTheFamilyItWasWrappedIn) {
		// Mono's advances are wider and its lines taller than body's, the default.
		ASSERT_TRUE(fontRenderer->Initialize(
			std::array{folder.write(kBody, "body"), folder.write(kMono, "mono", {.lineHeight = 1.5F, .advanceScale = 1.4F})}, kBody
		));
		const std::string text = "wrap these words";
		EXPECT_EQ(fontRenderer->wrapText(text, 1.0F, 100.0F).family, kBody);
		const FontRenderer::WrappedTextResult& wrapped = fontRenderer->wrapText(text, 1.0F, 100.0F, kMono);
		ASSERT_GE(wrapped.lines.size(), 2U);
		EXPECT_EQ(wrapped.family, kMono);
		EXPECT_FLOAT_EQ(wrapped.lineHeight, 1.5F * 16.0F);

		std::vector<FontRenderer::GlyphQuad> quads;
		fontRenderer->generateWrappedGlyphQuads(wrapped, glm::vec2(0.0F), glm::vec4(1.0F), Foundation::HorizontalAlign::Left, 0.0F, quads);

		// Each line as generateGlyphQuads lays it out in mono, a mono line below the one before.
		std::vector<FontRenderer::GlyphQuad> expected;
		for (size_t i = 0; i < wrapped.lines.size(); ++i) {
			const glm::vec2 lineOrigin(0.0F, static_cast<float>(i) * wrapped.lineHeight);
			fontRenderer->generateGlyphQuads(wrapped.lines[i], lineOrigin, 1.0F, glm::vec4(1.0F), expected, kMono);
		}
		ASSERT_EQ(quads.size(), expected.size());
		const TextureHandle mono = fontRenderer->getAtlasTexture(kMono);
		for (size_t i = 0; i < quads.size(); ++i) {
			EXPECT_EQ(quads[i].atlas, mono) << "glyph " << i;
			EXPECT_EQ(quads[i].position, expected[i].position) << "glyph " << i;
		}
	}

	TEST_F(FontRendererInitializeTest, TheDefaultIsTheCallersChoice) {
		const std::array sources{
			folder.write(kBody, "body"),
			folder.write(kMono, "mono", {.ascender = 0.8F, .lineHeight = 1.5F, .advanceScale = 1.4F}),
		};
		ASSERT_TRUE(fontRenderer->Initialize(sources, kMono));
		EXPECT_EQ(atlasTextures(), 2U);

		const glm::vec2 body = fontRenderer->MeasureText("Hello", 1.0F, kBody);
		const glm::vec2 mono = fontRenderer->MeasureText("Hello", 1.0F, kMono);
		ASSERT_NE(body, mono);
		EXPECT_EQ(fontRenderer->MeasureText("Hello", 1.0F), mono);
		EXPECT_EQ(fontRenderer->MeasureText("Hello", 1.0F, FontFamily{1}), mono); // never given
		EXPECT_FLOAT_EQ(fontRenderer->getAscent(1.0F), fontRenderer->getAscent(1.0F, kMono));
		EXPECT_EQ(fontRenderer->getAtlasTexture(), fontRenderer->getAtlasTexture(kMono));
		EXPECT_NE(fontRenderer->getAtlasTexture(kBody), fontRenderer->getAtlasTexture(kMono));
	}

	TEST_F(FontRendererInitializeTest, ASecondInitializeReplacesTheFirst) {
		const FontAtlasSource body = folder.write(kBody, "body");
		const FontAtlasSource mono = folder.write(kMono, "mono", {.advanceScale = 1.4F});
		ASSERT_TRUE(fontRenderer->Initialize(std::array{body, mono}, kBody));
		EXPECT_EQ(atlasTextures(), 2U);
		const float							 bodyWidth = fontRenderer->wrapText("Hello", 1.0F, 0.0F).totalWidth;
		std::vector<FontRenderer::GlyphQuad> bodyQuads;
		fontRenderer->generateGlyphQuads("Hello", glm::vec2(0.0F), 1.0F, glm::vec4(1.0F), bodyQuads);
		ASSERT_FALSE(bodyQuads.empty());

		// Only mono now, and the default: body resolves to it, and neither cache hands back body's text.
		ASSERT_TRUE(fontRenderer->Initialize(std::array{mono}, kMono));
		EXPECT_EQ(atlasTextures(), 1U);
		EXPECT_EQ(fontRenderer->getAtlasTexture(kBody), fontRenderer->getAtlasTexture(kMono));
		EXPECT_NE(fontRenderer->wrapText("Hello", 1.0F, 0.0F).totalWidth, bodyWidth);
		std::vector<FontRenderer::GlyphQuad> monoQuads;
		fontRenderer->generateGlyphQuads("Hello", glm::vec2(0.0F), 1.0F, glm::vec4(1.0F), monoQuads);
		ASSERT_EQ(monoQuads.size(), bodyQuads.size());
		EXPECT_NE(monoQuads.back().position.x, bodyQuads.back().position.x);

		EXPECT_FALSE(fontRenderer->Initialize({}, kBody));
		expectNothingLoaded();

		ASSERT_TRUE(fontRenderer->Initialize(std::array{body}, kBody));
		EXPECT_EQ(atlasTextures(), 1U);
		fontRenderer.reset();
		EXPECT_EQ(atlasTextures(), 0U);
	}

	TEST_F(FontRendererInitializeTest, TextLaidOutAgainAfterASecondInitializeCarriesTheNewAtlas) {
		// The same family from the same file, so the glyph-quad cache key is the same both times: only
		// clearing the cache keeps the old atlas, destroyed by now, out of the new quads.
		const FontAtlasSource body = folder.write(kBody, "body");
		ASSERT_TRUE(fontRenderer->Initialize(std::array{body}, kBody));
		std::vector<FontRenderer::GlyphQuad> before;
		fontRenderer->generateGlyphQuads("Hello", glm::vec2(0.0F), 1.0F, glm::vec4(1.0F), before, kBody);
		ASSERT_FALSE(before.empty());
		const TextureHandle first = fontRenderer->getAtlasTexture(kBody);
		EXPECT_EQ(before.front().atlas, first);

		ASSERT_TRUE(fontRenderer->Initialize(std::array{body}, kBody));
		const TextureHandle second = fontRenderer->getAtlasTexture(kBody);
		ASSERT_NE(second, first);
		std::vector<FontRenderer::GlyphQuad> after;
		fontRenderer->generateGlyphQuads("Hello", glm::vec2(0.0F), 1.0F, glm::vec4(1.0F), after, kBody);
		ASSERT_EQ(after.size(), before.size());
		for (const FontRenderer::GlyphQuad& quad : after) {
			EXPECT_EQ(quad.atlas, second);
		}
	}

	// --- Glyphs reading into each other ---

	TEST_F(FontRendererInitializeTest, APackedAtlasWhoseReadsMeetHasNoCrampedGlyphs) {
		// Boxes touch, and each quad reads half a texel past atlasBounds inset half a texel: to the box's edge.
		ASSERT_TRUE(fontRenderer->Initialize(std::array{folder.write(kBody, "body")}, kBody));
		EXPECT_EQ(fontRenderer->crampedGlyphs(), "");
		EXPECT_EQ(fontRenderer->crampedGlyphs(kBody), "");
	}

	TEST_F(FontRendererInitializeTest, AGlyphReadingIntoAnothersIsCrampedAndSoIsTheOther) {
		// '(' moves a fifth of a texel right into ')', '[' left into 'Z', '}' down into the 'm' below it
		// and é left into '~'; '{' moves 0.005 texels right, inside the tolerance for the JSON's rounding.
		const AtlasMetrics shifted{
			.shift = {
				{U'(', {0.2F, 0.0F}},
				{U'[', {-0.2F, 0.0F}},
				{U'}', {0.0F, -0.2F}},
				{0xE9, {-0.2F, 0.0F}},
				{U'{', {0.005F, 0.0F}},
			}
		};
		const std::array sources{
			folder.write(kBody, "body", shifted), folder.write(kMono, "mono", {.distanceRange = 8.0F, .shift = shifted.shift})
		};
		ASSERT_TRUE(fontRenderer->Initialize(sources, kBody)); // warned about, still loaded
		EXPECT_EQ(fontRenderer->crampedGlyphs(kBody), "()Z[m}~\xC3\xA9");
		EXPECT_EQ(fontRenderer->crampedGlyphs(kMono), "()Z[m}~\xC3\xA9"); // the range doesn't move the read

		std::vector<FontRenderer::GlyphQuad> quads;
		fontRenderer->generateGlyphQuads("()", glm::vec2(0.0F), 1.0F, glm::vec4(1.0F), quads, kBody);
		EXPECT_EQ(quads.size(), 2U);
	}

#ifdef DEVELOPMENT_BUILD
	TEST_F(FontRendererInitializeTest, LoadingWarnsOncePerCrampedAtlasNamingItsGlyphs) {
		const UiLog captured(foundation::LogLevel::Warning);

		// Two cramped atlases and a roomy one between them, at a range wide enough for small text.
		constexpr FontFamily kDisplay{5};
		const std::array	 sources{
			folder.write(kBody, "body", {.distanceRange = 6.0F, .shift = {{U'(', {0.2F, 0.0F}}}}),
			folder.write(kMono, "mono", {.distanceRange = 6.0F}),
			folder.write(kDisplay, "display", {.distanceRange = 6.0F, .shift = {{U'[', {-0.2F, 0.0F}}}}),
		};
		ASSERT_TRUE(fontRenderer->Initialize(sources, kBody));
		ASSERT_EQ(captured.messages.size(), 2U);
		EXPECT_TRUE(captured.messages[0].starts_with("SDF atlas body.json: ")) << captured.messages[0];
		EXPECT_TRUE(captured.messages[0].ends_with(": ()")) << captured.messages[0];
		EXPECT_TRUE(captured.messages[1].starts_with("SDF atlas display.json: ")) << captured.messages[1];
		EXPECT_TRUE(captured.messages[1].ends_with(": Z[")) << captured.messages[1];
	}

	TEST_F(FontRendererInitializeTest, AnAtlasWhoseRangeIsUnderASixthOfItsEmWarns) {
		const UiLog captured(foundation::LogLevel::Warning);

		// The test atlases' em is 32 texels: a range of 4 is under a sixth of it, 6 is not.
		ASSERT_TRUE(
			fontRenderer->Initialize(std::array{folder.write(kBody, "body", {.distanceRange = 6.0F}), folder.write(kMono, "mono")}, kBody)
		);
		ASSERT_EQ(captured.messages.size(), 1U);
		EXPECT_EQ(
			captured.messages[0],
			"SDF atlas mono.json: a distance range of 4.0 texels is under a sixth of its 32-texel em, too thin for small text; generate "
			"it with 6 or more"
		);
	}
#endif

} // namespace Renderer
