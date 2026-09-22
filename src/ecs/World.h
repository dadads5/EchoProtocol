// src/ecs/World.h —— 真 ECS 核心（M3 强化项：真 ECS 重构）
//
// 为什么是 ECS 而不是「Actor 基类 + 继承」？
//   时间循环世界里的物件种类多（玩家/门/核心/陷阱/房间/墙/出口），行为差异大、
//   组合性强。ECS 把「是什么」（Component）和「怎么动」（System）彻底分开：
//   加一种新物件 = 加一个 Component + 一个 System，不用动任何已有类。
//
// 这里是「最小可用的真 ECS」：
//   Entity     = 一个 uint32 id（本身不带数据）
//   Component  = 纯数据（Transform/PlayerTag/Door/Core/Trap/Room/Wall/Exit）
//   EcsWorld   = 按组件类型分桶的稀疏存储（每类型一张 unordered_map<Entity, Comp>）
//   System     = 自由函数，遍历「同时拥有某几个组件的实体」来推进逻辑
//
// 实体 = 它出现在哪些组件桶里。于是「查询拥有 Transform + Door 的所有实体」就是
// 取两桶交集，天然支持组合。这是 ECS 区别于「一堆继承子类」的本质。
//
// 约定：玩家本身的「单轮状态」（RunState：存活/携带/计时）属于 GDD 的 Run 层，
// 不进 ECS——它跟着循环者走，不是场景里的一个「物件」。ECS 只承载空间实体，
// 玩家的 Transform 由 RunState.player 同步而来（见 GameState::syncPlayerTransform）。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace echo {

using Entity = std::uint32_t;
constexpr Entity kNullEntity = 0;

// ===========================================================================
// 组件（纯数据，无方法）
// ===========================================================================
struct TransformComponent {
    glm::vec2 pos{0.0f, 0.0f};    // 中心
    glm::vec2 size{26.0f, 26.0f}; // 半尺寸的两倍
};

struct PlayerTag {};              // 标记「这是玩家实体」

struct DoorComponent {            // 密码门：记不记得密码决定它锁不锁
    std::string id;
    std::string requiresCode;
    glm::vec4   color{0.88f, 0.26f, 0.30f, 1.0f}; // 锁住时的渲染色
};

struct CoreComponent {            // 情报核心：携带一个密码 payload
    std::string id;
    std::string payload;
    std::string label;
    glm::vec4   color{0.86f, 0.36f, 0.95f, 1.0f}; // 渲染色
};

struct TrapComponent {            // 陷阱：踩到即死；id 用于写进 Meta 的已知陷阱
    int       id = -1;
    std::string label;
};

struct RoomComponent {            // 房间地面（仅渲染）
    std::string id;
    std::string label;
    glm::vec4   color{0.13f, 0.14f, 0.19f, 1.0f};
};

struct WallComponent {            // 墙（仅渲染）
    std::string id;
};

struct ExitComponent {};          // 出口（渲染 + 通关判定）

// 敌兵 FSM 状态（M4 主动威胁）
enum class AIState { Patrol, Alert, Search, Attack };

// 巡逻机器人（M4）：配置来自关卡，运行时字段每轮重置
struct EnemyComponent {
    // —— 配置（关卡静态数据）——
    int                   id = -1;     // 写进 Meta 的「已知敌人」标识
    std::string           label;        // 中文标签（记忆路线提示用）
    std::vector<glm::vec2> patrol;      // 巡逻路点；空 = 原地；patrol[0] 即出生点
    float                 speed = 90.0f;    // 巡逻速度（世界单位/秒）
    float                 vision = 260.0f;  // 视野半径（纯距离，M4 不做射线遮挡）
    float                 alertSpeed = 200.0f; // 发现后的追击速度
    float                 attackRange = 36.0f; // 接触致死距离
    glm::vec2             size = {30.0f, 30.0f};

    // —— 运行时（每轮由 resetEnemies 归零，不进存档）——
    AIState   state        = AIState::Patrol;
    int       patrolIndex  = 0;    // 当前前往的路点下标
    int       patrolDir    = 1;    // +1 / -1 循环方向
    float     searchTimer  = 0.0f; // Search 态倒计时
    glm::vec2 lastKnownPlayer{0.0f, 0.0f}; // 最后已知玩家位置

    // M5：导航运行时（沿 A* 网格路径绕墙，而非直线穿墙）
    std::vector<glm::vec2> path;        // 当前 A* 路径航点（世界坐标）
    int       pathIndex   = 0;          // 正在前往的航点下标
    float     pathTimer   = 0.0f;       // 路径重算节流计时
    int       lastGoalCellX = -1, lastGoalCellY = -1; // 上一次目标格，变了才重算
};

// ===========================================================================
// 世界：按组件类型分桶的稀疏存储
// ===========================================================================
class EcsWorld {
public:
    Entity createEntity() { return m_next++; }

    void destroy(Entity e) {
        m_transforms.erase(e); m_doors.erase(e); m_cores.erase(e);
        m_traps.erase(e);      m_rooms.erase(e); m_walls.erase(e);
        m_exits.erase(e);      m_players.erase(e); m_enemies.erase(e);
        if (e == m_player) m_player = kNullEntity;
    }

