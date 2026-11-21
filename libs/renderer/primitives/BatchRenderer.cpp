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
#include <cstring>
#include <glm/gtc/packing.hpp>
#include <iostream>
#include <limits>
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
		// The box round every quad drawn under its mask: what a Subtract's cover passes fill, and
		// what a layer must hold for the mask to be built inverted.
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

	BatchInstance& BatchRenderer::pushQuad(
		QuadMode							 mode,
		std::span<const Foundation::Vec2, 4> corners,
		Primitives::BlendMode				 blend,
		TextureHandle						 texture
	) {
		const bool			   isIdentity = transformClass == TransformClass::kIdentity;
		const Foundation::Vec2 onScreen[4] = {
			TransformPosition(corners[0], currentTransform, isIdentity),
			TransformPosition(corners[1], currentTransform, isIdentity),
			TransformPosition(corners[2], currentTransform, isIdentity),
			TransformPosition(corners[3], currentTransform, isIdentity),
		};
		return pushScreenQuad(mode, onScreen, blend, texture);
	}

	BatchInstance& BatchRenderer::pushScreenQuad(
		QuadMode							 mode,
		std::span<const Foundation::Vec2, 4> corners,
		Primitives::BlendMode				 blend,
		TextureHandle						 texture
	) {
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
		BatchInstance& quad = pushQuad(QuadMode::Rect, rectCorners(bounds, borderOuterExtent), blend);
		quad.geometry = {halfW, halfH, halfW + borderOuterExtent, halfH + borderOuterExtent};
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

	std::array<Foundation::Vec2, 4> rectCorners(const Foundation::Rect& bounds, float outset) {
		// From the edges, not the centre: at outset 0 these are the bounds' own x and x + width,
		// where the centre plus and minus half the width can land an ulp off.
		const float left = bounds.x - outset;
		const float top = bounds.y - outset;
		const float right = (bounds.x + bounds.width) + outset;
		const float bottom = (bounds.y + bounds.height) + outset;
		return {{{left, top}, {right, top}, {right, bottom}, {left, bottom}}};
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
		const Foundation::Rect moved{bounds.x + shadow.offset.x, bounds.y + shadow.offset.y, bounds.width, bounds.height};

		BatchInstance& quad = pushQuad(QuadMode::Shadow, rectCorners(moved, shadow.spread + blur), blend);
		quad.geometry = {halfW, halfH, halfW + blur, halfH + blur};
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
		const size_t elementMark = maskElements.size();
		const size_t elementPointMark = elementPoints.size();
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
				.content = layer.content,
				.elementStart = static_cast<uint32_t>(maskElements.size()),
			};
			if (layer.content) {
				for (const MaskElementGeometry& element : layer.elements) {
					internElement(element, isIdentity, hash);
				}
				entry.elementCount = static_cast<uint32_t>(maskElements.size()) - entry.elementStart;
			} else if (!layer.meshGeometry.isValid()) {
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
			entry.screenBox = screenBoxOf(entry.triangleStart, entry.triangleCount);
			mix(static_cast<uint64_t>(entry.composite));
			mix(entry.triangleCount);
			mix(entry.meshGeometry.value);
			mix(entry.elementCount);
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
				maskElements.resize(elementMark);
				elementPoints.resize(elementPointMark);
				elementValues.resize(elementPointMark);
				return static_cast<uint32_t>(index + 1);
			}
		}
		maskEntries.push_back({.layerStart = layerStart, .layerCount = layerCount, .hash = hash});
		return static_cast<uint32_t>(maskEntries.size());
	}

	void BatchRenderer::internElement(const MaskElementGeometry& element, bool isIdentity, uint64_t& hash) {
		const auto mix = [&hash](uint64_t value) {
			hash = (hash ^ value) * 1099511628211ULL;
		};
		MaskElementEntry entry{
			.triangleStart = static_cast<uint32_t>(elementPoints.size() / 3),
			.meshGeometry = element.meshGeometry,
			.meshInstances = element.meshInstances,
			.meshIndexCount = element.meshIndexCount,
			.camera = element.camera,
			.mask = element.mask,
			.blend = element.blend,
			.group = element.group,
			.groupBlend = element.groupBlend,
		};
		// A mesh's box is the view's: nothing on the CPU bounds it.
		Foundation::Vec4 box = viewBox();
		if (!element.meshGeometry.isValid()) {
			const size_t pointCount = std::min(element.triangles.size() / 3 * 3, element.values.size() / 3 * 3);
			box = Foundation::Vec4(
				std::numeric_limits<float>::max(),
				std::numeric_limits<float>::max(),
				std::numeric_limits<float>::lowest(),
				std::numeric_limits<float>::lowest()
			);
			for (size_t index = 0; index < pointCount; ++index) {
				const Foundation::Vec2 point = TransformPosition(element.triangles[index], currentTransform, isIdentity);
				const float			   value = std::clamp(element.values[index], 0.0F, 1.0F);
				elementPoints.push_back(point);
				elementValues.push_back(value);
				box = Foundation::Vec4(
					std::min(box.x, point.x), std::min(box.y, point.y), std::max(box.z, point.x), std::max(box.w, point.y)
				);
				mix(std::bit_cast<uint32_t>(point.x));
				mix(std::bit_cast<uint32_t>(point.y));
				mix(std::bit_cast<uint32_t>(value));
			}
			entry.triangleCount = static_cast<uint32_t>(pointCount / 3);
		}
		// An element's own mask is read only where the element lands, so its cover takes in the element's box.
		if (entry.mask != 0 && (entry.triangleCount > 0 || entry.meshGeometry.isValid())) {
			MaskEntry& masked = maskEntries[entry.mask - 1];
			masked.cover = masked.covered ? Foundation::Vec4(
												std::min(masked.cover.x, box.x),
												std::min(masked.cover.y, box.y),
												std::max(masked.cover.z, box.z),
												std::max(masked.cover.w, box.w)
											)
										  : box;
			masked.covered = true;
		}
		mix(entry.triangleCount);
		mix(entry.meshGeometry.value);
		mix(entry.mask);
		mix((static_cast<uint64_t>(entry.group) << 16U) | (static_cast<uint64_t>(entry.blend) << 8U) |
			static_cast<uint64_t>(entry.groupBlend));
		maskElements.push_back(entry);
	}

	bool BatchRenderer::sameLayer(const MaskLayerEntry& first, const MaskLayerEntry& second) const {
		if (first.composite != second.composite || first.triangleCount != second.triangleCount ||
			first.meshGeometry != second.meshGeometry || first.meshInstances != second.meshInstances ||
			first.meshIndexCount != second.meshIndexCount || !(first.camera == second.camera) || first.content != second.content ||
			first.elementCount != second.elementCount) {
			return false;
		}
		for (uint32_t offset = 0; offset < first.elementCount; ++offset) {
			if (!sameElement(maskElements[first.elementStart + offset], maskElements[second.elementStart + offset])) {
				return false;
			}
		}
		const auto firstTriangles = maskTriangles.begin() + (static_cast<ptrdiff_t>(first.triangleStart) * 3);
		const auto secondTriangles = maskTriangles.begin() + (static_cast<ptrdiff_t>(second.triangleStart) * 3);
		return std::equal(firstTriangles, firstTriangles + (static_cast<ptrdiff_t>(first.triangleCount) * 3), secondTriangles);
	}

	bool BatchRenderer::sameElement(const MaskElementEntry& first, const MaskElementEntry& second) const {
		if (first.triangleCount != second.triangleCount || first.meshGeometry != second.meshGeometry ||
			first.meshInstances != second.meshInstances || first.meshIndexCount != second.meshIndexCount ||
			!(first.camera == second.camera) || first.mask != second.mask || first.blend != second.blend || first.group != second.group ||
			first.groupBlend != second.groupBlend) {
			return false;
		}
		const auto firstStart = static_cast<ptrdiff_t>(first.triangleStart) * 3;
		const auto secondStart = static_cast<ptrdiff_t>(second.triangleStart) * 3;
		const auto count = static_cast<ptrdiff_t>(first.triangleCount) * 3;
		return std::equal(
				   elementPoints.begin() + firstStart, elementPoints.begin() + firstStart + count, elementPoints.begin() + secondStart
			   ) &&
			   std::equal(
				   elementValues.begin() + firstStart, elementValues.begin() + firstStart + count, elementValues.begin() + secondStart
			   );
	}

	Foundation::Vec4 BatchRenderer::viewBox() const {
		const Foundation::Vec2 size = coordinateSystem != nullptr
										  ? coordinateSystem->getWindowSize()
										  : Foundation::Vec2(static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
		return {0.0F, 0.0F, size.x, size.y};
	}

	std::optional<Foundation::Vec4> BatchRenderer::screenBoxOf(uint32_t triangleStart, uint32_t triangleCount) const {
		if (triangleCount != 2) {
			return std::nullopt;
		}
		// Two triangles (a, b, c) and (a, c, d) round the corners a, b, c, d of an axis-aligned
		// box, in either winding: exactly the box, split along its diagonal ac. Compared exactly,
		// so a transform that turns the box by a hair leaves it a general layer.
		const Foundation::Vec2* points = &maskTriangles[static_cast<size_t>(triangleStart) * 3];
		const Foundation::Vec2& first = points[0];
		const Foundation::Vec2& second = points[1];
		const Foundation::Vec2& third = points[2];
		const Foundation::Vec2& fourth = points[5];
		if (points[3] != first || points[4] != third) {
			return std::nullopt;
		}
		const bool rowFirst = first.y == second.y && second.x == third.x && third.y == fourth.y && fourth.x == first.x;
		const bool columnFirst = first.x == second.x && second.y == third.y && third.x == fourth.x && fourth.y == first.y;
		if (!rowFirst && !columnFirst) {
			return std::nullopt;
		}
		return Foundation::Vec4(
			std::min(first.x, third.x), std::min(first.y, third.y), std::max(first.x, third.x), std::max(first.y, third.y)
		);
	}

	bool BatchRenderer::coversEntry(const MaskLayerEntry& layer, const MaskEntry& entry) {
		if (!layer.screenBox.has_value() || !entry.covered) {
			return false;
		}
		const Foundation::Vec4& box = *layer.screenBox;
		return box.x <= entry.cover.x && box.y <= entry.cover.y && entry.cover.z <= box.z && entry.cover.w <= box.w;
	}

	void BatchRenderer::clearMasks() {
		maskEntries.clear();
		maskLayers.clear();
		maskTriangles.clear();
		maskElements.clear();
		elementPoints.clear();
		elementValues.clear();
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
		const uint32_t groupStart = static_cast<uint32_t>(instances.size());
		BatchInstance& quad = pushQuad(QuadMode::Image, rectCorners(args.bounds), args.blend, args.texture);
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

		// The whole batch, every flush: the stream buffer starts over. With masks, each layer's
		// triangles, each content element's triangles with its values as their colour, and each
		// mask's two cover quads, alpha 0 then alpha 1, follow the plan's quads, as flat quads with
		// no clip.
		std::span<const BatchInstance> upload = drawPlan.instances;
		if (!maskEntries.empty()) {
			maskedUpload.assign(drawPlan.instances.begin(), drawPlan.instances.end());
			const auto flatQuad = [this](const Foundation::Vec2(&corners)[4], const uint32_t (&fills)[4]) {
				BatchInstance& quad = maskedUpload.emplace_back();
				std::copy(std::begin(corners), std::end(corners), std::begin(quad.corners));
				std::copy(std::begin(fills), std::end(fills), std::begin(quad.fills));
				quad.mode = static_cast<uint8_t>(QuadMode::Flat);
			};
			constexpr uint32_t kNoFill[4] = {0, 0, 0, 0};
			layerFirstInstance.resize(maskLayers.size());
			for (size_t layerIndex = 0; layerIndex < maskLayers.size(); ++layerIndex) {
				const MaskLayerEntry& layer = maskLayers[layerIndex];
				layerFirstInstance[layerIndex] = static_cast<uint32_t>(maskedUpload.size());
				for (uint32_t triangle = 0; triangle < layer.triangleCount; ++triangle) {
					const Foundation::Vec2* points = &maskTriangles[(static_cast<size_t>(layer.triangleStart) + triangle) * 3];
					const Foundation::Vec2	corners[4] = {points[0], points[1], points[2], points[2]};
					flatQuad(corners, kNoFill);
				}
			}
			elementFirstInstance.resize(maskElements.size());
			for (size_t elementIndex = 0; elementIndex < maskElements.size(); ++elementIndex) {
				const MaskElementEntry& element = maskElements[elementIndex];
				elementFirstInstance[elementIndex] = static_cast<uint32_t>(maskedUpload.size());
				for (uint32_t triangle = 0; triangle < element.triangleCount; ++triangle) {
					const size_t			first = (static_cast<size_t>(element.triangleStart) + triangle) * 3;
					const Foundation::Vec2* points = &elementPoints[first];
					const float*			values = &elementValues[first];
					const Foundation::Vec2	corners[4] = {points[0], points[1], points[2], points[2]};
					const uint32_t			fills[4] = {
						 glm::packUnorm4x8(Foundation::Vec4(values[0])),
						 glm::packUnorm4x8(Foundation::Vec4(values[1])),
						 glm::packUnorm4x8(Foundation::Vec4(values[2])),
						 glm::packUnorm4x8(Foundation::Vec4(values[2])),
					 };
					flatQuad(corners, fills);
				}
			}
			coverQuadStart = static_cast<uint32_t>(maskedUpload.size());
			const uint32_t opaque = glm::packUnorm4x8(Foundation::Vec4(0.0F, 0.0F, 0.0F, 1.0F));
			const uint32_t kOpaqueFill[4] = {opaque, opaque, opaque, opaque};
			for (const MaskEntry& entry : maskEntries) {
				const Foundation::Vec4& box = entry.cover;
				const Foundation::Vec2	corners[4] = {{box.x, box.y}, {box.z, box.y}, {box.z, box.w}, {box.x, box.w}};
				flatQuad(corners, kNoFill);
				flatQuad(corners, kOpaqueFill);
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
		lastUniforms = uniforms;

		// One draw call per run, as one submission unless a mask is built between runs. A
		// masked run lands where the built mask holds, its accumulator bit set or, built
		// inverted, clear; consecutive runs under the same mask share its build, since nothing
		// between them writes the stencil. A run under a content layer is drawn a quad at a time,
		// the mask evaluated into the target's alpha before each.
		backend::StencilState maskTest{
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
				if (maskLayers[maskEntries[run.mask - 1].layerStart].content) {
					drawUnderCoverage(run);
					built = 0;
					continue;
				}
				const bool inverted = buildMask(run.mask);
				maskTest.reference = inverted ? 0 : kMaskAccumulatorBit;
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

	void BatchRenderer::useUniforms(const backend::DrawUniforms& next) {
		if (std::memcmp(&next, &lastUniforms, sizeof(backend::DrawUniforms)) == 0) {
			return;
		}
		submitPending();
		gpu.setUniforms(next);
		lastUniforms = next;
	}

	// A content layer's quads, each over its own evaluation of the mask in the target's alpha
	// (buildCoverageMask): the destination alpha first multiplied by the quad's own, then its
	// colour, straight, over the target by that alpha, so the quad lands at its alpha times the
	// mask, as CSS composites a masked element. The alpha is set back to 1 over the cover box after
	// each, which leaves the target opaque there whatever its alpha held, and the cover box's
	// stencil bit cleared with it; the bit is cleared across the target once before the run, since
	// a game's GL may have left it set.
	void BatchRenderer::drawUnderCoverage(const DrawRun& run) {
		if (!gpu.hasAlphaAndDepth()) {
			if (!warnedNoAlphaAndDepth) {
				warnedNoAlphaAndDepth = true;
				LOG_ERROR(
					Renderer,
					"A draw under a content mask met a target with no alpha or depth: it needs the window made with 8 alpha and 24 "
					"depth bits, or a RenderTarget whose format has alpha made with ColourAndStencil; it draws nothing (logged once)"
				);
			}
			return;
		}
		submitPending();
		gpu.clearStencil(kCoverBoxBit);
		backend::DrawUniforms straight = batchedUniforms;
		straight.straightOutput = 1;
		const uint32_t opaqueCover = coverQuadStart + (2 * (run.mask - 1)) + 1;
		for (uint32_t instance = run.instanceStart; instance < run.instanceStart + run.instanceCount; ++instance) {
			buildCoverageMask(run.mask);
			useUniforms(batchedUniforms);
			drawCalls.push_back(
				{.pipeline = pipeline,
				 .geometry = geometry,
				 .blend = backend::BlendState::MultiplyDestinationAlpha,
				 .writes = backend::ChannelWrites::Alpha,
				 .textures = run.textures,
				 .firstInstance = instance,
				 .instanceCount = 1}
			);
			useUniforms(straight);
			drawCalls.push_back(
				{.pipeline = pipeline,
				 .geometry = geometry,
				 .blend = backend::BlendState::OverByDestinationAlpha,
				 .writes = backend::ChannelWrites::Colour,
				 .textures = run.textures,
				 .firstInstance = instance,
				 .instanceCount = 1}
			);
			useUniforms(batchedUniforms);
			drawCalls.push_back(
				{.pipeline = pipeline,
				 .geometry = geometry,
				 .blend = backend::BlendState::Replace,
				 .stencil = {.test = true, .writeMask = kCoverBoxBit, .pass = backend::StencilOperation::Zero},
				 .writes = backend::ChannelWrites::Alpha,
				 .firstInstance = opaqueCover,
				 .instanceCount = 1}
			);
		}
		submitPending();
	}

	namespace {
		backend::BlendState coverageBlend(Primitives::MixBlendMode blend) {
			switch (blend) {
				case Primitives::MixBlendMode::Lighten:
					return backend::BlendState::Max;
				case Primitives::MixBlendMode::Darken:
					return backend::BlendState::Min;
				case Primitives::MixBlendMode::Normal:
				default:
					return backend::BlendState::Replace;
			}
		}
	} // namespace

	// The content layer on top of mask `index`, its value in the target's alpha over the cover box:
	//
	// - The layers under it are built in the stencil as any mask is.
	// - The canvas starts at 0 over the cover box, which sets the cover box's stencil bit there; every
	//   element draws only where that bit is set, so none writes alpha or depth outside the box.
	// - An element outside a group lands on the canvas by its blend, replace, max or min on alpha, under
	//   its own mask's stencil bit.
	// - An isolated group's value, the greatest of its elements, is found in depth: its elements are drawn
	//   with their values as depth, above every earlier group's, keeping the greater, colour off and
	//   each under its own mask; then drawn again where their depth equals what is stored, writing
	//   their values to alpha by the group's blend. Only the element that holds the greatest at a pixel,
	//   or one equal to it, passes, so the group lands as its greatest, where any of its elements does.
	// - Source over sets the alpha to 1 where the layers under it cover, source out to 0.
	void BatchRenderer::buildCoverageMask(uint32_t index) {
		const MaskEntry&	  entry = maskEntries[index - 1];
		const MaskLayerEntry& content = maskLayers[entry.layerStart];
		const uint32_t		  zeroCover = coverQuadStart + (2 * (index - 1));
		const bool			  layersUnder = entry.layerCount > 1;
		const bool			  inverted = layersUnder && buildMask(index, 1);
		useUniforms(batchedUniforms);
		drawCalls.push_back(
			{.pipeline = pipeline,
			 .geometry = geometry,
			 .blend = backend::BlendState::Replace,
			 .stencil = {.test = true, .reference = kCoverBoxBit, .writeMask = kCoverBoxBit, .pass = backend::StencilOperation::Replace},
			 .writes = backend::ChannelWrites::Alpha,
			 .firstInstance = zeroCover,
			 .instanceCount = 1}
		);

		builtElementMask = 0;
		uint32_t	groupsSinceClear = kCoverageGroupsPerClear;
		const float groupSpan = 1.0F / static_cast<float>(kCoverageGroupsPerClear);
		for (uint32_t offset = 0; offset < content.elementCount;) {
			const MaskElementEntry& element = maskElements[content.elementStart + offset];
			if (element.group == 0) {
				drawElement(
					content.elementStart + offset,
					coverageBlend(element.blend),
					backend::ChannelWrites::Alpha,
					backend::DepthState::Off,
					true,
					{0.0F, 0.0F}
				);
				++offset;
				continue;
			}
			uint32_t end = offset + 1;
			while (end < content.elementCount && maskElements[content.elementStart + end].group == element.group) {
				++end;
			}
			if (groupsSinceClear + 1 >= kCoverageGroupsPerClear) {
				submitPending();
				gpu.clearDepth();
				groupsSinceClear = 0;
			}
			++groupsSinceClear;
			const Foundation::Vec2 depth{static_cast<float>(groupsSinceClear) * groupSpan, 0.5F * groupSpan};
			for (uint32_t member = offset; member < end; ++member) {
				drawElement(
					content.elementStart + member,
					backend::BlendState::Alpha,
					backend::ChannelWrites::None,
					backend::DepthState::GreaterOrEqual,
					true,
					depth
				);
			}
			for (uint32_t member = offset; member < end; ++member) {
				drawElement(
					content.elementStart + member,
					coverageBlend(element.groupBlend),
					backend::ChannelWrites::Alpha,
					backend::DepthState::Equal,
					false,
					depth
				);
			}
			offset = end;
		}

		if (layersUnder) {
			useUniforms(batchedUniforms);
			const bool sourceOver = content.composite == Primitives::MaskComposite::Add;
			drawCalls.push_back(
				{.pipeline = pipeline,
				 .geometry = geometry,
				 .blend = backend::BlendState::Replace,
				 .stencil =
					 {.test = true,
					  .function = backend::StencilFunction::Equal,
					  .reference = inverted ? uint8_t{0} : kMaskAccumulatorBit,
					  .readMask = kMaskAccumulatorBit,
					  .writeMask = 0},
				 .writes = backend::ChannelWrites::Alpha,
				 .firstInstance = sourceOver ? zeroCover + 1 : zeroCover,
				 .instanceCount = 1}
			);
		}
		submitPending();
	}

	void BatchRenderer::drawElement(
		uint32_t			   elementIndex,
		backend::BlendState	   blend,
		backend::ChannelWrites writes,
		backend::DepthState	   depth,
		bool				   underOwnMask,
		Foundation::Vec2	   coverageDepth
	) {
		const MaskElementEntry& element = maskElements[elementIndex];
		backend::StencilState	stencil{
			  .test = true,
			  .function = backend::StencilFunction::Equal,
			  .reference = kCoverBoxBit,
			  .readMask = kCoverBoxBit,
			  .writeMask = 0,
		  };
		if (underOwnMask && element.mask != 0) {
			if (builtElementMask != element.mask) {
				builtElementMaskInverted = buildMask(
					element.mask, 0, {.accumulator = kElementMaskAccumulatorBit, .scratch = kElementMaskScratchBit, .clearOverCover = true}
				);
				builtElementMask = element.mask;
			}
			stencil.reference = builtElementMaskInverted ? kCoverBoxBit : static_cast<uint8_t>(kCoverBoxBit | kElementMaskAccumulatorBit);
			stencil.readMask = static_cast<uint8_t>(kCoverBoxBit | kElementMaskAccumulatorBit);
		}
		if (element.meshGeometry.isValid()) {
			backend::DrawUniforms world = worldUniforms(element.camera.position, element.camera.zoom, element.camera.pixelsPerMeter);
			world.bakedAlpha = 1.0F;
			world.coverageDepth = coverageDepth;
			useUniforms(world);
			submitPending();
			const InstanceData origin;
			gpu.updateBuffer(element.meshInstances, 0, sizeof(InstanceData), &origin);
			drawCalls.push_back(
				{.pipeline = pipeline,
				 .geometry = element.meshGeometry,
				 .blend = blend,
				 .stencil = stencil,
				 .writes = writes,
				 .depth = depth,
				 .indexCount = element.meshIndexCount,
				 .instanceCount = 1}
			);
			submitPending();
			return;
		}
		if (element.triangleCount == 0) {
			return;
		}
		backend::DrawUniforms flat = batchedUniforms;
		flat.coverageDepth = coverageDepth;
		useUniforms(flat);
		drawCalls.push_back(
			{.pipeline = pipeline,
			 .geometry = geometry,
			 .blend = blend,
			 .stencil = stencil,
			 .writes = writes,
			 .depth = depth,
			 .firstInstance = elementFirstInstance[elementIndex],
			 .instanceCount = element.triangleCount}
		);
	}

	// Layers [firstLayer, end) of the mask, composited bottom up, evaluated in two stencil bits over
	// the box round where the mask is read (the entry's cover box). The accumulator holds the
	// layers composited so far, either as is or inverted: the mask is the accumulator bit xor
	// `inverted`. The scratch bit holds a Subtract layer's coverage.
	//
	// - Add (and the bottom layer, whose composite is ignored) is the union: it sets the
	//   accumulator where the layer lands, or zeroes it there when inverted.
	// - Subtract by a layer that covers the whole box (coversEntry), all ones there, is the
	//   complement of what is under it, so it flips `inverted` and draws nothing.
	// - Subtract by any other layer is the layer outside what is under it. The layer is drawn
	//   into the scratch bit, then two cover passes over the box: zero the accumulator where the
	//   scratch bit is clear; then, where it is set, invert the accumulator (not inverted: the
	//   layer outside what it held) or leave it (inverted: what it holds is already the
	//   complement), and clear the scratch bit. The result is not inverted.
	//
	// The bits are cleared first, across the target, or over the cover box for an element's own mask,
	// whose bits are read only there. Every layer draws aliased, with no clip and no colour.
	// Returns `inverted`, which the draws under it read: they draw where the accumulator bit is
	// clear when it is set.
	bool BatchRenderer::buildMask(uint32_t index, uint32_t firstLayer, const MaskBits& bits) {
		using backend::StencilFunction;
		using backend::StencilOperation;
		const backend::StencilState addLayer{
			.test = true,
			.reference = bits.accumulator,
			.writeMask = bits.accumulator,
			.pass = StencilOperation::Replace,
		};
		const backend::StencilState addInvertedLayer{
			.test = true,
			.writeMask = bits.accumulator,
			.pass = StencilOperation::Zero,
		};
		const backend::StencilState scratchLayer{
			.test = true,
			.reference = bits.scratch,
			.writeMask = bits.scratch,
			.pass = StencilOperation::Replace,
		};
		const backend::StencilState clearOutsideLayer{
			.test = true,
			.function = StencilFunction::Equal,
			.reference = 0,
			.readMask = bits.scratch,
			.writeMask = bits.accumulator,
			.pass = StencilOperation::Zero,
		};
		const backend::StencilState invertInsideLayer{
			.test = true,
			.function = StencilFunction::Equal,
			.reference = bits.scratch,
			.readMask = bits.scratch,
			.writeMask = static_cast<uint8_t>(bits.accumulator | bits.scratch),
			.pass = StencilOperation::Invert,
		};
		const backend::StencilState keepInsideLayer{
			.test = true,
			.function = StencilFunction::Equal,
			.reference = bits.scratch,
			.readMask = bits.scratch,
			.writeMask = bits.scratch,
			.pass = StencilOperation::Invert,
		};
		const backend::StencilState clearBits{
			.test = true,
			.writeMask = static_cast<uint8_t>(bits.accumulator | bits.scratch),
			.pass = StencilOperation::Zero,
		};
		const auto stencilOnly = [this](const backend::StencilState& stencil, uint32_t firstInstance, uint32_t instanceCount) {
			if (instanceCount > 0) {
				drawCalls.push_back(
					{.pipeline = pipeline,
					 .geometry = geometry,
					 .stencil = stencil,
					 .writes = backend::ChannelWrites::None,
					 .firstInstance = firstInstance,
					 .instanceCount = instanceCount}
				);
			}
		};

		const MaskEntry& entry = maskEntries[index - 1];
		const uint32_t	 coverInstance = coverQuadStart + (2 * (index - 1));
		const auto		 subtracts = [&entry, this](uint32_t offset) {
			  return offset + 1 < entry.layerCount && maskLayers[entry.layerStart + offset].composite == Primitives::MaskComposite::Subtract;
		};

		useUniforms(batchedUniforms);
		if (bits.clearOverCover) {
			stencilOnly(clearBits, coverInstance, 1);
		} else {
			submitPending();
			gpu.clearStencil(static_cast<uint8_t>(bits.accumulator | bits.scratch));
		}
		bool inverted = false;
		for (uint32_t offset = entry.layerCount; offset-- > firstLayer;) {
			const uint32_t		  layerIndex = entry.layerStart + offset;
			const MaskLayerEntry& layer = maskLayers[layerIndex];
			const bool			  subtract = subtracts(offset);
			if (subtract && coversEntry(layer, entry)) {
				inverted = !inverted;
				continue;
			}
			const backend::StencilState& coverage = subtract ? scratchLayer : (inverted ? addInvertedLayer : addLayer);
			if (layer.meshGeometry.isValid()) {
				// One copy at the world origin under the layer's camera, drawn under the
				// instanced uniforms, then the batch's again.
				useUniforms(worldUniforms(layer.camera.position, layer.camera.zoom, layer.camera.pixelsPerMeter));
				const InstanceData origin;
				gpu.updateBuffer(layer.meshInstances, 0, sizeof(InstanceData), &origin);
				drawCalls.push_back(
					{.pipeline = pipeline,
					 .geometry = layer.meshGeometry,
					 .stencil = coverage,
					 .writes = backend::ChannelWrites::None,
					 .indexCount = layer.meshIndexCount,
					 .instanceCount = 1}
				);
				useUniforms(batchedUniforms);
			} else {
				stencilOnly(coverage, layerFirstInstance[layerIndex], layer.triangleCount);
			}
			if (subtract) {
				stencilOnly(clearOutsideLayer, coverInstance, 1);
				stencilOnly(inverted ? keepInsideLayer : invertInsideLayer, coverInstance, 1);
				inverted = false;
			}
		}
		submitPending();
		return inverted;
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
		float					   pixelsPerMeter,
		Primitives::IndexRange	   indices
	) {
		if (!handle.isValid() || count == 0 || instances == nullptr) {
			return;
		}
		const bool toEnd = indices.count == Primitives::IndexRange::kToEnd;
		const bool pastEnd = indices.first > handle.indexCount || (!toEnd && indices.count > handle.indexCount - indices.first);
		// A range that isn't whole triangles would draw triangles stitched from its neighbours.
		const bool wholeTriangles = indices.first % 3 == 0 && (toEnd || indices.count % 3 == 0);
		if (pastEnd || !wholeTriangles) {
			if (!warnedBadIndexRange) {
				warnedBadIndexRange = true;
				LOG_ERROR(
					Renderer,
					"drawInstanced: %u indices from %u are not whole triangles within the mesh's %u; not drawn (logged once)",
					indices.count,
					indices.first,
					handle.indexCount
				);
			}
			return;
		}
		const Primitives::IndexRange range{.first = indices.first, .count = toEnd ? handle.indexCount - indices.first : indices.count};
		if (range.count == 0) {
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
				.indexOffset = range.first,
				.indexCount = range.count,
				.instanceCount = batchSize
			};
			beginGpuSpan();
			gpu.draw({&call, 1});
			endGpuSpan();

			drawCallCount++;
			// The whole mesh's vertices are counted for a range too: which of them a range reads
			// isn't known here.
			frameVertexCount += static_cast<size_t>(handle.vertexCount) * batchSize;
			frameTriangleCount += static_cast<size_t>(range.count / 3) * batchSize;
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
