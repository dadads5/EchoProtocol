// test_nav.cpp —— M5 单元测试（Catch2 v3）：视线遮挡 + A* 寻路 + 条件门
//
// 覆盖：
//   ① 线段 vs AABB 求交（穿插 / 端点在内 / 完全错过）
//   ② 视线遮挡：中间隔一堵墙 → 看不见；墙移开 → 看得见
//   ③ A* 绕墙：正前方一堵挡墙但留上下缺口 → 路径存在、不穿墙、比直线长
//   ④ 敌兵「只在 LOS 清晰时发现」：墙挡视线 → 保持 Patrol；移开墙 → Alert
//   ⑤ 敌兵绕墙逼近（Search）：多帧后抵达目标，且全程不进入障碍
//   ⑥ 条件门：关着挡 LOS+nav；记住密码后打通
//   ⑦ 真实关卡可导航：level01 门全开时 spawn→exit 可达
//   ⑧ resetEnemies 清空 path
//
// 不变量：本文件与 test_gamestate.cpp(16) / test_enemy.cpp(7) 独立；M5 仅给 enemySystem
//         多一个 meta 形参，旧用例已同步补 meta 实参，契约零破坏。
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "core/EventBus.h"
#include "core/Events.h"
#include "ecs/World.h"
#include "game/GameState.h"
#include "game/LevelLoader.h"
#include "game/systems/LoopSystem.h"
#include "game/systems/Systems.h"

using namespace echo;

namespace {
// 造一个无障碍的小关（程序化，确定性）
LevelData openLevel() {
    LevelData lv;
    lv.boundsMin = glm::vec2(-500.0f, -500.0f);
    lv.boundsMax = glm::vec2( 500.0f,  500.0f);
    lv.spawn     = glm::vec2(-400.0f, 0.0f);
    return lv;
}

// 单敌兵实体（Transform + EnemyComponent），出生在 pos
Entity spawnEnemy(EcsWorld& world, const glm::vec2& pos) {
    Entity e = world.createEntity();
    TransformComponent t;
    t.pos = pos;
    t.size = glm::vec2(30.0f, 30.0f);
    world.add(e, t);
    EnemyComponent en;
    en.id = 1;
    en.vision = 400.0f;
    en.alertSpeed = 200.0f;
    en.speed = 90.0f;
    en.attackRange = 34.0f;
    world.add(e, en);
    return e;
}

bool near(const glm::vec2& a, const glm::vec2& b, float eps = 1.0f) {
    return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps;
}
} // namespace

// ===========================================================================
// ① 线段 vs AABB
// ===========================================================================
TEST_CASE("segmentIntersectsRect basic cases", "[nav][los]") {
    const Rect r{glm::vec2(0.0f, 0.0f), glm::vec2(100.0f, 100.0f)}; // [-50,50]^2
    CHECK(segmentIntersectsRect(glm::vec2(-100.0f, 0.0f), glm::vec2(100.0f, 0.0f), r)); // 横穿
    CHECK(segmentIntersectsRect(glm::vec2(0.0f, 0.0f),   glm::vec2(200.0f, 0.0f), r)); // 端点在内
    CHECK_FALSE(segmentIntersectsRect(glm::vec2(-200.0f, 0.0f), glm::vec2(-100.0f, 0.0f), r)); // 完全在左外
    CHECK_FALSE(segmentIntersectsRect(glm::vec2(0.0f, -200.0f), glm::vec2(0.0f, -100.0f), r));   // 完全在下外
}

// ===========================================================================
// ② 视线遮挡
// ===========================================================================
TEST_CASE("hasLineOfSight blocked by a wall between", "[nav][los]") {
    LevelData lv = openLevel();
    lv.walls.push_back(WallDef{"w", Rect{glm::vec2(0.0f, 0.0f), glm::vec2(40.0f, 600.0f)}});

    const glm::vec2 a(-200.0f, 0.0f), b(200.0f, 0.0f);
    CHECK_FALSE(hasLineOfSight(a, b, lv, RunState{}));      // 墙挡着
    CHECK(hasLineOfSight(glm::vec2(-200.0f, 0.0f), glm::vec2(-100.0f, 0.0f), lv, RunState{})); // 同侧

    lv.walls.clear();
    CHECK(hasLineOfSight(a, b, lv, RunState{}));            // 没墙了 → 通
}

