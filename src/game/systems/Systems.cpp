// src/game/systems/Systems.cpp
#include "game/systems/Systems.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>

namespace echo {
namespace {

echo::Rect playerRect(const RunState& run) {
    echo::Rect r;
    r.center = run.player;
    r.size   = run.playerSize;
    return r;
}

echo::Rect rectOf(const TransformComponent& t) {
    echo::Rect r;
    r.center = t.pos;
    r.size   = t.size;
    return r;
}

// 巡逻路点往返推进：到端点反向
void advancePatrol(EnemyComponent& en) {
    const int n = static_cast<int>(en.patrol.size());
    if (n <= 1) { en.patrolIndex = 0; return; }
    int ni = en.patrolIndex + en.patrolDir;
    if (ni >= n)      { en.patrolDir = -1; ni = n - 2; }
    else if (ni < 0)  { en.patrolDir =  1; ni = 1; }
    en.patrolIndex = ni;
}

// 找离当前位置最近的巡逻路点（Search -> Patrol 复位用）
int nearestPatrolIndex(const EnemyComponent& en, const glm::vec2& pos) {
    int   best = 0;
    float bestD = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(en.patrol.size()); ++i) {
        const float d = glm::distance(pos, en.patrol[i]);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

} // namespace

// ---------------------------------------------------------------------------
// 传送门（第四关）：成对双向。踩上一端 -> 立刻出现在另一端。
// teleportLatch 语义：传送发生时置 true；玩家"不踩任何传送门垫"时复位。
// 于是落在终点垫上不会立即回传，必须先走开再踩回来 —— 这也是
// "从 F、A 原路返回"这类路线能成立的关键。
// ---------------------------------------------------------------------------
void teleporterSystem(RunState& run, const LevelData& level, EventBus& bus) {
    if (level.teleporters.empty()) return;

    const Rect pr   = playerRect(run);
    bool        onAny = false;
    const TeleporterDef* hit = nullptr;

    for (const TeleporterDef& t : level.teleporters) {
        Rect tr;
        tr.center = t.rect.center;
        tr.size   = t.rect.size;
        if (overlapRect(pr, tr)) { onAny = true; hit = &t; break; }
    }

    if (!onAny) { run.teleportLatch = false; return; }
    if (run.teleportLatch || hit == nullptr) return;

    for (const TeleporterDef& other : level.teleporters) {
        if (other.group != hit->group || other.id == hit->id) continue;
        run.player        = other.rect.center; // 落点 = 另一端垫中心
        run.prevPlayer    = run.player;        // 传送不算"撞墙"，快照同步重置
        run.teleportLatch = true;
        bus.publish(TeleportUsedEvent{hit->group, run.player});
        return;
    }
    // 只有单端没有配对：配置错误，忽略（数据驱动要能容忍脏数据）
}

void movementSystem(float dt, const InputFrame& in, RunState& run, const LevelData& level) {
    run.prevPlayer = run.player; // 移动前快照：墙碰撞据此判断"从哪一侧撞进来"

    glm::vec2 dir(0.0f, 0.0f);
    if (in.left)  dir.x -= 1.0f;
    if (in.right) dir.x += 1.0f;
    if (in.up)    dir.y -= 1.0f;
    if (in.down)  dir.y += 1.0f;

    if (dir.x == 0.0f && dir.y == 0.0f) return;

    // 归一化：斜着走不能比直着走快
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len > 0.0001f) dir /= len;

