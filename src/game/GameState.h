// GameState.h —— 三层状态模型（M2 的核心，对应 GDD 第 4 / 5 节）
//
//   ① MetaState  = 永久层。跨循环保留，写进 profile.json。
//                  Reset 时**一个字节都不动**。
//                  装的是"玩家（作为循环者）已经知道的事"：密码、踩过的陷阱、开过的门。
//
//   ② RunState   = 单轮层。本轮的位置、剩余时间、是否携带情报。
//                  Reset 时**全部清空**，回到出生点。
//
//   ③ LevelData  = 静态层。只读的关卡数据，来自 assets/levels/level01.json。
//                  运行期不允许修改（数据驱动）。
//
// 这三层分开，就是"时间循环不是重置，而是信息永久积累"这句话在代码里的落地。
//
// 依赖约定：本文件与 GameState.cpp 不 include SDL / OpenGL，
//          纯数据 + 纯函数，因此可以直接被 Catch2 单测链接。
#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "ecs/World.h"

namespace echo {

// ===========================================================================
// 小工具：轴对齐矩形
// ===========================================================================
struct Rect {
    glm::vec2 center{0.0f, 0.0f};
    glm::vec2 size{0.0f, 0.0f};

    glm::vec2 half() const { return size * 0.5f; }
    float     left() const { return center.x - size.x * 0.5f; }
    float     right() const { return center.x + size.x * 0.5f; }
    float     top() const { return center.y - size.y * 0.5f; }
    float     bottom() const { return center.y + size.y * 0.5f; }
};

// 点是否在矩形内（含边界）
bool pointInRect(const glm::vec2& p, const Rect& r);

// 两个 AABB 是否相交（含边界相切）
bool overlapRect(const Rect& a, const Rect& b);

// 把中心点 p、半尺寸 halfExtent 的盒子夹在 [min,max] 内，返回修正后的中心
glm::vec2 clampToBounds(const glm::vec2& p, const glm::vec2& halfExtent,
                        const glm::vec2& min, const glm::vec2& max);

// 一帧的输入快照。由渲染层（main.cpp）从 SDL 键盘状态填好后传进来，
// 逻辑层（Game / System）不必知道 SDL 的存在。
struct InputFrame {
    bool up    = false;
    bool down  = false;
    bool left  = false;
    bool right = false;
};

// ===========================================================================
// ③ 静态层：关卡数据（只读）
// ===========================================================================
struct WallDef {
    std::string id;
    Rect        rect;
};

struct RoomDef {
    std::string id;
    std::string label;
    Rect        rect;
    glm::vec4   color{0.13f, 0.14f, 0.19f, 1.0f};
};

struct TrapDef {
    int         id = -1;
    std::string label;
    Rect        rect;
};

// 情报点：kind = "datacore" 等；payload 是它携带的密码
struct ItemDef {
    std::string id;
    std::string kind;
    std::string label;
    std::string payload;
    Rect        rect;
    glm::vec4   color{0.86f, 0.36f, 0.95f, 1.0f}; // 渲染色（第四关：紫/黄钥匙）
};

struct DoorDef {
    std::string id;
    std::string label;
    std::string requiresCode; // 需要 Meta 层记住哪个密码才能通过
    Rect        rect;
    glm::vec4   color{0.88f, 0.26f, 0.30f, 1.0f}; // 锁住时的渲染色（第四关：紫门/黄门）
};

// 传送门（第四关）：同 group 的两个互相传送（A<->A），双向。
// 玩家踩上任意一端 -> 立刻出现在另一端；防反复触发见 teleporterSystem。
struct TeleporterDef {
    std::string id;
    std::string group; // 配对键："A" / "F" / "B" / "C" / "D"
    std::string label; // 渲染在传送门上的字母
    Rect        rect;
    glm::vec4   color{0.86f, 0.36f, 0.95f, 1.0f};
};

// 敌兵定义（M4）：巡逻路点 + 视野/速度；id 用于写进 Meta 的「已知敌人」
struct EnemyDef {
    int         id = -1;
    std::string label;
    std::vector<glm::vec2> patrol; // 巡逻路点；空 = 原地；patrol[0] 即出生点
    float       speed      = 90.0f;
    float       vision     = 260.0f;
    float       alertSpeed = 200.0f;
    float       attackRange = 36.0f;
    glm::vec2   size       = {30.0f, 30.0f};
};

struct LevelData {
    std::string name;
    float       loopSeconds = 20.0f; // 单轮时长（主失败源：时间耗尽）

