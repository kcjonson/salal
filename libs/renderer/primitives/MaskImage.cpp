#include "primitives/MaskImage.h"

namespace Renderer::Primitives {

	MaskPaint MaskPaint::flat(float value) {
		MaskPaint paint;
		paint.fromValue = value;
		paint.toValue = value;
		return paint;
	}

	MaskPaint MaskPaint::linear(Foundation::Vec2 from, Foundation::Vec2 to, float fromValue, float toValue) {
		MaskPaint paint;
		paint.from = from;
		paint.to = to;
		paint.fromValue = fromValue;
		paint.toValue = toValue;
		paint.gradient = true;
		return paint;
	}

	MaskMesh::MaskMesh(const InstancedMeshHandle& mesh, const WorldCamera& camera)
		: geometry(mesh.geometry),
		  instances(mesh.instances),
		  indexCount(mesh.indexCount),
		  view(camera) {}

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

	MaskImage MaskImage::content(std::span<const MaskShape> shapes) {
		MaskImage image;
		image.kind = Kind::Content;
		image.shapes = shapes.data();
		image.shapeCount = shapes.size();
		return image;
	}

} // namespace Renderer::Primitives
