#version 450

// ---------------------------------------------------------------------------
// UI vertex shader — screen-space quads for all UI widgets.
//
// Shares the SpriteInstance layout (4 × vec4) with sprite.vert and sdf.vert.
// a_Rot.z carries a rendering mode flag set by the UI system:
//   0.0 = regular texture × tint  (panels, images, progress bars)
//   1.0 = SDF text rendering       (labels, button text)
// ---------------------------------------------------------------------------

layout(location = 0) in vec4 a_PosSize; // (centerX, centerY, halfW, halfH) screen pixels
layout(location = 1) in vec4 a_UV;      // (u0, v0, u1, v1)
layout(location = 2) in vec4 a_Tint;
layout(location = 3) in vec4 a_Rot;     // (_, _, mode, _)

layout(push_constant) uniform PC {
    vec2 viewportSize;
} pc;

layout(location = 0) out vec2  outUV;
layout(location = 1) out vec4  outTint;
layout(location = 2) out float outMode;

void main()
{
    const vec2 corners[6] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0)
    );

    vec2 corner    = corners[gl_VertexIndex % 6];
    vec2 screenPos = a_PosSize.xy + corner * a_PosSize.zw;
    vec2 ndc       = (screenPos / pc.viewportSize) * 2.0 - 1.0;

    outUV   = mix(a_UV.xy, a_UV.zw, corner * 0.5 + 0.5);
    outTint = a_Tint;
    outMode = a_Rot.z;

    gl_Position = vec4(ndc, 0.0, 1.0);
}
