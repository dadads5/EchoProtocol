// TextRenderer.cpp
#include "TextRenderer.h"

#include <algorithm>

#include "BitmapFont.h"
#include "RectRenderer.h"
#include "Shader.h"

namespace {

constexpr int kAdvance = echo::kGlyphW + 1; // 字形宽 + 1 列字间距
constexpr int kLineGap = 3;                 // 行间距（以"字体像素"为单位）
constexpr int kLineStep = echo::kGlyphH + kLineGap;

} // namespace

TextRenderer::TextRenderer(RectRenderer& rects, Shader& shader)
    : m_rects(rects), m_shader(shader) {}

float TextRenderer::textWidth(const std::string& text, float px) {
    int widest = 0;
    int cur    = 0;
    for (char c : text) {
        if (c == '\n') {
            widest = std::max(widest, cur);
            cur    = 0;
            continue;
        }
        ++cur;
    }
    widest = std::max(widest, cur);
    if (widest <= 0) return 0.0f;
    // n 个字占 n*kAdvance 列，最后一列的字间距不算入视觉宽度
    return static_cast<float>(widest * kAdvance - 1) * px;
}

float TextRenderer::textHeight(const std::string& text, float px) {
    int lines = 1;
    for (char c : text)
        if (c == '\n') ++lines;
    return static_cast<float>(lines * kLineStep - kLineGap) * px;
}

void TextRenderer::draw(const glm::mat4& vp, const std::string& text,
                        const glm::vec2& topLeft, float px,
                        const glm::vec4& color) const {
    const glm::vec2 pixelSize(px, px);
    float           cx = topLeft.x;
    float           cy = topLeft.y;

    for (char raw : text) {
        if (raw == '\n') {
            cx = topLeft.x;
            cy += kLineStep * px;
            continue;
        }

        char c = raw;
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A'); // 小写转大写

        int gi = echo::charIndex(c);
        if (gi < 0) gi = echo::charIndex(' '); // 未知字符当空格

        const std::uint8_t* rows = echo::kGlyphData[gi];
        for (int r = 0; r < echo::kGlyphH; ++r) {
            const std::uint8_t bits = rows[r];
            if (bits == 0) continue; // 整行空，省掉 5 次绘制
            for (int col = 0; col < echo::kGlyphW; ++col) {
                if (bits & (1u << (echo::kGlyphW - 1 - col))) {
                    m_rects.draw(m_shader, vp,
                                 glm::vec2(cx + (col + 0.5f) * px,
                                           cy + (r + 0.5f) * px),
                                 pixelSize, color);
                }
            }
        }
        cx += kAdvance * px;
    }
}
