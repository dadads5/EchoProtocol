// src/game/systems/Systems.h —— 五个「每帧」系统（M3 真 ECS 重构）
//
// 每个系统只关心自己那一类组件，互相不认识：
//   MovementSystem  玩家输入 -> 移动 + 边界夹取
//   DoorBlockSystem 锁着的门当成墙，把玩家推出来
//   PickupSystem    玩家碰到情报核心 -> 携带密码 + 发 PasswordCollectedEvent
//   ExitSystem      玩家碰到出口 -> 发 LevelEscapedEvent
//   TrapSystem      玩家碰到陷阱 -> 发 TrapHitEvent（命中即死）
//
// 系统不直接改 Meta（记忆），而是通过 EventBus 把「发生了什么」广播出去，
// 由 LoopSystem 统一收口、写记忆、落盘——系统之间零耦合。
#pragma once

#include "core/EventBus.h"
#include "core/Events.h"
#include "ecs/World.h"
#include "game/GameState.h" // RunState / MetaState / LevelData

namespace echo {

void movementSystem(float dt, const InputFrame& in, RunState& run, const LevelData& level);

// 玩家 vs 墙：把所有 wall 当成实体障碍，用「最小穿入轴」把玩家推出去（允许沿墙滑动）。
// 之前 wall 只参与敌兵导航 / 视线遮挡 / 渲染，玩家走得穿墙；Level 3 的岔路走廊靠它才成立。
void wallBlockSystem(RunState& run, const LevelData& level);

void doorBlockSystem(RunState& run, const MetaState& meta, const EcsWorld& world,
                     const LevelData& level);

// 第四关：传送门。玩家踩上任意一端 -> 立刻出现在同 group 的另一端。
// 防反复触发：传送后置 teleportLatch，直到玩家离开所有传送门垫才复位——
// 所以落在终点垫上不会"落地即回传"，走开再回来才会再次传送。敌人不受传送门影响。
void teleporterSystem(RunState& run, const LevelData& level, EventBus& bus);

void pickupSystem(RunState& run, const MetaState& meta, const EcsWorld& world, EventBus& bus);

void exitSystem(RunState& run, const MetaState& meta, const EcsWorld& world, EventBus& bus);

void trapSystem(RunState& run, const EcsWorld& world, EventBus& bus);

// M4：巡逻机器人 FSM（Patrol / Alert / Search / Attack）
//   读 RunState.player 作为玩家位置（与 trapSystem 一致，不读 ECS 玩家 Transform）；
//   视野带射线遮挡（M5：hasLineOfSight）；追击沿 A* 路径（M5：NavGrid）。
void enemySystem(float dt, const RunState& run, const LevelData& level,
                 const MetaState& meta, EcsWorld& world, EventBus& bus);

// M4：新一轮开始时把敌人归位 + FSM 复位（订阅 LoopStartedEvent 触发，切关复用）
void resetEnemies(EcsWorld& world);

// ===========================================================================
// M5：导航网格 + 视线遮挡（让敌兵"看不见穿墙的玩家"且"绕墙追"而非直线穿墙）
// ===========================================================================

// 在关卡 bounds 上铺的栅格。障碍 = 所有 walls + 所有「未记住密码的 doors」。
// solid[c+r*cols] == true 表示此格不可走（已按 agentRadius 外扩后和障碍相交）。
struct NavGrid {
    glm::vec2 min{0.0f, 0.0f};
    glm::vec2 max{0.0f, 0.0f};
    float     cell = 40.0f;
    int       cols = 0;
    int       rows = 0;
    std::vector<bool> solid; // 长度 cols*rows

    bool        inBounds(int c, int r) const;
    bool        isSolid(int c, int r) const;                 // 越界按实心处理
    glm::vec2   cellCenter(int c, int r) const;
    std::pair<int, int> worldToCell(const glm::vec2& p) const; // 不夹取，可能越界
    int         indexOf(int c, int r) const;

    // A*（8 邻接，禁止穿角）。返回世界坐标航点（已去掉共线点）。
    // 起/终点落在实心格时先环形 BFS 找最近空格，避免卡死；无解返回空。
    std::vector<glm::vec2> findPath(const glm::vec2& from, const glm::vec2& to) const;
};

// 从静态层构建：障碍 = walls + 本轮未持有钥匙的 doors。
// agentRadius 把格子按此半径外扩后再和障碍求交，保证路径与墙留有余量。
NavGrid buildNavGrid(const LevelData& level, const RunState& run,
                     float agentRadius = 24.0f);

// 线段 a→b 是否与 AABB r 相交（含端点在内），Liang–Barsky 裁剪，O(1)。
bool segmentIntersectsRect(const glm::vec2& a, const glm::vec2& b, const Rect& r);

// 敌兵 a 能否看见玩家 b：遍历 walls + 本轮未持有钥匙的 doors，任一相交即 false。
bool hasLineOfSight(const glm::vec2& a, const glm::vec2& b,
                    const LevelData& level, const RunState& run);

// 点是否落在任意障碍（wall / 未开 door）内，供敌兵移动后回退。
bool pointInObstacle(const glm::vec2& p, const LevelData& level, const RunState& run);

} // namespace echo
