#version 450

// ---------------------------------------------------------------------------
// SDF text fragment shader
//
// The font atlas stores a signed distance field in the R channel (baked by
// stb_truetype with onedge_value=128, pixel_dist_scale=64). The edge of the
// glyph sits at R ≈ 0.502 (128/255). Pixels inside the glyph are > 0.502;
// outside are < 0.502.
//
// fwidth() gives the screen-space gradient magnitude of the distance field,
// providing adaptive anti-aliasing: sharp at large sizes, slightly blurred
// at small sizes where a pixel covers multiple texels. A minimum floor of
// 0.004 prevents hairline artifacts when the font exactly matches pixel size.
// ---------------------------------------------------------------------------

layout(set = 0, binding = 0) uniform sampler2D u_Atlas;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inTint;

layout(location = 0) out vec4 outColor;

// TODO: Implement #include system for shared shader code (e.g. SDF sampling) to avoid duplication between this and ui.frag, and potentially other future shaders that use SDF text rendering.
vec4 sampleSDF(sampler2D tex)
{
    // SDF text: R channel encodes signed distance; edge at 128/255 ≈ 0.502.
    float dist      = texture(tex, inUV).r;
    float smoothing = max(fwidth(dist) * 0.5, 0.004);
    float alpha     = smoothstep(0.5 - smoothing, 0.5 + smoothing, dist);
    return vec4(inTint.rgb, inTint.a * alpha);
}

void main()
{
    outColor = sampleSDF(u_Atlas);
}
