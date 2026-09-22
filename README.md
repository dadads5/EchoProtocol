# Echo Protocol

时间循环 + 信息永久积累的策略游戏（作品集项目，C++ / SDL2 / OpenGL 3.3 / ECS）。

> 设计文档见 `E:/wb_data/游戏demo/EchoProtocol_GDD_v2.md`（与本项目分离存放）。

## 当前进度：M6 垂直切片 ✅（真 ECS + 专业 HUD/暂停菜单 + 第二关/选关菜单 + 着色器资源化 + 敌兵 AI + 视线遮挡 + A* 寻路 + 音效）

一个能编译运行、机制完整、且**像个完整游戏**的工程：SDL2 窗口 + OpenGL 3.3 Core 渲染，
落地了项目灵魂——**时间循环 + 信息永久积累**，并补齐了专业 HUD 与暂停菜单。

- **三层状态模型**：Meta（永久/落盘）· Run（单轮清零）· Static（关卡只读），Reset 只清 Run
- **事件驱动**：EventBus 类型安全路由，碰撞/循环/记忆系统互不耦合
- **数据驱动关卡**：关卡是 JSON（`assets/levels/level01.json`），改关不动代码
- **永久存档**：`profile.json` 记住密码与陷阱位置，重开程序仍在
- **专业 HUD + 暂停菜单（M3）**：分块信息卡 + ESC 暂停/重跑/清档/退出，见下方 M3 章节
- **单元/集成测试**：Catch2 覆盖三层重置语义 / 存档往返 / 关卡解析 / 事件路由 / 端到端死亡与通关 / 第二关结构与切关记忆隔离 / 敌兵 FSM 与「被杀记忆巡逻路线」/ 视线遮挡与 A* 绕墙寻路 / 程序化音效合成器，外加真实关卡「假密码诱饵」不变量（339 断言 / 34 用例全过）

### M2.1 补丁：通关结算与重跑 ✅

- **内置 5×7 点阵字体**（`src/BitmapFont.h` 由 `_gen_font.py` 生成 + `src/TextRenderer.{h,cpp}`）：
  零外部依赖的文字渲染，不用引 SDL_ttf / 字体资源，也是 M3 的 UI 基础
- **通关结算面板**：逃出后弹出面板，显示 `TIME / BEST / DEATHS / LOOPS / MEMORY`，不再只是「定格 + 暗幕」
- **重跑流程**：通关后按 `R` **保留全部记忆**重跑（`Game::replayAfterEscape()`）；游玩中 `R` 仍是「故意死一次」
- **常驻状态行**（M2.1）：屏幕底部随时显示 `LOOP / DEATHS / CODE / TRAPS / BEST`，让「记忆在积累」看得见

### M2.2 关卡丰富化（Echo Relay）✅

把关卡做成了**真正需要靠记忆抄近路的关卡**，且**没动引擎架构**（纯 JSON + 极小渲染改动）：

- **两道密码门** `GATE A(AX-7)` / `GATE B(Q9-T)` 串行拦路，强制「探索→记忆→优化路线」
- **8 个陷阱雷区**：第一次撞到才可见，分多轮把整张雷区点亮
- **3 个数据核心，1 个诱饵密码 `ZZ-00`**（开不了任何门）——「别什么都去摸」的教训
- **世界空间标签**：门上方 `NEED <密码>`、核心下方密码字面、房间名，让路线规划有据可依

### M3 垂直切片：专业 HUD + 暂停菜单 ✅

把「像个游戏」的补齐了——玩家不用翻控制台也能看懂全局状态，ESC 随时可以喘口气：

- **分块 HUD（中文，由 CjkText 渲染）**：左上 `循环 / 死亡` 卡、右上 `密码 / 陷阱` 卡（青=已知密码，暗红=已知陷阱，行前带色块图标）、右下 `最佳`、顶部倒计时条、底部操作提示。比 M2 的「一行状态行」更清晰、更专业。
- **ESC 暂停菜单（中文 UI）**：游玩中按 ESC 弹出中文面板，标题「已暂停」，四项 `继续 / 重新开始 / 清空存档 / 退出游戏`，`W/S`（或 ↑/↓）导航、`ENTER`/`SPACE` 确认、`ESC` 返回。
  - 中文由新增的 `CjkText`（Windows GDI 渲染成 GL 纹理）绘制，**不引入 SDL_ttf、不打包字体文件**，直接吃系统微软雅黑；HUD 与世界空间标签同样走 `CjkText`，只有密码码等数据仍用内置 5×7 点阵字体。
  - 暂停时**世界完全冻结**（倒计时、移动全停），不会偷偷计时扣血。
  - `RESTART` = `Game::restartKeepMeta()`（M3 新增）：重置当前轮但**保留全部 Meta 记忆**（与 `F5` 清空存档正好相反）。
  - `QUIT` 退出程序；通关定格时按 ESC 仍是直接退出。
