# Echo Protocol — M2 时间循环核心 手册

> 本手册讲清楚 M2 交付了什么、怎么跑、怎么验证，以及这套架构为什么要这么分。
> 设计源头见 `E:/wb_data/游戏demo/EchoProtocol_GDD_v2.md`（GDD v2 第 11~15 章）。

---

## 1. M2 交付了什么

M1 只把「能画一个会移动的世界」跑通。M2 把**游戏的灵魂——时间循环 + 信息永久积累**落地了：

- ✅ **三层状态模型**（Meta / Run / Static）真正分开，Reset 只清 Run，Meta 永远不动
- ✅ **事件总线（EventBus）**：系统级解耦，碰撞系统、循环系统、记忆系统互不相识
- ✅ **数据驱动关卡**：关卡是 JSON，不是硬编码，改关只改 `assets/levels/level01.json`
- ✅ **永久存档**：Meta 落盘到 `profile.json`，重开程序还记得密码和陷阱位置
- ✅ **完整的回合流程**：移动 / 门阻挡 / 情报拾取 / 出口 / 陷阱致死 / 倒计时耗尽 → 自动 Reset
- ✅ **单元测试**：Catch2 覆盖三层重置语义、存档往返、关卡解析、事件路由、端到端死亡与通关重跑、第二关结构与切关记忆隔离、敌兵 FSM 与「被杀记忆巡逻路线」、视线遮挡与 A* 绕墙寻路、程序化音效合成器，以及真实关卡「假密码诱饵」不变量（339 断言 / 34 用例全过）

**M2 + M2.1 + M2.2 + M3 现已构成一张可玩、可验证、且像样的时间循环切片**：
循环机制、记忆机制、关卡内容、专业 HUD、暂停菜单均已就位；
真正的轻量 ECS 重构、第二个差异化关卡 + 关卡选择、着色器抽资源文件等强化项**均已完成**（见第 9–12 节）。

---

## 2. 核心架构：三层状态模型

这是整个项目最该讲清楚的一点（游戏程序岗面试高频题：**「你的游戏怎么管理存档/回合/世界数据？」**）。

```
┌────────────────────────────────────────────────────────────┐
│  MetaState  （永久层 / 跨循环）                              │
│  loops, deaths, escapes, bestTime,                         │
│  knownPasswords, knownTrapIds, openedDoorIds                │
│  → beginNewLoop() 永远不碰它；只有死亡/拾取时 +=            │
│  → 唯一落盘的一层（SaveSystem ↔ profile.json）              │
├────────────────────────────────────────────────────────────┤
│  RunState   （单轮层 / 每循环清零）                          │
│  player, timeLeft, alive, escaped, carryingPayload...       │
│  → beginNewLoop() 调用 run.reset(spawn, loopSeconds) 全部推倒 │
│  → 角色固有属性（moveSpeed / playerSize）不算进度，保留      │
├────────────────────────────────────────────────────────────┤
│  LevelData  （静态层 / 只读）                                │
│  name, loopSeconds, rooms, walls, traps, items, doors...    │
│  → 由 LevelLoader 从 JSON 解析，运行时只读                   │
└────────────────────────────────────────────────────────────┘
```

**为什么这么分？** 时间循环游戏 90% 的 bug 都出在「该清的没清 / 不该清的被清了」。
把三层物理隔离 + 用单测钉死重置语义，这类 bug 从源头就被拦住。

代码位置：
- `src/game/GameState.h/.cpp` —— 三层聚合 + `beginNewLoop()`
- `src/game/GameState.h` 里的 `RunState::reset()`、`MetaState::remember*()` 是重点

---

## 3. 事件驱动：系统互不相识

`src/core/EventBus.h` 是类型安全的模板总线（按静态类型路由，不是字符串）：

```
碰撞系统  checkTraps()      → publish(TrapHitEvent)
循环系统  onTrapHit()       → 判定死亡（订阅 TrapHitEvent）
记忆系统  onPlayerDied()    → Meta.deaths++ / 记住陷阱位置（订阅 PlayerDiedEvent）
成就系统  （未来）           → 再 subscribe 一次即可，零改动现有代码
```

