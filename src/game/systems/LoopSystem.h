// src/game/systems/LoopSystem.h —— 时间循环 + 记忆的「收口」系统（M3）
//
// 它订阅 EventBus 上所有「发生了什么」的事件，负责：
//   - 判定死亡 / 处理单轮重置（beginLoop，延迟到帧末 finishFrameIfDead）
//   - 把记忆写进 Meta 层（密码 / 陷阱 / 门）
//   - 通关结算、最佳时间、落盘
//   - 打日志（drainLog 给渲染层每帧取走打印）
//
// 死亡重置「延迟到帧末」统一执行：因为死亡可能发生在遍历陷阱/核心的循环里，
// 当场 reset 会让这一帧剩下一半逻辑读到「已经重开的自己」。
#pragma once

#include <string>
#include <vector>

#include "core/EventBus.h"
#include "core/Events.h"
#include "ecs/World.h"
#include "game/GameState.h"

namespace echo {

class LoopSystem {
public:
    LoopSystem(GameState& gs, EventBus& bus)
        : m_gs(gs), m_bus(bus) {}

    void setProfile(const std::string& path) { m_profile = path; }
    void subscribe();
    void unsubscribe();

    // 切换关卡时清掉「通关 / 待死亡」标志（beginLoop 不碰这两个标志）
    void resetState() { m_finished = false; m_pendingDeath = false; }

    bool finished() const { return m_finished; }

    // 开始新一轮（Run 重置、Meta 不动、发 LoopStartedEvent）
    void beginLoop();

    // 帧末统一处理死亡重置；返回 true 表示本帧确实重置了
    bool finishFrameIfDead();

    void killForDebug();          // R 键：手动触发一次死亡并立即重置
    void wipeProfile();           // F5：清空存档，回到 LOOP 1
    void replayAfterEscape();      // 通关后重跑（Run 重置、Meta 保留）
    void restartKeepMeta();        // 暂停菜单 RESTART（Run 重置、Meta 保留）
    void timeOut();                // 单轮时间耗尽：触发一次死亡（供 Game::update 调用）

    // 无敌模式（暂停菜单开关）：开启时任何死因都无效（陷阱 / 敌兵 / 超时 / 调试）
    void setGodMode(bool on) { m_godMode = on; }
    bool godMode() const { return m_godMode; }

    std::vector<std::string> drainLog();
    void logLine(std::string line);

private:
    void killPlayer(const std::string& cause, int trapId = -1);
    void persist();

    // 事件回调
    void onTrapHit(const TrapHitEvent& e);
    void onEnemyHit(const EnemyHitEvent& e);
    void onLoopTimeout(const LoopTimeoutEvent& e);
    void onPlayerDied(const PlayerDiedEvent& e);
    void onPasswordCollected(const PasswordCollectedEvent& e);
    void onDoorOpened(const DoorOpenedEvent& e);
    void onTeleportUsed(const TeleportUsedEvent& e);
    void onLevelEscaped(const LevelEscapedEvent& e);
    void onLoopStarted(const LoopStartedEvent& e);

    GameState& m_gs;
    EventBus&  m_bus;
    std::string m_profile;
    bool m_pendingDeath = false;
    bool m_finished     = false;
    bool m_godMode      = false;   // 无敌模式：开启时 killPlayer 直接返回
    std::vector<std::string> m_log;
    std::vector<EventBus::HandlerId> m_subs;
};

} // namespace echo
