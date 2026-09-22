// test_gamestate.cpp —— M2 单元测试（Catch2 v3）
//
// 为什么值得写这些测试？
//   时间循环游戏最容易出的 bug，就是"该重置的没重置 / 不该重置的被重置了"。
//   这种 bug 表现很隐蔽（玩三五轮之后才发现），靠手点根本测不稳。
//   把三层状态的重置语义写成断言，才是能防住它的办法。
//
// 覆盖：
//   ① RunState::reset 到底清掉了什么、保留了什么
//   ② GameState::beginNewLoop 是否真的不碰 Meta
//   ③ MetaState 的记忆去重
//   ④ 存档 JSON 往返
//   ⑤ 关卡 JSON 解析（含非法数据兜底）
//   ⑥ EventBus 的类型路由与退订
//   ⑦ 端到端：真的踩上陷阱 -> 死亡 -> 重置 -> 记忆保留
//   ⑧ 端到端：真的跑通关 -> 定格 -> 重跑（Run 重置、Meta 记忆保留）
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>

#include "core/EventBus.h"
#include "core/Events.h"
#include "game/Game.h"
#include "game/GameState.h"
#include "game/LevelLoader.h"
#include "game/SaveSystem.h"
#include "game/systems/Systems.h" // teleporterSystem

using namespace echo;

// ===========================================================================
// ① Run 层：reset 清什么、留什么
// ===========================================================================
TEST_CASE("RunState::reset wipes this-loop progress only", "[state]") {
    RunState run;
    run.moveSpeed       = 220.0f;
    run.player          = glm::vec2(123.0f, 45.0f);
    run.timeLeft        = 3.0f;
    run.elapsed         = 17.0f;
    run.alive           = false;
    run.escaped         = true;
    run.carriedCodes = {"ZZ-99", "AA-11"};

    run.reset(glm::vec2(-700.0f, 0.0f), 25.0f);

    CHECK(run.player == glm::vec2(-700.0f, 0.0f));
    CHECK(run.timeLeft == 25.0f);
    CHECK(run.elapsed == 0.0f);
    CHECK(run.alive);
    CHECK_FALSE(run.escaped);
    CHECK_FALSE(run.carriesPayload());
    CHECK(run.carriedCodes.empty());
    CHECK_FALSE(run.holdsCode("ZZ-99"));

    // 角色固有属性不是"本轮进度"，不该被重置
    CHECK(run.moveSpeed == 220.0f);
    CHECK(run.playerSize == glm::vec2(26.0f, 26.0f));
}

// ===========================================================================
// ② 分层重置：Run 推倒重来，Meta 一个字节都不动
// ===========================================================================
TEST_CASE("beginNewLoop resets Run and never touches Meta", "[state]") {
    GameState gs;
    gs.world.spawn       = glm::vec2(-700.0f, 0.0f);
    gs.world.loopSeconds = 25.0f;

    gs.meta.rememberPassword("XY-42");
    gs.meta.rememberTrap(1);
    gs.meta.rememberDoor("door_b");
    gs.meta.deaths = 2;

    gs.run.player = glm::vec2(600.0f, 0.0f);
    gs.run.alive  = false;

    gs.beginNewLoop();

    // Meta：只增不减
    CHECK(gs.meta.loops == 1);
    CHECK(gs.meta.deaths == 2);
    CHECK(gs.meta.knowsPassword("XY-42"));
    CHECK(gs.meta.knowsTrap(1));
    CHECK(gs.meta.knowsDoor("door_b"));

    // Run：完全回到起点
    CHECK(gs.run.player == glm::vec2(-700.0f, 0.0f));
    CHECK(gs.run.timeLeft == 25.0f);
    CHECK(gs.run.alive);
}

