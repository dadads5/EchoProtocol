# Echo Protocol · Milestone 5 计划：敌兵 AI 增强（视线遮挡 + A* 寻路）

> 状态：计划文档（待用户批准编码）
> 前置：M4 已完成（巡逻机器人四态 FSM + 信息积累闭环，217 断言 / 23 用例全绿）
> 目标：把 M4 的「纯距离视野 + 直线追击」升级为「被墙挡住看不见 + 绕墙寻路追击」，
>       让敌兵在复杂关卡里具备真实威胁，且不破坏既有 217/23 契约。

---

## 0. 设计总览（为什么这样做）

M4 的敌兵有两个「假」：
1. **视野是纯距离的** —— 隔一堵墙也能「看见」玩家。
2. **追击是直线飞的** —— 会穿墙（靠关卡设计回避，脆弱）。

M5 用两项经典算法补齐，且都**复用已有的静态层数据** `LevelData`：

- **视线遮挡（Line-of-Sight）**：敌兵→玩家的线段 vs 所有障碍 AABB 求交。被挡住 → 看不见。
- **A\* 寻路**：在关卡 bounds 上铺一张网格，把障碍栅格化，敌兵在 Alert/Search 时沿网格路径绕墙逼近，
  而非直线。

**障碍定义（关键设计）**：障碍 = 所有 `walls` + 所有「未记住密码的 `doors`」（`requiresCode` 非空且 `!meta.knowsPassword`）。
这意味着：
- 关着的门既挡视线也挡路 → 玩家在门另一侧时敌兵看不见、也过不来（门的物理保护成立）。
- 一旦玩家记住密码（Meta 层），门「打开」→ 视线与路径都打通 → 敌兵能追进来。
- 这把 M5 和既有的「三层状态 / Meta 记忆」架构自然咬合，是绝佳的架构展示点。

**不变量**：现有 16（原）+ 7（M4）= 23 用例、217 断言零改动全部保留。新增字段都有默认值、
`resetEnemies` 会清理；测试夹具（无 walls/doors）下 LOS 永远通、nav 永远空 → 旧行为完全等价。

---

## 1. 新增导航/视线工具（声明在 `Systems.h`，实现在 `Systems.cpp`）

### M5.1 `NavGrid` 结构 + 构建
```cpp
struct NavGrid {
    glm::vec2 min, max;
    float cell = 40.0f;
    int cols = 0, rows = 0;
    std::vector<bool> solid;          // cols*rows，true=不可走

    bool inBounds(int c, int r) const;
    bool isSolid(int c, int r) const;
    glm::vec2 cellCenter(int c, int r) const;
    std::pair<int,int> worldToCell(const glm::vec2& p) const;
    int  indexOf(int c, int r) const;

    // A*（8 邻接，禁止穿角），返回世界坐标航点（已去掉共线点）。
    // 起/终点落在实心格时，先环形 BFS 找最近空格，避免卡死。
    std::vector<glm::vec2> findPath(const glm::vec2& from, const glm::vec2& to) const;
};

// 从静态层构建：障碍 = walls + 未记住密码的 doors；每个格子按 agentRadius 外扩后
// 与障碍 AABB 相交即标记 solid。agentRadius 默认 24（覆盖默认敌兵半尺寸 15 + 余量）。
NavGrid buildNavGrid(const LevelData& level, const MetaState& meta,
                     float agentRadius = 24.0f);
```

### M5.2 视线函数
```cpp
// 线段 a→b 是否与 AABB r 相交（含端点在内），用 Liang–Barsky 裁剪，O(1)。
bool segmentIntersectsRect(const glm::vec2& a, const glm::vec2& b, const Rect& r);

// 敌兵 a 能否看见玩家 b：遍历 walls + 未记住密码的 doors，任一相交即 false。
bool hasLineOfSight(const glm::vec2& a, const glm::vec2& b,
                    const LevelData& level, const MetaState& meta);

// 安全网：点是否落在任意障碍（wall / 未开 door）内，供敌兵移动后回退。
bool pointInObstacle(const glm::vec2& p, const LevelData& level, const MetaState& meta);
```
复用 `GameState.h` 既有 `Rect / overlapRect / pointInRect / clampToBounds`，不重写。

