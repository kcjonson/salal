// Primitive Rendering API implementation.
// Provides immediate-mode 2D drawing functions with internal batching.

#include "primitives/Primitives.h"
#include "CoordinateSystem/CoordinateSystem.h"
#include "font/FontRenderer.h"
#include "primitives/BatchRenderer.h"
#include "primitives/BoundaryLoops.h"
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
	static bool							g_warnedNoFontRenderer = false; // drawText warns once until a font renderer is set
	static TextureHandle				g_tileAtlasTexture;
	static std::vector<glm::vec4>		g_tileAtlasRects;
	static std::stack<Foundation::Mat4> g_transformStack;
	static Foundation::Mat4				g_currentTransform = Foundation::Mat4(1.0F);

	// The clip stack, each entry the intersection of every rect pushed up to it,
	// (minX, minY, maxX, maxY). One with no area is the empty clip, which culls every
	// draw; an intersection only shrinks, so every push over an empty clip stays empty.
	static std::vector<Foundation::Vec4> g_clipStack;

	// beginFrame found a clip or transform still pushed, and said so.
	static bool g_warnedUnbalancedStacks = false;

	// Tessellated polygon fills, keyed by a hash of the outline's vertex bits and
	// checked against a copy of the outline, so a hash collision is a miss, never a
	// wrong mesh. A silhouette drawn every frame (under a changing PushTransform)
	// tessellates once. Bounded by emptying the whole cache when it is full: a
	// frame's working set of silhouettes is far below the capacity, so it refills
	// in one frame, and nothing per entry needs tracking. The mesh's boundary loops,
	// which the fill's feather runs along, are found with it.
	struct PolygonCacheEntry {
		std::vector<Foundation::Vec2> outline;
		renderer::TessellatedMesh	  mesh;
		BoundaryLoops				  boundary;
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
		g_warnedNoFontRenderer = false;
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

	// --- Frame Lifecycle ---

	void clear(const Foundation::Color& color) {
		// Targets hold premultiplied colour, as everything drawn into them is.
		if (g_backend != nullptr) {
			if (g_batchRenderer != nullptr) {
				g_batchRenderer->beginGpuSpan();
			}
			g_backend->clear({color.r * color.a, color.g * color.a, color.b * color.a, color.a});
			if (g_batchRenderer != nullptr) {
				g_batchRenderer->endGpuSpan();
			}
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
		// A frame starts with no clip and no transform. One still pushed is a push whose pop never
		// ran, as when a render threw between them and the app logged it and went on; left, it would
		// clip or move every frame after.
		if (!g_clipStack.empty() || !g_transformStack.empty()) {
			if (!g_warnedUnbalancedStacks) {
				g_warnedUnbalancedStacks = true;
				LOG_WARNING(
					Renderer,
					"beginFrame: %zu clips and %zu transforms were still pushed, a push without its pop; dropped (warned once)",
					g_clipStack.size(),
					g_transformStack.size()
				);
			}
			g_clipStack.clear();
			g_transformStack = {};
			g_currentTransform = Foundation::Mat4(1.0F);
		}
		if (g_backend != nullptr) {
			g_backend->beginFrame();
		}
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->beginFrame();
			g_batchRenderer->setTransform(g_currentTransform);
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

	void sortBarrier() {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->sortBarrier();
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
		g_batchRenderer->addTriangles(
			{.vertices = quad,
			 .indices = kQuadIndices,
			 .color = args.style.color,
			 .zIndex = static_cast<float>(args.zIndex),
			 .blend = args.blend}
		);
	}

	void drawTriangles(const TrianglesArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		static thread_local BoundaryLoops boundary;
		const bool						  feathered =
			args.antiAlias && findBoundaryLoops({args.vertices, args.vertexCount}, {args.indices, args.indexCount}, boundary);
		g_batchRenderer->addTriangles(
			{.vertices = {args.vertices, args.vertexCount},
			 .indices = {args.indices, args.indexCount},
			 .color = args.color,
			 .colors = args.colors,
			 .zIndex = static_cast<float>(args.zIndex),
			 .blend = args.blend,
			 .feather = feathered ? &boundary : nullptr}
		);
	}

	void drawCircle(const CircleArgs& args) {
		if (g_batchRenderer == nullptr || !(args.radius > 0.0F)) {
			return;
		}

		if (args.style.fill.a > 0.0F) {
			g_batchRenderer->addArc(
				{.center = args.center,
				 .outerRadius = args.radius,
				 .color = args.style.fill,
				 .id = args.id,
				 .zIndex = args.zIndex,
				 .blend = args.blend}
			);
		}

		if (args.style.border.has_value()) {
			const Foundation::BorderStyle& border = args.style.border.value();
			float						   outerRadius = args.radius + (border.width * 0.5F);
			switch (border.position) {
				case Foundation::BorderPosition::Inside:
					outerRadius = args.radius;
					break;
				case Foundation::BorderPosition::Center:
					break;
				case Foundation::BorderPosition::Outside:
					outerRadius = args.radius + border.width;
					break;
			}
			// An inner radius below zero makes the ring a disk; addArc clamps it.
			g_batchRenderer->addArc(
				{.center = args.center,
				 .innerRadius = outerRadius - border.width,
				 .outerRadius = outerRadius,
				 .color = border.color,
				 .id = args.id,
				 .zIndex = args.zIndex,
				 .blend = args.blend}
			);
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

		// The cached fill for `outline`, tessellating it on a miss, its repeated points
		// dropped first so no zero-area triangle stands between neighbours. An outline the
		// tessellator rejects caches an empty mesh, so it is reported once; a mesh whose
		// boundary doesn't chain into loops (an outline touching itself) caches none, said
		// once, and its fill stays aliased.
		const PolygonCacheEntry& polygonFill(std::span<const Foundation::Vec2> outline) {
			const uint64_t hash = hashOutline(outline);
			const auto	   found = g_polygonCache.find(hash);
			if (found != g_polygonCache.end() && std::ranges::equal(found->second.outline, outline)) {
				return found->second;
			}
			if (g_polygonCache.size() >= kPolygonCacheCapacity) {
				g_polygonCache.clear();
			}
			PolygonCacheEntry entry;
			entry.outline.assign(outline.begin(), outline.end());
			static thread_local std::vector<Foundation::Vec2> distinct;
			distinctPoints(outline, true, distinct);
			renderer::Tessellator tessellator;
			if (!tessellator.Tessellate(renderer::VectorPath(distinct), entry.mesh)) {
				LOG_WARNING(Renderer, "drawPolygon: could not tessellate a %zu-point outline", outline.size());
				entry.mesh.clear();
			}
			if (!findBoundaryLoops(entry.mesh.vertices, entry.mesh.indices, entry.boundary) && !entry.mesh.indices.empty()) {
				LOG_DEBUG(
					Renderer,
					"drawPolygon: a %zu-point outline's fill has no closed boundary, as where it touches itself, so it is drawn aliased",
					outline.size()
				);
			}
			return g_polygonCache.insert_or_assign(hash, std::move(entry)).first->second;
		}

	} // namespace

	void drawPolygon(const PolygonArgs& args) {
		if (g_batchRenderer == nullptr || args.vertices.size() < 3) {
			return;
		}

		const float zIndex = static_cast<float>(args.zIndex);
		if (args.style.fill.a > 0.0F) {
			const PolygonCacheEntry& fill = polygonFill(args.vertices);
			if (!fill.mesh.indices.empty()) {
				g_batchRenderer->addTriangles(
					{.vertices = fill.mesh.vertices,
					 .indices = fill.mesh.indices,
					 .color = args.style.fill,
					 .zIndex = zIndex,
					 .blend = args.blend,
					 .feather = &fill.boundary}
				);
			}
		}

		if (args.style.border.has_value() && args.style.border->width > 0.0F) {
			const Foundation::LineStyle& border = args.style.border.value();
			// The border's inner edge runs a pixel inside the outline, so its edge ramp (a
			// pixel either side) lies over the fill, and outside the outline, where the
			// fill's feather fades, the border is whole.
			static thread_local std::vector<StrokePiece> pieces;
			strokeOutline(args.vertices, border.width, g_batchRenderer->getLocalPixel(), pieces);
			g_batchRenderer->addStroke(pieces, border.color, zIndex, args.blend);
		}
	}

	void drawPolyline(const PolylineArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}
		static thread_local std::vector<StrokePiece> pieces;
		// Points within a quarter of a device pixel of the line drawn without them go, so a
		// noisy line's near-repeats don't uncut the joints either side.
		strokePolyline(args, g_batchRenderer->getLocalPixel(), 0.25F * g_batchRenderer->getLocalDevicePixel(), pieces);
		if (!pieces.empty()) {
			g_batchRenderer->addStroke(pieces, args.color, static_cast<float>(args.zIndex), args.blend);
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
			if (!g_warnedNoFontRenderer) {
				g_warnedNoFontRenderer = true;
				LOG_WARNING(
					Engine,
					"drawText called but renderer not initialized (font=%p, batch=%p); no text until it is",
					static_cast<void*>(g_fontRenderer),
					static_cast<void*>(g_batchRenderer.get())
				);
			}
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
			drawGlyphs(quads.data(), quads.size(), static_cast<float>(args.zIndex), args.blend);
		};

		if (args.shadowColor.a > 0.0F) {
			emit({origin.x + args.shadowOffset.x, origin.y + args.shadowOffset.y}, args.shadowColor);
		}
		emit(origin, args.color);
	}

	void drawGlyphs(const FontRenderer::GlyphQuad* quads, size_t count, float zIndex, BlendMode blend) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		// Each glyph samples the atlas its UVs were laid out in, at that atlas's range,
		// both of which the quad carries; the batch splits draws where the atlas
		// changes (see BatchRenderer::plan).
		for (size_t i = 0; i < count; ++i) {
			const FontRenderer::GlyphQuad& quad = quads[i];
			g_batchRenderer->addTextQuad(
				Foundation::Vec2(quad.position.x, quad.position.y),
				Foundation::Vec2(quad.size.x, quad.size.y),
				Foundation::Vec2(quad.uvMin.x, quad.uvMin.y),
				Foundation::Vec2(quad.uvMax.x, quad.uvMax.y),
				Foundation::Color(quad.color.r, quad.color.g, quad.color.b, quad.color.a),
				Foundation::Vec2(quad.runOrigin.x, quad.runOrigin.y),
				quad.atlas,
				quad.pixelRange,
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

	// --- Clip Stack ---

	void pushClip(const Foundation::Rect& rect) {
		if (g_batchRenderer == nullptr) {
			return;
		}
		Foundation::Vec4 bounds(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
		if (std::isnan(bounds.x) || std::isnan(bounds.y) || std::isnan(bounds.z) || std::isnan(bounds.w)) {
			// std::max and std::min would drop a NaN against the parent's bounds and keep the parent.
			bounds = Foundation::Vec4(0.0F, 0.0F, 0.0F, 0.0F);
		} else if (!g_clipStack.empty()) {
			const Foundation::Vec4& parent = g_clipStack.back();
			bounds = {
				std::max(parent.x, bounds.x), std::max(parent.y, bounds.y), std::min(parent.z, bounds.z), std::min(parent.w, bounds.w)
			};
		}
		g_clipStack.push_back(bounds);
		g_batchRenderer->setClipBounds(bounds);
	}

	void popClip() {
		if (g_clipStack.empty()) {
			return;
		}
		g_clipStack.pop_back();
		if (g_batchRenderer != nullptr) {
			if (g_clipStack.empty()) {
				g_batchRenderer->clearClipBounds();
			} else {
				g_batchRenderer->setClipBounds(g_clipStack.back());
			}
		}
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
			stats.instanceCount = batchStats.instanceCount;
			stats.uploadBytes = batchStats.uploadBytes;
			stats.flushCount = batchStats.flushCount;
			stats.flushMs = batchStats.flushMs;
		}

		return stats;
	}

	void setGpuTiming(bool enabled) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->setGpuTiming(enabled);
		}
	}

	GpuFrameTime getGpuFrameTime() {
		if (g_batchRenderer == nullptr) {
			return {};
		}
		const GPUFrameTimer& timer = g_batchRenderer->getGpuTimer();
		return {.ms = timer.lastFrameMs(), .frames = timer.framesTimed()};
	}

} // namespace Renderer::Primitives

namespace Renderer::backend {

	Backend* active() {
		return Primitives::g_backend.get();
	}

} // namespace Renderer::backend