// ===========================================================================
// ③ 记忆去重
// ===========================================================================
TEST_CASE("memory entries are deduplicated", "[state]") {
    MetaState m;
    CHECK(m.rememberPassword("A"));
    CHECK_FALSE(m.rememberPassword("A"));
    CHECK(m.knownPasswords.size() == 1);

    CHECK(m.rememberTrap(3));
    CHECK_FALSE(m.rememberTrap(3));
    CHECK_FALSE(m.rememberTrap(-1)); // 非法 id 不记
    CHECK_FALSE(m.rememberPassword(""));

    m.clearMemory();
    CHECK(m.knownPasswords.empty());
    CHECK(m.knownTrapIds.empty());
    CHECK(m.openedDoorIds.empty());
    CHECK(m.bestTime < 0.0f);
}

// ===========================================================================
// ④ 存档往返
// ===========================================================================
TEST_CASE("MetaState survives a JSON round trip", "[save]") {
    MetaState m;
    m.loops    = 3;
    m.deaths   = 2;
    m.escapes  = 1;
    m.bestTime = 12.5f;
    m.rememberPassword("XY-42");
    m.rememberTrap(2);
    m.rememberDoor("door_b");

    const std::string text = save::toJsonText(m);

    MetaState back;
    std::string err;
    REQUIRE(save::fromJsonText(text, back, &err));
    CHECK(back.loops == 3);
    CHECK(back.deaths == 2);
    CHECK(back.escapes == 1);
    CHECK(back.bestTime == 12.5f);
    CHECK(back.knownPasswords == m.knownPasswords);
    CHECK(back.knownTrapIds == m.knownTrapIds);
    CHECK(back.openedDoorIds == m.openedDoorIds);
}

TEST_CASE("a save file missing fields still loads with defaults", "[save]") {
    MetaState back;
    std::string err;
    REQUIRE(save::fromJsonText(R"({"loops": 4})", back, &err));
    CHECK(back.loops == 4);
    CHECK(back.deaths == 0);
    CHECK(back.bestTime < 0.0f);
    CHECK(back.knownPasswords.empty());

    CHECK_FALSE(save::fromJsonText("not json at all", back, &err));
    CHECK_FALSE(err.empty());
}

// ===========================================================================
// ⑤ 关卡数据驱动
// ===========================================================================
namespace {
const char* kTestLevelJson = R"JSON({
  "name": "unit-test-level",
  "loopSeconds": 12.0,
  "spawn": [-700, 0],
  "bounds": { "min": [-1005, -190], "max": [790, 190] },
  "exit": { "center": [-960, 0], "size": [56, 150] },
  "rooms": [ { "id": "hall", "label": "HALL", "center": [-200, 0], "size": [1200, 430] } ],
  "walls": [ { "id": "top", "center": [-110, -290], "size": [1930, 60] } ],
  "traps": [ { "id": 7, "label": "corrosive", "center": [0, 0], "size": [46, 46] } ],
  "items": [ { "id": "datacore", "kind": "datacore", "payload": "XY-42",
               "center": [700, 0], "size": [36, 36] } ],
  "doors": [ { "id": "door_b", "requires": "XY-42", "center": [-830, 0], "size": [40, 460] } ]
})JSON";

// 一个没有陷阱、只有一道密码门的迷你关卡：用来稳定地"跑通关"
// 出生点 -100 在门(-150)右侧，数据核心在 300，出口在 -300（门左侧）。
// 流程：向右拿核心 → 门解锁 → 向左穿过门到出口 → 通关。
const char* kEscapeLevelJson = R"JSON({
  "name": "escape-test",
  "loopSeconds": 60.0,
  "spawn": [-100, 0],
  "bounds": { "min": [-400, -200], "max": [400, 200] },
  "exit": { "center": [-300, 0], "size": [40, 120] },
  "items": [ { "id": "dc", "kind": "datacore", "payload": "AA-11",
               "center": [300, 0], "size": [30, 30] } ],
  "doors": [ { "id": "door_x", "requires": "AA-11",
               "center": [-150, 0], "size": [20, 400] } ]
})JSON";
} // namespace

