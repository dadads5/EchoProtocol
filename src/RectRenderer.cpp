// RectRenderer.cpp —— 轴对齐矩形渲染实现（M1）
#include "RectRenderer.h"
#include "Shader.h"

// 单位正方形顶点（中心在原点，边长 1）
static const float kQuad[8] = {
    -0.5f, -0.5f,
     0.5f, -0.5f,
     0.5f,  0.5f,
    -0.5f,  0.5f,
};

// 两个三角形组成矩形（注意背面剔除未开启，顺时针/逆时针都可见）
static const GLuint kIndices[6] = { 0, 1, 2, 0, 2, 3 };

RectRenderer::RectRenderer() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices, GL_STATIC_DRAW);

    // location = 0 对应着色器里的 layout(location = 0) in vec2 aPos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindVertexArray(0); // 解绑后 VAO 已记录 VBO/EBO 状态
}

RectRenderer::~RectRenderer() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
}

void RectRenderer::draw(const Shader& shader,
                        const glm::mat4& viewProjection,
                        const glm::vec2& center,
                        const glm::vec2& size,
                        const glm::vec4& color) const {
    shader.use();
    shader.setMat4("uVP", &viewProjection[0][0]);
    shader.setVec2("uCenter", center.x, center.y);
    shader.setVec2("uSize", size.x, size.y);
    shader.setVec4("uColor", color.r, color.g, color.b, color.a);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
