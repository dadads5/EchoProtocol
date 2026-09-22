// Game.h —— 游戏逻辑编排层（M3：真 ECS 重构）
//
// 这一层不 include SDL / OpenGL，只做三件事：
//   ① 订阅事件  ② 按序推进各 System  ③ 通过事件把「发生了什么」告诉外面。
//
// 真 ECS 之后，本层变成「薄编排器」：
//   - 世界实体存在 GameState.ecs（EcsWorld），由 SceneBuilder 从 LevelData 展开
//   - 每帧逻辑拆成 System（Movement/DoorBlock/Pickup/Exit/Trap），遍历组件推进
//   - 死亡 / 重置 / 记忆 / 落盘统一收口在 LoopSystem（事件回调）
//   渲染层（main.cpp）每帧读 state()，需要什么画什么。
//
// 门面 API（init/update/finished/state/drainLog/...）保持不变，
// 因此 main.cpp 与单测无需改动即可继续工作。
#pragma once

#include <string>
#include <vector>

#include "audio/AudioSink.h"
#include "core/EventBus.h"
#include "core/Events.h"
#include "game/GameState.h"
#include "game/systems/LoopSystem.h"

namespace echo {

class Game {
public:
    explicit Game(EventBus& bus);
    ~Game();

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    // 加载关卡 + 读存档 + 构建 ECS 实体世界 + 注册事件订阅 + 开始第 1 轮
    bool init(const std::string& levelPath, const std::string& profilePath,
              std::string* err = nullptr);

    // 运行时切换关卡（选关菜单用）：换关卡 JSON + 该关独立存档，读盘还原这关的
    // Meta 记忆、重建 ECS、重置循环状态并开新一轮。事件订阅只注册一次，不重订。
    bool loadLevel(const std::string& levelPath, const std::string& profilePath);

    // 推进一帧游戏逻辑
    void update(float dt, const InputFrame& in);

    // ---- 调试 / 菜单用 ----
    void killForDebug();  // R 键：手动触发一次死亡
    void wipeProfile();   // F5：清空 Meta 存档
    void replayAfterEscape(); // 通关后重跑（保留记忆）
    void restartKeepMeta();   // 暂停菜单 RESTART（保留记忆重开）

    // 无敌模式（暂停菜单开关）：开启时玩家不会死亡（含倒计时结束）
    void setGodMode(bool on) { m_loop.setGodMode(on); }
    bool godMode() const { return m_loop.godMode(); }

    // ---- 外面（渲染层 / 测试）要读的 ----
    const GameState& state() const { return m_state; }
    bool             finished() const { return m_loop.finished(); }
    std::vector<std::string> drainLog();

    // ---- 音效：把音频输出挂进来（M6）。传 nullptr 则全程静默（默认）。
    //         必须在 init() 之前设置，事件订阅才接得上。 ----
    void setAudio(AudioSink* sink) { m_audio = sink; }
    AudioSink* audio() const { return m_audio; }

private:
    // 把 Run.player 同步进 ECS 玩家实体 Transform（每帧移动后调用）
    void syncPlayerTransform();

    EventBus&  m_bus;
    GameState  m_state;
    LoopSystem m_loop;          // 持有 finished / pendingDeath / 日志，收口循环与记忆
    AudioSink* m_audio = nullptr; // M6 音效：事件 → 音效 的接线点（默认静默）
    std::string m_profilePath;
};

} // namespace echo
