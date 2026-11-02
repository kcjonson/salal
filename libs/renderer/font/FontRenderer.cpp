// Text from msdf-atlas-gen's MSDF atlases, laid out by code point

#include "font/FontRenderer.h"
#include "primitives/Primitives.h"
#include "utils/Log.h"
#include "utils/Utf8.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <string_view>

namespace Renderer {

	namespace {
		constexpr float kBaseFontSize = 16.0F; // scale 1 draws an em this many pixels

		// How far past its UV edge a quad reads its atlas: bilinear filtering's half a texel.
		constexpr float kBilinearFootprintTexels = 0.5F;

		std::uint64_t kerningKey(char32_t left, char32_t right) {
			return (static_cast<std::uint64_t>(left) << 32U) | right;
		}

		// The single advance iteration shared by MeasureText and generateGlyphQuads,
		// so measured width always equals rendered width (alignment depends on it).
		// Decodes the text by code point, resolves each to its glyph or the atlas's
		// fallback, kerns each glyph against the one drawn before it, invokes
		// fn(glyph, penX) at the glyph's pen position, and returns the final pen x.
		// Letter-spacing sits between characters only (n characters -> n-1 gaps).
		template <typename AtlasType, typename Fn>
		float forEachGlyph(const AtlasType& atlas, std::string_view text, float fontSize, float letterSpacing, Fn&& fn) {
			float					penX = 0.0F;
			std::optional<char32_t> previous;
			for (std::size_t at = 0; at < text.size();) {
				const foundation::UTF8::Character character = foundation::UTF8::decodeAt(text, at);
				at += character.length;
				const auto drawn = atlas.lookUp(character.codePoint);
				if (drawn.glyph != nullptr) {
					if (previous) {
						penX += atlas.kern(*previous, drawn.codePoint) * fontSize;
					}
					fn(*drawn.glyph, penX);
					penX += drawn.glyph->advance * fontSize;
					previous = drawn.codePoint;
				}
				if (at < text.size()) {
					penX += letterSpacing;
				}
			}
			return penX;
		}

		const nlohmann::json* member(const nlohmann::json& object, const char* name) {
			const auto found = object.is_object() ? object.find(name) : object.end();
			return found != object.end() ? &*found : nullptr;
		}

		std::optional<double> numberOf(const nlohmann::json& object, const char* name) {
			const nlohmann::json* value = member(object, name);
			return value != nullptr && value->is_number() ? std::optional(value->get<double>()) : std::nullopt;
		}

		// A Unicode scalar value, which msdf-atlas-gen writes as an integer.
		std::optional<char32_t> codePointOf(const nlohmann::json& object, const char* name) {
			const std::optional<double> value = numberOf(object, name);
			if (!value || *value < 0.0 || *value > 0x10FFFF || *value != static_cast<double>(static_cast<std::uint32_t>(*value)) ||
				(*value >= 0xD800 && *value <= 0xDFFF)) {
				return std::nullopt;
			}
			return static_cast<char32_t>(*value);
		}

		struct Bounds {
			double left;
			double bottom;
			double right;
			double top;
		};

		std::optional<Bounds> boundsOf(const nlohmann::json& object, const char* name) {
			const nlohmann::json* value = member(object, name);
			if (value == nullptr) {
				return std::nullopt;
			}
			const std::array sides{
				numberOf(*value, "left"), numberOf(*value, "bottom"), numberOf(*value, "right"), numberOf(*value, "top")
			};
			if (!std::ranges::all_of(sides, [](const std::optional<double>& side) { return side.has_value(); })) {
				return std::nullopt;
			}
			return Bounds{.left = *sides[0], .bottom = *sides[1], .right = *sides[2], .top = *sides[3]};
		}

		std::string nameOf(char32_t codePoint) {
			std::array<char, 16> name{};
			std::snprintf(name.data(), name.size(), "U+%04X", static_cast<unsigned>(codePoint));
			return name.data();
		}
	} // namespace

	FontRenderer::FontRenderer() = default;

	FontRenderer::~FontRenderer() {
		release();
	}

