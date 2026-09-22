// SaveSystem.h —— Meta 层持久化（M2）
//
// 只存 MetaState。RunState 是"这一轮的临时进度"，死了就该消失，绝不写盘。
// 这条边界本身就是设计的一部分：写盘的东西 = 玩家真正永久获得的东西。
//
// 实现用 nlohmann-json 手写 to_json / from_json，
// 而不是给 MetaState 加成员函数 —— 这样 GameState.h 不会依赖 JSON 库，
// 状态层保持纯净（也更容易单测）。
#pragma once

#include <string>

#include "game/GameState.h"

namespace echo {

namespace save {

// 把 MetaState 序列化成 JSON 文本（缩进 2，方便直接打开看）
std::string toJsonText(const MetaState& meta);

// 从 JSON 文本解析。字段缺失时用默认值（向前兼容旧存档）。失败返回 false。
bool fromJsonText(const std::string& text, MetaState& out, std::string* err = nullptr);

// 落盘 / 读盘
bool writeFile(const std::string& path, const MetaState& meta, std::string* err = nullptr);
bool readFile(const std::string& path, MetaState& out, std::string* err = nullptr);
bool fileExists(const std::string& path);

} // namespace save

} // namespace echo
