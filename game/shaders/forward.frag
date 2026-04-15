#version 450

// ---------------------------------------------------------------------------
// Forward+ fragment shader — lit sprite rendering
//
// Samples albedo and normal from textures, then loops over all active point
// lights in the SSBO to accumulate per-pixel lighting.
// ---------------------------------------------------------------------------

layout(set = 0, binding = 0) uniform sampler2D u_Albedo;
layout(set = 1, binding = 0) uniform sampler2D u_Normal;

struct PointLight {
    vec2  position;    // NDC light center
    float radius;      // NDC radius — light falls off to zero at this distance
    float intensity;   // multiplier applied to the color contribution
    vec4  color;       // rgba light color (.a reserved for future use)
};

layout(std430, set = 2, binding = 0) readonly buffer LightBuffer {
    PointLight lights[];
};

layout(push_constant) uniform PC {
    vec2  position;
    vec2  scale;
    vec4  uv;
    vec4  ambient;
    uint  lightCount;
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec2 inNDC;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 albedo = texture(u_Albedo, inUV);

    // Discard fully transparent pixels so they don't write to the swapchain.
    if (albedo.a < 0.01)
        discard;

    // Decode normal from [0,1] texture space → [-1,1] tangent space.
    vec3 N = normalize(texture(u_Normal, inUV).rgb * 2.0 - 1.0);

    // Accumulate light contributions.
    vec3 lighting = pc.ambient.rgb;

    for (uint i = 0u; i < pc.lightCount; ++i)
    {
        PointLight light = lights[i];

        vec2  delta    = light.position - inNDC;
        float dist     = length(delta);

        if (dist >= light.radius)
            continue;

        // Smooth quadratic falloff: 1 at center, 0 at radius.
        float attenuation = 1.0 - (dist / light.radius);
        attenuation       = attenuation * attenuation;

        // Lambertian diffuse using the decoded surface normal.
        // Light direction in 2D points "towards the viewer" (z = 1).
        vec3 L       = normalize(vec3(delta, 1.0));
        float NdotL  = max(dot(N, L), 0.0);

        lighting += light.color.rgb * light.intensity * attenuation * NdotL;
    }

    outColor = vec4(albedo.rgb * lighting, albedo.a);
}