// ===========================================================================
// ③ A* 绕墙（墙留上下缺口）
// ===========================================================================
TEST_CASE("A* routes around a wall with a gap", "[nav][astar]") {
    LevelData lv = openLevel();
    const Rect wall{glm::vec2(0.0f, 0.0f), glm::vec2(40.0f, 600.0f)}; // y∈[-300,300]，上下各留缺口
    lv.walls.push_back(WallDef{"w", wall});

    const     NavGrid nav = buildNavGrid(lv, RunState{});
    REQUIRE(nav.cols > 0);
    REQUIRE(nav.rows > 0);

    const glm::vec2 from(-200.0f, 0.0f), to(200.0f, 0.0f);
    std::vector<glm::vec2> path = nav.findPath(from, to);

    REQUIRE(path.size() >= 2);
    CHECK(near(path.back(), to, 1.0f)); // 末尾是真实目标点

    // 航点都不落在墙内（路径与墙留余量）
    for (const glm::vec2& w : path)
        CHECK_FALSE(pointInRect(w, wall));

    // 相邻航点距离不超过一格对角（路径连续）
    for (size_t i = 1; i < path.size(); ++i) {
        const float seg = glm::distance(path[i - 1], path[i]);
        CHECK(seg < nav.cell * 1.5f + 1.0f);
    }

    // 必须绕路：总路径长度明显长于直线 400
    float total = 0.0f;
    for (size_t i = 1; i < path.size(); ++i)
        total += glm::distance(path[i - 1], path[i]);
    CHECK(total > 450.0f);
}

// ===========================================================================
// ④ 敌兵只在 LOS 清晰时才发现玩家
// ===========================================================================
TEST_CASE("enemy only alerts when line of sight is clear", "[nav][enemy][los]") {
    // 有墙挡在敌兵与玩家之间
    LevelData lv = openLevel();
    lv.walls.push_back(WallDef{"w", Rect{glm::vec2(0.0f, 0.0f), glm::vec2(40.0f, 600.0f)}});

    EcsWorld world;
    spawnEnemy(world, glm::vec2(-200.0f, 0.0f));

    RunState run;
    run.player = glm::vec2(200.0f, 0.0f); // 距离 400 == vision，但中间有墙
    run.alive  = true;
    EventBus bus;
    MetaState meta;

    enemySystem(0.1f, run, lv, meta, world, bus);
    // 找到敌兵实体
    Entity e1 = kNullEntity;
    for (const Entity e : world.query<TransformComponent, EnemyComponent>()) e1 = e;
    REQUIRE(e1 != kNullEntity);
    CHECK(world.get<EnemyComponent>(e1)->state == AIState::Patrol); // 被墙挡住 → 看不见

    // 移开墙 → 这一帧就能发现
    lv.walls.clear();
    enemySystem(0.1f, run, lv, meta, world, bus);
    CHECK(world.get<EnemyComponent>(e1)->state == AIState::Alert);
}

// ===========================================================================
// ⑤ 敌兵绕墙逼近（Search 态路径跟随 + 安全网）
// ===========================================================================
TEST_CASE("enemy paths around a wall to reach goal and never enters obstacle",
          "[nav][enemy][astar]") {
    LevelData lv = openLevel();
    const Rect wall{glm::vec2(0.0f, 0.0f), glm::vec2(40.0f, 600.0f)}; // 上下缺口
    lv.walls.push_back(WallDef{"w", wall});

    EcsWorld world;
    Entity e1 = spawnEnemy(world, glm::vec2(-200.0f, 0.0f));
    EnemyComponent* en = world.get<EnemyComponent>(e1);
    TransformComponent* t = world.get<TransformComponent>(e1);
    REQUIRE(en != nullptr);
    REQUIRE(t != nullptr);

    const glm::vec2 goal(200.0f, 0.0f);
    en->state = AIState::Search;            // 直接进入搜索态，朝 lastKnownPlayer 绕墙走
    en->lastKnownPlayer = goal;
    en->searchTimer = 3.0f;

    RunState run;
    run.player = goal;   // 玩家就在目标处；vision=100<距离400 → 保持 Search
    run.alive  = true;
    EventBus bus;
    MetaState meta;

    const float startDist = glm::distance(t->pos, goal);
    bool everInside = false;
    for (int i = 0; i < 200; ++i) {
        enemySystem(0.05f, run, lv, meta, world, bus);
        if (pointInObstacle(t->pos, lv, RunState{})) everInside = true;
        if (glm::distance(t->pos, goal) < 40.0f) break; // 抵达
    }
    CHECK_FALSE(everInside);                              // 从未穿墙
    CHECK(glm::distance(t->pos, goal) < 60.0f);          // 成功绕到目标附近
    CHECK(glm::distance(t->pos, goal) < startDist);       // 确实在接近
}

