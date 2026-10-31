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
// A glyph quad's UVs index one family's atlas, so each quad carries that atlas and
// drawGlyphs samples it: there is no second place to name a family, and so no way
// to draw one family's quads with another's atlas.
//
// A quad reads its atlas past the glyph's bounds, and the atlas generator has to
// leave that much room in each glyph's cell; loading checks it, and crampedGlyphs
// says what it found.
//
// The atlases are synthetic, in the JSON layout worldsim's generate_sdf_atlas
// writes: printable ASCII as boxes on a 16 x 6 grid of 32 px cells, each glyph
// with an advance of its own, and a one-texel PNG. These tests read metrics, not
// outlines, so no font goes into them.

#include "font/FontRenderer.h"
#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "primitives/Primitives.h"
#include "utils/Log.h"

#include <glm/gtc/packing.hpp>
#include <gtest/gtest.h>

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

		// stbi_load needs a real image; the null backend keeps none of its pixels.
		constexpr unsigned char kOneTexelPng[] = {
			0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
			0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00,
			0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0xc8,
			0xea, 0xeb, 0xf9, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
		};

		// Texels between a glyph and each edge of its cell.
		struct Room {
			float left = 4.0F;
			float bottom = 4.0F;
			float right = 4.0F;
			float top = 4.0F;
		};

		constexpr Room roomAround(float texels) {
			return {.left = texels, .bottom = texels, .right = texels, .top = texels};
		}

		struct AtlasMetrics {
			float				 ascender = 0.9F;
			float				 lineHeight = 1.2F;
			float				 advanceScale = 1.0F;  // Every advance, and the glyph boxes with them
			float				 distanceRange = 4.0F; // Texels
			std::map<char, Room> room;				   // Where not 4 texels on every side
			std::string			 cellless;			   // Glyphs whose "atlas" gives no cell rect
			std::string			 oddCells;			   // Glyphs whose cell has fields that aren't numbers
		};

		std::string number(float value) {
			return std::to_string(value);
		}

		// Space has no geometry; every other glyph is a box in its own cell.
		std::string atlasJson(const AtlasMetrics& metrics) {
			std::string json =
				R"({"atlas":{"type":"msdf","distanceRange":)" + number(metrics.distanceRange) + R"(,"size":32,"width":512,"height":192},)";
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
					const auto	room = metrics.room.find(static_cast<char>(c));
					const Room	inset = room != metrics.room.end() ? room->second : Room{};
					if (metrics.cellless.find(static_cast<char>(c)) != std::string::npos) {
						json += R"("atlas":{},)";
					} else if (metrics.oddCells.find(static_cast<char>(c)) != std::string::npos) {
						json += R"("atlas":{"x":null,"y":0,"width":"wide","height":0.0625},)";
					} else {
						json += R"("atlas":{"x":)" + number(col / 16.0F) + R"(,"y":)" + number(row / 6.0F) +
								R"(,"width":0.0625,"height":)" + number(1.0F / 6.0F) + "},";
					}
					json += R"("atlasBounds":{"left":)" + number((col * 32.0F + inset.left) / 512.0F) + R"(,"bottom":)" +
							number((row * 32.0F + inset.bottom) / 192.0F) + R"(,"right":)" +
							number((col * 32.0F + 32.0F - inset.right) / 512.0F) + R"(,"top":)" +
							number((row * 32.0F + 32.0F - inset.top) / 192.0F) + "},";
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

		const std::vector<backend::NullBackend::RecordedDraw>& draws = nullBackend->draws();
		ASSERT_EQ(draws.size(), 2U);
		EXPECT_EQ(draws[0].call.texture, mono);
		EXPECT_EQ(draws[0].call.instanceCount, 4U); // four glyphs, a quad each
		EXPECT_EQ(draws[1].call.texture, body);
		EXPECT_EQ(draws[1].call.instanceCount, 4U);
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

	// --- Room around each glyph in its cell ---

	TEST_F(FontRendererInitializeTest, ARoomyAtlasHasNoCrampedGlyphs) {
		// Four texels around every glyph, and a quad reads 2 + 0.5 past it at a distance range of 4.
		ASSERT_TRUE(fontRenderer->Initialize(std::array{folder.write(kBody, "body")}, kBody));
		EXPECT_EQ(fontRenderer->crampedGlyphs(), "");
		EXPECT_EQ(fontRenderer->crampedGlyphs(kBody), "");
	}

	TEST_F(FontRendererInitializeTest, AGlyphWithLessRoomThanItsQuadReadsIsCramped) {
		// The reach is half the distance range plus half a texel: 2.5 texels at a range of 4, and 1.5
		// at a range of 2. Each of '(', ')', '[' and ']' is short on one edge only: the top (as a tall
		// glyph is), the bottom, the left and the right. '{' has exactly the reach, and '|' reads
		// 0.003 texels past its cell, inside the tolerance for the JSON's rounding.
		const std::map<char, Room> room{
			{'(', {.top = 1.0F}},
			{')', {.bottom = 1.0F}},
			{'[', {.left = 2.4F}},
			{']', {.right = 2.4F}},
			{'{', roomAround(2.5F)},
			{'|', roomAround(2.497F)},
			{'}', roomAround(3.0F)},
		};
		const std::array sources{
			folder.write(kBody, "body", {.room = room}),
			folder.write(kMono, "mono", {.distanceRange = 2.0F, .room = room}),
		};
		ASSERT_TRUE(fontRenderer->Initialize(sources, kBody)); // warned about, still loaded
		EXPECT_EQ(fontRenderer->crampedGlyphs(kBody), "()[]");
		EXPECT_EQ(fontRenderer->crampedGlyphs(kMono), "()");

		std::vector<FontRenderer::GlyphQuad> quads;
		fontRenderer->generateGlyphQuads("()", glm::vec2(0.0F), 1.0F, glm::vec4(1.0F), quads, kBody);
		EXPECT_EQ(quads.size(), 2U);
	}

	TEST_F(FontRendererInitializeTest, AGlyphWithoutACellIsNotChecked) {
		// ')' and ']' are as cramped as '(', but ')' has no cell and ']' a cell of null and a string,
		// nothing to be cramped in. The atlas still loads, and both still draw.
		const AtlasMetrics atlas{
			.room = {{'(', roomAround(1.0F)}, {')', roomAround(1.0F)}, {']', roomAround(1.0F)}}, .cellless = ")", .oddCells = "]"
		};
		ASSERT_TRUE(fontRenderer->Initialize(std::array{folder.write(kBody, "body", atlas)}, kBody));
		EXPECT_EQ(fontRenderer->crampedGlyphs(), "(");

		std::vector<FontRenderer::GlyphQuad> quads;
		fontRenderer->generateGlyphQuads(")]", glm::vec2(0.0F), 1.0F, glm::vec4(1.0F), quads);
		EXPECT_EQ(quads.size(), 2U);
	}

#ifdef DEVELOPMENT_BUILD
	// The UI warnings logged while it lives.
	class UiWarnings {
	  public:
		UiWarnings() {
			foundation::Logger::setSink(
				[](void* context, const foundation::LogRecord& record) {
					if (record.category == foundation::LogCategory::UI && record.level == foundation::LogLevel::Warning) {
						static_cast<UiWarnings*>(context)->warnings.emplace_back(record.message);
					}
				},
				this
			);
		}
		~UiWarnings() { foundation::Logger::setSink(nullptr, nullptr); }

		UiWarnings(const UiWarnings&) = delete;
		UiWarnings& operator=(const UiWarnings&) = delete;
		UiWarnings(UiWarnings&&) = delete;
		UiWarnings& operator=(UiWarnings&&) = delete;

		std::vector<std::string> warnings;
	};

	TEST_F(FontRendererInitializeTest, LoadingWarnsOncePerCrampedAtlasNamingItsGlyphs) {
		const UiWarnings captured;

		// Two cramped atlases and a roomy one between them, at a range wide enough for small text.
		constexpr FontFamily kDisplay{5};
		const std::array	 sources{
			folder.write(kBody, "body", {.distanceRange = 6.0F, .room = {{'(', roomAround(1.0F)}, {')', roomAround(1.0F)}}}),
			folder.write(kMono, "mono", {.distanceRange = 6.0F}),
			folder.write(kDisplay, "display", {.distanceRange = 6.0F, .room = {{'[', roomAround(1.0F)}}}),
		};
		ASSERT_TRUE(fontRenderer->Initialize(sources, kBody));
		ASSERT_EQ(captured.warnings.size(), 2U);
		EXPECT_TRUE(captured.warnings[0].starts_with("SDF atlas body.json: ")) << captured.warnings[0];
		EXPECT_TRUE(captured.warnings[0].ends_with(": ()")) << captured.warnings[0];
		EXPECT_TRUE(captured.warnings[1].starts_with("SDF atlas display.json: ")) << captured.warnings[1];
		EXPECT_TRUE(captured.warnings[1].ends_with(": [")) << captured.warnings[1];
	}

	TEST_F(FontRendererInitializeTest, AnAtlasWhoseRangeIsUnderASixthOfItsEmWarns) {
		const UiWarnings captured;

		// The test atlases' em is 32 texels: a range of 4 is under a sixth of it, 6 is not.
		ASSERT_TRUE(
			fontRenderer->Initialize(std::array{folder.write(kBody, "body", {.distanceRange = 6.0F}), folder.write(kMono, "mono")}, kBody)
		);
		ASSERT_EQ(captured.warnings.size(), 1U);
		EXPECT_EQ(
			captured.warnings[0],
			"SDF atlas mono.json: a distance range of 4.0 texels is under a sixth of its 32-texel em, too thin for small text; generate "
			"it with 6 or more"
		);
	}
#endif

} // namespace Renderer
