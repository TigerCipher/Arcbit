#version 450

// ---------------------------------------------------------------------------
// Sprite batch fragment shader — Forward+ lighting with per-sprite tint
//
// Identical lighting model to forward.frag (Lambertian diffuse, quadratic
// falloff, normal-mapped surfaces) with the addition of a per-instance tint
// that multiplies the sampled albedo before lighting is applied.
// ---------------------------------------------------------------------------

// Must match ShadowResolution in RenderThread.h.
#define SHADOW_RESOLUTION 256

layout(set = 0, binding = 0) uniform sampler2D u_Albedo;
layout(set = 1, binding = 0) uniform sampler2D u_Normal;

struct PointLight {
    vec2  position;    // world-space center in pixels, matching Sprite::Position
    float radius;      // world-space radius in pixels — falls off to zero at this distance
    float intensity;   // multiplier applied to the color contribution
    vec4  color;       // rgb = light color; a = shadow SSBO row (-1 = no shadows)
};

layout(std430, set = 2, binding = 0) readonly buffer LightBuffer {
    PointLight lights[];
};

// One row per shadow-casting light: SHADOW_RESOLUTION floats storing the
// world-space distance to the nearest solid occluder at each polar angle.
layout(std430, set = 3, binding = 0) readonly buffer ShadowBuffer {
    float shadowData[];
};

// Push constant layout must match SpritePushConstants on the CPU (44 bytes, tightly packed).
// vec4 ambient is split into four floats to avoid the 16-byte alignment padding that vec4
// would insert after rotSin, which would shift ambient to offset 32 instead of 24.
layout(push_constant) uniform PC {
    vec2  camPos;
    vec2  viewportSize;
    float rotCos;
    float rotSin;
    float ambientR;
    float ambientG;
    float ambientB;
    float ambientA;
    uint  lightCount;
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec2 inWorldPos;
layout(location = 2) in vec4 inTint;

layout(location = 0) out vec4 outColor;

// ---------------------------------------------------------------------------
// Shadow lookup with 3-tap PCF
//
// Returns 1.0 when the fragment is lit, 0.0 when fully in shadow.
// Samples three adjacent polar-angle buckets and averages to soften edges.
// ---------------------------------------------------------------------------
float SampleShadow(int row, vec2 delta, float dist)
{
    if (row < 0) return 1.0;

    const float TWO_PI = 6.28318530718;
    float norm   = fract(atan(-delta.y, -delta.x) / TWO_PI + 1.0); // [0,1] — negate: delta points fragment→light, rays point light→fragment
    int   center = int(norm * float(SHADOW_RESOLUTION)) % SHADOW_RESOLUTION;
    int   prev   = (center - 1 + SHADOW_RESOLUTION) % SHADOW_RESOLUTION;
    int   next   = (center + 1) % SHADOW_RESOLUTION;

    int   base     = row * SHADOW_RESOLUTION;
    float occluder = (shadowData[base + prev] + shadowData[base + center] + shadowData[base + next]) / 3.0;

    return step(dist, occluder); // 1 if lit, 0 if behind occluder
}

void main()
{
    vec4 albedo = texture(u_Albedo, inUV);

    // Discard fully transparent pixels to avoid writing to the depth / stencil
    // and to let sprites with alpha cutout edges behave correctly.
    if (albedo.a < 0.01)
        discard;

    // Apply per-sprite tint (white tint = no change).
    albedo *= inTint;

    // Decode tangent-space normal from [0,1] texture range → [-1,1].
    vec3 N = normalize(texture(u_Normal, inUV).rgb * 2.0 - 1.0);

    // Accumulate lighting: start with ambient, then add each point light.
    vec3 lighting = vec3(pc.ambientR, pc.ambientG, pc.ambientB);

    for (uint i = 0u; i < pc.lightCount; ++i)
    {
        PointLight light = lights[i];

        vec2  delta = light.position - inWorldPos;
        float dist  = length(delta);

        if (dist >= light.radius)
            continue;

        // Smooth quadratic falloff: 1 at center, 0 at radius edge.
        float attenuation = 1.0 - (dist / light.radius);
        attenuation       = attenuation * attenuation;

        // Lambertian diffuse. Normalize the 2D delta before adding the Z component
        // so the elevation angle stays consistent regardless of world-space scale.
        // Without this, large pixel distances would make the light nearly horizontal,
        // collapsing NdotL to ~0 against a flat normal.
        vec2  dir2d = (dist > 0.001) ? (delta / dist) : vec2(0.0, 0.0);
        vec3  L     = normalize(vec3(dir2d, 1.0));
        float NdotL = max(dot(N, L), 0.0);

        float shadow = SampleShadow(int(light.color.a), delta, dist);

        lighting += light.color.rgb * light.intensity * attenuation * NdotL * shadow;
    }

    outColor = vec4(albedo.rgb * lighting, albedo.a);
}