	bool FontRenderer::Initialize(std::span<const FontAtlasSource> sources, FontFamily defaultFamily) {
		LOG_INFO(UI, "Initializing FontRenderer...");
		release();

		// The sources are checked whole before anything loads.
		if (sources.empty()) {
			LOG_ERROR(UI, "FontRenderer: no font atlases given");
			return false;
		}
		std::array<bool, 256> given{}; // one per FontFamily value
		size_t				  slots = 0;
		for (const FontAtlasSource& source : sources) {
			const auto id = static_cast<size_t>(source.family);
			if (source.family == FontFamily::Default) {
				LOG_ERROR(UI, "FontRenderer: FontFamily::Default given as a family: %s", source.pngPath.c_str());
				return false;
			}
			if (given[id]) {
				LOG_ERROR(UI, "FontRenderer: font family %zu given twice (%s)", id, source.pngPath.c_str());
				return false;
			}
			given[id] = true;
			slots = std::max(slots, id + 1);
		}
		if (!given[static_cast<size_t>(defaultFamily)]) {
			LOG_ERROR(UI, "FontRenderer: the default font family %d is not among the atlases given", static_cast<int>(defaultFamily));
			return false;
		}

		// In order. A family that fails is logged and skipped, and draws with the default;
		// the default failing ends it, holding nothing, as there is nothing to fall back to.
		// loadAtlas's error names the file; two paths would outrun the log line.
		atlases.resize(slots);
		for (const FontAtlasSource& source : sources) {
			if (loadAtlas(atlases[static_cast<size_t>(source.family)], source.pngPath, source.jsonPath)) {
				continue;
			}
			if (source.family == defaultFamily) {
				LOG_ERROR(UI, "FontRenderer: the default font family %d did not load", static_cast<int>(defaultFamily));
				release();
				return false;
			}
			LOG_WARNING(UI, "Font family %d unavailable, skipping: it draws with the default", static_cast<int>(source.family));
		}
		fallbackFamily = defaultFamily;

		LOG_INFO(UI, "FontRenderer initialization complete (SDF atlas mode)");
		return true;
	}

	void FontRenderer::release() {
		atlases.clear(); // their textures go with them
		fallbackFamily = FontFamily::Default;
		glyphQuadCache.clear();
		wrappedTextCache.clear();
	}

	FontFamily FontRenderer::resolve(FontFamily family) const {
		const auto requested = static_cast<size_t>(family);
		return requested < atlases.size() && atlases[requested].loaded ? family : fallbackFamily;
	}

	const FontRenderer::Atlas& FontRenderer::atlasFor(FontFamily family) const {
		const auto resolved = static_cast<size_t>(resolve(family));
		if (resolved < atlases.size()) {
			return atlases[resolved];
		}
		static const Atlas kNoAtlas;
		return kNoAtlas;
	}

	FontRenderer::Lookup FontRenderer::Atlas::lookUp(char32_t codePoint) const {
		auto found = glyphs.find(codePoint);
		if (found == glyphs.end() && fallback) {
			found = glyphs.find(*fallback);
		}
		return found != glyphs.end() ? Lookup{.codePoint = found->first, .glyph = &found->second} : Lookup{};
	}

	float FontRenderer::Atlas::kern(char32_t left, char32_t right) const {
		if (kerning.empty()) {
			return 0.0F;
		}
		const auto found = kerning.find(kerningKey(left, right));
		return found != kerning.end() ? found->second : 0.0F;
	}

	glm::vec2 FontRenderer::MeasureText(const std::string& text, float scale, FontFamily family, float letterSpacing) const {
		if (text.empty()) {
			return glm::vec2(0.0F, 0.0F);
		}

		const Atlas& atlas = atlasFor(family);
		const float	 fontSize = kBaseFontSize * scale; // Requested rendering size, not atlas size
		const float	 totalWidth = forEachGlyph(atlas, text, fontSize, letterSpacing, [](const Glyph&, float) {});
		return glm::vec2(totalWidth, atlas.metrics.lineHeight * fontSize);
	}

