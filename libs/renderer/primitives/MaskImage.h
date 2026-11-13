#pragma once

// A CSS mask on a draw (docs/masks.md): mask-image layers, each composited with the layers under it
// by mask-composite. Coverage is binary, so a mask's edges are hard.

#include "Handle.h"
#include "graphics/Rect.h"
#include "math/Types.h"
#include "primitives/InstanceData.h"
#include "primitives/WorldCamera.h"
#include <cstdint>
#include <span>

namespace Renderer::Primitives { // NOLINT(readability-identifier-naming)

	// CSS mask-composite. The layer is the source; the layers under it, composited first, are the destination.
	//   Add       source over: the layer and everything under it
	//   Subtract  the layer where it falls outside what is under it
	enum class MaskComposite : uint8_t { Add, Subtract };

	// A layer's mask-image: vector content, as the children of an SVG <mask>. Hard-edged coverage.
	// Made only through its factories.
	class MaskImage {
	  public:
		// A rectangle, in the current transform's space.
		static MaskImage rect(const Foundation::Rect& bounds);

		// A closed outline filled by the nonzero rule, as drawPolygon fills it, in the current
		// transform's space. The points are read by the draw the layer is given to, so they need
		// to live until that call returns.
		static MaskImage polygon(std::span<const Foundation::Vec2> points);

		// An uploaded mesh, one copy at the world origin, in world space under `camera`. The mesh
		// must not be released before the draw it masks is flushed; releasing it while draws are
		// pending already waits for the flush.
		static MaskImage mesh(const InstancedMeshHandle& mesh, const WorldCamera& camera);

	  private:
		friend struct MaskImageAccess;

		enum class Kind : uint8_t { Rect, Polygon, Mesh };

		MaskImage() = default;

		Kind							  kind = Kind::Rect;
		Foundation::Rect				  bounds{};
		std::span<const Foundation::Vec2> points;
		GeometryHandle					  meshGeometry;
		BufferHandle					  meshInstances;
		uint32_t						  meshIndexCount = 0;
		WorldCamera						  camera;
	};

	// One layer of a CSS mask: its mask-image and its mask-composite.
	struct MaskLayer {
		MaskImage	  image;
		MaskComposite composite = MaskComposite::Add;
	};

} // namespace Renderer::Primitives
