#pragma once

// A CSS mask on a draw (docs/masks.md): mask-image layers, each composited with the layers under it
// by mask-composite. A rect, polygon or mesh layer covers a pixel or doesn't; a content layer, an
// SVG <mask> of shapes painted with values and combined by mix-blend-mode, covers it partly.

#include "Handle.h"
#include "graphics/Rect.h"
#include "math/Types.h"
#include "primitives/InstanceData.h"
#include "primitives/WorldCamera.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Renderer::Primitives { // NOLINT(readability-identifier-naming)

	struct MaskShape;

	// CSS mask-composite. The layer is the source; the layers under it, composited first, are the destination.
	//   Add       source over: the layer and everything under it
	//   Subtract  source out: the layer where it falls outside what is under it
	enum class MaskComposite : uint8_t { Add, Subtract };

	// CSS mask-mode: how a layer's image becomes coverage. Alpha reads the image's alpha, Luminance
	// its luminance times its alpha. A rect, polygon or mesh layer is white where it covers, so the
	// two agree on it; a content layer's shapes are opaque greys, so Alpha reads where any shape
	// paints and Luminance reads the values they paint.
	enum class MaskMode : uint8_t { Alpha, Luminance };

	// CSS mix-blend-mode on a content layer's shape: how its value meets what is painted under it.
	//   Normal   its value
	//   Lighten  the greater of the two
	//   Darken   the lesser of the two
	enum class MaskBlend : uint8_t { Normal, Lighten, Darken };

	// CSS isolation on a group of shapes. Isolate composites the group's shapes together first, on
	// nothing, and blends the result with what is under it by the group's MaskBlend. A group whose
	// blend isn't Normal is isolated either way, as CSS makes it.
	enum class MaskIsolation : uint8_t { Auto, Isolate };

	// How a shape in a content layer is filled: one grey, or an SVG linearGradient in user space
	// with two stops, padded past its ends. A value is a grey from 0, black, to 1, white.
	class MaskPaint {
	  public:
		static MaskPaint flat(float value);

		// The value runs from `fromValue` at `from` to `toValue` at `to`, in the shape's own space,
		// and holds each end's value past it.
		static MaskPaint linear(Foundation::Vec2 from, Foundation::Vec2 to, float fromValue, float toValue);

	  private:
		friend struct MaskImageAccess;

		MaskPaint() = default;

		Foundation::Vec2 from{0.0F, 0.0F};
		Foundation::Vec2 to{0.0F, 0.0F};
		float			 fromValue = 0.0F;
		float			 toValue = 0.0F;
		bool			 gradient = false;
	};

	// An uploaded mesh as a shape in a content layer, one copy at the world origin in world space
	// under `camera`. Each vertex's value is its colour's alpha, the colour itself read as white.
	// The mesh must not be released before the draw it masks is flushed.
	class MaskMesh {
	  public:
		MaskMesh() = default;
		MaskMesh(const InstancedMeshHandle& mesh, const WorldCamera& camera); // NOLINT(google-explicit-constructor): .mesh = {mesh, camera}

	  private:
		friend struct MaskImageAccess;

		GeometryHandle geometry;
		BufferHandle   instances;
		uint32_t	   indexCount = 0;
		WorldCamera	   view;
	};

	// A layer's mask-image. Made only through its factories.
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

		// An SVG <mask> whose content is `shapes`, painted in order on nothing (black, value 0). The
		// shapes are read by the draw the layer is given to, so they need to live until that call
		// returns.
		static MaskImage content(std::span<const MaskShape> shapes);

	  private:
		friend struct MaskImageAccess;

		enum class Kind : uint8_t { Rect, Polygon, Mesh, Content };

		MaskImage() = default;

		Kind							  kind = Kind::Rect;
		Foundation::Rect				  bounds{};
		std::span<const Foundation::Vec2> points;
		GeometryHandle					  meshGeometry;
		BufferHandle					  meshInstances;
		uint32_t						  meshIndexCount = 0;
		WorldCamera						  camera;
		const MaskShape*				  shapes = nullptr;
		size_t							  shapeCount = 0;
	};

	// One layer of a CSS mask: its mask-image, mask-composite and mask-mode.
	struct MaskLayer {
		MaskImage	  image;
		MaskComposite composite = MaskComposite::Add;
		MaskMode	  mode = MaskMode::Alpha;
	};

	// A shape in a content layer, as an element inside an SVG <mask>: a <polygon> (`points`, filled
	// by the nonzero rule in the current transform's space), an uploaded mesh (`mesh`), or a <g> of
	// `children`. A polygon is filled by `paint`, black unless set, as SVG fills; a mesh by its
	// vertices' values. `mask` is the shape's own mask, rect, polygon and mesh layers only: it
	// paints only where they cover. Everything is read by the draw, as the layers are.
	struct MaskShape {
		std::span<const Foundation::Vec2> points;
		MaskMesh						  mesh;
		std::vector<MaskShape>			  children;
		MaskPaint						  paint = MaskPaint::flat(0.0F);
		std::span<const MaskLayer>		  mask;
		MaskBlend						  blend = MaskBlend::Normal;
		MaskIsolation					  isolation = MaskIsolation::Auto;
	};

} // namespace Renderer::Primitives