	float FontRenderer::getMaxGlyphHeight(float scale, FontFamily family) const {
		return atlasFor(family).metrics.lineHeight * scale;
	}

	float FontRenderer::getAscent(float scale, FontFamily family) const {
		return atlasFor(family).metrics.ascender * kBaseFontSize * scale;
	}

	bool FontRenderer::loadAtlas(Atlas& atlas, const std::string& pngPath, const std::string& jsonPath) {
		LOG_INFO(UI, "Loading font atlas %s and %s", jsonPath.c_str(), pngPath.c_str());

		// The file name, not the path, and the reason last: the logger stops at 256 characters.
		const std::string file = std::filesystem::path(jsonPath).filename().string();
		const auto		  fail = [&file](const std::string& reason) {
			   LOG_ERROR(UI, "SDF atlas %s: %s", file.c_str(), reason.c_str());
			   return false;
		};

		nlohmann::json json;
		{
			std::ifstream in(jsonPath);
			if (!in.is_open()) {
				return fail("can't be opened");
			}
			try {
				in >> json;
			} catch (const std::exception& e) {
				return fail(std::string("isn't JSON: ") + e.what());
			}
		}

		if (member(json, "variants") != nullptr) {
			return fail("holds several fonts; give each family an atlas of its own");
		}
		const nlohmann::json* meta = member(json, "atlas");
		const nlohmann::json* metrics = member(json, "metrics");
		const nlohmann::json* glyphs = member(json, "glyphs");
		if (meta == nullptr || !meta->is_object() || metrics == nullptr || !metrics->is_object() || glyphs == nullptr) {
			return fail("needs atlas, metrics and glyphs");
		}
		if (!glyphs->is_array()) {
			return fail(
				"its glyphs aren't an array, as msdf-atlas-gen writes them; scripts/convert-font-atlas.mjs converts salal's old JSON"
			);
		}

		const nlohmann::json*		type = member(*meta, "type");
		constexpr std::array		kFieldTypes{"msdf", "mtsdf", "sdf", "psdf"};
		const std::optional<double> range = numberOf(*meta, "distanceRange");
		const std::optional<double> size = numberOf(*meta, "size");
		const std::optional<double> width = numberOf(*meta, "width");
		const std::optional<double> height = numberOf(*meta, "height");
		const nlohmann::json*		yOrigin = member(*meta, "yOrigin");
		if (type == nullptr || std::ranges::none_of(kFieldTypes, [type](const char* name) { return *type == name; })) {
			return fail("atlas.type isn't a distance field the text mode reads: msdf, mtsdf, sdf or psdf");
		}
		if (!range || *range <= 0.0 || !size || *size <= 0.0) {
			return fail("atlas.distanceRange and atlas.size must be positive numbers");
		}
		const auto positiveInteger = [](const std::optional<double>& value) {
			return value && *value >= 1.0 && *value <= 65536.0 && *value == static_cast<double>(static_cast<int>(*value));
		};
		if (!positiveInteger(width) || !positiveInteger(height)) {
			return fail("atlas.width and atlas.height must be positive integers");
		}
		// The text mode takes the field's middle for the edge.
		if (numberOf(*meta, "distanceRangeMiddle").value_or(0.0) != 0.0) {
			return fail("atlas.distanceRangeMiddle must be 0");
		}
		// Older msdf-atlas-gen writes no yOrigin, and measures from the bottom.
		if (yOrigin != nullptr && *yOrigin != "top" && *yOrigin != "bottom") {
			return fail("atlas.yOrigin must be \"top\" or \"bottom\"");
		}
		const bool fromTop = yOrigin != nullptr && *yOrigin == "top";

		const std::optional<double> emSize = numberOf(*metrics, "emSize");
		const std::optional<double> lineHeight = numberOf(*metrics, "lineHeight");
		const std::optional<double> ascender = numberOf(*metrics, "ascender");
		if (!emSize || *emSize <= 0.0 || !lineHeight || !ascender) {
			return fail("metrics needs a positive emSize, a lineHeight and an ascender");
		}

		// Everything below goes to em (divided by emSize) and to y up, flipping a top-origin
		// file: its plane bounds and ascender point down, and its atlas bounds count rows
		// from the image's top.
		const double atlasWidth = *width;
		const double atlasHeight = *height;
		const double up = fromTop ? -1.0 : 1.0;
		Metrics&	 atlasMetrics = atlas.metrics;
		atlasMetrics = {
			.distanceRange = static_cast<float>(*range),
			.emTexels = static_cast<float>(*size),
			.width = static_cast<int>(atlasWidth),
			.height = static_cast<int>(atlasHeight),
			.ascender = static_cast<float>(up * *ascender / *emSize),
			.lineHeight = static_cast<float>(*lineHeight / *emSize),
		};

		for (const nlohmann::json& entry : *glyphs) {
			const std::optional<char32_t> codePoint = codePointOf(entry, "unicode");
			if (!codePoint) {
				return fail(
					member(entry, "index") != nullptr
						? "its glyphs are by glyph index; generate it from a charset, so they're by code point"
						: "a glyph has no unicode code point"
				);
			}
			const std::string			name = nameOf(*codePoint);
			const std::optional<double> advance = numberOf(entry, "advance");
			if (!advance) {
				return fail(name + " has no advance");
			}
			Glyph glyph{.advance = static_cast<float>(*advance / *emSize)};

			const bool hasPlane = member(entry, "planeBounds") != nullptr;
			const bool hasAtlas = member(entry, "atlasBounds") != nullptr;
			if (hasPlane != hasAtlas) {
				return fail(name + " has " + (hasPlane ? "planeBounds and no atlasBounds" : "atlasBounds and no planeBounds"));
			}
			if (hasPlane) {
				const std::optional<Bounds> plane = boundsOf(entry, "planeBounds");
				const std::optional<Bounds> box = boundsOf(entry, "atlasBounds");
				if (!plane || !box) {
					return fail(name + "'s bounds aren't four numbers each");
				}
				const double planeBottom = up * plane->bottom / *emSize;
				const double planeTop = up * plane->top / *emSize;
				const double boxBottom = fromTop ? atlasHeight - box->bottom : box->bottom;
				const double boxTop = fromTop ? atlasHeight - box->top : box->top;
				if (plane->left > plane->right || planeBottom > planeTop) {
					return fail(name + "'s planeBounds are inverted");
				}
				if (box->left > box->right || boxBottom > boxTop || box->left < 0.0 || boxBottom < 0.0 || box->right > atlasWidth ||
					boxTop > atlasHeight) {
					return fail(name + "'s atlasBounds are inverted or leave the atlas");
				}
				glyph.planeMin = glm::vec2(static_cast<float>(plane->left / *emSize), static_cast<float>(planeBottom));
				glyph.planeMax = glm::vec2(static_cast<float>(plane->right / *emSize), static_cast<float>(planeTop));
				glyph.uvMin = glm::vec2(static_cast<float>(box->left / atlasWidth), static_cast<float>(boxBottom / atlasHeight));
				glyph.uvMax = glm::vec2(static_cast<float>(box->right / atlasWidth), static_cast<float>(boxTop / atlasHeight));
				glyph.hasGeometry = true;
			}
			if (!atlas.glyphs.emplace(*codePoint, glyph).second) {
				return fail(name + " is given twice");
			}
		}
		for (const char32_t candidate : {foundation::UTF8::kReplacement, U'?'}) {
			if (atlas.glyphs.contains(candidate)) {
				atlas.fallback = candidate;
				break;
			}
		}

		// Older msdf-atlas-gen writes no kerning.
		if (const nlohmann::json* pairs = member(json, "kerning")) {
			if (!pairs->is_array()) {
				return fail("its kerning isn't an array");
			}
			for (const nlohmann::json& pair : *pairs) {
				const std::optional<char32_t> left = codePointOf(pair, "unicode1");
				const std::optional<char32_t> right = codePointOf(pair, "unicode2");
				const std::optional<double>	  advance = numberOf(pair, "advance");
				if (!left || !right || !advance) {
					return fail(
						member(pair, "index1") != nullptr ? "its kerning is by glyph index; generate it from a charset"
														  : "a kerning pair needs unicode1, unicode2 and advance"
					);
				}
				atlas.kerning[kerningKey(*left, *right)] = static_cast<float>(*advance / *emSize);
			}
		}

		LOG_INFO(
			UI,
			"Atlas metadata: size=%dx%d, em=%.1f texels, range=%.1f, %zu glyphs, %zu kerning pairs",
			atlasMetrics.width,
			atlasMetrics.height,
			atlasMetrics.emTexels,
			atlasMetrics.distanceRange,
			atlas.glyphs.size(),
			atlas.kerning.size()
		);
		if (thinDistanceRange(atlasMetrics)) {
			LOG_WARNING(
				UI,
				"SDF atlas %s: a distance range of %.1f texels is under a sixth of its %g-texel em, too thin for small text; generate it "
				"with %d or more",
				file.c_str(),
				atlasMetrics.distanceRange,
				atlasMetrics.emTexels,
				static_cast<int>(std::ceil(atlasMetrics.emTexels / 6.0F))
			);
		}

		// OpenGL expects (0,0) at bottom-left, but images are stored with (0,0) at top-left
		// Flip vertically so texture coordinates match
		stbi_set_flip_vertically_on_load(1);
		int		   pngWidth = 0;
		int		   pngHeight = 0;
		int		   channels = 0;
		const auto image = std::unique_ptr<unsigned char, decltype(&stbi_image_free)>(
			stbi_load(pngPath.c_str(), &pngWidth, &pngHeight, &channels, 3), &stbi_image_free // Force RGB
		);
		if (image == nullptr) {
			return fail("its PNG doesn't load: " + std::filesystem::path(pngPath).filename().string());
		}
		if (pngWidth != atlasMetrics.width || pngHeight != atlasMetrics.height) {
			return fail(
				"is for a " + std::to_string(atlasMetrics.width) + "x" + std::to_string(atlasMetrics.height) + " atlas, and its PNG is " +
				std::to_string(pngWidth) + "x" + std::to_string(pngHeight)
			);
		}

		// RGB8, linear, clamped: the SDF is sampled bilinearly and decoded in the shader.
		atlas.texture = Primitives::createTexture(
			{.width = pngWidth, .height = pngHeight, .format = TextureFormat::Rgb8, .linear = true, .pixels = image.get()}
		);
		if (!atlas.texture.isValid()) {
			return fail("its texture couldn't be made");
		}
		atlas.loaded = true;

		const std::string cramped = findCrampedGlyphs(atlas);
		if (!cramped.empty()) {
			// The glyphs last: the logger stops at 256 characters.
			LOG_WARNING(
				UI, "SDF atlas %s: glyphs whose quads read into another's, and draw slivers of it: %s", file.c_str(), cramped.c_str()
			);
		}

		LOG_INFO(UI, "SDF atlas loaded successfully");
		return true;
	}

