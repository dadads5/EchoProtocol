# 《Echo Protocol》M4 技术方案：敌兵 AI（FSM 巡逻机器人）

> 状态：方案稿（待评审，未写码）
> 目标里程碑：GDD v2 的 **Milestone 4** 核心（AI：FSM 含 Patrol / Alert / Search / Attack）。
> 当前代码已完成：M0 脚手架 / M1 渲染 / M2 时间循环 + HUD + 暂停菜单 + 中文化 / M3 真 ECS 重构 + 第二关 + 选关菜单 + 着色器资源化。
> 不变量承诺：**绝不破坏现有 158 断言 / 16 用例的单测契约**；Shader、存档磁盘格式、现有系统零回归。

---

## 1. 目标与范围

新增一类**主动威胁实体——巡逻机器人（Enemy）**，让时间循环世界从"只有静态陷阱"升级为"有会追杀你的东西"。

具体交付：
1. 真 ECS 上的一个 `EnemyComponent` + 一个 `enemySystem`（FSM：Patrol → Alert → Search → Attack）。
2. 数据驱动：关卡 JSON 加 `enemies[]`，由 `LevelLoader` 解析、`SceneBuilder` 展开。
3. 敌人**致死走事件通道**（复用 `LoopSystem` 的死亡/重置/落盘收口），与陷阱同链路。
4. **信息积累闭环（核心卖点呼应）**：被某敌人杀死 → 永久记住该敌人 → 之后每轮在屏幕上画出它的**巡逻路线警示线 + 标签**，让玩家靠记忆提前规避。与"记忆陷阱才可见"完全对称。
5. 单测 `test_enemy.cpp`（解析 / 巡逻 / 视野发现 / 追击致死 / 重置归位 / 记忆落盘）。
6. 文档（README / M2 手册 / GDD 里程碑）+ 三态预览图（巡逻 / 追击 / 记忆路线）。

**M4 明确不做**（防时间黑洞，GDD 第 19 节已警示）：
- 不做墙体射线遮挡（视野用纯距离半径判定）。
- 不做 A\* / 寻路（敌人移动仅 `clampToBounds`，巡逻路线由关卡设计保证在开放走廊、不穿墙）。
- 不做行为树（M4 用四态 FSM 足矣，行为树留作 M5 可选）。

---

## 2. 设计原则

1. **复用现有骨架**：敌人是一个 ECS 实体 + 一个 System，跟 Trap/Door 完全一致的接入模式（`World.h` 加组件、`Systems.h/.cpp` 加系统、`main.cpp` 加绘制、`LevelLoader` 加解析）。
2. **零耦合、事件收口**：敌人造成伤害只 `publish(EnemyHitEvent)`，由 `LoopSystem` 统一 `killPlayer` / 记账 / 写记忆 / 落盘 / 帧末重置。绝不在 `enemySystem` 里当场 reset 世界。
3. **不破坏单测契约**：新增 `MetaState` 字段用 `j.contains()` 守卫读取（旧存档 / 旧测试喂的 `{"loops":4}` 不含新字段，安全默认）；新增组件不影响现有 `query<...>`（现有系统按特定组件组合查询）；`beginNewLoop` 加的敌人复位对"无敌人"关卡是 no-op。
4. **信息积累对称**：陷阱 = 踩过才可见位置；敌人 = 被杀过才可见巡逻路线。两者共用"死亡写记忆"链路，强化"我知道更多所以我能做得更好"的叙事。

---

## 3. 敌兵 FSM 设计

状态枚举（放在 `World.h`，紧邻 `EnemyComponent`）：

```cpp
enum class AIState { Patrol, Alert, Search, Attack };
```

转移表：

| 当前态 | 条件 | 下一态 | 行为 |
|--------|------|--------|------|
| Patrol | 玩家进入 `vision` 半径 | Alert | 记录 `lastKnownPlayer`，切 `alertSpeed` 朝玩家移动 |
| Patrol | —— | Patrol | 沿 `patrol[]` 路点循环移动 |
| Alert  | 距离 ≤ `attackRange` | Attack | 发 `EnemyHitEvent`（致死） |
| Alert  | 玩家离开 `vision` 半径 | Search | 朝 `lastKnownPlayer` 移动，`searchTimer = K` |
| Search | 到达 `lastKnownPlayer` 或 `searchTimer` 耗尽 | Patrol | `patrolIndex` 重置到最近路点 |
| Search | 玩家重新进入 `vision` | Alert | 刷新 `lastKnownPlayer` |
| Attack | （致死瞬间，由 LoopSystem 收口重置）| —— | 不再自转移，帧末重置 |

> 说明：视野用**纯距离半径**判定（M4 不做射线遮挡）。`attackRange` 默认略大于敌人半尺寸（接触即死）。