事件类型定义在 `src/core/Events.h`：
`PlayerDiedEvent / LoopTimeoutEvent / PasswordCollectedEvent / TrapHitEvent / DoorOpenedEvent / LevelEscapedEvent / LoopStartedEvent`。

**分层纪律**：`Game`（逻辑层）和 `echo_core` 库**不 include SDL / OpenGL**。
渲染层（`main.cpp`）每帧来读 `game.state()`，要画什么画什么。
好处是核心逻辑能直接跑单元测试，不需要开窗口。

---

## 4. 数据驱动：关卡即 JSON

`assets/levels/level01.json` 是正式演示关「回声中继站 / Echo Relay」：

| 字段 | 含义 |
|------|------|
| `loopSeconds: 28` | 每轮 28 秒倒计时，归零即死 |
| `spawn: [-1500, 0]` | 出生点（最左侧气闸 AIRLOCK） |
| `exit: [1080, 0]` | 出口（最右侧 ESCAPE BAY 的逃生舱） |
| `rooms: 3` | AIRLOCK / CORRIDOR / ESCAPE BAY，只作地面着色与命名 |
| `traps: 8` | 8 个隐藏陷阱组成雷区，撞到即死；**第一次撞到后位置被永久记住** |
| `items: 3` | 三个数据核心：`AX-7`、`Q9-T`（真密码）、`ZZ-00`（**诱饵密码，开不了任何门**） |
| `doors: 2` | `GATE A requires AX-7`、`GATE B requires Q9-T`；记得密码就开，否则当墙 |

解析：`src/game/LevelLoader.cpp` 的 `parseLevelJson()`（纯函数，便于单测）。

> 关卡俯视布局预览（按真实坐标生成）：`E:/wb_data/游戏demo/EchoProtocol_关卡布局.html`

---

## 5. 怎么跑

1. VS2022 **「打开本地文件夹」** 选 `EchoProtocol/` 目录
2. 配置选 `x64-Debug`
3. Ctrl+F5 运行（会弹出游戏窗口 + 一个控制台黑窗，黑窗是调试输出，**保留**）

### 按键
| 键 | 作用 |
|----|------|
| WASD / 方向键 | 移动玩家 |
| R | 游玩中：故意死一次（演示完整 Reset，不丢记忆）；**通关后：保留记忆重跑** |
| F5 | **清空存档**（回到什么都不记得的状态，谨慎） |
| ESC | 游玩中：暂停菜单；通关定格：退出 |

### 约定演示流程（Echo Relay）
```
循环 1  从左侧「气闸」向右探索：先摸到 AX-7 核心（门 A 需要的密码），
         穿过 GATE A；进 CORRIDOR 撞上雷区里看不见的陷阱 → 死。
         陷阱位置和已摸到的 AX-7 被永久记住。
循环 2  舱门A 因记忆自动解锁；绕开已知陷阱，去摸 Q9-T（门 B 需要的真密码）。
         但可能仍撞上一个从未死过的陷阱 → 再死一次。ZZ-00 诱饵顺手摸了也没用。
循环 3  所有陷阱都看得见（暗红块）；直线穿过舱门A → 摸 Q9-T → 舱门B 变绿 →
         穿过雷区冲到右侧 ESCAPE BAY 的出口 → 通关。
通关后重开程序：profile.json 里还记得 AX-7 / Q9-T 和全部陷阱 —— Meta 真的落盘了。
```

屏幕上能直接看到的「信息积累」：
- 顶部青色小方块 = 已知密码；暗红小方块 = 已知陷阱
- 顶部长条 = 本轮倒计时（绿→黄→红）
- 被记住的陷阱 = 半透明暗红矩形（从未踩过的陷阱完全不可见）
- **世界空间标签**：每个房间名、每道门上方的 `NEED <密码>`、每个核心下方的密码字面 ——
  让「哪道门要哪个密码、哪个核心是诱饵」在画面里一眼可读，靠记忆规划路线才有依据

