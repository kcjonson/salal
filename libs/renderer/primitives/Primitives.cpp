// Primitive Rendering API implementation.
// Provides immediate-mode 2D drawing functions with internal batching.

#include "primitives/Primitives.h"
#include "CoordinateSystem/CoordinateSystem.h"
#include "font/FontRenderer.h"
#include "graphics/ClipTypes.h"
#include "primitives/BatchRenderer.h"
#include "primitives/Stroke.h"
#include "resources/RenderTarget.h"
#include "vector/Tessellator.h"
#include "vector/Types.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <numbers>
#include <stack>
#include <unordered_map>
#include <utils/Log.h>
#include <vector>

// Text rendering is implemented via the base shader in BatchRenderer.
// Text shapes call BatchRenderer::addTextQuad() directly to batch text with shapes.

namespace Renderer::Primitives {

	// The backend init() was given and the batch drawing on it, leaked on purpose: a
	// FontRenderer, TileTextureAtlas, RenderTarget or InstancedMeshHandle held in a
	// static releases through them during static destruction, after an ordinary static
	// here may be gone (MSVC's ~unique_ptr leaves the pointer set), and no GL runs at
	// exit after the context. shutdown() empties both.
	static auto& g_backend = *new std::unique_ptr<backend::Backend>;
	static auto& g_batchRenderer = *new std::unique_ptr<BatchRenderer>;

	// The window's framebuffer size, as setViewport() last recorded it with no RenderTarget begun.
	static int g_backbufferWidth = 0;
	static int g_backbufferHeight = 0;

	// Internal state
	static CoordinateSystem*			g_coordinateSystem = nullptr;
	static FontRenderer*				g_fontRenderer = nullptr;
	static FrameUpdateCallback			g_frameUpdateCallback = nullptr;
	static TextureHandle				g_tileAtlasTexture;
	static std::vector<glm::vec4>		g_tileAtlasRects;
	static std::stack<Foundation::Rect> g_scissorStack;
	static std::stack<Foundation::Mat4> g_transformStack;
	static Foundation::Rect				g_currentScissor;
	static Foundation::Mat4				g_currentTransform = Foundation::Mat4(1.0F);

	// Clip stack for shader-based clipping (preserves batching)
	// Each entry stores the ClipSettings and the computed bounds
	struct ClipStackEntry {
		Foundation::ClipSettings settings;
		Foundation::Vec4		 bounds; // Computed (minX, minY, maxX, maxY)
	};
	static std::stack<ClipStackEntry> g_clipStack;

	// Tessellated polygon fills, keyed by a hash of the outline's vertex bits and
	// checked against a copy of the outline, so a hash collision is a miss, never a
	// wrong mesh. A silhouette drawn every frame (under a changing PushTransform)
	// tessellates once. Bounded by emptying the whole cache when it is full: a
	// frame's working set of silhouettes is far below the capacity, so it refills
	// in one frame, and nothing per entry needs tracking.
	struct PolygonCacheEntry {
		std::vector<Foundation::Vec2> outline;
		renderer::TessellatedMesh	  mesh;
	};
	constexpr size_t									   kPolygonCacheCapacity = 1024;
	static std::unordered_map<uint64_t, PolygonCacheEntry> g_polygonCache;

	// The hook blocks of the base shader init() was given, by hook name.
	struct HookBlockEntry {
		std::string hook;
		uint32_t	binding = 0;
		uint32_t	size = 0;
	};
	static std::vector<HookBlockEntry> g_hookBlocks;

	// --- Initialization ---

	void init(std::unique_ptr<backend::Backend> backend, const backend::ShaderSource& baseShader) {
		if (backend == nullptr) {
			LOG_ERROR(Renderer, "Primitives::init: no backend; the renderer stays off");
			return;
		}
		g_batchRenderer.reset();
		g_backend = std::move(backend);
		g_backend->setWindowSize(g_backbufferWidth, g_backbufferHeight);
		g_batchRenderer = std::make_unique<BatchRenderer>(*g_backend, baseShader);
		g_hookBlocks.clear();
		for (const backend::HookUniformBlock& block : baseShader.hookBlocks) {
			g_hookBlocks.push_back({.hook = block.hook != nullptr ? block.hook : "", .binding = block.binding, .size = block.size});
		}
		g_batchRenderer->setCoordinateSystem(g_coordinateSystem);

		// Initialize identity transform
		g_currentTransform = Foundation::Mat4(1.0F);
	}

