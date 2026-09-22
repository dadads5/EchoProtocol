// Shader.cpp —— 着色器程序实现（M1 落地，M3.4 改为资源文件加载）
#include "Shader.h"
#include "game/Utf8Path.h"
#include <iostream>
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
// 兜底源码：与 assets/shaders/*.vert|*.frag 内容保持一致。
// 仅当资源文件读取失败时才启用，保证引擎永不静默黑屏。
// ---------------------------------------------------------------------------
const char* Shader::kDefaultVertexSrc = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
uniform mat4 uVP;
uniform vec2 uCenter;
uniform vec2 uSize;
void main() {
    vec2 world = aPos * uSize + uCenter;
    gl_Position = uVP * vec4(world, 0.0, 1.0);
}
)GLSL";

const char* Shader::kDefaultFragmentSrc = R"GLSL(
#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = uColor;
}
)GLSL";

const char* Shader::kDefaultTexVertexSrc = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
uniform mat4 uVP;
uniform vec2 uCenter;
uniform vec2 uSize;
out vec2 vUV;
void main() {
    vUV = aPos + 0.5;
    vec2 world = aPos * uSize + uCenter;
    gl_Position = uVP * vec4(world, 0.0, 1.0);
}
)GLSL";

const char* Shader::kDefaultTexFragmentSrc = R"GLSL(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    float a = texture(uTex, vUV).a;
    fragColor = vec4(uColor.rgb, uColor.a * a);
}
)GLSL";

// ---------------------------------------------------------------------------
static GLuint compileShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0');
        glGetShaderInfoLog(sh, len, nullptr, log.data());
        std::cerr << "Shader compile error ("
                  << (type == GL_VERTEX_SHADER ? "vertex" : "fragment")
                  << "):\n" << log << std::endl;
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

std::string Shader::readTextFile(const std::string& path) {
    std::ifstream f(echo::u8path(path), std::ios::binary);
    if (!f) return std::string();
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

Shader Shader::fromFiles(const std::string& vertexPath,
                         const std::string& fragmentPath,
                         const char* fallbackVertex,
                         const char* fallbackFragment) {
    std::string vs = readTextFile(vertexPath);
    std::string fs = readTextFile(fragmentPath);
    if (vs.empty()) {
        std::cerr << "Shader: failed to read '" << vertexPath
                  << "', using embedded fallback\n";
        vs = (fallbackVertex ? fallbackVertex : "");
    }
    if (fs.empty()) {
        std::cerr << "Shader: failed to read '" << fragmentPath
                  << "', using embedded fallback\n";
        fs = (fallbackFragment ? fallbackFragment : "");
    }
    return Shader(vs.c_str(), fs.c_str());
}

Shader::Shader(const char* vertexSrc, const char* fragmentSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);
    if (vs == 0 || fs == 0) {
        m_program = 0;
        return;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);

    GLint ok = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0');
        glGetProgramInfoLog(m_program, len, nullptr, log.data());
        std::cerr << "Program link error:\n" << log << std::endl;
    }

    // 链接后即可删除 shader 对象（program 已保留其编译结果）
    glDeleteShader(vs);
    glDeleteShader(fs);
}

Shader::Shader(Shader&& other) noexcept : m_program(other.m_program) {
    other.m_program = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (m_program) glDeleteProgram(m_program);
        m_program = other.m_program;
        other.m_program = 0;
    }
    return *this;
}

Shader::~Shader() {
    if (m_program) glDeleteProgram(m_program);
}

void Shader::use() const {
    glUseProgram(m_program);
}

void Shader::setMat4(const char* name, const float* data) const {
    glUniformMatrix4fv(glGetUniformLocation(m_program, name), 1, GL_FALSE, data);
}

void Shader::setVec2(const char* name, float x, float y) const {
    glUniform2f(glGetUniformLocation(m_program, name), x, y);
}

void Shader::setVec4(const char* name, float r, float g, float b, float a) const {
    glUniform4f(glGetUniformLocation(m_program, name), r, g, b, a);
}