---

## 6. 单元测试怎么跑

M2 把核心逻辑抽成 `echo_core` 静态库，测试直接链接它，不需要窗口：

```bash
# 在 out/build/<配置> 目录下
./EchoProtocolTests.exe
# 期望：All tests passed (217 assertions in 23 test cases)
```

> 想看单测覆盖什么，直接读 `tests/test_gamestate.cpp`，每个 TEST_CASE 顶部都有中文注释说明在防什么 bug。
> 其中 `level01.json loads and contains a decoy password` 会真正加载游戏用的关卡文件，
> 断言「存在携带密码但没有任何门需要它的诱饵核心」这一设计不变量。

---

## 7. M2.1 补丁：通关结算 + 重跑

M2 最初「逃出去就定格、只打命令行日志」，窗口里没有反馈、也没法继续，是个明显的体验缺口。
M2.1 补齐了这一段，顺带加了项目第一套 UI 基础设施。

**① 内置 5×7 点阵字体（零依赖文字渲染）**
- `src/BitmapFont.h`：由 `_gen_font.py` 从 ASCII art 生成的 50 个字形（A-Z / 0-9 / 若干符号），
  每行压成 1 个 `uint8_t`（bit4=最左像素）
- `src/TextRenderer.{h,cpp}`：把字符串拆成「亮像素」，每个亮像素画一个 `RectRenderer` 小方块；
  支持缩放、颜色、换行，宽度/高度可查询（用于居中）
- 为什么不引 SDL_ttf：多一个 vcpkg 依赖 + 要附带 `.ttf` 资源；像素风游戏用点阵字体更搭，
  而且这套是 M3 做 UI 的直接基础

**② 通关结算面板**（`main.cpp` 内，屏幕像素空间）
```
        ┌─────────────────────────────────┐
        │            已逃脱               │  ← 青色标题（中文，GDI 渲染）
        │  时间   : 24.0 秒               │
        │  最佳   : 24.0 秒               │  ← 统计（左对齐，22px 中文）
        │  死亡   : 3                     │
        │  循环   : 4                     │
        │  记忆   : 1 密码 / 2 陷阱       │
        │   按 R 再跑一次                 │  ← 闪烁提示
        │   F5 清空存档    ESC 退出       │
        └─────────────────────────────────┘
```

**③ 重跑语义（关键设计）**：`Game::replayAfterEscape()`
- 只在「已通关定格」时才生效，否则什么都不做
- 清掉 `m_finished`、重置 `Run`（回出生点 / 时钟重置 / `escaped=false`），**`Meta` 一个字节不动**
- 和 `wipeProfile()`（F5）的区别：**wipe 清记忆，replay 留记忆**
- 单测 `replayAfterEscape` 覆盖：通关前调用无副作用、通关后调用保留 `knownPasswords`/`escapes`/`bestTime`、`loops` 递增

**④ 常驻状态行**：屏幕底部随时显示中文 `循环 / 死亡 / 密码 / 陷阱 / 最佳`，
让「信息永久积累」这件事在玩的过程中一直看得见（不用去翻控制台）。

> 结算界面预览（按真实布局生成）：`E:/wb_data/游戏demo/EchoProtocol_结算界面预览.html`

---

## 7.5 关卡丰富化（Echo Relay / M2.2）

M2.1 只补了「通关后流程」，关卡本身还是教学关的 2 陷阱 1 门。这一阶段把关卡做成了
**真正需要靠记忆抄近路的关卡**，而且**纯数据驱动 + 极小渲染改动**，没动引擎架构。

**关卡设计要点（全部在 `assets/levels/level01.json` 里）**
- **两道密码门**：`GATE A(AX-7)` 与 `GATE B(Q9-T)` 串行拦在通道上，
  必须先去摸对应核心、再回来过门 → 强制「探索 → 记忆 → 优化路线」的循环节奏
- **8 个陷阱组成的雷区**：6 个在通道（含正落在 y=0 直线上的，逼你绕）、2 个在逃生舱前，
  第一次撞到才可见 → 分多轮逐步把整张雷区点亮
