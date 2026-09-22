// TextRenderer.h —— 点阵文字渲染（M2.1）
//
// 基于内置 5x7 点阵字体（BitmapFont.h）：把字符串拆成一个个"亮像素"，
// 每个亮像素用 RectRenderer 画一个小方块。零外部依赖。
//
// 为什么不引 SDL_ttf：
//   ① 要多一个 vcpkg 依赖，还得随包附带 .ttf 资源，构建/分发都更重
//   ② 像素风游戏用点阵字体本来就更搭，也能直接当 M3 的 UI 基础设施
// 代价：目前只支持大写字母 / 数字 / 少量符号（见 kGlyphOrder），小写会自动转大写。
#pragma once

#include <string>

#include <glm/glm.hpp>

class Shader;
class RectRenderer;

class TextRenderer {
public:
    TextRenderer(RectRenderer& rects, Shader& shader);

    // topLeft：文字左上角（坐标系取决于传入的 vp，可世界可屏幕）
    // px     ：单个"字体像素"的边长（即一个字占 5*px 宽、7*px 高）
    void draw(const glm::mat4& viewProjection,
              const std::string& text,
              const glm::vec2& topLeft,
              float px,
              const glm::vec4& color) const;

    // 尺寸估算，用于居中 / 排版。含 '\n' 时按最长行算宽。
    static float textWidth(const std::string& text, float px);
    static float textHeight(const std::string& text, float px);

private:
    RectRenderer& m_rects;
    Shader&       m_shader;
};
