// Utf8Path.h —— Windows 下把 UTF-8 编码路径转成宽字符路径再打开文件。
//
// 背景：SDL_GetBasePath() 返回 UTF-8 编码的 exe 所在目录（可能含中文/空格）。
// 而窄字符 std::ifstream(path) 在 MSVC 上按 ANSI(GBK) 解释路径字节，
// 遇到「E:\游戏例子」这类中文目录就会打不开（表现为闪退/资源加载失败）。
// 解法：统一用 MultiByteToWideChar 转成 std::filesystem::path(wstring)，
// 再传给文件流 —— MSVC 的 fstream 有接受 filesystem::path 的重载，
// 内部走宽字符 API，任何合法文件名都能打开。
#pragma once

#include <filesystem>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace echo {

#ifdef _WIN32
inline std::filesystem::path u8path(const std::string& p) {
    if (p.empty()) return std::filesystem::path();
    int n = MultiByteToWideChar(CP_UTF8, 0, p.c_str(),
                                static_cast<int>(p.size()), nullptr, 0);
    if (n <= 0) return std::filesystem::path(p); // 转换失败退回原样
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p.c_str(), static_cast<int>(p.size()),
                        w.data(), n);
    // MultiByteToWideChar 写入的字符串含结尾 L'\0'，去掉
    while (!w.empty() && w.back() == L'\0') w.pop_back();
    return std::filesystem::path(std::move(w));
}
#else
inline std::filesystem::path u8path(const std::string& p) {
    return std::filesystem::path(p);
}
#endif

} // namespace echo