	void FontRenderer::generateGlyphQuads(
		const std::string&		text,
		const glm::vec2&		position,
		float					scale,
		const glm::vec4&		color,
		std::vector<GlyphQuad>& outQuads,
		FontFamily				family,
		float					letterSpacing
	) const {
		const FontFamily resolved = resolve(family);
		const Atlas&	 atlas = atlasFor(resolved);

		// IMPORTANT: fontSize should be the REQUESTED rendering size, not atlas glyph size!
		// The atlas may be generated at higher resolution (e.g., 32px) for quality,
		// but scale=1.0 should render at kBaseFontSize (16px), not the atlas's em.
		// The glyph metrics are in EM units, so we scale by the requested pixel size.
		const float fontSize = kBaseFontSize * scale;			   // Requested rendering size in pixels
		const float baselineY = atlas.metrics.ascender * fontSize; // Relative to origin for caching

		// The run origin the renderer snaps to the pixel grid is the pen origin AT
		// THE BASELINE (not the box top-left): aligning the baseline is what makes
		// stroke bottoms land on pixel boundaries, and it's shared by every glyph
		// of the run so advances/kerning are untouched.
		const glm::vec2 penOrigin = position + glm::vec2(0.0F, baselineY);

		// Try cache lookup if enabled
		if (FontRendererConfig::kEnableGlyphQuadCache) {
			CacheKey key{resolved, text, scale, letterSpacing};
			auto	 it = glyphQuadCache.find(key);

			if (it != glyphQuadCache.end()) {
				// Cache hit! Copy quads and adjust position/color
				const std::vector<GlyphQuad>& cachedQuads = it->second.quads;

				// Update LRU tracking
				it->second.lastAccessFrame = currentFrame;

				// Copy quads with position/color adjustment
				outQuads.reserve(outQuads.size() + cachedQuads.size());
				for (const auto& cachedQuad : cachedQuads) {
					GlyphQuad quad = cachedQuad;
					// Adjust position (cached quads are relative to origin)
					quad.position += position;
					quad.runOrigin = penOrigin;
					// Update color (cached quads have color from first render)
					quad.color = color;
					outQuads.push_back(quad);
				}

				return; // Cache hit, done!
			}
		}

		// Cache miss or caching disabled - generate quads
		size_t startIdx = outQuads.size(); // Track where we started adding

		forEachGlyph(atlas, text, fontSize, letterSpacing, [&](const Glyph& glyph, float penX) {
			// Only generate quad if glyph has geometry (not whitespace)
			if (!glyph.hasGeometry) {
				return;
			}

			GlyphQuad quad{};
			quad.position = glm::vec2(penX + glyph.planeMin.x * fontSize, baselineY - glyph.planeMax.y * fontSize);
			quad.size = (glyph.planeMax - glyph.planeMin) * fontSize;
			quad.uvMin = glyph.uvMin;
			quad.uvMax = glyph.uvMax;
			quad.color = color;
			quad.atlas = atlas.texture.handle();
			quad.pixelRange = atlas.metrics.distanceRange;

			outQuads.push_back(quad);
		});

		// Cache the generated quads if enabled
		if (FontRendererConfig::kEnableGlyphQuadCache) {
			// Check if cache is full and needs eviction
			if (glyphQuadCache.size() >= FontRendererConfig::kMaxGlyphQuadCacheEntries) {
				// Find and evict the LRU entry
				auto oldestIt = glyphQuadCache.begin();
				for (auto it = glyphQuadCache.begin(); it != glyphQuadCache.end(); ++it) {
					if (it->second.lastAccessFrame < oldestIt->second.lastAccessFrame) {
						oldestIt = it;
					}
				}
				glyphQuadCache.erase(oldestIt);
			}

			// Cache the generated quads (relative to origin, before position adjustment)
			CacheEntry entry;
			entry.lastAccessFrame = currentFrame;
			entry.quads.reserve(outQuads.size() - startIdx);
			for (size_t i = startIdx; i < outQuads.size(); ++i) {
				entry.quads.push_back(outQuads[i]);
			}

			CacheKey key{resolved, text, scale, letterSpacing};
			glyphQuadCache[key] = std::move(entry);

			// Now adjust positions in outQuads for the caller
			for (size_t i = startIdx; i < outQuads.size(); ++i) {
				outQuads[i].position += position;
				outQuads[i].runOrigin = penOrigin;
			}
		} else {
			// No caching - just adjust positions
			for (size_t i = startIdx; i < outQuads.size(); ++i) {
				outQuads[i].position += position;
				outQuads[i].runOrigin = penOrigin;
			}
		}
	}