TEST_CASE("level JSON becomes static world data", "[level]") {
    LevelData lv;
    std::string err;
    REQUIRE(parseLevelJson(kTestLevelJson, lv, &err));

    CHECK(lv.name == "unit-test-level");
    CHECK(lv.loopSeconds == 12.0f);
    CHECK(lv.spawn == glm::vec2(-700.0f, 0.0f));
    CHECK(lv.exitRect.center == glm::vec2(-960.0f, 0.0f));
    CHECK(lv.boundsMax == glm::vec2(790.0f, 190.0f));

    REQUIRE(lv.rooms.size() == 1);
    REQUIRE(lv.walls.size() == 1);
    REQUIRE(lv.traps.size() == 1);
    REQUIRE(lv.items.size() == 1);
    REQUIRE(lv.doors.size() == 1);

    CHECK(lv.traps[0].id == 7);
    CHECK(lv.items[0].payload == "XY-42");
    CHECK(lv.doors[0].requiresCode == "XY-42");
}

TEST_CASE("a level without a valid exit is rejected", "[level]") {
    LevelData lv;
    std::string err;
    CHECK_FALSE(parseLevelJson(R"({"name": "broken"})", lv, &err));
    CHECK_FALSE(err.empty());

    std::string err2;
    CHECK_FALSE(parseLevelJson("{ this is not json", lv, &err2));
    CHECK_FALSE(err2.empty());
}

// ===========================================================================
// ⑥ 事件总线
// ===========================================================================
TEST_CASE("EventBus routes strictly by static type", "[event]") {
    EventBus bus;
    int died = 0, timedOut = 0;

    const EventBus::HandlerId id =
        bus.subscribe<PlayerDiedEvent>([&](const PlayerDiedEvent&) { died += 1; });
    bus.subscribe<LoopTimeoutEvent>([&](const LoopTimeoutEvent&) { timedOut += 1; });

    bus.publish(LoopTimeoutEvent{1});
    CHECK(died == 0);
    CHECK(timedOut == 1);

    bus.publish(PlayerDiedEvent{1, glm::vec2(0.0f), "trap", 1});
    CHECK(died == 1);

    bus.unsubscribe(id);
    bus.publish(PlayerDiedEvent{2, glm::vec2(0.0f), "trap", 1});
    CHECK(died == 1);
    CHECK(bus.subscriberCount<PlayerDiedEvent>() == 0);
}

TEST_CASE("publishing to a type nobody listens to is harmless", "[event]") {
    EventBus bus;
    CHECK_NOTHROW(bus.publish(TrapHitEvent{3, glm::vec2(1.0f, 2.0f)}));
    CHECK(bus.subscriberCount<TrapHitEvent>() == 0);
}

// ===========================================================================
// ⑦ 端到端：踩陷阱 -> 死亡 -> 重置 -> 记忆保留
// ===========================================================================
TEST_CASE("stepping on a trap restarts the loop and keeps the memory", "[game]") {
    const std::string levelPath   = "tmp_m2_level.json";
    const std::string profilePath = "tmp_m2_profile.json";
    std::remove(levelPath.c_str());
    std::remove(profilePath.c_str());

    {
        std::ofstream out(levelPath, std::ios::binary);
        REQUIRE(out.good());
        out << kTestLevelJson;
    }

    EventBus bus;
    Game game(bus);

    std::string err;
    REQUIRE(game.init(levelPath, profilePath, &err));

    CHECK(game.state().meta.loops == 1);
    CHECK(game.state().run.player == glm::vec2(-700.0f, 0.0f));
    CHECK(game.state().run.alive);
    CHECK_FALSE(game.state().meta.knowsTrap(1));

    // 一直朝右走，第 1 个陷阱在 (0,0)，一定会撞上
    InputFrame right;
    right.right = true;

    const float dt = 0.05f;
    int steps = 0;
    while (game.state().meta.deaths == 0 && steps < 400) {
        game.update(dt, right);
        ++steps;
    }

    REQUIRE(game.state().meta.deaths == 1);
    CHECK(steps < 400);                       // 确实在时间耗尽之前就死了
    CHECK(game.state().meta.loops == 2);      // 已经自动进入第 2 轮
    CHECK(game.state().meta.knowsTrap(7));    // 陷阱位置被永久记住
    CHECK(game.state().run.alive);            // Run 层重置后是活的
    CHECK(game.state().run.player == glm::vec2(-700.0f, 0.0f)); // 回到出生点
    CHECK(game.state().run.timeLeft > 0.0f);

    // Meta 变更落盘了
    CHECK(save::fileExists(profilePath));
    MetaState fromDisk;
    REQUIRE(save::readFile(profilePath, fromDisk, &err));
    CHECK(fromDisk.deaths == 1);
    CHECK(fromDisk.knowsTrap(7));

    std::remove(levelPath.c_str());
    std::remove(profilePath.c_str());
}