- **3 个数据核心，其中 1 个是诱饵**：`ZZ-00` 携带的密码没有任何门需要，
  是「别什么都去摸」的教训；门上方的 `NEED <密码>` 标签让诱饵一眼可辨
- **更长的横向关卡**（~2800 宽）：相机自动横向跟随，需要分多轮才能把整张图摸透

**渲染层只加了世界空间文字标签**（`main.cpp` 第 6.5 步）
- 房间名、门需求密码（`NEED AX-7` / `NEED Q9-T`）、核心携带密码（`AX-7`/`Q9-T`/`ZZ-00`）
- 复用 M2.1 的 `TextRenderer`，在「世界 viewProjection」下绘制，随相机平移
- 门标签颜色随锁定/解锁状态变黄/变绿，给出即时反馈

> 关卡俯视布局预览：`E:/wb_data/游戏demo/EchoProtocol_关卡布局.html`
> （红格=陷阱、紫块=核心、红门=需密码闸门、青块=出口、绿点=出生点）

---

## 8. 构建验证（沙箱内已跑通）

本机沙箱用 VS2022 自带的 cmake + Ninja 配 MSVC 19.40 手动验证：
- 主程序 `EchoProtocol.exe`：全部编译目标全过（含 M2.2 世界空间标签、M3 分块 HUD 与暂停菜单），链接成功
- 单测 `EchoProtocolTests.exe`：339 断言 / 34 用例全过（含真实关卡「假密码诱饵」用例 + `restartKeepMeta` 用例 + 第二关结构用例 + 切关记忆隔离用例 + 7 个敌兵 FSM/记忆用例 + 8 个视线遮挡/A* 寻路用例 + 3 个程序化音效合成器用例）

M1 的三个坑（UTF-8、`SDL_MAIN_HANDLED`、`LANGUAGES C CXX`）M2 继续适用，已固化进 `CMakeLists.txt`。

**新增构建注意**：`echo_core` 库 + `EchoProtocolTests` 目标依赖 Catch2，
`CMakeLists.txt` 末尾用 `include(Catch)` + `catch_discover_tests` 把测试用例挂进 CTest。
本地用 VS「打开文件夹」时 CMake 会自动处理，无需手动操作。

---

## 9. M3 垂直切片：专业 HUD + 暂停菜单（已完成）

M3 的第一步先把「像个游戏」的补齐——玩家不用翻控制台就能看懂全局，ESC 随时能喘口气。

**① 分块 HUD（中文，由 CjkText 渲染）**
- 左上 `循环 / 死亡` 卡、右上 `密码 / 陷阱` 卡（青=已知密码、暗红=已知陷阱，行前带色块图标）、
  右下 `最佳`、顶部倒计时条、底部操作提示——比 M2 的「一行状态行」更清晰、更专业。
  实现见 `src/main.cpp` 的 HUD 块（`screenVP` 屏幕像素空间）。

**② ESC 暂停菜单**
- 游玩中按 ESC 弹出中文面板，标题「已暂停」，四项 `继续 / 重新开始 / 清空存档 / 退出游戏`；
  `W/S`（或 ↑/↓）导航、`ENTER`/`SPACE` 确认、`ESC` 返回（取消暂停）。
- 暂停时**世界完全冻结**：`main.cpp` 主循环里 `if (!paused) game.update(...)`，
  倒计时与移动全停，不会在菜单背后偷偷计时扣血。
- `RESTART` = `Game::restartKeepMeta()`（M3 新增）：重置当前轮、**保留全部 Meta 记忆**；
  与 `F5`（`wipeProfile()` 清空存档）正好相反，`replayAfterEscape()`（R，仅通关后）则保留记忆重跑。