	void shutdown() {
		g_batchRenderer.reset();
		g_backend.reset();
		g_coordinateSystem = nullptr;
		g_polygonCache.clear();
		g_hookBlocks.clear();
	}

	void setCoordinateSystem(CoordinateSystem* coordSystem) {
		g_coordinateSystem = coordSystem;
		// Also update the batch renderer with the coordinate system (even if nullptr)
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->setCoordinateSystem(g_coordinateSystem);
		}
	}

	void setFontRenderer(FontRenderer* fontRenderer) {
		g_fontRenderer = fontRenderer;
	}

	FontRenderer* getFontRenderer() {
		return g_fontRenderer;
	}

	void setFontAtlas(TextureHandle atlas, float pixelRange) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->setFontAtlas(atlas, pixelRange);
		}
	}

	void setTileAtlas(TextureHandle atlas, const std::vector<glm::vec4>& rects) {
		g_tileAtlasTexture = atlas;
		g_tileAtlasRects = rects;
	}

	TextureHandle getTileAtlasTexture() {
		return g_tileAtlasTexture;
	}

	const std::vector<glm::vec4>& getTileAtlasRects() {
		return g_tileAtlasRects;
	}

	// --- Textures ---

	TextureHandle createTexture(const TextureDesc& desc) {
		if (g_backend == nullptr) {
			LOG_ERROR(Renderer, "createTexture: the renderer is not initialized");
			return {};
		}
		return g_backend->createTexture(desc);
	}

	void updateTexture(TextureHandle handle, int x, int y, int width, int height, const void* pixels) {
		if (g_backend != nullptr) {
			g_backend->updateTexture(handle, x, y, width, height, pixels);
		}
	}

	void destroyTexture(TextureHandle handle) {
		if (g_backend != nullptr) {
			g_backend->destroyTexture(handle);
		}
	}

	void setFrameUpdateCallback(FrameUpdateCallback callback) {
		g_frameUpdateCallback = callback;
	}

	// --- Frame Lifecycle ---

	void clear(const Foundation::Color& color) {
		// Targets hold premultiplied colour, as everything drawn into them is.
		if (g_backend != nullptr) {
			g_backend->clear({color.r * color.a, color.g * color.a, color.b * color.a, color.a});
		}
	}

	void readBackbuffer(std::span<uint8_t> rgba, int& width, int& height) {
		width = g_backbufferWidth;
		height = g_backbufferHeight;
		if (rgba.empty() || width <= 0 || height <= 0 || g_backend == nullptr) {
			return;
		}
		const size_t needed = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
		if (rgba.size() < needed) {
			LOG_ERROR(Renderer, "readBackbuffer: %zu bytes given, %zu needed for %dx%d", rgba.size(), needed, width, height);
			return;
		}
		g_backend->readPixels({}, rgba);
	}

	void invalidateState() {
		if (g_backend != nullptr) {
			g_backend->invalidateState();
		}
	}

	void beginFrame() {
		if (g_backend != nullptr) {
			g_backend->beginFrame();
		}
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->beginFrame();
		}

		// Invoke frame update callback for FontRenderer cache LRU tracking
		if (g_frameUpdateCallback != nullptr) {
			g_frameUpdateCallback();
		}
	}

	void endFrame() {
		// Flush all batched geometry (shapes + text) in a single draw call
		// The base shader handles both SDF shapes and MSDF text
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->endFrame();
		}
		if (g_backend != nullptr) {
			g_backend->endFrame();
		}
	}

	void flush() {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->flush();
		}
	}

	void setViewport(int width, int height) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->setViewport(width, height);
		}
		// A RenderTarget's size is its own; only the backbuffer's is learned here. With
		// a coordinate system the window's framebuffer size is the backbuffer, whatever
		// size a world pass sets the projection to (worldsim's baked pass uses the logical size).
		if (!RenderTarget::anyBegun()) {
			if (g_coordinateSystem != nullptr) {
				const Viewport& window = g_coordinateSystem->viewport();
				g_backbufferWidth = window.framebufferWidth;
				g_backbufferHeight = window.framebufferHeight;
			} else {
				g_backbufferWidth = width;
				g_backbufferHeight = height;
			}
			if (g_backend != nullptr) {
				g_backend->setWindowSize(g_backbufferWidth, g_backbufferHeight);
			}
		}
	}

	void getViewport(int& width, int& height) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->getViewport(width, height);
		} else {
			width = 800;
			height = 600;
		}
	}

	void getLogicalViewport(int& width, int& height) {
		if (g_coordinateSystem != nullptr) {
			glm::vec2 windowSize = g_coordinateSystem->getWindowSize();
			width = static_cast<int>(windowSize.x);
			height = static_cast<int>(windowSize.y);
		} else if (g_batchRenderer != nullptr) {
			// Fallback to physical viewport if no coordinate system
			g_batchRenderer->getViewport(width, height);
		} else {
			width = 800;
			height = 600;
		}
	}

	// --- Coordinate System Helpers ---

	Foundation::Mat4 getScreenSpaceProjection() {
		if (g_coordinateSystem != nullptr) {
			return g_coordinateSystem->CreateScreenSpaceProjection();
		}
		// Fallback to default projection
		return glm::ortho(0.0F, 800.0F, 600.0F, 0.0F, -1.0F, 1.0F);
	}

	Foundation::Mat4 getWorldSpaceProjection() {
		if (g_coordinateSystem != nullptr) {
			return g_coordinateSystem->CreateWorldSpaceProjection();
		}
		// Fallback to default projection
		return glm::ortho(-400.0F, 400.0F, -300.0F, 300.0F, -1.0F, 1.0F);
	}

	float PercentWidth(float percent) {
		if (g_coordinateSystem != nullptr) {
			return g_coordinateSystem->PercentWidth(percent);
		}
		return 800.0F * (percent / 100.0F);
	}

	float PercentHeight(float percent) {
		if (g_coordinateSystem != nullptr) {
			return g_coordinateSystem->PercentHeight(percent);
		}
		return 600.0F * (percent / 100.0F);
	}

	Foundation::Vec2 PercentSize(float widthPercent, float heightPercent) {
		if (g_coordinateSystem != nullptr) {
			return g_coordinateSystem->PercentSize(widthPercent, heightPercent);
		}
		return Foundation::Vec2(800.0F * (widthPercent / 100.0F), 600.0F * (heightPercent / 100.0F));
	}

	Foundation::Vec2 PercentPosition(float xPercent, float yPercent) {
		if (g_coordinateSystem != nullptr) {
			return g_coordinateSystem->PercentPosition(xPercent, yPercent);
		}
		return Foundation::Vec2(800.0F * (xPercent / 100.0F), 600.0F * (yPercent / 100.0F));
	}

	// --- Drawing Functions ---

	void drawRect(const RectArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		// Use SDF-based rendering for fill, border, and corner radius
		// Extract corner radius from border if present, otherwise use 0
		float cornerRadius = 0.0F;
		if (args.style.border.has_value() && args.style.border.value().cornerRadius > 0.0F) {
			cornerRadius = args.style.border.value().cornerRadius;
		}

		// Outer box-shadow / glow sits behind the fill (emit first).
		if (args.style.boxShadow.has_value()) {
			g_batchRenderer->addShadowQuad(
				args.bounds, args.style.boxShadow.value(), cornerRadius, static_cast<float>(args.zIndex), args.blend
			);
		}

		// Single AddQuad call handles fill, border, rounded corners, and an optional
		// gradient (per-corner vertex colors) via the GPU.
		g_batchRenderer->addQuad(
			args.bounds, args.style.fill, args.style.border, cornerRadius, args.style.gradient, static_cast<float>(args.zIndex), args.blend
		);
	}

	void drawLine(const LineArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		// Draw line as thin rectangle
		Foundation::Vec2 dir = args.end - args.start;
		float			 length = glm::length(dir);

		if (length < 0.001F) {
			return; // Too short to draw
		}

		Foundation::Vec2 normal = Foundation::Vec2(-dir.y, dir.x) / length;
		Foundation::Vec2 offset = normal * (args.style.width * 0.5F);

		// Rotated quad following the line direction. Emit it as two triangles, not
		// the axis-aligned bounding box of the corners: a diagonal line's AABB is a
		// large filled rectangle, which paints any non-orthogonal edge as a box.
		const Foundation::Vec2 quad[4] = {
			args.start - offset, // start, left
			args.start + offset, // start, right
			args.end + offset,	 // end, right
			args.end - offset,	 // end, left
		};
		static constexpr uint16_t kQuadIndices[6] = {0, 1, 2, 0, 2, 3};
		g_batchRenderer->addTriangles(quad, kQuadIndices, 4, 6, args.style.color, nullptr, static_cast<float>(args.zIndex), args.blend);
	}

	void drawTriangles(const TrianglesArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		g_batchRenderer->addTriangles(
			args.vertices,
			args.indices,
			args.vertexCount,
			args.indexCount,
			args.color,
			args.colors,
			static_cast<float>(args.zIndex),
			args.blend
		);
	}

	void drawCircle(const CircleArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		// Tessellate circle into triangle fan
		constexpr int	segments = 64; // Enough for smooth circles
		constexpr float angleStep = (2.0F * std::numbers::pi_v<float>) / static_cast<float>(segments);
		constexpr int	vertexCount = segments + 1; // Center + perimeter vertices
		constexpr int	indexCount = segments * 3;	// Each segment creates a triangle

		// Ensure vertex count fits in uint16_t index buffer
		static_assert(vertexCount <= 65535, "Circle vertex count exceeds uint16_t index range");

		// Use thread-local buffers to avoid allocations on every call
		static thread_local std::vector<Foundation::Vec2> vertices;
		static thread_local std::vector<uint16_t>		  indices;
		vertices.clear();
		indices.clear();
		vertices.reserve(vertexCount);
		indices.reserve(indexCount);

		// Center vertex
		vertices.push_back(args.center);

		// Perimeter vertices
		for (int i = 0; i < segments; ++i) {
			float angle = static_cast<float>(i) * angleStep;
			float x = args.center.x + args.radius * std::cos(angle);
			float y = args.center.y + args.radius * std::sin(angle);
			vertices.emplace_back(x, y);
		}

		// Generate triangle fan indices
		for (int i = 0; i < segments; ++i) {
			indices.push_back(0);											  // Center
			indices.push_back(static_cast<uint16_t>(i + 1));				  // Current perimeter vertex
			indices.push_back(static_cast<uint16_t>(1 + (i + 1) % segments)); // Next perimeter vertex
		}

		// Draw filled circle
		if (args.style.fill.a > 0.0F) {
			drawTriangles(
				{.vertices = vertices.data(),
				 .indices = indices.data(),
				 .vertexCount = vertices.size(),
				 .indexCount = indices.size(),
				 .color = args.style.fill,
				 .id = args.id,
				 .zIndex = args.zIndex,
				 .blend = args.blend}
			);
		}

		// Draw border if specified
		if (args.style.border.has_value()) {
			const auto& border = args.style.border.value();

			// Draw border as connected line segments
			for (int i = 0; i < segments; ++i) {
				Foundation::Vec2 start = vertices[i + 1];
				Foundation::Vec2 end = vertices[1 + (i + 1) % segments];
				drawLine({.start = start, .end = end, .style = {.color = border.color, .width = border.width}, .blend = args.blend});
			}
		}
	}

	namespace {

		// FNV-1a over the outline's coordinate bits.
		uint64_t hashOutline(std::span<const Foundation::Vec2> outline) {
			uint64_t hash = 14695981039346656037ULL;
			for (const Foundation::Vec2& point : outline) {
				for (const float coordinate : {point.x, point.y}) {
					uint32_t bits = 0;
					std::memcpy(&bits, &coordinate, sizeof(bits));
					for (int byte = 0; byte < 4; ++byte) {
						hash ^= (bits >> (byte * 8)) & 0xFFU;
						hash *= 1099511628211ULL;
					}
				}
			}
			return hash;
		}

		// The cached fill for `outline`, tessellating it on a miss. An outline the
		// tessellator rejects caches an empty mesh, so it is reported once.
		const renderer::TessellatedMesh& polygonFill(std::span<const Foundation::Vec2> outline) {
			const uint64_t hash = hashOutline(outline);
			const auto	   found = g_polygonCache.find(hash);
			if (found != g_polygonCache.end() && std::ranges::equal(found->second.outline, outline)) {
				return found->second.mesh;
			}
			if (g_polygonCache.size() >= kPolygonCacheCapacity) {
				g_polygonCache.clear();
			}
			PolygonCacheEntry entry;
			entry.outline.assign(outline.begin(), outline.end());
			renderer::Tessellator tessellator;
			if (!tessellator.Tessellate(renderer::VectorPath(entry.outline), entry.mesh)) {
				LOG_WARNING(Renderer, "drawPolygon: could not tessellate a %zu-point outline", outline.size());
				entry.mesh.clear();
			}
			PolygonCacheEntry& stored = g_polygonCache.insert_or_assign(hash, std::move(entry)).first->second;
			return stored.mesh;
		}

	} // namespace

	void drawPolygon(const PolygonArgs& args) {
		if (g_batchRenderer == nullptr || args.vertices.size() < 3) {
			return;
		}

		const float zIndex = static_cast<float>(args.zIndex);
		if (args.style.fill.a > 0.0F) {
			const renderer::TessellatedMesh& mesh = polygonFill(args.vertices);
			if (!mesh.indices.empty()) {
				g_batchRenderer->addTriangles(
					mesh.vertices.data(),
					mesh.indices.data(),
					mesh.vertices.size(),
					mesh.indices.size(),
					args.style.fill,
					nullptr,
					zIndex,
					args.blend
				);
			}
		}

		if (args.style.border.has_value() && args.style.border->width > 0.0F) {
			const Foundation::LineStyle& border = args.style.border.value();
			// The fill is not anti-aliased: a pixel is fill or not by its centre. The
			// border's inner edge runs a pixel inside the outline, so its edge ramp (a pixel
			// either side) lies over fill and every pixel outside the fill is fully border.
			static thread_local std::vector<StrokePiece>	  pieces;
			static thread_local std::vector<Foundation::Vec2> wedges;
			strokeOutline(args.vertices, border.width, g_batchRenderer->getLocalPixel(), pieces, wedges);
			g_batchRenderer->addStroke(pieces, wedges, border.color, zIndex, args.blend);
		}
	}

	void drawPolyline(const PolylineArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}
		static thread_local std::vector<StrokePiece> pieces;
		strokePolyline(args, pieces);
		if (!pieces.empty()) {
			g_batchRenderer->addStroke(pieces, {}, args.color, static_cast<float>(args.zIndex), args.blend);
		}
	}

	void drawArc(const ArcArgs& args) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->addArc(args);
		}
	}

	void drawImage(const ImageArgs& args) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->addImageQuad(args);
		}
	}

	void drawText(const TextArgs& args) {
		if (g_fontRenderer == nullptr || g_batchRenderer == nullptr) {
			LOG_WARNING(
				Engine,
				"drawText called but renderer not initialized (font=%p, batch=%p)",
				static_cast<void*>(g_fontRenderer),
				static_cast<void*>(g_batchRenderer.get())
			);
			return;
		}

		// CSS text-transform: fold the case once up front so measure and emit agree.
		std::string effective = args.text;
		if (args.transform == Foundation::TextTransform::Uppercase) {
			for (char& c : effective) {
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			}
		}

		// CSS text-align within the optional [position, position+box] rect. With no
		// box, or Left/Top alignment, the origin stays at position (plain top-left)
		// and we skip the measure entirely (it's the common case, keep it cheap).
		Foundation::Vec2 origin = args.position;
		const bool		 needsHAlign = args.boxWidth > 0.0F && args.hAlign != Foundation::HorizontalAlign::Left;
		const bool		 needsVAlign = args.boxHeight > 0.0F && args.vAlign != Foundation::VerticalAlign::Top;
		if (needsHAlign || needsVAlign) {
			const glm::vec2 m = g_fontRenderer->MeasureText(effective, args.scale, args.font, args.letterSpacing);
			if (needsHAlign) {
				origin.x += (args.hAlign == Foundation::HorizontalAlign::Center) ? (args.boxWidth - m.x) * 0.5F : (args.boxWidth - m.x);
			}
			if (needsVAlign) {
				origin.y += (args.vAlign == Foundation::VerticalAlign::Middle) ? (args.boxHeight - m.y) * 0.5F : (args.boxHeight - m.y);
			}
		}

		// Emit one pass of the string at a position/color (shared by the shadow
		// and the main text so text-shadow is a single inline property, not a
		// caller-side double draw).
		const auto emit = [&](Foundation::Vec2 pos, const Foundation::Color& col) {
			std::vector<FontRenderer::GlyphQuad> quads;
			g_fontRenderer->generateGlyphQuads(
				effective, glm::vec2(pos.x, pos.y), args.scale, glm::vec4(col.r, col.g, col.b, col.a), quads, args.font, args.letterSpacing
			);
			drawGlyphs(quads.data(), quads.size(), args.font, static_cast<float>(args.zIndex), args.blend);
		};

		if (args.shadowColor.a > 0.0F) {
			emit({origin.x + args.shadowOffset.x, origin.y + args.shadowOffset.y}, args.shadowColor);
		}
		emit(origin, args.color);
	}

	void drawGlyphs(const FontRenderer::GlyphQuad* quads, size_t count, FontFamily family, float zIndex, BlendMode blend) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		// Each glyph carries its family's atlas so the batch groups it with other
		// glyphs from the same atlas (see BatchRenderer::flush). With no font
		// renderer the quads sample the default atlas, as family-less callers always did.
		const TextureHandle atlas = g_fontRenderer != nullptr ? g_fontRenderer->getAtlasTexture(family) : TextureHandle{};
		for (size_t i = 0; i < count; ++i) {
			const FontRenderer::GlyphQuad& quad = quads[i];
			g_batchRenderer->addTextQuad(
				Foundation::Vec2(quad.position.x, quad.position.y),
				Foundation::Vec2(quad.size.x, quad.size.y),
				Foundation::Vec2(quad.uvMin.x, quad.uvMin.y),
				Foundation::Vec2(quad.uvMax.x, quad.uvMax.y),
				Foundation::Color(quad.color.r, quad.color.g, quad.color.b, quad.color.a),
				Foundation::Vec2(quad.runOrigin.x, quad.runOrigin.y),
				atlas,
				zIndex,
				blend
			);
		}
	}

	// --- World-Space Instancing ---

	InstancedMeshHandle uploadInstancedMesh(const renderer::TessellatedMesh& mesh, uint32_t maxInstances) {
		if (g_batchRenderer == nullptr) {
			return {};
		}
		return g_batchRenderer->uploadInstancedMesh(mesh, maxInstances);
	}

	void releaseInstancedMesh(InstancedMeshHandle& handle) {
		BatchRenderer::releaseInstancedMesh(g_backend.get(), handle);
	}

	void drawInstanced(const InstancedMeshHandle& handle, const InstanceData* instances, uint32_t count, const WorldCamera& camera) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->drawInstanced(handle, instances, count, camera.position, camera.zoom, camera.pixelsPerMeter);
		}
	}

	void setGroundcover(const GroundcoverParams& params) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->setGroundcover(params);
		}
	}

	void setBakedAlpha(float alpha) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->setBakedAlpha(alpha);
		}
	}

	void setHookUniforms(std::string_view hook, std::span<const std::byte> bytes) {
		if (g_backend == nullptr) {
			return;
		}
		const auto entry =
			std::find_if(g_hookBlocks.begin(), g_hookBlocks.end(), [hook](const HookBlockEntry& block) { return block.hook == hook; });
		if (entry == g_hookBlocks.end() || bytes.size() != entry->size) {
			LOG_ERROR(
				Renderer,
				"setHookUniforms: the base shader has no %u-byte block for hook %.*s",
				static_cast<unsigned>(bytes.size()),
				static_cast<int>(hook.size()),
				hook.data()
			);
			return;
		}
		// Straight to the backend: only instanced draws run the hooks, and they are not
		// batched, so every draw issued after this reads it and none before.
		g_backend->setHookUniforms(entry->binding, bytes);
	}

	void setHookUniforms(const backend::HookUniformBlock& block, std::span<const std::byte> bytes) {
		const std::string_view hook = block.hook != nullptr ? block.hook : "";
		const auto			   entry =
			std::find_if(g_hookBlocks.begin(), g_hookBlocks.end(), [hook](const HookBlockEntry& known) { return known.hook == hook; });
		if (g_backend != nullptr && (entry == g_hookBlocks.end() || entry->binding != block.binding || entry->size != block.size)) {
			LOG_ERROR(Renderer, "setHookUniforms: hook %.*s's block is not the base shader's", static_cast<int>(hook.size()), hook.data());
			return;
		}
		setHookUniforms(hook, bytes);
	}

	void beginBakedPass(const WorldCamera& camera) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->beginBakedPass(camera);
		}
	}

	void endBakedPass() {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->endBakedPass();
		}
	}

	// --- Clip Stack (Shader-based, batching-friendly) ---

	// Helper to compute Vec4 bounds from ClipSettings
	static Foundation::Vec4 ComputeClipBounds(const Foundation::ClipSettings& settings) {
		// Check for ClipRect (fast path)
		if (const auto* clipRect = std::get_if<Foundation::ClipRect>(&settings.shape)) {
			if (clipRect->bounds.has_value()) {
				const auto& rect = clipRect->bounds.value();
				return Foundation::Vec4(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
			}
			// If no explicit bounds, use full viewport (no clipping)
			return Foundation::Vec4(0.0F, 0.0F, 0.0F, 0.0F);
		}

		// Stub for future complex shapes (rounded rect, circle, path)
		// These will use stencil buffer in future phases
		if (std::holds_alternative<Foundation::ClipRoundedRect>(settings.shape)) {
			// TODO: Phase 3 - use stencil buffer for rounded rect
			// For now, use bounding box approximation
			const auto& rr = std::get<Foundation::ClipRoundedRect>(settings.shape);
			if (rr.bounds.has_value()) {
				const auto& rect = rr.bounds.value();
				return Foundation::Vec4(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
			}
		}

		if (std::holds_alternative<Foundation::ClipCircle>(settings.shape)) {
			// TODO: Phase 3 - use stencil buffer for circle
			// For now, use bounding box approximation
			const auto& circle = std::get<Foundation::ClipCircle>(settings.shape);
			float		minX = circle.center.x - circle.radius;
			float		minY = circle.center.y - circle.radius;
			float		maxX = circle.center.x + circle.radius;
			float		maxY = circle.center.y + circle.radius;
			return Foundation::Vec4(minX, minY, maxX, maxY);
		}

		if (std::holds_alternative<Foundation::ClipPath>(settings.shape)) {
			// TODO: Phase 3 - use stencil buffer for path
			// For now, compute bounding box of path vertices
			const auto& path = std::get<Foundation::ClipPath>(settings.shape);
			if (!path.vertices.empty()) {
				float minX = path.vertices[0].x;
				float minY = path.vertices[0].y;
				float maxX = minX;
				float maxY = minY;
				for (const auto& v : path.vertices) {
					minX = std::min(minX, v.x);
					minY = std::min(minY, v.y);
					maxX = std::max(maxX, v.x);
					maxY = std::max(maxY, v.y);
				}
				return Foundation::Vec4(minX, minY, maxX, maxY);
			}
		}

		// No clipping
		return Foundation::Vec4(0.0F, 0.0F, 0.0F, 0.0F);
	}

	// Helper to intersect two clip bounds
	static Foundation::Vec4 IntersectClipBounds(const Foundation::Vec4& a, const Foundation::Vec4& b) {
		// If either is empty (0,0,0,0), use the other
		bool aEmpty = (a.z <= a.x || a.w <= a.y);
		bool bEmpty = (b.z <= b.x || b.w <= b.y);

		if (aEmpty)
			return b;
		if (bEmpty)
			return a;

		// Intersect the two rectangles
		float minX = std::max(a.x, b.x);
		float minY = std::max(a.y, b.y);
		float maxX = std::min(a.z, b.z);
		float maxY = std::min(a.w, b.w);

		// Check for no intersection (empty result)
		if (maxX <= minX || maxY <= minY) {
			return Foundation::Vec4(0.0F, 0.0F, 0.0F, 0.0F);
		}

		return Foundation::Vec4(minX, minY, maxX, maxY);
	}

	void pushClip(const Foundation::ClipSettings& settings) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		// Compute bounds for this clip
		Foundation::Vec4 bounds = ComputeClipBounds(settings);

		// Intersect with current clip (nested clipping)
		if (!g_clipStack.empty()) {
			bounds = IntersectClipBounds(g_clipStack.top().bounds, bounds);
		}

		// Push to stack
		g_clipStack.push({settings, bounds});

		// Update batch renderer with new clip bounds
		g_batchRenderer->setClipBounds(bounds);
	}

	void popClip() {
		if (g_clipStack.empty()) {
			return;
		}

		g_clipStack.pop();

		// Restore parent clip or clear if stack is empty
		if (g_batchRenderer != nullptr) {
			if (g_clipStack.empty()) {
				g_batchRenderer->clearClipBounds();
			} else {
				g_batchRenderer->setClipBounds(g_clipStack.top().bounds);
			}
		}
	}

	Foundation::Vec4 getCurrentClipBounds() {
		if (g_clipStack.empty()) {
			return Foundation::Vec4(0.0F, 0.0F, 0.0F, 0.0F);
		}
		return g_clipStack.top().bounds;
	}

	bool IsClipActive() {
		if (g_clipStack.empty()) {
			return false;
		}
		const auto& bounds = g_clipStack.top().bounds;
		// Clip is active if bounds form a valid rectangle (maxX > minX, maxY > minY)
		return (bounds.z > bounds.x) && (bounds.w > bounds.y);
	}

	// --- Convenience Functions for Future Clip Shapes ---
	// These delegate to pushClip() with the appropriate ClipShape variant.
	// Currently use bounding-box approximation; Phase 3 will add stencil-buffer support.

	void pushClipRoundedRect(const Foundation::Rect& bounds, float cornerRadius) {
		Foundation::ClipSettings settings;
		settings.shape = Foundation::ClipRoundedRect{.bounds = bounds, .cornerRadius = cornerRadius};
		pushClip(settings);
	}

	void pushClipCircle(const Foundation::Vec2& center, float radius) {
		Foundation::ClipSettings settings;
		settings.shape = Foundation::ClipCircle{.center = center, .radius = radius};
		pushClip(settings);
	}

	void pushClipPath(const std::vector<Foundation::Vec2>& vertices) {
		Foundation::ClipSettings settings;
		settings.shape = Foundation::ClipPath{.vertices = vertices};
		pushClip(settings);
	}

	// --- Scissor Stack (Legacy) ---

	void PushScissor(const Foundation::Rect& clipRect) {
		// Intersect with current scissor (nested clipping)
		if (!g_scissorStack.empty()) {
			g_currentScissor = Foundation::Rect::intersection(g_currentScissor, clipRect);
		} else {
			g_currentScissor = clipRect;
		}

		g_scissorStack.push(g_currentScissor);

		// TODO: Apply to OpenGL state
		// For now, just track it
	}

	void PopScissor() {
		if (!g_scissorStack.empty()) {
			g_scissorStack.pop();

			if (!g_scissorStack.empty()) {
				g_currentScissor = g_scissorStack.top();
			} else {
				g_currentScissor = Foundation::Rect(); // Clear scissor
			}

			// TODO: Apply to OpenGL state
		}
	}

	Foundation::Rect getCurrentScissor() {
		return g_currentScissor;
	}

	// --- Transform Stack ---

	void PushTransform(const Foundation::Mat4& transform) {
		g_transformStack.push(g_currentTransform);
		g_currentTransform = g_currentTransform * transform;

		// Apply to batch renderer (will be baked into vertex positions at add-time)
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->setTransform(g_currentTransform);
		}
	}

	void PopTransform() {
		if (!g_transformStack.empty()) {
			g_currentTransform = g_transformStack.top();
			g_transformStack.pop();

			// Apply to batch renderer (will be baked into vertex positions at add-time)
			if (g_batchRenderer != nullptr) {
				g_batchRenderer->setTransform(g_currentTransform);
			}
		}
	}

	Foundation::Mat4 getCurrentTransform() {
		return g_currentTransform;
	}

	// --- Statistics ---

	RenderStats getStats() {
		RenderStats stats = {};

		if (g_batchRenderer != nullptr) {
			auto batchStats = g_batchRenderer->getStats();
			stats.drawCalls = batchStats.drawCalls;
			stats.vertexCount = batchStats.vertexCount;
			stats.triangleCount = batchStats.triangleCount;
		}

		return stats;
	}

} // namespace Renderer::Primitives

namespace Renderer::backend {

	Backend* active() {
		return Primitives::g_backend.get();
	}

} // namespace Renderer::backend
