#version 330 core
layout(location = 0) in vec2 aPos;
uniform mat4 uVP;
uniform vec2 uCenter;
uniform vec2 uSize;
void main() {
    vec2 world = aPos * uSize + uCenter;
    gl_Position = uVP * vec4(world, 0.0, 1.0);
}
