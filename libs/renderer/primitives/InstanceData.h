#pragma once

// InstanceData - Data structures for GPU instancing
//
// These structures define the per-instance data uploaded to the GPU
// for instanced rendering of entities.

#include "Handle.h"
#include "graphics/Color.h"
#include "math/Types.h"
#include <cstdint>

namespace Renderer {

	/// Per-instance data for GPU instancing (32 bytes, 16-byte aligned)
	/// This data is uploaded once per instance and used by the vertex shader
	/// to transform mesh vertices to screen space.
	struct alignas(16) InstanceData {
		Foundation::Vec2 worldPosition; // World-space position (8 bytes)
		float			 rotation;		// Rotation in radians (4 bytes)
		float			 scale;			// Uniform scale multiplier (4 bytes)
		Foundation::Vec4 colorTint;		// RGBA color multiplier (16 bytes)

		/// Default constructor - identity instance at origin
		InstanceData()
			: worldPosition(0.0F, 0.0F),
			  rotation(0.0F),
			  scale(1.0F),
			  colorTint(1.0F, 1.0F, 1.0F, 1.0F) {}

		/// Full constructor
		InstanceData(Foundation::Vec2 pos, float rot, float scl, Foundation::Vec4 tint)
			: worldPosition(pos),
			  rotation(rot),
			  scale(scl),
			  colorTint(tint) {}

		/// Convenience constructor with Color
		InstanceData(Foundation::Vec2 pos, float rot, float scl, const Foundation::Color& tint)
			: worldPosition(pos),
			  rotation(rot),
			  scale(scl),
			  colorTint(tint.r, tint.g, tint.b, tint.a) {}
	};

	static_assert(sizeof(InstanceData) == 32, "InstanceData must be 32 bytes for GPU alignment");
	static_assert(alignof(InstanceData) == 16, "InstanceData must be 16-byte aligned for GPU upload");

	/// A mesh uploaded for instanced drawing, from Primitives::uploadInstancedMesh().
	/// Move-only; the destructor releases its buffers and geometry on the backend
	/// (so does Primitives::releaseInstancedMesh(), which also leaves the handle
	/// invalid). After Primitives::shutdown() there is nothing left to release.
	class InstancedMeshHandle {
	  public:
		InstancedMeshHandle() = default;
		~InstancedMeshHandle();

		InstancedMeshHandle(const InstancedMeshHandle&) = delete;
		InstancedMeshHandle& operator=(const InstancedMeshHandle&) = delete;

		InstancedMeshHandle(InstancedMeshHandle&& other) noexcept;
		InstancedMeshHandle& operator=(InstancedMeshHandle&& other) noexcept;

		/// Check if this handle is valid (has GPU resources)
		[[nodiscard]] bool isValid() const { return geometry.isValid(); }

	  private:
		friend class BatchRenderer;

		// The backend's objects; BatchRenderer makes and releases them.
		GeometryHandle geometry;		 // the mesh and instance buffers in the InstancedMesh layout
		BufferHandle   vertices;		 // InstancedMeshVertex, static
		BufferHandle   indices;			 // uint16 triangles, static
		BufferHandle   instances;		 // InstanceData, rewritten per draw
		uint32_t	   indexCount = 0;	 // Number of indices in mesh
		uint32_t	   vertexCount = 0;	 // Number of vertices in mesh (for stats)
		uint32_t	   maxInstances = 0; // Capacity of instance buffer
	};

} // namespace Renderer