// ===========================================================================
// ⑥ 条件门：关着挡 LOS+nav；本轮拿着钥匙后打通
// ===========================================================================
TEST_CASE("closed door blocks LOS and nav; carried key opens it",
          "[nav][enemy][door]") {
    LevelData lv = openLevel();
    lv.doors.push_back(DoorDef{"gate", "舱门", "AX-7",
                               Rect{glm::vec2(0.0f, 0.0f), glm::vec2(40.0f, 1000.0f)}}); // 整高，关门即封死

    const glm::vec2 a(-200.0f, 0.0f), b(200.0f, 0.0f);
    RunState closed;
    CHECK_FALSE(hasLineOfSight(a, b, lv, closed));         // 没拿钥匙 → 关门挡视线
    {
        NavGrid g = buildNavGrid(lv, closed);
        const auto [c, r] = g.worldToCell(glm::vec2(0.0f, 0.0f));
        CHECK(g.isSolid(c, r));                            // 门所在格为实心
    }

    RunState opened;
    opened.carriedCodes.push_back("AX-7");
    CHECK(hasLineOfSight(a, b, lv, opened));               // 拿钥匙 → 开门通视线
    {
        NavGrid g = buildNavGrid(lv, opened);
        const auto [c, r] = g.worldToCell(glm::vec2(0.0f, 0.0f));
        CHECK_FALSE(g.isSolid(c, r));                      // 门开了 → 该格可走
        std::vector<glm::vec2> path = g.findPath(a, b);
        REQUIRE(path.size() >= 2);
        CHECK(near(path.back(), b, 1.0f));
    }
}

// ===========================================================================
// ⑦ 真实关卡可导航（门全开（本轮持钥匙）后 spawn → exit 可达）
// ===========================================================================
TEST_CASE("level01 is navigable once its doors' keys are carried", "[nav][integration]") {
    LevelData lv;
    std::string err;
    REQUIRE(loadLevelFile("assets/levels/level01.json", lv, &err));

    RunState empty;
    NavGrid gEmpty = buildNavGrid(lv, empty);
    REQUIRE(gEmpty.cols > 0);
    REQUIRE(gEmpty.rows > 0);
    // 网格里应既有实心（墙/门）也有空心
    bool anyFree = false, anySolid = false;
    for (bool s : gEmpty.solid) { if (s) anySolid = true; else anyFree = true; }
    CHECK(anyFree);
    CHECK(anySolid);

    // 本轮拿着两道门的钥匙 → 全关连通
    RunState opened;
    opened.carriedCodes.push_back("AX-7");
    opened.carriedCodes.push_back("Q9-T");
    NavGrid gOpen = buildNavGrid(lv, opened);
    std::vector<glm::vec2> path = gOpen.findPath(lv.spawn, lv.exitRect.center);
    REQUIRE(path.size() >= 2);
    CHECK(near(path.back(), lv.exitRect.center, 1.0f));
}

// ===========================================================================
// ⑧ resetEnemies 清空 path
// ===========================================================================
TEST_CASE("resetEnemies clears the navigation path", "[nav][enemy][reset]") {
    LevelData lv = openLevel(); // 无障碍，敌兵进 Alert 后会算路径
    EcsWorld world;
    Entity e1 = spawnEnemy(world, glm::vec2(-200.0f, 0.0f));
    EnemyComponent* en = world.get<EnemyComponent>(e1);
    REQUIRE(en);

    RunState run;
    run.player = glm::vec2(100.0f, 0.0f); // 在视野内、LOS 通 → 进 Alert 并算路径
    run.alive  = true;
    EventBus bus;
    MetaState meta;

    enemySystem(0.1f, run, lv, meta, world, bus);
    CHECK(en->state == AIState::Alert);
    CHECK_FALSE(en->path.empty());          // 已经算出了路径

    resetEnemies(world);
    CHECK(en->state == AIState::Patrol);
    CHECK(en->path.empty());
    CHECK(en->pathTimer == 0.0f);
    CHECK(en->lastGoalCellX == -1);
}

