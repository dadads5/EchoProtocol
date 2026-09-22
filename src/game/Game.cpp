// Game.cpp —— 薄编排器：按序跑各 System（M3 真 ECS 重构）
#include "game/Game.h"

#include "game/LevelLoader.h"
#include "game/SceneBuilder.h"
#include "game/SaveSystem.h"
#include "game/systems/Systems.h"

namespace echo {

Game::Game(EventBus& bus) : m_bus(bus), m_loop(m_state, m_bus) {}

Game::~Game() {
    // 退订，避免 Game 析构后总线还拿着悬空的 lambda
    m_loop.unsubscribe();
}

bool Game::init(const std::string& levelPath, const std::string& profilePath,
                std::string* err) {
    LevelData level;
    if (!loadLevelFile(levelPath, level, err)) return false;
    m_state.world = std::move(level);

    m_profilePath = profilePath;
    m_loop.setProfile(profilePath);

    // ---- 读 Meta 存档（只有 Meta 层会落盘）----
    std::string saveErr;
    if (save::fileExists(m_profilePath)) {
        if (save::readFile(m_profilePath, m_state.meta, &saveErr)) {
            m_loop.logLine("profile loaded from " + m_profilePath);
        } else {
            m_loop.logLine("! profile unreadable (" + saveErr + "), starting with empty memory");
            m_state.meta.clearAll();
        }
    } else {
        m_loop.logLine("no profile found - this is the very first run, memory is empty");
    }

    // ---- 关卡资产 -> ECS 实体世界（只建一次，之后不再重建）----
    buildScene(m_state.world, m_state.ecs);

    // ---- 注册事件订阅（系统级）----
    m_loop.subscribe();

    // 每轮开始（含切关后的 beginLoop）都复位敌兵位置 + FSM。
    // 订阅只注册一次，loadLevel 不重订 —— 切关也走这条（buildScene 重建后 beginLoop 触发）。
    m_bus.subscribe<LoopStartedEvent>([this](const LoopStartedEvent&) {
        resetEnemies(m_state.ecs);
    });

    // ---- 音效接线（M6）：把关键事件映射到 AudioSink。
    //      m_audio 为空（如单测、或没挂音频）时跳过订阅，逻辑零改动。 ----
    if (m_audio) {
        m_bus.subscribe<PlayerDiedEvent>([this](const PlayerDiedEvent&) {
            m_audio->play(Sfx::Death);
        });
        m_bus.subscribe<PasswordCollectedEvent>([this](const PasswordCollectedEvent&) {
            m_audio->play(Sfx::Password);
        });
        m_bus.subscribe<DoorOpenedEvent>([this](const DoorOpenedEvent&) {
            m_audio->play(Sfx::DoorOpen);
        });
        m_bus.subscribe<LoopStartedEvent>([this](const LoopStartedEvent&) {
            m_audio->play(Sfx::LoopReset);
        });
        m_bus.subscribe<LevelEscapedEvent>([this](const LevelEscapedEvent&) {
            m_audio->play(Sfx::Escape);
        });
        m_bus.subscribe<EnemyAlertEvent>([this](const EnemyAlertEvent&) {
            m_audio->play(Sfx::Alert);
        });
    }

    m_loop.logLine("level loaded: " + m_state.world.name);
    m_loop.logLine("controls: WASD/arrows move | R = die on purpose | F5 = wipe profile | ESC = quit");

    m_loop.beginLoop(); // 开始第 1 轮
    return true;
}

void Game::syncPlayerTransform() { m_state.syncPlayerTransform(); }

// ---------------------------------------------------------------------------
// 每帧推进：顺序与原先一致，只是逻辑搬进了 System
// ---------------------------------------------------------------------------
void Game::update(float dt, const InputFrame& in) {
    if (m_loop.finished()) return;

    // --- 1. 计时（主失败源）---
    m_state.run.elapsed += dt;
    m_state.run.timeLeft -= dt;
    if (m_state.run.timeLeft <= 0.0f) {
        m_state.run.timeLeft = 0.0f;
        if (!m_loop.godMode()) {
            m_bus.publish(LoopTimeoutEvent{m_state.meta.loops});
            m_loop.timeOut();
            m_loop.finishFrameIfDead();
            return;
        }
        // 无敌模式：倒计时停在 0，超时不再触发死亡/重置循环，
        // 否则 killPlayer 被短路、timeLeft 恒为 0，每帧都会走进这里
        // → 刷 "ran out of time" 且跳过移动系统，玩家彻底卡死。
    }

    // --- 2. 移动 ---
    movementSystem(dt, in, m_state.run, m_state.world);
    // --- 2.1 墙碰撞：把玩家推出所有 wall（让岔路走廊真正挡得住人）---
    wallBlockSystem(m_state.run, m_state.world);
    // --- 2.2 传送门（第四关）：踩上垫子瞬移到另一端 ---
    teleporterSystem(m_state.run, m_state.world, m_bus);
    syncPlayerTransform(); // Run.player -> ECS Transform

    // --- 2.5 敌兵（M5：FSM 巡逻 / 视线遮挡 / A* 绕墙追击 / 致死）---
    enemySystem(dt, m_state.run, m_state.world, m_state.meta, m_state.ecs, m_bus);
    if (m_loop.finishFrameIfDead()) return;

    // --- 3. 门（锁着的时候当墙用）---
    doorBlockSystem(m_state.run, m_state.meta, m_state.ecs, m_state.world);

    // --- 4. 情报 ---
    pickupSystem(m_state.run, m_state.meta, m_state.ecs, m_bus);
    if (m_loop.finishFrameIfDead()) return;

    // --- 5. 出口 ---
    exitSystem(m_state.run, m_state.meta, m_state.ecs, m_bus);
    if (m_loop.finished()) return;

    // --- 6. 陷阱放最后：命中即死，死了这一帧就不用再算了 ---
    trapSystem(m_state.run, m_state.ecs, m_bus);
    m_loop.finishFrameIfDead();
}

void Game::killForDebug() { m_loop.killForDebug(); }
void Game::wipeProfile()  { m_loop.wipeProfile(); }
void Game::replayAfterEscape() { m_loop.replayAfterEscape(); }
void Game::restartKeepMeta()   { m_loop.restartKeepMeta(); }

std::vector<std::string> Game::drainLog() { return m_loop.drainLog(); }

bool Game::loadLevel(const std::string& levelPath, const std::string& profilePath) {
    LevelData level;
    std::string err;
    if (!loadLevelFile(levelPath, level, &err)) {
        m_loop.logLine("! loadLevel failed: " + err);
        return false;
    }

    m_state.world = std::move(level);

    // 切到这关自己的存档：先清成空白 Meta，再从盘里读回这关的记忆
    m_profilePath = profilePath;
    m_loop.setProfile(profilePath);
    m_state.meta = MetaState{};
    if (save::fileExists(m_profilePath)) {
        std::string serr;
        if (!save::readFile(m_profilePath, m_state.meta, &serr))
            m_state.meta.clearAll();
    }

    // 关卡资产 -> ECS 实体世界（重建，因为世界是新的）
    buildScene(m_state.world, m_state.ecs);

    // 重置循环状态（通关 / 待死亡），然后开新一轮（Run 推倒重来、Meta 不动）
    m_loop.resetState();
    m_loop.beginLoop();

    m_loop.logLine("level (re)loaded: " + m_state.world.name);
    return true;
}

} // namespace echo
