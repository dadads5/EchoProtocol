// Echo Protocol - M2 时间循环核心（关卡：回声中继站 / Echo Relay）
//
// 这一层是「应用外壳」：窗口、OpenGL 上下文、输入采集、渲染、主循环。
// 游戏逻辑全在 echo::Game / echo::GameState 里，这里读它们的状态来画。
//
// 关卡结构（数据驱动，assets/levels/level01.json）：
//   出生在左侧「气闸」，右侧「逃生舱」是出口。
//   中间「走廊」里有两道密码门：舱门A(AX-7) / 舱门B(Q9-T)，
//   以及一整片陷阱雷区 + 三个数据核心（其中 ZZ-00 是打不开任何门的诱饵）。
//
// 典型节奏（靠"记忆永久积累"抄近路）：
//   LOOP 1  向右探索，撞上看不见的陷阱 -> 死。但陷阱位置 + 已摸到的密码被永久记住。
//   LOOP 2  门已因记忆自动解锁；绕开已知的雷区，却可能撞上还没死过的陷阱。
//   LOOP 3  所有陷阱都看得见，直线穿过两道门冲到右侧出口 -> 通关。
//   通关后重开程序，profile.json 里还记得密码和全部陷阱 —— Meta 层真的落盘了。
//
// 按键：WASD / 方向键 移动 | R 故意死一次 | F5 清空存档 | ESC 暂停
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <glad/glad.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Camera2D.h"
#include "RectRenderer.h"
#include "Shader.h"
#include "TextRenderer.h"
#include "CjkText.h"
#include "audio/AudioSystem.h"
#include "core/EventBus.h"
#include "core/Events.h"
#include "game/Game.h"
#include "game/SaveSystem.h"
#include "ecs/World.h"

// ---------------------------------------------------------------------------
// 顶层界面状态：选关菜单 / 游戏中
// ---------------------------------------------------------------------------
enum { MODE_MENU, MODE_PLAY };

// ---------------------------------------------------------------------------
// 关卡登记表：每关一份独立存档，记忆互不串门
// ---------------------------------------------------------------------------
struct LevelEntry {
    const char* path;     // 相对 exe 的关卡 JSON
    const char* profile;  // 该关独立存档文件名
    const char* name;     // 选关菜单显示名
    const char* desc;     // 一句话介绍
};

static const LevelEntry kLevels[] = {
    { "assets/levels/level01.json", "profile_level01.json",
      "第一关 · 回声中继站", "气闸 → 两道门 → 逃生舱" },
    { "assets/levels/level02.json", "profile_level02.json",
      "第二关 · 信号深渊", "更长更密的雷区，两个核心可开门" },
    { "assets/levels/level03.json", "profile_level03.json",
      "第三关 · 回声回廊", "环廊绕行至右侧长廊，取钥匙开核心舱" },
    { "assets/levels/level04.json", "profile_level04.json",
      "第四关 · 回声之门", "五对传送门 A/F/B/C/D，双钥匙开双门" },
};

static std::vector<std::string> g_menuStatus; // 每关进度文案（进菜单前刷新）

// ---------------------------------------------------------------------------
// 着色器已从代码抽到资源文件：assets/shaders/basic.vert / basic.frag
// 运行时由 Shader::fromFiles 读盘加载（见下方 init 段）。
// ---------------------------------------------------------------------------

namespace {

// 期望在屏幕上看到的世界范围（单位）。缩放由它和窗口尺寸共同决定。
constexpr float kViewWorldWidth  = 1000.0f;
constexpr float kViewWorldHeight = 720.0f;

// ---- 配色 ----
const glm::vec4 kWallColor     (0.40f, 0.43f, 0.50f, 1.00f);
const glm::vec4 kPlayerColor   (0.30f, 0.90f, 0.45f, 1.00f);
const glm::vec4 kDoorLocked    (0.88f, 0.26f, 0.30f, 1.00f);
const glm::vec4 kDoorOpen      (0.24f, 0.78f, 0.46f, 1.00f);
const glm::vec4 kExitColor     (0.25f, 0.85f, 0.90f, 1.00f);
const glm::vec4 kItemColor     (0.86f, 0.36f, 0.95f, 1.00f);
const glm::vec4 kKnownTrap     (0.85f, 0.16f, 0.16f, 0.55f); // 记忆里的陷阱
const glm::vec4 kKeyHeldIcon   (1.00f, 0.82f, 0.30f, 1.00f); // HUD 持有钥匙图标（金）
const glm::vec4 kEnemyColor    (0.95f, 0.32f, 0.18f, 1.00f); // 敌兵（巡逻态）
const glm::vec4 kEnemyAlert    (1.00f, 0.20f, 0.10f, 1.00f); // 追击 / 攻击态（更亮红）
const glm::vec4 kEnemySearch   (1.00f, 0.55f, 0.15f, 1.00f); // 搜索态（橙）
const glm::vec4 kKnownRoute    (0.95f, 0.30f, 0.25f, 0.45f); // 记忆里的敌兵巡逻路线
const glm::vec4 kPathHint      (1.00f, 0.85f, 0.20f, 0.55f); // 敌兵当前 A* 导航路径（淡黄）
const glm::vec4 kHudBack       (0.09f, 0.10f, 0.14f, 0.90f);
const glm::vec4 kHudGood       (0.30f, 0.80f, 0.45f, 1.00f);
const glm::vec4 kHudWarn       (0.92f, 0.76f, 0.25f, 1.00f);
const glm::vec4 kHudBad        (0.92f, 0.30f, 0.30f, 1.00f);

// 把浮点格式化成保留 1 位小数的字符串（HUD 上显示秒数用）
std::string fixed1(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    return std::string(buf);
}

// 把字符串数组拼成 "A B C"，HUD 上显示已知密码用
std::string joinStrings(const std::vector<std::string>& v, const std::string& sep) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += sep;
        s += v[i];
    }
    return s;
}