TEST_CASE("wipeProfile erases every memory and restarts from loop 1", "[game]") {
    const std::string levelPath   = "tmp_m2_level2.json";
    const std::string profilePath = "tmp_m2_profile2.json";
    std::remove(levelPath.c_str());
    std::remove(profilePath.c_str());
    {
        std::ofstream out(levelPath, std::ios::binary);
        REQUIRE(out.good());
        out << kTestLevelJson;
    }

    EventBus bus;
    Game game(bus);
    std::string err;
    REQUIRE(game.init(levelPath, profilePath, &err));

    InputFrame right;
    right.right = true;
    for (int i = 0; i < 400 && game.state().meta.deaths == 0; ++i) game.update(0.05f, right);
    REQUIRE(game.state().meta.deaths == 1);
    REQUIRE(game.state().meta.knowsTrap(7));

    game.wipeProfile();

    CHECK(game.state().meta.deaths == 0);
    CHECK(game.state().meta.loops == 1);
    CHECK_FALSE(game.state().meta.knowsTrap(7));
    CHECK(game.state().run.player == glm::vec2(-700.0f, 0.0f));

    std::remove(levelPath.c_str());
    std::remove(profilePath.c_str());
}

// ===========================================================================
// ⑧ 通关后重跑（M2.1）：Run 重置、Meta 记忆一个不丢
// ===========================================================================
TEST_CASE("escaping finishes the run, and replay keeps every memory", "[game][escape]") {
    const std::string levelPath   = "tmp_m2_escape.json";
    const std::string profilePath = "tmp_m2_escape_profile.json";
    std::remove(levelPath.c_str());
    std::remove(profilePath.c_str());
    {
        std::ofstream out(levelPath, std::ios::binary);
        REQUIRE(out.good());
        out << kEscapeLevelJson;
    }

    EventBus bus;
    Game     game(bus);
    std::string err;
    REQUIRE(game.init(levelPath, profilePath, &err));
    CHECK_FALSE(game.finished());

    // 还没通关时调用 replay 应当什么都不做
    game.replayAfterEscape();
    CHECK_FALSE(game.finished());
    CHECK(game.state().meta.loops == 1);

    // 向右走拿到数据核心 -> 密码被永久记住 -> 门解锁
    InputFrame right;
    right.right = true;
    for (int i = 0; i < 2000 && !game.state().meta.knowsPassword("AA-11"); ++i)
        game.update(0.05f, right);
    REQUIRE(game.state().meta.knowsPassword("AA-11"));
    CHECK_FALSE(game.finished());

    // 掉头向左，穿过已解锁的门，碰到出口 -> 通关
    InputFrame left;
    left.left = true;
    for (int i = 0; i < 2000 && !game.finished(); ++i)
        game.update(0.05f, left);

    CHECK(game.finished());
    CHECK(game.state().run.escaped);
    CHECK(game.state().meta.escapes == 1);
    CHECK(game.state().meta.loops == 1); // 第一轮就跑出去了
    CHECK(game.state().run.elapsed > 0.0f);

    // ---- 通关后重跑：Run 回到起点，Meta 一个不丢 ----
    game.replayAfterEscape();

    CHECK_FALSE(game.finished());
    CHECK_FALSE(game.state().run.escaped);
    CHECK(game.state().run.alive);
    CHECK(game.state().run.elapsed == 0.0f);
    CHECK(game.state().run.player == glm::vec2(-100.0f, 0.0f)); // 回到出生点
    CHECK(game.state().run.timeLeft == 60.0f);                  // 时钟重置

    CHECK(game.state().meta.knowsPassword("AA-11")); // 记忆保留
    CHECK(game.state().meta.escapes == 1);
    CHECK(game.state().meta.loops == 2);             // 只是又开了新一轮
    CHECK(game.state().meta.bestTime > 0.0f);

    std::remove(levelPath.c_str());
    std::remove(profilePath.c_str());
}

