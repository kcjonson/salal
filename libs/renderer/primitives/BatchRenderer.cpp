// Uber Batch Renderer implementation.
// Accumulates 2D geometry (shapes + text) and renders in optimized batches.

#include "primitives/BatchRenderer.h"
#include "CoordinateSystem/CoordinateSystem.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <utility>

namespace Renderer {

	namespace {
		// Helper to transform a 2D position by a 4x4 matrix
		// isIdentity flag is pre-computed in setTransform() to avoid per-vertex checks
		inline Foundation::Vec2 TransformPosition(const Foundation::Vec2& pos, const Foundation::Mat4& transform, bool isIdentity) {
			if (isIdentity) {
				return pos;
			}
			// Apply transform: multiply position by matrix
			Foundation::Vec4 result = transform * Foundation::Vec4(pos.x, pos.y, 0.0F, 1.0F);
			return Foundation::Vec2(result.x, result.y);
		}

		constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;

		// The stream buffers' first size; the backend grows them as a flush needs.
		constexpr size_t kInitialVertexBytes = 4096 * sizeof(UberVertex);
		constexpr size_t kInitialIndexBytes = 6144 * sizeof(uint32_t);
	} // namespace

	// --- InstancedMeshHandle ---
	// Defined here because releasing goes through Primitives, which InstanceData.h does not include.

	InstancedMeshHandle::~InstancedMeshHandle() {
		Primitives::releaseInstancedMesh(*this);
	}

	InstancedMeshHandle::InstancedMeshHandle(InstancedMeshHandle&& other) noexcept
		: geometry(std::exchange(other.geometry, {})),
		  vertices(std::exchange(other.vertices, {})),
		  indices(std::exchange(other.indices, {})),
		  instances(std::exchange(other.instances, {})),
		  indexCount(std::exchange(other.indexCount, 0)),
		  vertexCount(std::exchange(other.vertexCount, 0)),
		  maxInstances(std::exchange(other.maxInstances, 0)) {}

	InstancedMeshHandle& InstancedMeshHandle::operator=(InstancedMeshHandle&& other) noexcept {
		if (this != &other) {
			Primitives::releaseInstancedMesh(*this);
			geometry = std::exchange(other.geometry, {});
			vertices = std::exchange(other.vertices, {});
			indices = std::exchange(other.indices, {});
			instances = std::exchange(other.instances, {});
			indexCount = std::exchange(other.indexCount, 0);
			vertexCount = std::exchange(other.vertexCount, 0);
			maxInstances = std::exchange(other.maxInstances, 0);
		}
		return *this;
	}

	BatchRenderer::BatchRenderer(backend::Backend& renderBackend)
		: gpu(renderBackend),
		  pipeline(renderBackend.createPipeline({.vertex = "uber.vert", .fragment = "uber.frag"})),
		  vertexBuffer(renderBackend.createBuffer(backend::BufferUsage::Stream, kInitialVertexBytes, nullptr)),
		  indexBuffer(renderBackend.createBuffer(backend::BufferUsage::Stream, kInitialIndexBytes, nullptr)),
		  geometry(
			  renderBackend.createGeometry({.layout = backend::VertexLayout::Uber, .vertices = vertexBuffer, .indices = indexBuffer})
		  ) {
		if (!pipeline.isValid()) {
			std::cerr << "Failed to load uber shaders!" << std::endl;
		}
		// Reserve space for vertices to minimize allocations
		vertices.reserve(10000);
		indices.reserve(15000);
		vertexAtlas.reserve(10000);
	}

	BatchRenderer::~BatchRenderer() {
		gpu.destroyGeometry(geometry);
		gpu.destroyBuffer(indexBuffer);
		gpu.destroyBuffer(vertexBuffer);
		gpu.destroyPipeline(pipeline);
	}

	void BatchRenderer::recordGroup(uint32_t groupStart, float zIndex, Primitives::BlendMode blend) {
		const uint32_t end = static_cast<uint32_t>(indices.size());
		if (end > groupStart) {
			drawGroups.push_back({.indexStart = groupStart, .indexCount = end - groupStart, .zIndex = zIndex, .blend = blend});
			if (zIndex != 0.0F) {
				anyExplicitZ = true;
			}
		}
	}

	void BatchRenderer::pushQuad(
		const Foundation::Vec2 (&corners)[4],
		const Foundation::Vec2 (&texCoords)[4],
		const Foundation::Vec4& color,
		const Foundation::Vec4& data1,
		const Foundation::Vec4& data2,
		TextureHandle			texture
	) {
		const uint32_t baseIndex = static_cast<uint32_t>(vertices.size());
		const bool	   isIdentity = transformClass == TransformClass::kIdentity;
		for (int i = 0; i < 4; ++i) {
			vertices.push_back(
				{TransformPosition(corners[i], currentTransform, isIdentity), texCoords[i], color, data1, data2, currentClipBounds}
			);
		}
		for (const uint32_t corner : {0U, 1U, 2U, 0U, 2U, 3U}) {
			indices.push_back(baseIndex + corner);
		}
		vertexAtlas.insert(vertexAtlas.end(), 4, texture);
	}

