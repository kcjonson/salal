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

// The camera and viewport uniforms it reads are in the DrawUniforms block
// (uniforms.glsl), and the hook chain is in hooks.glsl; base.vert includes both first.

/// Transform a local mesh vertex to screen space using instance data, running the
/// hook chain (docs/shader-hooks.md) in mesh space before the instance transform and in
/// world space after it.
/// @param localPos Vertex position in local mesh space (usually centered around origin)
/// @return Screen-space position
vec2 instanceToScreen(vec2 localPos) {
    // Unpack instance data
    vec2 worldPos = a_instanceData1.xy;
    float rotation = a_instanceData1.z;
    float scale = a_instanceData1.w;

    vec2 lp = applyInstanceLocalHooks(localPos, worldPos);

    // Apply rotation around origin
    float cosR = cos(rotation);
    float sinR = sin(rotation);
    vec2 rotated = vec2(
        lp.x * cosR - lp.y * sinR,
        lp.x * sinR + lp.y * cosR
    );

    // Apply scale and translate to world position
    vec2 worldVertex = rotated * scale + worldPos;
    worldVertex = applyInstanceWorldHooks(worldVertex, localPos, worldPos);

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