---

## 4. 信息积累闭环（与陷阱对称）

```
玩家被敌人 #k 杀死
   └─ enemySystem: publish(EnemyHitEvent{k, pos})
        └─ LoopSystem::onEnemyHit:
             meta.rememberEnemy(k)        // 新增：knownEnemyIds
             killPlayer("enemy #k", -1)   // trapId=-1，不写 knownTrap
                  └─ publish(PlayerDiedEvent)
                       └─ onPlayerDied: deaths++ ; persist() ; m_pendingDeath=true
                            └─ 帧末 beginLoop → 新一轮
下一轮渲染：
   若 meta.knowsEnemy(k)：
     ① 在该敌人 patrol[] 路点间画淡红警示折线（"已知巡逻路线"）
     ② 在敌人旁画中文标签「巡逻兵 #k」（或状态提示）
   否则：不显示任何危险提示（与"未踩过的陷阱不可见"一致）
```

这样玩家第一轮被追杀、记住路线后，后续轮次能提前规划规避——这就是时间循环机制在"主动威胁"上的牙齿。

---

## 5. 数据驱动 Schema

`LevelData`（GameState.h）新增：

```cpp
struct EnemyDef {
    int id = -1;                       // 用于写进 Meta 的已知敌人
    std::string label;                 // 中文标签（渲染提示用）
    std::vector<glm::vec2> patrol;     // 巡逻路点；空 = 原地；patrol[0] 即出生点
    float speed = 90.0f;               // 巡逻速度（世界单位/秒）
    float vision = 260.0f;             // 视野半径（纯距离）
    float alertSpeed = 200.0f;         // 发现后的追击速度
    float attackRange = 36.0f;         // 接触致死距离
    glm::vec2 size = {30.0f, 30.0f};
};
// LevelData 内加：
std::vector<EnemyDef> enemies;
```

JSON 示例（`level01.json` / `level02.json` 各加 `enemies[]`）：

```json
"enemies": [
  {
    "id": 1,
    "label": "巡逻兵 α",
    "patrol": [[-300, 0], [300, 0]],
    "speed": 90.0,
    "vision": 260.0,
    "alertSpeed": 200.0,
    "attackRange": 36.0
  },
  {
    "id": 2,
    "label": "巡逻兵 β",
    "patrol": [[600, -80], [600, 80], [900, 80], [900, -80]],
    "speed": 110.0,
    "vision": 300.0
  }
]
```

`LevelLoader.cpp`：复制 `traps` 的 `readArray` 模式，新增 `enemies` 解析 lambda（含 `id` 自动编号兜底，与 trap 一致）。
`SceneBuilder.cpp`：`buildScene` 内加一个 `for (const EnemyDef& e : level.enemies)` 循环，`createEntity()` + 挂 `TransformComponent{patrol[0] 或 center, size}` + 挂 `EnemyComponent{...}`（配置 + 初始运行时 `state=Patrol, patrolIndex=0, patrolDir=1`）。

**关卡可通关约束**：敌人巡逻路线须留给玩家绕过空间 / 巡逻间隙；不能完全堵死唯一通道。`level02` 走廊较长，适合放 1–2 个横向巡逻兵。

---

## 6. 逐文件改动清单（精确到接入点）

### 6.1 `src/ecs/World.h`
- 在组件区（行 37–70 之间）新增 `enum class AIState {...}` 与 `struct EnemyComponent {...}`（含 §5 配置字段 + 运行时字段 `state/alertLevel/searchTimer/patrolIndex/patrolDir/lastKnownPlayer`）。
- 私有成员区（行 107–114）新增 `std::unordered_map<Entity, EnemyComponent> m_enemies;`
- `destroy()`（行 79–84）补充 `m_enemies.erase(e);`
- `store<>` 特化：在行 127–143 的 8 对之后，新增 `EnemyComponent` 的非 const + const 两对特化。

### 6.2 `src/game/GameState.h`
- `LevelData` 结构体（行 97–112）新增 `std::vector<EnemyDef> enemies;` 与 `struct EnemyDef`（§5）。
- `MetaState`（行 117–139）新增 `std::vector<int> knownEnemyIds;`；新增 `knowsEnemy(int)` / `rememberEnemy(int)`（去重，与 `knowsTrap`/`rememberTrap` 对称实现，见 `GameState.cpp`）。

### 6.3 `src/game/GameState.cpp`
- 实现 `knowsEnemy` / `rememberEnemy`（仿 `knowsTrap` / `rememberTrap`，行 55–94 附近）。

### 6.4 `src/core/Events.h`
- 新增事件（仿 `TrapHitEvent`，行 22–25）：
  ```cpp
  struct EnemyHitEvent { int enemyId = -1; glm::vec2 pos{0,0}; };
  ```

