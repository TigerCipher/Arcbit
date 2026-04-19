#version 450

// ---------------------------------------------------------------------------
// UI fragment shader — handles both regular textures and SDF text in one pass.
//
// mode == 0: sample texture × tint  (panels, images, progress bar fills)
// mode == 1: SDF distance field      (labels, button text via stb_truetype)
//
// Using a single pipeline for all UI lets the layer sort control draw order
// for both backgrounds and text without requiring separate render passes.
// ---------------------------------------------------------------------------

layout(set = 0, binding = 0) uniform sampler2D u_Texture;

layout(location = 0) in vec2  inUV;
layout(location = 1) in vec4  inTint;
layout(location = 2) in float inMode;

layout(location = 0) out vec4 outColor;

void main()
{
    if (inMode > 0.5) {
        // SDF text: R channel encodes signed distance; edge at 128/255 ≈ 0.502.
        float dist      = texture(u_Texture, inUV).r;
        float smoothing = max(fwidth(dist) * 0.5, 0.004);
        float alpha     = smoothstep(0.5 - smoothing, 0.5 + smoothing, dist);
        outColor        = vec4(inTint.rgb, inTint.a * alpha);
    } else {
        // Regular texture: sample and multiply by tint.
        // Solid-color quads bind a 1×1 white texture and set tint to the desired color.
        outColor = texture(u_Texture, inUV) * inTint;
    }
}
