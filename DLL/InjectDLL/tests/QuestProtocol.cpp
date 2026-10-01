// Compile the production serializer without starting the injected client.
#include "Serialization.h"
#include "LoggerService.h"
#include "ActorSpawnParams.h"
#include <cassert>
#include <iostream>
namespace Logging {
void LoggerService::LogInformation(std::string, const char*) {}
}
int main() {
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
    DTO::WorldDTO world{};
    DTO::ClientCharacterDTO player{};
    DTO::EnemyDTO enemies{};
    DTO::QuestDTO quests{};
    quests.Completed = {"V0", "V1365", std::string(64, 'x')};
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
    assert(frame[offset] == 0);
    std::cout << "Quest IDs match the wire format; bounded Wii U spawn-param decoding passes.\n";
}
