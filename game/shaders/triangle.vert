#version 450

// Hardcoded triangle in NDC space — no vertex buffer needed.
// gl_VertexIndex is the built-in vertex index (0, 1, 2 from Draw(3)).
const vec2 positions[3] = vec2[](
    vec2( 0.0, -0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5,  0.5)
);

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
