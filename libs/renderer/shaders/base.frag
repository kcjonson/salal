#version 330 core

// Base shader - unified fragment shader for shapes and text
// Combines primitive.frag (SDF shapes) and msdf_text.frag (MSDF text)
// (Ground tiles render in a dedicated pass: tile.vert/tile.frag)
//
// Output is premultiplied alpha in every mode, blended (ONE, ONE_MINUS_SRC_ALPHA).
// Vertex colours arrive straight; each branch multiplies its colour by its alpha
// at the end. An additive draw writes alpha 0, which makes the same blend state
// add its colour and leave the destination alpha alone.

in vec2 v_texCoord;
in vec4 v_color;
in vec4 v_data1;
in vec4 v_data2;
in vec4 v_clipBounds;  // Clip rect (minX, minY, maxX, maxY) or (0,0,0,0) for no clip

out vec4 FragColor;

// The run's texture: the MSDF font atlas for text, the image for image quads
// (ignored by every other mode)
uniform sampler2D u_atlas;

// u_bakedAlpha (instanced and baked draws), and u_viewportHeight and u_pixelRatio
// (the clip test's y flip: OpenGL's origin is bottom-left, the UI's top-left)
#include "includes/uniforms.glsl"

// ============================================================================
// SHAPE SDF FUNCTIONS (from primitive.frag)
// ============================================================================

