cmake_minimum_required(VERSION 3.25)

# 必须启用 C 语言：GLAD 是 C 源文件（glad.c），只写 CXX 会导致
# "CMake can not determine linker language for target: glad"
project(EchoProtocol LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC：源码含中文注释，强制按 UTF-8 读取，避免 C4819 连带解析错乱
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---------------------------------------------------------------------------
# 第三方库（vcpkg：sdl2 glm nlohmann-json catch2）
# ---------------------------------------------------------------------------
find_package(SDL2 REQUIRED)
find_package(glm REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Catch2 REQUIRED)

# GLAD：官网生成后随仓库提交（不进 vcpkg，构建无网络依赖）
set(GLAD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad)
add_library(glad STATIC ${GLAD_DIR}/src/glad.c)
target_include_directories(glad PUBLIC ${GLAD_DIR}/include)

# ---------------------------------------------------------------------------
# echo_core：游戏核心逻辑，不依赖 SDL / OpenGL
#
# 单独抽成静态库有两个好处：
#   ① 单元测试可以直接链接它，不需要开窗口、不需要 GL 上下文
#   ② 依赖方向被编译器强制约束 —— 谁不小心在逻辑层 include 了 SDL，这里就编不过
# ---------------------------------------------------------------------------
add_library(echo_core STATIC
    src/game/GameState.cpp
    src/game/LevelLoader.cpp
    src/game/SaveSystem.cpp
    src/game/SceneBuilder.cpp
    src/game/systems/Systems.cpp
    src/game/systems/LoopSystem.cpp
    src/game/Game.cpp)
target_include_directories(echo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(echo_core PUBLIC
    glm::glm
    nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------------------
# 可执行程序（渲染 + 输入 + 主循环）
# ---------------------------------------------------------------------------
add_executable(EchoProtocol
    src/main.cpp
    src/Shader.cpp
    src/Camera2D.cpp
    src/RectRenderer.cpp
    src/TextRenderer.cpp
    src/CjkText.cpp
    src/audio/AudioSystem.cpp)
target_include_directories(EchoProtocol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GLAD_DIR}/include)
target_link_libraries(EchoProtocol PRIVATE
    echo_core
    SDL2::SDL2
    glad)

# 关卡等数据资源拷到 exe 旁边，程序用 SDL_GetBasePath() 找它
add_custom_command(TARGET EchoProtocol POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:EchoProtocol>/assets
    COMMENT "Copying assets next to the executable")

# ---------------------------------------------------------------------------
# 单元测试
# ---------------------------------------------------------------------------
add_executable(EchoProtocolTests
    tests/test_gamestate.cpp
    tests/test_enemy.cpp
    tests/test_nav.cpp
    tests/test_audio.cpp)
target_link_libraries(EchoProtocolTests PRIVATE
    echo_core
    Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(EchoProtocolTests)
