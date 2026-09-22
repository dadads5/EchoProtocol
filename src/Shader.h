// Shader.h —— 着色器程序封装（M1 落地，M3.4 改为资源文件加载）
// 极简封装：从 .vert/.frag 资源文件加载并编译顶点/片段着色器、链接 program，
// 提供 setMat4 / setVec2 / setVec4 设置 uniform。
//
// M3.4 改动：原先内嵌在 main.cpp/CjkText.cpp 的着色器字符串，已抽到
// assets/shaders/*.vert|*.frag，运行时读盘加载。文件缺失时回退到本类内
// kDefault* 兜底源码（与资源文件内容一致），保证即便资源未随 exe 拷贝，
// 引擎也不会静默黑屏。
#pragma once

#include <glad/glad.h>
#include <string>

class Shader {
public:
    // 从字符串源码编译（保留，供兜底与 fromFiles 内部使用）
    Shader(const char* vertexSrc, const char* fragmentSrc);

    // 从 .vert/.frag 资源文件加载；读取失败时回退到 fallback* 兜底源码
    static Shader fromFiles(const std::string& vertexPath,
                            const std::string& fragmentPath,
                            const char* fallbackVertex = kDefaultVertexSrc,
                            const char* fallbackFragment = kDefaultFragmentSrc);

    ~Shader();

    // 可移动（fromFiles 按值返回），不可拷贝（GPU 对象唯一所有权）
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    void use() const;
    void setMat4(const char* name, const float* data) const;
    void setVec2(const char* name, float x, float y) const;
    void setVec4(const char* name, float r, float g, float b, float a) const;

    GLuint handle() const { return m_program; }

    // 兜底源码（与 assets/shaders/*.vert|*.frag 内容保持一致）
    static const char* kDefaultVertexSrc;    // 矩形/纯色
    static const char* kDefaultFragmentSrc;  // 矩形/纯色
    static const char* kDefaultTexVertexSrc;  // 中文纹理
    static const char* kDefaultTexFragmentSrc;// 中文纹理

private:
    static std::string readTextFile(const std::string& path);
    GLuint m_program = 0;
};
