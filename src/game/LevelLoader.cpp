// LevelLoader.cpp
#include "game/LevelLoader.h"
#include "game/Utf8Path.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace echo {

using nlohmann::json;

namespace {

// 读 [x, y] 形式的数组；缺字段就退回默认值（数据驱动要能容忍字段缺失）
glm::vec2 readVec2(const json& node, const glm::vec2& fallback) {
    if (!node.is_array() || node.size() < 2) return fallback;
    if (!node[0].is_number() || !node[1].is_number()) return fallback;
    return glm::vec2(node[0].get<float>(), node[1].get<float>());
}

Rect readRect(const json& node, const Rect& fallback) {
    Rect r = fallback;
    if (!node.is_object()) return r;
    if (node.contains("center")) r.center = readVec2(node["center"], r.center);
    if (node.contains("size"))   r.size   = readVec2(node["size"], r.size);
    return r;
}

std::string readString(const json& node, const char* key, const std::string& fallback = {}) {
    if (node.contains(key) && node[key].is_string()) return node[key].get<std::string>();
    return fallback;
}

glm::vec4 readColor(const json& node, const glm::vec4& fallback) {
    if (!node.is_array() || node.size() < 3) return fallback;
    glm::vec4 c = fallback;
    c.x = node[0].get<float>();
    c.y = node[1].get<float>();
    c.z = node[2].get<float>();
    c.w = (node.size() >= 4) ? node[3].get<float>() : 1.0f;
    return c;
}

} // namespace

bool parseLevelJson(const std::string& text, LevelData& out, std::string* err) {
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        if (err) *err = std::string("JSON parse error: ") + e.what();
        return false;
    }
    if (!j.is_object()) {
        if (err) *err = "level root must be a JSON object";
        return false;
    }

    LevelData lv;
    lv.name        = readString(j, "name", "untitled");
    lv.loopSeconds = static_cast<float>(j.value("loopSeconds", 20.0));

    if (j.contains("spawn")) lv.spawn = readVec2(j["spawn"], lv.spawn);

    if (j.contains("bounds") && j["bounds"].is_object()) {
        const json& b = j["bounds"];
        if (b.contains("min")) lv.boundsMin = readVec2(b["min"], lv.boundsMin);
        if (b.contains("max")) lv.boundsMax = readVec2(b["max"], lv.boundsMax);
    }
    lv.viewScale = static_cast<float>(j.value("viewScale", 1.0));

    if (j.contains("exit")) lv.exitRect = readRect(j["exit"], lv.exitRect);

    auto readArray = [&](const char* key, auto&& fn) {
        if (!j.contains(key) || !j[key].is_array()) return;
        for (const json& node : j[key]) fn(node);
    };

    readArray("rooms", [&](const json& n) {
        RoomDef r;
        r.id    = readString(n, "id");
        r.label = readString(n, "label");
        r.rect  = readRect(n, r.rect);
        if (n.contains("color")) r.color = readColor(n["color"], r.color);
        lv.rooms.push_back(std::move(r));
    });

    readArray("walls", [&](const json& n) {
        WallDef w;
        w.id   = readString(n, "id");
        w.rect = readRect(n, w.rect);
        lv.walls.push_back(std::move(w));
    });

    int autoTrapId = 1;
    readArray("traps", [&](const json& n) {
        TrapDef t;
        if (n.contains("id") && n["id"].is_number_integer()) {
            t.id = n["id"].get<int>();
        } else {
            t.id = autoTrapId; // 没写 id 就按顺序自动编号
        }
        ++autoTrapId;
        t.label = readString(n, "label");
        t.rect  = readRect(n, t.rect);
        lv.traps.push_back(std::move(t));
    });

    readArray("items", [&](const json& n) {
        ItemDef it;
        it.id      = readString(n, "id");
        it.kind    = readString(n, "kind", "generic");
        it.label   = readString(n, "label");
        it.payload = readString(n, "payload");
        it.rect    = readRect(n, it.rect);
        if (n.contains("color")) it.color = readColor(n["color"], it.color);
        lv.items.push_back(std::move(it));
    });

    readArray("doors", [&](const json& n) {
        DoorDef d;
        d.id           = readString(n, "id");
        d.label        = readString(n, "label");
        d.requiresCode = readString(n, "requires");
        d.rect         = readRect(n, d.rect);
        if (n.contains("color")) d.color = readColor(n["color"], d.color);
        lv.doors.push_back(std::move(d));
    });

    readArray("teleporters", [&](const json& n) {
        TeleporterDef t;
        t.id    = readString(n, "id");
        t.group = readString(n, "group");
        t.label = readString(n, "label", t.group);
        t.rect  = readRect(n, t.rect);
        if (n.contains("color")) t.color = readColor(n["color"], t.color);
        lv.teleporters.push_back(std::move(t));
    });

    int autoEnemyId = 1;
    readArray("enemies", [&](const json& n) {
        EnemyDef e;
        if (n.contains("id") && n["id"].is_number_integer()) {
            e.id = n["id"].get<int>();
        } else {
            e.id = autoEnemyId; // 没写 id 就按顺序自动编号（与 trap 一致）
        }
        ++autoEnemyId;
        e.label = readString(n, "label");
        // 巡逻路点：每个是 [x, y]，无效点直接跳过（不塞世界原点的脏路点）
        if (n.contains("patrol") && n["patrol"].is_array()) {
            for (const json& p : n["patrol"]) {
                if (p.is_array() && p.size() >= 2 &&
                    p[0].is_number() && p[1].is_number()) {
                    e.patrol.push_back(glm::vec2(p[0].get<float>(), p[1].get<float>()));
                }
            }
        }
        e.speed       = static_cast<float>(n.value("speed", e.speed));
        e.vision      = static_cast<float>(n.value("vision", e.vision));
        e.alertSpeed  = static_cast<float>(n.value("alertSpeed", e.alertSpeed));
        e.attackRange = static_cast<float>(n.value("attackRange", e.attackRange));
        if (n.contains("size") && n["size"].is_array())
            e.size = readVec2(n["size"], e.size);
        lv.enemies.push_back(std::move(e));
    });

    // 基本合法性检查：没有出口 / 出口尺寸为 0 的关卡跑不起来
    if (lv.exitRect.size.x <= 0.0f || lv.exitRect.size.y <= 0.0f) {
        if (err) *err = "level has no valid \"exit\" rect";
        return false;
    }

    out = std::move(lv);
    return true;
}

bool loadLevelFile(const std::string& path, LevelData& out, std::string* err) {
    std::ifstream in(u8path(path), std::ios::binary);
    if (!in) {
        if (err) *err = "cannot open level file: " + path;
        return false;
    }

    std::ostringstream ss;
    ss << in.rdbuf();

    if (err) {
        std::string localErr;
        if (!parseLevelJson(ss.str(), out, &localErr)) {
            *err = path + ": " + localErr;
            return false;
        }
        return true;
    }
    return parseLevelJson(ss.str(), out, nullptr);
}

} // namespace echo
