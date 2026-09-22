// src/game/SceneBuilder.cpp
#include "game/SceneBuilder.h"

namespace echo {

void buildScene(const LevelData& level, EcsWorld& world) {
    world = EcsWorld(); // 清空（含玩家登记）

    for (const RoomDef& r : level.rooms) {
        const Entity e = world.createEntity();
        world.add(e, TransformComponent{r.rect.center, r.rect.size});
        world.add(e, RoomComponent{r.id, r.label, r.color});
    }
    for (const WallDef& w : level.walls) {
        const Entity e = world.createEntity();
        world.add(e, TransformComponent{w.rect.center, w.rect.size});
        world.add(e, WallComponent{w.id});
    }
    for (const TrapDef& t : level.traps) {
        const Entity e = world.createEntity();
        world.add(e, TransformComponent{t.rect.center, t.rect.size});
        world.add(e, TrapComponent{t.id, t.label});
    }
    for (const ItemDef& it : level.items) {
        const Entity e = world.createEntity();
        world.add(e, TransformComponent{it.rect.center, it.rect.size});
        world.add(e, CoreComponent{it.id, it.payload, it.label, it.color});
    }
    for (const DoorDef& d : level.doors) {
        const Entity e = world.createEntity();
        world.add(e, TransformComponent{d.rect.center, d.rect.size});
        world.add(e, DoorComponent{d.id, d.requiresCode, d.color});
    }

    // 敌兵（M4）：巡逻机器人。配置来自 LevelData.enemies，运行时字段初值为 Patrol。
    for (const EnemyDef& e : level.enemies) {
        const Entity ent = world.createEntity();
        const glm::vec2 spawn = e.patrol.empty() ? level.spawn : e.patrol[0];
        world.add(ent, TransformComponent{spawn, e.size});

        EnemyComponent ec;
        ec.id            = e.id;
        ec.label         = e.label;
        ec.patrol        = e.patrol;
        ec.speed         = e.speed;
        ec.vision        = e.vision;
        ec.alertSpeed    = e.alertSpeed;
        ec.attackRange   = e.attackRange;
        ec.size          = e.size;
        ec.state         = AIState::Patrol;
        ec.patrolIndex   = 0;
        ec.patrolDir     = 1;
        ec.searchTimer   = 0.0f;
        ec.lastKnownPlayer = spawn;
        world.add(ent, std::move(ec));
    }

    // 出口
    {
        const Entity e = world.createEntity();
        world.add(e, TransformComponent{level.exitRect.center, level.exitRect.size});
        world.add(e, ExitComponent{});
    }

    // 玩家（登记为 world.player()）
    {
        const Entity p = world.createEntity();
        world.add(p, TransformComponent{level.spawn, {26.0f, 26.0f}});
        world.add(p, PlayerTag{});
        world.setPlayer(p);
    }
}

} // namespace echo