- 通关定格时按 ESC 仍是直接退出程序（✕ 按钮无论何时都退出）。
- **中文渲染实现**：新增 `CjkText`（src/CjkText.{h,cpp}）——用 Windows GDI 把 UTF-8 文字光栅化成
  「白字 + alpha 遮罩」纹理，绘制时按 `uColor` 染色（选中项变绿）。**不引 SDL_ttf、不打包字体**，
  直接用系统微软雅黑；纹理按 `(utf8, fontPx)` 缓存，不会每帧重建。HUD 与世界空间标签同样走 `CjkText`，
  只有密码码等数据仍用内置 5×7 点阵字体。

**③ 单测补强**：`restartKeepMeta` 行为被钉死——Run 重置（玩家回出生点、时钟归零、alive）、
Meta 保留（密码/陷阱/deaths/escapes 不丢，loops 仅递增），并对照 `wipeProfile` 验证「清记忆」语义。
共 **138 断言 / 14 用例全过**（沙箱内 VS2022 cmake + Ninja + MSVC 19.40 验证）。（注：后续又做了「真 ECS 重构」「第二关 + 选关菜单」「着色器资源化」「敌兵 AI(M4)」「敌兵增强·视线遮挡+A\* 寻路(M5)」「音效(M6)」六次强化，单测现升至 **339 断言 / 34 用例**，见第 10–15 节。）

> 暂停菜单预览（按真实布局生成）：`E:/wb_data/游戏demo/EchoProtocol_暂停菜单预览.html`

**M3 + M4 强化项**（全部完成）：
- ✅ 真正的轻量 ECS（见第 10 节）
- ✅ 第二个差异化关卡「信号深渊」+ 关卡选择菜单（见第 11 节）
- ✅ 把内嵌着色器抽成 `assets/shaders/*.vert|.frag`（见第 12 节）
- ✅ 敌兵 AI：巡逻机器人 FSM + 被杀记忆巡逻路线（见第 13 节）
- ✅ M5 敌兵增强：敌人视线遮挡(Liang–Barsky) + A\* 绕墙寻路(8 邻接禁穿角) + 条件门与 Meta 咬合（见第 14 节）
- ✅ M6 音效：SDL2 原生音频 API 程序化合成（零素材零新依赖）+ 事件→音效接线 + 无设备静默降级（见第 15 节）

## 10. M3 真 ECS 重构（已完成）

把世界物件从「显式数组 + 硬编码 if」升级为真正的 Entity/Component/System：

- **`src/ecs/World.h`**：`Entity`(uint32) + `Component`(纯数据) + `EcsWorld`（按组件类型分桶的稀疏存储，`query<...>()` 取桶交集）。
- **`src/game/SceneBuilder.cpp`**：把 `LevelData`(prefab) 一次性展开成 ECS 实体世界，玩家登记为 `world.player()`。
- **`src/game/systems/`**：五个「每帧」系统 `Movement / DoorBlock / Pickup / Exit / Trap`，各只关心自己那类组件、互不认识；`LoopSystem` 收口死亡 / 重置 / 记忆 / 落盘。
- **`Game` 变薄编排器**：`main.cpp` 渲染改为 `gs.ecs.query<Transform, X>()`。
- 三层状态模型与事件总线一道没动（GDD 灵魂）。行为靠单测钉死：ECS 重构后 138 断言 / 14 用例全过；再加第二关与切关后升至 **158 断言 / 16 用例**。

## 11. M3 第二关 + 选关菜单（已完成）

在「内容量」与「导航」上补齐：

