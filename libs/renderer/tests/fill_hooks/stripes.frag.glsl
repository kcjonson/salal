// stripes.frag.glsl - the fill hook the renderer tests (ShaderHooks.test.cpp) and the sandbox's
// fill_hook scene draw with: upright stripes across the rect, params.x's colour then params.y's
// (rgba8, premultiplied), each params.z local units wide (a float's bits), counted from the rect's
// left edge plus the block's shift. A pixel across a stripe's edge takes both colours by how much
// of it lies each side, so one centred on an edge is half and half. A width of 0 is the rect's own
// fill; the block starts zeroed, which shifts nothing.

layout(std140) uniform StripesUniforms {
	float shift; // 0
} stripes;

vec4 stripes_rgba8(uint word) {
	return vec4(float(word & 255u), float((word >> 8u) & 255u), float((word >> 16u) & 255u), float(word >> 24u)) / 255.0;
}

vec4 stripes_fill(vec4 color, vec2 local, vec2 halfSize, uvec3 params, float pixelSize) {
	float width = uintBitsToFloat(params.z);
	if (!(width > 0.0)) {
		return color;
	}
	float x = (local.x + halfSize.x - stripes.shift) / width;
	float band = floor(x);
	bool  even = mod(band, 2.0) < 0.5;
	// Pixels from the pixel's centre to its stripe's nearer edge
	float inside = min(x - band, band + 1.0 - x) * width / max(pixelSize, 1e-6);
	return mix(stripes_rgba8(even ? params.y : params.x), stripes_rgba8(even ? params.x : params.y), clamp(0.5 + inside, 0.0, 1.0));
}
