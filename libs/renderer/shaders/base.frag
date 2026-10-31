#version 330 core

// Base shader - unified fragment shader for shapes and text
// (Ground tiles render in a dedicated pass: tile.vert/tile.frag)
//
// Output is premultiplied alpha in every mode, blended (ONE, ONE_MINUS_SRC_ALPHA).
// Batched colours arrive premultiplied; the instanced and baked paths' arrive
// straight. An additive draw writes alpha 0, which makes the same blend state add
// its colour and leave the destination alpha alone.

in vec2			v_local;
in vec2			v_texCoord;
in vec4			v_fill;
flat in vec2	v_halfSize;
flat in vec4	v_radii;
flat in vec4	v_border;
flat in vec4	v_shape; // (border width, reach, blur or range, opacity)
flat in vec4	v_clip;	 // (minX, minY, maxX, maxY), or zero for none
flat in uvec2	v_mode;	 // (mode, flags)

out vec4 FragColor;

// The run's texture: the MSDF font atlas for text, the image for image quads
// (ignored by every other mode)
uniform sampler2D u_atlas;

// u_bakedAlpha (instanced and baked draws), and u_viewportHeight and u_pixelRatio
// (the clip test's y flip: OpenGL's origin is bottom-left, the UI's top-left)
#include "includes/uniforms.glsl"

// Modes and flags, as BatchRenderer.h has them; instanced is base.vert's own
const uint kModeFlat = 0u;
const uint kModeRect = 1u;
const uint kModeShadow = 2u;
const uint kModeArc = 3u;
const uint kModeImage = 4u;
const uint kModeInstanced = 7u;
const uint kFlagAdditive = 1u;
const uint kFlagBorderShift = 1u; // two bits: the border inside, centred or outside

const float kPi = 3.14159265;

