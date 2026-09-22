# Echo Protocol — M1 渲染手册（相机 + 矩形绘制）

> 配套代码已写入 `src/`，本手册解释「做了什么 / 为什么 / 怎么验证」。
> 设计文档见 `E:/wb_data/游戏demo/EchoProtocol_GDD_v2.md`。

## 1. M1 目标

在 M0（能建窗口 + 清屏）的基础上，**打通「世界坐标 → 屏幕」的渲染管线**：

- 引入正交相机 `Camera2D`（视图变换）
- 引入着色器 `Shader`（GLSL 编译 / 链接）
- 引入矩形渲染器 `RectRenderer`（顶点缓冲 + uniform 控制位置/大小/颜色）
- 用键盘移动相机，验证「相机移动时世界里的矩形相对屏幕滑动」——证明 view 变换正确

M1 仍属「硬编码原型」阶段，**尚未引入 ECS / 时间循环**，目的是先把渲染地基打牢，让 M2 的循环逻辑有画面可呈现。

## 2. 新增 / 修改的文件

| 文件 | 作用 |
|---|---|
| `src/Shader.h` / `src/Shader.cpp` | 着色器封装：从字符串源码编译链接 program，提供 `setMat4` / `setVec2` / `setVec4` |
| `src/Camera2D.h` / `src/Camera2D.cpp` | 2D 正交相机，输出 `View × Projection` 矩阵 |
| `src/RectRenderer.h` / `src/RectRenderer.cpp` | 单位正方形 + 三个 uniform 绘制轴对齐矩形 |
| `src/main.cpp` | 重写：初始化三件套，WASD/方向键移动相机，绘制玩家 + 墙块 |
| `CMakeLists.txt` | `add_executable` 加入 3 个新 `.cpp`；`target_include_directories` 加入 `src` |

## 3. 核心概念

### 3.1 顶点变换链路

```
aPos(单位正方形 -0.5..0.5)
  → world = aPos * uSize + uCenter        // 摆到世界里的指定位置/大小
  → gl_Position = uVP * vec4(world, 0, 1) // 世界 → 裁剪空间
```

- `uSize` / `uCenter` 把"单位正方形"变成世界坐标下的一个矩形
- `uVP`（View×Projection）由相机算出，是 M1 要验证的关键

### 3.2 正交相机（Camera2D）

- **约定**：世界坐标 **Y 轴向下**（屏幕上方 = y 负，下方 = y 正），符合 2D 游戏直觉
- 相机位置 = 屏幕中心对应的世界坐标
- `viewProjection()` = `ortho(...) * translate(-position)`
- `zoom`：>1 放大（看得近、可见区域小）

> 为什么 `ortho` 的 bottom/top 写成 `(halfH, -halfH)`？这样把 Y 翻转，使世界 Y 向下映射到屏幕 Y 向下。这是 2D 游戏最常见的坐标约定。

### 3.3 矩形渲染（RectRenderer）

- 一个静态 VAO（单位正方形 + 索引缓冲 EBO），`draw` 时**只改 uniform**，不重建几何
- 绘制顺序决定遮挡（M1 未开深度测试）：先画墙（背景层），再画玩家（前景层）

## 4. 操作验证

1. VS2022 → 文件 → 打开 → 文件夹 → `D:\daimajihe\cpp-demo\EchoProtocol`
2. 顶部选 `x64-Debug` → 等「输出」出现 `CMake 生成完毕` → **Ctrl+F5**
3. 预期：深灰背景窗口，中央一个**绿色方块**（玩家，固定世界原点 `(0,0)`），四周四块**灰色墙**
4. 按住 **W / A / S / D 或方向键** 移动相机：
   - 绿方块会**偏离屏幕中心**（它固定在世界原点，相机动了所以它相对屏幕移动）
   - 灰色墙块也随相机滑动
   - 这证明「世界坐标 → 屏幕」的 view 变换在工作
5. ESC / ✕ 退出

## 5. 验证清单（全中即 M1 完成）

- [ ] 中央绿色方块 + 四边灰色墙块可见
- [ ] 按 WASD 移动相机，所有矩形**相对屏幕滑动**（而非原地缩放）
- [ ] 控制台打印 `OpenGL 3.3 context ready`
- [ ] 窗口可缩放，缩放后画面**不拉伸**（`glViewport` + `camera.resize` 已同步）
- [ ] ESC 正常退出

## 6. 这一步的注意点（避坑）

- `find_package(glm)` 的 target 是 `glm::glm`（header-only），已在 CMakeLists 链接；`Camera2D` 用到的 `glm/gtc/matrix_transform.hpp` 是 glm 自带子模块，无需额外依赖。
- **绘制矩形必须建 EBO（索引缓冲）**：若只用 VBO + `glDrawElements` 却没绑 EBO，会读到错误索引导致画面异常。本实现在 VAO 内同时绑定了 VBO 与 EBO。
- 顶点属性 `layout(location = 0)` 与 `glVertexAttribPointer(0, ...)` 必须对应，否则顶点数据送不进着色器。
- M0 已解决的坑（中文注释 `/utf-8`、`SDL_MAIN_HANDLED`、`project` 启用 C）M1 沿用，无需再处理。

## 7. 下一步 M2：时间循环核心

M1 把「画什么、怎么投影」打通了，M2 落地项目的灵魂——**时间循环**：

- 三层状态模型（永久 Meta / 单轮 Run / 世界静态 Static）的 `Reset()` 分隔
- `EventBus` 事件系统（系统级订阅）
- 死亡 → 重置 Run 层 → 保留 Meta 层（知识 / 密码）
- 先用 M1 的矩形做一个「踩到陷阱重置、但已解锁的开关保留」的最小演示

M2 同样会写成可照做手册。