// ===========================================================================
// ⑨ 真实关卡（level01.json）的结构与"假密码"不变量
// ===========================================================================
TEST_CASE("level01.json loads and contains a decoy password", "[level][integration]") {
    // 资源由 CMake POST_BUILD 拷贝到 exe 同目录的 assets/ 下，
    // 单测从构建目录（与 exe 同目录）跑，相对路径找得到。
    LevelData lv;
    std::string err;
    REQUIRE(loadLevelFile("assets/levels/level01.json", lv, &err));

    CHECK(lv.loopSeconds > 0.0f);
    CHECK(lv.traps.size() >= 8);   // 足够密的雷区，强制分多轮去记忆
    CHECK(lv.doors.size() == 2);   // 两道密码门
    CHECK(lv.items.size() == 3);   // 三个情报核心（含 1 个诱饵）

    // 收集所有门需要的密码
    std::set<std::string> needed;
    for (const auto& d : lv.doors) needed.insert(d.requiresCode);

    // 至少存在一个"情报"携带的密码，但没有任何一道门需要它 —— 即"假密码诱饵"
    int decoys = 0;
    for (const auto& it : lv.items) {
        if (!it.payload.empty() && needed.find(it.payload) == needed.end())
            ++decoys;
    }
    CHECK(decoys >= 1);
}

// ===========================================================================
// ⑩ 暂停菜单的"重开"（M3）：Run 重置、Meta 记忆保留（对照 wipeProfile）
// ===========================================================================
TEST_CASE("restartKeepMeta resets Run but keeps Meta, unlike wipeProfile", "[game][pause]") {
    const std::string levelPath   = "tmp_m3_restart.json";
    const std::string profilePath = "tmp_m3_restart_profile.json";
    std::remove(levelPath.c_str());
    std::remove(profilePath.c_str());
    {
        std::ofstream out(levelPath, std::ios::binary);
        REQUIRE(out.good());
        out << kEscapeLevelJson;
    }

    EventBus bus;
    Game     game(bus);
    std::string err;
    REQUIRE(game.init(levelPath, profilePath, &err));
    CHECK(game.state().meta.loops == 1);

    // 向右走拿到密码（无陷阱，不会死、不会重置）
    InputFrame right;
    right.right = true;
    for (int i = 0; i < 2000 && !game.state().meta.knowsPassword("AA-11"); ++i)
        game.update(0.05f, right);
    REQUIRE(game.state().meta.knowsPassword("AA-11"));
    CHECK(game.state().meta.loops == 1); // 没死过，轮数不变

    // 玩家此时停在偏离出生点的位置
    const glm::vec2 offSpawn = game.state().run.player;
    CHECK(offSpawn != game.state().world.spawn); // 确实走开了

    // 暂停菜单 "RESTART"：Run 回到起点，Meta 记忆一个不丢
    game.restartKeepMeta();

    CHECK(game.state().run.player == game.state().world.spawn);
    CHECK(game.state().run.elapsed == 0.0f);
    CHECK(game.state().run.alive);
    CHECK(game.state().run.timeLeft == 60.0f);

    CHECK(game.state().meta.knowsPassword("AA-11")); // 密码记忆保留
    CHECK(game.state().meta.deaths == 0);
    CHECK(game.state().meta.escapes == 0);
    CHECK(game.state().meta.loops == 2);             // 正常推进新一轮
    CHECK(game.state().meta.bestTime < 0.0f);

    // 对照：wipeProfile 会把记忆彻底清空
    game.wipeProfile();
    CHECK_FALSE(game.state().meta.knowsPassword("AA-11"));
    CHECK(game.state().meta.loops == 1);

    std::remove(levelPath.c_str());
    std::remove(profilePath.c_str());
}