    glm::vec2 spawn{0.0f, 0.0f};
    glm::vec2 boundsMin{-1000.0f, -200.0f};
    glm::vec2 boundsMax{1000.0f, 200.0f};
    float     viewScale = 1.0f; // 相机视野缩放：<1 放大世界（看得更少、更大）

    Rect exitRect;

    std::vector<RoomDef> rooms;
    std::vector<WallDef> walls;
    std::vector<TrapDef> traps;
    std::vector<ItemDef> items;
    std::vector<DoorDef> doors;
    std::vector<EnemyDef> enemies; // M4：巡逻机器人（主动威胁）
    std::vector<TeleporterDef> teleporters; // 第四关：成对传送门
};

// ===========================================================================
// ① Meta 层：永久存档，Reset 不动
// ===========================================================================
struct MetaState {
    int   loops   = 0;   // 已经历的循环次数
    int   deaths  = 0;   // 累计死亡次数
    int   escapes = 0;   // 累计通关次数
    float bestTime = -1.0f; // 最快通关总用时（秒）= 历次轮回用时之和，-1 表示还没通关过
    float totalElapsed = 0.0f; // 当前这次通关尝试的累计用时（各轮回 elapsed 之和），逃出后归零

    // 循环者"记得"的东西 —— 这三样是全部永久成长
    std::vector<std::string> knownPasswords; // 已知密码
    std::vector<int>         knownTrapIds;   // 已知陷阱（踩过一次就记住了）
    std::vector<std::string> openedDoorIds;  // 已知可通行的门
    std::vector<int>         knownEnemyIds;  // 已知敌兵（被某敌人杀过才记住它的巡逻路线）

    bool knowsPassword(const std::string& code) const;
    bool knowsTrap(int trapId) const;
    bool knowsDoor(const std::string& doorId) const;
    bool knowsEnemy(int enemyId) const;

    bool rememberPassword(const std::string& code); // 返回 true 表示是新情报
    bool rememberTrap(int trapId);
    bool rememberDoor(const std::string& doorId);
    bool rememberEnemy(int enemyId);

    // 存档用：把"记忆"清空，但保留统计（F5 硬重置用全清版本）
    void clearMemory();
    void clearAll();
};

// ===========================================================================
// ② Run 层：单轮状态，Reset 全清
// ===========================================================================
struct RunState {
    glm::vec2 player{0.0f, 0.0f};
    glm::vec2 prevPlayer{0.0f, 0.0f}; // 本帧移动前的位置（墙碰撞用：只往撞进来的那侧推）
    glm::vec2 playerSize{26.0f, 26.0f};
    float     moveSpeed  = 220.0f; // 世界单位/秒
    float     timeLeft   = 20.0f;
    float     elapsed    = 0.0f;
    bool      alive      = true;
    bool      escaped    = false;
    bool      teleportLatch = false; // 刚传送过且还踩在传送门上（防止落地即回传）

    // 本轮手里拿着的钥匙（M7：死亡复活会掉落，必须重新拾取，所以只存 Run 层）
    std::vector<std::string> carriedCodes;
    bool carriesPayload() const { return !carriedCodes.empty(); }
    bool holdsCode(const std::string& code) const {
        return std::find(carriedCodes.begin(), carriedCodes.end(), code) != carriedCodes.end();
    }

    // 关键：单轮重置。只碰 Run 层字段，不碰任何 Meta 数据。
    void reset(const glm::vec2& spawnPoint, float loopSeconds);
};

// ===========================================================================
// 三层容器
// ===========================================================================
class GameState {
public:
    LevelData world; // Static（关卡资产 / prefab）
    EcsWorld  ecs;   // 由 world 展开出的实体世界（房间/门/核心/陷阱/墙/出口/玩家）
    MetaState meta;  // Meta
    RunState  run;   // Run

    // 开始新一轮：loops +1，Run 层完全重置，Meta 层原封不动。
    void beginNewLoop();

    // 只重置 Run 层（例如"重开本轮"调试键），连 loops 都不加。
    void resetRunOnly();

    Rect playerRect() const;

    // 把 Run 层玩家位置同步进 ECS 玩家实体的 Transform（实体世界不存"单轮状态"，
    // Transform 只是 Run.player 的镜像，供系统与渲染读取）。无玩家实体时安全 no-op。
    void syncPlayerTransform();
};

} // namespace echo
