// test_enemy.cpp —— M4 单元测试（Catch2 v3）：敌兵 AI（FSM 巡逻机器人）
//
// 覆盖：
//   ① 关卡 JSON 解析 enemies[]（路点 / 速度 / 视野 / 缺 id 自动编号）
//   ② 巡逻：玩家远离时敌人沿路点移动且保持 Patrol
//   ③ 视野发现：玩家进入 vision 半径 -> Alert
//   ④ 接触致死：玩家贴脸 -> 发 EnemyHitEvent（致死收口交给 LoopSystem，这里只测事件）
//   ⑤ 重置归位：resetEnemies 把敌人复位到 patrol[0] 且 state=Patrol
//   ⑥ 端到端：被敌人 #k 杀死 -> 永久记住 -> 下一轮敌兵归位 -> 落盘还原记忆
//   ⑦ 真实关卡（level01 / level02）确实包含敌兵
//
// 不变量：本文件的新增测试与 test_gamestate.cpp 的 16 用例彼此独立；
//         新增的 MetaState.knownEnemyIds 字段用 j.contains 守卫读取，旧存档零影响。
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "core/EventBus.h"
#include "core/Events.h"
#include "ecs/World.h"
#include "game/Game.h"
#include "game/GameState.h"
#include "game/LevelLoader.h"
#include "game/SaveSystem.h"
#include "game/SceneBuilder.h"
#include "game/systems/Systems.h"

using namespace echo;
using namespace std::string_literals;

// 一个带两名敌兵、无陷阱/门的迷你关：用来稳定地测试 FSM 与解析。
// 敌人 1 在 x 轴横向巡逻；敌人 2 没写 id（测试自动编号兜底）。
const char* kEnemyLevelJson = R"JSON({
  "name": "enemy-test",
  "loopSeconds": 60.0,
  "spawn": [-800, 0],
  "bounds": { "min": [-1000, -300], "max": [1000, 300] },
  "exit": { "center": [900, 0], "size": [40, 120] },
  "enemies": [
    { "id": 1, "patrol": [[-300, 0], [300, 0]],
      "speed": 100.0, "vision": 150.0, "alertSpeed": 200.0, "attackRange": 34.0 },
    { "patrol": [[0, 100], [0, -100]],
      "speed": 80.0, "vision": 120.0 }
  ]
})JSON";

// 一个玩家出生点就压在敌人上的关：第一帧即被接触致死，用来测「记忆 + 落盘」。
const char* kEnemyKillJson = R"JSON({
  "name": "enemy-kill",
  "loopSeconds": 60.0,
  "spawn": [0, 0],
  "bounds": { "min": [-500, -300], "max": [500, 300] },
  "exit": { "center": [450, 0], "size": [40, 120] },
  "enemies": [
    { "id": 7, "patrol": [[0, 0], [200, 0]],
      "speed": 50.0, "vision": 400.0, "alertSpeed": 100.0, "attackRange": 34.0 }
  ]
})JSON";

namespace {
// 解析 + 展开成 ECS，返回关卡数据与世界（调用方可继续填 Run）
void buildEnemyWorld(const char* json, LevelData& lv, EcsWorld& world, std::string& err) {
    REQUIRE(parseLevelJson(json, lv, &err));
    buildScene(lv, world);
}
}

// ===========================================================================
// ① 解析
// ===========================================================================
TEST_CASE("enemy JSON becomes EnemyDef with patrol and auto id", "[enemy][level]") {
    LevelData lv;
    std::string err;
    REQUIRE(parseLevelJson(kEnemyLevelJson, lv, &err));

    REQUIRE(lv.enemies.size() == 2);
    CHECK(lv.enemies[0].id == 1);
    REQUIRE(lv.enemies[0].patrol.size() == 2);
    CHECK(lv.enemies[0].patrol[0] == glm::vec2(-300.0f, 0.0f));
    CHECK(lv.enemies[0].patrol[1] == glm::vec2(300.0f, 0.0f));
    CHECK(lv.enemies[0].speed == 100.0f);
    CHECK(lv.enemies[0].vision == 150.0f);

    // 没写 id 的敌人按出现顺序自动编号（与 trap 一致）
    CHECK(lv.enemies[1].id == 2);
    REQUIRE(lv.enemies[1].patrol.size() == 2);
}