// ===========================================================================
// ⑪ 第二关（level02.json）的结构与"假密码"不变量
// ===========================================================================
TEST_CASE("level02.json loads as a distinct, valid second level", "[level][integration]") {
    // 资源由 CMake POST_BUILD 拷贝到 exe 同目录的 assets/ 下
    LevelData lv;
    std::string err;
    REQUIRE(loadLevelFile("assets/levels/level02.json", lv, &err));

    CHECK(lv.loopSeconds > 0.0f);
    CHECK(lv.traps.size() >= 8);   // 足够密的雷区，强制分多轮去记忆
    CHECK(lv.doors.size() == 2);   // 两道密码门
    CHECK(lv.items.size() == 3);   // 三个情报核心（含 1 个诱饵）

    // 至少存在一个"情报"携带的密码，但没有任何一道门需要它 —— 即"假密码诱饵"
    std::set<std::string> needed;
    for (const auto& d : lv.doors) needed.insert(d.requiresCode);
    int decoys = 0;
    for (const auto& it : lv.items) {
        if (!it.payload.empty() && needed.find(it.payload) == needed.end())
            ++decoys;
    }
    CHECK(decoys >= 1);
}

// ===========================================================================
// ⑫ 运行时切关：loadLevel 切换关卡，且每关记忆在各自存档上互不串门
// ===========================================================================
TEST_CASE("loadLevel switches levels and restores each level's memory", "[game][levels]") {
    const std::string lvlA  = "tmp_lvl_a.json";
    const std::string profA = "tmp_lvl_a_profile.json";
    const std::string lvlB  = "tmp_lvl_b.json";
    const std::string profB = "tmp_lvl_b_profile.json";
    for (const char* f : { lvlA.c_str(), profA.c_str(), lvlB.c_str(), profB.c_str() })
        std::remove(f);

    // A 关：含一个陷阱（trap #7）的迷你关，用来制造"记忆"
    {
        std::ofstream out(lvlA, std::ios::binary);
        REQUIRE(out.good());
        out << kTestLevelJson;
    }
    // B 关：没有任何陷阱、只有一道门的极简关，用来确认切过去后记忆是干净的
    {
        std::ofstream out(lvlB, std::ios::binary);
        REQUIRE(out.good());
        out << R"JSON({
          "name": "level-b",
          "loopSeconds": 30.0,
          "spawn": [-100, 0],
          "bounds": { "min": [-400, -200], "max": [400, 200] },
          "exit": { "center": [300, 0], "size": [40, 120] },
          "doors": [ { "id": "dx", "requires": "BB-22", "center": [-50, 0], "size": [20, 400] } ]
        })JSON";
    }

    EventBus bus;
    Game     game(bus);
    std::string err;
    REQUIRE(game.init(lvlA, profA, &err));
    CHECK(game.state().meta.loops == 1);

    // 在 A 关撞陷阱 -> 记住 trap #7，并落盘到 profA
    InputFrame right;
    right.right = true;
    for (int i = 0; i < 400 && game.state().meta.deaths == 0; ++i)
        game.update(0.05f, right);
    REQUIRE(game.state().meta.knowsTrap(7));
    REQUIRE(game.state().meta.loops == 2);

    // 切到 B 关：记忆应当被清空（读的是 profB，还是空的）
    REQUIRE(game.loadLevel(lvlB, profB));
    CHECK(game.state().meta.loops == 1);
    CHECK_FALSE(game.state().meta.knowsTrap(7));
    CHECK(game.state().world.doors.size() == 1);

    // 切回 A 关：应当从 profA 把 trap #7 的记忆（和统计）恢复回来
    REQUIRE(game.loadLevel(lvlA, profA));
    CHECK(game.state().meta.knowsTrap(7));   // 记忆被还原
    CHECK(game.state().meta.deaths == 1);     // 统计也被还原
    CHECK(game.state().world.doors.size() == 1);

    for (const char* f : { lvlA.c_str(), profA.c_str(), lvlB.c_str(), profB.c_str() })
        std::remove(f);
}

