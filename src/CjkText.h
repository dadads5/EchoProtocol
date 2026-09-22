// CjkText.h —— 中文 / UTF-8 文字渲染（M3 补强：暂停菜单中文化）
//
// 为什么不用 SDL_ttf：项目刻意零额外依赖、不附带 .ttf 资源（见 TextRenderer.h）。
// 这里改用 Windows GDI 把一段 UTF-8 文字（可含中文）光栅化成一张 RGBA 纹理，
// 再用自带的小纹理着色器画出来。零新 vcpkg 依赖，直接吃系统自带中文字体
// （默认微软雅黑 / Microsoft YaHei），也不需要打包字体文件。
//
// 纹理里只存"白色文字 + alpha 遮罩"（RGB 恒为白，A = 笔画覆盖度），
// 绘制时再用 uColor 统一染色 —— 这样同一段文字可随选中态换色，且纹理可缓存复用。
#pragma once

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

#include "Shader.h"

class CjkText {
public:
    // vertexPath / fragmentPath：纹理着色器资源文件（assets/shaders/text.*）。
    // 文件缺失时 Shader 内部回退到兜底源码。
    CjkText(const std::string& vertexPath, const std::string& fragmentPath);
    ~CjkText();

    CjkText(const CjkText&) = delete;
    CjkText& operator=(const CjkText&) = delete;

    // 用屏幕像素空间正交矩阵把一段中文文字画出来。
    // screenVP ：glm::ortho(0, w, h, 0, ...)（原点左上、y 向下）
    // utf8     ：UTF-8 文本（可含中文）
    // fontPx   ：字号（像素），纹理按 1:1 显示，最清晰
    // topLeft  ：文字左上角（屏幕像素坐标）
    // color    ：文字颜色（RGBA，0~1）
    void draw(const glm::mat4& screenVP,
              const std::string& utf8,
              int fontPx,
              const glm::vec2& topLeft,
              const glm::vec4& color) const;

    // 仅测量文字像素尺寸（用于居中排版），不改任何 GL 状态。
    void measure(const std::string& utf8, int fontPx, float& w, float& h) const;

private:
    struct Entry {
        GLuint   tex = 0;
        unsigned w   = 0;
        unsigned h   = 0;
    };

    // 取缓存；没有就现场用 GDI 渲染一张。
    Entry& getOrRender(const std::string& utf8, int fontPx) const;

    // GDI 光栅化：白字 + alpha 遮罩，返回纹理 id（0 表示失败）。
    GLuint renderMask(const std::string& utf8, int fontPx,
                      unsigned* outW, unsigned* outH) const;

    Shader                              m_shader;                       // 纹理着色器
    GLuint                              m_vao = 0, m_vbo = 0, m_ebo = 0;
    mutable std::unordered_map<std::string, Entry> m_cache;              // key: utf8 + '|' + fontPx
};