- **单测补强**：`restartKeepMeta` 行为被钉死（Run 重置 + Meta 保留，对照 `wipeProfile` 清空），共 **138 断言 / 14 用例全过**。

> 中文 HUD + 暂停菜单预览：`E:/wb_data/游戏demo/EchoProtocol_暂停菜单预览.html`（含 循环/死亡、密码/陷阱、最佳 卡与「已暂停」菜单）
> 选关菜单预览：`E:/wb_data/游戏demo/EchoProtocol_选关菜单预览.html`（中文选关 + 每关进度）
> 关卡布局预览：`E:/wb_data/游戏demo/EchoProtocol_关卡布局.html`（俯视地图，一眼看穿路线）

### M3 真 ECS 重构 ✅

把「世界物件」从「一堆显式数组 + 硬编码 if」升级为**真 ECS**（Entity / Component / System），
逻辑层（Game）变成只负责「按序跑系统」的薄编排器。三层状态模型（Meta 永久 / Run 单轮 / Static 关卡）与
事件总线完全保留，是 GDD 的灵魂，**一道没动**；行为靠 138 断言 / 14 用例全过钉死。

- **`src/ecs/World.h`**：`Entity`(uint32 id) + `Component`(纯数据：Transform/PlayerTag/Door/Core/Trap/Room/Wall/Exit)
  + `EcsWorld`（按组件类型分桶的稀疏存储 `unordered_map<Entity,Comp>`，`query<...>()` 取桶交集）。
- **`src/game/SceneBuilder.cpp`**：把数据驱动的 `LevelData`（prefab）一次性展开成 ECS 实体世界，
  玩家登记为 `world.player()`；之后只改 Run/Meta，实体（静态世界）不再重建。
- **`src/game/systems/`**：五个「每帧」系统，各只关心自己那类组件、互不认识——
  `MovementSystem`(输入→移动) / `DoorBlockSystem`(锁门当墙推出) / `PickupSystem`(碰核心→发密码事件) /
  `ExitSystem`(碰出口→发通关事件) / `TrapSystem`(碰陷阱→发死亡事件)。
- **`src/game/systems/LoopSystem.cpp`**：时间循环 + 记忆的「收口」系统，订阅 EventBus 上所有事件，
  负责死亡判定 / 单轮重置 / 写 Meta 记忆 / 落盘 / 日志。**死亡重置延迟到帧末**统一执行。
- **渲染改读 ECS**：`src/main.cpp` 的世界绘制从遍历 `world.rooms/doors/...` 改为 `gs.ecs.query<Transform, X>()`。
- **依赖方向被编译器强制**：`echo_core` 静态库不碰 SDL / GL；`main.cpp` 才链接窗口与渲染。

> 设计要点：玩家本身的「单轮状态」(RunState：存活/携带/计时) 属于 GDD 的 Run 层，**不进 ECS**——
> 它跟着循环者走，不是场景里的一个物件；ECS 玩家的 Transform 由 `RunState.player` 同步（见 `GameState::syncPlayerTransform`）。

### M3 第二关 + 选关菜单 ✅

在「像个完整游戏」之上补了**内容量**与**导航**：