	const FontRenderer::WrappedTextResult&
	FontRenderer::wrapText(const std::string& text, float scale, float maxWidth, FontFamily family) const {
		// Check cache first
		WrapCacheKey key{resolve(family), text, scale, maxWidth};
		auto		 cacheIt = wrappedTextCache.find(key);
		if (cacheIt != wrappedTextCache.end()) {
			cacheIt->second.lastAccessFrame = currentFrame;
			return cacheIt->second.result;
		}

		// Cache miss - compute wrapped text
		WrappedTextResult result;
		result.family = key.family;
		result.scale = scale;
		result.lineHeight = atlasFor(family).metrics.lineHeight * kBaseFontSize * scale;

		// Handle empty text
		if (text.empty()) {
			result.lines.push_back("");
			result.lineWidths.push_back(0.0F);
			result.totalWidth = 0.0F;
			result.totalHeight = result.lineHeight;

			// Cache and return
			WrapCacheEntry entry{result, currentFrame};
			wrappedTextCache[key] = std::move(entry);
			return wrappedTextCache[key].result;
		}

		// No wrapping case (maxWidth <= 0)
		if (maxWidth <= 0.0F) {
			result.lines.push_back(text);
			glm::vec2 size = MeasureText(text, scale, family);
			result.lineWidths.push_back(size.x);
			result.totalWidth = size.x;
			result.totalHeight = size.y;

			// Cache and return
			WrapCacheEntry entry{result, currentFrame};
			wrappedTextCache[key] = std::move(entry);
			return wrappedTextCache[key].result;
		}

		// Word-based wrapping. Spaces and newlines are single bytes that no other character's
		// bytes contain, so splitting at them splits no character. A line is measured whole as
		// it grows, so the kerning across its spaces counts.
		std::string currentLine;
		float		currentLineWidth = 0.0F;

		size_t i = 0;
		while (i < text.size()) {
			// Handle explicit newlines
			if (text[i] == '\n') {
				result.lines.push_back(currentLine);
				result.lineWidths.push_back(currentLineWidth);
				currentLine.clear();
				currentLineWidth = 0.0F;
				++i;
				continue;
			}

			// Skip leading whitespace at line start (except first line)
			if (currentLine.empty() && !result.lines.empty() && text[i] == ' ') {
				++i;
				continue;
			}

			// Extract next word (sequence of non-space, non-newline characters)
			size_t wordStart = i;
			while (i < text.size() && text[i] != ' ' && text[i] != '\n') {
				++i;
			}
			std::string word = text.substr(wordStart, i - wordStart);

			if (currentLine.empty()) {
				// An empty line takes the word even if it's too long
				currentLine = std::move(word);
				currentLineWidth = MeasureText(currentLine, scale, family).x;
			} else {
				std::string longer = currentLine + ' ' + word;
				const float longerWidth = MeasureText(longer, scale, family).x;
				if (longerWidth <= maxWidth) {
					currentLine = std::move(longer);
					currentLineWidth = longerWidth;
				} else {
					// Word doesn't fit - start new line
					result.lines.push_back(currentLine);
					result.lineWidths.push_back(currentLineWidth);
					currentLine = std::move(word);
					currentLineWidth = MeasureText(currentLine, scale, family).x;
				}
			}

			// Skip single space after word (will be handled by next iteration)
			if (i < text.size() && text[i] == ' ') {
				++i;
			}
		}

		// Add final line if not empty
		if (!currentLine.empty() || result.lines.empty()) {
			result.lines.push_back(currentLine);
			result.lineWidths.push_back(currentLineWidth);
		}

		// Calculate totals
		result.totalWidth = 0.0F;
		for (float lineWidth : result.lineWidths) {
			result.totalWidth = std::max(result.totalWidth, lineWidth);
		}
		result.totalHeight = static_cast<float>(result.lines.size()) * result.lineHeight;

		// Evict oldest entry if cache is full
		if (wrappedTextCache.size() >= FontRendererConfig::kMaxGlyphQuadCacheEntries) {
			auto oldestIt = wrappedTextCache.begin();
			for (auto it = wrappedTextCache.begin(); it != wrappedTextCache.end(); ++it) {
				if (it->second.lastAccessFrame < oldestIt->second.lastAccessFrame) {
					oldestIt = it;
				}
			}
			wrappedTextCache.erase(oldestIt);
		}

		// Cache and return
		WrapCacheEntry entry{result, currentFrame};
		wrappedTextCache[key] = std::move(entry);
		return wrappedTextCache[key].result;
	}

