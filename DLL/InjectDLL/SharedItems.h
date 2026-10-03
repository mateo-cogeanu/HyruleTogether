#pragma once
#include "ActorSpawnParams.h"
#include "CharacterEquipment.h"
#include "Memory.h"
#include "TestTelemetry.h"
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <set>

namespace SharedItems {
struct Item {
    std::string id, name, map, section;
    int owner = -1;
    bool removed = false;
    uint32_t revision = 0, appliedRevision = 0;
    DWORD lastPosePublished = 0, lastPoseApplied = 0, liveSince = 0;
    bool motionFixtureApplied = false;
    std::vector<ActorSpawnParams::Entry> params;
    uint32_t actor = 0;
    DWORD captured = 0;
    bool sent = false, remote = false, deleting = false;
    unsigned attempts = 0;
    bool live = false;
    bool equipmentCandidate = false;
    int deleteReason = -1;
    DataTypes::Vec3f position;
};
inline bool revisionsEnabled = false;
inline std::atomic<bool> worldInitialized{false};
inline std::mutex mutex;
inline std::map<std::string, Item> items;
inline std::deque<Item> captures;
inline std::map<std::string, DWORD> equipmentRequests;
inline std::deque<std::string> spawnQueue, deleteQueue;
inline std::string expecting, localMap, localSection, lastSentId;
inline DWORD expectedSince = 0, lastPaused = 0;
inline DataTypes::Vec3f localPosition;
inline uint32_t creator = 0, heap = 0;
inline unsigned sequence = 0;
inline void reset() {
    std::lock_guard<std::mutex> lock(mutex);
    worldInitialized.store(false, std::memory_order_release);
    items.clear();
    captures.clear();
    equipmentRequests.clear();
    spawnQueue.clear();
    deleteQueue.clear();
    expecting.clear();
    lastSentId.clear();
    localMap.clear();
    localSection.clear();
    creator = heap = 0;
    expectedSince = lastPaused = 0;
}
// Caller holds mutex. A pickup can arrive while the game factory is still
// creating its replica; the late callback must schedule that actor's deletion.
inline void bindActor(Item &item, uint32_t actor) {
    item.actor = actor;
    item.captured = GetTickCount();
    item.attempts = 0;
    item.live = false;
    item.liveSince = item.lastPoseApplied = 0;
    item.appliedRevision = 0;
    item.deleting = item.removed;
    if (item.removed) deleteQueue.push_back(item.id);
}
inline const std::map<std::string, uint8_t> allowed = {
    {"IsPlayerPut", 3}, {"AddParam", 0}, {"AddSpecialFlag", 0}, {"IsWeaponCreateByRawLife", 3},
    {"@RL", 0},         {"@S", 4},       {"Life", 0},           {"@M", 7}};
inline bool equippedResourceMatches(const std::string& name, const DataTypes::CharacterEquipment& equipment) {
    auto matches = [&](const std::string& prefix, unsigned id) {
        return id > 0 && id <= 999 && name == prefix + std::to_string(1000 + id).substr(1);
    };
    const std::string melee = equipment.WType == 1 ? "Weapon_Sword_" :
        equipment.WType == 2 ? "Weapon_Lsword_" : equipment.WType == 3 ? "Weapon_Spear_" : "";
    return (!melee.empty() && matches(melee, equipment.Sword)) ||
        matches("Weapon_Shield_", equipment.Shield) || matches("Weapon_Bow_", equipment.Bow);
}
inline std::string hex(const std::vector<uint8_t> &value) {
    const char *digits = "0123456789abcdef";
    std::string out;
    for (auto v : value) {
        out += digits[v >> 4];
        out += digits[v & 15];
    }
    return out;
}
inline bool unhex(const std::string &text, std::vector<uint8_t> &value) {
    if (text.size() > 96 || text.size() % 2)
        return false;
    auto digit = [](char c) {
        return c >= '0' && c <= '9'   ? c - '0'
               : c >= 'a' && c <= 'f' ? c - 'a' + 10
               : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                      : -1;
    };
    for (size_t i = 0; i < text.size(); i += 2) {
        int a = digit(text[i]), b = digit(text[i + 1]);
        if (a < 0 || b < 0)
            return false;
        value.push_back((a << 4) | b);
    }
    return true;
}
inline bool validate(const Item &item) {
    if (item.id.empty() || item.id.size() > 64 || item.name.size() > 80 || item.map.empty() ||
        item.map.size() > 32 || item.section.size() > 32 ||
        (item.name.rfind("Weapon_", 0) && item.name.rfind("Item_", 0) &&
         item.name != "Obj_FireWoodBundle"))
        return false;
    for (char c : item.id)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-')
            return false;
    for (char c : item.name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
    std::set<std::string> keys;
    size_t size = 0;
    constexpr size_t sizes[] = {4, 4, 4, 1, 12, 0, 4, 48};
    for (const auto &p : item.params) {
        auto found = allowed.find(p.key);
        if (found == allowed.end() || p.type != found->second || !keys.insert(p.key).second ||
            p.value.size() != sizes[p.type])
            return false;
        if (p.type == 3 && p.value[0] > 1)
            return false;
        if (p.type == 2 || p.type == 4 || p.type == 7)
            for (size_t i = 0; i < p.value.size(); i += 4) {
                uint32_t bits = 0;
                for (size_t j = 0; j < 4; ++j)
                    bits = (bits << 8) | p.value[i + j];
                float f;
                memcpy(&f, &bits, 4);
                if (!std::isfinite(f) || std::abs(f) > 1000000)
                    return false;
            }
        if (p.key == "@M") {
            double length = 0;
            for (size_t at : {size_t(12), size_t(28), size_t(44)}) {
                uint32_t bits = 0;
                for (size_t j = 0; j < 4; ++j)
                    bits = (bits << 8) | p.value[at + j];
                float f;
                memcpy(&f, &bits, 4);
                length += double(f) * f;
            }
            if (length < 100 || length > 1e12)
                return false;
        }
        size += 5 + p.value.size();
    }
    return item.params.size() <= 24 && size <= 192 && keys.count("@M") && keys.count("IsPlayerPut");
}
inline void writeJson(rapidjson::Writer<rapidjson::StringBuffer> &w, const Item &item) {
    w.StartObject();
    w.Key("Id");
    w.String(item.id.c_str());
    w.Key("Owner");
    w.Int(item.owner);
    w.Key("Revision");
    w.Uint(item.revision);
    w.Key("Removed");
    w.Bool(item.removed);
    w.Key("Name");
    w.String(item.name.c_str());
    w.Key("Map");
    w.String(item.map.c_str());
    w.Key("Section");
    w.String(item.section.c_str());
    w.Key("Params");
    w.StartArray();
    for (auto &p : item.params) {
        w.StartObject();
        w.Key("Key");
        w.String(p.key.c_str());
        w.Key("Type");
        w.Uint(p.type);
        w.Key("Value");
        w.String(hex(p.value).c_str());
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
}
inline std::string outgoing() {
    std::lock_guard<std::mutex> lock(mutex);
    rapidjson::StringBuffer b;
    rapidjson::Writer<rapidjson::StringBuffer> w(b);
    w.StartArray();
    // A continuously moving early identity must not block later drops or
    // pickup tombstones. Retry all pending identities in round-robin order.
    auto next = items.upper_bound(lastSentId);
    for (size_t count = 0; count < items.size(); ++count) {
        if (next == items.end()) next = items.begin();
        auto &item = next->second;
        ++next;
        if (!item.sent && (!item.remote || item.removed)) {
            writeJson(w, item);
            lastSentId = item.id;
            break;
        }
    }
    w.EndArray();
    return b.GetString();
}
inline void incoming(const std::string &json, int self) {
    rapidjson::Document d;
    d.Parse(json.c_str());
    if (d.HasParseError() || !d.IsArray() || d.Size() > 2)
        return;
    std::lock_guard<std::mutex> lock(mutex);
    for (auto &v : d.GetArray()) {
        if (!v.IsObject() || !v.HasMember("Id") || !v["Id"].IsString() || !v.HasMember("Owner") ||
            !v["Owner"].IsInt() || !v.HasMember("Removed") || !v["Removed"].IsBool())
            continue;
        std::string id = v["Id"].GetString();
        const uint32_t revision = v.HasMember("Revision") && v["Revision"].IsUint()
            ? v["Revision"].GetUint() : 0;
        auto existing = items.find(id);
        if (existing != items.end() && (v["Removed"].GetBool() ||
            existing->second.removed || !existing->second.remote)) {
            auto &item = existing->second;
            if (v["Removed"].GetBool()) {
                item.removed = true;
                if (item.actor && !item.deleting) {
                    deleteQueue.push_back(id);
                    item.deleting = true;
                }
            }
            // Late creation/pose acknowledgements must not retire a newer
            // update, and no pose can acknowledge a pending pickup tombstone.
            item.sent = item.removed ? v["Removed"].GetBool() : revision >= item.revision;
            TestTelemetry::emit("item_acknowledged", -1, [&](auto &w) {
                w.Key("id"); w.String(id.c_str());
                w.Key("removed"); w.Bool(v["Removed"].GetBool());
                w.Key("revision"); w.Uint(revision);
            }, true);
            continue;
        }
        if (v["Removed"].GetBool()) {
            Item tombstone;
            tombstone.id = id;
            tombstone.removed = true;
            tombstone.sent = true;
            items.emplace(id, tombstone);
            continue;
        }
        if (!v.HasMember("Name") || !v["Name"].IsString() || !v.HasMember("Map") ||
            !v["Map"].IsString() || !v.HasMember("Section") || !v["Section"].IsString() ||
            !v.HasMember("Params") || !v["Params"].IsArray())
            continue;
        Item item;
        item.id = id;
        item.owner = v["Owner"].GetInt();
        item.revision = revision;
        item.name = v["Name"].GetString();
        item.map = v["Map"].GetString();
        item.section = v["Section"].GetString();
        // An unknown identity is always a replica, even when a disconnected
        // creator's player slot has since been assigned to this client.
        item.remote = true;
        item.sent = true;
        bool valid = true;
        for (auto &p : v["Params"].GetArray()) {
            if (!p.IsObject() || !p.HasMember("Key") || !p["Key"].IsString() ||
                !p.HasMember("Type") || !p["Type"].IsUint() || p["Type"].GetUint() > 7 ||
                !p.HasMember("Value") || !p["Value"].IsString()) {
                valid = false;
                break;
            }
            ActorSpawnParams::Entry entry{p["Key"].GetString(), uint8_t(p["Type"].GetUint()), {}};
            if (!unhex(p["Value"].GetString(), entry.value)) {
                valid = false;
                break;
            }
            item.params.push_back(entry);
        }
        if (!valid || !validate(item) || (existing == items.end() && items.size() >= 4096))
            continue;
        if (existing != items.end()) {
            auto &current = existing->second;
            if (revision <= current.revision || item.name != current.name ||
                item.map != current.map || item.section != current.section) continue;
            current.params = item.params;
            current.revision = revision;
            continue;
        }
        items.emplace(id, item);
        if (item.remote)
            spawnQueue.push_back(id);
        TestTelemetry::emit(
            "item_received", -1,
            [&](auto &w) {
                w.Key("id");
                w.String(id.c_str());
                w.Key("name");
                w.String(item.name.c_str());
            },
            true);
    }
}
} // namespace SharedItems
