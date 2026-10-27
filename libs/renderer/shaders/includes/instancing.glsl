// instancing.glsl - GPU Instancing support for the base shader
//
// Provides per-instance data attributes and world-to-screen transformation
// for efficient rendering of many instances of the same mesh.

// Instance data attributes (per-instance, divisor = 1)
// Layout:
//   a_instanceData1.xy = world position
//   a_instanceData1.z  = rotation (radians)
//   a_instanceData1.w  = scale
//   a_instanceData2    = color tint (RGBA)
layout(location = 6) in vec4 a_instanceData1;
layout(location = 7) in vec4 a_instanceData2;

// The camera, viewport and groundcover uniforms it reads are in the DrawUniforms
// block (uniforms.glsl), which base.vert includes first.

/// Transform a local mesh vertex to screen space using instance data.
/// @param localPos Vertex position in local mesh space (usually centered around origin)
/// @return Screen-space position
vec2 instanceToScreen(vec2 localPos) {
    // Unpack instance data
    vec2 worldPos = a_instanceData1.xy;
    float rotation = a_instanceData1.z;
    float scale = a_instanceData1.w;

    vec2  lp = localPos;
    float reachFrac = 0.0; // 0 at the clump base, 1 at the outer tips
    float infl = 0.0;      // cursor influence, 0 far → 1 at center

    if (u_groundcoverMode == 1) {
        reachFrac = clamp(length(localPos) / max(u_groundcoverReach, 0.0001), 0.0, 1.0);
        float dC = distance(worldPos, u_cursorWorld);
        infl = 1.0 - smoothstep(0.0, max(u_cursorRadius, 0.0001), dC);
        // Compact at rest, full size where the cursor reveals it.
        float openness = mix(u_groundcoverOpenness, 1.0, infl);
        lp *= openness;
    }

    // Apply rotation around origin
    float cosR = cos(rotation);
    float sinR = sin(rotation);
    vec2 rotated = vec2(
        lp.x * cosR - lp.y * sinR,
        lp.x * sinR + lp.y * cosR
    );

    // Apply scale and translate to world position
    vec2 worldVertex = rotated * scale + worldPos;

    if (u_groundcoverMode == 1 && infl > 0.0) {
        // Bend away from the cursor: outer tips move most, the rooted base not at all.
        vec2  away = worldVertex - u_cursorWorld;
        float d = length(away);
        vec2  pushDir = d > 0.0001 ? away / d : vec2(0.0, -1.0);
        worldVertex += pushDir * (infl * reachFrac * u_cursorStrength);
    }

    // Transform world → screen
    // Screen center is viewport center, camera position is world center
    float viewScale = u_pixelsPerMeter * u_cameraZoom;
    vec2 screenPos = (worldVertex - u_cameraPosition) * viewScale + u_viewportSize * 0.5;

    return screenPos;
}

/// Get the instance color tint
vec4 getInstanceColorTint() {
    return a_instanceData2;
}

/// Transform world-space vertex to screen space.
/// Used by baked mesh path (u_instanced == 2) where vertices are pre-transformed.
/// @param worldPos Vertex position in world space (meters)
/// @return Screen-space position
vec2 worldToScreen(vec2 worldPos) {
    float viewScale = u_pixelsPerMeter * u_cameraZoom;
    return (worldPos - u_cameraPosition) * viewScale + u_viewportSize * 0.5;
}
