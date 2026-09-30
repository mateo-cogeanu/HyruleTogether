#pragma once
// Opt-in diagnostics for the two-client integration harness. No game state writes.
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "Vec3f.h"

namespace TestTelemetry {
inline bool enabled() {
    static const bool value = std::getenv("HYRULE_TEST_TELEMETRY") != nullptr;
    return value;
}
template<class Build> void emit(const char* kind, int slot, Build build) {
    if (!enabled()) return;
    extern std::mutex sinkMutex;
    extern std::ofstream sinkFile;
    extern std::map<std::string, long long> lastSample;
    std::lock_guard<std::mutex> lock(sinkMutex);
    if (!sinkFile.is_open()) sinkFile.open(std::getenv("HYRULE_TEST_TELEMETRY"), std::ios::app);
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string key = std::string(kind) + std::to_string(slot);
    if (now - lastSample[key] < 100) return;
    lastSample[key] = now;
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
    json.StartObject();
    json.Key("time_ms"); json.Int64(now);
    json.Key("kind"); json.String(kind);
    json.Key("slot"); json.Int(slot);
    build(json);
    json.EndObject();
    sinkFile << buffer.GetString() << '\n';
    sinkFile.flush();
}
inline std::mutex sinkMutex;
inline std::ofstream sinkFile;
inline std::map<std::string, long long> lastSample;

template<class Writer> void position(Writer& json, DataTypes::Vec3f p) {
    json.Key("position"); json.StartArray();
    for (int i = 0; i < 3; ++i) {
        if (std::isfinite(p[i])) json.Double(p[i]); else json.Null();
    }
    json.EndArray();
}
template<class Character> void character(const char* kind, int slot, const Character& data) {
    emit(kind, slot, [&](auto& json) {
        position(json, data.Position);
        json.Key("animation"); json.Int(data.Animation);
        json.Key("health"); json.Int(data.Health);
        json.Key("equipment_state"); json.Int(data.EquipmentState);
        json.Key("equipment"); json.StartArray();
        for (int v : {int(data.Equipment.WType), int(data.Equipment.Sword), int(data.Equipment.Shield),
                      int(data.Equipment.Bow), int(data.Equipment.Head), int(data.Equipment.Upper), int(data.Equipment.Lower)}) json.Int(v);
        json.EndArray();
        json.Key("arrow_id"); json.Int(data.Arrow.Id);
        json.Key("arrow_active"); json.Bool(data.Arrow.Active);
        json.Key("map"); json.String(data.Location.Map.c_str());
    });
}
inline void applied(int slot, uint64_t actor, DataTypes::Vec3f p) {
    emit("applied", slot, [&](auto& json) {
        json.Key("actor"); json.Uint64(actor);
        position(json, p);
    });
}
}