---

## 2. 组件与系统改造

### M5.3 `EnemyComponent` 增运行时导航字段（`World.h`）
```cpp
// —— 运行时（每轮 resetEnemies 归零）——
std::vector<glm::vec2> path;   // 当前 A* 路径航点（世界坐标）
int       pathIndex  = 0;      // 正在前往的航点下标
float     pathTimer  = 0.0f;   // 路径重算节流计时
int       lastGoalCellX = -1, lastGoalCellY = -1; // 上一次目标格，变了才重算
```
（配置字段不变；注释里把 `vision` 的「M4 不做射线遮挡」删掉，改为「M5 起带射线遮挡」。）

### M5.4 `enemySystem` 改造（`Systems.cpp`）
- **签名**：`enemySystem(float dt, const RunState& run, const LevelData& level,
   const MetaState& meta, EcsWorld& world, EventBus& bus)`（多一个 `meta`）。
- 帧首（alive 检查后）**构建一次** `NavGrid nav = buildNavGrid(level, meta);`，所有敌兵共用。
- **顶部接触致死守卫**：保留不变（`dist <= attackRange` 即发 `EnemyHitEvent` + Attack）。
- **状态转移（检测需 LOS）**：
  ```cpp
  case Patrol: case Search:
      if (dist <= en->vision && hasLineOfSight(t->pos, playerPos, level, meta)) {
          en->state = Alert; en->lastKnownPlayer = playerPos;
      }
  ```
  （Alert→Search 失去视野 3s、Search→Patrol 计时结束，逻辑不变。）
- **行为（Alert/Attack/Search 改为沿路径）**：
  - Patrol：不变（巡逻路点由关卡设计保证不穿墙）。
  - 目标世界点 = `Alert/Attack`→`playerPos`；`Search`→`lastKnownPlayer`。
  - **重算节流**：`en->pathTimer -= dt`；当 `pathTimer<=0` 或玩家所在格相对上次变了 →
    `en->path = nav.findPath(t->pos, goal); en->pathIndex=0; en->pathTimer=0.3f;`
  - **跟随路径**：若 `!path.empty()` 且 `pathIndex<path.size()`：
    `target = path[pathIndex]`；`if (distance(t->pos,target) < cell*0.6) ++pathIndex;`；
    朝 `target` 以 `alertSpeed` 移动。路径空（无解/已到）→ 退回直线 `target=goal`。
  - **安全网**：移动前存 `prev = t->pos`；移动后若 `pointInObstacle(t->pos,...)` → `t->pos = prev;`。
  - 末 `clampToBounds`（保留）。
- `resetEnemies`：`path.clear(); pathIndex=0; pathTimer=0; lastGoalCellX/Y=-1;`（其余不变）。

### M5.5 `Game.cpp` 调用更新
- 第 83 行：`enemySystem(dt, m_state.run, m_state.world, m_state.meta, m_state.ecs, m_bus);`

### M5.6 `test_enemy.cpp` 调用更新
- 7 处 `enemySystem(...)` 调用补一个 `MetaState meta;`（夹具无 walls/doors → 行为完全等价）。

---

## 3. 单元测试（新增 `tests/test_nav.cpp`，约 8 用例 → 23→~31 用例，217→~250 断言）

1. `segmentIntersectsRect` 基础：穿插=true / 端点在内=true / 完全错过=false。
2. `hasLineOfSight`：中间放一面墙 → 被挡(false)；移开墙 → 通(true)。
3. `NavGrid::findPath` 绕墙：正前方一堵墙挡死直线，路径存在、所有航点格均非 solid、
   路径长度 > 曼哈顿、相邻航点 8 邻接（不穿角）。
4. **敌兵只在 LOS 清晰时发现**：敌兵与玩家距离 < vision，但中间一堵墙 → 保持 Patrol；
   去掉墙 → 变 Alert。（程序化构造带 wall 的 `LevelData` + 默认 `MetaState`。）