// ===========================================================================
// ⑨ 玩家 vs 墙碰撞（Level 3 岔路走廊靠它成立）
// ===========================================================================
TEST_CASE("wallBlockSystem pushes the player out of any solid wall", "[wall][collision]") {
    LevelData lv = openLevel();
    // 竖直墙在 x=0，厚 40、高 600（y∈[-300,300]）
    lv.walls.push_back(WallDef{"w", Rect{glm::vec2(0.0f, 0.0f), glm::vec2(40.0f, 600.0f)}});

    RunState run;
    run.playerSize = glm::vec2(26.0f, 26.0f);

    // 玩家中心正好嵌在墙里（从左侧撞进来）→ 必须被推出到墙外（不再相交）
    run.player    = glm::vec2(0.0f, 0.0f);
    run.prevPlayer = glm::vec2(-100.0f, 0.0f);
    wallBlockSystem(run, lv);
    {
        Rect p; p.center = run.player; p.size = run.playerSize;
        CHECK_FALSE(overlapRect(p, lv.walls[0].rect));
    }

    // 不重叠时不应改动玩家位置（纯 no-op）
    run.player = glm::vec2(-300.0f, 0.0f);
    const glm::vec2 before = run.player;
    wallBlockSystem(run, lv);
    CHECK(near(run.player, before));
}

TEST_CASE("wallBlockSystem lets the player slide along a wall", "[wall][collision]") {
    LevelData lv = openLevel();
    // 竖直墙在 x=0，与玩家左边缘只差一点点（玩家从左侧贴上来）
    lv.walls.push_back(WallDef{"w", Rect{glm::vec2(0.0f, 0.0f), glm::vec2(40.0f, 600.0f)}});

    RunState run;
    run.playerSize = glm::vec2(26.0f, 26.0f);
    // 玩家从左侧贴上竖直墙：prevPlayer 在墙左，碰撞应当只沿 X 推回左侧、Y 不变
    run.prevPlayer = glm::vec2(-100.0f, 50.0f);
    // 玩家右边缘恰好压到墙左面（x = -20 - 13 = -33 < 墙左 -20？墙左 = -20，玩家右 = x+13）
    // 让玩家右边缘嵌入墙 5 个单位：x = -20 - 13 + 5 = -28
    run.player = glm::vec2(-28.0f, 50.0f);
    wallBlockSystem(run, lv);
    // 应只被沿 X 推回左侧，Y 不变（贴墙滑动）
    CHECK(run.player.y == 50.0f);
    {
        Rect p; p.center = run.player; p.size = run.playerSize;
        CHECK_FALSE(overlapRect(p, lv.walls[0].rect));
        CHECK(run.player.x < -20.0f); // 推回到墙左侧
    }
}

TEST_CASE("wallBlockSystem never pushes the player out of bounds", "[wall][collision]") {
    LevelData lv = openLevel();
    // 一面贴着下边界的厚墙（模拟 level01 的 wall_bottom 那种边界墙）
    lv.walls.push_back(WallDef{"w", Rect{glm::vec2(0.0f, -470.0f), glm::vec2(1000.0f, 60.0f)}});

    RunState run;
    run.playerSize = glm::vec2(26.0f, 26.0f);
    // 玩家贴着下边界（bounds 下沿 -500，玩家中心最低 -500+13 = -487），正好顶到墙
    // 上一帧在墙下方 → 应被向下（朝边界）推回，且仍停在边界内
    run.prevPlayer = glm::vec2(0.0f, -600.0f);
    run.player = glm::vec2(0.0f, -487.0f);
    wallBlockSystem(run, lv);
    // 推回后仍在边界内（不会越界）
    CHECK(run.player.y >= lv.boundsMin.y + run.playerSize.y * 0.5f - 0.5f);
    CHECK(run.player.y <= lv.boundsMax.y - run.playerSize.y * 0.5f + 0.5f);
}

TEST_CASE("wallBlockSystem blocks the player at a wide-thin wall (no clip-through)", "[wall][regression]") {
    // 回归：又宽又薄的墙（如 level03 的 tube_top：宽 450、高 30）。旧版"最小穿入量"
    // 解算会选错轴，玩家卡在墙里上下弹跳、还能顺着墙钻出去。
    LevelData lv = openLevel();
    lv.walls.push_back(WallDef{"w", Rect{glm::vec2(-405.0f, 85.0f), glm::vec2(450.0f, 30.0f)}});

    RunState run;
    run.playerSize = glm::vec2(26.0f, 26.0f);
    run.player = glm::vec2(-400.0f, 0.0f); // 从墙下方出发，向上顶

    const echo::Rect wall = lv.walls[0].rect;
    for (int i = 0; i < 600; ++i) {
        echo::InputFrame in;
        in.up = true;                 // 持续向上顶墙
        movementSystem(1.0f / 60.0f, in, run, lv);
        wallBlockSystem(run, lv);
        // 任何一帧都不能卡进墙里
        echo::Rect pr; pr.center = run.player; pr.size = run.playerSize;
        CHECK_FALSE(echo::overlapRect(pr, wall));
    }
    // 被挡在墙下方（墙顶 y=70，玩家应停在 y<=70 一侧）
    CHECK(run.player.y < 70.0f);
}

