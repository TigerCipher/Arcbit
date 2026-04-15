#version 450

// Push constants — must match ForwardPushConstants in RenderThread.cpp exactly.
layout(push_constant) uniform PC {
    vec2  position;    // NDC center of the quad
    vec2  scale;       // NDC half-size (1,1 = fullscreen)
    vec4  uv;          // (u0, v0, u1, v1) atlas sub-region in normalized UV space
    vec4  ambient;     // rgb ambient color + unused alpha
    uint  lightCount;  // number of active lights in the SSBO
} pc;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec2 outNDC; // fragment position in NDC, for light distance math

void main()
{
    // Two-triangle quad; vertices derived from the vertex index, no vertex buffer needed.
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

    vec2 localPos = positions[gl_VertexIndex];
    vec2 worldPos = localPos * pc.scale + pc.position;

    // mix(a, b, t) maps [0,1] range → atlas sub-region [uv.xy, uv.zw].
    outUV       = mix(pc.uv.xy, pc.uv.zw, baseUVs[gl_VertexIndex]);
    outNDC      = worldPos;
    gl_Position = vec4(worldPos, 0.0, 1.0);
}
