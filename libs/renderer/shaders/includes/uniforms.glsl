// uniforms.glsl - The per-draw uniforms, one std140 block for base.vert and base.frag
//
// The backend writes the whole block for every draw (setUniforms). It is
// backend::DrawUniforms in backend/Backend.h member for member, which asserts the
// offsets in the comments; change the two together. Included once per stage.

layout(std140) uniform DrawUniforms {
	mat4  u_projection;     //   0
	mat4  u_transform;      //  64
	vec2  u_cameraPosition; // 128 camera world position (center of view)
	vec2  u_viewportSize;   // 136 viewport in logical pixels, for the world-space modes
	float u_cameraZoom;     // 144 1.0 = normal, >1 = zoomed in
	float u_pixelsPerMeter; // 148 world scale (pixels per world unit)
	float u_bakedAlpha;     // 152 alpha for instanced and baked draws (far-zoom impostor cross-fade)
	float u_viewportHeight; // 156 framebuffer height in physical pixels, for the clip test's y flip
	float u_pixelRatio;     // 160 physical pixels per logical pixel
	int   u_instanced;      // 164 0 batched, 1 GPU instancing, 2 baked world space
	vec2  u_coverageDepth;  // 168 a partial-coverage mask's value as window depth: (base, span), none at span 0
	int   u_straightOutput; // 176 1: the fragment's colour leaves straight, not premultiplied
};