// SDF for rounded rectangle - Inigo Quilez's optimized implementation
// https://iquilezles.org/articles/distfunctions2d/
float sdRoundedBox(vec2 p, vec2 size, float radius) {
	// Clamp radius to half the smallest dimension to prevent invalid shapes
	radius = min(radius, min(size.x, size.y));

	// Exploit symmetry - work in positive quadrant
	vec2 q = abs(p) - size + radius;

	// Distance calculation
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

// ============================================================================
// TEXT MSDF FUNCTIONS (from msdf_text.frag)
// ============================================================================

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

// ============================================================================
// RENDER MODES
// ============================================================================
// data2.w carries the mode and the additive flag (BatchRenderer.h has the same
// table): shapes store their border position 0..2, plus 4 when additive; the
// negative modes below are 10 lower when additive. Instanced and baked draws
// (-2, set by base.vert) are never additive. Compares keep half-unit margins.
const float kRenderModeText = -1.0;      // MSDF text rendering
const float kRenderModeInstanced = -2.0; // Simple solid color (instanced entities)
const float kRenderModeShadow = -3.0;    // Box-shadow / glow (SDF soft falloff)
const float kRenderModeArc = -4.0;       // Ring, arc or pie (SDF)
const float kRenderModeImage = -5.0;     // Plain texture sample
const float kAdditiveShapeOffset = 4.0;
const float kAdditiveModeOffset = -10.0;
// A shape's mode carries its border's transparency in its fraction, 0 to this
const float kBorderTransparencySpan = 0.25;

const float kPi = 3.14159265;

bool isMode(float mode, float constant) {
	return abs(mode - constant) < 0.5;
}

// The colour `rgb` at coverage-weighted alpha `a`, premultiplied; alpha 0 when
// additive.
vec4 premultiplied(vec3 rgb, float a, bool additive) {
	return vec4(rgb * a, additive ? 0.0 : a);
}

// How much of the pixel lies where dist <= edge, ramped over a pixel either side.
float coverage(float edge, float dist, float pixelSize) {
	return 1.0 - smoothstep(edge - pixelSize, edge + pixelSize, dist);
}

// ============================================================================
// MAIN - Branch on render mode
// ============================================================================

void main() {
	// Every derivative is taken here, in uniform control flow, before any branch or
	// discard: inside the mode branches they would be undefined. The texture reads
	// below take these as their gradients for the same reason. The rounded box is
	// the shape's and the shadow's distance; the other modes ignore it.
	vec2  texCoordDx = dFdx(v_texCoord);
	vec2  texCoordDy = dFdy(v_texCoord);
	float boxDist = sdRoundedBox(v_texCoord, v_data2.xy, v_data2.z);
	float boxPixel = length(vec2(dFdx(boxDist), dFdy(boxDist)));

	bool  additive = v_data2.w > kAdditiveShapeOffset - 0.5 || v_data2.w < kAdditiveModeOffset - 0.5;
	float mode = v_data2.w;
	if (additive) {
		mode -= mode > 0.0 ? kAdditiveShapeOffset : kAdditiveModeOffset;
	}

	// ========== INSTANCED ENTITY RENDERING (simple solid color) ==========
	if (isMode(mode, kRenderModeInstanced)) {
		// Output the vertex color (includes instance color tint from SVG asset)
		FragColor = premultiplied(v_color.rgb, v_color.a * u_bakedAlpha, false);
		return;
	}

	// ========== FAST-PATH RECT CLIPPING (for UI elements only) ==========
	// Clip bounds check: (minX, minY, maxX, maxY) in logical UI coordinates (top-left origin)
	// NOT used for instanced world entities (v_clipBounds is zeroed for those)
	// A clipBounds of (0,0,0,0) means no clipping (maxX <= minX check detects this)
	if (v_clipBounds.z > v_clipBounds.x) {
		// Scale clip bounds from logical pixels to physical pixels for gl_FragCoord comparison
		vec4 physicalClipBounds = v_clipBounds * u_pixelRatio;

		// Convert gl_FragCoord.y from OpenGL coordinates (bottom-left origin, physical pixels)
		// to UI coordinates (top-left origin, physical pixels)
		float physicalY = u_viewportHeight - gl_FragCoord.y;

		// Discard outside the clip, half-open in physical pixels: a pixel centre on the
		// right or bottom edge is outside, so a clip w pixels wide keeps w columns
		if (gl_FragCoord.x < physicalClipBounds.x || gl_FragCoord.x >= physicalClipBounds.z ||
			physicalY < physicalClipBounds.y || physicalY >= physicalClipBounds.w) {
			discard;
		}
	}

	if (mode > -0.5) {
		// ========== SHAPE RENDERING (SDF) ==========
		// Data unpacking:
		// v_texCoord = rectLocalPos (SDF coordinates from rect center)
		// v_data1 = (borderColor.rgb, borderWidth)
		// v_data2 = (halfWidth, halfHeight, cornerRadius, mode): the border position,
		//   plus the border's transparency over kBorderTransparencySpan

		vec2 halfSize = v_data2.xy;
		int borderPos = int(mode + kBorderTransparencySpan);
		float borderAlpha = 1.0 - clamp((mode - float(borderPos)) / kBorderTransparencySpan, 0.0, 1.0);

		vec3 borderColor = v_data1.rgb;
		float borderWidth = v_data1.a;

		// The distance to the rect's edge, and a pixel's length in its units
		float dist = boxDist;
		float pixelSize = boxPixel;

		// Determine border region boundaries based on position mode
		float borderInner, borderOuter;
		if (borderPos == 0) {
			// Inside: border is drawn inside the shape
			borderOuter = 0.0;
			borderInner = -borderWidth;
		} else if (borderPos == 1) {
			// Center: border straddles the shape edge
			borderOuter = borderWidth * 0.5;
			borderInner = -borderWidth * 0.5;
		} else {
			// Outside: border is drawn outside the shape
			borderOuter = borderWidth;
			borderInner = 0.0;
		}

		// The fill covers dist <= 0 and the border its band, each pixel by coverage. A
		// zero-size box with no border is flat (triangles, a fill's feather ring, a flat
		// stroke piece): its own triangles are the shape, so coverage 1. Left to
		// smoothstep, triangles would ask for smoothstep(0, 0, 0), which GLSL leaves
		// undefined.
		float fillCover = 1.0;
		float bandCover = 0.0;	  // between the border's inner and outer edges
		float bandOverFill = 0.0; // the part of the band inside the shape's edge
		if (halfSize.x > 0.0 || halfSize.y > 0.0 || borderWidth > 0.001) {
			fillCover = coverage(0.0, dist, pixelSize);
		}
		if (borderWidth > 0.001) {
			float innerCover = coverage(borderInner, dist, pixelSize);
			bandCover = coverage(borderOuter, dist, pixelSize) - innerCover;
			bandOverFill = fillCover - innerCover;
		}

		// Early exit if completely transparent (optimization)
		if (fillCover + bandCover < 0.001) {
			discard;
		}

		// Premultiplied, the border over the fill where the band lies inside the
		// shape and beside it outside. The coverages share their edge ramps, so where
		// the fill and an outside border abut they add up to the pixel exactly.
		vec4 fill = vec4(v_color.rgb * v_color.a, v_color.a);
		vec4 border = vec4(borderColor * borderAlpha, borderAlpha);
		vec4 color = fill * (fillCover - borderAlpha * bandOverFill) + border * bandCover;
		FragColor = vec4(color.rgb, additive ? 0.0 : color.a);

	} else if (isMode(mode, kRenderModeShadow)) {
		// ========== BOX-SHADOW / GLOW (SDF soft falloff) ==========
		// v_texCoord = rectLocalPos (SDF coords from the shadow-shape center)
		// v_data1.x  = blur radius (px)
		// v_data2    = (halfWidth, halfHeight, cornerRadius, -3)
		// v_color    = shadow color (rgb + peak alpha)
		float blur = max(v_data1.x, 0.5);

		float dist = boxDist;
		// Approximate a CSS box-shadow's Gaussian blur: the falloff is centered on
		// the shape edge (dist 0 -> ~half alpha), reaching full `blur` px inside and
		// zero `blur` px outside. This keeps the edge soft rather than full-strength.
		float a = 1.0 - smoothstep(-blur, blur, dist);
		if (a < 0.001) {
			discard;
		}
		FragColor = premultiplied(v_color.rgb, v_color.a * a, additive);

	} else if (isMode(mode, kRenderModeArc)) {
		// ========== RING / ARC / PIE (SDF) ==========
		// v_texCoord = position from the centre, turned so the arc's middle lies along +x
		// v_data2    = (innerRadius, outerRadius, halfSweep, -4)
		// Folded across the x axis, the arc is every angle up to halfSweep; its ends
		// are straight radial cuts.
		vec2 p = vec2(v_texCoord.x, abs(v_texCoord.y));
		float r = length(p);
		// Outside the outer radius, or inside the inner one when there is a hole: a pie's
		// centre is not an edge.
		float dist = r - v_data2.y;
		if (v_data2.x > 0.0) {
			dist = max(dist, v_data2.x - r);
		}
		if (v_data2.z < kPi - 0.0001) {
			vec2 end = vec2(cos(v_data2.z), sin(v_data2.z));
			// Past the end ray is positive. Behind the centre the nearest point of the
			// ray is the centre itself.
			float side = end.x * p.y - end.y * p.x;
			dist = max(dist, dot(p, end) > 0.0 ? side : sign(side) * r);
		}
		// One pixel in arc-local units, from the coordinates themselves: the folds
		// above leave the distance's own derivatives unreliable.
		float pixelSize = sqrt(0.5 * (dot(texCoordDx, texCoordDx) + dot(texCoordDy, texCoordDy)));
		float a = 1.0 - smoothstep(-pixelSize, pixelSize, dist);
		if (a < 0.001) {
			discard;
		}
		FragColor = premultiplied(v_color.rgb, v_color.a * a, additive);

	} else if (isMode(mode, kRenderModeImage)) {
		// ========== IMAGE (plain sample) ==========
		// v_texCoord = texture coordinates; v_color = tint, straight alpha
		// Texels are premultiplied, so the tint multiplies in premultiplied too.
		vec4 texel = textureGrad(u_atlas, v_texCoord, texCoordDx, texCoordDy);
		vec4 color = texel * vec4(v_color.rgb * v_color.a, v_color.a);
		FragColor = vec4(color.rgb, additive ? 0.0 : color.a);

	} else {
		// ========== TEXT RENDERING (MSDF) ==========
		// Data unpacking:
		// v_texCoord = UV coordinates into MSDF atlas
		// v_color = text color RGBA
		// v_data1 = unused (0,0,0,0)
		// v_data2 = (pixelRange, 0, 0, -1.0)

		float pixelRange = v_data2.x;  // 4.0 from atlas generation

		// Sample the multi-channel distance field
		vec3 msd = textureGrad(u_atlas, v_texCoord, texCoordDx, texCoordDy).rgb;

		// Calculate signed distance using median-of-three
		float sd = median(msd.r, msd.g, msd.b);

		// Convert to screen-space distance
		float screenPxDistance = screenPxRange(pixelRange, abs(texCoordDx) + abs(texCoordDy)) * (sd - 0.5);

		// Anti-aliased opacity using smoothstep for proper anti-aliasing
		float opacity = smoothstep(-0.5, 0.5, screenPxDistance);

		// Output color with opacity
		FragColor = premultiplied(v_color.rgb, v_color.a * opacity, additive);
	}
}