- **第二关「信号深渊 / Signal Abyss」**（`assets/levels/level02.json`）：4 房间、2 道密码门（`舱门 A` 需 `MK-3` / `舱门 B` 需 `7G-P`）、3 个数据核心（含诱饵 `ZZ-00`，开不了任何门）、10 个陷阱。机制与第一关一致：踩陷阱→死亡→记位置，循环里把雷区点亮、记住密码开门通关。
- **中文选关菜单**：启动即进「选择关卡」，列出所有关卡（中文名 + 一句话简介 + 每关进度：未开始 / 已探索 N 轮 / 已通关·最佳 X 秒），`W/S`（或 ↑/↓）选、`ENTER`/`SPACE` 开始、`ESC` 退出。
- **每关独立存档**：第一关 `profile_level01.json`、第二关 `profile_level02.json`，**记忆互不串门**——切关不丢本关进度，也不污染另一关。`Game` 新增 `loadLevel(path, profile)`，切关时读该关独立存档还原 Meta、重建 ECS、重置循环状态并开新一轮。
- **通关后按 `ESC` 回选关菜单**（而非直接退出），方便换关重玩；暂停菜单 `QUIT GAME` 仍是退出程序。
- **修复**：通关定格时 `R`「再跑一次」此前键盘上没生效（只有程序内 `replayAfterEscape()` 接口能用），现补上 `R`/`F5` 在通关态也响应。
- **单测新增 2 用例**：`level02.json` 结构 + 诱饵不变量；`loadLevel` 切关后记忆隔离（切到 B 关记忆清零、切回 A 关从各自存档还原）。共 **158 断言 / 16 用例全过**。

> 选关菜单预览：`E:/wb_data/游戏demo/EchoProtocol_选关菜单预览.html`

## 12. M3 着色器抽资源文件（已完成）

把硬编码在 `main.cpp` / `CjkText.cpp` 里的 GLSL 字符串，抽到数据资源目录，运行时读盘编译：

- **资源位置** `assets/shaders/`：`basic.vert`+`basic.frag`（矩形/纯色，世界与 HUD 的 `RectRenderer` 用）、`text.vert`+`text.frag`（中文纹理，带 `sampler2D`）。
- **加载方式**：`Shader::fromFiles(vert, frag, fallbackV, fallbackF)` 静态工厂；`Shader` 现支持移动语义（`Shader(Shader&&)` + `operator=(Shader&&)`），GPU 对象唯一所有权。
- **兜底策略**：文件缺失时回退到 `Shader.cpp` 内的 `kDefault*Src` 兜底源码（与资源文件内容一致），引擎绝不静默黑屏；`main.cpp` 额外在 `handle()==0` 时致命错误退出。
- **随构建分发**：`CMakeLists.txt` 的 `POST_BUILD` 已 `copy_directory` 整个 `assets/`，新增 `shaders/` 子目录自动覆盖，无需改 CMake。
- **契约不变**：`Shader` 仍只做"编译链接 + 设 uniform"，行为零变化；单测（链接 `echo_core`，不含 GL）仍为 **158 断言 / 16 用例全过**。

> 设计收益：着色器脱离 C++ 源码，美术/TA 调参无需重编，资源与代码分离，更符合作品集叙事。

## 13. M4 敌兵 AI（FSM 巡逻机器人）（已完成）

M4 给时间循环世界加上**主动威胁**——巡逻机器人，让世界从「只有静态陷阱」升级为「有会追杀你的东西」，并接进既有的「信息永久积累」叙事。