- **第二关「信号深渊 / Signal Abyss」**（`assets/levels/level02.json`）：独立 JSON 关卡，更长更密——4 个房间、2 道密码门（`舱门 A` 需 `MK-3` / `舱门 B` 需 `7G-P`）、3 个数据核心（含诱饵 `ZZ-00`，开不了任何门）、10 个陷阱。机制与第一关一致：踩陷阱→死亡→记位置，循环里把雷区点亮、记住密码开门通关。
- **中文选关菜单**：启动即进「选择关卡」，列出所有关卡（中文名 + 一句话简介 + 每关进度：未开始 / 已探索 N 轮 / 已通关·最佳 X 秒），`W/S`（或 ↑/↓）选、`ENTER`/`SPACE` 开始、`ESC` 退出。
- **每关独立存档**：第一关 `profile_level01.json`、第二关 `profile_level02.json`，**记忆互不串门**——切关不丢本关进度，也不污染另一关。实现上 `Game` 新增 `loadLevel(path, profile)`，切关时读该关独立存档还原 Meta、重建 ECS、重置循环状态并开新一轮。
- **通关后按 `ESC` 回选关菜单**（而非直接退出），方便换关重玩；暂停菜单 `QUIT GAME` 仍是退出程序。
- **修复**：通关定格时 `R`「再跑一次」此前在键盘上没生效（只有程序内 `replayAfterEscape()` 接口能用），现补上 `R`/`F5` 在通关态也响应。
- **单测新增 2 用例**：`level02.json` 结构 + 诱饵不变量；`loadLevel` 切关后记忆隔离（切到 B 关记忆清零、切回 A 关从各自存档还原）。共 **158 断言 / 16 用例全过**。

> 选关菜单预览：`E:/wb_data/游戏demo/EchoProtocol_选关菜单预览.html`

### M3 着色器抽资源文件 ✅

M3 最后一项强化：把原先硬编码在 `main.cpp` / `CjkText.cpp` 里的 GLSL 字符串，抽到
数据资源目录，运行时读盘编译。

- **资源位置**：`assets/shaders/`
  - `basic.vert` + `basic.frag` —— 矩形/纯色着色器（世界与 HUD 的 `RectRenderer` 用）
  - `text.vert` + `text.frag` —— 中文纹理着色器（`CjkText` 用，带 `sampler2D`）
- **加载方式**：`Shader::fromFiles(vert, frag, fallbackV, fallbackF)` 静态工厂，读文件 → 构造
  `Shader`；`Shader` 已支持移动语义（GPU 对象唯一所有权）。
- **兜底策略**：文件读取失败时，`Shader` 内部回退到 `kDefault*Src` 兜底源码（与资源文件内容一致），
  保证即便资源未随 exe 拷贝，引擎也不会静默黑屏；`main.cpp` 额外做了 `handle()==0` 的致命错误退出。
- **资源随构建分发**：`CMakeLists.txt` 的 `POST_BUILD` 已 `copy_directory` 整个 `assets/` 到 exe 旁边，
  新增 `shaders/` 子目录自动覆盖，无需改 CMake。
- **契约不变**：`Shader` 仍只做"编译链接 + 设 uniform"，行为零变化；单测（链接 `echo_core`，不含 GL）不受影响，仍为 **158 断言 / 16 用例全过**。

> 设计收益：着色器脱离 C++ 源码，美术/TA 调参无需重编；资源与代码分离，更符合作品集叙事。

### M4 敌兵 AI（FSM 巡逻机器人）✅

M4 给时间循环世界加上**主动威胁**——巡逻机器人，让世界从「只有静态陷阱」升级为「有会追杀你的东西」，并把它接进既有的「信息永久积累」叙事。

- **真 ECS 组件**：`EnemyComponent`（配置：巡逻路点 / 速度 / 视野 / 追击速度 / 攻击距离 + 运行时：FSM 状态 / 路点下标 / 搜索计时 / 最后已知玩家位置）。
- **四态 FSM**（`enemySystem`）：`Patrol → Alert → Search → Attack`。视野在 M5 升级为**带射线遮挡**（敌兵→玩家线段与障碍 AABB 求交，隔墙看不见）；进入攻击距离即死（接触致死，事件收口交给 `LoopSystem`）。
- **致死走事件通道**：`EnemyHitEvent → LoopSystem::onEnemyHit` 统一收口死亡/重置/落盘，与陷阱同链路，绝不当场 reset。
- **信息积累闭环（呼应核心卖点）**：被某敌人杀过 → 永久记住它（`MetaState.knownEnemyIds`）→ 之后每轮屏幕上画出它的**巡逻路线淡红警示折线 + 标签**，与「踩过的陷阱才可见」完全对称。
- **数据驱动**：关卡 JSON 新增 `enemies[]`（第一关 1 个、第二关 2 个横向巡逻兵，路线留绕过空间保证可通关）；`LevelLoader` 解析 + `SceneBuilder` 展开。
- **重置归位**：`resetEnemies` 在 `LoopStartedEvent`（含切关）时把敌人复位到 `patrol[0]` 且 `Patrol`。
- **HUD**：右上信息卡新增「敌兵 N」记忆计数。
- **单测新增 7 用例**（`tests/test_enemy.cpp`）：解析 / 巡逻移动 / 视野发现 / 接触致死事件 / 重置归位 / 被杀记忆+落盘 / 真实关卡含敌兵。共 **217 断言 / 23 用例全过**。
- **契约不变**：`MetaState` 新字段用 `j.contains()` 守卫读取，旧存档与旧测试零影响；ECS 世界不在每轮重建的坑由 `resetEnemies` 显式复位补齐。