// 选关菜单（中文）：标题 + 每关一行（高亮项加绿色方块光标）
static void drawLevelMenu(Shader& shader, RectRenderer& rects, CjkText& cjk,
                          const glm::mat4& screenVP, float w, float h,
                          int sel, const std::vector<std::string>& status) {
    const int n = static_cast<int>(sizeof(kLevels) / sizeof(kLevels[0]));

    // 暗底 + 面板（高度随关卡数自适应）+ 上下强调条
    rects.draw(shader, screenVP, glm::vec2(w * 0.5f, h * 0.5f),
               glm::vec2(w, h), glm::vec4(0.04f, 0.05f, 0.07f, 1.0f));
    const float panelW = std::min(600.0f, w - 60.0f);
    const float rowH   = 96.0f;   // 每关一行的高度
    const float headH  = 76.0f;   // 标题区高度
    const float footH  = 56.0f;   // 底部提示区高度
    const float panelH = std::min(headH + static_cast<float>(n) * rowH + footH, h - 60.0f);
    const glm::vec2 pc(w * 0.5f, h * 0.5f);
    rects.draw(shader, screenVP, pc, glm::vec2(panelW, panelH),
               glm::vec4(0.06f, 0.10f, 0.14f, 0.98f));
    rects.draw(shader, screenVP, glm::vec2(pc.x, pc.y - panelH * 0.5f + 5.0f),
               glm::vec2(panelW - 20.0f, 8.0f), kExitColor);
    rects.draw(shader, screenVP, glm::vec2(pc.x, pc.y + panelH * 0.5f - 5.0f),
               glm::vec2(panelW - 20.0f, 8.0f), kHudGood);

    // 标题
    float tw = 0, th = 0;
    cjk.measure("选择关卡", 32, tw, th);
    const float titleY = pc.y - panelH * 0.5f + 26.0f;
    cjk.draw(screenVP, "选择关卡", 32, glm::vec2(pc.x - tw * 0.5f, titleY), kExitColor);

    // 每关一行（选中项带高亮底条 + 左侧光标）
    const float contentTop = titleY + th + 16.0f;
    for (int i = 0; i < n; ++i) {
        const bool   s    = (i == sel);
        const float  rowY = contentTop + i * rowH;
        if (s) {
            rects.draw(shader, screenVP,
                       glm::vec2(pc.x, rowY + 36.0f),
                       glm::vec2(panelW - 32.0f, rowH - 14.0f),
                       glm::vec4(0.10f, 0.22f, 0.18f, 0.85f));
            rects.draw(shader, screenVP,
                       glm::vec2(pc.x - panelW * 0.5f + 30.0f, rowY + 26.0f),
                       glm::vec2(10.0f, 10.0f), kHudGood);
        }
        const glm::vec4 nameCol = s ? kHudGood : glm::vec4(0.78f, 0.82f, 0.88f, 1.0f);
        float nw = 0, nh = 0;
        cjk.measure(kLevels[i].name, 26, nw, nh);
        cjk.draw(screenVP, kLevels[i].name, 26, glm::vec2(pc.x - nw * 0.5f, rowY), nameCol);
        float dw = 0, dh = 0;
        cjk.measure(kLevels[i].desc, 15, dw, dh);
        cjk.draw(screenVP, kLevels[i].desc, 15,
                 glm::vec2(pc.x - dw * 0.5f, rowY + 32.0f),
                 s ? glm::vec4(0.62f, 0.70f, 0.78f, 1.0f) : glm::vec4(0.52f, 0.57f, 0.65f, 1.0f));
        const std::string st = (i < static_cast<int>(status.size())) ? status[i] : "";
        float sw = 0, sh = 0;
        cjk.measure(st, 15, sw, sh);
        cjk.draw(screenVP, st, 15, glm::vec2(pc.x - sw * 0.5f, rowY + 54.0f),
                 s ? glm::vec4(0.78f, 0.86f, 0.92f, 1.0f) : glm::vec4(0.62f, 0.68f, 0.76f, 1.0f));
    }

    // 底部操作提示（在绿色强调条上方，留出间距）
    const char* tip = "W/S 选择    回车 开始    ESC 退出";
    float tipW = 0, tipH = 0;
    cjk.measure(tip, 17, tipW, tipH);
    cjk.draw(screenVP, tip, 17, glm::vec2(pc.x - tipW * 0.5f, pc.y + panelH * 0.5f - 36.0f),
             glm::vec4(0.50f, 0.55f, 0.62f, 1.0f));
}

// 进菜单前刷新每关进度文案（读各关独立存档）
static void refreshMenuStatus(const std::string& base) {
    const int n = static_cast<int>(sizeof(kLevels) / sizeof(kLevels[0]));
    g_menuStatus.assign(n, "未开始");
    for (int i = 0; i < n; ++i) {
        const std::string pp = base + kLevels[i].profile;
        echo::MetaState m;
        std::string e;
        if (echo::save::fileExists(pp) && echo::save::readFile(pp, m, &e)) {
            if (m.escapes > 0 && m.bestTime >= 0.0f)
                g_menuStatus[i] = "已通关 · 最佳总用时 " + fixed1(m.bestTime) + " 秒";
            else if (m.loops > 0)
                g_menuStatus[i] = "已探索 " + std::to_string(m.loops) + " 轮";
            else
                g_menuStatus[i] = "未开始";
        }
    }
}

} // namespace

