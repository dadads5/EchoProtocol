#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    float a = texture(uTex, vUV).a;  // 纹理只存遮罩 alpha
    fragColor = vec4(uColor.rgb, uColor.a * a);
}
