// FontRenderer on the null backend, with atlases written to a temporary folder.
//
// MeasureText and generateGlyphQuads must agree on advances (one shared
// iteration), or centered/right-aligned text drifts against its measured box.
// The key assertion renders a doubled string (s+s): the second copy's quads
// must sit exactly MeasureText(s) + letterSpacing to the right of the first
// copy's, which proves measured width == rendered pen advance, including the
// '?' fallback for characters missing from the atlas.
//
// Initialize takes its atlases from the caller: the contract tests check what it
// refuses, that a refusal leaves nothing loaded, and what a family that is not
// loaded draws with.
//
// The atlases are synthetic, in the JSON layout worldsim's generate_sdf_atlas
// writes: printable ASCII as boxes on a 16 x 6 grid of 32 px cells, each glyph
// with an advance of its own, and a one-texel PNG. These tests read metrics, not
// outlines, so no font goes into them.

#include "font/FontRenderer.h"
#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "primitives/Primitives.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace Renderer {

	namespace {

		constexpr FontFamily kBody{0};
		constexpr FontFamily kMono{3};

		// stbi_load needs a real image; the null backend keeps none of its pixels.
		constexpr unsigned char kOneTexelPng[] = {
			0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
			0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00,
			0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0xc8,
			0xea, 0xeb, 0xf9, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
		};

		struct AtlasMetrics {
			float ascender = 0.9F;
			float lineHeight = 1.2F;
			float advanceScale = 1.0F; // Every advance, and the glyph boxes with them
		};

		std::string number(float value) {
			return std::to_string(value);
		}

		// Space has no geometry; every other glyph is a box in its own cell.
		std::string atlasJson(const AtlasMetrics& metrics) {
			std::string json = R"({"atlas":{"type":"msdf","distanceRange":4,"size":32,"width":512,"height":192},)";
			json += R"("metrics":{"emSize":1,"ascender":)" + number(metrics.ascender) + R"(,"descender":-0.25,"lineHeight":)" +
					number(metrics.lineHeight) + R"(},"glyphs":{)";
			for (int c = ' '; c <= '~'; ++c) {
				const int	cell = c - ' ';
				const float advance = (0.45F + 0.03F * static_cast<float>(cell % 9)) * metrics.advanceScale;
				std::string key(1, static_cast<char>(c));
				if (c == '"' || c == '\\') {
					key = "\\" + key;
				}
				json += (c == ' ' ? "\"" : ",\"") + key + "\":{";
				if (c == ' ') {
					json += R"("atlas":null,"atlasBounds":null,"plane":null,)";
				} else {
					const float col = static_cast<float>(cell % 16);
					const float row = static_cast<float>(cell / 16);
					json += R"("atlas":{"x":)" + number(col / 16.0F) + R"(,"y":)" + number(row / 6.0F) + R"(,"width":0.0625,"height":)" +
							number(1.0F / 6.0F) + "},";
					json += R"("atlasBounds":{"left":)" + number((col * 32.0F + 4.0F) / 512.0F) + R"(,"bottom":)" +
							number((row * 32.0F + 4.0F) / 192.0F) + R"(,"right":)" + number((col * 32.0F + 28.0F) / 512.0F) + R"(,"top":)" +
							number((row * 32.0F + 28.0F) / 192.0F) + "},";
					json += R"("plane":{"left":0.05,"bottom":-0.1,"right":)" + number(advance - 0.05F) + R"(,"top":0.7},)";
				}
				json += R"("advance":)" + number(advance) + "}";
			}
			json += "}}";
			return json;
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
				const FontAtlasSource source{.family = family, .pngPath = file(name + ".png"), .jsonPath = file(name + ".json")};
				std::ofstream(source.jsonPath, std::ios::binary) << atlasJson(metrics);
				std::ofstream(source.pngPath, std::ios::binary)
					.write(reinterpret_cast<const char*>(kOneTexelPng), static_cast<std::streamsize>(sizeof(kOneTexelPng)));
				return source;
			}

			// A path in the folder, which may name nothing.
			std::string file(const std::string& name) const { return (path / name).string(); }

		  private:
			std::filesystem::path path;
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

			std::vector<FontRenderer::GlyphQuad> quadsFor(const std::string& text, float scale, float letterSpacing) const {
				std::vector<FontRenderer::GlyphQuad> quads;
				fontRenderer->generateGlyphQuads(text, glm::vec2(0.0F, 0.0F), scale, glm::vec4(1.0F), quads, kBody, letterSpacing);
				return quads;
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
		// fallback inside the doubled-string advance check).
		const std::string missingChar = std::string("mi") + '\x01' + "ssing";
		const std::string testStrings[] = {"Hello, World!", "iiiillll", "A B C", missingChar};
		const float		  letterSpacings[] = {0.0F, 2.5F};
		const float		  scales[] = {1.0F, 1.25F};

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

	TEST_F(FontRendererTest, MissingGlyphMeasuresAndRendersAsFallback) {
		const std::string withMissing = std::string("a") + '\x01' + "b";
		const std::string withFallback = "a?b";

		EXPECT_FLOAT_EQ(fontRenderer->MeasureText(withMissing, 1.0F).x, fontRenderer->MeasureText(withFallback, 1.0F).x);

		const auto quadsMissing = quadsFor(withMissing, 1.0F, 0.0F);
		const auto quadsFallback = quadsFor(withFallback, 1.0F, 0.0F);
		ASSERT_EQ(quadsMissing.size(), quadsFallback.size());
		for (size_t i = 0; i < quadsMissing.size(); ++i) {
			EXPECT_FLOAT_EQ(quadsMissing[i].position.x, quadsFallback[i].position.x);
			EXPECT_FLOAT_EQ(quadsMissing[i].position.y, quadsFallback[i].position.y);
			EXPECT_FLOAT_EQ(quadsMissing[i].uvMin.x, quadsFallback[i].uvMin.x);
			EXPECT_FLOAT_EQ(quadsMissing[i].uvMax.x, quadsFallback[i].uvMax.x);
		}
	}

	TEST_F(FontRendererTest, LetterSpacingSitsBetweenGlyphsOnly) {
		const std::string text = "spacing";
		const float		  letterSpacing = 3.0F;
		const float		  base = fontRenderer->MeasureText(text, 1.0F).x;
		const float		  spaced = fontRenderer->MeasureText(text, 1.0F, kBody, letterSpacing).x;
		EXPECT_NEAR(spaced - base, letterSpacing * static_cast<float>(text.size() - 1), 1e-3F);

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

} // namespace Renderer