> 三态预览：`E:/wb_data/游戏demo/EchoProtocol_敌兵AI预览.html`（巡逻 / 追击 / 记忆路线）

### M5 敌兵增强（视线遮挡 + A* 寻路）✅

M5 把 M4 的「纯距离视野 + 直线追击」补齐为「隔墙看不见 + 绕墙追」，让敌兵在复杂关卡具备真实威胁，并和既有的三层状态 / Meta 架构咬合。

- **视线遮挡（Line-of-Sight）**：`hasLineOfSight` 用 Liang–Barsky 把敌兵→玩家线段与所有障碍 AABB 求交；被墙/门挡住即看不到，即使落在视野半径内也保持巡逻。
- **A\* 寻路（绕墙而非穿墙）**：每帧在关卡 `bounds` 上铺一张 `NavGrid`（格子按 `agentRadius` 外扩后与障碍求交标记实心），敌兵在 Alert / Search 时沿 **8 邻接（禁穿角）** A\* 路径逼近玩家；路径重算做了节流（目标所在格变化或每 0.3s 一次）；移动后若撞进障碍立即回退——安全网防卡墙。
- **障碍 = 墙 + 未记住密码的门**：关着的门既挡视线也挡路；一旦在 **Meta 层记住其密码**，门「打开」，视线与路径同时打通。这是 M5 与「信息永久积累」叙事的展示点（level01 玩家在门另一侧时敌兵看不见也过不来，开门后威胁才落地）。
- **渲染**：敌兵追击/搜索时画出淡黄 A\* 导航路径折线，直观展示「绕墙」；HUD「敌兵 N」记忆计数不变。
- **单测新增 8 用例**（`tests/test_nav.cpp`）：线段求交 / 墙挡视线 / A\* 绕墙（不穿墙且比直线长）/ 仅 LOS 清晰才警觉 / 绕墙逼近且全程不进障碍 / 条件门（关→挡、记住密码→通）/ 真实关卡门全开后可达 / `resetEnemies` 清 path。共 **304 断言 / 31 用例全过**（旧 23 用例一字未改，夹具无墙无门 → 行为完全等价）。
- **契约不变**：`enemySystem` 仅多一个 `meta` 形参；`NavGrid` / LOS 为新代码，不影响既有存档与单测。

### M6 音效（程序化合成，零素材文件 / 零新依赖）✅

GDD Milestone 5 的「打磨」项之一——给游戏接上音效，但**不引 SDL_mixer、不打包任何 .wav**：
直接用 SDL2 自带音频 API（`SDL_OpenAudioDevice` + 回调）在运行时**程序化合成**所有音效，
自带一个极简「多声部加法混音器」。契合项目「不打包字体 / 不引额外依赖」的极简哲学，也是作品集好讲的亮点。

- **SDL 无关的抽象**：`AudioSink`（纯虚接口，含 `Sfx` 枚举）放在 `echo_core`，具体 `AudioSystem`（SDL）只编进主程序——
  逻辑层永远不会因为误 include SDL 而编不过，单测也不需要音频设备（`tests/test_audio.cpp` 只测纯合成器，不碰 SDL）。
