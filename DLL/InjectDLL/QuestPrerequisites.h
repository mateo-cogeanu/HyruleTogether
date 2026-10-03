#pragma once
#include <array>
#include <cstdint>

// Monotonic quest dependencies present in the owned Wii U v208 QuestProduct
// but absent from the original vanilla flag catalogue. P identities are stable
// additions; existing quest IDs stay unchanged. DLC availability (HasAoCVer*)
// is deliberately local and never synchronized.
namespace QuestPrerequisites {
struct Flag { const char *id, *name; uint32_t hash; };
inline constexpr std::array<Flag, 8> flags{{
    {"P0", "100enemy_Finish", 0xd6b483cc},
    {"P1", "AncientLabo_AncientAssistant001_SSLv2Get", 0x39da26b0},
    {"P2", "CarryingBlueFireEXMini_Repaired", 0x7357b7f4},
    {"P3", "Clear_RemainsElectric", 0x18d100e9},
    {"P4", "Clear_RemainsFire", 0x4007e1ee},
    {"P5", "Clear_RemainsWind", 0x39d2bcdf},
    {"P6", "IsGet_Obj_Camera", 0xf7dd3e03},
    {"P7", "IsPlayed_Demo104_0", 0xb53d1492},
}};
}