	glm::vec2 FontRenderer::measureTextWithWrapping(const std::string& text, float scale, float maxWidth, FontFamily family) const {
		if (maxWidth <= 0.0F) {
			// No wrapping - use simple measurement
			return MeasureText(text, scale, family);
		}

		const WrappedTextResult& wrapped = wrapText(text, scale, maxWidth, family);
		return glm::vec2(wrapped.totalWidth, wrapped.totalHeight);
	}

	void FontRenderer::generateWrappedGlyphQuads(
		const WrappedTextResult&	wrapped,
		const glm::vec2&			position,
		const glm::vec4&			color,
		Foundation::HorizontalAlign hAlign,
		float						containerWidth,
		std::vector<GlyphQuad>&		outQuads
	) const {
		const FontFamily family = wrapped.family;
		const float		 scale = wrapped.scale;
		float			 currentY = position.y;

		for (const std::string& line : wrapped.lines) {
			// Calculate X offset for alignment
			float lineX = position.x;
			if (containerWidth > 0.0F && !line.empty()) {
				glm::vec2 lineSize = MeasureText(line, scale, family);
				switch (hAlign) {
					case Foundation::HorizontalAlign::Center:
						lineX += (containerWidth - lineSize.x) * 0.5F;
						break;
					case Foundation::HorizontalAlign::Right:
						lineX += containerWidth - lineSize.x;
						break;
					case Foundation::HorizontalAlign::Left:
					default:
						// No offset
						break;
				}
			}

			// Generate quads for this line
			if (!line.empty()) {
				generateGlyphQuads(line, glm::vec2(lineX, currentY), scale, color, outQuads, family);
			}

			currentY += wrapped.lineHeight;
		}
	}