// ---------------------------------------------------------------------------
// ⑨ 回归测试：无敌模式 + 倒计时归零，玩家不能被卡死
//   旧 bug：Game::update 在 timeLeft<=0 时调用 timeOut() -> killPlayer，
//   无敌模式把 killPlayer 短路掉 -> 循环不重置、timeLeft 恒为 0，
//   每帧都提前 return（跳过移动系统）→ 玩家冻住 + 控制台刷 "ran out of time"。
//   正确行为：无敌模式下倒计时停在 0，游戏继续正常推进。
// ---------------------------------------------------------------------------
TEST_CASE("god mode: timeout does not freeze the player", "[game][god]") {
    const std::string levelPath   = "tmp_god_timeout_level.json";
    const std::string profilePath = "tmp_god_timeout_profile.json";
    std::remove(levelPath.c_str());
    std::remove(profilePath.c_str());
    {
        std::ofstream out(levelPath, std::ios::binary);
        REQUIRE(out.good());
        out << kTestLevelJson; // loopSeconds = 12
    }

    EventBus bus;
    Game     game(bus);
    std::string err;
    REQUIRE(game.init(levelPath, profilePath, &err));
    game.setGodMode(true);

    // 站着不动，把 12 秒倒计时耗干
    InputFrame none;
    for (int i = 0; i < 200 && game.state().run.timeLeft > 0.0f; ++i)
        game.update(0.1f, none);
    REQUIRE(game.state().run.timeLeft == 0.0f);

    // 超时之后：循环数不涨、没死、而且必须还能动
    CHECK(game.state().meta.loops == 1);
    CHECK(game.state().run.alive);

    const float xBefore = game.state().run.player.x;
    InputFrame right;
    right.right = true;
    for (int i = 0; i < 30; ++i) game.update(0.1f, right);
    CHECK(game.state().run.player.x > xBefore + 50.0f); // 旧 bug：x 一动不动
    CHECK(game.state().meta.loops == 1);                // 也不会偷偷重置循环

    std::remove(levelPath.c_str());
    std::remove(profilePath.c_str());
}

// ===========================================================================
// ⑬ 第四关传送门：引擎行为（teleporterSystem） + 关卡数据结构
// ===========================================================================
TEST_CASE("teleporterSystem teleports to the paired pad and latches", "[teleporter]") {
    EventBus bus;
    LevelData level;
    TeleporterDef a1, a2;
    a1.id = "tp_a1"; a1.group = "A"; a1.rect = Rect{glm::vec2(0.0f, 0.0f),   glm::vec2(40.0f, 36.0f)};
    a2.id = "tp_a2"; a2.group = "A"; a2.rect = Rect{glm::vec2(500.0f, 0.0f), glm::vec2(40.0f, 36.0f)};
    level.teleporters = {a1, a2};

    RunState run;
    run.player     = glm::vec2(0.0f, 0.0f); // 踩在 a1 上
    run.prevPlayer = run.player;

    teleporterSystem(run, level, bus);
    CHECK(run.player == glm::vec2(500.0f, 0.0f)); // 出现在另一端 a2
    CHECK(run.teleportLatch == true);

    // 仍踩在 a2 上 -> 不立即回传（latch 防止落地即回弹）
    teleporterSystem(run, level, bus);
    CHECK(run.player == glm::vec2(500.0f, 0.0f));
    CHECK(run.teleportLatch == true);

    // 离开所有传送门垫 -> latch 复位
    run.player = glm::vec2(100.0f, 100.0f);
    run.prevPlayer = run.player;
    teleporterSystem(run, level, bus);
    CHECK(run.teleportLatch == false);

    // 再次踩上 a2 -> 传回 a1
    run.player = glm::vec2(500.0f, 0.0f);
    run.prevPlayer = run.player;
    teleporterSystem(run, level, bus);
    CHECK(run.player == glm::vec2(0.0f, 0.0f));
}

