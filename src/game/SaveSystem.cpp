// SaveSystem.cpp
#include "game/SaveSystem.h"
#include "game/Utf8Path.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace echo {
namespace save {

using nlohmann::json;

std::string toJsonText(const MetaState& meta) {
    json j;
    j["version"] = 1;
    j["loops"]   = meta.loops;
    j["deaths"]  = meta.deaths;
    j["escapes"] = meta.escapes;
    j["bestTime"] = meta.bestTime;
    j["knownPasswords"] = meta.knownPasswords;
    j["knownTrapIds"]   = meta.knownTrapIds;
    j["openedDoorIds"]  = meta.openedDoorIds;
    j["knownEnemyIds"]  = meta.knownEnemyIds;
    return j.dump(2);
}

bool fromJsonText(const std::string& text, MetaState& out, std::string* err) {
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        if (err) *err = std::string("save parse error: ") + e.what();
        return false;
    }
    if (!j.is_object()) {
        if (err) *err = "save root must be a JSON object";
        return false;
    }

    MetaState m;
    m.loops   = j.value("loops", 0);
    m.deaths  = j.value("deaths", 0);
    m.escapes = j.value("escapes", 0);
    m.bestTime = static_cast<float>(j.value("bestTime", -1.0));

    // 缺失的字段直接留空，等价于"这个存档还没记过这些事"
    if (j.contains("knownPasswords") && j["knownPasswords"].is_array())
        m.knownPasswords = j["knownPasswords"].get<std::vector<std::string>>();
    if (j.contains("knownTrapIds") && j["knownTrapIds"].is_array())
        m.knownTrapIds = j["knownTrapIds"].get<std::vector<int>>();
    if (j.contains("openedDoorIds") && j["openedDoorIds"].is_array())
        m.openedDoorIds = j["openedDoorIds"].get<std::vector<std::string>>();
    if (j.contains("knownEnemyIds") && j["knownEnemyIds"].is_array())
        m.knownEnemyIds = j["knownEnemyIds"].get<std::vector<int>>();

    out = std::move(m);
    return true;
}

bool fileExists(const std::string& path) {
    std::ifstream in(u8path(path), std::ios::binary);
    return static_cast<bool>(in);
}

bool writeFile(const std::string& path, const MetaState& meta, std::string* err) {
    std::ofstream out(u8path(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        if (err) *err = "cannot write save file: " + path;
        return false;
    }
    out << toJsonText(meta);
    if (!out.good()) {
        if (err) *err = "write failed: " + path;
        return false;
    }
    return true;
}

bool readFile(const std::string& path, MetaState& out, std::string* err) {
    std::ifstream in(u8path(path), std::ios::binary);
    if (!in) {
        if (err) *err = "cannot open save file: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return fromJsonText(ss.str(), out, err);
}

} // namespace save
} // namespace echo