    // 给实体挂一个组件
    template <class T>
    void add(Entity e, T comp);

    // 取组件（没有该组件返回 nullptr）
    template <class T>
    T* get(Entity e);
    template <class T>
    const T* get(Entity e) const;

    template <class T>
    bool has(Entity e) const;

    // 查询「同时拥有 Ts... 全部组件的实体」
    template <class... Ts>
    std::vector<Entity> query() const;

    Entity player() const { return m_player; }
    void   setPlayer(Entity e) { m_player = e; }

private:
    std::unordered_map<Entity, TransformComponent> m_transforms;
    std::unordered_map<Entity, DoorComponent>      m_doors;
    std::unordered_map<Entity, CoreComponent>      m_cores;
    std::unordered_map<Entity, TrapComponent>      m_traps;
    std::unordered_map<Entity, RoomComponent>      m_rooms;
    std::unordered_map<Entity, WallComponent>      m_walls;
    std::unordered_map<Entity, ExitComponent>      m_exits;
    std::unordered_map<Entity, PlayerTag>          m_players;
    std::unordered_map<Entity, EnemyComponent>     m_enemies;

    Entity m_next   = 1;
    Entity m_player = kNullEntity;

    template <class T> std::unordered_map<Entity, T>&       store();
    template <class T> const std::unordered_map<Entity, T>& store() const;

    template <class First, class... Rest>
    const std::unordered_map<Entity, First>& firstStore() const { return store<First>(); }
};

// 组件桶的特化（每类组件一张 map）
template <> inline std::unordered_map<Entity, TransformComponent>& EcsWorld::store<TransformComponent>() { return m_transforms; }
template <> inline std::unordered_map<Entity, DoorComponent>&      EcsWorld::store<DoorComponent>()      { return m_doors; }
template <> inline std::unordered_map<Entity, CoreComponent>&      EcsWorld::store<CoreComponent>()      { return m_cores; }
template <> inline std::unordered_map<Entity, TrapComponent>&      EcsWorld::store<TrapComponent>()      { return m_traps; }
template <> inline std::unordered_map<Entity, RoomComponent>&      EcsWorld::store<RoomComponent>()      { return m_rooms; }
template <> inline std::unordered_map<Entity, WallComponent>&      EcsWorld::store<WallComponent>()      { return m_walls; }
template <> inline std::unordered_map<Entity, ExitComponent>&      EcsWorld::store<ExitComponent>()      { return m_exits; }
template <> inline std::unordered_map<Entity, PlayerTag>&          EcsWorld::store<PlayerTag>()          { return m_players; }
template <> inline std::unordered_map<Entity, EnemyComponent>&      EcsWorld::store<EnemyComponent>()      { return m_enemies; }

template <> inline const std::unordered_map<Entity, TransformComponent>& EcsWorld::store<TransformComponent>() const { return m_transforms; }
template <> inline const std::unordered_map<Entity, DoorComponent>&      EcsWorld::store<DoorComponent>()      const { return m_doors; }
template <> inline const std::unordered_map<Entity, CoreComponent>&      EcsWorld::store<CoreComponent>()      const { return m_cores; }
template <> inline const std::unordered_map<Entity, TrapComponent>&      EcsWorld::store<TrapComponent>()      const { return m_traps; }
template <> inline const std::unordered_map<Entity, RoomComponent>&      EcsWorld::store<RoomComponent>()      const { return m_rooms; }
template <> inline const std::unordered_map<Entity, WallComponent>&      EcsWorld::store<WallComponent>()      const { return m_walls; }
template <> inline const std::unordered_map<Entity, ExitComponent>&      EcsWorld::store<ExitComponent>()      const { return m_exits; }
template <> inline const std::unordered_map<Entity, PlayerTag>&          EcsWorld::store<PlayerTag>()          const { return m_players; }
template <> inline const std::unordered_map<Entity, EnemyComponent>&      EcsWorld::store<EnemyComponent>()      const { return m_enemies; }

// 模板成员在特化之后定义，保证实例化时能看到 store<> 的特化
template <class T>
void EcsWorld::add(Entity e, T comp) { store<T>()[e] = std::move(comp); }

template <class T>
T* EcsWorld::get(Entity e) {
    auto it = store<T>().find(e);
    return it == store<T>().end() ? nullptr : &it->second;
}

template <class T>
const T* EcsWorld::get(Entity e) const {
    auto it = store<T>().find(e);
    return it == store<T>().end() ? nullptr : &it->second;
}

template <class T>
bool EcsWorld::has(Entity e) const { return store<T>().count(e) > 0; }

template <class... Ts>
std::vector<Entity> EcsWorld::query() const {
    std::vector<Entity> out;
    if constexpr (sizeof...(Ts) == 0) {
        return out;
    } else {
        const auto& first = firstStore<Ts...>();
        for (const auto& kv : first) {
            const Entity e = kv.first;
            if ((has<Ts>(e) && ...)) out.push_back(e);
        }
    }
    return out;
}

} // namespace echo
