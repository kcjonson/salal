// Text from MSDF (multi-channel signed distance field) atlases as msdf-atlas-gen writes them. Text is
// UTF-8, laid out by code point.

#pragma once

#include "Handle.h"
#include "Texture.h"
#include "graphics/PrimitiveStyles.h"
#include "primitives/FontFamily.h"
#include <cmath>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Renderer { // NOLINT(readability-identifier-naming)

	/**
	 * Configuration constants for FontRenderer performance tuning
	 */
	namespace FontRendererConfig {
		// Glyph quad cache settings
		// Size to handle complex UIs: ~1000 unique strings × ~4 scales = 4000 entries
		// Memory cost: ~6-8MB max (1.5KB average per entry)
		constexpr size_t kMaxGlyphQuadCacheEntries = 4096;

		// Runtime toggle for cache (disable for testing/comparison)
		constexpr bool kEnableGlyphQuadCache = true;
	} // namespace FontRendererConfig

	/**
	 * One atlas for FontRenderer::Initialize: the family the game loads it as, and the
	 * atlas's PNG and the JSON msdf-atlas-gen wrote with it, as paths the renderer opens
	 * as given.
	 */
	struct FontAtlasSource {
		FontFamily	family;
		std::string pngPath;
		std::string jsonPath;
	};

	class FontRenderer {
	  public:
		FontRenderer();
		~FontRenderer();

		// Delete copy constructor and assignment operator
		FontRenderer(const FontRenderer&) = delete;
		FontRenderer& operator=(const FontRenderer&) = delete;

		// Not movable either: a renderer owns its atlas textures, and nothing moves one.
		FontRenderer(FontRenderer&&) = delete;
		FontRenderer& operator=(FontRenderer&&) = delete;

		/**
		 * Load the game's font atlases, replacing any an earlier call loaded. Needs
		 * Primitives::init() first: the atlases are textures on the renderer's backend.
		 *
		 * The sources load in order. One that fails to load is logged and skipped, and
		 * its family draws with the default, as does any family not given.
		 *
		 * The JSON is msdf-atlas-gen's for one font: glyphs keyed by code point, with
		 * planeBounds and atlasBounds or only an advance (whitespace), either yOrigin, any
		 * emSize, and kerning pairs, which measuring and layout apply. The atlas type is
		 * one the text mode's median of red, green and blue reads: msdf or mtsdf, or sdf
		 * or psdf, whose one channel loads into all three. A JSON that isn't this, or a
		 * PNG of another size than it says, fails that atlas with the reason logged.
		 *
		 * A second call frees the atlases the first made, so a handle from
		 * getAtlasTexture goes stale: a game that handed one to Primitives::setFontAtlas
		 * hands over the new one. So does every GlyphQuad laid out before it, which
		 * carries its atlas and now draws nothing: lay the text out again. Glyphs
		 * already batched still draw; their atlases are freed after the flush.
		 *
		 * An atlas whose distance range is under a sixth of its em warns at load:
		 * too thin a range for text at the small sizes ui draws.
		 * @param sources One atlas per family; no family twice, and not FontFamily::Default
		 * @param defaultFamily The family FontFamily::Default stands for; one of the sources
		 * @return false, with nothing loaded, when the sources are empty or break the rules
		 *         above, or when the default family's atlas fails to load
		 */
		[[nodiscard]] bool Initialize(std::span<const FontAtlasSource> sources, FontFamily defaultFamily);

		/**
		 * Calculate the dimensions of a text string with the given scale
		 * @param text UTF-8. A code point the atlas lacks measures and draws as U+FFFD, or as
		 *        '?' where the atlas lacks that too, and each ill-formed sequence as one U+FFFD
		 * @param scale Scaling factor (default 1.0F)
		 * @param family Font family to measure with (default: the game's default family)
		 * @param letterSpacing Extra px between glyphs (CSS letter-spacing); 0 = none
		 * @return Width and height of the text in pixels
		 */
		glm::vec2
		MeasureText(const std::string& text, float scale = 1.0F, FontFamily family = FontFamily::Default, float letterSpacing = 0.0F) const;

		/**
		 * Get the maximum glyph height scaled by the given factor
		 * @param scale Scaling factor for the font size (1.0F = original size)
		 * @param family Font family to query (default: the game's default family)
		 * @return Height of the tallest glyph in the font at the given scale
		 */
		float getMaxGlyphHeight(float scale = 1.0F, FontFamily family = FontFamily::Default) const;

		/**
		 * Get the font's ascent (distance from baseline to top) scaled by the given factor
		 * @param scale Scaling factor for the font size (1.0F = original size)
		 * @param family Font family to query (default: the game's default family)
		 * @return Font ascent at the given scale
		 */
		float getAscent(float scale = 1.0F, FontFamily family = FontFamily::Default) const;

		/**
		 * Glyph quad data for batched text rendering. The UVs index one family's
		 * atlas, and the quad carries that atlas and its distance range, so
		 * Primitives::drawGlyphs samples the texture the UVs were laid out in and
		 * draws its edges at that texture's range.
		 */
		struct GlyphQuad {
			glm::vec2	  position;			 // Top-left position of the quad
			glm::vec2	  size;				 // Width and height of the quad
			glm::vec2	  uvMin;			 // Texture coordinate bottom-left
			glm::vec2	  uvMax;			 // Texture coordinate top-right
			glm::vec4	  color;			 // RGBA color
			glm::vec2	  runOrigin;		 // Pen origin of the run at the baseline (shared per run, for pixel snapping)
			TextureHandle atlas;			 // The atlas the UVs index, stale after another Initialize; invalid samples setFontAtlas's
			float		  pixelRange = 0.0F; // The atlas's distance range in texels; with an invalid atlas, setFontAtlas's range instead
		};

		/**
		 * Result of text wrapping operation (cached for performance). It keeps the
		 * family and scale it was measured with, which generateWrappedGlyphQuads lays it
		 * out with, so glyph sizes, line widths and line height all come from one scale.
		 */
		struct WrappedTextResult {
			std::vector<std::string> lines;		  // Each line of wrapped text
			std::vector<float>		 lineWidths;  // Width of each line in pixels
			float					 totalWidth;  // Max line width (for alignment)
			float					 totalHeight; // Sum of line heights
			float					 lineHeight;  // Height per line in pixels
			FontFamily				 family;	  // The family the lines were measured in, as resolved (the one they draw with)
			float					 scale;		  // The scale the lines were measured at (fontSize / 16.0)
		};

		/**
		 * Generate glyph quads for batched rendering (does not render immediately)
		 * @param text UTF-8, read as MeasureText reads it
		 * @param position Top-left position of the text in screen space
		 * @param scale Scaling factor for the text size (1.0F = 16px base size)
		 * @param color RGBA color of the text (0-1 range)
		 * @param outQuads Output vector to append generated quads to
		 * @param family Font family to lay the text out in; each quad carries its atlas
		 * @param letterSpacing Extra px between glyphs (CSS letter-spacing); 0 = none
		 */
		void generateGlyphQuads(
			const std::string&		text,
			const glm::vec2&		position,
			float					scale,
			const glm::vec4&		color,
			std::vector<GlyphQuad>& outQuads,
			FontFamily				family = FontFamily::Default,
			float					letterSpacing = 0.0F
		) const;

		/**
		 * Wrap text to fit within a maximum width
		 * Uses word-based wrapping (breaks at spaces/punctuation, not mid-word)
		 * Handles explicit \n as hard line breaks
		 * @param text The text to wrap
		 * @param scale Scaling factor (fontSize / 16.0)
		 * @param maxWidth Maximum line width in pixels (0 = no wrapping, single line)
		 * @param family Font family used for glyph metrics (default: the game's default family)
		 * @return Cached reference to wrapped result (lines, dimensions)
		 */
		const WrappedTextResult&
		wrapText(const std::string& text, float scale, float maxWidth, FontFamily family = FontFamily::Default) const;

		/**
		 * Measure text with optional wrapping
		 * @param text The text to measure
		 * @param scale Scaling factor
		 * @param maxWidth Maximum line width (0 = no wrapping, returns single-line size)
		 * @param family Font family used for glyph metrics (default: the game's default family)
		 * @return Width and height of the text block in pixels
		 */
		glm::vec2
		measureTextWithWrapping(const std::string& text, float scale, float maxWidth, FontFamily family = FontFamily::Default) const;

		/**
		 * Generate glyph quads for wrapped/multi-line text, in the family and at the
		 * scale and line height it was wrapped with
		 * @param wrapped The lines from wrapText, with their family, scale and line height
		 * @param position Top-left position of the text block
		 * @param color Text color (RGBA 0-1 range)
		 * @param hAlign Horizontal alignment per line
		 * @param containerWidth Container width for alignment (0 = use line width)
		 * @param outQuads Output vector to append generated quads to
		 */
		void generateWrappedGlyphQuads(
			const WrappedTextResult&	wrapped,
			const glm::vec2&			position,
			const glm::vec4&			color,
			Foundation::HorizontalAlign hAlign,
			float						containerWidth,
			std::vector<GlyphQuad>&		outQuads
		) const;

		/**
		 * Get the atlas texture of a font family (for batching)
		 * @param family Font family whose atlas texture to return (default: the game's default family)
		 * @return The atlas texture; invalid unless the last Initialize succeeded
		 */
		TextureHandle getAtlasTexture(FontFamily family = FontFamily::Default) const;

		/**
		 * The glyphs of the atlas this family draws with (the default's, for a family
		 * that isn't loaded) whose quads read the atlas where another glyph's quad does.
		 * A quad reads half a texel past its glyph's atlasBounds, as far as bilinear
		 * filtering reaches at its edge. msdf-atlas-gen insets atlasBounds by that half
		 * texel from each glyph's box, so packed boxes touch and no glyph reads into
		 * another's; where two reads overlap, each glyph draws slivers of the other.
		 * Loading warns about them.
		 * @param family Font family whose atlas to check (default: the game's default family)
		 * @return The offending glyphs in code point order, as UTF-8; empty when none, or nothing is loaded
		 */
		std::string crampedGlyphs(FontFamily family = FontFamily::Default) const;

		/**
		 * Update the internal frame counter for cache LRU tracking
		 * Should be called once per frame from the main application loop
		 */
		void updateFrame();

		/**
		 * Clear the glyph quad cache (e.g., on scene transitions)
		 */
		void clearGlyphQuadCache();

		/**
		 * Get the current size of the glyph quad cache (for debugging/profiling)
		 * @return Number of entries currently cached
		 */
		size_t getGlyphQuadCacheSize() const;

	  private:
		/**
		 * One glyph in the units its quad is built in: em, y up from the pen on the
		 * baseline, and UVs into the atlas as uploaded, its first row the image's bottom.
		 * msdf-atlas-gen's bounds hold the half of the distance range the edge fades
		 * over, so the quad is the plane bounds and samples the atlas bounds as given.
		 */
		struct Glyph {
			glm::vec2 planeMin{};
			glm::vec2 planeMax{};
			glm::vec2 uvMin{};
			glm::vec2 uvMax{};
			float	  advance = 0.0F;
			bool	  hasGeometry = false; // False for whitespace: an advance and nothing to draw
		};

		// What a code point draws as: itself, the atlas's fallback, or nothing (a null glyph).
		struct Lookup {
			char32_t	 codePoint = 0;
			const Glyph* glyph = nullptr;
		};

		struct Metrics {
			float distanceRange = 0.0F; // In texels
			float emTexels = 0.0F;		// Texels an em, msdf-atlas-gen's atlas.size
			int	  width = 0;			// Of the atlas, in texels
			int	  height = 0;
			float ascender = 0.0F;	 // Above the baseline, in em
			float lineHeight = 0.0F; // In em
		};

		/**
		 * A single loaded font: its glyphs, kerning, metrics and atlas texture. One Atlas
		 * per loaded family.
		 */
		struct Atlas {
			std::unordered_map<char32_t, Glyph>		 glyphs;
			std::unordered_map<std::uint64_t, float> kerning;  // In em, by the pair of code points
			std::optional<char32_t>					 fallback; // U+FFFD, or '?' without it, when the atlas has either
			Metrics									 metrics{};
			Texture									 texture; // In the renderer's table, owned here
			bool									 loaded = false;

			Lookup lookUp(char32_t codePoint) const;
			// The extra advance, in em, between two glyphs drawn side by side.
			float kern(char32_t left, char32_t right) const;
		};

		/**
		 * Load an atlas from its PNG and msdf-atlas-gen JSON into the given slot, or log
		 * why it can't be.
		 * @return true if the atlas loaded
		 */
		bool loadAtlas(Atlas& atlas, const std::string& pngPath, const std::string& jsonPath);

		// The glyphs of `atlas` whose quads read into another's (see crampedGlyphs).
		static std::string findCrampedGlyphs(const Atlas& atlas);

		// Whether an atlas's distance range is under a sixth of its em, both in texels.
		static bool thinDistanceRange(const Metrics& metrics) { return metrics.distanceRange * 6.0F < metrics.emTexels; }

		/**
		 * The family a request draws with: itself when it is loaded, otherwise the
		 * default (FontFamily::Default before Initialize succeeds). Both caches key on it,
		 * so every way of naming one atlas shares their entries.
		 */
		FontFamily resolve(FontFamily family) const;

		/**
		 * Resolve a family to its atlas. FontFamily::Default, and a family that is not
		 * loaded, resolve to the default family's, which is loaded whenever Initialize
		 * succeeded; before that, or after it failed, to an empty atlas.
		 */
		const Atlas& atlasFor(FontFamily family) const;

		// Destroy every atlas texture and forget the atlases, the default and both caches.
		void release();

		/**
		 * Cache key for glyph quad caching (family + text + scale + letterSpacing)
		 */
		struct CacheKey {
			FontFamily	family;
			std::string text;
			float		scale;
			float		letterSpacing;

			bool operator==(const CacheKey& other) const {
				return family == other.family && text == other.text && std::abs(scale - other.scale) < 0.001F &&
					   std::abs(letterSpacing - other.letterSpacing) < 0.001F;
			}
		};

		/**
		 * Hash function for CacheKey
		 */
		struct CacheKeyHash {
			size_t operator()(const CacheKey& key) const {
				size_t h1 = std::hash<std::string>{}(key.text);
				size_t h2 = std::hash<float>{}(key.scale);
				size_t h3 = std::hash<int>{}(static_cast<int>(key.family));
				size_t h4 = std::hash<float>{}(key.letterSpacing);
				return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
			}
		};

		/**
		 * Cache entry storing generated quads + LRU tracking
		 */
		struct CacheEntry {
			std::vector<GlyphQuad> quads;
			uint64_t			   lastAccessFrame;
		};

		// Indexed by family id, up to the highest the sources named; a slot no source
		// filled, or whose atlas failed to load, stays unloaded.
		std::vector<Atlas> atlases;

		// The family Initialize was given as the default (FontFamily::Default until then).
		FontFamily fallbackFamily = FontFamily::Default;

		// Glyph quad cache (mutable for const GenerateGlyphQuads)
		mutable std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> glyphQuadCache;
		mutable uint64_t											   currentFrame = 0;

		/**
		 * Cache key for text wrapping (text + scale + wrap width)
		 */
		struct WrapCacheKey {
			FontFamily	family;
			std::string text;
			float		scale;
			float		wrapWidth;

			bool operator==(const WrapCacheKey& other) const {
				return family == other.family && text == other.text && std::abs(scale - other.scale) < 0.001F &&
					   std::abs(wrapWidth - other.wrapWidth) < 0.1F;
			}
		};

		/**
		 * Hash function for WrapCacheKey
		 * Rounds floats to match the precision used in operator== to maintain hash invariant
		 */
		struct WrapCacheKeyHash {
			size_t operator()(const WrapCacheKey& key) const {
				// Round to same precision as equality comparison
				// scale uses 0.001 precision, wrapWidth uses 0.1 precision
				auto roundedScale = std::round(key.scale * 1000.0F) / 1000.0F;
				auto roundedWidth = std::round(key.wrapWidth * 10.0F) / 10.0F;

				size_t h1 = std::hash<std::string>{}(key.text);
				size_t h2 = std::hash<float>{}(roundedScale);
				size_t h3 = std::hash<float>{}(roundedWidth);
				size_t h4 = std::hash<int>{}(static_cast<int>(key.family));
				return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
			}
		};

		/**
		 * Cached wrapping result with LRU tracking
		 */
		struct WrapCacheEntry {
			WrappedTextResult result;
			uint64_t		  lastAccessFrame;
		};

		// Wrapped text cache (mutable for const wrapText)
		mutable std::unordered_map<WrapCacheKey, WrapCacheEntry, WrapCacheKeyHash> wrappedTextCache;
	};

} // namespace Renderer
