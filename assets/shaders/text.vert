#version 330 core
layout(location = 0) in vec2 aPos;
uniform mat4 uVP;
uniform vec2 uCenter;
uniform vec2 uSize;
out vec2 vUV;
void main() {
    vUV = aPos + 0.5;                 // 0..1，原点左下
    vec2 world = aPos * uSize + uCenter;
    gl_Position = uVP * vec4(world, 0.0, 1.0);
}
