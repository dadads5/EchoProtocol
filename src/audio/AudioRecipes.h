// AudioRecipes.h —— 程序化音效合成（M6 音效，纯函数 / 无 SDL 依赖）
//
// 不打包任何 .wav，全部用正弦 + 噪声在运行时合成。好处：
//   ① 零素材资源，构建/分发不用带音频文件
//   ② 合成器本身就是"自己写的迷你音频引擎"，作品集好讲
//   ③ 纯函数、无 SDL，能被单测直接覆盖（见 tests/test_audio.cpp）
//
// renderSfx(id, sampleRate) 返回单声道、范围 [-1,1] 的 float 采样缓冲。
#pragma once

#include <vector>
#include <cmath>
#include <cstdlib>

#include "audio/AudioSink.h"

namespace echo {

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// 向缓冲 b 追加一段「频率 f0→f1 线性扫频」的正弦音，带简单 ADSR 包络。
// 缓冲会自动按需扩容，因此多个 appendTone 可以叠出和弦 / 琶音。
inline void appendTone(std::vector<float>& b, int sr, float t0, float dur,
                       float f0, float f1, float peak) {
    const int start = static_cast<int>(t0 * sr);
    const int n     = static_cast<int>(dur * sr);
    if (b.size() < static_cast<size_t>(start + n))
        b.resize(static_cast<size_t>(start + n), 0.0f);
    const float a = dur * 0.12f; // attack
    const float r = dur * 0.30f; // release
    for (int i = 0; i < n; ++i) {
        const float t   = static_cast<float>(i) / static_cast<float>(sr);
        const float env = (t < a) ? (t / a)
                                  : (t > dur - r ? std::max(0.0f, (dur - t) / r) : 1.0f);
        const float f   = f0 + (f1 - f0) * (t / dur);
        b[start + i] += peak * env * std::sin(2.0f * M_PI * f * (t0 + t));
    }
}

// 向缓冲追加一段白噪声爆破（用于死亡 / 开门的"撞击感"）。
inline void appendNoise(std::vector<float>& b, int sr, float t0, float dur, float peak) {
    const int start = static_cast<int>(t0 * sr);
    const int n     = static_cast<int>(dur * sr);
    if (b.size() < static_cast<size_t>(start + n))
        b.resize(static_cast<size_t>(start + n), 0.0f);
    const float a = dur * 0.05f;
    const float r = dur * 0.40f;
    for (int i = 0; i < n; ++i) {
        const float t   = static_cast<float>(i) / static_cast<float>(sr);
        const float env = (t < a) ? (t / a)
                                  : (t > dur - r ? std::max(0.0f, (dur - t) / r) : 1.0f);
        const float x   = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
        b[start + i] += peak * env * x;
    }
}

// 按 Sfx 合成对应的采样缓冲。各音效的设计意图见 AudioSink.h 的枚举注释。
inline std::vector<float> renderSfx(Sfx id, int sr) {
    std::vector<float> b;
    switch (id) {
    case Sfx::Alert: // 双音"哔—哔"上扬，警觉警示
        appendTone(b, sr, 0.00f, 0.09f, 680.0f, 680.0f, 0.55f);
        appendTone(b, sr, 0.09f, 0.12f, 920.0f, 920.0f, 0.55f);
        break;
    case Sfx::Death: // 噪声撞击 + 频率下扫，压抑的"完蛋"
        appendNoise(b, sr, 0.00f, 0.06f, 0.30f);
        appendTone(b, sr, 0.02f, 0.55f, 420.0f, 70.0f, 0.55f);
        break;
    case Sfx::Password: // 两音上行小铃，情报到手的清脆"叮"
        appendTone(b, sr, 0.00f, 0.10f, 523.0f, 523.0f, 0.45f);
        appendTone(b, sr, 0.10f, 0.16f, 784.0f, 784.0f, 0.45f);
        break;
    case Sfx::DoorOpen: // 低频"咚" + 轻微撞击，记忆开门的厚重感
        appendTone(b, sr, 0.00f, 0.28f, 150.0f, 120.0f, 0.70f);
        appendNoise(b, sr, 0.00f, 0.03f, 0.22f);
        break;
    case Sfx::LoopReset: // 低频上扫，循环重置很频繁 → 刻意压低增益
        appendTone(b, sr, 0.00f, 0.30f, 200.0f, 520.0f, 0.30f);
        break;
    case Sfx::Escape: // C-E-G-C 上行琶音，通关胜利小号
        appendTone(b, sr, 0.00f, 0.14f, 523.0f, 523.0f, 0.40f);
        appendTone(b, sr, 0.14f, 0.14f, 659.0f, 659.0f, 0.40f);
        appendTone(b, sr, 0.28f, 0.14f, 784.0f, 784.0f, 0.40f);
        appendTone(b, sr, 0.42f, 0.22f, 1046.0f, 1046.0f, 0.45f);
        break;
    case Sfx::MenuMove: // 极短高频"嗒"，光标移动
        appendTone(b, sr, 0.00f, 0.035f, 1200.0f, 1200.0f, 0.22f);
        break;
    case Sfx::MenuConfirm: // 柔和"嘟"，确认
        appendTone(b, sr, 0.00f, 0.08f, 720.0f, 720.0f, 0.32f);
        break;
    }
    return b;
}

} // namespace echo
