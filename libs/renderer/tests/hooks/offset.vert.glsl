// offset.vert.glsl - the renderer tests' hook (ShaderHooks.test.cpp): it moves every
// instanced vertex by `local` in mesh space, before the instance's scale and rotation
// apply, and by `world` after the instance transform. The block starts zeroed, which
// moves nothing.

layout(std140) uniform OffsetUniforms {
	vec2 local; // 0
	vec2 world; // 8
} offset;

vec2 offset_instanceLocal(vec2 position, vec2 instancePosition) {
	return position + offset.local;
}

vec2 offset_instanceWorld(vec2 position, vec2 localPosition, vec2 instancePosition) {
	return position + offset.world;
}
