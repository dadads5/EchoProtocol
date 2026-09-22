// Events.h —— 游戏事件定义（M2）
//
// 事件 = 系统之间唯一的通信方式。谁发布、谁订阅，都在这里能一眼看全。
//
//   发布者                 事件                     订阅者
//   --------------------   ---------------------   ----------------------
//   CollisionSystem        TrapHitEvent            LoopSystem（判定死亡）
//   LoopSystem             LoopTimeoutEvent        LoopSystem（判定死亡）
//   LoopSystem             PlayerDiedEvent         LoopSystem（记账 + 写盘）
//   LoopSystem             PasswordCollectedEvent  MemorySystem（写入 Meta）
//   LoopSystem             DoorOpenedEvent         MemorySystem（永久开门）
//   LoopSystem             LoopStartedEvent        HUD / 音效（M2 只打日志）
//   LoopSystem             LevelEscapedEvent       HUD / 关卡系统
#pragma once

#include <glm/glm.hpp>
#include <string>

namespace echo {

// 踩到陷阱。trapId 对应 level.json 里 traps[].id
struct TrapHitEvent {
    int       trapId = -1;
    glm::vec2 pos{0.0f, 0.0f};
};

// 被巡逻机器人命中（M4）。enemyId 对应 level.json 里 enemies[].id
struct EnemyHitEvent {
    int       enemyId = -1;
    glm::vec2 pos{0.0f, 0.0f};
};

// 敌兵从「巡逻 / 搜索」转入「警觉（发现玩家）」态（M6 音效触发点）。
// 仅在状态跃迁的那一帧发布一次，避免每帧刷屏（音效也只需要"发现"的那一声）。
struct EnemyAlertEvent {
    int       enemyId = -1;
    glm::vec2 pos{0.0f, 0.0f};
};

// 单轮时间耗尽（GDD 第 12 节的主失败源）
struct LoopTimeoutEvent {
    int loopIndex = 0;
};

// 玩家死亡。cause 用于日志；trapId >= 0 表示死于陷阱（会写进 Meta 的"已知陷阱"）
struct PlayerDiedEvent {
    int         loopIndex = 0;
    glm::vec2   pos{0.0f, 0.0f};
    std::string cause;
    int         trapId = -1;
};

// 读取到情报（密码）。因为时间循环者记得，所以这一步直接写入 Meta 层，永久保留。
struct PasswordCollectedEvent {
    std::string code;
    glm::vec2   pos{0.0f, 0.0f};
};

// 新一轮开始（Run 层已重置完毕，Meta 层原封不动）
struct LoopStartedEvent {
    int loopIndex = 0;
};

// 门被打开：不是"这轮拿到了钥匙"，而是"记忆里本来就知道密码"
struct DoorOpenedEvent {
    std::string doorId;
    std::string code;
};

// 玩家踩上传送门被送到另一端（第四关）。groupId = 配对键（"A"/"F"/...）
struct TeleportUsedEvent {
    std::string groupId;
    glm::vec2   pos{0.0f, 0.0f}; // 传送到的那一端的位置
};

// 抵达出口，本轮通关
struct LevelEscapedEvent {
    int   loopIndex = 0;
    float runTime   = 0.0f;
};

} // namespace echo
