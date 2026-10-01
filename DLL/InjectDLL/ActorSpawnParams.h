#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ActorSpawnParams {
struct Entry {
    std::string key;
    uint8_t type;
    std::vector<uint8_t> value;
};

// Checked Wii U InstParamPack::Buffer layout: big-endian key pointer,
// one-byte type, followed by inline data. Guest pointers stay local.
template<class ReadString>
bool decode(const std::vector<uint8_t>& data, unsigned count, ReadString readString,
            std::vector<Entry>& result)
{
    result.clear();
    if (!count || count > 24 || data.size() > 192) return false;
    size_t offset = 0;
    std::vector<Entry> parsed;
    for (unsigned i = 0; i < count; ++i) {
        if (offset + 5 > data.size()) return false;
        uint32_t key = 0;
        for (unsigned j = 0; j < 4; ++j) key = (key << 8) | data[offset++];
        Entry entry{readString(key), data[offset++], {}};
        if (entry.key.empty() || entry.key.size() > 80 || entry.type > 7) return false;
        // Wii U callbacks (@D) store a four-byte pointer, despite the shared
        // type number being called UInt64 in the Switch reference. Other type
        // 6 values have not been verified and must not be guessed.
        if (entry.type == 6 && entry.key != "@D") return false;
        constexpr size_t sizes[] = {4, 4, 4, 1, 12, 0, 4, 48};
        size_t length = sizes[entry.type];
        if (entry.type == 5) {
            while (offset + length < data.size() && data[offset + length]) ++length;
            ++length; // Require the inline string's terminating zero.
        }
        if (offset + length > data.size()) return false;
        entry.value.assign(data.begin() + offset, data.begin() + offset + length);
        offset += length;
        parsed.push_back(std::move(entry));
    }
    if (offset != data.size()) return false;
    result = std::move(parsed);
    return true;
}
}