- **事件 → 音效 接线**（`Game::init` 里订阅，默认静默）：
  `EnemyAlertEvent`→发现(Alert) / `PlayerDiedEvent`→死亡(Death) / `PasswordCollectedEvent`→读密码(Password) /
  `DoorOpenedEvent`→记忆开门(DoorOpen) / `LoopStartedEvent`→轮回重置(LoopReset) / `LevelEscapedEvent`→通关(Escape)。
  菜单导航 / 确认另在 `main.cpp` 直接播 `MenuMove` / `MenuConfirm`，ESC 开关暂停菜单也有提示音。
- **稳健降级**：设备打不开（如沙箱 / 无声 CI）时 `init()` 返回 false、`play` 全 no-op，**游戏逻辑零影响**。
- **混音线程安全**：SDL 回调跑在独立线程，`m_voices` 用 `std::mutex` 保护，主线程 `play()` 安全入队。
- **单测新增 3 用例**（`tests/test_audio.cpp`）：每个 `Sfx` 产出非空 / 时长合理 / 样本有界（不逐样本断言）；
  同一（纯正弦）`Sfx` 合成可复现；不同 `Sfx` 时长不同。共 **339 断言 / 34 用例全过**（旧 31 用例一字未改）。

> 三态预览：`E:/wb_data/游戏demo/EchoProtocol_M5_敌兵增强预览.html`（视线遮挡 / A\* 绕墙 / 条件门）

## 构建步骤

依赖已就绪（vcpkg 位于 `D:/vcp/vcpkg`，GLAD 已生成入库）。换机器时按下列步骤重建：

1. 安装 vcpkg 并接入 `vcpkg integrate install`
2. 安装依赖：`vcpkg install sdl2 glm nlohmann-json catch2 --triplet x64-windows`
3. 用 VS2022 **“打开本地文件夹”** 选择本目录（含 `CMakeLists.txt`），选 `x64-Debug`，Ctrl+F5 运行。

> CMake 通过 `CMakePresets.json` 中显式指定的
> `CMAKE_TOOLCHAIN_FILE = D:/vcp/vcpkg/scripts/buildsystems/vcpkg.cmake` 找到 vcpkg 库；
> 若 vcpkg 换了位置，改这一处即可。

## 已踩坑（已修复，记录备查）

- **中文注释需 `/utf-8`**：MSVC 默认按系统代码页(936/GBK)读源文件，UTF-8 中文注释会触发 C4819 并把后续代码解析乱掉。
  `CMakeLists.txt` 已对 MSVC 加 `add_compile_options(/utf-8)`。
- **入口用 `SDL_MAIN_HANDLED`**：`src/main.cpp` 顶部 `#define SDL_MAIN_HANDLED` 让 `int main()` 直接作为入口，
  跳过 SDL2 在 Windows 上的 WinMain 桥接（否则链接需额外处理子系统入口 /SUBSYSTEM:WINDOWS）。
  因此 `CMakeLists.txt` 只链接 `SDL2::SDL2`，不链 `SDL2::SDL2main`。
- **`project()` 必须启用 C 语言**：GLAD 是 C 源文件（`glad.c`），若写成
  `project(EchoProtocol LANGUAGES CXX)` 只启用 C++，CMake 会报
  `CMake can not determine linker language for target: glad`（配置阶段直接失败）。
  正确写法：`project(EchoProtocol LANGUAGES C CXX)`。
- GLAD 已用 Python 的 glad 包按 OpenGL 3.3 Core 生成入库（`third_party/glad/`），构建无网络依赖。

## 按键无反应的排查（M1.1 已加入自诊断）

现象：窗口和矩形都正常显示，但按 WASD 相机不动。
**这类问题几乎都不是移动逻辑的错，而是按键根本没送进这个窗口。** 排查顺序：

1. **看控制台有没有 `[key] down: W`**
   - **没有** → 按键没到达程序，属于焦点或输入法问题，看第 2、3 条。
   - **有** → 按键到达了，问题在逻辑侧（回来找我，我继续查）。
2. **输入法在中文模式**：按 `Shift` 或 `Ctrl+空格` 切到英文再按 WASD。
   微软拼音在中文态下会拦截字母键做输入组合，游戏收不到。
3. **焦点在别的窗口**：点一下游戏窗口。启动时控制台会抢焦点，按键全被控制台吃掉
   （表现是控制台里打出了 `wasd` 字样）。
   代码里已加 `SDL_RaiseWindow` + `SDL_SetWindowInputFocus` 主动抢焦点，正常不会再出现。