// ===========================================================================
// ② 巡逻（玩家远离：保持 Patrol 且沿路点移动）
// ===========================================================================
TEST_CASE("enemy patrols along its route while player is far", "[enemy][fsm]") {
    LevelData lv;
    EcsWorld   world;
    std::string err;
    buildEnemyWorld(kEnemyLevelJson, lv, world, err);

    // 找敌人 #1 的实体
    Entity e1 = kNullEntity;
    for (const Entity e : world.query<TransformComponent, EnemyComponent>()) {
        const EnemyComponent* en = world.get<EnemyComponent>(e);
        if (en && en->id == 1) { e1 = e; break; }
    }
    REQUIRE(e1 != kNullEntity);

    TransformComponent* t = world.get<TransformComponent>(e1);
    EnemyComponent*      en = world.get<EnemyComponent>(e1);
    REQUIRE(t != nullptr);
    REQUIRE(en != nullptr);
    const float startX = t->pos.x;
    CHECK(en->state == AIState::Patrol);

    // 玩家远远地待在左边，不应被发现
    RunState run;
    MetaState meta; // 默认空记忆：夹具无墙无门，LOS 永远通、nav 永远空（行为等价于 M4）
    run.player = glm::vec2(-800.0f, 0.0f);
    run.alive  = true;
    EventBus bus;

    enemySystem(0.1f, run, lv, meta, world, bus);

    CHECK(en->state == AIState::Patrol);              // 仍巡逻
    CHECK(std::abs(t->pos.x - (startX + 10.0f)) < 1.0f); // 朝 patrol[1] 移动了 100*0.1
    CHECK(std::abs(t->pos.y) < 1.0f);                 // 没乱跑 y
}

// ===========================================================================
// ③ 视野发现（玩家进入 vision 半径 -> Alert）
// ===========================================================================
TEST_CASE("enemy switches to Alert when player enters vision", "[enemy][fsm]") {
    LevelData lv;
    EcsWorld   world;
    std::string err;
    buildEnemyWorld(kEnemyLevelJson, lv, world, err);

    Entity e1 = kNullEntity;
    for (const Entity e : world.query<TransformComponent, EnemyComponent>()) {
        const EnemyComponent* en = world.get<EnemyComponent>(e);
        if (en && en->id == 1) { e1 = e; break; }
    }
    REQUIRE(e1 != kNullEntity);
    EnemyComponent* en = world.get<EnemyComponent>(e1);
    REQUIRE(en);

    // 玩家放在距离敌人 100 处（> attackRange 34，< vision 150）：应当警觉而非致死
    RunState run;
    MetaState meta; // 默认空记忆：夹具无墙无门，LOS 永远通、nav 永远空（行为等价于 M4）
    run.player = glm::vec2(-200.0f, 0.0f); // 敌人起点 (-300,0)，距离 100
    run.alive  = true;
    EventBus bus;

    enemySystem(0.1f, run, lv, meta, world, bus);

    CHECK(en->state == AIState::Alert);
    // 警觉后朝玩家（右边）追：pos.x 应该比起点(-300)更靠近 -200
    CHECK(world.get<TransformComponent>(e1)->pos.x > -300.0f);
}

// ===========================================================================
// ④ 接触致死（发 EnemyHitEvent；致死收口由 LoopSystem 负责，这里只测事件发射）
// ===========================================================================
TEST_CASE("enemy publishes EnemyHitEvent on contact", "[enemy][fsm]") {
    LevelData lv;
    EcsWorld   world;
    std::string err;
    buildEnemyWorld(kEnemyLevelJson, lv, world, err);

    Entity e1 = kNullEntity;
    for (const Entity e : world.query<TransformComponent, EnemyComponent>()) {
        const EnemyComponent* en = world.get<EnemyComponent>(e);
        if (en && en->id == 1) { e1 = e; break; }
    }
    REQUIRE(e1 != kNullEntity);
    EnemyComponent* en = world.get<EnemyComponent>(e1);
    REQUIRE(en);

    EventBus bus;
    int hits = 0;
    bus.subscribe<EnemyHitEvent>([&](const EnemyHitEvent& ev) {
        ++hits;
        CHECK(ev.enemyId == 1);
    });

    // 玩家贴脸（距离 0）：应当立刻发事件并进入 Attack（不再自转移）
    RunState run;
    MetaState meta; // 默认空记忆：夹具无墙无门，LOS 永远通、nav 永远空（行为等价于 M4）
    run.player = glm::vec2(-300.0f, 0.0f); // 敌人起点即此
    run.alive  = true;

    enemySystem(0.1f, run, lv, meta, world, bus);

    CHECK(hits == 1);
    CHECK(en->state == AIState::Attack);
}

