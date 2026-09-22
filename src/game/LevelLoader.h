// LevelLoader.h —— 数据驱动：把 level01.json 读成 LevelData（M2）
//
// 为什么不把关卡写死在代码里？
//   数值外置之后，改关卡不用重新编译；也方便后面做关卡编辑器（导出同一份 JSON）。
//   这是 GDD 第 8 / 13 节「数据驱动 + Prefab 化」的最小落地。
//
// 分成两个函数是为了可测：
//   parseLevelJson() 纯函数，输入字符串，不碰文件系统 —— 单测直接喂 JSON 文本。
//   loadLevelFile()  负责读文件，然后转交 parseLevelJson。
#pragma once

#include <string>

#include "game/GameState.h"

namespace echo {

// 解析关卡 JSON 文本。失败返回 false 并写入 err。
bool parseLevelJson(const std::string& text, LevelData& out, std::string* err = nullptr);

// 从磁盘读取关卡文件。失败返回 false 并写入 err。
bool loadLevelFile(const std::string& path, LevelData& out, std::string* err = nullptr);

} // namespace echo