- **真 ECS 组件** `EnemyComponent`（`src/ecs/World.h`）：配置字段（巡逻路点 `patrol` / `speed` / `vision` / `alertSpeed` / `attackRange` / `size`）+ 运行时字段（`state` / `patrolIndex` / `patrolDir` / `searchTimer` / `lastKnownPlayer`）。`EcsWorld` 新增 `m_enemies` 桶 + `store<>` 两对特化 + `destroy` 擦除。
- **四态 FSM** `enemySystem`（`src/game/systems/Systems.cpp`）：`Patrol → Alert → Search → Attack`。视野在 M5 升级为**带射线遮挡**（见第 14 节）；**进入攻击距离即死**——顶部 `attackRange` 守卫先触发 `EnemyHitEvent`，保证「从 Patrol 一帧内贴脸」也立刻死；其他转移与移动在守卫之后执行。
- **致死走事件通道**：`EnemyHitEvent → LoopSystem::onEnemyHit` 统一收口死亡/重置/落盘（与陷阱同链路，`trapId=-1` 只写敌人记忆，不写陷阱记忆），绝不当场 reset。
- **信息积累闭环（呼应核心卖点）**：被某敌人杀过 → 永久记住（`MetaState.knownEnemyIds`，与 `knownTrapIds` 对称）→ 之后每轮屏幕画出它的**巡逻路线淡红警示折线 +「巡逻兵 #id」标签**，与「踩过的陷阱才可见」完全对称。`SaveSystem` 对 `knownEnemyIds` 用 `j.contains` 守卫读取，旧存档零影响。
- **数据驱动**：关卡 JSON 新增 `enemies[]`（第一关 1 个、第二关 2 个横向巡逻兵，路线留绕过空间保证可通关）；`LevelLoader` 解析（缺 `id` 自动编号，与 trap 一致）+ `SceneBuilder` 展开成 `TransformComponent` + `EnemyComponent`。
- **重置归位**：`resetEnemies` 在 `LoopStartedEvent`（含切关 `loadLevel` 后的 `beginLoop`）触发，把敌人复位到 `patrol[0]` 且 `Patrol`——补齐「ECS 世界不在每轮重建」的坑。
- **渲染**：`main.cpp` 配色区新增 `kEnemyColor`/`kEnemyAlert`/`kEnemySearch`/`kKnownRoute`；敌人本体按 FSM 态变色（巡逻橙红 / 警觉攻击亮红 / 搜索橙）；记忆路线用 `drawSeg` 画淡红折线 + 中文标签；HUD 右上卡新增「敌兵 N」计数。
- **单测新增 7 用例** `tests/test_enemy.cpp`：解析 / 巡逻移动 / 视野发现 / 接触致死事件 / 重置归位 / 被杀记忆+落盘 / 真实关卡含敌兵。
- **契约不变**：旧 16 用例零回归；新增 Meta 字段用 `j.contains` 守卫，旧存档/旧测试喂的 `{"loops":4}` 仍安全默认。

> 三态预览：`E:/wb_data/游戏demo/EchoProtocol_敌兵AI预览.html`（巡逻 / 追击 / 记忆路线）

## 14. M5 敌兵增强（视线遮挡 + A* 寻路）（已完成）

M5 把 M4 的「纯距离视野 + 直线追击」补齐为「隔墙看不见 + 绕墙追」，让敌兵在复杂关卡具备真实威胁，并和既有的三层状态 / Meta 架构咬合。

- **视线遮挡** `hasLineOfSight`（`src/game/systems/Systems.cpp`）：Liang–Barsky 把敌兵→玩家线段与所有障碍 AABB 求交；被墙/未开的门挡住即看不到，即使距离在视野半径内也保持巡逻。M4 的「纯距离发现」改为「需 LOS 清晰才 Alert」。
- **A\* 寻路** `NavGrid` + `findPath`（同文件）：每帧在关卡 `bounds` 上铺网格（格子按 `agentRadius` 外扩后和障碍求交标记实心），敌兵在 Alert / Search 时沿 **8 邻接（禁穿角）** A\* 路径逼近玩家；路径重算做了节流（目标所在格变化或每 0.3s 一次）；移动后若撞进障碍立即回退——安全网防卡墙。`EnemyComponent` 新增运行时导航字段（`path` / `pathIndex` / `pathTimer` / `lastGoalCellX/Y`），由 `resetEnemies` 一并归零。
- **障碍 = 墙 + 未记住密码的门**：关着的门既挡视线也挡路；一旦在 **Meta 层记住其密码**，门「打开」，视线与路径同时打通——与「信息永久积累」叙事咬合（level01 玩家在门另一侧时敌兵看不见也过不来，开门后威胁才落地）。
- **渲染**：敌兵追击/搜索时画出淡黄 A\* 导航路径折线（`kPathHint`），直观展示「绕墙」；HUD「敌兵 N」记忆计数不变。
- **单测新增 8 用例** `tests/test_nav.cpp`：线段求交 / 墙挡视线 / A\* 绕墙（不穿墙且比直线长）/ 仅 LOS 清晰才警觉 / 绕墙逼近且全程不进障碍 / 条件门（关→挡、记住密码→通）/ 真实关卡门全开后可达 / `resetEnemies` 清 path。**共 339 断言 / 34 用例全过**（旧 23 用例一字未改，`enemySystem` 仅多一个 `meta` 形参，夹具无墙无门 → 行为完全等价；另 +3 音效合成器用例）。
- **契约不变**：`NavGrid` / LOS 为新代码，不影响既有存档与单测。

