// src/game/systems/LoopSystem.cpp
#include "game/systems/LoopSystem.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "game/SaveSystem.h"

namespace echo {
namespace {

std::string secondsText(float s) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(1) << s << "s";
    return os.str();
}

std::string joinList(const std::vector<std::string>& v) {
    std::ostringstream os;
    os << "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) os << ",";
        os << v[i];
    }
    os << "]";
    return os.str();
}

std::string joinList(const std::vector<int>& v) {
    std::ostringstream os;
    os << "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) os << ",";
        os << v[i];
    }
    os << "]";
    return os.str();
}

} // namespace

void LoopSystem::subscribe() {
    m_subs.push_back(m_bus.subscribe<TrapHitEvent>(
        [this](const TrapHitEvent& e) { onTrapHit(e); }));
    m_subs.push_back(m_bus.subscribe<EnemyHitEvent>(
        [this](const EnemyHitEvent& e) { onEnemyHit(e); }));
    m_subs.push_back(m_bus.subscribe<LoopTimeoutEvent>(
        [this](const LoopTimeoutEvent& e) { onLoopTimeout(e); }));
    m_subs.push_back(m_bus.subscribe<PlayerDiedEvent>(
        [this](const PlayerDiedEvent& e) { onPlayerDied(e); }));
    m_subs.push_back(m_bus.subscribe<PasswordCollectedEvent>(
        [this](const PasswordCollectedEvent& e) { onPasswordCollected(e); }));
    m_subs.push_back(m_bus.subscribe<DoorOpenedEvent>(
        [this](const DoorOpenedEvent& e) { onDoorOpened(e); }));
    m_subs.push_back(m_bus.subscribe<TeleportUsedEvent>(
        [this](const TeleportUsedEvent& e) { onTeleportUsed(e); }));
    m_subs.push_back(m_bus.subscribe<LevelEscapedEvent>(
        [this](const LevelEscapedEvent& e) { onLevelEscaped(e); }));
    m_subs.push_back(m_bus.subscribe<LoopStartedEvent>(
        [this](const LoopStartedEvent& e) { onLoopStarted(e); }));
}

void LoopSystem::unsubscribe() {
    for (EventBus::HandlerId id : m_subs) m_bus.unsubscribe(id);
    m_subs.clear();
}

void LoopSystem::beginLoop() {
    m_gs.beginNewLoop(); // Meta 不动，Run 推倒重来（并同步玩家 Transform）
    m_bus.publish(LoopStartedEvent{m_gs.meta.loops});
}

bool LoopSystem::finishFrameIfDead() {
    if (!m_pendingDeath) return false;
    m_pendingDeath = false;
    beginLoop();
    return true;
}

void LoopSystem::killForDebug() {
    killPlayer("manual reset (R)", -1);
    finishFrameIfDead();
}

void LoopSystem::wipeProfile() {
    m_gs.meta.clearAll();
    m_pendingDeath = false;
    m_finished     = false;
    logLine("### profile wiped: every memory erased, back to knowing nothing ###");
    beginLoop();
    persist();
}

void LoopSystem::replayAfterEscape() {
    if (!m_finished) return; // 只有通关定格时才需要「重跑」
    m_finished     = false;
    m_pendingDeath = false;
    m_gs.run.elapsed = 0.0f; // 上一轮已计入总用时，清零防止复盘时重复累加
    logLine("");
    logLine("### replay: memory kept, the clock is reset and you are back at the spawn ###");
    beginLoop(); // Run 全部推倒重来；Meta 一个字节不动
}

void LoopSystem::restartKeepMeta() {
    m_finished     = false;
    m_pendingDeath = false;
    logLine("");
    logLine("### restarted: memory kept, clock reset, back at the spawn ###");
    beginLoop(); // Run 全部推倒重来；Meta 一个字节不动
}

void LoopSystem::timeOut() { killPlayer("time out"); }

void LoopSystem::killPlayer(const std::string& cause, int trapId) {
    if (m_godMode) return;        // 无敌模式：任何死因都无效（倒计时结束也不会死）
    if (!m_gs.run.alive) return; // 同一帧内不重复死
    m_gs.run.alive = false;

    PlayerDiedEvent ev;
    ev.loopIndex = m_gs.meta.loops;
    ev.pos       = m_gs.run.player;
    ev.cause     = cause;
    ev.trapId    = trapId;
    m_bus.publish(ev);
}

void LoopSystem::persist() {
    std::string err;
    if (!save::writeFile(m_profile, m_gs.meta, &err)) logLine("! save failed: " + err);
}

void LoopSystem::logLine(std::string line) { m_log.push_back(std::move(line)); }

std::vector<std::string> LoopSystem::drainLog() {
    std::vector<std::string> out;
    out.swap(m_log);
    return out;
}

// ---------------------------------------------------------------------------
// 事件回调
// ---------------------------------------------------------------------------
void LoopSystem::onTrapHit(const TrapHitEvent& e) {
    killPlayer("trap #" + std::to_string(e.trapId), e.trapId);
}