TEST_CASE("teleporterSystem is a no-op on a level with no teleporters", "[teleporter]") {
    EventBus bus;
    LevelData level; // 无传送门
    RunState run;
    run.player     = glm::vec2(10.0f, 10.0f);
    run.prevPlayer = run.player;
    teleporterSystem(run, level, bus); // 不应崩溃、不应改变位置
    CHECK(run.player == glm::vec2(10.0f, 10.0f));
    CHECK(run.teleportLatch == false);
}

TEST_CASE("level04.json is a well-formed portal level", "[level][integration][portal]") {
    LevelData lv;
    std::string err;
    REQUIRE(loadLevelFile("assets/levels/level04.json", lv, &err));

    // 5 对传送门（A/F/B/C/D），每对恰好两端
    REQUIRE(lv.teleporters.size() == 10);
    std::map<std::string, int> groupCount;
    for (const auto& t : lv.teleporters) groupCount[t.group]++;
    CHECK(groupCount.size() == 5);
    for (const auto& kv : groupCount) CHECK(kv.second == 2);

    // 双钥匙 + 双门，门要求的密码必须对应某个核心的 payload
    REQUIRE(lv.items.size() == 2);
    REQUIRE(lv.doors.size() == 2);
    std::set<std::string> payloads;
    for (const auto& it : lv.items) payloads.insert(it.payload);
    for (const auto& d : lv.doors) CHECK(payloads.count(d.requiresCode) == 1);

    // 终点关在「只能从 D 传送进」的 nook 里：出口中心在 nook_wall 左侧
    const auto nook = std::find_if(lv.walls.begin(), lv.walls.end(),
                                    [](const WallDef& w){ return w.id == "nook_wall"; });
    REQUIRE(nook != lv.walls.end());
    CHECK(lv.exitRect.center.x < nook->rect.center.x);
}

// ===========================================================================
// ⑩ M8：通关总时间 = 历次轮回用时之和（死亡/超时也会累计，而非只记最后一轮）
// ===========================================================================
TEST_CASE("total elapsed accumulates across loops, not just the final loop", "[meta][time]") {
    echo::GameState gs;
    gs.world.spawn      = {0.0f, 0.0f};
    gs.world.loopSeconds = 60.0f;

    gs.beginNewLoop();            // 第 1 轮开始（首轮 elapsed 为 0，不计入）
    gs.run.elapsed = 10.0f;       // 第 1 轮玩了 10 秒后死亡
    gs.beginNewLoop();            // 进入第 2 轮：totalElapsed += 10
    gs.run.elapsed = 15.0f;       // 第 2 轮玩了 15 秒后逃出
    gs.meta.totalElapsed += gs.run.elapsed;   // 逃出这轮回一并计入（与 LoopSystem 行为一致）
    const float total = gs.meta.totalElapsed;
    gs.meta.bestTime = (gs.meta.bestTime < 0.0f || total < gs.meta.bestTime) ? total : gs.meta.bestTime;
    gs.meta.totalElapsed = 0.0f;  // 通关尝试结束归零

    CHECK(gs.meta.loops == 2);
    CHECK(total == 25.0f);        // 10 + 15，而非只记最后一轮的 15
    CHECK(gs.meta.bestTime == 25.0f);
}