> 三态预览：`E:/wb_data/游戏demo/EchoProtocol_M5_敌兵增强预览.html`（视线遮挡 / A\* 绕墙 / 条件门）

## 15. M6 音效（程序化合成，零素材 / 零新依赖）（已完成）

GDD Milestone 5「打磨」项之一：给游戏接上音效，但**不引 SDL_mixer、不打包任何 .wav**——
直接用 SDL2 自带音频 API 在运行时**程序化合成**全部音效，并自带一个极简「多声部加法混音器」。
契合项目「不打包字体 / 不引额外依赖」的极简哲学，也是作品集里很好讲的一个「我自己写了个迷你音频引擎」亮点。

- **SDL 无关的抽象层（保持纪律）**：`src/audio/AudioSink.h` 定义纯虚接口 `AudioSink` + `Sfx` 枚举，**放在 `echo_core`**（逻辑层不链 SDL）；
  具体 `AudioSystem`（`src/audio/AudioSystem.{h,cpp}`，SDL）**只编进主程序**。`Game` 持有 `AudioSink*`（默认 `nullptr`=静默），事件处理器里 `if (audio) audio->play(...)`。
  因此：① 逻辑层误 include SDL 会立刻编不过；② 单测链接 `echo_core` 不需要音频设备。
- **合成器是纯函数**（`src/audio/AudioRecipes.h`，header-only、无 SDL）：`renderSfx(Sfx, sr)` 用正弦 + 白噪声 + 包络合成单声道 `[-1,1]` 采样缓冲；每个 `Sfx` 一段「音色配方」（如 Escape = C-E-G-C 上行琶音）。
- **M6 新增事件 `EnemyAlertEvent`**：敌兵从 Patrol/Search 转入 Alert 的**那一刻**发布一次（不每帧刷），作为「发现玩家」音效触发点。
- **事件 → 音效 接线**（`Game::init` 订阅，仅当 `m_audio` 非空）：`EnemyAlertEvent`→Alert（警觉双音）/ `PlayerDiedEvent`→Death（噪声撞击+下扫）/ `PasswordCollectedEvent`→Password（读密码叮）/ `DoorOpenedEvent`→DoorOpen（低频咚）/ `LoopStartedEvent`→LoopReset（轮回上扫，低增益）/ `LevelEscapedEvent`→Escape（胜利琶音）。菜单导航/确认在 `main.cpp` 直接播 `MenuMove`/`MenuConfirm`，ESC 开关暂停菜单也有提示音。
- **稳健降级**：`AudioSystem::init()` 调 `SDL_InitSubSystem(AUDIO)` + `SDL_OpenAudioDevice`，任一失败则 `m_ok=false`、`play` 全 no-op——**沙箱 / 无声 CI 不会崩，游戏逻辑零影响**。
- **混音线程安全**：SDL 回调跑在独立线程，`m_voices` 用 `std::mutex` 保护；`play()` 缓存合成结果并安全入队，主音量 0.35 留余量防削波。
- **单测新增 3 用例** `tests/test_audio.cpp`：每个 `Sfx` 产出非空 / 时长合理 / 样本有界（**不逐样本 REQUIRE**，避免断言爆炸）；纯正弦 `Sfx` 合成可复现；不同 `Sfx` 时长不同。**共 339 断言 / 34 用例全过**（旧 31 用例一字未改）。
- **契约不变**：`AudioSink`/`AudioSystem` 为新代码，且默认静默；既有 31 用例与存档格式零影响。

> M0/M1 手册：`EchoProtocol_M0_搭建手册.md`、`EchoProtocol_M1_渲染手册.md`
> 设计总纲：`E:/wb_data/游戏demo/EchoProtocol_GDD_v2.md`