// 双击 exe 运行时没有控制台，stderr 看不到。所有启动期致命错误都弹窗告知用户，
// 避免「窗口一闪就消失、毫无提示」的体验。
static void fatalBox(const char* title, const std::string& msg) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg.c_str(), nullptr);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    // SDL_MAIN_HANDLED 模式下，我们自己接管入口，需显式告知 SDL 主已就绪
    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int vpW = 960, vpH = 660;

    SDL_Window* window = SDL_CreateWindow(
        "Echo Protocol - Echo Relay (time loop)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        vpW, vpH,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        fatalBox("Echo Protocol 启动失败",
                 std::string("无法创建窗口（SDL_CreateWindow 失败）：\n") + SDL_GetError());
        SDL_Quit();
        return -1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << std::endl;
        fatalBox("Echo Protocol 启动失败",
                 std::string("无法创建 OpenGL 上下文（需要 OpenGL 3.3）：\n") + SDL_GetError()
                 + "\n\n请确认显卡驱动已安装，或更新显卡驱动后重试。");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        fatalBox("Echo Protocol 启动失败", "无法初始化 OpenGL 函数（GLAD 加载失败）。");
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    std::cout << "OpenGL " << GLVersion.major << "." << GLVersion.minor
              << " context ready" << std::endl;

    SDL_GL_SetSwapInterval(1);          // VSync
    SDL_RaiseWindow(window);            // 抢焦点（否则从 VS 启动时按键会送进控制台）
    SDL_SetWindowInputFocus(window);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);                 // HUD / 记忆陷阱需要半透明
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.06f, 0.07f, 0.10f, 1.0f);

    // 资源根目录：关卡 JSON / 着色器都在 <exedir>/assets 下（CMake POST_BUILD 拷贝）
    const char* basePathRaw = SDL_GetBasePath();
    const std::string basePath = basePathRaw ? basePathRaw : "./";

    // 着色器从资源文件加载（文件缺失时 Shader 内部回退到兜底源码）
    Shader shader = Shader::fromFiles(basePath + "assets/shaders/basic.vert",
                                     basePath + "assets/shaders/basic.frag");
    if (shader.handle() == 0) {
        std::cerr << "FATAL: shader program failed to build "
                     "(check assets/shaders/*.vert|.frag)\n";
        fatalBox("Echo Protocol 启动失败",
                 "着色器编译失败，请确认 assets/shaders/ 下的 basic.vert/basic.frag 与 text.* 文件存在。");
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    Camera2D     camera(static_cast<float>(vpW), static_cast<float>(vpH));
    RectRenderer rects;
    TextRenderer text(rects, shader); // 点阵文字（HUD / 结算面板用）
    CjkText      cjk(basePath + "assets/shaders/text.vert",
                     basePath + "assets/shaders/text.frag"); // 中文 / UTF-8 文字

    // -----------------------------------------------------------------------
    // 游戏：加载关卡 + 读存档
    // -----------------------------------------------------------------------
    const std::string levelPath   = basePath + kLevels[0].path;
    const std::string profilePath = basePath + kLevels[0].profile;

    echo::EventBus bus;
    echo::Game     game(bus);

    // 音效（M6）：用 SDL2 原生音频 API 程序化合成，零素材文件、零新依赖。
    // 无音频设备时 init() 静默失败，play 全部 no-op —— 不影响游戏逻辑。
    echo::AudioSystem audio;
    audio.init();
    game.setAudio(&audio); // 必须在 init() 之前挂上，事件订阅才接得上

    std::string loadErr;
    if (!game.init(levelPath, profilePath, &loadErr)) {
        std::cerr << "Game init failed: " << loadErr << std::endl;
        std::cerr << "level path was: " << levelPath << std::endl;
        fatalBox("Echo Protocol 启动失败",
                 std::string("游戏初始化失败：\n") + loadErr
                 + "\n\n尝试加载的关卡路径：\n" + levelPath
                 + "\n\n请确认 exe 同目录下存在 assets/levels/ 文件夹及关卡 JSON。");
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    refreshMenuStatus(basePath); // 进菜单前刷新每关进度

    bool                        running   = true;
    SDL_Event                   event;
    Uint32                      lastTicks = SDL_GetTicks();
    bool                        keyHeld[SDL_NUM_SCANCODES] = {};
    float                       pulse     = 0.0f;
    bool                        paused    = false;  // ESC 暂停菜单开关
    int                         pauseMenu = 0;      // 当前高亮的暂停菜单项索引

    // ---- 选关菜单状态 ----
    int                         mode        = MODE_MENU; // MODE_MENU / MODE_PLAY
    int                         menuSel     = 0;         // 选关菜单高亮项
    int                         loadedLevel = 0;         // 当前已加载关卡下标（init 加载了第 0 关）

    while (running) {
        const Uint32 now = SDL_GetTicks();
        float dt = static_cast<float>(now - lastTicks) / 1000.0f;
        lastTicks = now;
        if (dt > 0.1f) dt = 0.1f; // 切窗口回来时 dt 会爆，夹一下

        // ------------------- 输入 -------------------
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;

            case SDL_KEYDOWN: {
                const SDL_Keycode sym = event.key.keysym.sym;

                // ESC：菜单里退出程序；通关定格时回选关菜单；游玩中切换暂停菜单
                if (sym == SDLK_ESCAPE) {
                    if (mode == MODE_MENU) {
                        running = false;
                    } else if (game.finished()) {
                        mode = MODE_MENU;                 // 通关后回选关菜单
                        paused = false;
                        refreshMenuStatus(basePath);
                    } else {
                        paused = !paused;       // 进/出暂停都清掉按键残留
                        pauseMenu = 0;
                        std::memset(keyHeld, 0, sizeof(keyHeld));
                        audio.play(echo::Sfx::MenuMove);
                    }
                    break;
                }

                if (!event.key.repeat) {
                    keyHeld[event.key.keysym.scancode] = true;

                    if (mode == MODE_MENU) {
                        // 选关菜单导航
                        const int ln = static_cast<int>(
                            sizeof(kLevels) / sizeof(kLevels[0]));
                        if (sym == SDLK_UP || sym == SDLK_w) {
                            menuSel = (menuSel + ln - 1) % ln;
                            audio.play(echo::Sfx::MenuMove);
                        }
                        if (sym == SDLK_DOWN || sym == SDLK_s) {
                            menuSel = (menuSel + 1) % ln;
                            audio.play(echo::Sfx::MenuMove);
                        }
                        if (sym == SDLK_RETURN || sym == SDLK_SPACE ||
                            sym == SDLK_KP_ENTER) {
                            audio.play(echo::Sfx::MenuConfirm);
                            if (menuSel != loadedLevel) {
                                game.loadLevel(basePath + kLevels[menuSel].path,
                                               basePath + kLevels[menuSel].profile);
                                loadedLevel = menuSel;
                            }
                            mode = MODE_PLAY;   // 直接玩已加载的关卡
                            paused = false;
                        }
                    } else if (paused) {
                        // 暂停菜单导航
                        if (sym == SDLK_UP || sym == SDLK_w) {
                            pauseMenu = (pauseMenu + 4) % 5;
                            audio.play(echo::Sfx::MenuMove);
                        }
                        if (sym == SDLK_DOWN || sym == SDLK_s) {
                            pauseMenu = (pauseMenu + 1) % 5;
                            audio.play(echo::Sfx::MenuMove);
                        }
                        if (sym == SDLK_RETURN || sym == SDLK_SPACE ||
                            sym == SDLK_KP_ENTER) {
                            audio.play(echo::Sfx::MenuConfirm);
                            if (pauseMenu == 0) {
                                paused = false;                       // RESUME
                            } else if (pauseMenu == 1) {
                                game.setGodMode(!game.godMode());     // 无敌模式 开关
                            } else if (pauseMenu == 2) {
                                game.restartKeepMeta(); paused = false; // RESTART
                            } else if (pauseMenu == 3) {
                                game.wipeProfile();      paused = false; // WIPE SAVE
                            } else {
                                running = false;                       // QUIT
                            }
                        }
                    } else if (game.finished()) {
                        // 通关定格：R 再跑一次，F5 清档，M/回车/ESC 回选关菜单
                        if (sym == SDLK_r)  game.replayAfterEscape();
                        if (sym == SDLK_F5) game.wipeProfile();
                        if (sym == SDLK_m || sym == SDLK_RETURN ||
                            sym == SDLK_KP_ENTER) {
                            audio.play(echo::Sfx::MenuConfirm);
                            mode   = MODE_MENU;
                            paused = false;
                            refreshMenuStatus(basePath);
                        }
                    } else {
                        // 正常游玩的快捷键（暂停里不响应，由菜单负责）
                        if (sym == SDLK_r)  game.killForDebug();  // 故意死一次
                        if (sym == SDLK_F5) game.wipeProfile();   // 清空存档
                    }
                }
                break;
            }

            case SDL_KEYUP:
                keyHeld[event.key.keysym.scancode] = false;
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    vpW = event.window.data1;
                    vpH = event.window.data2;
                    glViewport(0, 0, vpW, vpH);
                    camera.resize(static_cast<float>(vpW), static_cast<float>(vpH));
                } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                    std::cout << "[focus] game window focused" << std::endl;
                } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    std::memset(keyHeld, 0, sizeof(keyHeld));
                    std::cout << "[focus] focus lost - keys go elsewhere now" << std::endl;
                }
                break;

            default:
                break;
            }
        }

        // SDL 全局键盘状态 ∪ 事件维护的按下集合（任一可用即可，抗失焦丢键）
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        auto down = [&](SDL_Scancode sc) {
            return keyHeld[sc] || (ks != nullptr && ks[sc] != 0);
        };

        echo::InputFrame input;
        input.up    = down(SDL_SCANCODE_W) || down(SDL_SCANCODE_UP);
        input.down  = down(SDL_SCANCODE_S) || down(SDL_SCANCODE_DOWN);
        input.left  = down(SDL_SCANCODE_A) || down(SDL_SCANCODE_LEFT);
        input.right = down(SDL_SCANCODE_D) || down(SDL_SCANCODE_RIGHT);

        // ------------------- 逻辑 -------------------
        // 暂停时不推进世界（倒计时、移动全冻结）；通关定格时 update 内部也会早退
        if (!paused) game.update(dt, input);
        for (const std::string& line : game.drainLog()) {
            std::cout << line << std::endl;
        }

        const echo::GameState& gs = game.state();

        // ------------------- 相机 -------------------
        // viewScale < 1 时视野窗口按比例缩小 → 世界显得更大，需移动才能看全
        const float viewW = kViewWorldWidth * gs.world.viewScale;
        const float viewH = kViewWorldHeight * gs.world.viewScale;
        const float zoom  = std::min(static_cast<float>(vpW) / viewW,
                                     static_cast<float>(vpH) / viewH);
        camera.setZoom(zoom);

        const float halfViewW = (static_cast<float>(vpW) / camera.zoom()) * 0.5f;
        const float loX = gs.world.boundsMin.x - 95.0f + halfViewW;
        const float hiX = gs.world.boundsMax.x + 95.0f - halfViewW;
        float camX = gs.run.player.x;
        if (loX <= hiX) camX = std::clamp(camX, loX, hiX);
        else            camX = (gs.world.boundsMin.x + gs.world.boundsMax.x) * 0.5f;

        // 相机同时跟随玩家上下（旧版把 Y 写死 0，玩家上移时视角不跟着走）
        const float halfViewH = (static_cast<float>(vpH) / camera.zoom()) * 0.5f;
        const float loY = gs.world.boundsMin.y - 95.0f + halfViewH;
        const float hiY = gs.world.boundsMax.y + 95.0f - halfViewH;
        float camY = gs.run.player.y;
        if (loY <= hiY) camY = std::clamp(camY, loY, hiY);
        else            camY = (gs.world.boundsMin.y + gs.world.boundsMax.y) * 0.5f;

        camera.setPosition(glm::vec2(camX, camY));

        // ------------------- 选关菜单 / 世界渲染 分发 -------------------
        if (mode == MODE_MENU) {
            glClear(GL_COLOR_BUFFER_BIT);
            const glm::mat4 screenVP = glm::ortho(0.0f, static_cast<float>(vpW),
                                                  static_cast<float>(vpH), 0.0f, -1.0f, 1.0f);
            drawLevelMenu(shader, rects, cjk, screenVP, static_cast<float>(vpW),
                          static_cast<float>(vpH), menuSel, g_menuStatus);
            SDL_GL_SwapWindow(window);
            pulse += dt;
            continue; // 菜单态不渲染世界 / HUD
        }

        glClear(GL_COLOR_BUFFER_BIT);
        const glm::mat4 vp = camera.viewProjection();

        // 1) 房间地面（ECS：Transform + RoomComponent）
        for (const echo::Entity e : gs.ecs.query<echo::TransformComponent, echo::RoomComponent>()) {
            const echo::RoomComponent*  rc = gs.ecs.get<echo::RoomComponent>(e);
            const echo::TransformComponent* tc = gs.ecs.get<echo::TransformComponent>(e);
            if (rc && tc) rects.draw(shader, vp, tc->pos, tc->size, rc->color);
        }

        // 2) 出口（呼吸感的青色，提示"这里能出去"）
        {
            const float k = 0.55f + 0.45f * std::sin(pulse * 3.0f);
            glm::vec4 c = kExitColor;
            c.w = 0.45f + 0.45f * k;
            for (const echo::Entity e : gs.ecs.query<echo::TransformComponent, echo::ExitComponent>()) {
                const echo::TransformComponent* tc = gs.ecs.get<echo::TransformComponent>(e);
                if (tc) rects.draw(shader, vp, tc->pos, tc->size, c);
            }
        }

        // 3) 墙（ECS：Transform + WallComponent）
        for (const echo::Entity e : gs.ecs.query<echo::TransformComponent, echo::WallComponent>()) {
            const echo::TransformComponent* tc = gs.ecs.get<echo::TransformComponent>(e);
            if (tc) rects.draw(shader, vp, tc->pos, tc->size, kWallColor);
        }

        // 4) 门：锁着用关卡配置色（第四关紫门/黄门），开了就是绿的
        for (const echo::Entity e : gs.ecs.query<echo::TransformComponent, echo::DoorComponent>()) {
            const echo::DoorComponent* d  = gs.ecs.get<echo::DoorComponent>(e);
            const echo::TransformComponent* tc = gs.ecs.get<echo::TransformComponent>(e);
            if (!d || !tc) continue;
            const bool locked = !gs.run.holdsCode(d->requiresCode);
            rects.draw(shader, vp, tc->pos, tc->size, locked ? d->color : kDoorOpen);
        }

        // 4.1) 传送门（第四关）：呼吸发光的垫子 + 字母，踩上去传到同组的另一端
        {
            const float k = 0.55f + 0.45f * std::sin(pulse * 4.0f);
            for (const echo::TeleporterDef& t : gs.world.teleporters) {
                glm::vec4 c = t.color;
                c.w = 0.55f + 0.35f * k;
                rects.draw(shader, vp, t.rect.center, t.rect.size, c);
                float lw = 0, lh = 0;
                const std::string lbl = "传送门 " + t.label;
                cjk.measure(lbl, 18, lw, lh);
                cjk.draw(vp, lbl, 18,
                         glm::vec2(t.rect.center.x - lw * 0.5f,
                                   t.rect.center.y - t.rect.size.y * 0.5f - 20.0f),
                         glm::vec4(0.85f, 0.55f, 0.95f, 1.0f));
            }
        }

        // 5) 情报点（用关卡配置色：第四关紫/黄钥匙）
        //    M7 后钥匙是"实物"：没持有 = 原色躺在原地（可捡）；
        //    已持有 = 变淡 + 白描边（随身携带，死亡会掉回原地）。
        for (const echo::Entity e : gs.ecs.query<echo::TransformComponent, echo::CoreComponent>()) {
            const echo::CoreComponent* c  = gs.ecs.get<echo::CoreComponent>(e);
            const echo::TransformComponent* tc = gs.ecs.get<echo::TransformComponent>(e);
            if (!c || !tc) continue;
            const bool held = gs.run.holdsCode(c->payload);
            glm::vec4 col = c->color;
            if (held) {
                col.a = 0.35f; // 已在身上，画淡
                const glm::vec4 outline(1.0f, 1.0f, 1.0f, 0.85f);
                const glm::vec2 gp = tc->pos, gsx = tc->size + glm::vec2(10.0f);
                rects.draw(shader, vp, gp, gsx, outline);      // 白描边底层
                rects.draw(shader, vp, gp, tc->size, col);     // 淡色本体
            } else {
                rects.draw(shader, vp, tc->pos, tc->size, col); // 原色：在场上，可拾取
            }
        }

        // 6) 记忆里的陷阱 —— 只有 Meta 层记住了的才画得出来。
        //    这就是"信息永久积累"在屏幕上的样子。
        for (const echo::Entity e : gs.ecs.query<echo::TransformComponent, echo::TrapComponent>()) {
            const echo::TrapComponent* tr = gs.ecs.get<echo::TrapComponent>(e);
            const echo::TransformComponent* tc = gs.ecs.get<echo::TransformComponent>(e);
            if (!tr || !tc) continue;
            if (!gs.meta.knowsTrap(tr->id)) continue;
            rects.draw(shader, vp, tc->pos, tc->size, kKnownTrap);
        }

        // 6.1) 敌兵本体（M4：主动威胁）。永远可见；颜色随 FSM 状态变化。
        for (const echo::Entity e : gs.ecs.query<echo::TransformComponent, echo::EnemyComponent>()) {
            const echo::EnemyComponent* en = gs.ecs.get<echo::EnemyComponent>(e);
            const echo::TransformComponent* tc = gs.ecs.get<echo::TransformComponent>(e);
            if (!en || !tc) continue;
            glm::vec4 col = kEnemyColor;
            if (en->state == echo::AIState::Alert || en->state == echo::AIState::Attack)
                col = kEnemyAlert;
            else if (en->state == echo::AIState::Search)
                col = kEnemySearch;
            rects.draw(shader, vp, tc->pos, tc->size, col);

            // M5：追击/搜索时画出当前 A* 导航路径（淡黄折线），直观展示"绕墙逼近"
            if ((en->state == echo::AIState::Alert || en->state == echo::AIState::Attack
                 || en->state == echo::AIState::Search) && !en->path.empty()) {
                const auto& pp = en->path;
                const float thick = 3.0f;
                for (std::size_t i = 0; i + 1 < pp.size(); ++i) {
                    const glm::vec2 mid = (pp[i] + pp[i + 1]) * 0.5f;
                    const glm::vec2 d = pp[i + 1] - pp[i];
                    if (std::abs(d.x) >= std::abs(d.y))
                        rects.draw(shader, vp, mid, glm::vec2(std::abs(d.x), thick), kPathHint);
                    else
                        rects.draw(shader, vp, mid, glm::vec2(thick, std::abs(d.y)), kPathHint);
                }
            }
        }

        // 6.2) 记忆里的敌兵巡逻路线（M4）：被某敌人杀过才显示其巡逻折线 + 标签。
        //      与"踩过的陷阱才可见"对称，是"信息永久积累"在主动威胁上的体现。
        auto drawSeg = [&](const glm::vec2& a, const glm::vec2& b, const glm::vec4& c) {
            const glm::vec2 mid = (a + b) * 0.5f;
            const glm::vec2 d = b - a;
            const float thick = 4.0f;
            if (std::abs(d.x) >= std::abs(d.y))
                rects.draw(shader, vp, mid, glm::vec2(std::abs(d.x), thick), c);
            else
                rects.draw(shader, vp, mid, glm::vec2(thick, std::abs(d.y)), c);
        };
        for (const echo::Entity e : gs.ecs.query<echo::TransformComponent, echo::EnemyComponent>()) {
            const echo::EnemyComponent* en = gs.ecs.get<echo::EnemyComponent>(e);
            const echo::TransformComponent* tc = gs.ecs.get<echo::TransformComponent>(e);
            if (!en || !tc) continue;
            if (!gs.meta.knowsEnemy(en->id)) continue;
            const auto& p = en->patrol;
            if (p.size() >= 2) {
                for (std::size_t i = 0; i + 1 < p.size(); ++i)
                    drawSeg(p[i], p[i + 1], kKnownRoute);
            }
            const std::string lbl = "巡逻兵 #" + std::to_string(en->id);
            float lw = 0, lh = 0;
            cjk.measure(lbl, 20, lw, lh);
            cjk.draw(vp, lbl, 20,
                     glm::vec2(tc->pos.x - lw * 0.5f, tc->pos.y - tc->size.y * 0.5f - 22.0f),
                     kKnownRoute);
        }

        // 6.5) 世界空间标签：房间名 / 门的密码需求 / 核心携带的密码。
        //      让"哪道门要哪个密码、哪个核心是诱饵"在画面里一眼可读，
        //      玩家才能真的靠记忆规划路线、而不是乱撞。
        for (const echo::Entity e : gs.ecs.query<echo::TransformComponent, echo::RoomComponent>()) {
            const echo::RoomComponent* rc = gs.ecs.get<echo::RoomComponent>(e);
            const echo::TransformComponent* tc = gs.ecs.get<echo::TransformComponent>(e);
            if (!rc || !tc) continue;
            float rw = 0, rh = 0;
            cjk.measure(rc->label, 22, rw, rh);
            cjk.draw(vp, rc->label, 22,
                     glm::vec2(tc->pos.x - rw * 0.5f, tc->pos.y - tc->size.y * 0.5f + 18.0f),
                     glm::vec4(0.50f, 0.55f, 0.62f, 1.0f));
        }
        for (const echo::Entity e : gs.ecs.query<echo::TransformComponent, echo::DoorComponent>()) {
            const echo::DoorComponent* d  = gs.ecs.get<echo::DoorComponent>(e);
            const echo::TransformComponent* tc = gs.ecs.get<echo::TransformComponent>(e);
            if (!d || !tc) continue;
            const bool locked = !gs.run.holdsCode(d->requiresCode);
            const glm::vec4 c = locked ? kHudWarn : kHudGood;
            const std::string lbl = "需要 " + d->requiresCode;
            float dw = 0, dh = 0;
            cjk.measure(lbl, 20, dw, dh);
            cjk.draw(vp, lbl, 20,
                     glm::vec2(tc->pos.x - dw * 0.5f, tc->pos.y - tc->size.y * 0.5f - 22.0f), c);
        }
        for (const echo::Entity e : gs.ecs.query<echo::TransformComponent, echo::CoreComponent>()) {
            const echo::CoreComponent* c  = gs.ecs.get<echo::CoreComponent>(e);
            const echo::TransformComponent* tc = gs.ecs.get<echo::TransformComponent>(e);
            if (!c || !tc) continue;
            const bool held  = gs.run.holdsCode(c->payload);
            const bool known = gs.meta.knowsPassword(c->payload);
            glm::vec4 lc;
            if (held)       lc = glm::vec4(1.00f, 1.00f, 1.00f, 0.95f); // 已持有：白
            else if (known) lc = glm::vec4(0.55f, 0.40f, 0.60f, 0.80f); // 记得但不在身上：淡
            else            lc = glm::vec4(0.92f, 0.45f, 0.98f, 1.00f); // 陌生：亮
            const std::string lbl = held ? (c->payload + " (已持有)") : c->payload;
            text.draw(vp, lbl, glm::vec2(tc->pos.x, tc->pos.y + 30.0f), 2.5f, lc);
        }

        // 7) 玩家
        rects.draw(shader, vp, gs.run.player, gs.run.playerSize, kPlayerColor);

        // ---- HUD（屏幕像素空间，原点左上、y 向下）----
        const glm::mat4 screenVP = glm::ortho(0.0f, static_cast<float>(vpW),
                                              static_cast<float>(vpH), 0.0f, -1.0f, 1.0f);
        const float w = static_cast<float>(vpW);
        const float h = static_cast<float>(vpH);

        // 顶部倒计时条
        const float barX = 20.0f, barY = 24.0f;
        const float barW = w - 40.0f, barH = 16.0f;
        rects.draw(shader, screenVP, glm::vec2(barX + barW * 0.5f, barY),
                   glm::vec2(barW, barH), kHudBack);

        const float ratio = (gs.world.loopSeconds > 0.0f)
                                ? std::clamp(gs.run.timeLeft / gs.world.loopSeconds, 0.0f, 1.0f)
                                : 0.0f;
        const glm::vec4 barColor = (ratio > 0.5f) ? kHudGood
                                 : (ratio > 0.25f) ? kHudWarn : kHudBad;
        if (ratio > 0.001f) {
            const float fillW = barW * ratio;
            rects.draw(shader, screenVP, glm::vec2(barX + fillW * 0.5f, barY),
                       glm::vec2(fillW, barH - 4.0f), barColor);
        }

        // ---- 左上信息卡：轮数 / 死亡 / 无敌 ----
        {
            const float pad = 12.0f;
            const float cardX = 20.0f, cardY = barY + barH + 12.0f;
            const float cardW = 200.0f, cardH = 96.0f;
            rects.draw(shader, screenVP, glm::vec2(cardX + cardW * 0.5f, cardY + cardH * 0.5f),
                       glm::vec2(cardW, cardH), kHudBack);
            cjk.draw(screenVP, "循环 " + std::to_string(gs.meta.loops),
                     22, glm::vec2(cardX + pad, cardY + 10.0f), kHudGood);
            cjk.draw(screenVP, "死亡 " + std::to_string(gs.meta.deaths),
                     18, glm::vec2(cardX + pad, cardY + 42.0f),
                     glm::vec4(0.95f, 0.45f, 0.45f, 1.0f));
            const bool inv = game.godMode();
            cjk.draw(screenVP, "无敌 " + std::string(inv ? "开" : "关"),
                     16, glm::vec2(cardX + pad, cardY + 70.0f),
                     inv ? glm::vec4(0.45f, 0.95f, 0.55f, 1.0f)
                         : glm::vec4(0.62f, 0.68f, 0.76f, 1.0f));
        }

        // ---- 右上信息卡：持有钥匙 / 已知密码 / 已知陷阱 ----
        {
            const float pad   = 12.0f;
            const float cardW = 230.0f, cardH = 128.0f;
            const float cardX = w - 20.0f - cardW, cardY = barY + barH + 12.0f;
            rects.draw(shader, screenVP, glm::vec2(cardX + cardW * 0.5f, cardY + cardH * 0.5f),
                       glm::vec2(cardW, cardH), kHudBack);

            // 行前小方块图标
            rects.draw(shader, screenVP, glm::vec2(cardX + 8.0f, cardY + 18.0f),
                       glm::vec2(12.0f, 12.0f), kKeyHeldIcon);  // KEY 图标（金）
            rects.draw(shader, screenVP, glm::vec2(cardX + 8.0f, cardY + 50.0f),
                       glm::vec2(12.0f, 12.0f), kItemColor);   // CODE 图标（青）
            rects.draw(shader, screenVP, glm::vec2(cardX + 8.0f, cardY + 82.0f),
                       glm::vec2(12.0f, 12.0f), kKnownTrap);   // TRAPS 图标（暗红）
            rects.draw(shader, screenVP, glm::vec2(cardX + 8.0f, cardY + 114.0f),
                       glm::vec2(12.0f, 12.0f), kEnemyColor);  // ENEMY 图标（橙红）

            // 持有钥匙：本轮手里实际拿着的（死亡会掉落清空）
            const std::string heldStr =
                gs.run.carriedCodes.empty()
                    ? "--" : joinStrings(gs.run.carriedCodes, " ");
            cjk.draw(screenVP, "持有 " + heldStr,
                     18, glm::vec2(cardX + pad + 4.0f, cardY + 11.0f), kKeyHeldIcon);
            // 已知密码：永久记忆里发现过的（信息积累，不会因死亡丢失）
            const std::string codeStr =
                gs.meta.knownPasswords.empty()
                    ? "--" : joinStrings(gs.meta.knownPasswords, " ");
            cjk.draw(screenVP, "密码 " + codeStr,
                     18, glm::vec2(cardX + pad + 4.0f, cardY + 43.0f), kItemColor);
            cjk.draw(screenVP, "陷阱 " + std::to_string(gs.meta.knownTrapIds.size()),
                     18, glm::vec2(cardX + pad + 4.0f, cardY + 75.0f),
                     glm::vec4(0.95f, 0.45f, 0.45f, 1.0f));
            cjk.draw(screenVP, "敌兵 " + std::to_string(gs.meta.knownEnemyIds.size()),
                     18, glm::vec2(cardX + pad + 4.0f, cardY + 107.0f), kEnemyColor);

            // 最佳时间：右下角独立小条
            const std::string bestStr = (gs.meta.bestTime >= 0.0f)
                ? ("最佳 " + fixed1(gs.meta.bestTime) + "秒") : "最佳 --";
            float bw = 0, bh = 0;
            cjk.measure(bestStr, 20, bw, bh);
            cjk.draw(screenVP, bestStr,
                     20, glm::vec2(w - 20.0f - bw, static_cast<float>(vpH) - 24.0f),
                     glm::vec4(0.70f, 0.78f, 0.86f, 1.0f));
        }

        // ---- 底部操作提示 ----
        {
            const std::string hint = game.finished()
                ? "R 重跑    F5 清档    ESC 退出"
                : "WASD 移动    R 死亡    F5 清档    ESC 暂停";
            cjk.draw(screenVP, hint,
                     18, glm::vec2(20.0f, static_cast<float>(vpH) - 24.0f),
                     glm::vec4(0.60f, 0.66f, 0.76f, 1.0f));
        }

        // ---- 通关结算面板（M2.1）----
        if (game.finished()) {
            const float h = static_cast<float>(vpH);
            const bool  god = game.godMode();   // 无敌模式：本次不记入通关记录

            // 暗幕
            rects.draw(shader, screenVP, glm::vec2(w * 0.5f, h * 0.5f),
                       glm::vec2(w, h), glm::vec4(0.02f, 0.03f, 0.05f, 0.72f));

            // 面板 + 上下强调条
            const float panelW = std::min(640.0f, w - 60.0f);
            const float panelH = god ? 372.0f : 330.0f;   // 无敌时多一行警告
            const glm::vec2 pc(w * 0.5f, h * 0.5f);
            rects.draw(shader, screenVP, pc, glm::vec2(panelW, panelH),
                       glm::vec4(0.05f, 0.09f, 0.12f, 0.98f));
            rects.draw(shader, screenVP, glm::vec2(pc.x, pc.y - panelH * 0.5f + 5.0f),
                       glm::vec2(panelW - 20.0f, 8.0f), kExitColor);
            rects.draw(shader, screenVP, glm::vec2(pc.x, pc.y + panelH * 0.5f - 5.0f),
                       glm::vec2(panelW - 20.0f, 8.0f), kHudGood);

            // 标题（中文）
            float titleW = 0, titleH = 0;
            cjk.measure("已逃脱", 40, titleW, titleH);
            cjk.draw(screenVP, "已逃脱", 40,
                     glm::vec2(pc.x - titleW * 0.5f, pc.y - 140.0f), kExitColor);

            // 统计（中文，左对齐排版）
            const float statsPx = 22.0f;
            const float sx = pc.x - panelW * 0.5f + 36.0f;
            cjk.draw(screenVP, "时间   : " + fixed1(gs.run.elapsed) + " 秒", statsPx,
                     glm::vec2(sx, pc.y - 96.0f), glm::vec4(0.86f, 0.90f, 0.95f, 1.0f));
            if (god) {
                // 无敌模式：不刷新成绩，明确标注
                cjk.draw(screenVP, "最佳   : 不记录(无敌模式)", statsPx,
                         glm::vec2(sx, pc.y - 56.0f), glm::vec4(0.98f, 0.70f, 0.30f, 1.0f));
            } else {
                cjk.draw(screenVP, "最佳   : " + fixed1(gs.meta.bestTime) + " 秒", statsPx,
                         glm::vec2(sx, pc.y - 56.0f), glm::vec4(0.86f, 0.90f, 0.95f, 1.0f));
            }
            cjk.draw(screenVP, "死亡   : " + std::to_string(gs.meta.deaths), statsPx,
                     glm::vec2(sx, pc.y - 16.0f), glm::vec4(0.86f, 0.90f, 0.95f, 1.0f));
            cjk.draw(screenVP, "循环   : " + std::to_string(gs.meta.loops), statsPx,
                     glm::vec2(sx, pc.y + 24.0f), glm::vec4(0.86f, 0.90f, 0.95f, 1.0f));
            cjk.draw(screenVP, "记忆   : " + std::to_string(gs.meta.knownPasswords.size())
                          + " 密码 / " + std::to_string(gs.meta.knownTrapIds.size()) + " 陷阱",
                     statsPx, glm::vec2(sx, pc.y + 64.0f),
                     glm::vec4(0.86f, 0.90f, 0.95f, 1.0f));

            // 无敌模式警告（唯一与正常结算不同的视觉提示）
            if (god) {
                float ww = 0, wh = 0;
                cjk.measure("⚠ 无敌模式开启：本次逃脱不记入通关记录", 18, ww, wh);
                cjk.draw(screenVP, "⚠ 无敌模式开启：本次逃脱不记入通关记录", 18,
                         glm::vec2(pc.x - ww * 0.5f, pc.y + 96.0f),
                         glm::vec4(0.98f, 0.70f, 0.30f, 1.0f));
            }

            // 闪烁提示：R 重跑
            const float blink  = 0.40f + 0.60f * (0.5f + 0.5f * std::sin(pulse * 4.0f));
            const float prY   = god ? pc.y + 128.0f : pc.y + 96.0f;
            float prW = 0, prH = 0;
            cjk.measure("按 R 再跑一次", 24, prW, prH);
            cjk.draw(screenVP, "按 R 再跑一次", 24,
                     glm::vec2(pc.x - prW * 0.5f, prY),
                     glm::vec4(kExitColor.r, kExitColor.g, kExitColor.b, blink));

            const float shY = god ? pc.y + 160.0f : pc.y + 128.0f;
            float shW = 0, shH = 0;
            cjk.measure("回车/M 返回选关    F5 清空存档    ESC 选关", 18, shW, shH);
            cjk.draw(screenVP, "回车/M 返回选关    F5 清空存档    ESC 选关", 18,
                     glm::vec2(pc.x - shW * 0.5f, shY),
                     glm::vec4(0.55f, 0.60f, 0.68f, 1.0f));
        }

        // ---- 暂停菜单（M3）----
        if (paused && !game.finished()) {
            const float mw = std::min(380.0f, w - 60.0f);
            const float mh = 320.0f;
            const glm::vec2 mc(w * 0.5f, h * 0.5f);

            // 暗幕 + 面板 + 上下强调条
            rects.draw(shader, screenVP, glm::vec2(w * 0.5f, h * 0.5f),
                       glm::vec2(w, h), glm::vec4(0.02f, 0.03f, 0.05f, 0.72f));
            rects.draw(shader, screenVP, mc, glm::vec2(mw, mh),
                       glm::vec4(0.06f, 0.10f, 0.14f, 0.98f));
            rects.draw(shader, screenVP, glm::vec2(mc.x, mc.y - mh * 0.5f + 5.0f),
                       glm::vec2(mw - 20.0f, 8.0f), kExitColor);
            rects.draw(shader, screenVP, glm::vec2(mc.x, mc.y + mh * 0.5f - 5.0f),
                       glm::vec2(mw - 20.0f, 8.0f), kHudGood);

            // 标题（中文）
            float titleW = 0, titleH = 0;
            cjk.measure("已暂停", 34, titleW, titleH);
            cjk.draw(screenVP, "已暂停", 34,
                     glm::vec2(mc.x - titleW * 0.5f, mc.y - mh * 0.5f + 24.0f),
                     glm::vec4(kExitColor.r, kExitColor.g, kExitColor.b, 1.0f));

            // 菜单项（中文；选中项左侧画一个绿色小方块当光标）
            const std::string invLabel =
                std::string("无敌模式：") + (game.godMode() ? "开" : "关");
            const char* itemsZH[5] = { "继续", invLabel.c_str(), "重新开始",
                                       "清空存档", "退出游戏" };
            const float itemPx = 26.0f;
            const float startY = mc.y - 80.0f;
            for (int i = 0; i < 5; ++i) {
                const bool  sel = (i == pauseMenu);
                // 无敌模式项：开启时即使未选中也用绿色提示
                glm::vec4 col = sel ? kHudGood : glm::vec4(0.62f, 0.68f, 0.76f, 1.0f);
                if (i == 1 && game.godMode()) col = glm::vec4(0.45f, 0.95f, 0.55f, 1.0f);
                float iw = 0, ih = 0;
                cjk.measure(itemsZH[i], itemPx, iw, ih);
                const float ix = mc.x - iw * 0.5f;
                const float iy = startY + i * 44.0f;
                cjk.draw(screenVP, itemsZH[i], itemPx, glm::vec2(ix, iy), col);
                if (sel) {
                    rects.draw(shader, screenVP,
                               glm::vec2(ix - 22.0f, iy + ih * 0.5f),
                               glm::vec2(10.0f, 10.0f), kHudGood);
                }
            }

            // 底部操作提示（中文，按键名保留英文）
            const char* tipZH = "W/S 选择    回车 确认    ESC 返回";
            float tipW = 0, tipH = 0;
            cjk.measure(tipZH, 18, tipW, tipH);
            cjk.draw(screenVP, tipZH, 18,
                     glm::vec2(mc.x - tipW * 0.5f, mc.y + mh * 0.5f - 26.0f),
                     glm::vec4(0.50f, 0.55f, 0.62f, 1.0f));
        }

        SDL_GL_SwapWindow(window);
        pulse += dt;
    }

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
