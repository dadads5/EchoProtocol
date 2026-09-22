// AudioSystem.cpp —— SDL2 音频设备 + 极简加法混音器（M6 音效）
#include "audio/AudioSystem.h"
#include "audio/AudioRecipes.h"

#include <algorithm>
#include <cmath>

namespace echo {

AudioSystem::~AudioSystem() { close(); }

void SDLCALL AudioSystem::audioCallback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<AudioSystem*>(userdata);
    if (!self) return;

    const int frames = len / 2; // AUDIO_S16 单声道：每帧 2 字节
    Sint16* out = reinterpret_cast<Sint16*>(stream);

    std::lock_guard<std::mutex> lk(self->m_mtx);
    for (int i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (auto& v : self->m_voices) {
            if (v.pos < v.buf.size())
                sum += v.buf[v.pos++] * v.gain;
        }
        sum *= 0.35f; // 主音量：留余量，避免多声部叠加削波
        if (sum > 1.0f) sum = 1.0f;
        if (sum < -1.0f) sum = -1.0f;
        out[i] = static_cast<Sint16>(sum * 32767.0f);
    }
    // 把播完的声部清掉
    auto& vs = self->m_voices;
    vs.erase(std::remove_if(vs.begin(), vs.end(),
                            [](const AudioSystem::Voice& v) {
                                return v.pos >= v.buf.size();
                            }),
             vs.end());
}

bool AudioSystem::init() {
    // 单独初始化音频子系统（主程序已 SDL_Init(VIDEO)）；失败则静默降级。
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        m_ok = false;
        return false;
    }

    SDL_AudioSpec want{};
    want.freq     = 44100;
    want.format   = AUDIO_S16; // 16 位有符号，跨平台支持最稳
    want.channels = 1;         // 单声道足够
    want.samples  = 1024;
    want.callback = &AudioSystem::audioCallback;
    want.userdata = this;

    SDL_AudioSpec have{};
    m_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (m_dev == 0) {
        m_ok = false;
        return false;
    }
    m_sr  = have.freq; // 用设备实际采样率（通常是 44100）
    m_ok  = true;
    SDL_PauseAudioDevice(m_dev, 0); // 解除暂停，开始播放
    return true;
}

void AudioSystem::close() {
    if (m_dev != 0) {
        SDL_PauseAudioDevice(m_dev, 1);
        SDL_CloseAudioDevice(m_dev);
        m_dev = 0;
    }
    m_ok = false;
}

void AudioSystem::play(Sfx id) {
    if (!m_ok || m_muted) return;

    std::vector<float> buf;
    auto it = m_cache.find(static_cast<int>(id));
    if (it == m_cache.end()) {
        buf = renderSfx(id, m_sr); // 首次：合成并缓存
        m_cache[static_cast<int>(id)] = buf;
    } else {
        buf = it->second;
    }
    if (buf.empty()) return;

    std::lock_guard<std::mutex> lk(m_mtx);
    m_voices.push_back({buf, 0, 1.0f});
    // 并发声部上限，避免极端情况下堆积爆音
    if (m_voices.size() > 24)
        m_voices.erase(m_voices.begin());
}

} // namespace echo
