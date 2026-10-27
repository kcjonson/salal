// uniforms.glsl - The per-draw uniforms, one std140 block for base.vert and base.frag
//
// The backend writes the whole block for every draw (setUniforms). It is
// backend::DrawUniforms in backend/Backend.h member for member, which asserts the
// offsets in the comments; change the two together. Included once per stage.

layout(std140) uniform DrawUniforms {
	mat4  u_projection;          //   0
	mat4  u_transform;           //  64
	vec2  u_cameraPosition;      // 128 camera world position (center of view)
	vec2  u_viewportSize;        // 136 viewport in logical pixels, for the world-space modes
	vec2  u_cursorWorld;         // 144 groundcover interaction center, world space
	float u_cameraZoom;          // 152 1.0 = normal, >1 = zoomed in
	float u_pixelsPerMeter;      // 156 world scale (pixels per world unit)
	float u_bakedAlpha;          // 160 alpha for instanced and baked draws (far-zoom impostor cross-fade)
	float u_viewportHeight;      // 164 framebuffer height in physical pixels, for the clip test's y flip
	float u_pixelRatio;          // 168 physical pixels per logical pixel
	int   u_instanced;           // 172 0 batched, 1 GPU instancing, 2 baked world space
	// Groundcover deform, a no-op at mode 0 (trees and other instanced entities).
	// When u_groundcoverMode == 1, a clump is compact by default (scaled by u_groundcoverOpenness, so
	// top-down it reads small) and reveals, expanding toward full size and bending away, within
	// u_cursorRadius of u_cursorWorld, like groundcover parting as a colonist walks through.
	int   u_groundcoverMode;     // 176 1 = apply the groundcover deform
	float u_groundcoverOpenness; // 180 resting clump scale 0..1 (1.0 = full size)
	float u_groundcoverReach;    // 184 max local vertex distance from a clump's base (meters)
	float u_cursorRadius;        // 188 interaction radius, world space
	float u_cursorStrength;      // 192 push distance at the clump's outer tips, world space
};
