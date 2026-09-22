// src/game/SceneBuilder.h —— 把「关卡资产」实例化成 ECS 实体世界（M3）
//
// LevelData 是数据驱动读出来的「prefab / 资产」（房间/墙/门/核心/陷阱/出口/出生点）。
// buildScene() 把它一次性展开成 ECS 实体：每个物件一个 Entity，挂上对应组件。
// 之后运行时只改 Run 层与 Meta 层，实体本身（静态世界）不再重建。
#pragma once

#include "ecs/World.h"
#include "game/GameState.h" // LevelData

namespace echo {

// 把 level 展开成实体世界（会先清空 world 再填充）。
// 玩家被登记为 world.player()，供系统与渲染识别。
void buildScene(const LevelData& level, EcsWorld& world);

} // namespace echo
