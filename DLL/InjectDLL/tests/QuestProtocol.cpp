// Compile the production serializer without starting the injected client.
#include "Serialization.h"
#include "LoggerService.h"
#include "ActorSpawnParams.h"
#include "SharedItems.h"
#include "EnemyCombat.h"
#include "RemoteAnimationQueue.h"
#include <cassert>
#include <iostream>
namespace Logging {
void LoggerService::LogInformation(std::string, const char*) {}
}
namespace {
std::vector<BYTE> questMemory(4);
}
namespace Memory {
std::vector<BYTE> read_bytes(uint64_t address, int size, const char*) {
    return {questMemory.begin() + address, questMemory.begin() + address + size};
}
int read_bigEndian4Bytes(uint64_t address, const char*) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value = (value << 8) | questMemory.at(address+i);
    return static_cast<int>(value);
}
}
int main() {
    Memory::Quests_class questService;
    auto &discovery = questService.QuestList["L0"];
    discovery.Type = "L"; discovery.Address = 3;
    questMemory = {0,0,1,0}; // Visited value 256, whose last byte is zero.
    discovery.updateValue();
    assert(discovery.Value == 256 && discovery.changed);
    discovery.changed = false; discovery.updateValue();
    assert(!discovery.changed);
    auto &categoryFlag = questService.QuestList["K0"];
    categoryFlag.Type = "K"; categoryFlag.Address = 3;
    questMemory[3] = 0x16; categoryFlag.updateValue();
    assert(!categoryFlag.changed);
    questMemory[3] = 0x17; categoryFlag.updateValue();
    assert(categoryFlag.changed);

    std::vector<QueueAnimation> animations;
    std::map<int, CompletedAnimation> completed{{1, {0x1000, 10}}};
    queueAnimationUpdate(animations, completed, {1, 0x1000, 20});
    queueAnimationUpdate(animations, completed, {1, 0x1000, 10});
    assert(animations.size() == 1 && animations.front().animation == 10);
    animations.clear();
    beginAnimationDispatch(completed, 1); // AS 20 is in flight.
    queueAnimationUpdate(animations, completed, {1, 0x1000, 10});
    assert(animations.size() == 1 && animations.front().animation == 10);
    completed[1] = {0x1000, 20}; // AS 20 returns; AS 10 must still run.
    assert(animations.front().animation != completed.at(1).animation);
    animations.clear();
    completed[1] = {0x1000, 10};
    queueAnimationUpdate(animations, completed, {1, 0x1000, 10});
    assert(animations.empty());
    queueAnimationUpdate(animations, completed, {1, 0x2000, 10});
    queueAnimationUpdate(animations, completed, {2, 0x3000, 30});
    assert(animations.size() == 2 && animations.front().actorAddress == 0x2000);

    DataTypes::EnemyCombat combat;
    assert(combat.observe(100) == 100 && combat.takeUpdate() == 100);
    combat.receive(80);
    assert(combat.observe(100) == 80 && !combat.pending()); // Remote hit does not echo.
    assert(combat.observe(70) == 70 && combat.pending());
    assert(combat.takeUpdate() == std::numeric_limits<int>::min() + 10);
    combat.receive(60);
    assert(combat.observe(65) == 60); // Local 5 damage arrives beside a remote hit.
    assert(combat.takeUpdate() == std::numeric_limits<int>::min() + 5);
    assert(combat.observe(60) == 60 && !combat.pending());
    combat.receive(90); // Stale reply cannot heal.
    assert(combat.observe(60) == 60);
    assert(combat.observe(-1) == -1 && !combat.pending());
    DataTypes::EnemyCombat interrupted;
    interrupted.observe(100); interrupted.takeUpdate(); interrupted.receive(80);
    int memoryHealth = 90, attempts = 0;
    assert(interrupted.synchronize(memoryHealth, [&](int& expected, int desired) {
        if (++attempts == 1) memoryHealth = 85; // Another local hit wins the first write race.
        if (expected != memoryHealth) { expected = memoryHealth; return false; }
        memoryHealth = desired; return true;
    }) == 80);
    assert(attempts == 2 && memoryHealth == 80);
    assert(interrupted.takeUpdate() == std::numeric_limits<int>::min() + 15);
    assert(interrupted.observe(80) == 80 && !interrupted.pending());
    std::vector<uint8_t> params = {0, 0, 0, 1, 0, 0, 0, 7, 0xd0,
                                  0, 0, 0, 2, 3, 0};
    const auto keys = [](uint32_t p) { return p == 1 ? "Life" : p == 2 ? "IsPlayerPut" : ""; };
    std::vector<ActorSpawnParams::Entry> entries;
    assert(ActorSpawnParams::decode(params, 2, keys, entries));
    assert(entries.size() == 2 && entries[0].key == "Life" && entries[0].value[2] == 7);
    assert(!ActorSpawnParams::decode(params, 1, keys, entries) && entries.empty());
    auto truncated = params; truncated.pop_back();
    assert(!ActorSpawnParams::decode(truncated, 2, keys, entries));
    auto invalid = params; invalid[4] = 8;
    assert(!ActorSpawnParams::decode(invalid, 2, keys, entries));
    invalid = params; invalid[3] = 3;
    assert(!ActorSpawnParams::decode(invalid, 2, keys, entries));
    invalid = {0, 0, 0, 1, 5, 'x'};
    assert(!ActorSpawnParams::decode(invalid, 1, keys, entries));
    invalid = {0, 0, 0, 1, 6, 0, 0, 0, 0};
    assert(!ActorSpawnParams::decode(invalid, 1, keys, entries));
    assert(ActorSpawnParams::decode(invalid, 1, [](uint32_t) { return "@D"; }, entries));
    assert(entries[0].value.size() == 4); // Callback pointer, never a network-safe value.
    assert(!ActorSpawnParams::decode(std::vector<uint8_t>(193), 1, keys, entries));
    SharedItems::Item item;
    item.id = "test-1"; item.name = "Weapon_Spear_030";
    item.owner = 0; item.map = "MainField"; item.section = "D-6";
    std::vector<uint8_t> matrix(48, 0);
    matrix[12] = 0x44; matrix[13] = 0x7a; // World X = 1000.
    item.params = {{"IsPlayerPut", 3, {0}}, {"Life", 0, {0,0,7,0xd0}}, {"@M", 7, matrix}};
    assert(SharedItems::validate(item));
    DataTypes::CharacterEquipment equipment{};
    equipment.WType = 1; equipment.Sword = 4; equipment.Shield = 41;
    assert(SharedItems::equippedResourceMatches("Weapon_Sword_004", equipment));
    assert(SharedItems::equippedResourceMatches("Weapon_Shield_041", equipment));
    assert(!SharedItems::equippedResourceMatches("Weapon_Spear_004", equipment));
    assert(!SharedItems::equippedResourceMatches("Weapon_Sword_005", equipment));
    equipment.Sword = 0;
    assert(!SharedItems::equippedResourceMatches("Weapon_Sword_004", equipment));
    auto unsafe = item; unsafe.params.push_back({"@D", 6, {0,0,0,0}});
    assert(!SharedItems::validate(unsafe));
    unsafe = item; unsafe.params.push_back({"@I", 0, {0,0,0,3}});
    assert(!SharedItems::validate(unsafe)); // Local carry initialization never crosses clients.
    unsafe = item; unsafe.params.push_back(item.params[0]);
    assert(!SharedItems::validate(unsafe));
    unsafe = item; unsafe.params[2].value[0] = 0x7f; unsafe.params[2].value[1] = 0x80;
    assert(!SharedItems::validate(unsafe));
    rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartArray(); SharedItems::writeJson(writer, item); writer.EndArray();
    SharedItems::reset();
    SharedItems::incoming(buffer.GetString(), 1);
    SharedItems::incoming(buffer.GetString(), 1);
    assert(SharedItems::spawnQueue.size() == 1 && SharedItems::items.size() == 1);
    assert(SharedItems::outgoing() == "[]"); // Receiving must never echo a spawn.
    SharedItems::incoming("[{\"Id\":\"test-1\",\"Owner\":0,\"Removed\":true}]", 1);
    SharedItems::incoming(buffer.GetString(), 1);
    assert(SharedItems::items.at("test-1").removed); // A delayed spawn cannot resurrect it.
    SharedItems::reset();
    SharedItems::incoming(buffer.GetString(), 0); // Reused owner slot after reconnect.
    assert(SharedItems::spawnQueue.size() == 1 && SharedItems::items.at("test-1").remote);
    // Pickup overtakes the server's first creation acknowledgement.
    auto& pickedUp = SharedItems::items.at("test-1");
    pickedUp.removed = true;
    pickedUp.sent = false;
    SharedItems::incoming(buffer.GetString(), 0);
    assert(!pickedUp.sent && SharedItems::outgoing() != "[]");
    SharedItems::incoming("[{\"Id\":\"test-1\",\"Owner\":0,\"Removed\":true}]", 0);
    assert(pickedUp.sent && SharedItems::outgoing() == "[]");
    SharedItems::reset();
    // A newer pose updates the existing replica, not its actor identity.
    auto encodeItem = [](const SharedItems::Item &value) {
        rapidjson::StringBuffer b; rapidjson::Writer<rapidjson::StringBuffer> w(b);
        w.StartArray(); SharedItems::writeJson(w, value); w.EndArray();
        return std::string(b.GetString());
    };
    SharedItems::incoming(encodeItem(item), 1);
    SharedItems::items.at(item.id).actor = 123;
    auto moved = item; moved.revision = 2; moved.params[2].value[13] = 0x7b;
    SharedItems::incoming(encodeItem(moved), 1);
    assert(SharedItems::items.at(item.id).revision == 2);
    assert(SharedItems::items.at(item.id).actor == 123 && SharedItems::spawnQueue.size() == 1);
    SharedItems::incoming(encodeItem(item), 1);
    assert(SharedItems::items.at(item.id).params[2].value == moved.params[2].value);
    SharedItems::reset();
    moved.remote = false; moved.sent = false;
    SharedItems::items.emplace(moved.id, moved);
    SharedItems::incoming(encodeItem(item), 0);
    assert(!SharedItems::items.at(item.id).sent);
    SharedItems::incoming(encodeItem(moved), 0);
    assert(SharedItems::items.at(item.id).sent);
    SharedItems::items.at(item.id).removed = true;
    SharedItems::items.at(item.id).sent = false;
    SharedItems::incoming(encodeItem(moved), 0);
    assert(!SharedItems::items.at(item.id).sent);
    SharedItems::reset();
    auto lateReplica = item; lateReplica.removed = true;
    SharedItems::bindActor(lateReplica, 456);
    assert(lateReplica.actor == 456 && lateReplica.deleting);
    assert(SharedItems::deleteQueue.size() == 1 && SharedItems::deleteQueue.front() == item.id);
    SharedItems::reset();
    auto another = item; another.id = "test-2";
    SharedItems::items.emplace(item.id, item);
    SharedItems::items.emplace(another.id, another);
    assert(SharedItems::outgoing() == encodeItem(item));
    assert(SharedItems::outgoing() == encodeItem(another));
    assert(SharedItems::outgoing() == encodeItem(item)); // Both still need an ACK.
    SharedItems::reset();
    DTO::WorldDTO world{};
    DTO::ClientCharacterDTO player{};
    DTO::EnemyDTO enemies{};
    DTO::QuestDTO quests{};
    quests.Completed = {"V0", "V1365", "P1", std::string(64, 'x')};
    DTO::ClientDTO request{&world, &player, &enemies, &quests};
    byte frame[7168]{};
    Serialization::Serializer::SerializeClientData(frame, &request);
    // Fixed wire fields: message byte, world, character, empty enemy count.
    size_t offset = 1 + 12 + 186 + 1;
    assert(frame[offset++] == quests.Completed.size());
    for (const auto& id : quests.Completed) {
        assert(frame[offset++] == id.size());
        assert(std::string(reinterpret_cast<char*>(frame + offset), id.size()) == id);
        offset += id.size();
    }
    assert(frame[offset] == 0); // An old client has no optional tail.
    request.SharedItems = "[]";
    Serialization::Serializer::SerializeClientData(frame, &request);
    assert(frame[offset] == 0x48 && frame[offset + 1] == 0x31);
    assert(frame[offset + 2] == 2 && frame[offset + 3] == 0);
    assert(frame[offset + 4] == '[' && frame[offset + 5] == ']');
    request.SharedItems = std::string(2049, 'x');
    Serialization::Serializer::SerializeClientData(frame, &request);
    assert(frame[offset] == 0); // Oversized extensions cannot overrun the frame.
    byte response[7168]{};
    constexpr size_t tail = 12 + 6 + 15 + 13 + 12 + 15;
    response[tail] = 0x48; response[tail + 1] = 0x31;
    response[tail + 2] = 2; response[tail + 4] = '['; response[tail + 5] = ']';
    assert(Serialization::Serializer::DeserializeServerData(response)->SharedItems == "[]");
    response[tail + 2] = 1; response[tail + 3] = 8; // 2049 exceeds the cap.
    assert(Serialization::Serializer::DeserializeServerData(response)->SharedItems.empty());
    std::cout << "Quest IDs match the wire format; bounded Wii U spawn-param decoding passes.\n";
}