### 6.5 `src/game/SaveSystem.cpp`
- `toJsonText`（行 14–25）：增一行写 `knownEnemyIds`（仿 `knownTrapIds`）。
- `fromJsonText`（行 27–56）：用 `if (j.contains("knownEnemyIds"))` 守卫读取（与现有 `knownTrapIds` 同模式）—— **保证旧存档 / 旧测试安全**。

### 6.6 `src/game/systems/Systems.h`
- 新增声明（仿 `trapSystem`，行 30）：
  ```cpp
  void enemySystem(float dt, const RunState& run, EcsWorld& world, EventBus& bus);
  void resetEnemies(EcsWorld& world);   // 归位 + FSM 复位（订阅 LoopStartedEvent 触发）
  ```

### 6.7 `src/game/systems/Systems.cpp`（或新建 `EnemySystem.cpp`）
- 实现 `enemySystem`：按 §3 FSM；读 `run.player` 作为玩家位置（与 `trapSystem` 一致，玩家单轮状态在 Run 层，**不**读 ECS 玩家 Transform）；视野用距离；接触发 `EnemyHitEvent`；移动用 `clampToBounds`。
- 实现 `resetEnemies`：遍历 `world.query<TransformComponent, EnemyComponent>()`，把 `Transform.pos` 复位到 `patrol[0]`（空则不动），运行时字段归零、`state=Patrol`、`patrolIndex=0`。

### 6.8 `src/game/systems/LoopSystem.cpp`
- `subscribe()`（行 43–58）新增 `m_bus.subscribe<EnemyHitEvent>(... onEnemyHit ...)`。
- 新增 `onEnemyHit(const EnemyHitEvent& e)`：`if (m_gs.meta.rememberEnemy(e.enemyId)) logLine("  memory += enemy #" + id + "  (its patrol route is now marked on every future loop)");` 然后 `killPlayer("enemy #" + id, -1);`（注意 `trapId=-1`，不写陷阱记忆，仅写敌人记忆）。

### 6.9 `src/game/Game.cpp`
- `init()`（行 18–51）订阅区（行 43–44 附近）加：`m_bus.subscribe<LoopStartedEvent>([this](const LoopStartedEvent&){ resetEnemies(m_state.ecs); });` —— 注册一次，切关（`loadLevel` 也 `beginLoop` 发此事件）同样触发复位。
- `update()`（行 58–90）：在步骤 2（movement，行 73）之后、步骤 3（doorBlock，行 77）之前插入：
  ```cpp
  // --- 2.5 敌人（FSM 巡逻 / 追击 / 致死）---
  enemySystem(dt, m_state.run, m_state.ecs, m_bus);
  if (m_loop.finishFrameIfDead()) return;
  ```

### 6.10 `src/main.cpp`
- 配色区（行 79–89）新增 `const glm::vec4 kEnemyColor(0.95f, 0.32f, 0.18f, 1.00f);`（亮红橙，区别于 `kKnownTrap` 的半透明暗红）。
- 渲染区：在"6) 记忆陷阱"（行 496–502）之后加两段：
  - **敌人本体**：`query<Transform, Enemy>` 绘制；按 `AIState` 变色（Patrol=`kEnemyColor`；Alert/Attack=更亮红 `vec4(1.0,0.2,0.1,1)`）；Search=偏橙。
  - **记忆巡逻路线**：若 `meta.knowsEnemy(id)`，在该敌人 `patrol[]` 路点间画淡红折线（`rects.draw` 画细矩形段或线段），并在敌人旁用 `cjk.draw` 画标签「巡逻兵 #id」。

---

## 7. 任务拆分（建议执行顺序）

| 任务 | 内容 | 关键产物 |
|------|------|----------|
| M4.1 | 数据层 | `EnemyDef` + `LevelData.enemies` + `LevelLoader` 解析 + 解析单测 |
| M4.2 | ECS 组件 | `AIState` + `EnemyComponent`；`World.h` 的 map / store 特化 / destroy erase |
| M4.3 | 场景展开 | `SceneBuilder` 加 enemies 循环 |
| M4.4 | 事件 + 记忆 | `EnemyHitEvent`；`LoopSystem` 订阅 + `onEnemyHit`；`Meta.knownEnemyIds` + `SaveSystem` 读写（j.contains 守卫） |
| M4.5 | FSM 逻辑 | `enemySystem` 四态实现（视野 / 追击 / 致死走事件） |
| M4.6 | 重置归位 | `resetEnemies` + `Game::init` 订阅 `LoopStartedEvent`（切关复用） |
| M4.7 | 主循环集成 | `Game::update` 插入 `enemySystem` + 帧末 `finishFrameIfDead` |
| M4.8 | 渲染 | `kEnemyColor` + 敌人本体绘制（FSM 变色）+ 记忆巡逻路线 + 标签 |
| M4.9 | 关卡内容 | `level01.json` / `level02.json` 加 `enemies[]`（保证可通关不卡死） |
| M4.10 | 单测 | 新建 `tests/test_enemy.cpp` + `CMakeLists.txt` 第 77 行追加源；6+ 用例 |
| M4.11 | 文档 + 预览 | README / M2 手册 / GDD 补 M4 章节；三态预览 HTML |
| M4.12 | 构建验证 | 沙箱 `_verify_build.py` 6 target 全绿；单测全过、断言数增长 |

