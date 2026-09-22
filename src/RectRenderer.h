// RectRenderer.h —— 轴对齐矩形渲染（M1）
// 用一个单位正方形（-0.5..0.5）的顶点缓冲，配合 shader 的
// uCenter / uSize / uColor 三个 uniform，每次 draw 画出不同位置/大小/颜色的矩形。
// 顶点在着色器里做：world = aPos * uSize + uCenter，再乘 uVP。
#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

class Shader; // 仅引用，实现文件里再 include 完整定义

class RectRenderer {
public:
    RectRenderer();
    ~RectRenderer();

    RectRenderer(const RectRenderer&) = delete;
    RectRenderer& operator=(const RectRenderer&) = delete;

    // center / size 为世界坐标；color 为 RGBA（分量 0..1）
    void draw(const Shader& shader,
              const glm::mat4& viewProjection,
              const glm::vec2& center,
              const glm::vec2& size,
              const glm::vec4& color) const;

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
};
