// AudioSystem.h —— 基于 SDL2 原生音频 API 的具体音效实现（M6 音效）
//
// 只编进主程序（EchoProtocol 可执行目标），不进 echo_core，因此这里可以安全 include SDL。
// 设计要点：
//   - 用 SDL_OpenAudioDevice + 回调，自己写一个极简「多声部加法混音器」
//   - 每个音效 = 一段预合成好的 float 采样缓冲（见 AudioRecipes.h），play 时塞进声部队列
//   - 设备打开失败时 ok()==false，play 全部 no-op —— 无音频设备的环境（如沙箱 / 无声CI）不会崩
#pragma once

#include "audio/AudioSink.h"

#include <SDL2/SDL.h>

#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace echo {

class AudioSystem : public AudioSink {
public:
    AudioSystem() = default;
    ~AudioSystem() override;

    // 初始化 SDL 音频子系统并打开设备。失败返回 false（游戏照常跑，只是没声音）。
    bool init();

    void play(Sfx id) override;
    void setMuted(bool muted) override { m_muted = muted; }
    bool ok() const override { return m_ok; }

    // 显式关闭设备（析构会自动调用）。
    void close();

private:
    struct Voice {
        std::vector<float> buf; // 该声部的采样（来自 renderSfx，已缓存）
        size_t             pos = 0; // 播放游标
        float              gain = 1.0f;
    };

    SDL_AudioDeviceID                 m_dev = 0;
    int                               m_sr  = 44100;
    bool                              m_ok  = false;
    bool                              m_muted = false;
    std::mutex                        m_mtx; // 保护 m_voices（回调线程与主线程都会碰）
    std::vector<Voice>                m_voices;
    std::unordered_map<int, std::vector<float>> m_cache; // 按 (int)Sfx 缓存合成结果

    // SDL 音频回调（静态成员，可访问私有成员）。在独立线程里按块调用。
    static void SDLCALL audioCallback(void* userdata, Uint8* stream, int len);
};

} // namespace echo