	void BatchRenderer::addQuad( // NOLINT(readability-convert-member-functions-to-static)
		const Foundation::Rect&							 bounds,
		const Foundation::Color&						 fillColor,
		const std::optional<Foundation::BorderStyle>&	 border,
		float											 cornerRadius,
		const std::optional<Foundation::LinearGradient>& gradient,
		float											 zIndex,
		Primitives::BlendMode							 blend
	) { // NOLINT(readability-convert-member-functions-to-static)
		const uint32_t zGroupStart = static_cast<uint32_t>(indices.size());
		uint32_t	   baseIndex = static_cast<uint32_t>(vertices.size());

		// Calculate rect center and half-dimensions for SDF
		float halfW = bounds.width * 0.5F;
		float halfH = bounds.height * 0.5F;

		// Per-corner fill colors (vertex order: TL, TR, BR, BL). Without a gradient
		// all four are the flat fill, matching prior behavior exactly. With one, the
		// stops are placed on the corners and the GPU interpolates across the quad.
		Foundation::Vec4 colorTL = fillColor.toVec4();
		Foundation::Vec4 colorTR = colorTL;
		Foundation::Vec4 colorBR = colorTL;
		Foundation::Vec4 colorBL = colorTL;
		if (gradient.has_value()) {
			const Foundation::Vec4 from = gradient->from.toVec4();
			const Foundation::Vec4 to = gradient->to.toVec4();
			if (gradient->horizontal) {
				colorTL = from;
				colorBL = from;
				colorTR = to;
				colorBR = to;
			} else {
				colorTL = from;
				colorTR = from;
				colorBR = to;
				colorBL = to;
			}
		}

		// Pack border data (color RGB + width)
		Foundation::Vec4 borderData(0.0F, 0.0F, 0.0F, 0.0F);
		float			 borderWidth = 0.0F;
		float			 borderPosEnum = 1.0F; // Default to Center
		if (border.has_value()) {
			borderData = Foundation::Vec4(border->color.r, border->color.g, border->color.b, border->width);
			borderWidth = border->width;
			// Use corner radius from border if provided
			if (border->cornerRadius > 0.0F) {
				cornerRadius = border->cornerRadius;
			}
			switch (border->position) {
				case Foundation::BorderPosition::Inside:
					borderPosEnum = 0.0F;
					break;
				case Foundation::BorderPosition::Center:
					borderPosEnum = 1.0F;
					break;
				case Foundation::BorderPosition::Outside:
					borderPosEnum = 2.0F;
					break;
			}
		}

		// Calculate how much the border extends outside the shape bounds
		// Inside (0): border entirely inside, no expansion needed
		// Center (1): border straddles edge, half extends outside
		// Outside (2): border entirely outside, full width extends outside
		float borderOuterExtent = 0.0F;
		if (borderPosEnum == 1.0F) {
			borderOuterExtent = borderWidth * 0.5F; // Center: half outside
		} else if (borderPosEnum == 2.0F) {
			borderOuterExtent = borderWidth; // Outside: full width outside
		}

		// Expand the quad to cover the border that extends outside the shape
		float expandedHalfW = halfW + borderOuterExtent;
		float expandedHalfH = halfH + borderOuterExtent;

		// Calculate expanded screen-space bounds
		float centerX = bounds.x + halfW;
		float centerY = bounds.y + halfH;

		// Pack shape parameters (halfWidth, halfHeight, cornerRadius, borderPosition)
		// Note: shapeParams still uses the ORIGINAL halfW/halfH for SDF calculation
		Foundation::Vec4 shapeParams(halfW, halfH, cornerRadius, renderMode(borderPosEnum, blend));

		// Add 4 vertices with expanded screen positions but rect-local coordinates
		// that extend beyond the original shape bounds.
		// Positions are transformed by currentTransform to support scrolling/offset.

		// Top-left corner
		vertices.push_back(
			{TransformPosition(
				 Foundation::Vec2(centerX - expandedHalfW, centerY - expandedHalfH),
				 currentTransform,
				 transformClass == TransformClass::kIdentity
			 ),
			 Foundation::Vec2(-expandedHalfW, -expandedHalfH), // Rect-local: top-left (expanded)
			 colorTL,
			 borderData,
			 shapeParams,
			 currentClipBounds}
		);

		// Top-right corner
		vertices.push_back(
			{TransformPosition(
				 Foundation::Vec2(centerX + expandedHalfW, centerY - expandedHalfH),
				 currentTransform,
				 transformClass == TransformClass::kIdentity
			 ),
			 Foundation::Vec2(expandedHalfW, -expandedHalfH), // Rect-local: top-right (expanded)
			 colorTR,
			 borderData,
			 shapeParams,
			 currentClipBounds}
		);

		// Bottom-right corner
		vertices.push_back(
			{TransformPosition(
				 Foundation::Vec2(centerX + expandedHalfW, centerY + expandedHalfH),
				 currentTransform,
				 transformClass == TransformClass::kIdentity
			 ),
			 Foundation::Vec2(expandedHalfW, expandedHalfH), // Rect-local: bottom-right (expanded)
			 colorBR,
			 borderData,
			 shapeParams,
			 currentClipBounds}
		);

		// Bottom-left corner
		vertices.push_back(
			{TransformPosition(
				 Foundation::Vec2(centerX - expandedHalfW, centerY + expandedHalfH),
				 currentTransform,
				 transformClass == TransformClass::kIdentity
			 ),
			 Foundation::Vec2(-expandedHalfW, expandedHalfH), // Rect-local: bottom-left (expanded)
			 colorBL,
			 borderData,
			 shapeParams,
			 currentClipBounds}
		);

		// Add 6 indices (2 triangles)
		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 1);
		indices.push_back(baseIndex + 2);

		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 3);

		// Shapes ignore the bound atlas (shader branches on data2.w); tag invalid.
		vertexAtlas.insert(vertexAtlas.end(), 4, TextureHandle{});

		recordGroup(zGroupStart, zIndex, blend);
	}

	void BatchRenderer::addShadowQuad(
		const Foundation::Rect&		 bounds,
		const Foundation::BoxShadow& shadow,
		float						 cornerRadius,
		float						 zIndex,
		Primitives::BlendMode		 blend
	) {
		const uint32_t zGroupStart = static_cast<uint32_t>(indices.size());
		// The shadow's SDF shape is the rect grown by `spread`; the quad is grown a
		// further `blur` so the shader has room to fade the falloff to zero.
		const float halfW = (bounds.width * 0.5F) + shadow.spread;
		const float halfH = (bounds.height * 0.5F) + shadow.spread;
		if (halfW <= 0.0F || halfH <= 0.0F) {
			return;
		}
		const float blur = shadow.blur > 0.0F ? shadow.blur : 0.5F;
		const float cr = cornerRadius + shadow.spread;
		const float cx = bounds.x + (bounds.width * 0.5F) + shadow.offset.x;
		const float cy = bounds.y + (bounds.height * 0.5F) + shadow.offset.y;
		const float qHalfW = halfW + blur;
		const float qHalfH = halfH + blur;

		const uint32_t		   baseIndex = static_cast<uint32_t>(vertices.size());
		const Foundation::Vec4 colorVec = shadow.color.toVec4();
		const Foundation::Vec4 data1(blur, 0.0F, 0.0F, 0.0F);
		const Foundation::Vec4 data2(halfW, halfH, cr, renderMode(kRenderModeShadow, blend));

		// Corners TL, TR, BR, BL. rectLocalPos is the SDF coordinate from the shadow
		// center: the shape edge sits at +-halfSize, the falloff runs out to +-blur.
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(cx - qHalfW, cy - qHalfH), currentTransform, transformClass == TransformClass::kIdentity),
			 Foundation::Vec2(-qHalfW, -qHalfH),
			 colorVec,
			 data1,
			 data2,
			 currentClipBounds}
		);
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(cx + qHalfW, cy - qHalfH), currentTransform, transformClass == TransformClass::kIdentity),
			 Foundation::Vec2(qHalfW, -qHalfH),
			 colorVec,
			 data1,
			 data2,
			 currentClipBounds}
		);
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(cx + qHalfW, cy + qHalfH), currentTransform, transformClass == TransformClass::kIdentity),
			 Foundation::Vec2(qHalfW, qHalfH),
			 colorVec,
			 data1,
			 data2,
			 currentClipBounds}
		);
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(cx - qHalfW, cy + qHalfH), currentTransform, transformClass == TransformClass::kIdentity),
			 Foundation::Vec2(-qHalfW, qHalfH),
			 colorVec,
			 data1,
			 data2,
			 currentClipBounds}
		);

		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 1);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 3);

		vertexAtlas.insert(vertexAtlas.end(), 4, TextureHandle{});

		recordGroup(zGroupStart, zIndex, blend);
	}

	void BatchRenderer::addTriangles( // NOLINT(readability-convert-member-functions-to-static)
		const Foundation::Vec2*	 inputVertices,
		const uint16_t*			 inputIndices,
		size_t					 vertexCount,
		size_t					 indexCount,
		const Foundation::Color& color,
		const Foundation::Color* inputColors,
		float					 zIndex,
		Primitives::BlendMode	 blend
	) { // NOLINT(readability-convert-member-functions-to-static)
		const uint32_t zGroupStart = static_cast<uint32_t>(indices.size());
		uint32_t	   baseIndex = static_cast<uint32_t>(vertices.size());

		Foundation::Vec4 uniformColorVec = color.toVec4();

		// Default data (not used for tessellated shapes, but required for vertex format)
		// Use borderPosition=1 (Center) so shader treats these as shapes, not text
		Foundation::Vec2 zeroVec2(0.0F, 0.0F);
		Foundation::Vec4 zeroVec4(0.0F, 0.0F, 0.0F, 0.0F);
		Foundation::Vec4 shapeParams(0.0F, 0.0F, 0.0F, renderMode(1.0F, blend)); // borderPos=1 marks as shape

		// Add all vertices (positions transformed by currentTransform)
		for (size_t i = 0; i < vertexCount; ++i) {
			// Use per-vertex color if provided, otherwise uniform color
			Foundation::Vec4 colorVec = (inputColors != nullptr) ? inputColors[i].toVec4() : uniformColorVec;

			vertices.push_back({
				TransformPosition(inputVertices[i], currentTransform, transformClass == TransformClass::kIdentity),
				zeroVec2, // texCoord (unused for triangles)
				colorVec,
				zeroVec4,		  // data1 (unused)
				shapeParams,	  // data2 with borderPos >= 0 marks as shape
				currentClipBounds // clip bounds
			});
		}

		// Add all indices (offset by baseIndex)
		for (size_t i = 0; i < indexCount; ++i) {
			indices.push_back(baseIndex + inputIndices[i]);
		}

		// Tessellated shapes ignore the bound atlas; tag every vertex invalid.
		vertexAtlas.insert(vertexAtlas.end(), vertexCount, TextureHandle{});

		recordGroup(zGroupStart, zIndex, blend);
	}

	void BatchRenderer::addStroke(
		std::span<const StrokePiece>	  pieces,
		std::span<const Foundation::Vec2> triangles,
		const Foundation::Color&		  color,
		float							  zIndex,
		Primitives::BlendMode			  blend
	) {
		const uint32_t		   groupStart = static_cast<uint32_t>(indices.size());
		const Foundation::Vec4 colorVec = color.toVec4();
		const Foundation::Vec4 noBorder(0.0F, 0.0F, 0.0F, 0.0F);
		// Border position 1 (center) with a zero border width: a plain filled SDF box.
		const float mode = renderMode(1.0F, blend);
		for (const StrokePiece& piece : pieces) {
			// Box-local x runs along the axis, y along its normal; the quad reaches a
			// pixel past the box on every side.
			const Foundation::Vec2 normal(-piece.axis.y, piece.axis.x);
			const float			   reachX = piece.halfLength + pixelPadding;
			const float			   reachY = piece.halfWidth + pixelPadding;
			const Foundation::Vec2 alongX = piece.axis * reachX;
			const Foundation::Vec2 alongY = normal * reachY;
			const Foundation::Vec2 corners[4] = {
				piece.center - alongX - alongY,
				piece.center + alongX - alongY,
				piece.center + alongX + alongY,
				piece.center - alongX + alongY,
			};
			const Foundation::Vec2 local[4] = {{-reachX, -reachY}, {reachX, -reachY}, {reachX, reachY}, {-reachX, reachY}};
			pushQuad(corners, local, colorVec, noBorder, {piece.halfLength, piece.halfWidth, piece.cornerRadius, mode});
		}
		// Flat triangles as addTriangles draws them: a zero-size box, so the shape is the
		// triangle itself.
		const bool isIdentity = transformClass == TransformClass::kIdentity;
		for (size_t i = 0; i + 2 < triangles.size(); i += 3) {
			const uint32_t baseIndex = static_cast<uint32_t>(vertices.size());
			for (size_t corner = 0; corner < 3; ++corner) {
				vertices.push_back(
					{TransformPosition(triangles[i + corner], currentTransform, isIdentity),
					 Foundation::Vec2(0.0F, 0.0F),
					 colorVec,
					 noBorder,
					 Foundation::Vec4(0.0F, 0.0F, 0.0F, mode),
					 currentClipBounds}
				);
				indices.push_back(baseIndex + static_cast<uint32_t>(corner));
			}
			vertexAtlas.insert(vertexAtlas.end(), 3, TextureHandle{});
		}
		recordGroup(groupStart, zIndex, blend);
	}

	void BatchRenderer::addArc(const Primitives::ArcArgs& args) {
		float start = args.startAngle;
		float sweep = args.sweep;
		if (sweep < 0.0F) {
			start += sweep;
			sweep = -sweep;
		}
		sweep = std::min(sweep, kTwoPi);
		const float innerRadius = std::max(args.innerRadius, 0.0F);
		if (!(sweep > 0.0F) || !(args.outerRadius > innerRadius)) {
			return;
		}

		// The quad's local coordinates are turned so the arc's middle lies along +x,
		// which leaves the shader one symmetric wedge test and no angle to add.
		const float halfSweep = sweep * 0.5F;
		const float middle = start + halfSweep;
		const float cosMiddle = std::cos(middle);
		const float sinMiddle = std::sin(middle);
		const auto	arcLocal = [&](const Foundation::Vec2& offset) {
			 return Foundation::Vec2((cosMiddle * offset.x) + (sinMiddle * offset.y), (cosMiddle * offset.y) - (sinMiddle * offset.x));
		};
		const float			   reach = args.outerRadius + pixelPadding;
		const Foundation::Vec2 offsets[4] = {{-reach, -reach}, {reach, -reach}, {reach, reach}, {-reach, reach}};
		const Foundation::Vec2 corners[4] = {
			args.center + offsets[0], args.center + offsets[1], args.center + offsets[2], args.center + offsets[3]
		};
		const Foundation::Vec2 local[4] = {arcLocal(offsets[0]), arcLocal(offsets[1]), arcLocal(offsets[2]), arcLocal(offsets[3])};

		const uint32_t groupStart = static_cast<uint32_t>(indices.size());
		pushQuad(
			corners,
			local,
			args.color.toVec4(),
			{0.0F, 0.0F, 0.0F, 0.0F},
			{innerRadius, args.outerRadius, halfSweep, renderMode(kRenderModeArc, args.blend)}
		);
		recordGroup(groupStart, static_cast<float>(args.zIndex), args.blend);
	}

	void BatchRenderer::addImageQuad(const Primitives::ImageArgs& args) {
		if (!args.texture.isValid()) {
			return;
		}
		const Foundation::Rect& b = args.bounds;
		const Foundation::Vec2	corners[4] = {{b.x, b.y}, {b.x + b.width, b.y}, {b.x + b.width, b.y + b.height}, {b.x, b.y + b.height}};
		const Foundation::Vec2	uv[4] = {
			 {args.uvMin.x, args.uvMin.y}, {args.uvMax.x, args.uvMin.y}, {args.uvMax.x, args.uvMax.y}, {args.uvMin.x, args.uvMax.y}
		 };

		const uint32_t groupStart = static_cast<uint32_t>(indices.size());
		pushQuad(
			corners,
			uv,
			args.tint.toVec4(),
			{0.0F, 0.0F, 0.0F, 0.0F},
			{0.0F, 0.0F, 0.0F, renderMode(kRenderModeImage, args.blend)},
			args.texture
		);
		recordGroup(groupStart, static_cast<float>(args.zIndex), args.blend);
	}

	void BatchRenderer::addTextQuad(
		const Foundation::Vec2&	 position,
		const Foundation::Vec2&	 size,
		const Foundation::Vec2&	 uvMin,
		const Foundation::Vec2&	 uvMax,
		const Foundation::Color& color,
		const Foundation::Vec2&	 runOrigin,
		TextureHandle			 atlas,
		float					 zIndex,
		Primitives::BlendMode	 blend
	) {
		const uint32_t zGroupStart = static_cast<uint32_t>(indices.size());
		// Resolve the atlas this glyph samples. An invalid handle means "use the
		// default atlas" (Roboto, set via setFontAtlas), so existing callers are unchanged.
		const TextureHandle resolvedAtlas = atlas.isValid() ? atlas : defaultFontAtlas;

		uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

		Foundation::Vec4 colorVec = color.toVec4();

		// Text data packing:
		// data1 = unused (0,0,0,0)
		// data2 = (pixelRange, 0, 0, -1) where -1 signals text rendering mode
		Foundation::Vec4 zeroVec4(0.0F, 0.0F, 0.0F, 0.0F);
		Foundation::Vec4 textParams(fontPixelRange, 0.0F, 0.0F, renderMode(kRenderModeText, blend));

		// Under identity/translate-only transforms, snap the RUN origin (not the
		// glyph) to the device pixel grid: every glyph of a run computes the same
		// delta from the same runOrigin, so inter-glyph spacing stays exactly as
		// authored by the atlas metrics while the whole run lands on whole device
		// pixels (fractional origins otherwise blur the MSDF edge AA). A general
		// transform (rotation/scale) has no meaningful pixel grid; positions pass
		// through the matrix unsnapped.
		const bool		 useMatrix = (transformClass == TransformClass::kGeneral);
		Foundation::Vec2 snappedPosition = position;
		if (!useMatrix) {
			float pixelRatio = 1.0F;
			if (coordinateSystem != nullptr) {
				pixelRatio = coordinateSystem->getPixelRatio();
			}
			if (pixelRatio != textSnapCache.pixelRatio || transformTranslation != textSnapCache.translation ||
				runOrigin != textSnapCache.runOrigin) {
				const Foundation::Vec2 origin = runOrigin + transformTranslation;
				textSnapCache.pixelRatio = pixelRatio;
				textSnapCache.translation = transformTranslation;
				textSnapCache.runOrigin = runOrigin;
				textSnapCache.delta = Foundation::Vec2(
					std::round(origin.x * pixelRatio) / pixelRatio - origin.x, std::round(origin.y * pixelRatio) / pixelRatio - origin.y
				);
			}
			snappedPosition = position + transformTranslation + textSnapCache.delta;
		}

		const auto cornerPosition = [&](float dx, float dy) {
			if (useMatrix) {
				return TransformPosition(Foundation::Vec2(position.x + dx, position.y + dy), currentTransform, false);
			}
			return Foundation::Vec2(snappedPosition.x + dx, snappedPosition.y + dy);
		};

		// Add 4 vertices for glyph quad
		// Note: UV Y coordinates are flipped for OpenGL coordinate system

		// Top-left
		vertices.push_back(
			{cornerPosition(0.0F, 0.0F),
			 Foundation::Vec2(uvMin.x, uvMax.y), // UV flipped
			 colorVec,
			 zeroVec4,
			 textParams,
			 currentClipBounds}
		);

		// Top-right
		vertices.push_back(
			{cornerPosition(size.x, 0.0F),
			 Foundation::Vec2(uvMax.x, uvMax.y), // UV flipped
			 colorVec,
			 zeroVec4,
			 textParams,
			 currentClipBounds}
		);

		// Bottom-right
		vertices.push_back(
			{cornerPosition(size.x, size.y),
			 Foundation::Vec2(uvMax.x, uvMin.y), // UV flipped
			 colorVec,
			 zeroVec4,
			 textParams,
			 currentClipBounds}
		);

		// Bottom-left
		vertices.push_back(
			{cornerPosition(0.0F, size.y),
			 Foundation::Vec2(uvMin.x, uvMin.y), // UV flipped
			 colorVec,
			 zeroVec4,
			 textParams,
			 currentClipBounds}
		);

		// Add 6 indices (2 triangles)
		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 1);
		indices.push_back(baseIndex + 2);

		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 3);

		// Tag all 4 vertices with the atlas they sample, so flush() can group
		// this glyph into the matching per-atlas draw run.
		vertexAtlas.insert(vertexAtlas.end(), 4, resolvedAtlas);

		recordGroup(zGroupStart, zIndex, blend);
	}

	void BatchRenderer::setFontAtlas(TextureHandle atlas, float pixelRange) {
		defaultFontAtlas = atlas;
		fontPixelRange = pixelRange;
	}

	DrawPlan BatchRenderer::plan() {
		// Resolve draw order. Submission ("organic") order is kept unless some draw
		// carried an explicit (non-zero) z; then the per-draw-call groups are
		// stable-sorted by z and the emit-order index list is rebuilt (groups, not
		// triangles). The zIndex came from the component layer; the renderer only
		// orders by it. The fast path costs nothing beyond the group records.
		std::span<const uint32_t> emit = indices;
		if (anyExplicitZ) {
			std::stable_sort(drawGroups.begin(), drawGroups.end(), [](const DrawGroup& a, const DrawGroup& b) {
				return a.zIndex < b.zIndex;
			});
			sortedIndices.clear();
			sortedIndices.reserve(indices.size());
			for (const DrawGroup& g : drawGroups) {
				sortedIndices.insert(sortedIndices.end(), indices.begin() + g.indexStart, indices.begin() + g.indexStart + g.indexCount);
			}
			emit = sortedIndices;
		}

		// Run splitting.
		//
		// All draws share one VBO/IBO and one draw order. Text and image vertices are
		// tagged in `vertexAtlas` with the texture they sample; everything else is
		// tagged invalid (it ignores the bound texture, the shader branches on
		// data2.w). Walking the groups in emit order, a new run starts where a
		// triangle needs a texture other than the bound one, or where a group's GL
		// blend state differs from the run's. Triangles are never reordered, so
		// z-order is exact, and with one texture and one blend state (all text from
		// the default atlas, any mix of Alpha and Additive) the frame is one run.
		runs.clear();
		DrawRun	   current{.indexStart = 0, .indexCount = 0, .texture = defaultFontAtlas, .blendState = backend::BlendState::Alpha};
		uint32_t   emitted = 0;
		const auto cutAt = [&](uint32_t at) {
			if (at > current.indexStart) {
				current.indexCount = at - current.indexStart;
				runs.push_back(current);
			}
			current.indexStart = at;
		};
		for (const DrawGroup& group : drawGroups) {
			const backend::BlendState state = blendState(group.blend);
			if (state != current.blendState) {
				cutAt(emitted);
				current.blendState = state;
			}
			for (uint32_t tri = 0; tri < group.indexCount; tri += 3) {
				// A triangle's vertices are all untextured or all the same texture; the
				// first valid tag is the texture if any, else invalid.
				TextureHandle required = vertexAtlas[indices[group.indexStart + tri]];
				if (!required.isValid()) {
					required = vertexAtlas[indices[group.indexStart + tri + 1]];
				}
				if (!required.isValid()) {
					required = vertexAtlas[indices[group.indexStart + tri + 2]];
				}
				if (required.isValid() && required != current.texture) {
					cutAt(emitted + tri);
					current.texture = required;
				}
			}
			emitted += group.indexCount;
		}
		cutAt(emitted);

		return {.indices = emit, .runs = runs};
	}

	void BatchRenderer::flush() {
		if (vertices.empty()) {
			return;
		}

		const DrawPlan drawPlan = plan();

		// The whole batch, every flush: the stream buffers start over
		gpu.updateBuffer(vertexBuffer, 0, vertices.size() * sizeof(UberVertex), vertices.data());
		gpu.updateBuffer(indexBuffer, 0, drawPlan.indices.size_bytes(), drawPlan.indices.data());

		// Create projection matrix
		// If CoordinateSystem is set, use it for DPI-aware projection (logical pixels)
		// Otherwise fall back to viewport dimensions (may be incorrect on high-DPI displays)
		Foundation::Mat4 projection;
		if (coordinateSystem != nullptr) {
			projection = coordinateSystem->CreateScreenSpaceProjection();
		} else {
			projection = glm::ortho(0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F, -1.0F, 1.0F);
		}

		uniforms.projection = projection;
		// Transform is identity since transforms are baked into vertex positions at add time
		// This allows different parts of the scene to have different transforms within one batch
		uniforms.transform = Foundation::Mat4(1.0F);

		// Set viewport height and pixel ratio for clipping
		// gl_FragCoord uses physical pixels, so we need framebuffer height and pixel ratio
		float framebufferHeight = static_cast<float>(viewportHeight);
		float pixelRatio = 1.0F;
		if (coordinateSystem != nullptr) {
			// Get pixel ratio for DPI scaling
			pixelRatio = coordinateSystem->getPixelRatio();
			// Get logical height and convert to framebuffer (physical) height
			glm::vec2 windowSize = coordinateSystem->getWindowSize();
			framebufferHeight = windowSize.y * pixelRatio;
		}
		uniforms.viewportHeight = framebufferHeight;
		uniforms.pixelRatio = pixelRatio;

		// Set instanced = false for standard batched rendering path
		uniforms.instanced = 0;
		gpu.setUniforms(uniforms);

		// One draw call per run, as one submission
		drawCalls.clear();
		for (const DrawRun& run : drawPlan.runs) {
			drawCalls.push_back(
				{.pipeline = pipeline,
				 .geometry = geometry,
				 .blend = run.blendState,
				 .texture = run.texture,
				 .indexOffset = run.indexStart,
				 .indexCount = run.indexCount}
			);
		}
		gpu.draw(drawCalls);
		drawCallCount += drawCalls.size();

		// Accumulate stats before clearing
		frameVertexCount += vertices.size();
		frameTriangleCount += indices.size() / 3;

		// Clear buffers for next batch
		vertices.clear();
		indices.clear();
		vertexAtlas.clear();
		drawGroups.clear();
		anyExplicitZ = false;
	}

	void BatchRenderer::beginFrame() {
		drawCallCount = 0;
		frameVertexCount = 0;
		frameTriangleCount = 0;
		vertices.clear();
		indices.clear();
		vertexAtlas.clear();
		drawGroups.clear();
		anyExplicitZ = false;
	}

	void BatchRenderer::endFrame() {
		flush();
	}

	void BatchRenderer::setViewport(int width, int height) {
		viewportWidth = width;
		viewportHeight = height;
	}

	void BatchRenderer::getViewport(int& width, int& height) const {
		width = viewportWidth;
		height = viewportHeight;
	}

	// Sets the coordinate system to use for rendering.
	// Note: BatchRenderer does NOT take ownership of the CoordinateSystem pointer.
	// The caller is responsible for ensuring that the CoordinateSystem outlives the BatchRenderer.
	void BatchRenderer::setCoordinateSystem(CoordinateSystem* coordSystem) {
		coordinateSystem = coordSystem;
	}

	BatchRenderer::RenderStats BatchRenderer::getStats() const { // NOLINT(readability-convert-member-functions-to-static)
		RenderStats stats;
		stats.drawCalls = static_cast<uint32_t>(drawCallCount);
		stats.vertexCount = static_cast<uint32_t>(frameVertexCount);
		stats.triangleCount = static_cast<uint32_t>(frameTriangleCount);
		return stats;
	}

	void BatchRenderer::setClipBounds(const Foundation::Vec4& bounds) {
		currentClipBounds = bounds;
	}

	void BatchRenderer::clearClipBounds() {
		currentClipBounds = Foundation::Vec4(0.0F, 0.0F, 0.0F, 0.0F);
	}

	void BatchRenderer::setTransform(const Foundation::Mat4& transform) {
		currentTransform = transform;
		// Classify once per transform change (expensive to do per-vertex).
		// GLM uses column-major storage: column 3 is the translation, the rest
		// is the linear (rotation/scale/shear) part.
		const bool linearIsIdentity = transform[0][0] == 1.0F && transform[1][1] == 1.0F && transform[2][2] == 1.0F &&
									  transform[3][3] == 1.0F && transform[0][1] == 0.0F && transform[0][2] == 0.0F &&
									  transform[0][3] == 0.0F && transform[1][0] == 0.0F && transform[1][2] == 0.0F &&
									  transform[1][3] == 0.0F && transform[2][0] == 0.0F && transform[2][1] == 0.0F &&
									  transform[2][3] == 0.0F;
		transformTranslation = Foundation::Vec2(transform[3][0], transform[3][1]);
		const float areaScale = std::sqrt(std::abs((transform[0][0] * transform[1][1]) - (transform[0][1] * transform[1][0])));
		pixelPadding = 1.0F / std::max(areaScale, 1e-3F);
		if (!linearIsIdentity || transform[3][2] != 0.0F) {
			transformClass = TransformClass::kGeneral;
		} else if (transform[3][0] == 0.0F && transform[3][1] == 0.0F) {
			transformClass = TransformClass::kIdentity;
		} else {
			transformClass = TransformClass::kTranslateOnly;
		}
	}

	// --- GPU Instancing Methods ---

	// Maximum allowed instances to prevent excessive GPU memory allocation.
	// Groundcover (dense grass) needs hundreds of thousands per instanced mesh, so
	// this is sized for that; the instance buffer is only as large as the requested
	// maxInstances (e.g. 250k blades = 8MB), not this cap.
	constexpr uint32_t kMaxAllowedInstances = 2000000;

	InstancedMeshHandle BatchRenderer::uploadInstancedMesh(const renderer::TessellatedMesh& mesh, uint32_t maxInstances) {
		// Validate maxInstances parameter
		if (maxInstances == 0 || maxInstances > kMaxAllowedInstances) {
			std::cerr << "[BatchRenderer] Invalid maxInstances: " << maxInstances << " (must be 1-" << kMaxAllowedInstances << ")"
					  << std::endl;
			return InstancedMeshHandle{};
		}

		InstancedMeshHandle handle;
		handle.maxInstances = maxInstances;

		// Convert TessellatedMesh to InstancedMeshVertex format
		// This format is simpler: just position + color (no SDF data needed for entities)
		std::vector<InstancedMeshVertex> meshVertices;
		meshVertices.reserve(mesh.vertices.size());

		bool			  hasColors = mesh.hasColors();
		Foundation::Color defaultColor(1.0F, 1.0F, 1.0F, 1.0F);

		for (size_t i = 0; i < mesh.vertices.size(); ++i) {
			InstancedMeshVertex v;
			v.position = mesh.vertices[i];
			v.color = hasColors ? mesh.colors[i] : defaultColor;
			meshVertices.push_back(v);
		}

		handle.vertices =
			gpu.createBuffer(backend::BufferUsage::Static, meshVertices.size() * sizeof(InstancedMeshVertex), meshVertices.data());
		handle.indices = gpu.createBuffer(backend::BufferUsage::Static, mesh.indices.size() * sizeof(uint16_t), mesh.indices.data());
		// Per-instance data, rewritten for each draw
		handle.instances = gpu.createBuffer(backend::BufferUsage::Dynamic, maxInstances * sizeof(InstanceData), nullptr);
		handle.geometry = gpu.createGeometry(
			{.layout = backend::VertexLayout::InstancedMesh,
			 .vertices = handle.vertices,
			 .indices = handle.indices,
			 .instances = handle.instances}
		);
		handle.indexCount = static_cast<uint32_t>(mesh.indices.size());
		handle.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
		return handle;
	}

	void BatchRenderer::releaseInstancedMesh(backend::Backend* owner, InstancedMeshHandle& handle) {
		if (owner != nullptr) {
			owner->destroyGeometry(handle.geometry);
			owner->destroyBuffer(handle.instances);
			owner->destroyBuffer(handle.indices);
			owner->destroyBuffer(handle.vertices);
		}
		handle.geometry = {};
		handle.instances = {};
		handle.indices = {};
		handle.vertices = {};
		handle.indexCount = 0;
		handle.vertexCount = 0;
		handle.maxInstances = 0;
	}

	void BatchRenderer::drawInstanced(
		const InstancedMeshHandle& handle,
		const InstanceData*		   instances,
		uint32_t				   count,
		Foundation::Vec2		   cameraPosition,
		float					   cameraZoom,
		float					   pixelsPerMeter
	) {
		if (!handle.isValid() || count == 0 || instances == nullptr) {
			return;
		}

		// Set up projection matrix (same as flush())
		Foundation::Mat4 projection;
		if (coordinateSystem != nullptr) {
			projection = coordinateSystem->CreateScreenSpaceProjection();
		} else {
			projection = glm::ortho(0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F, -1.0F, 1.0F);
		}
		uniforms.projection = projection;

		// Identity transform (world→screen is done in shader via instancing uniforms)
		uniforms.transform = Foundation::Mat4(1.0F);

		// Set instancing uniforms
		uniforms.instanced = 1; // Enable instanced rendering path
		uniforms.cameraPosition = cameraPosition;
		uniforms.cameraZoom = cameraZoom;
		uniforms.pixelsPerMeter = pixelsPerMeter;
		// Use logical pixels for viewport size to match the projection matrix
		// (CreateScreenSpaceProjection uses logical pixels, not physical pixels)
		float logicalWidth = static_cast<float>(viewportWidth);
		float logicalHeight = static_cast<float>(viewportHeight);
		if (coordinateSystem != nullptr) {
			glm::vec2 windowSize = coordinateSystem->getWindowSize();
			logicalWidth = windowSize.x;
			logicalHeight = windowSize.y;
		}
		uniforms.viewportSize = Foundation::Vec2(logicalWidth, logicalHeight);
		gpu.setUniforms(uniforms);

		// Handle overflow by batching multiple draw calls if needed
		// This ensures all instances render even if count > maxInstances
		uint32_t remaining = count;
		uint32_t offset = 0;

		while (remaining > 0) {
			uint32_t batchSize = std::min(remaining, handle.maxInstances);

			// Upload this batch of instance data, then draw it
			gpu.updateBuffer(handle.instances, 0, batchSize * sizeof(InstanceData), instances + offset);
			const backend::DrawCall call{
				.pipeline = pipeline,
				.geometry = handle.geometry,
				.blend = backend::BlendState::Alpha,
				.indexCount = handle.indexCount,
				.instanceCount = batchSize
			};
			gpu.draw({&call, 1});

			drawCallCount++;
			// Each instance renders all mesh vertices; triangle count = indices / 3
			frameVertexCount += static_cast<size_t>(handle.vertexCount) * batchSize;
			frameTriangleCount += static_cast<size_t>(handle.indexCount / 3) * batchSize;

			remaining -= batchSize;
			offset += batchSize;
		}
	}

	void BatchRenderer::setGroundcover(const Primitives::GroundcoverParams& params) {
		// Read by the next instanced draw's uniforms
		uniforms.groundcoverMode = params.enabled ? 1 : 0;
		uniforms.groundcoverOpenness = params.openness;
		uniforms.groundcoverReach = params.reach;
		uniforms.cursorWorld = params.cursorWorld;
		uniforms.cursorRadius = params.cursorRadius;
		uniforms.cursorStrength = params.cursorStrength;
	}

	void BatchRenderer::setBakedAlpha(float alpha) {
		uniforms.bakedAlpha = alpha;
		if (bakedPassOpen) {
			gpu.setUniforms(uniforms); // the caller's next baked draw reads it
		}
	}

	void BatchRenderer::beginBakedPass(const Primitives::WorldCamera& camera) {
		// Projection and viewport size from the setViewport() dimensions, which the
		// caller sets from the same logical size it baked and culled with.
		const float width = static_cast<float>(viewportWidth);
		const float height = static_cast<float>(viewportHeight);
		uniforms.projection = glm::ortho(0.0F, width, height, 0.0F, -1.0F, 1.0F);
		uniforms.transform = Foundation::Mat4(1.0F);

		// Baked world-space mode (u_instanced = 2)
		uniforms.instanced = 2;
		uniforms.cameraPosition = camera.position;
		uniforms.cameraZoom = camera.zoom;
		uniforms.pixelsPerMeter = camera.pixelsPerMeter;
		uniforms.viewportSize = Foundation::Vec2(width, height);
		uniforms.bakedAlpha = 1.0F;
		gpu.setUniforms(uniforms);

		// The same blend state as every batch draw; the caller's VAO draws follow
		gpu.beginForeignDraws(pipeline, backend::BlendState::Alpha);
		bakedPassOpen = true;
	}

	void BatchRenderer::endBakedPass() {
		// Alpha back to 1 so the instanced path (same shader branch) is unaffected
		uniforms.bakedAlpha = 1.0F;
		gpu.endForeignDraws();
		bakedPassOpen = false;
	}

} // namespace Renderer