	TextureHandle FontRenderer::getAtlasTexture(FontFamily family) const {
		return atlasFor(family).texture.handle();
	}

	std::string FontRenderer::crampedGlyphs(FontFamily family) const {
		return findCrampedGlyphs(atlasFor(family));
	}

	std::string FontRenderer::findCrampedGlyphs(const Atlas& atlas) {
		// In texels. The tolerance absorbs the rounding in the JSON's coordinates, far under a
		// texel; two reads that meet at an edge, as packed boxes do, don't overlap.
		constexpr float kToleranceTexels = 0.01F;
		const glm::vec2 atlasSize(static_cast<float>(atlas.metrics.width), static_cast<float>(atlas.metrics.height));
		struct Read {
			glm::vec2 min;
			glm::vec2 max;
			char32_t  codePoint;
		};
		std::vector<Read> reads;
		for (const auto& [codePoint, glyph] : atlas.glyphs) {
			if (glyph.hasGeometry) {
				reads.push_back(
					{.min = glyph.uvMin * atlasSize - kBilinearFootprintTexels,
					 .max = glyph.uvMax * atlasSize + kBilinearFootprintTexels,
					 .codePoint = codePoint}
				);
			}
		}

		// Swept left to right, so each read is checked only against those that start before it ends.
		std::ranges::sort(reads, {}, [](const Read& read) { return read.min.x; });
		std::vector<char32_t> cramped;
		for (size_t i = 0; i < reads.size(); ++i) {
			for (size_t j = i + 1; j < reads.size() && reads[j].min.x < reads[i].max.x - kToleranceTexels; ++j) {
				if (reads[j].min.y < reads[i].max.y - kToleranceTexels && reads[i].min.y < reads[j].max.y - kToleranceTexels) {
					cramped.push_back(reads[i].codePoint);
					cramped.push_back(reads[j].codePoint);
				}
			}
		}
		std::ranges::sort(cramped);
		const auto [duplicates, end] = std::ranges::unique(cramped);
		cramped.erase(duplicates, end);

		std::string named;
		for (const char32_t codePoint : cramped) {
			named += foundation::UTF8::encode(codePoint);
		}
		return named;
	}

	void FontRenderer::updateFrame() {
		currentFrame++;
	}

	void FontRenderer::clearGlyphQuadCache() {
		glyphQuadCache.clear();
		wrappedTextCache.clear();
		LOG_DEBUG(UI, "Cleared glyph quad cache and wrapped text cache");
	}

	size_t FontRenderer::getGlyphQuadCacheSize() const {
		return glyphQuadCache.size();
	}

} // namespace Renderer