// ===========================================================================
// ⑩ 真实关卡：level03（回声岔路）门全开（本轮持钥匙）后 spawn→exit 可达
// ===========================================================================
TEST_CASE("level03 fork is navigable when its gates' keys are carried", "[nav][integration]") {
    LevelData lv;
    std::string err;
    REQUIRE(loadLevelFile("assets/levels/level03.json", lv, &err));

    // 本轮拿着两道闸门钥匙 → 全图连通，中央枢纽走下行支路可到出口
    RunState meta;
    meta.carriedCodes.push_back("K3-V");
    meta.carriedCodes.push_back("K3-X");

    NavGrid g = buildNavGrid(lv, meta);
    REQUIRE(g.cols > 0);
    REQUIRE(g.rows > 0);

    std::vector<glm::vec2> path = g.findPath(lv.spawn, lv.exitRect.center);
    REQUIRE(path.size() >= 2);
    CHECK(near(path.back(), lv.exitRect.center, 1.0f));
}

TEST_CASE("level03 live path needs only K3-V, not the decoy K3-X", "[nav][integration]") {
    LevelData lv;
    std::string err;
    REQUIRE(loadLevelFile("assets/levels/level03.json", lv, &err));

    // 只拿下行活路的 K3-V；上行诱饵 K3-X 不拿 → 上行闸门关着。
    // 活路本就不经过上行闸门，因此 spawn -> exit 仍应可达。
    RunState meta;
    meta.carriedCodes.push_back("K3-V");

    NavGrid g = buildNavGrid(lv, meta);
    REQUIRE(g.cols > 0);
    std::vector<glm::vec2> path = g.findPath(lv.spawn, lv.exitRect.center);
    REQUIRE(path.size() >= 2);
    CHECK(near(path.back(), lv.exitRect.center, 1.0f));
}

// ===========================================================================
// ⑪ 无敌模式：LoopSystem 在开启时吞掉一切死因（陷阱 / 敌兵 / 超时）
// ===========================================================================
TEST_CASE("god mode keeps the player alive through any death cause", "[loop][godmode]") {
    EventBus bus;
    GameState gs;
    gs.run.alive = true;
    LoopSystem loop(gs, bus);

    // 默认（无敌关）：超时会致死
    loop.timeOut();
    CHECK_FALSE(gs.run.alive);

    // 重新活过来，开启无敌
    gs.run.alive = true;
    loop.setGodMode(true);
    CHECK(loop.godMode());

    // 超时（倒计时结束）也不死 —— 所有死因都收口在 killPlayer，被这里拦下
    loop.timeOut();
    CHECK(gs.run.alive);

    // 关闭无敌后，死因恢复生效
    loop.setGodMode(false);
    loop.timeOut();
    CHECK_FALSE(gs.run.alive);
}

// ⑫ 无敌模式：逃脱时仍可过关，但不记入通关记录（escapes / bestTime 不更新）
TEST_CASE("god mode escape is not recorded into clear stats", "[loop][godmode]") {
    EventBus bus;
    GameState gs;
    LoopSystem loop(gs, bus);
    loop.subscribe();   // 注册事件处理（onLevelEscaped 等）

    auto nearF = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };

    // 正常通关：escapes +1，bestTime 写入
    bus.publish(LevelEscapedEvent{1, 12.5f});
    CHECK(gs.meta.escapes == 1);
    CHECK(nearF(gs.meta.bestTime, 12.5f));

    // 更快的通关刷新 bestTime
    bus.publish(LevelEscapedEvent{2, 9.0f});
    CHECK(gs.meta.escapes == 2);
    CHECK(nearF(gs.meta.bestTime, 9.0f));

    // 开启无敌后逃脱：不记入（即使更快也不刷新 bestTime）
    loop.setGodMode(true);
    bus.publish(LevelEscapedEvent{3, 3.0f});
    CHECK(gs.meta.escapes == 2);                    // 没累加
    CHECK(nearF(gs.meta.bestTime, 9.0f));            // 没刷新

    // 关闭无敌后恢复记录
    loop.setGodMode(false);
    bus.publish(LevelEscapedEvent{4, 20.0f});
    CHECK(gs.meta.escapes == 3);
    CHECK(nearF(gs.meta.bestTime, 9.0f));            // 更慢，不刷新
}