    run.player += dir * run.moveSpeed * dt;
    run.player = echo::clampToBounds(run.player, run.playerSize * 0.5f,
                                     level.boundsMin, level.boundsMax);
}

// 玩家 vs 墙：用「上一帧位置(prevPlayer)」判断玩家从哪一侧撞进来，只往回推那一侧，
// 绝不推到另一侧。这是修"卡过去"的关键 —— 旧版的「最小穿入量」解算在撞到又宽又薄的墙时
// 会选错轴（竖直穿透比水平小），导致玩家卡在墙里上下弹跳、还能顺着墙钻出去。
// 贴着墙移动时是滑动（只推来时的那一侧），而不是穿透。留一点 gap 防止贴墙时反复触发。
void wallBlockSystem(RunState& run, const LevelData& level) {
    const float gap = 1.0f;
    const float hx  = run.playerSize.x * 0.5f;
    const float hy  = run.playerSize.y * 0.5f;
    const glm::vec2 prev = run.prevPlayer;

    echo::Rect p = playerRect(run);
    for (const WallDef& w : level.walls) {
        const echo::Rect wall = w.rect;
        if (!echo::overlapRect(p, wall)) continue;

        // 四个候选推出量：把玩家盒子正好移到该侧墙外 + gap（符号即方向）
        const float pushLeft  = (wall.left()  - hx - gap) - run.player.x; // 推左(-x)
        const float pushRight = (wall.right() + hx + gap) - run.player.x; // 推右(+x)
        const float pushUp    = (wall.top()   - hy - gap) - run.player.y; // 推上(-y)
        const float pushDown  = (wall.bottom()+ hy + gap) - run.player.y; // 推下(+y)

        // 依据 prevPlayer（撞进来之前的位置）决定从哪一侧回来
        const bool cameFromLeft  = prev.x <= wall.left()  - hx; // 之前在墙左
        const bool cameFromRight = prev.x >= wall.right() + hx; // 之前在墙右
        const bool cameFromAbove = prev.y <= wall.top()   - hy; // 之前在墙上侧(y 更小)
        const bool cameFromBelow = prev.y >= wall.bottom()+ hy; // 之前在墙下侧(y 更大)

        glm::vec2 push(0.0f, 0.0f);
        if (cameFromLeft)         push = glm::vec2(pushLeft, 0.0f);
        else if (cameFromRight)   push = glm::vec2(pushRight, 0.0f);
        else if (cameFromAbove)   push = glm::vec2(0.0f, pushUp);
        else if (cameFromBelow)   push = glm::vec2(0.0f, pushDown);
        else {
            // 上一帧就已经在墙内（极端情况）：退回最小穿透轴，避免越界或卡死
            float mp = std::fabs(pushLeft);
            push = glm::vec2(pushLeft, 0.0f);
            if (std::fabs(pushRight) < mp) { mp = std::fabs(pushRight); push = glm::vec2(pushRight, 0.0f); }
            if (std::fabs(pushUp)    < mp) { mp = std::fabs(pushUp);    push = glm::vec2(0.0f, pushUp); }
            if (std::fabs(pushDown)  < mp) { mp = std::fabs(pushDown);  push = glm::vec2(0.0f, pushDown); }
        }

        run.player += push;
        run.player = echo::clampToBounds(run.player, run.playerSize * 0.5f,
                                         level.boundsMin, level.boundsMax);
        p = playerRect(run); // 顺序处理多面墙，用更新后的盒子判断下一次
    }
}

void doorBlockSystem(RunState& run, const MetaState& meta, const EcsWorld& world,
                     const LevelData& level) {
    const echo::Rect p = playerRect(run);
    for (const Entity e : world.query<TransformComponent, DoorComponent>()) {
        const DoorComponent* d = world.get<DoorComponent>(e);
        const TransformComponent* t = world.get<TransformComponent>(e);
        if (!d || !t) continue;
        if (d->requiresCode.empty()) continue;
        // M7：门开不开，取决于「本轮有没有拿着对应的钥匙」（死亡会掉落，必须重捡）
        if (run.holdsCode(d->requiresCode)) continue;

        const echo::Rect door = rectOf(*t);
        if (!echo::overlapRect(p, door)) continue;

        // 门是竖着的：从最近的一侧推出去（留缝隙，避免贴着时反复触发）
        const float gap     = 0.5f;
        const float halfX   = p.half().x;
        const float leftPos  = door.left() - halfX - gap;
        const float rightPos = door.right() + halfX + gap;
        const float pick = (std::abs(p.center.x - leftPos) <= std::abs(p.center.x - rightPos))
                               ? leftPos : rightPos;

        run.player.x = pick;
        run.player = echo::clampToBounds(run.player, run.playerSize * 0.5f,
                                         level.boundsMin, level.boundsMax);
    }
}

void pickupSystem(RunState& run, const MetaState& meta, const EcsWorld& world, EventBus& bus) {
    const echo::Rect p = playerRect(run);
    for (const Entity e : world.query<TransformComponent, CoreComponent>()) {
        const CoreComponent* c = world.get<CoreComponent>(e);
        const TransformComponent* t = world.get<TransformComponent>(e);
        if (!c || !t || c->payload.empty()) continue;
        if (!echo::overlapRect(p, rectOf(*t))) continue;

        // M7：本轮拾取的钥匙只记在 Run 层，死亡复活会清空（掉落）→ 必须重捡
        if (!run.holdsCode(c->payload)) {
            run.carriedCodes.push_back(c->payload);
            // 若该钥匙对应某道门，发开门事件（音效 + 记入已通门记忆）
            for (const Entity de : world.query<TransformComponent, DoorComponent>()) {
                const DoorComponent* dd = world.get<DoorComponent>(de);
                if (dd && dd->requiresCode == c->payload)
                    bus.publish(DoorOpenedEvent{dd->id, dd->requiresCode});
            }
        }

        if (meta.knowsPassword(c->payload)) continue; // 永久记忆早已记下，不重复发事件
        bus.publish(PasswordCollectedEvent{c->payload, t->pos});
    }
}

void exitSystem(RunState& run, const MetaState& meta, const EcsWorld& world, EventBus& bus) {
    const echo::Rect p = playerRect(run);
    for (const Entity e : world.query<TransformComponent, ExitComponent>()) {
        const TransformComponent* t = world.get<TransformComponent>(e);
        if (!t) continue;
        if (echo::overlapRect(p, rectOf(*t)))
            bus.publish(LevelEscapedEvent{meta.loops, run.elapsed});
    }
}

void trapSystem(RunState& run, const EcsWorld& world, EventBus& bus) {
    const echo::Rect p = playerRect(run);
    for (const Entity e : world.query<TransformComponent, TrapComponent>()) {
        const TrapComponent* tr = world.get<TrapComponent>(e);
        const TransformComponent* t = world.get<TransformComponent>(e);
        if (!tr || !t) continue;
        if (echo::overlapRect(p, rectOf(*t))) {
            bus.publish(TrapHitEvent{tr->id, t->pos});
            return; // 死一次就够了
        }
    }
}

// ===========================================================================
// M4：巡逻机器人 FSM
// ===========================================================================
void enemySystem(float dt, const RunState& run, const LevelData& level,
                 const MetaState& meta, EcsWorld& world, EventBus& bus) {
    if (!run.alive) return; // 本帧已死：不再动（与 trapSystem 一样，死亡统一帧末收口）

    const glm::vec2 playerPos = run.player;
    const NavGrid   nav = buildNavGrid(level, run); // 每帧构建一次，全敌兵共用

    for (const Entity e : world.query<TransformComponent, EnemyComponent>()) {
        TransformComponent* t  = world.get<TransformComponent>(e);
        EnemyComponent*    en = world.get<EnemyComponent>(e);
        if (!t || !en) continue;

        const float dist = glm::distance(playerPos, t->pos);

        // 接触即死：进入攻击距离立刻致死（无论当前处于什么态），事件收口交给 LoopSystem。
        // 放在状态机之前，保证「从 Patrol 一帧内贴脸」也能立刻死，而非先跳 Alert 等下一帧。
        if (dist <= en->attackRange) {
            bus.publish(EnemyHitEvent{en->id, t->pos});
            en->state = AIState::Attack;
            continue;
        }

        // ---- 状态转移（M5：检测需视线遮挡，隔墙看不见）----
        const AIState before = en->state; // 记下跃迁前状态，用于发布"发现"事件（仅一次）
        switch (en->state) {
        case AIState::Patrol:
        case AIState::Search:
            if (dist <= en->vision && hasLineOfSight(t->pos, playerPos, level, run)) {
                en->state = AIState::Alert;
                en->lastKnownPlayer = playerPos;
            }
            break;
        case AIState::Alert:
            en->lastKnownPlayer = playerPos;
            // 超出视野 或 被墙/门挡住视线 → 失去目标，转入搜索
            if (dist > en->vision || !hasLineOfSight(t->pos, playerPos, level, run)) {
                en->state = AIState::Search;
                en->searchTimer = 3.0f; // 失去视野后搜索 3 秒
            }
            break;
        case AIState::Attack:
            continue; // 已致死，等帧末重置
        }

        // 状态刚从「巡逻 / 搜索」跃迁到「警觉」时，发一次"发现玩家"事件
        // （音效接线在 Game 里；每帧重复发现不需要，所以只在跃迁帧发布）。
        if ((before == AIState::Patrol || before == AIState::Search) &&
            en->state == AIState::Alert) {
            bus.publish(EnemyAlertEvent{en->id, t->pos});
        }

        // ---- 行为：按当前态决定目标点 + 速度 ----
        glm::vec2 target;
        float     spd;
        if (en->state == AIState::Patrol) {
            if (en->patrol.empty()) continue; // 原地不动
            if (en->patrolIndex >= static_cast<int>(en->patrol.size()))
                en->patrolIndex = 0;
            // 先到达判断（推进到下一路点），再取目标 —— 否则会卡在当前路点上不动
            if (glm::distance(t->pos, en->patrol[en->patrolIndex]) < 4.0f)
                advancePatrol(*en);
            target = en->patrol[en->patrolIndex];
            spd    = en->speed;
        } else {
            // Alert / Attack / Search 都沿 A* 路径（M5：绕墙而非直线穿墙）
            const glm::vec2 goal = (en->state == AIState::Search)
                                      ? en->lastKnownPlayer : playerPos;

            // 节流：计时到 或 目标所在格变化 才重算路径，避免每帧重算
            const auto [gc, gr] = nav.worldToCell(goal);
            const bool goalMoved = (gc != en->lastGoalCellX) || (gr != en->lastGoalCellY);
            en->pathTimer -= dt;
            if (en->pathTimer <= 0.0f || goalMoved || en->path.empty()) {
                en->path = nav.findPath(t->pos, goal);
                en->pathIndex = 0;
                en->pathTimer = 0.3f;
                en->lastGoalCellX = gc;
                en->lastGoalCellY = gr;
            }

            // 跟随路径：取当前航点，靠近就推进到下一个
            if (!en->path.empty() && en->pathIndex < static_cast<int>(en->path.size())) {
                target = en->path[en->pathIndex];
                const float reach = nav.cell * 0.6f;
                if (glm::distance(t->pos, target) < reach) {
                    ++en->pathIndex;
                    if (en->pathIndex < static_cast<int>(en->path.size()))
                        target = en->path[en->pathIndex];
                }
            } else {
                target = goal; // 无路径（被完全封死）：退回直线追击，靠安全网兜底
            }
            spd = en->alertSpeed;
        }

        // 朝目标移动（spd 已是标量速度，方向归一化）
        const glm::vec2 prev = t->pos;
        const glm::vec2 dir = target - t->pos;
        const float     len = glm::length(dir);
        if (len > 0.0001f) t->pos += (dir / len) * spd * dt;

        // M5 安全网：若这一步撞进了障碍（墙 / 未开的门），回退到移动前的位置
        if (pointInObstacle(t->pos, level, run)) t->pos = prev;

        // 夹在边界内
        t->pos = echo::clampToBounds(t->pos, t->size * 0.5f,
                                     level.boundsMin, level.boundsMax);
    }
}

// 新一轮开始：敌兵归位 + FSM 复位（LoopStartedEvent 触发，切关复用）
void resetEnemies(EcsWorld& world) {
    for (const Entity e : world.query<TransformComponent, EnemyComponent>()) {
        TransformComponent* t  = world.get<TransformComponent>(e);
        EnemyComponent*    en = world.get<EnemyComponent>(e);
        if (!t || !en) continue;
        const glm::vec2 spawn = en->patrol.empty() ? t->pos : en->patrol[0];
        t->pos            = spawn;
        en->state         = AIState::Patrol;
        en->patrolIndex   = 0;
        en->patrolDir     = 1;
        en->searchTimer   = 0.0f;
        en->lastKnownPlayer = spawn;
        // M5：导航运行时字段一并归零
        en->path.clear();
        en->pathIndex  = 0;
        en->pathTimer  = 0.0f;
        en->lastGoalCellX = -1;
        en->lastGoalCellY = -1;
    }
}

// ===========================================================================
// M5：导航网格 + 视线遮挡
// ===========================================================================

bool NavGrid::inBounds(int c, int r) const {
    return c >= 0 && c < cols && r >= 0 && r < rows;
}

bool NavGrid::isSolid(int c, int r) const {
    if (!inBounds(c, r)) return true; // 越界按实心，防止走出网格
    return solid[indexOf(c, r)];
}

glm::vec2 NavGrid::cellCenter(int c, int r) const {
    return min + glm::vec2((c + 0.5f) * cell, (r + 0.5f) * cell);
}

std::pair<int, int> NavGrid::worldToCell(const glm::vec2& p) const {
    const int c = static_cast<int>(std::floor((p.x - min.x) / cell));
    const int r = static_cast<int>(std::floor((p.y - min.y) / cell));
    return {c, r};
}

int NavGrid::indexOf(int c, int r) const { return r * cols + c; }

NavGrid buildNavGrid(const LevelData& level, const RunState& run, float agentRadius) {
    NavGrid g;
    g.min  = level.boundsMin;
    g.max  = level.boundsMax;
    g.cell = 40.0f;

    const float w = g.max.x - g.min.x;
    const float h = g.max.y - g.min.y;
    if (w <= 0.0f || h <= 0.0f) return g; // 退化 bounds：返回空网格

    g.cols = static_cast<int>(std::ceil(w / g.cell));
    g.rows = static_cast<int>(std::ceil(h / g.cell));
    if (g.cols < 1) g.cols = 1;
    if (g.rows < 1) g.rows = 1;
    g.solid.assign(static_cast<size_t>(g.cols) * g.rows, false);

    // 收集障碍矩形：所有墙 + 未记住密码的门
    std::vector<Rect> obstacles;
    obstacles.reserve(level.walls.size() + level.doors.size());
    for (const WallDef& wdef : level.walls) obstacles.push_back(wdef.rect);
    for (const DoorDef& ddef : level.doors) {
        if (ddef.requiresCode.empty()) continue;          // 无密码门永远开
        if (run.holdsCode(ddef.requiresCode)) continue;   // M7：本轮持有对应钥匙 → 开
        obstacles.push_back(ddef.rect);
    }

    // 按 agentRadius 外扩每个格子，与障碍求交即标记实心（保证路径与墙留余量）
    const float pad = agentRadius;
    for (int r = 0; r < g.rows; ++r) {
        for (int c = 0; c < g.cols; ++c) {
            const glm::vec2 ctr = g.cellCenter(c, r);
            const Rect cellRect{ctr, glm::vec2(g.cell, g.cell)};
            const Rect expanded{ctr, glm::vec2(g.cell + 2.0f * pad, g.cell + 2.0f * pad)};
            bool solid = false;
            for (const Rect& ob : obstacles) {
                if (echo::overlapRect(expanded, ob)) { solid = true; break; }
            }
            g.solid[g.indexOf(c, r)] = solid;
        }
    }
    return g;
}

namespace {
// 环形 BFS 找离 (c,r) 最近的非实心格（起/终点落在墙里时回退用）
std::pair<int, int> nearestFreeCell(const NavGrid& g, int c, int r, int maxRing) {
    c = std::max(0, std::min(g.cols - 1, c));
    r = std::max(0, std::min(g.rows - 1, r));
    if (!g.isSolid(c, r)) return {c, r};
    for (int ring = 1; ring <= maxRing; ++ring) {
        for (int dr = -ring; dr <= ring; ++dr) {
            for (int dc = -ring; dc <= ring; ++dc) {
                if (std::abs(dr) != ring && std::abs(dc) != ring) continue; // 只查环
                const int nc = c + dc, nr = r + dr;
                if (g.inBounds(nc, nr) && !g.isSolid(nc, nr)) return {nc, nr};
            }
        }
    }
    return {-1, -1};
}
} // namespace

std::vector<glm::vec2> NavGrid::findPath(const glm::vec2& from, const glm::vec2& to) const {
    if (cols <= 0 || rows <= 0) return {to}; // 退化网格：退回直线

    auto [sc, sr] = nearestFreeCell(*this, worldToCell(from).first,  worldToCell(from).second,  8);
    auto [gc, gr] = nearestFreeCell(*this, worldToCell(to).first,    worldToCell(to).second,    8);
    if (sc < 0 || gc < 0) return {to}; // 找不到落脚格：退回直线（配合 pointInObstacle 安全网）

    const int start = indexOf(sc, sr);
    const int goal  = indexOf(gc, gr);

    const int N = cols * rows;
    std::vector<float>  gScore(N, std::numeric_limits<float>::max());
    std::vector<int>    came(N, -1);
    std::vector<uint8_t> closed(N, 0);

    // 八方向（含对角），对角移动禁止穿角
    static const int dcs[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int drs[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

    auto heuristic = [&](int c, int r) -> float {
        const int dx = std::abs(c - gc), dy = std::abs(r - gr);
        const float octile = static_cast<float>(dx + dy)
                           + (1.41421356f - 2.0f) * static_cast<float>(std::min(dx, dy));
        return octile * cell;
    };

    gScore[start] = 0.0f;
    // 最小堆：(-f, cellIndex) 用 greater 取最小
    std::priority_queue<std::pair<float, int>,
                        std::vector<std::pair<float, int>>,
                        std::greater<std::pair<float, int>>> open;
    open.push({heuristic(sc, sr), start});

    while (!open.empty()) {
        const auto top = open.top(); open.pop();
        const int cur = top.second;
        if (closed[cur]) continue;
        closed[cur] = 1;
        if (cur == goal) break;

        const int cc = cur % cols;
        const int cr = cur / cols;
        for (int i = 0; i < 8; ++i) {
            const int nc = cc + dcs[i], nr = cr + drs[i];
            if (isSolid(nc, nr)) continue;
            if (dcs[i] != 0 && drs[i] != 0) { // 对角：两侧正交格都不能是实心
                if (isSolid(cc + dcs[i], cr) || isSolid(cc, cr + drs[i])) continue;
            }
            const int ni = indexOf(nc, nr);
            if (closed[ni]) continue;
            const float step = (dcs[i] != 0 && drs[i] != 0) ? cell * 1.41421356f : cell;
            const float tg = gScore[cur] + step;
            if (tg < gScore[ni]) {
                gScore[ni] = tg;
                came[ni]   = cur;
                open.push({(tg + heuristic(nc, nr)), ni});
            }
        }
    }

    if (came[goal] < 0 && goal != start) return {to}; // 不可达：退回直线

    // 回溯
    std::vector<std::pair<int, int>> cells;
    for (int cur = goal; cur != -1; cur = came[cur]) cells.push_back({cur % cols, cur / cols});
    std::reverse(cells.begin(), cells.end());

    // 转世界坐标（格子中心）；保留全部相邻航点，保证相邻格间不走捷径穿墙，
    // 末尾追加真实目标点（让敌兵精确到达玩家所在位置）。
    std::vector<glm::vec2> path;
    path.reserve(cells.size() + 1);
    for (const auto& cell : cells) path.push_back(cellCenter(cell.first, cell.second));
    path.push_back(to);
    return path;
}

bool segmentIntersectsRect(const glm::vec2& a, const glm::vec2& b, const Rect& r) {
    // Liang–Barsky：线段 (a→b) 与 AABB [r.left, r.right]×[r.top, r.bottom] 求交
    const float xmin = r.left(), xmax = r.right();
    const float ymin = r.top(),  ymax = r.bottom();
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;

    float t0 = 0.0f, t1 = 1.0f;
    const auto clip = [&](float p, float q) -> bool {
        if (p == 0.0f) {
            if (q < 0.0f) return false; // 平行且在窗外
            return true;
        }
        const float rr = q / p;
        if (p < 0.0f) {
            if (rr > t1) return false;
            if (rr > t0) t0 = rr;
        } else {
            if (rr < t0) return false;
            if (rr < t1) t1 = rr;
        }
        return true;
    };
    if (!clip(-dx, a.x - xmin)) return false;
    if (!clip( dx, xmax - a.x)) return false;
    if (!clip(-dy, a.y - ymin)) return false;
    if (!clip( dy, ymax - a.y)) return false;
    return t0 <= t1;
}

bool hasLineOfSight(const glm::vec2& a, const glm::vec2& b,
                    const LevelData& level, const RunState& run) {
    for (const WallDef& w : level.walls)
        if (segmentIntersectsRect(a, b, w.rect)) return false;
    for (const DoorDef& d : level.doors) {
        if (d.requiresCode.empty()) continue;
        if (run.holdsCode(d.requiresCode)) continue; // M7：持有钥匙 → 门透明
        if (segmentIntersectsRect(a, b, d.rect)) return false;
    }
    return true;
}

bool pointInObstacle(const glm::vec2& p, const LevelData& level, const RunState& run) {
    for (const WallDef& w : level.walls)
        if (echo::pointInRect(p, w.rect)) return true;
    for (const DoorDef& d : level.doors) {
        if (d.requiresCode.empty()) continue;
        if (run.holdsCode(d.requiresCode)) continue; // M7：持有钥匙 → 门不挡
        if (echo::pointInRect(p, d.rect)) return true;
    }
    return false;
}

} // namespace echo
