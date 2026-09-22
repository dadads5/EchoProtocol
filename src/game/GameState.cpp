// GameState.cpp —— 三层状态模型实现（纯逻辑，无 SDL / GL 依赖）
#include "game/GameState.h"

#include <algorithm>
#include <cmath>

namespace echo {

// ---------------------------------------------------------------------------
// 几何工具
// ---------------------------------------------------------------------------
bool pointInRect(const glm::vec2& p, const Rect& r) {
    const glm::vec2 h = r.half();
    return p.x >= r.center.x - h.x && p.x <= r.center.x + h.x &&
           p.y >= r.center.y - h.y && p.y <= r.center.y + h.y;
}

bool overlapRect(const Rect& a, const Rect& b) {
    const glm::vec2 ha = a.half();
    const glm::vec2 hb = b.half();
    return std::abs(a.center.x - b.center.x) <= (ha.x + hb.x) &&
           std::abs(a.center.y - b.center.y) <= (ha.y + hb.y);
}

glm::vec2 clampToBounds(const glm::vec2& p, const glm::vec2& halfExtent,
                        const glm::vec2& min, const glm::vec2& max) {
    const float loX = min.x + halfExtent.x;
    const float hiX = max.x - halfExtent.x;
    const float loY = min.y + halfExtent.y;
    const float hiY = max.y - halfExtent.y;

    glm::vec2 out = p;
    // 若可行区间反了（盒子比场地还大），就摆中间，避免抖动
    out.x = (loX <= hiX) ? std::clamp(p.x, loX, hiX) : (min.x + max.x) * 0.5f;
    out.y = (loY <= hiY) ? std::clamp(p.y, loY, hiY) : (min.y + max.y) * 0.5f;
    return out;
}

// ---------------------------------------------------------------------------
// MetaState
// ---------------------------------------------------------------------------
namespace {
template <typename T>
bool contains(const std::vector<T>& v, const T& value) {
    return std::find(v.begin(), v.end(), value) != v.end();
}
template <typename T>
bool pushUnique(std::vector<T>& v, T value) {
    if (contains(v, value)) return false;
    v.push_back(std::move(value));
    return true;
}
} // namespace

bool MetaState::knowsPassword(const std::string& code) const {
    return contains(knownPasswords, code);
}

bool MetaState::knowsTrap(int trapId) const {
    return contains(knownTrapIds, trapId);
}

bool MetaState::knowsDoor(const std::string& doorId) const {
    return contains(openedDoorIds, doorId);
}

bool MetaState::knowsEnemy(int enemyId) const {
    return contains(knownEnemyIds, enemyId);
}

bool MetaState::rememberPassword(const std::string& code) {
    if (code.empty()) return false;
    return pushUnique(knownPasswords, code);
}

bool MetaState::rememberTrap(int trapId) {
    if (trapId < 0) return false;
    return pushUnique(knownTrapIds, trapId);
}

bool MetaState::rememberDoor(const std::string& doorId) {
    if (doorId.empty()) return false;
    return pushUnique(openedDoorIds, doorId);
}

bool MetaState::rememberEnemy(int enemyId) {
    if (enemyId < 0) return false;
    return pushUnique(knownEnemyIds, enemyId);
}

void MetaState::clearMemory() {
    knownPasswords.clear();
    knownTrapIds.clear();
    openedDoorIds.clear();
    knownEnemyIds.clear();
    bestTime = -1.0f;
    totalElapsed = 0.0f;
}

void MetaState::clearAll() {
    clearMemory();
    loops   = 0;
    deaths  = 0;
    escapes = 0;
}

// ---------------------------------------------------------------------------
// RunState
// ---------------------------------------------------------------------------
void RunState::reset(const glm::vec2& spawnPoint, float loopSeconds) {
    // 只有这里会写 Run 层 —— Run 层的"生命周期"完全由这个函数定义。
    player          = spawnPoint;
    prevPlayer      = spawnPoint;
    timeLeft        = loopSeconds;
    elapsed         = 0.0f;
    alive           = true;
    escaped         = false;
    teleportLatch   = false;
    carriedCodes.clear();
    // 注意：moveSpeed / playerSize 属于"角色固有属性"，不属于单轮进度，故意不重置。
}

// ---------------------------------------------------------------------------
// GameState
// ---------------------------------------------------------------------------
void GameState::beginNewLoop() {
    meta.totalElapsed += run.elapsed;             // M8：把刚结束这一轮的用时计入总用时
    meta.loops += 1;                              // Meta：只增不减
    run.reset(world.spawn, world.loopSeconds);    // Run：推倒重来
    syncPlayerTransform();                        // ECS 玩家实体跟上出生点
    // world 一个字段都不动 —— 世界是静态的，不会因为你死了就改变
}

void GameState::resetRunOnly() {
    run.reset(world.spawn, world.loopSeconds);
    syncPlayerTransform();
}

Rect GameState::playerRect() const {
    Rect r;
    r.center = run.player;
    r.size   = run.playerSize;
    return r;
}

void GameState::syncPlayerTransform() {
    if (ecs.player() == kNullEntity) return; // 还没 buildScene 时安全跳过
    TransformComponent* t = ecs.get<TransformComponent>(ecs.player());
    if (t) {
        t->pos  = run.player;
        t->size = run.playerSize;
    }
}

} // namespace echo
