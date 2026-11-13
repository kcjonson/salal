#include "primitives/MaskImage.h"

namespace Renderer::Primitives {

	MaskImage MaskImage::rect(const Foundation::Rect& bounds) {
		MaskImage image;
		image.kind = Kind::Rect;
		image.bounds = bounds;
		return image;
	}

	MaskImage MaskImage::polygon(std::span<const Foundation::Vec2> points) {
		MaskImage image;
		image.kind = Kind::Polygon;
		image.points = points;
		return image;
	}

	MaskImage MaskImage::mesh(const InstancedMeshHandle& mesh, const WorldCamera& camera) {
		MaskImage image;
		image.kind = Kind::Mesh;
		image.meshGeometry = mesh.geometry;
		image.meshInstances = mesh.instances;
		image.meshIndexCount = mesh.indexCount;
		image.camera = camera;
		return image;
	}

} // namespace Renderer::Primitives