4. 每次按键都会打印 `[key] down/up: <键名> (scancode=..)`，相机真的在动时每 0.5 秒打印
   一次 `[dbg] cam=(x, y)`。**这套输出是 M1 的调试设施**，M2 做正经 HUD 时会替换掉。

> 另：输入状态同时取自 `SDL_GetKeyboardState` 和自维护的按键集合（事件驱动的 `SDL_KEYDOWN`/`SDL_KEYUP`）。
> 失焦时自维护集合会被清空，避免"松开键了但状态还留着导致相机一直漂"。
> M2 起不再逐键打印 `[key] down`（那套是 M1 诊断设施），但保留了 `[focus]` 焦点日志：
> 窗口拿到焦点会打印 `game window focused`，焦点被切走会清空按键并打印 `focus lost`，方便确认按键确实落在游戏窗口里。

## 验证清单（全中即 M3 完成）

**主程序（Ctrl+F5 实跑）：**
- [ ] 控制台打印 `OpenGL 3.3 context ready`
- [ ] 顶部出现倒计时长条（绿→黄→红），每轮开始满格、随时间流逝
- [ ] 左上 `循环/死亡` 卡、右上 `密码/陷阱` 卡随游戏实时变化，右下 `最佳`，顶部倒计时条
- [ ] 绿色玩家方块可用 WASD/方向键移动，相机横向跟随
- [ ] 走廊里每道门上方显示「需要 AX-7」/「需要 Q9-T」；紫色数据核心下方显示它携带的密码
- [ ] 撞到隐藏陷阱 → 死亡 → 自动回到出生点，那块陷阱之后变暗红色可见
- [ ] 摸到 `AX-7` 核心后，`GATE A` 立即变绿（解锁）；摸到 `Q9-T` 后 `GATE B` 变绿
- [ ] 穿过两道门冲到右侧 ESCAPE BAY 出口 → 通关
- [ ] **通关后弹出结算面板**：中文「已逃脱」标题 + `时间/最佳/死亡/循环/记忆` 统计 + 闪烁「按 R 再跑一次」
- [ ] 结算面板按 `R` → **记忆保留**重跑（面板消失、回到出生点、密码仍都在）
- [ ] 游戏中按 `ESC` 弹出「已暂停」菜单（中文）；`W/S` 高亮切换、`ENTER` 确认、`ESC` 取消
- [ ] 暂停时倒计时与移动冻结；`RESTART` 回到出生点且密码/陷阱记忆保留；`WIPE SAVE` 清空存档
- [ ] 通关后按 `ESC` 回「选择关卡」菜单；暂停菜单 `QUIT GAME` 才是退出程序
- [ ] 启动进入中文「选择关卡」菜单，列出两关与各自进度（未开始 / 已探索 N 轮 / 已通关·最佳 X 秒）
- [ ] `W/S` 选关、`ENTER` 开始；选中已加载关卡直接进，选中另一关先切关再进
- [ ] 第二关「信号深渊」可用相同机制通关（踩陷阱记位置、摸 `MK-3`/`7G-P` 开门）
- [ ] 切到第二关后第一关的记忆不出现；切回第一关记忆从各自存档还原
- [ ] F5 清空**当前关**存档；每关存档独立（`profile_level01.json` / `profile_level02.json`）
- [ ] 着色器从 `assets/shaders/*.vert|*.frag` 读盘加载（控制台无 `Shader compile/link error`、无 `failed to read` 警告即正常）
- [ ] 第一关「回声中继站」走廊里有一台橙红色巡逻兵横向往返；第二关「信号深渊」有 2 台
- [ ] 玩家进入敌人视野 → 敌人变亮红并加速追来；贴脸 → 死亡 → 自动回到出生点
- [ ] 被某敌人杀过一次后，之后每轮该敌人的**巡逻路线淡红折线 +「巡逻兵 #id」标签**可见（右上「敌兵 1」计数+1）
- [ ] 敌人只沿关卡设计的开放走廊巡逻，不会永久堵死唯一通道（可正常通关）

**单元测试：**
- [ ] `EchoProtocolTests.exe` 输出 `All tests passed (339 assertions in 34 test cases)`

> M1 讲解见 `EchoProtocol_M1_渲染手册.md`；M2 讲解见 `EchoProtocol_M2_时间循环手册.md`。
