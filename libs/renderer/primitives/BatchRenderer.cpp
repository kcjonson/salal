// Batch Renderer implementation.
// Accumulates 2D geometry (shapes + text) as one instance a quad and renders it in
// optimized batches.

#include "primitives/BatchRenderer.h"
#include "CoordinateSystem/CoordinateSystem.h"
#include "primitives/BoundaryLoops.h"
#include "utils/Log.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <glm/gtc/packing.hpp>
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

		// How far a feather ring's outer edge may run out at a sharp corner, in pixels.
		constexpr float kFeatherMiterLimit = 4.0F;

		// The stream buffer's first size; the backend grows it as a flush needs.
		constexpr size_t kInitialInstanceBytes = 2048 * sizeof(BatchInstance);

		// A straight-alpha colour premultiplied, as rgba8.
		uint32_t premultiplied(const Foundation::Vec4& color) {
			const float alpha = std::clamp(color.a, 0.0F, 1.0F);
			return glm::packUnorm4x8(Foundation::Vec4(glm::clamp(Foundation::Vec3(color), 0.0F, 1.0F) * alpha, alpha));
		}

		// Two values as halves, the first in the low 16 bits.
		uint32_t halves(float low, float high) {
			return glm::packHalf2x16(Foundation::Vec2(low, high));
		}
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

	BatchRenderer::BatchRenderer(backend::Backend& renderBackend, const backend::ShaderSource& baseShader)
		: gpu(renderBackend),
		  pipeline(renderBackend.createPipeline(baseShader)),
		  instanceBuffer(renderBackend.createBuffer(backend::BufferUsage::Stream, kInitialInstanceBytes, nullptr)),
		  geometry(renderBackend.createGeometry({.layout = backend::VertexLayout::Batched, .vertices = instanceBuffer})),
		  gpuTimer(renderBackend) {
		if (!pipeline.isValid()) {
			std::cerr << "Failed to load the base shader!" << std::endl;
		}
		instances.reserve(4096);
		instanceTextures.reserve(4096);
	}

	BatchRenderer::~BatchRenderer() {
		freePending();
		gpu.destroyGeometry(geometry);
		gpu.destroyBuffer(instanceBuffer);
		gpu.destroyPipeline(pipeline);
	}

	void BatchRenderer::recordGroup(uint32_t groupStart, int32_t key, Primitives::BlendMode blend) {
		const uint32_t end = static_cast<uint32_t>(instances.size());
		if (end <= groupStart) {
			return;
		}
		const uint64_t order = (static_cast<uint64_t>(sortDomain) << 32U) | (static_cast<uint32_t>(key) ^ 0x80000000U);
		// Straight after a group with the same order and blend, an add joins it: the
		// stable sort would keep the two together anyway, and a text run, a glyph an add,
		// stays one group.
		if (!drawGroups.empty()) {
			DrawGroup& last = drawGroups.back();
			if (last.instanceStart + last.instanceCount == groupStart && last.order == order && last.blend == blend) {
				last.instanceCount = end - last.instanceStart;
				return;
			}
		}
		drawGroups.push_back({.instanceStart = groupStart, .instanceCount = end - groupStart, .order = order, .blend = blend});
		if (key != 0) {
			anyKey = true;
		}
	}

	BatchInstance&
	BatchRenderer::pushQuad(QuadMode mode, const Foundation::Vec2 (&corners)[4], Primitives::BlendMode blend, TextureHandle texture) {
		const bool			   isIdentity = transformClass == TransformClass::kIdentity;
		const Foundation::Vec2 onScreen[4] = {
			TransformPosition(corners[0], currentTransform, isIdentity),
			TransformPosition(corners[1], currentTransform, isIdentity),
			TransformPosition(corners[2], currentTransform, isIdentity),
			TransformPosition(corners[3], currentTransform, isIdentity),
		};
		return pushScreenQuad(mode, onScreen, blend, texture);
	}

	BatchInstance&
	BatchRenderer::pushScreenQuad(QuadMode mode, const Foundation::Vec2 (&corners)[4], Primitives::BlendMode blend, TextureHandle texture) {
		BatchInstance& quad = instances.emplace_back();
		std::copy(std::begin(corners), std::end(corners), std::begin(quad.corners));
		quad.clip = currentClipBounds;
		quad.mode = static_cast<uint8_t>(mode);
		quad.flags = blend == Primitives::BlendMode::Additive ? kQuadAdditive : 0U;
		instanceTextures.push_back(texture);
		return quad;
	}

	void BatchRenderer::addQuad( // NOLINT(readability-convert-member-functions-to-static)
		const Foundation::Rect&							 bounds,
		const Foundation::Color&						 fillColor,
		const std::optional<Foundation::BorderStyle>&	 border,
		float											 cornerRadius,
		const std::optional<Foundation::LinearGradient>& gradient,
		int32_t											 key,
		Primitives::BlendMode							 blend
	) { // NOLINT(readability-convert-member-functions-to-static)
		if (clipEmpty) {
			return;
		}
		const uint32_t groupStart = static_cast<uint32_t>(instances.size());
		const float	   halfW = bounds.width * 0.5F;
		const float	   halfH = bounds.height * 0.5F;

		// Each corner's fill (TL, TR, BR, BL): the flat fill, or the gradient's stops on
		// its corners, interpolated by the GPU, premultiplied first so a gradient to
		// transparent doesn't darken on its way.
		Foundation::Vec4 colors[4] = {fillColor.toVec4(), fillColor.toVec4(), fillColor.toVec4(), fillColor.toVec4()};
		if (gradient.has_value()) {
			const Foundation::Vec4 from = gradient->from.toVec4();
			const Foundation::Vec4 to = gradient->to.toVec4();
			if (gradient->horizontal) {
				colors[0] = from;
				colors[3] = from;
				colors[1] = to;
				colors[2] = to;
			} else {
				colors[0] = from;
				colors[1] = from;
				colors[2] = to;
				colors[3] = to;
			}
		}

		// The border: its colour, width and position, centred when there is none
		uint32_t borderColor = 0;
		float	 borderWidth = 0.0F;
		uint8_t	 borderPosition = 1;
		if (border.has_value()) {
			borderColor = premultiplied(border->color.toVec4());
			borderWidth = border->width;
			if (border->cornerRadius > 0.0F) {
				cornerRadius = border->cornerRadius;
			}
			switch (border->position) {
				case Foundation::BorderPosition::Inside:
					borderPosition = 0;
					break;
				case Foundation::BorderPosition::Center:
					borderPosition = 1;
					break;
				case Foundation::BorderPosition::Outside:
					borderPosition = 2;
					break;
			}
		}

		// The quad grows to cover the part of the border outside the shape: half of a
		// centred one, all of an outside one.
		float borderOuterExtent = 0.0F;
		if (borderPosition == 1) {
			borderOuterExtent = borderWidth * 0.5F;
		} else if (borderPosition == 2) {
			borderOuterExtent = borderWidth;
		}
		const float			   extentW = halfW + borderOuterExtent;
		const float			   extentH = halfH + borderOuterExtent;
		const float			   centerX = bounds.x + halfW;
		const float			   centerY = bounds.y + halfH;
		const Foundation::Vec2 corners[4] = {
			{centerX - extentW, centerY - extentH},
			{centerX + extentW, centerY - extentH},
			{centerX + extentW, centerY + extentH},
			{centerX - extentW, centerY + extentH},
		};

		BatchInstance& quad = pushQuad(QuadMode::Rect, corners, blend);
		quad.geometry = {halfW, halfH, extentW, extentH};
		quad.radii[0] = halves(cornerRadius, cornerRadius);
		quad.radii[1] = quad.radii[0];
		for (int i = 0; i < 4; ++i) {
			quad.fills[i] = premultiplied(colors[i]);
		}
		quad.border = borderColor;
		quad.shape[0] = halves(borderWidth, 0.0F);
		quad.flags |= static_cast<uint8_t>(borderPosition << kQuadBorderShift);

		recordGroup(groupStart, key, blend);
	}

	void BatchRenderer::addShadowQuad(
		const Foundation::Rect&		 bounds,
		const Foundation::BoxShadow& shadow,
		float						 cornerRadius,
		int32_t						 key,
		Primitives::BlendMode		 blend
	) {
		const uint32_t groupStart = static_cast<uint32_t>(instances.size());
		// The shadow's SDF shape is the rect grown by `spread`; the quad is grown a
		// further `blur` so the shader has room to fade the falloff to zero.
		const float halfW = (bounds.width * 0.5F) + shadow.spread;
		const float halfH = (bounds.height * 0.5F) + shadow.spread;
		if (clipEmpty || halfW <= 0.0F || halfH <= 0.0F) {
			return;
		}
		const float			   blur = shadow.blur > 0.0F ? shadow.blur : 0.5F;
		const float			   radius = cornerRadius + shadow.spread;
		const float			   cx = bounds.x + (bounds.width * 0.5F) + shadow.offset.x;
		const float			   cy = bounds.y + (bounds.height * 0.5F) + shadow.offset.y;
		const float			   extentW = halfW + blur;
		const float			   extentH = halfH + blur;
		const Foundation::Vec2 corners[4] = {
			{cx - extentW, cy - extentH}, {cx + extentW, cy - extentH}, {cx + extentW, cy + extentH}, {cx - extentW, cy + extentH}
		};

		BatchInstance& quad = pushQuad(QuadMode::Shadow, corners, blend);
		quad.geometry = {halfW, halfH, extentW, extentH};
		quad.radii[0] = halves(radius, radius);
		quad.radii[1] = quad.radii[0];
		const uint32_t color = premultiplied(shadow.color.toVec4());
		std::fill(std::begin(quad.fills), std::end(quad.fills), color);
		quad.shape[1] = halves(blur, 0.0F);

		recordGroup(groupStart, key, blend);
	}

	void BatchRenderer::addTriangles(const TriangleMeshArgs& mesh) {
		if (clipEmpty) {
			return;
		}
		const uint32_t groupStart = static_cast<uint32_t>(instances.size());
		const bool	   isIdentity = transformClass == TransformClass::kIdentity;
		const uint32_t uniformColor = premultiplied(mesh.color.toVec4());
		meshPositions.resize(mesh.vertices.size());
		meshColors.resize(mesh.vertices.size());
		for (size_t i = 0; i < mesh.vertices.size(); ++i) {
			meshPositions[i] = TransformPosition(mesh.vertices[i], currentTransform, isIdentity);
			meshColors[i] = mesh.colors != nullptr ? premultiplied(mesh.colors[i].toVec4()) : uniformColor;
		}

		// A flat quad a triangle, its last corner repeated
		for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
			const uint16_t		   a = mesh.indices[t];
			const uint16_t		   b = mesh.indices[t + 1];
			const uint16_t		   c = mesh.indices[t + 2];
			const Foundation::Vec2 corners[4] = {meshPositions[a], meshPositions[b], meshPositions[c], meshPositions[c]};
			BatchInstance&		   quad = pushScreenQuad(QuadMode::Flat, corners, mesh.blend);
			quad.fills[0] = meshColors[a];
			quad.fills[1] = meshColors[b];
			quad.fills[2] = meshColors[c];
			quad.fills[3] = meshColors[c];
		}

		if (mesh.feather != nullptr && !mesh.feather->empty()) {
			addFeather(*mesh.feather, mesh.blend);
		}

		recordGroup(groupStart, mesh.key, mesh.blend);
	}

	void BatchRenderer::addFeather(const BoundaryLoops& loops, Primitives::BlendMode blend) {
		const float pixel = 1.0F / (coordinateSystem != nullptr ? coordinateSystem->getPixelRatio() : 1.0F);
		// The loops keep the mesh on their left; a mirroring transform turns it to the right.
		const float determinant = (currentTransform[0][0] * currentTransform[1][1]) - (currentTransform[0][1] * currentTransform[1][0]);
		const float side = determinant < 0.0F ? -1.0F : 1.0F;
		const auto	outward = [side](const Foundation::Vec2& from, const Foundation::Vec2& to) {
			 const Foundation::Vec2 direction = to - from;
			 const float			length = glm::length(direction);
			 return length > 0.0F ? Foundation::Vec2(direction.y, -direction.x) * (side / length) : Foundation::Vec2(0.0F, 0.0F);
		};

		for (size_t loop = 0; loop < loops.count(); ++loop) {
			const uint32_t start = loops.starts[loop];
			const uint32_t size = loops.starts[loop + 1] - start;
			// One outer point per boundary vertex, the fill's own vertex its inner one.
			featherOuter.resize(size);
			for (uint32_t v = 0; v < size; ++v) {
				const Foundation::Vec2 previous = meshPositions[loops.vertices[start + ((v + size - 1) % size)]];
				const Foundation::Vec2 here = meshPositions[loops.vertices[start + v]];
				const Foundation::Vec2 next = meshPositions[loops.vertices[start + ((v + 1) % size)]];
				Foundation::Vec2	   normalIn = outward(previous, here);
				Foundation::Vec2	   normalOut = outward(here, next);
				if (normalIn == Foundation::Vec2(0.0F, 0.0F)) {
					normalIn = normalOut;
				} else if (normalOut == Foundation::Vec2(0.0F, 0.0F)) {
					normalOut = normalIn;
				}
				// The miter is 1 / cos(half the turn), 2 / |sum|, pixels long; a spike back on
				// itself has no miter and goes a pixel along its last edge's normal.
				const Foundation::Vec2 sum = normalIn + normalOut;
				const float			   sumLength = glm::length(sum);
				featherOuter[v] = here + (sumLength > 1e-6F ? sum * (pixel * std::min(kFeatherMiterLimit, 2.0F / sumLength) / sumLength)
															: normalOut * pixel);
			}
			// A quad an edge, its inner side the fill's colour fading to transparent outside
			for (uint32_t v = 0; v < size; ++v) {
				const uint32_t		   w = (v + 1) % size;
				const uint32_t		   innerA = loops.vertices[start + v];
				const uint32_t		   innerB = loops.vertices[start + w];
				const Foundation::Vec2 corners[4] = {meshPositions[innerA], meshPositions[innerB], featherOuter[w], featherOuter[v]};
				BatchInstance&		   quad = pushScreenQuad(QuadMode::Flat, corners, blend);
				quad.fills[0] = meshColors[innerA];
				quad.fills[1] = meshColors[innerB];
			}
		}
	}

	void BatchRenderer::addStroke(
		std::span<const StrokePiece> pieces,
		const Foundation::Color&	 color,
		int32_t						 key,
		Primitives::BlendMode		 blend
	) {
		if (clipEmpty || !invertible) {
			return;
		}
		const uint32_t groupStart = static_cast<uint32_t>(instances.size());
		const uint32_t fill = premultiplied(color.toVec4());
		const bool	   isIdentity = transformClass == TransformClass::kIdentity;
		for (const StrokePiece& piece : pieces) {
			Foundation::Vec2 onScreen[StrokePiece::kMaxCorners];
			for (uint8_t k = 0; k < piece.cornerCount; ++k) {
				onScreen[k] = TransformPosition(piece.corners[k], currentTransform, isIdentity);
			}
			// The piece's frame from a screen offset: box-local x runs along the axis, y
			// along its normal, both through the inverse of the transform's linear part.
			const bool			   flat = !(piece.halfLength > 0.0F) && !(piece.halfWidth > 0.0F);
			const Foundation::Vec2 normal(-piece.axis.y, piece.axis.x);
			const Foundation::Vec2 alongAxis = glm::transpose(inverseLinear) * piece.axis;
			const Foundation::Vec2 alongNormal = glm::transpose(inverseLinear) * normal;
			const Foundation::Vec2 center = TransformPosition(piece.center, currentTransform, isIdentity);
			// A fan over the corners, two triangles a quad, the last corner repeated when one is over.
			for (uint8_t k = 1; k + 1 < piece.cornerCount; k += 2) {
				const uint8_t		   last = std::min<uint8_t>(k + 2, piece.cornerCount - 1);
				const Foundation::Vec2 corners[4] = {onScreen[0], onScreen[k], onScreen[k + 1], onScreen[last]};
				BatchInstance&		   quad = pushScreenQuad(flat ? QuadMode::Flat : QuadMode::Stroke, corners, blend);
				std::fill(std::begin(quad.fills), std::end(quad.fills), fill);
				if (!flat) {
					quad.geometry = {alongAxis.x, alongAxis.y, alongNormal.x, alongNormal.y};
					quad.radii[0] = std::bit_cast<uint32_t>(center.x);
					quad.radii[1] = std::bit_cast<uint32_t>(center.y);
					quad.shape[0] = std::bit_cast<uint32_t>(piece.halfLength);
					quad.shape[1] = std::bit_cast<uint32_t>(piece.halfWidth);
					quad.border = std::bit_cast<uint32_t>(piece.cornerRadius);
				}
			}
		}
		recordGroup(groupStart, key, blend);
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
		if (clipEmpty || !(sweep > 0.0F) || !(args.outerRadius > innerRadius)) {
			return;
		}

		// The quad reaches a pixel past the outer radius, so the edge ramp fits; base.vert
		// turns its local coordinates so the arc's middle lies along +x, which leaves the
		// shader one symmetric wedge test and no angle to add.
		const float			   halfSweep = sweep * 0.5F;
		const float			   reach = args.outerRadius + pixelPadding;
		const Foundation::Vec2 corners[4] = {
			args.center + Foundation::Vec2(-reach, -reach),
			args.center + Foundation::Vec2(reach, -reach),
			args.center + Foundation::Vec2(reach, reach),
			args.center + Foundation::Vec2(-reach, reach),
		};

		const uint32_t groupStart = static_cast<uint32_t>(instances.size());
		BatchInstance& quad = pushQuad(QuadMode::Arc, corners, args.blend);
		quad.geometry = {innerRadius, args.outerRadius, halfSweep, start + halfSweep};
		std::fill(std::begin(quad.fills), std::end(quad.fills), premultiplied(args.color.toVec4()));
		quad.shape[0] = halves(0.0F, pixelPadding);
		recordGroup(groupStart, args.key, args.blend);
	}

	void BatchRenderer::addImageQuad(const Primitives::ImageArgs& args) {
		if (clipEmpty || !args.texture.isValid() || !sampleable(args.texture)) {
			return;
		}
		const Foundation::Rect& b = args.bounds;
		const Foundation::Vec2	corners[4] = {{b.x, b.y}, {b.x + b.width, b.y}, {b.x + b.width, b.y + b.height}, {b.x, b.y + b.height}};

		const uint32_t groupStart = static_cast<uint32_t>(instances.size());
		BatchInstance& quad = pushQuad(QuadMode::Image, corners, args.blend, args.texture);
		quad.geometry = {args.uvMin.x, args.uvMin.y, args.uvMax.x, args.uvMax.y};
		std::fill(std::begin(quad.fills), std::end(quad.fills), premultiplied(args.tint.toVec4()));
		recordGroup(groupStart, args.key, args.blend);
	}

	void BatchRenderer::addTextQuad(
		const Foundation::Vec2&	 position,
		const Foundation::Vec2&	 size,
		const Foundation::Vec2&	 uvMin,
		const Foundation::Vec2&	 uvMax,
		const Foundation::Color& color,
		const Foundation::Vec2&	 runOrigin,
		TextureHandle			 atlas,
		float					 pixelRange,
		int32_t					 key,
		Primitives::BlendMode	 blend
	) {
		if (clipEmpty) {
			return;
		}
		const uint32_t groupStart = static_cast<uint32_t>(instances.size());
		// Resolve the atlas this glyph samples, and its range. An invalid handle
		// means the default atlas (the game's default family's, set via setFontAtlas)
		// at the default's range.
		const TextureHandle resolvedAtlas = atlas.isValid() ? atlas : defaultFontAtlas;
		const float			resolvedPixelRange = atlas.isValid() ? pixelRange : fontPixelRange;
		if (resolvedAtlas.isValid() && !sampleable(resolvedAtlas)) {
			return;
		}

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
		const Foundation::Vec2 corners[4] = {
			cornerPosition(0.0F, 0.0F), cornerPosition(size.x, 0.0F), cornerPosition(size.x, size.y), cornerPosition(0.0F, size.y)
		};

		// The glyph's uv rect, flipped in v: the atlas is uploaded bottom row first.
		// Tagged with its atlas, so flush() can group it into the matching run.
		BatchInstance& quad = pushScreenQuad(QuadMode::Text, corners, blend, resolvedAtlas);
		quad.geometry = {uvMin.x, uvMax.y, uvMax.x, uvMin.y};
		std::fill(std::begin(quad.fills), std::end(quad.fills), premultiplied(color.toVec4()));
		quad.shape[1] = halves(resolvedPixelRange, 0.0F);

		recordGroup(groupStart, key, blend);
	}

	void BatchRenderer::setFontAtlas(TextureHandle atlas, float pixelRange) {
		defaultFontAtlas = atlas;
		fontPixelRange = pixelRange;
	}

	void BatchRenderer::releaseTexture(TextureHandle texture) {
		if (!texture.isValid()) {
			return;
		}
		if (texture == liveTexture) {
			liveTexture = {};
		}
		if (instances.empty()) {
			gpu.destroyTexture(texture);
		} else {
			pendingFrees.push_back(texture);
		}
	}

	bool BatchRenderer::sampleable(TextureHandle texture) {
		if (texture == liveTexture) {
			return true;
		}
		if (!gpu.hasTexture(texture) || std::ranges::find(pendingFrees, texture) != pendingFrees.end()) {
			if (!warnedStale) {
				warnedStale = true;
				LOG_WARNING(
					Renderer,
					"A draw sampled texture %u after it was freed; it draws nothing (warned once)",
					static_cast<unsigned>(texture.value)
				);
			}
			return false;
		}
		liveTexture = texture;
		return true;
	}

	void BatchRenderer::freePending() {
		for (const TextureHandle texture : pendingFrees) {
			gpu.destroyTexture(texture);
		}
		pendingFrees.clear();
	}

	DrawPlan BatchRenderer::plan() {
		// Resolve draw order. Submission ("organic") order is kept unless some draw
		// carried a non-zero key; then the per-draw-call groups are stable-sorted by
		// sort domain, then key, and their quads copied out in that order. Domains rise
		// in submission order, so the fast path already has them in order, and costs
		// nothing beyond the group records.
		std::span<BatchInstance> emit = instances;
		if (anyKey) {
			std::stable_sort(drawGroups.begin(), drawGroups.end(), [](const DrawGroup& a, const DrawGroup& b) {
				return a.order < b.order;
			});
			sortedInstances.clear();
			sortedInstances.reserve(instances.size());
			for (const DrawGroup& g : drawGroups) {
				sortedInstances.insert(
					sortedInstances.end(), instances.begin() + g.instanceStart, instances.begin() + g.instanceStart + g.instanceCount
				);
			}
			emit = sortedInstances;
		}

		// Run splitting and texture slots.
		//
		// Text and image quads are tagged in `instanceTextures` with the texture they
		// sample; everything else is tagged invalid (it ignores the textures bound).
		// Walking the groups in emit order, each textured quad takes its texture's slot
		// in the run, adding it when it's new; a new run starts where a quad needs a
		// texture the run has no slot left for, or where a group's GPU blend state
		// differs from the run's. Quads are never reordered, so key order is exact, and
		// with up to kTextureSlots textures and one blend state (any mix of Alpha and
		// Additive) the frame is one run.
		runs.clear();
		DrawRun	   current{.instanceStart = 0, .instanceCount = 0, .blendState = backend::BlendState::Alpha};
		uint8_t	   slotsUsed = 0;
		uint32_t   emitted = 0;
		const auto cutAt = [&](uint32_t at) {
			if (at > current.instanceStart) {
				current.instanceCount = at - current.instanceStart;
				runs.push_back(current);
			}
			current.instanceStart = at;
			current.textures = {};
			slotsUsed = 0;
		};
		for (const DrawGroup& group : drawGroups) {
			const backend::BlendState state = blendState(group.blend);
			if (state != current.blendState) {
				cutAt(emitted);
				current.blendState = state;
			}
			for (uint32_t i = 0; i < group.instanceCount; ++i) {
				const TextureHandle required = instanceTextures[group.instanceStart + i];
				if (!required.isValid()) {
					continue;
				}
				const auto bound = std::find(current.textures.begin(), current.textures.begin() + slotsUsed, required);
				uint8_t	   slot = static_cast<uint8_t>(bound - current.textures.begin());
				if (slot == slotsUsed) {
					if (slotsUsed == backend::kTextureSlots) {
						cutAt(emitted + i);
						slot = 0;
					}
					current.textures[slot] = required;
					slotsUsed = static_cast<uint8_t>(slot + 1);
				}
				emit[emitted + i].slot = slot;
			}
			emitted += group.instanceCount;
		}
		cutAt(emitted);

		return {.instances = emit, .runs = runs};
	}

	void BatchRenderer::flush() {
		if (instances.empty()) {
			return;
		}
		const auto flushStart = std::chrono::steady_clock::now();

		const DrawPlan drawPlan = plan();

		// The whole batch, every flush: the stream buffer starts over
		gpu.updateBuffer(instanceBuffer, 0, drawPlan.instances.size_bytes(), drawPlan.instances.data());
		frameUploadBytes += drawPlan.instances.size_bytes();

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
		// Transform is identity since transforms are baked into the corners at add time
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
				 .textures = run.textures,
				 .firstInstance = run.instanceStart,
				 .instanceCount = run.instanceCount}
			);
		}
		// A GPU span holds draws, never the uploads before them, here and in drawInstanced().
		beginGpuSpan();
		gpu.draw(drawCalls);
		endGpuSpan();
		drawCallCount += drawCalls.size();
		freePending();

		// Accumulate stats before clearing: an instance, six vertices and two triangles a quad
		frameInstanceCount += instances.size();
		frameVertexCount += instances.size() * 6;
		frameTriangleCount += instances.size() * 2;
		++frameFlushCount;
		frameFlushSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - flushStart).count();

		// Clear buffers for next batch
		instances.clear();
		instanceTextures.clear();
		drawGroups.clear();
		anyKey = false;
		sortDomain = 0;
	}

	void BatchRenderer::beginFrameStats() {
		drawCallCount = 0;
		frameVertexCount = 0;
		frameTriangleCount = 0;
		frameInstanceCount = 0;
		frameUploadBytes = 0;
		frameFlushCount = 0;
		frameFlushSeconds = 0.0;
		if (gpuTiming) {
			gpuTimer.beginFrame();
		}
	}

	void BatchRenderer::beginFrame() {
		freePending(); // nothing left in the batch samples them
		instances.clear();
		instanceTextures.clear();
		drawGroups.clear();
		anyKey = false;
		sortDomain = 0;
		clearClipBounds();
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
		stats.instanceCount = static_cast<uint32_t>(frameInstanceCount);
		stats.uploadBytes = frameUploadBytes;
		stats.flushCount = static_cast<uint32_t>(frameFlushCount);
		stats.flushMs = static_cast<float>(frameFlushSeconds * 1000.0);
		return stats;
	}

	void BatchRenderer::setGpuTiming(bool enabled) {
		if (!enabled) {
			gpuTimer.reset();
		}
		gpuTiming = enabled;
	}

	void BatchRenderer::beginGpuSpan() {
		if (gpuTiming) {
			gpuTimer.beginSpan();
		}
	}

	void BatchRenderer::endGpuSpan() {
		if (gpuTiming) {
			gpuTimer.endSpan();
		}
	}

	void BatchRenderer::setClipBounds(const Foundation::Vec4& bounds) {
		// Written so NaN bounds are empty too.
		clipEmpty = !(bounds.z > bounds.x && bounds.w > bounds.y);
		currentClipBounds = clipEmpty ? Foundation::Vec4(0.0F, 0.0F, 0.0F, 0.0F) : bounds;
	}

	void BatchRenderer::clearClipBounds() {
		clipEmpty = false;
		currentClipBounds = Foundation::Vec4(0.0F, 0.0F, 0.0F, 0.0F);
	}

	float BatchRenderer::getLocalDevicePixel() const {
		return 1.0F / (std::max(largestScale, 1e-3F) * (coordinateSystem != nullptr ? coordinateSystem->getPixelRatio() : 1.0F));
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
		const float squares = (transform[0][0] * transform[0][0]) + (transform[0][1] * transform[0][1]) +
							  (transform[1][0] * transform[1][0]) + (transform[1][1] * transform[1][1]);
		const float determinant = areaScale * areaScale;
		largestScale = std::sqrt(0.5F * (squares + std::sqrt(std::max((squares * squares) - (4.0F * determinant * determinant), 0.0F))));
		if (!linearIsIdentity || transform[3][2] != 0.0F) {
			transformClass = TransformClass::kGeneral;
		} else if (transform[3][0] == 0.0F && transform[3][1] == 0.0F) {
			transformClass = TransformClass::kIdentity;
		} else {
			transformClass = TransformClass::kTranslateOnly;
		}
		const glm::mat2 linear(transform[0][0], transform[0][1], transform[1][0], transform[1][1]);
		const float		linearDeterminant = glm::determinant(linear);
		invertible = linearDeterminant != 0.0F && std::isfinite(linearDeterminant);
		inverseLinear = invertible ? glm::inverse(linear) : glm::mat2(1.0F);
	}

	// --- GPU Instancing Methods ---

	// Maximum allowed instances to prevent excessive GPU memory allocation.
	// A dense field of small instances needs hundreds of thousands per mesh, so this
	// is sized for that; the instance buffer is only as large as the requested
	// maxInstances (e.g. 250k instances = 8MB), not this cap.
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
			beginGpuSpan();
			gpu.draw({&call, 1});
			endGpuSpan();

			drawCallCount++;
			// Each instance renders all mesh vertices; triangle count = indices / 3
			frameVertexCount += static_cast<size_t>(handle.vertexCount) * batchSize;
			frameTriangleCount += static_cast<size_t>(handle.indexCount / 3) * batchSize;
			frameInstanceCount += batchSize;
			frameUploadBytes += batchSize * sizeof(InstanceData);

			remaining -= batchSize;
			offset += batchSize;
		}
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
		beginGpuSpan();
	}

	void BatchRenderer::endBakedPass() {
		// Alpha back to 1 so the instanced path (same shader branch) is unaffected
		uniforms.bakedAlpha = 1.0F;
		endGpuSpan();
		gpu.endForeignDraws();
		bakedPassOpen = false;
	}

} // namespace Renderer