---

## 8. 单测计划（`tests/test_enemy.cpp`）

复用 `test_gamestate.cpp` 的范式（`Game(bus)` + 临时 JSON + `game.update(dt, input)` 推进，见行 254–305）。用例：

1. **解析**：含 `enemies[]` 的 JSON → `parseLevelJson` 后 `lv.enemies.size()` 正确、`patrol` 点数、`speed/vision` 取值正确、缺 `id` 自动编号。
2. **巡逻移动**：玩家远离敌人 → 推进若干帧 → 敌人 `Transform.pos` 在 `patrol[0]↔patrol[1]` 间推进且 `state==Patrol`。
3. **视野发现**：把玩家放到敌人 `vision` 内 → 一帧后 `state==Alert`。
4. **追击致死**：玩家贴脸（距离 ≤ `attackRange`）→ `EnemyHitEvent` → 帧末收口 → `meta.deaths==1`、`run.alive` 重置、`meta.loops==2`。
5. **重置归位**：触发一次死亡后进入第 2 轮 → 敌人 `Transform.pos` 回到 `patrol[0]`、`state==Patrol`。
6. **记忆落盘**：被敌人 #k 杀死后 → `meta.knowsEnemy(k)` 为 true；`save::readFile` 后 `fromDisk.knowsEnemy(k)` 为 true（验证 `j.contains` 守卫不影响旧字段）。
7. **旧契约不变**（回归）：现有 `tests/test_gamestate.cpp` 的 16 用例 / 158 断言全过（新增字段用守卫读取，零影响）。

> 注：单测只测逻辑层（`echo_core` 静态库，不含 SDL/GL）。敌人渲染只在 `main.cpp`，不入单测。

---

## 9. 风险与坑（来自代码事实）

1. **ECS 世界不在每轮重建**：`beginNewLoop()`（GameState.cpp 114–119）只动 Run 层、`buildScene` 不重跑。敌兵运行时位置 / FSM 状态会残留 → **必须 `resetEnemies` 在 `LoopStartedEvent` 时显式复位**（M4.6 处理）。
2. **死亡统一帧末收口**：`LoopSystem::finishFrameIfDead()`（LoopSystem.cpp 70–75）才 `beginLoop`。`enemySystem` 致死只能发事件，不能在系统里当场 reset。
3. **store 特化漏写 = 链接失败**：新增 `EnemyComponent` 必须同时加非 const + const 两对 `store<>` 特化 + `destroy` 里 `erase`，否则 `add/get/has/query` 链接不过。
4. **旧存档兼容**：`MetaState` 新字段一律 `j.contains` 守卫读取（仿现有 `knownTrapIds`），保证 §8.7 旧契约与 `a save file missing fields still loads with defaults` 测试不受影响。
5. **玩家位置来源**：敌人读 `RunState.player`（单轮状态在 Run 层），**不**读 ECS 玩家 Transform（后者只是镜像，见 GameState.h 注释 17–19）。
6. **可通关性**：关卡设计时敌人路线须留绕过空间，避免"唯一通道被巡逻兵永久堵死"导致无法通关。

---

## 10. 验证标准

- `python _verify_build.py`：6 target 全绿（含 `EchoProtocolTests`）。
- 单测：`All tests passed (≥ N assertions in ≥ 17 test cases)`（新增 `test_enemy.cpp`，旧 16 用例 / 158 断言零回归）。
- 手感（本机 VS 跑）：第一关放 1 个横向巡逻兵、第二关放 1–2 个；被追杀→记住路线→下轮看到淡红巡逻警示线并能规避。
- 预览图：三态（巡逻 / 追击变亮红 / 记忆巡逻路线）按真实布局渲染到 HTML。

---

## 11. 工作量估计

- 代码改动量：~8 文件、约 200–300 行新增（含单测）。
- 属"中等偏上"功能，但接入点清晰、模式成熟（全复用 Trap/Door 既有模式），风险可控。
- 预计一次性按计划执行即可，无需探索性试错。
