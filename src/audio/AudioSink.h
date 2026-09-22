// AudioSink.h —— 与 SDL 无关的音频输出抽象（M6 音效）
//
// 设计意图：游戏逻辑层（echo_core，**不链 SDL**）只认这个接口，
// 具体的 SDL 音频设备实现（AudioSystem）只在主程序里，因此：
//   ① 逻辑层永远不会因为"不小心 include 了 SDL"而编不过
//   ② 单测链接 echo_core 时不需要开音频设备，audio 句柄传 nullptr 即可静默
//
// 事件 → 音效 的接线全部发生在 Game 的事件订阅里：
//   Game 持有 AudioSink*（默认 nullptr = 静默），handler 里 `if (audio) audio->play(...)`。
#pragma once

#include <cstdint>

namespace echo {

// 所有音效的枚举。命名即语义，方便叙事讲解。
enum class Sfx : uint8_t {
    Alert,       // 敌兵发现玩家（从巡逻/搜索转入警觉态）
    Death,       // 玩家死亡（陷阱 / 敌兵 / 超时，任何一种死法）
    Password,    // 读到新密码（情报永久积累的那一声"叮"）
    DoorOpen,    // 记忆开门（门因记住密码而开）
    LoopReset,   // 新一轮开始（循环重置，发生很频繁 → 低增益）
    Escape,      // 通关逃脱（胜利小号）
    MenuMove,    // 菜单光标移动
    MenuConfirm, // 菜单确认
};

// 音频输出抽象。具体实现见 AudioSystem（仅主程序可见）。
class AudioSink {
public:
    virtual ~AudioSink() = default;

    // 播放一个音效。无设备 / 静音时实现应当直接 no-op。
    virtual void play(Sfx id) = 0;

    // 全局静音开关。
    virtual void setMuted(bool muted) = 0;

    // 设备是否可用。不可用时 play 应静默，调用方无需特殊处理。
    virtual bool ok() const = 0;
};

} // namespace echo
