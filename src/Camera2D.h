// Camera2D.h —— 2D 正交相机（M1）
// 约定：世界坐标 Y 轴向下（屏幕上方 = y 负，下方 = y 正），符合 2D 游戏直觉。
// 相机位置表示"当前屏幕中心对应的世界坐标"。
// zoom > 1 放大（看得更近、可见区域更小），zoom < 1 缩小。
#pragma once

#include <glm/glm.hpp>

class Camera2D {
public:
    Camera2D(float viewportWidth, float viewportHeight);

    void resize(float width, float height);
    void setPosition(glm::vec2 worldPos);
    void move(glm::vec2 deltaWorld);
    glm::vec2 position() const { return m_position; }

    void setZoom(float z) { m_zoom = (z > 0.01f) ? z : 0.01f; }
    float zoom() const { return m_zoom; }

    // 返回 View * Projection 矩阵（列主序，可直接 glUniformMatrix4fv 传入）。
    // 这是"世界坐标 -> 裁剪空间"的变换，是 M1 要验证的核心。
    glm::mat4 viewProjection() const;

private:
    float m_vpWidth  = 800.0f;
    float m_vpHeight = 600.0f;
    glm::vec2 m_position{0.0f, 0.0f};
    float m_zoom = 1.0f;
};
