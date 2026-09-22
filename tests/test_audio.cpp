// test_audio.cpp —— 音效纯合成器的单元覆盖（M6 音效）
//
// 只测 AudioRecipes.h 里的 renderSfx（纯函数、无 SDL），验证：
//   ① 每个 Sfx 都产出非空缓冲
//   ② 时长落在合理区间（不会太长也不会太短）
//   ③ 所有样本都在 [-1,1] 内（不逐样本 REQUIRE，避免断言数爆炸）
//   ④ 同一 Sfx（仅含正弦、无噪声）合成结果可复现
//
// 注意：AudioSystem（SDL 设备）不在此测，因为单测环境没有音频设备；
// 它的静默降级路径由运行时保证（设备打不开 → play 全 no-op）。
#include "audio/AudioRecipes.h"

#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("renderSfx produces bounded, non-empty buffers for every Sfx", "[audio]") {
    const int sr = 44100;
    const echo::Sfx all[] = {
        echo::Sfx::Alert, echo::Sfx::Death, echo::Sfx::Password,
        echo::Sfx::DoorOpen, echo::Sfx::LoopReset, echo::Sfx::Escape,
        echo::Sfx::MenuMove, echo::Sfx::MenuConfirm,
    };

    for (echo::Sfx id : all) {
        std::vector<float> b = echo::renderSfx(id, sr);
        REQUIRE(!b.empty());

        // 时长在合理区间（0.02s ~ 0.8s）
        const float dur = static_cast<float>(b.size()) / static_cast<float>(sr);
        REQUIRE(dur >= 0.02f);
        REQUIRE(dur <= 0.80f);

        // 所有样本有界（留极小浮点容差）；不逐样本 REQUIRE
        float maxAbs = 0.0f;
        for (float s : b) maxAbs = std::max(maxAbs, std::fabs(s));
        REQUIRE(maxAbs <= 1.0001f);
    }
}

TEST_CASE("renderSfx is deterministic for the same Sfx", "[audio]") {
    const int sr = 44100;
    // Escape 只含正弦（无随机噪声），两次合成应逐样本一致
    auto a = echo::renderSfx(echo::Sfx::Escape, sr);
    auto b = echo::renderSfx(echo::Sfx::Escape, sr);
    REQUIRE(a.size() == b.size());
    float maxDiff = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        maxDiff = std::max(maxDiff, std::fabs(a[i] - b[i]));
    REQUIRE(maxDiff < 1e-5f);
}

TEST_CASE("different Sfx yield different timbres", "[audio]") {
    const int sr = 44100;
    auto alert   = echo::renderSfx(echo::Sfx::Alert, sr);
    auto escape  = echo::renderSfx(echo::Sfx::Escape, sr);
    REQUIRE(alert.size() != escape.size()); // Alert 短、Escape 长
}
