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

		// How far a feather ring's outer edge may run out at a sharp corner, in ring widths.
		constexpr float kFeatherMiterLimit = 4.0F;

		// The stream buffer's first size; each flush's write resizes it to that flush.
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
		for (const backend::FillHook& hook : baseShader.fillHooks) {
			if (hook.index == 0 || hook.index >= pendingFills.size()) {
				continue;
			}
			fillHooks.resize(std::max<size_t>(fillHooks.size(), hook.index));
			fillHooks[hook.index - 1] = hook.hook != nullptr ? hook.hook : "";
		}
	}

	BatchRenderer::~BatchRenderer() {
		freePending();
		gpu.destroyGeometry(geometry);
		gpu.destroyBuffer(instanceBuffer);
		gpu.destroyPipeline(pipeline);
	}

	void BatchRenderer::recordGroup(uint32_t groupStart, int32_t key, Primitives::BlendMode blend, uint32_t mask) {
		const uint32_t end = static_cast<uint32_t>(instances.size());
		if (end <= groupStart) {
			return;
		}
		// A Subtract's cover passes fill the box round every quad drawn under its mask.
		if (mask != 0) {
			MaskEntry& entry = maskEntries[mask - 1];
			for (uint32_t index = groupStart; index < end; ++index) {
				for (const Foundation::Vec2& corner : instances[index].corners) {
					entry.cover = entry.covered ? Foundation::Vec4(
													  std::min(entry.cover.x, corner.x),
													  std::min(entry.cover.y, corner.y),
													  std::max(entry.cover.z, corner.x),
													  std::max(entry.cover.w, corner.y)
												  )
												: Foundation::Vec4(corner.x, corner.y, corner.x, corner.y);
					entry.covered = true;
				}
			}
		}
		const uint64_t order = (static_cast<uint64_t>(sortDomain) << 32U) | (static_cast<uint32_t>(key) ^ 0x80000000U);
		// Straight after a group with the same order, blend and mask, an add joins it: the
		// stable sort would keep the two together anyway, and a text run, a glyph an add,
		// stays one group.
		if (!drawGroups.empty()) {
			DrawGroup& last = drawGroups.back();
			if (last.instanceStart + last.instanceCount == groupStart && last.order == order && last.blend == blend && last.mask == mask) {
				last.instanceCount = end - last.instanceStart;
				return;
			}
		}
		drawGroups.push_back(
			{.instanceStart = groupStart, .instanceCount = end - groupStart, .order = order, .blend = blend, .mask = mask}
		);
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
		const CornerRadii&								 radii,
		const std::optional<Foundation::LinearGradient>& gradient,
		int32_t											 key,
		Primitives::BlendMode							 blend,
		const Primitives::Fill&							 fill,
		const char*										 id,
		uint32_t										 mask
	) { // NOLINT(readability-convert-member-functions-to-static)
		if (clipEmpty) {
			return;
		}
		const uint32_t groupStart = static_cast<uint32_t>(instances.size());
		const float	   halfW = bounds.width * 0.5F;
		const float	   halfH = bounds.height * 0.5F;

		// A hooked rect has one fill colour, corner 0's, and its params in the other three.
		const uint8_t hookIndex = fill.hook != nullptr ? fillIndex(*fill.hook, id) : 0;
		if (hookIndex != 0 && gradient.has_value() && warnedGradientIds.insert(id != nullptr ? id : "").second) {
			LOG_WARNING(
				Renderer,
				"drawRect %s: a rect with fill hook %s has one fill colour, so its gradient is dropped (warned once per id)",
				id != nullptr ? id : "(no id)",
				fill.hook->hook
			);
		}

		// Each corner's fill (TL, TR, BR, BL): the flat fill, or the gradient's stops on
		// its corners, interpolated by the GPU, premultiplied first so a gradient to
		// transparent doesn't darken on its way.
		Foundation::Vec4 colors[4] = {fillColor.toVec4(), fillColor.toVec4(), fillColor.toVec4(), fillColor.toVec4()};
		if (gradient.has_value() && hookIndex == 0) {
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

		// The border: its colour, width, position, centred when there is none, and bare sides
		uint32_t borderColor = 0;
		float	 borderWidth = 0.0F;
		uint8_t	 borderPosition = 1;
		uint8_t	 bare = 0;
		if (border.has_value()) {
			borderColor = premultiplied(border->color.toVec4());
			borderWidth = border->width;
			const Foundation::BorderSides& sides = border->sides;
			bare =
				static_cast<uint8_t>((sides.top ? 0U : 1U) | (sides.right ? 0U : 2U) | (sides.bottom ? 0U : 4U) | (sides.left ? 0U : 8U));
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
		quad.radii[0] = halves(radii[0], radii[1]);
		quad.radii[1] = halves(radii[2], radii[3]);
		for (int i = 0; i < 4; ++i) {
			quad.fills[i] = premultiplied(colors[i]);
		}
		if (hookIndex != 0) {
			std::copy(fill.params.begin(), fill.params.end(), std::begin(quad.fills) + 1);
			quad.slot = hookIndex;
			pendingFills.set(hookIndex);
		}
		quad.border = borderColor;
		quad.shape[0] = halves(borderWidth, 0.0F);
		// A dash runs round the border's centre line, a whole number of periods, one centred on
		// the top-left corner's middle, where the shader starts measuring.
		if (border.has_value() && border->dash.has_value() && borderWidth > 0.0F) {
			const float		   centre = borderPosition == 0 ? -borderWidth * 0.5F : (borderPosition == 2 ? borderWidth * 0.5F : 0.0F);
			const ResolvedDash dash = resolveDash(*border->dash, borderWidth, rectOutlineLength({halfW, halfH}, radii, centre), true);
			if (dash.gap > 0.0F) {
				quad.shape[0] = halves(borderWidth, dash.dash);
				quad.shape[1] =
					halves(dash.round ? -dash.gap : dash.gap, std::fmod(dash.offset + (dash.dash * 0.5F), dash.dash + dash.gap));
			}
		}
		quad.flags |= static_cast<uint8_t>((borderPosition << kQuadBorderShift) | (bare << kQuadBareShift));

		recordGroup(groupStart, key, blend, mask);
	}

	ResolvedDash resolveDash(const Foundation::DashPattern& pattern, float width, float length, bool closed) {
		float dash = pattern.dash;
		float gap = pattern.gap;
		float phase = pattern.phase;
		if (pattern.cap == Foundation::DashCap::Square) {
			dash += width;
			gap -= width;
			phase += width * 0.5F;
		}
		if (!(dash >= 0.0F) || !(gap > 0.0F) || !std::isfinite(dash + gap + phase)) {
			return {};
		}
		float period = dash + gap;
		if (closed && length > 0.0F && std::isfinite(length)) {
			const float scale = length / (std::max(std::round(length / period), 1.0F) * period);
			dash *= scale;
			gap *= scale;
			phase *= scale;
			period *= scale;
		}
		float offset = std::fmod(phase, period);
		if (offset < 0.0F) {
			offset += period;
		}
		return {.dash = dash, .gap = gap, .offset = offset, .round = pattern.cap == Foundation::DashCap::Round};
	}

	float rectOutlineLength(Foundation::Vec2 halfSize, const CornerRadii& radii, float centre) {
		const Foundation::Vec2 half = halfSize + centre;
		if (!(half.x > 0.0F) || !(half.y > 0.0F)) {
			return 0.0F;
		}
		// Four straight runs and four quarter arcs: each corner's radius takes 2r off the runs and
		// gives back a quarter of its circle, as base.frag's outlineLength walks them.
		float radiusSum = 0.0F;
		for (const float radius : radii) {
			radiusSum += std::clamp(radius + centre, 0.0F, std::min(half.x, half.y));
		}
		return (4.0F * (half.x + half.y)) + (((std::numbers::pi_v<float> * 0.5F) - 2.0F) * radiusSum);
	}

	CornerRadii rectCornerRadii(float radius, const std::optional<Foundation::BorderStyle>& border) {
		if (!border.has_value() || !(border->width > 0.0F)) {
			return {radius, radius, radius, radius};
		}
		const Foundation::BorderSides& sides = border->sides;
		return {
			sides.top && sides.left ? radius : 0.0F,
			sides.top && sides.right ? radius : 0.0F,
			sides.bottom && sides.right ? radius : 0.0F,
			sides.bottom && sides.left ? radius : 0.0F,
		};
	}

	uint8_t BatchRenderer::fillIndex(const backend::FillHook& hook, const char* id) {
		const std::string_view name = hook.hook != nullptr ? hook.hook : "";
		if (hook.index != 0 && hook.index <= fillHooks.size() && fillHooks[hook.index - 1] == name && !name.empty()) {
			return static_cast<uint8_t>(hook.index);
		}
		if (warnedForeignFillIds.insert(id != nullptr ? id : "").second) {
			LOG_ERROR(
				Renderer,
				"drawRect %s: fill hook %.*s is not the base shader's; the rect draws its plain fill (logged once per id)",
				id != nullptr ? id : "(no id)",
				static_cast<int>(name.size()),
				name.data()
			);
		}
		return 0;
	}

	bool BatchRenderer::holdsFill(std::string_view hook) const {
		for (size_t i = 0; i < fillHooks.size(); ++i) {
			if (fillHooks[i] == hook) {
				return pendingFills.test(i + 1);
			}
		}
		return false;
	}

	void BatchRenderer::addShadowQuad(
		const Foundation::Rect&		 bounds,
		const Foundation::BoxShadow& shadow,
		const CornerRadii&			 radii,
		int32_t						 key,
		Primitives::BlendMode		 blend,
		uint32_t					 mask
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
		const float			   cx = bounds.x + (bounds.width * 0.5F) + shadow.offset.x;
		const float			   cy = bounds.y + (bounds.height * 0.5F) + shadow.offset.y;
		const float			   extentW = halfW + blur;
		const float			   extentH = halfH + blur;
		const Foundation::Vec2 corners[4] = {
			{cx - extentW, cy - extentH}, {cx + extentW, cy - extentH}, {cx + extentW, cy + extentH}, {cx - extentW, cy + extentH}
		};

		BatchInstance& quad = pushQuad(QuadMode::Shadow, corners, blend);
		quad.geometry = {halfW, halfH, extentW, extentH};
		quad.radii[0] = halves(radii[0] + shadow.spread, radii[1] + shadow.spread);
		quad.radii[1] = halves(radii[2] + shadow.spread, radii[3] + shadow.spread);
		const uint32_t color = premultiplied(shadow.color.toVec4());
		std::fill(std::begin(quad.fills), std::end(quad.fills), color);
		quad.shape[1] = halves(blur, 0.0F);

		recordGroup(groupStart, key, blend, mask);
	}

	// --- Masks ---

	uint32_t BatchRenderer::internMask(std::span<const MaskLayerGeometry> layers) {
		if (layers.empty()) {
			return 0;
		}
		const auto	 layerStart = static_cast<uint32_t>(maskLayers.size());
		const size_t triangleMark = maskTriangles.size();
		const bool	 isIdentity = transformClass == TransformClass::kIdentity;
		uint64_t	 hash = 14695981039346656037ULL; // FNV-1a
		const auto	 mix = [&hash](uint64_t value) {
			  hash = (hash ^ value) * 1099511628211ULL;
		};
		for (const MaskLayerGeometry& layer : layers) {
			MaskLayerEntry entry{
				.composite = layer.composite,
				.triangleStart = static_cast<uint32_t>(maskTriangles.size() / 3),
				.meshGeometry = layer.meshGeometry,
				.meshInstances = layer.meshInstances,
				.meshIndexCount = layer.meshIndexCount,
				.camera = layer.camera,
			};
			if (!layer.meshGeometry.isValid()) {
				for (size_t index = 0; index + 2 < layer.indices.size(); index += 3) {
					if (layer.indices[index] >= layer.vertices.size() || layer.indices[index + 1] >= layer.vertices.size() ||
						layer.indices[index + 2] >= layer.vertices.size()) {
						continue;
					}
					for (size_t corner = 0; corner < 3; ++corner) {
						maskTriangles.push_back(
							TransformPosition(layer.vertices[layer.indices[index + corner]], currentTransform, isIdentity)
						);
					}
				}
			}
			entry.triangleCount = static_cast<uint32_t>(maskTriangles.size() / 3) - entry.triangleStart;
			mix(static_cast<uint64_t>(entry.composite));
			mix(entry.triangleCount);
			mix(entry.meshGeometry.value);
			maskLayers.push_back(entry);
		}
		for (size_t index = triangleMark; index < maskTriangles.size(); ++index) {
			mix(std::bit_cast<uint32_t>(maskTriangles[index].x));
			mix(std::bit_cast<uint32_t>(maskTriangles[index].y));
		}

		// An equal mask already in the table is shared, so equal masks build once.
		const auto layerCount = static_cast<uint32_t>(layers.size());
		for (size_t index = 0; index < maskEntries.size(); ++index) {
			const MaskEntry& existing = maskEntries[index];
			if (existing.hash != hash || existing.layerCount != layerCount) {
				continue;
			}
			bool same = true;
			for (uint32_t offset = 0; offset < layerCount && same; ++offset) {
				same = sameLayer(maskLayers[existing.layerStart + offset], maskLayers[layerStart + offset]);
			}
			if (same) {
				maskLayers.resize(layerStart);
				maskTriangles.resize(triangleMark);
				return static_cast<uint32_t>(index + 1);
			}
		}
		maskEntries.push_back({.layerStart = layerStart, .layerCount = layerCount, .hash = hash});
		return static_cast<uint32_t>(maskEntries.size());
	}

	bool BatchRenderer::sameLayer(const MaskLayerEntry& first, const MaskLayerEntry& second) const {
		if (first.composite != second.composite || first.triangleCount != second.triangleCount ||
			first.meshGeometry != second.meshGeometry || first.meshInstances != second.meshInstances ||
			first.meshIndexCount != second.meshIndexCount || !(first.camera == second.camera)) {
			return false;
		}
		const auto firstTriangles = maskTriangles.begin() + (static_cast<ptrdiff_t>(first.triangleStart) * 3);
		const auto secondTriangles = maskTriangles.begin() + (static_cast<ptrdiff_t>(second.triangleStart) * 3);
		return std::equal(firstTriangles, firstTriangles + (static_cast<ptrdiff_t>(first.triangleCount) * 3), secondTriangles);
	}

	void BatchRenderer::clearMasks() {
		maskEntries.clear();
		maskLayers.clear();
		maskTriangles.clear();
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
			meshPositions[i] = TransformPosition(mesh.vertices[i] + mesh.offset, currentTransform, isIdentity);
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
			// A local width reaches its area scale's worth of logical pixels on screen.
			const float width = mesh.featherWidth > 0.0F ? mesh.featherWidth / pixelPadding
														 : 1.0F / (coordinateSystem != nullptr ? coordinateSystem->getPixelRatio() : 1.0F);
			addFeather(*mesh.feather, width, mesh.blend);
		}

		recordGroup(groupStart, mesh.key, mesh.blend);
	}

	void BatchRenderer::addFeather(const BoundaryLoops& loops, float width, Primitives::BlendMode blend) {
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
				// The miter is 1 / cos(half the turn), 2 / |sum|, widths long; a spike back on
				// itself has no miter and goes a width along its last edge's normal.
				const Foundation::Vec2 sum = normalIn + normalOut;
				const float			   sumLength = glm::length(sum);
				featherOuter[v] = here + (sumLength > 1e-6F ? sum * (width * std::min(kFeatherMiterLimit, 2.0F / sumLength) / sumLength)
															: normalOut * width);
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
		Primitives::BlendMode		 blend,
		const ResolvedDash&			 dash
	) {
		if (clipEmpty || !invertible) {
			return;
		}
		const uint32_t groupStart = static_cast<uint32_t>(instances.size());
		const uint32_t fill = premultiplied(color.toVec4());
		const float	   period = dash.dash + dash.gap;
		const uint32_t dashWord = dash.gap > 0.0F ? halves(dash.dash, dash.round ? -dash.gap : dash.gap) : 0U;
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
				if (flat) {
					std::fill(std::begin(quad.fills), std::end(quad.fills), fill);
				} else {
					quad.fills[0] = fill;
					if (dashWord != 0U) {
						quad.fills[1] = dashWord;
						quad.fills[2] = std::bit_cast<uint32_t>(std::fmod(piece.along + dash.offset, period));
					}
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
		Primitives::BlendMode	 blend,
		const TextEdge&			 edge
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

		// Under a transform that keeps the axes (identity, a translation, an upright
		// scale with one), snap the RUN origin (not the glyph) to the device pixel
		// grid: every glyph of a run computes the same delta from the same runOrigin,
		// so inter-glyph spacing stays exactly as authored by the atlas metrics,
		// scaled, while the whole run lands on whole device pixels (fractional
		// origins otherwise blur the MSDF edge AA). The delta is taken on screen,
		// after the scale. A general transform (rotation, shear, a flip) has no
		// meaningful pixel grid; positions pass through the matrix unsnapped.
		const bool		 useMatrix = (transformClass == TransformClass::kGeneral);
		Foundation::Vec2 snappedPosition = position;
		if (!useMatrix) {
			float pixelRatio = 1.0F;
			if (coordinateSystem != nullptr) {
				pixelRatio = coordinateSystem->getPixelRatio();
			}
			if (pixelRatio != textSnapCache.pixelRatio || transformScale != textSnapCache.scale ||
				transformTranslation != textSnapCache.translation || runOrigin != textSnapCache.runOrigin) {
				const Foundation::Vec2 origin = (runOrigin * transformScale) + transformTranslation;
				textSnapCache.pixelRatio = pixelRatio;
				textSnapCache.scale = transformScale;
				textSnapCache.translation = transformTranslation;
				textSnapCache.runOrigin = runOrigin;
				textSnapCache.delta = Foundation::Vec2(
					std::round(origin.x * pixelRatio) / pixelRatio - origin.x, std::round(origin.y * pixelRatio) / pixelRatio - origin.y
				);
			}
			snappedPosition = (position * transformScale) + transformTranslation + textSnapCache.delta;
		}

		const auto cornerPosition = [&](float dx, float dy) {
			if (useMatrix) {
				return TransformPosition(Foundation::Vec2(position.x + dx, position.y + dy), currentTransform, false);
			}
			return Foundation::Vec2(snappedPosition.x + (dx * transformScale.x), snappedPosition.y + (dy * transformScale.y));
		};
		const Foundation::Vec2 corners[4] = {
			cornerPosition(0.0F, 0.0F), cornerPosition(size.x, 0.0F), cornerPosition(size.x, size.y), cornerPosition(0.0F, size.y)
		};

		// The glyph's uv rect, flipped in v: the atlas is uploaded bottom row first.
		// Tagged with its atlas, so flush() can group it into the matching run.
		BatchInstance& quad = pushScreenQuad(QuadMode::Text, corners, blend, resolvedAtlas);
		quad.geometry = {uvMin.x, uvMax.y, uvMax.x, uvMin.y};
		std::fill(std::begin(quad.fills), std::end(quad.fills), premultiplied(color.toVec4()));
		quad.shape[0] = halves(edge.spread, edge.blur);
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
		for (const MeshObjects& mesh : pendingMeshFrees) {
			gpu.destroyGeometry(mesh.geometry);
			gpu.destroyBuffer(mesh.instances);
			gpu.destroyBuffer(mesh.indices);
			gpu.destroyBuffer(mesh.vertices);
		}
		pendingMeshFrees.clear();
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
		// texture the run has no slot left for, or where a group's GPU blend state or mask
		// differs from the run's. Quads are never reordered, so key order is exact, and
		// with up to kTextureSlots textures, one blend state (any mix of Alpha and
		// Additive) and no masks the frame is one run.
		runs.clear();
		DrawRun	   current{.instanceStart = 0, .instanceCount = 0, .blendState = backend::BlendState::Alpha, .mask = 0};
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
			if (state != current.blendState || group.mask != current.mask) {
				cutAt(emitted);
				current.blendState = state;
				current.mask = group.mask;
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

		// The whole batch, every flush: the stream buffer starts over. With masks, each
		// layer's triangles and each mask's cover quad follow the plan's quads, as flat
		// quads with no clip.
		std::span<const BatchInstance> upload = drawPlan.instances;
		uint32_t					   coverStart = 0;
		if (!maskEntries.empty()) {
			maskedUpload.assign(drawPlan.instances.begin(), drawPlan.instances.end());
			const auto flatQuad = [this](const Foundation::Vec2(&corners)[4]) {
				BatchInstance& quad = maskedUpload.emplace_back();
				std::copy(std::begin(corners), std::end(corners), std::begin(quad.corners));
				quad.mode = static_cast<uint8_t>(QuadMode::Flat);
			};
			layerFirstInstance.resize(maskLayers.size());
			for (size_t layerIndex = 0; layerIndex < maskLayers.size(); ++layerIndex) {
				const MaskLayerEntry& layer = maskLayers[layerIndex];
				layerFirstInstance[layerIndex] = static_cast<uint32_t>(maskedUpload.size());
				for (uint32_t triangle = 0; triangle < layer.triangleCount; ++triangle) {
					const Foundation::Vec2* points = &maskTriangles[(static_cast<size_t>(layer.triangleStart) + triangle) * 3];
					const Foundation::Vec2	corners[4] = {points[0], points[1], points[2], points[2]};
					flatQuad(corners);
				}
			}
			coverStart = static_cast<uint32_t>(maskedUpload.size());
			for (const MaskEntry& entry : maskEntries) {
				const Foundation::Vec4& box = entry.cover;
				const Foundation::Vec2	corners[4] = {{box.x, box.y}, {box.z, box.y}, {box.z, box.w}, {box.x, box.w}};
				flatQuad(corners);
			}
			upload = maskedUpload;
		}
		gpu.updateBuffer(instanceBuffer, 0, upload.size_bytes(), upload.data());
		frameUploadBytes += upload.size_bytes();

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
		batchedUniforms = uniforms;
		gpu.setUniforms(uniforms);

		// One draw call per run, as one submission unless a mask is built between runs. A
		// masked run lands where the built mask's accumulator bit is set; consecutive runs
		// under the same mask share its build, since nothing between them writes the stencil.
		const backend::StencilState maskTest{
			.test = true,
			.function = backend::StencilFunction::Equal,
			.reference = kMaskAccumulatorBit,
			.readMask = kMaskAccumulatorBit,
			.writeMask = 0,
		};
		drawCalls.clear();
		// A GPU span holds draws, never the uploads before them, here and in drawInstanced().
		beginGpuSpan();
		uint32_t built = 0;
		for (const DrawRun& run : drawPlan.runs) {
			if (run.mask != 0 && run.mask != built) {
				// Decided here, against the target bound now: a target is begun without a flush.
				if (!gpu.hasStencil()) {
					if (!warnedNoStencil) {
						warnedNoStencil = true;
						LOG_ERROR(Renderer, "A masked draw met a target with no stencil buffer; it draws nothing (logged once)");
					}
					continue;
				}
				buildMask(run.mask, coverStart + run.mask - 1);
				built = run.mask;
			}
			drawCalls.push_back(
				{.pipeline = pipeline,
				 .geometry = geometry,
				 .blend = run.blendState,
				 .stencil = run.mask != 0 ? maskTest : backend::StencilState{},
				 .textures = run.textures,
				 .firstInstance = run.instanceStart,
				 .instanceCount = run.instanceCount}
			);
		}
		submitPending();
		endGpuSpan();
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
		pendingFills.reset();
		anyKey = false;
		sortDomain = 0;
		clearMasks();
	}

	void BatchRenderer::submitPending() {
		if (drawCalls.empty()) {
			return;
		}
		gpu.draw(drawCalls);
		drawCallCount += drawCalls.size();
		drawCalls.clear();
	}

	// The mask in two stencil bits: the accumulator holds the layers composited so far, bottom
	// up, and the scratch bit a Subtract layer's coverage. Add sets the accumulator where the
	// layer lands (source over, for binary coverage the union). Subtract draws the layer into the
	// scratch bit, then two cover passes over the masked quads' box make the accumulator the
	// layer outside what it held: zero it where the scratch bit is clear, then invert both bits
	// where it is set, which also clears the scratch bit. The bottom layer's composite is
	// ignored. Every layer draws aliased, with no clip and no colour.
	void BatchRenderer::buildMask(uint32_t index, uint32_t coverInstance) {
		using backend::StencilFunction;
		using backend::StencilOperation;
		const backend::StencilState addLayer{
			.test = true,
			.reference = kMaskAccumulatorBit,
			.writeMask = kMaskAccumulatorBit,
			.pass = StencilOperation::Replace,
		};
		const backend::StencilState scratchLayer{
			.test = true,
			.reference = kMaskScratchBit,
			.writeMask = kMaskScratchBit,
			.pass = StencilOperation::Replace,
		};
		const backend::StencilState clearOutsideLayer{
			.test = true,
			.function = StencilFunction::Equal,
			.reference = 0,
			.readMask = kMaskScratchBit,
			.writeMask = kMaskAccumulatorBit,
			.pass = StencilOperation::Zero,
		};
		const backend::StencilState invertInsideLayer{
			.test = true,
			.function = StencilFunction::Equal,
			.reference = kMaskScratchBit,
			.readMask = kMaskScratchBit,
			.writeMask = kMaskAccumulatorBit | kMaskScratchBit,
			.pass = StencilOperation::Invert,
		};
		const auto stencilOnly = [this](const backend::StencilState& stencil, uint32_t firstInstance, uint32_t instanceCount) {
			if (instanceCount > 0) {
				drawCalls.push_back(
					{.pipeline = pipeline,
					 .geometry = geometry,
					 .stencil = stencil,
					 .writeColour = false,
					 .firstInstance = firstInstance,
					 .instanceCount = instanceCount}
				);
			}
		};

		submitPending();
		gpu.clearStencil(kMaskAccumulatorBit | kMaskScratchBit);
		const MaskEntry& entry = maskEntries[index - 1];
		for (uint32_t offset = entry.layerCount; offset-- > 0;) {
			const uint32_t				 layerIndex = entry.layerStart + offset;
			const MaskLayerEntry&		 layer = maskLayers[layerIndex];
			const bool					 subtract = offset + 1 < entry.layerCount && layer.composite == Primitives::MaskComposite::Subtract;
			const backend::StencilState& coverage = subtract ? scratchLayer : addLayer;
			if (layer.meshGeometry.isValid()) {
				// One copy at the world origin under the layer's camera, drawn under the
				// instanced uniforms, then the batch's again.
				submitPending();
				gpu.setUniforms(worldUniforms(layer.camera.position, layer.camera.zoom, layer.camera.pixelsPerMeter));
				const InstanceData origin;
				gpu.updateBuffer(layer.meshInstances, 0, sizeof(InstanceData), &origin);
				const backend::DrawCall meshCall{
					.pipeline = pipeline,
					.geometry = layer.meshGeometry,
					.stencil = coverage,
					.writeColour = false,
					.indexCount = layer.meshIndexCount,
					.instanceCount = 1,
				};
				gpu.draw({&meshCall, 1});
				++drawCallCount;
				gpu.setUniforms(batchedUniforms);
			} else {
				stencilOnly(coverage, layerFirstInstance[layerIndex], layer.triangleCount);
			}
			if (subtract) {
				stencilOnly(clearOutsideLayer, coverInstance, 1);
				stencilOnly(invertInsideLayer, coverInstance, 1);
			}
		}
		submitPending();
	}

	backend::DrawUniforms BatchRenderer::worldUniforms(Foundation::Vec2 cameraPosition, float cameraZoom, float pixelsPerMeter) const {
		backend::DrawUniforms world = uniforms;
		world.projection = coordinateSystem != nullptr
							   ? coordinateSystem->CreateScreenSpaceProjection()
							   : glm::ortho(0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F, -1.0F, 1.0F);
		// World to screen is done in the shader from the camera
		world.transform = Foundation::Mat4(1.0F);
		world.instanced = 1;
		world.cameraPosition = cameraPosition;
		world.cameraZoom = cameraZoom;
		world.pixelsPerMeter = pixelsPerMeter;
		// Logical pixels, as the projection is (CreateScreenSpaceProjection uses logical pixels)
		world.viewportSize = Foundation::Vec2(static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
		if (coordinateSystem != nullptr) {
			world.viewportSize = coordinateSystem->getWindowSize();
		}
		return world;
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
		pendingFills.reset();
		anyKey = false;
		sortDomain = 0;
		clearMasks();
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
		const bool linearIsDiagonal = transform[2][2] == 1.0F && transform[3][3] == 1.0F && transform[0][1] == 0.0F &&
									  transform[0][2] == 0.0F && transform[0][3] == 0.0F && transform[1][0] == 0.0F &&
									  transform[1][2] == 0.0F && transform[1][3] == 0.0F && transform[2][0] == 0.0F &&
									  transform[2][1] == 0.0F && transform[2][3] == 0.0F;
		const bool linearIsIdentity = linearIsDiagonal && transform[0][0] == 1.0F && transform[1][1] == 1.0F;
		const bool upright = linearIsDiagonal && transform[0][0] > 0.0F && transform[1][1] > 0.0F && transform[3][2] == 0.0F;
		transformTranslation = Foundation::Vec2(transform[3][0], transform[3][1]);
		transformScale = upright ? Foundation::Vec2(transform[0][0], transform[1][1]) : Foundation::Vec2(1.0F, 1.0F);
		const float areaScale = std::sqrt(std::abs((transform[0][0] * transform[1][1]) - (transform[0][1] * transform[1][0])));
		pixelPadding = 1.0F / std::max(areaScale, 1e-3F);
		const float squares = (transform[0][0] * transform[0][0]) + (transform[0][1] * transform[0][1]) +
							  (transform[1][0] * transform[1][0]) + (transform[1][1] * transform[1][1]);
		const float determinant = areaScale * areaScale;
		largestScale = std::sqrt(0.5F * (squares + std::sqrt(std::max((squares * squares) - (4.0F * determinant * determinant), 0.0F))));
		if (upright && !linearIsIdentity) {
			transformClass = TransformClass::kScaleTranslate;
		} else if (!linearIsIdentity || transform[3][2] != 0.0F) {
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

	void BatchRenderer::releaseInstancedMesh(BatchRenderer* batch, InstancedMeshHandle& handle) {
		if (batch != nullptr && (handle.geometry.isValid() || handle.vertices.isValid())) {
			batch->pendingMeshFrees.push_back(
				{.geometry = handle.geometry, .vertices = handle.vertices, .indices = handle.indices, .instances = handle.instances}
			);
			// A pending draw's mask may name the mesh; with none it goes now.
			if (batch->instances.empty()) {
				batch->freePending();
			}
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

		uniforms = worldUniforms(cameraPosition, cameraZoom, pixelsPerMeter);
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