// SDF for a rounded rectangle, Inigo Quilez's, each quadrant with its own corner's
// radius, (top-left, top-right, bottom-right, bottom-left) with y down. A radius
// larger than the shape is clamped to its half size, a capsule.
float sdRoundedBox(vec2 p, vec2 size, vec4 radii) {
	float radius = p.x < 0.0 ? (p.y < 0.0 ? radii.x : radii.w) : (p.y < 0.0 ? radii.y : radii.z);
	radius = min(radius, min(size.x, size.y));
	vec2 q = abs(p) - size + radius;
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

// Median of three values (for multi-channel SDF)
float median(float r, float g, float b) {
	return max(min(r, g), min(max(r, g), b));
}

// The distance field's range in screen pixels, from the UV's change per pixel
// (fwidth of v_texCoord)
float screenPxRange(float pixelRange, vec2 texCoordWidth) {
	vec2 unitRange = vec2(pixelRange) / vec2(textureSize(u_atlas, 0));
	vec2 screenTexSize = vec2(1.0) / texCoordWidth;
	return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

// How much of the pixel lies where dist <= edge, ramped over a pixel either side.
float coverage(float edge, float dist, float pixelSize) {
	return 1.0 - smoothstep(edge - pixelSize, edge + pixelSize, dist);
}

// A premultiplied colour, its alpha 0 when additive.
vec4 blended(vec4 color, bool additive) {
	return vec4(color.rgb, additive ? 0.0 : color.a);
}

void main() {
	// Every derivative is taken here, in uniform control flow, before any branch or
	// discard: inside the mode branches they would be undefined. The texture reads
	// below take these as their gradients for the same reason. The rounded box is
	// the rect's and the shadow's distance; the other modes ignore it.
	vec2  texCoordDx = dFdx(v_texCoord);
	vec2  texCoordDy = dFdy(v_texCoord);
	vec2  localDx = dFdx(v_local);
	vec2  localDy = dFdy(v_local);
	float boxDist = sdRoundedBox(v_local, v_halfSize, v_radii);
	float boxPixel = length(vec2(dFdx(boxDist), dFdy(boxDist)));

	uint mode = v_mode.x;
	bool additive = (v_mode.y & kFlagAdditive) != 0u;

	if (mode == kModeInstanced) {
		float alpha = v_fill.a * u_bakedAlpha;
		FragColor = vec4(v_fill.rgb * alpha, alpha);
		return;
	}

	// The clip, in logical pixels with a top-left origin, tested in physical pixels,
	// half-open: a pixel centre on the right or bottom edge is outside, so a clip w
	// pixels wide keeps w columns. A clip with no width is none.
	if (v_clip.z > v_clip.x) {
		vec4  physicalClip = v_clip * u_pixelRatio;
		float physicalY = u_viewportHeight - gl_FragCoord.y;
		if (gl_FragCoord.x < physicalClip.x || gl_FragCoord.x >= physicalClip.z || physicalY < physicalClip.y ||
			physicalY >= physicalClip.w) {
			discard;
		}
	}

	if (mode == kModeFlat) {
		// Triangles, lines, feather rings: the colour interpolated across the corners
		FragColor = blended(v_fill, additive);

	} else if (mode == kModeRect) {
		// The fill covers boxDist <= 0 and the border its band, each pixel by coverage.
		// A stroke piece is a rect with no border.
		uint  borderPos = (v_mode.y >> kFlagBorderShift) & 3u;
		float borderWidth = v_shape.x;
		float borderInner;
		float borderOuter;
		if (borderPos == 0u) {
			borderOuter = 0.0;
			borderInner = -borderWidth;
		} else if (borderPos == 1u) {
			borderOuter = borderWidth * 0.5;
			borderInner = -borderWidth * 0.5;
		} else {
			borderOuter = borderWidth;
			borderInner = 0.0;
		}

		// A zero-size box with no border covers its whole quad: left to smoothstep,
		// it would ask for smoothstep(0, 0, 0), which GLSL leaves undefined.
		float fillCover = 1.0;
		float bandCover = 0.0;	  // between the border's inner and outer edges
		float bandOverFill = 0.0; // the part of the band inside the shape's edge
		if (v_halfSize.x > 0.0 || v_halfSize.y > 0.0 || borderWidth > 0.001) {
			fillCover = coverage(0.0, boxDist, boxPixel);
		}
		if (borderWidth > 0.001) {
			float innerCover = coverage(borderInner, boxDist, boxPixel);
			bandCover = coverage(borderOuter, boxDist, boxPixel) - innerCover;
			bandOverFill = fillCover - innerCover;
		}
		if (fillCover + bandCover < 0.001) {
			discard;
		}

		// The border over the fill where the band lies inside the shape and beside it
		// outside. The coverages share their edge ramps, so where the fill and an
		// outside border abut they add up to the pixel exactly.
		vec4 color = v_fill * (fillCover - v_border.a * bandOverFill) + v_border * bandCover;
		FragColor = blended(color, additive);

	} else if (mode == kModeShadow) {
		// Box-shadow or glow: an approximation of CSS's Gaussian blur, the falloff centred
		// on the shape's edge (half alpha there), full `blur` px inside, zero `blur` outside.
		float blur = max(v_shape.z, 0.5);
		float a = 1.0 - smoothstep(-blur, blur, boxDist);
		if (a < 0.001) {
			discard;
		}
		FragColor = blended(v_fill * a, additive);

	} else if (mode == kModeArc) {
		// Ring, arc or pie: v_radii is (inner, outer, halfSweep). Folded across the x
		// axis, the arc is every angle up to halfSweep; its ends are straight radial cuts.
		vec2  p = vec2(v_local.x, abs(v_local.y));
		float r = length(p);
		// Outside the outer radius, or inside the inner one when there is a hole: a pie's
		// centre is not an edge.
		float dist = r - v_radii.y;
		if (v_radii.x > 0.0) {
			dist = max(dist, v_radii.x - r);
		}
		if (v_radii.z < kPi - 0.0001) {
			vec2 end = vec2(cos(v_radii.z), sin(v_radii.z));
			// Past the end ray is positive. Behind the centre the nearest point of the
			// ray is the centre itself.
			float side = end.x * p.y - end.y * p.x;
			dist = max(dist, dot(p, end) > 0.0 ? side : sign(side) * r);
		}
		// One pixel in arc-local units, from the coordinates themselves: the folds
		// above leave the distance's own derivatives unreliable.
		float pixelSize = sqrt(0.5 * (dot(localDx, localDx) + dot(localDy, localDy)));
		float a = 1.0 - smoothstep(-pixelSize, pixelSize, dist);
		if (a < 0.001) {
			discard;
		}
		FragColor = blended(v_fill * a, additive);

	} else if (mode == kModeImage) {
		// Texels are premultiplied, so the premultiplied tint multiplies straight in.
		FragColor = blended(textureGrad(u_atlas, v_texCoord, texCoordDx, texCoordDy) * v_fill, additive);

	} else {
		// MSDF text: the median of the three channels is the signed distance, v_shape.z
		// the atlas's distance range in texels
		vec3  msd = textureGrad(u_atlas, v_texCoord, texCoordDx, texCoordDy).rgb;
		float sd = median(msd.r, msd.g, msd.b);
		float screenPxDistance = screenPxRange(v_shape.z, abs(texCoordDx) + abs(texCoordDy)) * (sd - 0.5);
		float opacity = smoothstep(-0.5, 0.5, screenPxDistance);
		FragColor = blended(v_fill * opacity, additive);
	}
}