// ===========================================================================
// ⑤ 重置归位
// ===========================================================================
TEST_CASE("resetEnemies returns everything to spawn and Patrol", "[enemy][reset]") {
    LevelData lv;
    EcsWorld   world;
    std::string err;
    buildEnemyWorld(kEnemyLevelJson, lv, world, err);

    Entity e1 = kNullEntity;
    for (const Entity e : world.query<TransformComponent, EnemyComponent>()) {
        const EnemyComponent* en = world.get<EnemyComponent>(e);
        if (en && en->id == 1) { e1 = e; break; }
    }
    REQUIRE(e1 != kNullEntity);
    EnemyComponent* en = world.get<EnemyComponent>(e1);
    REQUIRE(en);

    // 先让它跑几帧、进入追击态
    RunState run;
    MetaState meta; // 默认空记忆：夹具无墙无门，LOS 永远通、nav 永远空（行为等价于 M4）
    run.player = glm::vec2(-200.0f, 0.0f);
    run.alive  = true;
    EventBus bus;
    for (int i = 0; i < 5; ++i) enemySystem(0.1f, run, lv, meta, world, bus);
    CHECK(en->state != AIState::Patrol); // 已经离开 Patrol

    resetEnemies(world);

    TransformComponent* t = world.get<TransformComponent>(e1);
    REQUIRE(t != nullptr);
    REQUIRE(en != nullptr);
    CHECK(en->state == AIState::Patrol);
    CHECK(t->pos == lv.enemies[0].patrol[0]); // 回到 patrol[0]
    CHECK(en->patrolIndex == 0);
    CHECK(en->patrolDir == 1);
}

// ===========================================================================
// ⑥ 端到端：被敌人杀死 -> 记住 -> 下一轮归位 -> 落盘还原
// ===========================================================================
TEST_CASE("dying to an enemy is remembered and survives a loop + disk reload",
          "[enemy][game][memory]") {
    const std::string levelPath   = "tmp_m4_enemy_level.json";
    const std::string profilePath = "tmp_m4_enemy_profile.json";
    std::remove(levelPath.c_str());
    std::remove(profilePath.c_str());
    {
        std::ofstream out(levelPath, std::ios::binary);
        REQUIRE(out.good());
        out << kEnemyKillJson;
    }

    EventBus bus;
    Game     game(bus);
    std::string err;
    REQUIRE(game.init(levelPath, profilePath, &err));

    CHECK(game.state().meta.loops == 1);
    CHECK_FALSE(game.state().meta.knowsEnemy(7));

    // 玩家出生点就压在敌人上 -> 第一帧即被接触致死
    InputFrame none; // 无输入
    game.update(0.05f, none);

    // 帧末收口：已进入第 2 轮，记忆里记下了敌人 #7
    CHECK(game.state().meta.deaths == 1);
    CHECK(game.state().meta.loops == 2);
    CHECK(game.state().meta.knowsEnemy(7));   // 敌人记忆永久保留
    CHECK(game.state().run.alive);            // Run 层重置后活着
    CHECK(game.state().run.player == glm::vec2(0.0f, 0.0f)); // 回到出生点

    // 下一轮敌人已归位（回到 patrol[0]）
    const echo::EcsWorld& ecs = game.state().ecs;
    bool enemyAtSpawn = false;
    for (const Entity e : ecs.query<TransformComponent, EnemyComponent>()) {
        const EnemyComponent* en = ecs.get<EnemyComponent>(e);
        const TransformComponent* t = ecs.get<TransformComponent>(e);
        if (en && en->id == 7 && t) {
            enemyAtSpawn = (t->pos == glm::vec2(0.0f, 0.0f)) && (en->state == AIState::Patrol);
        }
    }
    CHECK(enemyAtSpawn);

    // 记忆落盘了，且按 j.contains 守卫读回时 knownEnemyIds 正确
    CHECK(save::fileExists(profilePath));
    MetaState fromDisk;
    REQUIRE(save::readFile(profilePath, fromDisk, &err));
    CHECK(fromDisk.deaths == 1);
    CHECK(fromDisk.knowsEnemy(7));

    std::remove(levelPath.c_str());
    std::remove(profilePath.c_str());
}

// ===========================================================================
// ⑦ 真实关卡确实包含敌兵（数据驱动落地）
// ===========================================================================
TEST_CASE("real levels contain patrolling enemies", "[enemy][integration]") {
    {
        LevelData lv;
        std::string err;
        REQUIRE(loadLevelFile("assets/levels/level01.json", lv, &err));
        CHECK(lv.enemies.size() >= 1);
        CHECK(lv.enemies[0].id >= 1);
        CHECK(lv.enemies[0].patrol.size() >= 2);
    }
    {
        LevelData lv;
        std::string err;
        REQUIRE(loadLevelFile("assets/levels/level02.json", lv, &err));
        CHECK(lv.enemies.size() >= 1);
    }
}
