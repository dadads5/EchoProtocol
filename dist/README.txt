Echo Protocol — 可直接运行版（Release）
========================================

本文件夹是「时间循环 + 信息积累」策略游戏《Echo Protocol》的免安装便携包，
双击 EchoProtocol.exe 即可游玩，无需安装 Visual Studio 或任何运行库。

【怎么玩】
1. 直接双击 EchoProtocol.exe（建议在本文件夹内运行，不要单独把 exe 移走，
   因为它需要同目录下的 SDL2.dll、运行库和 assets 关卡文件）。
2. 进入后选择关卡，按提示操作。
   - WASD / 方向键：移动
   - 死亡或超时：本轮轮回重来（已记住的密码永久保留，持有钥匙会掉落需重捡）
   - 通关后：按 回车 / M 返回选关，F5 清空存档

【文件夹里都有什么】
- EchoProtocol.exe        游戏主程序（Release 版）
- SDL2.dll                SDL2 运行库（发布版，游戏唯一需要随带的第三方库）
- vcruntime140.dll / VCRUNTIME140_1.dll / msvcp140.dll
                          MSVC C/C++ 运行时（发布版）
- ucrtbase.dll 及 api-ms-win-crt-*.dll
                          Windows 通用 C 运行时（UCRT）及其 API 集合转发器
- assets/                 关卡数据（levels/*.json）与着色器（shaders/）

所有这些 DLL 已随包附带，因此即使目标电脑没装「Visual C++ 可再发行组件」
也能直接运行。如果你想瘦包、改由系统已装的 VC++ Redistributable 提供，
可以删掉 vcruntime140*.dll、msvcp140.dll、ucrtbase.dll 和 api-ms-win-crt-*.dll
（SDL2.dll 必须保留）。

【操作 / 设置说明】
- 全屏/窗口：游戏内按 F 或根据提示切换（以游戏内实际为准）。
- 存档文件会写在运行目录或用户目录，清空存档可按 F5。

【给开发者的说明】
- 源码根：D:/daimajihe/cpp-demo/EchoProtocol
- 当前 Release 构建目录：out/build/release
- 依赖通过 vcpkg（x64-windows）管理：SDL2 / glm / nlohmann-json / Catch2
- 之前的两个 Debug 构建（out/build/verify、out/build/x64-debug）仅供开发与
  单元测试使用，不能脱离 Visual Studio 调试运行库独立运行。

祝你玩得开心！
