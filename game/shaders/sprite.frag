#version 450

// ---------------------------------------------------------------------------
// Sprite batch fragment shader — Forward+ lighting with per-sprite tint
//
// Identical lighting model to forward.frag (Lambertian diffuse, quadratic
// falloff, normal-mapped surfaces) with the addition of a per-instance tint
// that multiplies the sampled albedo before lighting is applied.
// ---------------------------------------------------------------------------

layout(set = 0, binding = 0) uniform sampler2D u_Albedo;
layout(set = 1, binding = 0) uniform sampler2D u_Normal;

struct PointLight {
    vec2  position;    // world-space center in pixels, matching Sprite::Position
    float radius;      // world-space radius in pixels — falls off to zero at this distance
    float intensity;   // multiplier applied to the color contribution
    vec4  color;       // rgba light color (.a reserved)
};

layout(std430, set = 2, binding = 0) readonly buffer LightBuffer {
    PointLight lights[];
};

layout(push_constant) uniform PC {
    vec2  camPos;
    vec2  viewportSize;
    float rotCos;
    float rotSin;
    vec4  ambient;
    uint  lightCount;
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec2 inWorldPos;
layout(location = 2) in vec4 inTint;

layout(location = 0) out vec4 outColor;

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
    vec3 lighting = pc.ambient.rgb;

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

        lighting += light.color.rgb * light.intensity * attenuation * NdotL;
    }

    outColor = vec4(albedo.rgb * lighting, albedo.a);
}
