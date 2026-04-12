#version 450

// Fullscreen quad using the "large triangle" trick — two triangles forming a
// screen-covering quad, positions computed from gl_VertexIndex (no VBO needed).
//
// Vertex layout (6 vertices, 2 triangles):
//   0: top-left     (-1, -1)   uv (0, 0)
//   1: top-right    ( 1, -1)   uv (1, 0)
//   2: bottom-left  (-1,  1)   uv (0, 1)
//   3: top-right    ( 1, -1)   uv (1, 0)
//   4: bottom-right ( 1,  1)   uv (1, 1)
//   5: bottom-left  (-1,  1)   uv (0, 1)

layout(location = 0) out vec2 outUV;

void main()
{
    const vec2 positions[6] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0,  1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0)
    );

    const vec2 uvs[6] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 1.0)
    );

    outUV       = uvs[gl_VertexIndex];
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
