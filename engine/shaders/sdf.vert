#version 450

// ---------------------------------------------------------------------------
// SDF text vertex shader — screen-space quads
//
// Identical instance layout to sprite.vert (4 × vec4) so both pipelines
// share the same SpriteInstance struct. a_Rot is not used for text; it is
// bound but ignored (GPU discards unused instance attributes without cost).
//
// Coordinate system
// -----------------
// a_PosSize.xy is the quad center in screen pixels — (0,0) = top-left,
// (viewportSize.x, viewportSize.y) = bottom-right of the window.
// This makes positioning debug overlays and HUD elements straightforward:
// pass screen-pixel coordinates directly and let the shader do the NDC math.
// ---------------------------------------------------------------------------

layout(location = 0) in vec4 a_PosSize; // (centerX, centerY, halfW, halfH) in screen pixels
layout(location = 1) in vec4 a_UV;      // (u0, v0, u1, v1)
layout(location = 2) in vec4 a_Tint;
layout(location = 3) in vec4 a_Rot;     // unused

layout(push_constant) uniform PC {
    vec2 viewportSize; // actual framebuffer dimensions in pixels
} pc;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outTint;

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

    // Screen pixels → NDC: (0,0) top-left maps to (-1,-1), (W,H) to (1,1).
    vec2 ndc = (screenPos / pc.viewportSize) * 2.0 - 1.0;

    outUV   = mix(a_UV.xy, a_UV.zw, corner * 0.5 + 0.5);
    outTint = a_Tint;

    gl_Position = vec4(ndc, 0.0, 1.0);
}
