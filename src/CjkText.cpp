// CjkText.cpp —— 中文 / UTF-8 文字渲染实现（M3 补强）
//
// 渲染管线（与 RectRenderer 平行，但用纹理而不是纯色块）：
//   ① GDI 离屏 DC + CreateDIBSection 拿到一块 BGRA 位图
//   ② 白底 + 黑字（抗锯齿灰阶）画上去，再用"亮度反推 alpha"得到遮罩
//      —— 白底亮度 255 → alpha 0（透明），黑字亮度 0 → alpha 255（不透明）
//   ③ 上传成 GL 纹理（白 RGB + 覆盖度 A），绘制时按 uColor 染色
//   ④ 自带纹理着色器 + 单位四边形 VAO，draw() 时 bind 纹理、画一个带纹理的矩形
//
// 同 (utf8, fontPx) 只渲染一次，之后从 m_cache 取，避免每帧重建纹理。
// 必须在任何可能间接包含 <windows.h> 的头之前定义，避免 min/max 宏污染 glm/STL
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <CjkText.h>

#include <windows.h>

#include <cstring>

// ---- 纹理着色器（带 sampler2D，按 uColor 染色）----
// 源码已抽到 assets/shaders/text.vert / text.frag，运行时由 Shader::fromFiles 加载；
// 兜底源码见 Shader.cpp 的 kDefaultTexVertexSrc / kDefaultTexFragmentSrc。

namespace {
// 单位正方形顶点（中心在原点，边长 1），与 RectRenderer 一致
const float kQuad[8] = {
    -0.5f, -0.5f,
     0.5f, -0.5f,
     0.5f,  0.5f,
    -0.5f,  0.5f,
};
const GLuint kIndices[6] = { 0, 1, 2, 0, 2, 3 };
} // namespace

CjkText::CjkText(const std::string& vertexPath, const std::string& fragmentPath)
    : m_shader(Shader::fromFiles(vertexPath, fragmentPath,
                                Shader::kDefaultTexVertexSrc,
                                Shader::kDefaultTexFragmentSrc)) {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

CjkText::~CjkText() {
    if (m_vao)   glDeleteVertexArrays(1, &m_vao);
    if (m_vbo)   glDeleteBuffers(1, &m_vbo);
    if (m_ebo)   glDeleteBuffers(1, &m_ebo);
    for (auto& kv : m_cache)
        if (kv.second.tex) glDeleteTextures(1, &kv.second.tex);
}

CjkText::Entry& CjkText::getOrRender(const std::string& utf8, int fontPx) const {
    const std::string key = utf8 + "|" + std::to_string(fontPx);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    Entry e;
    e.tex = renderMask(utf8, fontPx, &e.w, &e.h);
    return m_cache.emplace(key, e).first->second;
}

void CjkText::measure(const std::string& utf8, int fontPx, float& w, float& h) const {
    const Entry& e = getOrRender(utf8, fontPx);
    w = static_cast<float>(e.w);
    h = static_cast<float>(e.h);
}

void CjkText::draw(const glm::mat4& screenVP,
                   const std::string& utf8,
                   int fontPx,
                   const glm::vec2& topLeft,
                   const glm::vec4& color) const {
    const Entry& e = getOrRender(utf8, fontPx);
    if (e.tex == 0) return;

    m_shader.use();
    m_shader.setMat4("uVP", &screenVP[0][0]);
    m_shader.setVec2("uCenter", topLeft.x + e.w * 0.5f, topLeft.y + e.h * 0.5f);
    m_shader.setVec2("uSize", static_cast<float>(e.w), static_cast<float>(e.h));
    m_shader.setVec4("uColor", color.r, color.g, color.b, color.a);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, e.tex);
    glUniform1i(glGetUniformLocation(m_shader.handle(), "uTex"), 0);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

GLuint CjkText::renderMask(const std::string& utf8, int fontPx,
                           unsigned* outW, unsigned* outH) const {
    // UTF-8 -> UTF-16（中文必经这步）
    int need = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring w;
    if (need > 0) {
        w.resize(static_cast<std::size_t>(need));
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], need);
    }
    if (!w.empty() && w.back() == L'\0') w.pop_back();

    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc) return 0;

    LOGFONTW lf{};
    lf.lfHeight   = -fontPx;                 // 负高度 = 字号像素
    lf.lfWeight   = FW_NORMAL;
    lf.lfQuality  = ANTIALIASED_QUALITY;      // 灰阶抗锯齿，避免 ClearType 彩边
    lf.lfCharSet  = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Microsoft YaHei");
    HFONT hf  = CreateFontIndirectW(&lf);
    HFONT oldF = static_cast<HFONT>(SelectObject(hdc, hf));

    // 先量尺寸
    RECT r{ 0, 0, 0, 0 };
    DrawTextW(hdc, w.c_str(), -1, &r, DT_LEFT | DT_TOP | DT_CALCRECT);
    int tw = r.right, th = r.bottom;
    if (tw <= 0) tw = 1;
    if (th <= 0) th = 1;
    const int pad = 4;
    const int W = tw + pad * 2;
    const int H = th + pad * 2;

    // 32 位 DIB（负高度 = 自上而下，首行即顶部，贴合 GL 纹理坐标）
    BITMAPINFOHEADER bi{};
    bi.biSize        = sizeof(BITMAPINFOHEADER);
    bi.biWidth       = W;
    bi.biHeight      = -H;
    bi.biPlanes      = 1;
    bi.biBitCount    = 32;
    bi.biCompression = BI_RGB;
    RGBQUAD* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi),
                                    DIB_RGB_COLORS, reinterpret_cast<void**>(&bits),
                                    nullptr, 0);
    HBITMAP oldB = static_cast<HBITMAP>(SelectObject(hdc, hbmp));

    // 白底
    std::memset(bits, 0xFF, static_cast<std::size_t>(W) * H * 4);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));         // 黑字
    RECT dr{ pad, pad, pad + tw, pad + th };
    DrawTextW(hdc, w.c_str(), -1, &dr, DT_LEFT | DT_TOP | DT_NOCLIP);

    // 亮度反推 alpha：灰阶下 R==G==B
    for (int i = 0; i < W * H; ++i) {
        const BYTE lum = bits[i].rgbRed;     // 白底 255，黑字 0
        bits[i].rgbRed = 255;
        bits[i].rgbGreen = 255;
        bits[i].rgbBlue = 255;
        bits[i].rgbReserved = static_cast<BYTE>(255 - lum);
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, bits);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    SelectObject(hdc, oldB); DeleteObject(hbmp);
    SelectObject(hdc, oldF); DeleteObject(hf);
    DeleteDC(hdc);

    *outW = static_cast<unsigned>(W);
    *outH = static_cast<unsigned>(H);
    return tex;
}
