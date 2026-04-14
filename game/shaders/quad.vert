#version 450

// Per-draw transform and UV sub-region, passed via push constants.
// Must match the pcData layout in RenderThread.cpp exactly.
layout(push_constant) uniform PC {
    vec2 position; // NDC center of the quad
    vec2 scale;    // NDC half-size (1,1 = fullscreen)
    vec2 uvMin;    // top-left UV of the atlas sub-region
    vec2 uvMax;    // bottom-right UV of the atlas sub-region
} pc;

layout(location = 0) out vec2 outUV;

void main()
{
    // Unit quad vertices in [-1, 1] — scaled and translated by push constants.
    const vec2 positions[6] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0,  1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0)
    );

    const vec2 baseUVs[6] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 1.0)
    );

    // mix(a, b, t) maps [0,1] → [uvMin, uvMax] for atlas sub-region sampling.
    outUV       = mix(pc.uvMin, pc.uvMax, baseUVs[gl_VertexIndex]);
    gl_Position = vec4(positions[gl_VertexIndex] * pc.scale + pc.position, 0.0, 1.0);
}
