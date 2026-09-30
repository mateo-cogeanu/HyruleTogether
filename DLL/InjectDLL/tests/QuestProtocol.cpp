// Compile the production serializer without starting the injected client.
#include "Serialization.h"
#include "LoggerService.h"
#include <cassert>
#include <iostream>
namespace Logging {
void LoggerService::LogInformation(std::string, const char*) {}
}
int main() {
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
    std::cout << "Native short and heap-backed quest IDs match the wire format.\n";
}
