#version 300 es
// Triangle (GLES) — vértice: pos vec4 (xy usados) + cor vec4.
// Espelho do tests/shaders/triangle_vk.vert (paridade, missão §40).
// Nota ES 3.00: locations explícitos são permitidos em vertex INPUTS e
// fragment OUTPUT; inter-stage (out/in) vincula por NOME (3.10+ exigiria
// GL_EXT_separate_shader_objects).
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;
out vec4 vColor;
void main() {
    vColor = inColor;
    gl_Position = vec4(inPosition.xy, 0.0, 1.0);
}
