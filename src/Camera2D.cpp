// Camera2D.cpp —— 2D 正交相机实现（M1）
#include "Camera2D.h"
#include <glm/gtc/matrix_transform.hpp>

Camera2D::Camera2D(float viewportWidth, float viewportHeight)
    : m_vpWidth(viewportWidth), m_vpHeight(viewportHeight) {}

void Camera2D::resize(float width, float height) {
    m_vpWidth  = width;
    m_vpHeight = height;
}

void Camera2D::setPosition(glm::vec2 worldPos) {
    m_position = worldPos;
}

void Camera2D::move(glm::vec2 deltaWorld) {
    m_position += deltaWorld;
}

glm::mat4 Camera2D::viewProjection() const {
    // 受 zoom 影响的半视口（世界单位）。zoom 越大，半视口越小 -> 看得越近。
    const float halfW = (m_vpWidth  * 0.5f) / m_zoom;
    const float halfH = (m_vpHeight * 0.5f) / m_zoom;

    // 正交投影：左/右/下/上。这里传 bottom=+halfH, top=-halfH，
    // 等价于把 Y 轴翻转，使世界 Y 向下映射到屏幕 Y 向下。
    glm::mat4 proj = glm::ortho(-halfW, halfW, halfH, -halfH, -1.0f, 1.0f);

    // 视图矩阵：把相机位置平移到原点（相机看向 -z）。
    glm::mat4 view = glm::translate(glm::mat4(1.0f),
                                    glm::vec3(-m_position.x, -m_position.y, 0.0f));

    // 注意顺序：先视图（世界->相机），再投影（相机->裁剪）。
    return proj * view;
}