void LoopSystem::onEnemyHit(const EnemyHitEvent& e) {
    // 注意：trapId = -1 —— 敌人致死只写「敌人记忆」，不写陷阱记忆。
    if (m_gs.meta.rememberEnemy(e.enemyId)) {
        logLine("  memory += enemy #" + std::to_string(e.enemyId) +
                "   (its patrol route is now marked on every future loop)");
    }
    killPlayer("enemy #" + std::to_string(e.enemyId), -1);
}

void LoopSystem::onLoopTimeout(const LoopTimeoutEvent& e) {
    logLine("!! loop " + std::to_string(e.loopIndex) + " ran out of time");
}

void LoopSystem::onPlayerDied(const PlayerDiedEvent& e) {
    m_gs.meta.deaths += 1;

    if (e.trapId >= 0 && m_gs.meta.rememberTrap(e.trapId)) {
        logLine("  memory += trap #" + std::to_string(e.trapId) +
                "   (its position is now visible on every future loop)");
    }

    logLine("--- died: " + e.cause + "   at (" +
            std::to_string(static_cast<int>(e.pos.x)) + ", " +
            std::to_string(static_cast<int>(e.pos.y)) + ") ---");

    m_pendingDeath = true; // 真正的重置放到帧末
    persist();
}

void LoopSystem::onPasswordCollected(const PasswordCollectedEvent& e) {
    if (m_gs.meta.rememberPassword(e.code)) {
        logLine("  memory += password \"" + e.code + "\"   (permanent, survives every Reset)");
    }
    persist();
}

void LoopSystem::onDoorOpened(const DoorOpenedEvent& e) {
    if (m_gs.meta.rememberDoor(e.doorId)) {
        logLine("  memory += door \"" + e.doorId + "\" is passable");
    }
    logLine("  door \"" + e.doorId + "\" accepted code " + e.code);
    persist();
}

void LoopSystem::onTeleportUsed(const TeleportUsedEvent& e) {
    logLine("  传送门 [" + e.groupId + "] 空间折叠 —— 你出现在了另一端");
}

void LoopSystem::onLevelEscaped(const LevelEscapedEvent& e) {
    m_finished           = true;
    m_gs.run.escaped     = true;

    if (m_godMode) {
        // 无敌模式开启：仍可逃脱过关，但不记入通关记录
        // （escapes 计数与 bestTime 最佳成绩均不更新，避免污染正常成绩榜）
        const float total = m_gs.meta.totalElapsed + e.runTime; // 本次累计用时（含这轮回）
        m_gs.meta.totalElapsed = 0.0f; // 通关尝试结束，归零
        logLine("");
        logLine("##########  ESCAPED  (无敌模式：本次逃脱不记入通关记录)  ##########");
        logLine("  loop reached : " + std::to_string(e.loopIndex));
        logLine("  this run took: " + secondsText(total) +
                "  (累计各轮回用时之和)");
        logLine("  ⚠ 无敌模式开启 → escapes / bestTime 均不更新");
        logLine("  F5 = wipe profile and start over from zero memory | ESC = quit");
        persist();
        return;
    }

    m_gs.meta.totalElapsed += e.runTime;          // 把逃出这轮回的用时也计入总用时
    const float total      = m_gs.meta.totalElapsed;
    m_gs.meta.escapes     += 1;
    if (m_gs.meta.bestTime < 0.0f || total < m_gs.meta.bestTime) {
        m_gs.meta.bestTime = total;
    }
    m_gs.meta.totalElapsed = 0.0f;   // 通关尝试结束，归零，下次从头累计

    logLine("");
    logLine("##########  ESCAPED  ##########");
    logLine("  loop reached : " + std::to_string(e.loopIndex));
    logLine("  this run took: " + secondsText(total) +
            "   (best " + secondsText(m_gs.meta.bestTime) + ")");
    logLine("  loops played : " + std::to_string(m_gs.meta.loops));
    logLine("  total deaths : " + std::to_string(m_gs.meta.deaths));
    logLine("  F5 = wipe profile and start over from zero memory | ESC = quit");
    persist();
}

void LoopSystem::onLoopStarted(const LoopStartedEvent& e) {
    logLine("");
    logLine("=================== LOOP " + std::to_string(e.loopIndex) + " ===================");
    logLine("  memory : loops=" + std::to_string(m_gs.meta.loops) +
            " deaths=" + std::to_string(m_gs.meta.deaths) +
            " escapes=" + std::to_string(m_gs.meta.escapes) +
            " passwords=" + joinList(m_gs.meta.knownPasswords) +
            " knownTraps=" + joinList(m_gs.meta.knownTrapIds) +
            " openedDoors=" + joinList(m_gs.meta.openedDoorIds) +
            (m_gs.meta.bestTime >= 0.0f
                 ? " bestTime=" + secondsText(m_gs.meta.bestTime) : ""));

    // M7：门是否开启只看本轮是否拿着钥匙；轮回开始持有列表已清空，门全部上锁。
    bool anyOpen = false;
    for (const DoorDef& d : m_gs.world.doors) {
        if (d.requiresCode.empty()) continue;
        if (m_gs.run.holdsCode(d.requiresCode)) anyOpen = true; // 本轮已持有（理论上轮回开始为 false）
    }
    if (!anyOpen) {
        logLine("  status : GATE is locked. You must re-fetch the data core each loop.");
    }
    logLine("  warning: traps you have never touched are invisible.");
}

} // namespace echo