5. **敌兵绕墙逼近**：障碍迫使绕行、但 LOS 通（无墙挡视线）→ 多帧后敌兵净靠近玩家，
   且全程位置从不落入 `pointInObstacle`（没穿墙）。
6. **条件门**：同一道 `requiresCode` 门，关着时挡 LOS+nav（路径需绕或不可达）；
   `meta.rememberPassword(code)` 后 → LOS 通、路径直达。
7. **真实关卡可导航**：`level01.json` 构建的 nav 网格，spawn 与 exit 格均为空，
   `findPath(spawn, exit)` 非空（带默认空 Meta，门关闭时仍能从 spawn 走到 exit？——
   注意：level01 玩家出生在 airlock 左侧、exit 在右侧，两道关着的门会挡路 →
   此用例改为断言「spawn 与 exit 格非 solid 且 findPath 返回（可能需绕/或仅验证网格有效）」，
   或临时 `rememberPassword` 两密码后再断言直达。用后者更干净。）
8. `resetEnemies` 清空 `path`（接 M4 已测项，补一行断言）。

契约保护：原 `test_gamestate.cpp` 16 用例、`test_enemy.cpp` 7 用例一字不改（仅补 meta 实参）。

---

## 4. 渲染增强（`src/main.cpp`，可演示性）

- **6.1 敌兵本体**：当 `state==Alert||Attack` 且 `en->path` 非空时，额外用淡黄虚线折线
  画它的当前导航路径（世界坐标，`kPathHint=(1.0,0.85,0.2,0.5)`）——直观展示「绕墙追击」。
  可选：Alert 时从敌兵向玩家画一条极淡的视线连线，被墙挡住时断开（演示 LOS）。
- **6.2 记忆路线**：不变。
- HUD：不动（敌兵计数已存在）。

---

## 5. 构建 / 文档 / 预览（交付）

- `CMakeLists.txt`：无需改（`NavGrid` 并入 `Systems.cpp`）。
- 验证：跑 `_verify_build.py`（6 target 全绿）→ 直接执行 `EchoProtocolTests.exe` 确认
  `All tests passed (≈250 assertions in ≈31 test cases)`，旧 23 用例零回归。
- 文档：
  - `README.md`：进度头「M5 敌兵增强 ✅」、测试数 217/23 → ≈250/31、新增 M5 章（LOS + A* + 条件门）。
  - `EchoProtocol_M2_时间循环手册.md`：补 §14 M5。
  - `EchoProtocol_GDD_v2.md`：Milestone 5 标记 ✅（视线遮挡 + 寻路实现；音效/Demo 录制仍为可选）。
- 预览：`_gen_m5_mockup.py`（复用暂停菜单预览风格）渲染「墙挡视线 / 机器人绕墙」两态，
  生成 `EchoProtocol_M5_敌兵增强预览.html`。

---

## 6. 任务拆分（编码阶段）

- M5.1 `NavGrid` 结构 + `buildNavGrid` + `findPath`（8 邻接 A*、最近空格回退、共线化简）
- M5.2 `segmentIntersectsRect` / `hasLineOfSight` / `pointInObstacle`
- M5.3 `EnemyComponent` 运行时导航字段
- M5.4 `enemySystem` 改造（签名+LOS 检测+路径跟随+安全网）
- M5.5 `Game.cpp` 调用补 `meta`
- M5.6 `test_enemy.cpp` 调用补 `meta`
- M5.7 `tests/test_nav.cpp` 新单测（8 用例）
- M5.8 `main.cpp` 渲染路径/视线
- M5.9 构建验证 + 文档 + 预览

## 7. 风险与回滚

- 唯一破坏性改动是 `enemySystem` 签名（加 `meta`）；所有调用点（Game.cpp + test_enemy.cpp）同步更新即可。
- 新字段均有默认值，`resetEnemies` 清理；旧断言不依赖这些字段。
- 若某关 nav 网格在极端 bounds 下退化（cell 过大/过小），`buildNavGrid` 内部对 cols/rows 做 `>=1` 防御，
  且 `findPath` 对空网格安全返回空路径（敌兵退回直线）。
