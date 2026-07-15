#pragma once

#include <cstdint>
#include <vector>
#include "Platform.h"
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <any>
#include <map>
#include <regex>
#include "rapidjson/writer.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include <shared_mutex>
#include "LoggerService.h"
#include "ClientDTO.h"

namespace Memory
{

    static uint64_t base_addr = 0;
    static uint64_t ScanOffset = 0;
    static uint64_t RegionStart = 0;

    ////////////////// Scanner.cpp //////////////////

	uint64_t PatternScan(std::vector<int> signature, uint64_t baseAddr, int region = 0, uint64_t regionOffset = 0, bool Multiple = false, bool multipleRegions = false, uint64_t regionMaxOffset = 0);
    uint64_t TryPatternScan(std::vector<int> signature, uint64_t baseAddr, int region = 0, uint64_t regionOffset = 0, bool Multiple = false, bool multipleRegions = false, uint64_t regionMaxOffset = 0, int retries = 0, std::string flagName = "");
	std::vector<uint64_t> PatternScanMultiple(std::vector<int> signature, uint64_t baseAddr, int region = 0, uint64_t regionOffset = 0, bool multipleRegions = false, uint64_t regionMaxOffset = 0, int expectedValues = 0);
    uint64_t findRegionBaseAddress(uint64_t baseAddr, int region);


    ////////////////// ReadCemu.cpp //////////////////


    typedef void* (*memory_getBaseType)();
    uint64_t getBaseAddress();

    DWORD read_memory(uint64_t Addr, const char* caller = "");
    int swap_Endian(int number);
    std::vector<BYTE> read_bytes(uint64_t Addr, int bytes = 1, const char* caller = "");
    float read_bigEndianFloat(uint64_t Addr, const char* caller = "");
    int read_bigEndian4Bytes(uint64_t Addr, const char* caller = "");
    int read_bigEndian4BytesOffset(uint64_t Addr, const char* caller = "");
    std::string read_string(uint64_t Addr, int bytes = 50, const char* caller = "");
    void write_bigEndianFloat(uint64_t Addr, float value, const char* caller = "");
    void write_bigEndian4Bytes(uint64_t Addr, int value, const char* caller = "");
    void write_byte(uint64_t Addr, BYTE byte, const char* caller = "");
    void write_bytes(uint64_t Addr, std::vector<BYTE> bytes, const char* caller = "");
    void write_string(uint64_t Addr, std::string string, int bytes = 0, const char* caller = "");
    std::vector<BYTE> getNop(int length);
    std::string hexStr(std::vector<BYTE> data);
    std::string extractLocName(uint64_t Addr, int bytes = 50);
    bool CompareSignatures(std::vector<BYTE> First, std::vector<BYTE> Second, std::vector<int> WildCards = {  } );
    uint64_t ReadPointers(uint64_t InitialAddress, std::vector<int> readingOffsets, bool IncludeBaseAddress = false);
    bool TryReadBigEndian4BytesOffset(uint64_t Offset, uint32_t& Value, std::string* FailureReason = nullptr);
    bool TryReadPointers(uint64_t InitialAddress, const std::vector<int>& readingOffsets, uint64_t& Result,
        bool IncludeBaseAddress = false, std::string* FailureReason = nullptr);
    void ValidateAddress(uint64_t address);


    ////////////////// CharacterClasses.cpp //////////////////

    class World_class
    {
    public:

        float WorldTime = 0;
        int WorldDay = 0;
        
        uint64_t timeAddr;
        uint64_t dayAddr;

        void UpdateTime();
        void SetWorldTime(float serverTime, int serverDay);

        std::string GetTime();
        std::string GetDay();
        std::string to_string_precision(float number, int precision);

    };

    class Link_class
    {
    public:
        float Position[3];
        float Speed[3] = { 0, 0, 0 };
        float Rotation;
        int Animation;
        int Health;
        float atkup;
        int def;
        int IsEquipped;

        std::string Map;
        std::string Section;
        std::string Town;
        std::vector<std::string> Equipment;
        std::map<std::string, std::string> lastEquipment;
        rapidjson::Document WeaponDamages;
        uint64_t PosAddr;
        uint64_t RotAddr;
        uint64_t SpdAddr;
        uint64_t HealthAddr;
        uint64_t AnimAddr;
        uint64_t locationAddr;
        uint64_t townAddr;
        uint64_t porchAddr;
        uint64_t equippedAddr;
        uint64_t atkupAddr;
        uint64_t defAddr;
        uint64_t EqAddr;
        DWORD lastHealthUpdate;
        DWORD lastHealthReduce;

        void UpdatePosition();
        void UpdateRotation();
        void UpdateSpeed();
        void UpdateAnimation();
        void UpdateLocation();
        int UpdateHealth();
        void UpdateAtkDef();
        std::vector<uint64_t> ScanData();
        int UpdateData();
        void UpdatePlayerEquipment();
        void UpdateIsEquipped();
        void reduceHealth(int healthToReduce);
        rapidjson::Document readWeaponDamages();

        std::map<std::string, std::string> GetPosition();
        std::string GetRotation();
        std::string GetAnimation();
        std::map<std::string, std::string> GetSpeed();
        std::map<std::string, std::string> GetLocation();
        std::string GetHealth();
        std::map<std::string, std::string> GetEquipment();
        std::string GetAtkUp();
        std::string GetDef();
        std::string GetIsEquipped();
        std::string GetDataString();
        std::string to_string_precision(float number, int precision);
        void Teleport(float newPosition[3]);

    };

    class Bomb
    {

    public:

        std::string BombType;
        uint64_t BaseAddress;
        float Pos[3];
        int BombIndex;
        bool Exploded;
        bool Exists;

        //Bomb(std::string bombType, float initialPos[3]);
        void SetData(std::string bombType, float initialPos[3]);
        void UpdatePos(float newPosition[3]);

    private:
        uint64_t FindPosAddr();
        bool RequestCreate();

    };


    class OtherPlayer_class
    {

    public:
        std::shared_mutex WeaponChangeMutex;
        bool WeaponChanged;
        unsigned int BaseAddress;
        uint64_t LeftHandWeapon;
        uint64_t RightHandWeapon;
        std::vector<uint64_t> ArmorAddresses = {0, 0, 0, 0 ,0};
        float Pos[3];
        float Rot[3];
        int isEquipped;
        uint64_t PosAddr;
        uint64_t RotAddr;
        uint64_t GlyphAddr;
        uint64_t baseAnimationAddr;
        uint64_t deleteAddr;
        uint64_t existsAddr;
        uint64_t isEqAddr;
        uint64_t NameAddr;
        uint64_t DisplayNameAddr;
        int counter;
        bool firstTime = TRUE;
        int playerNumber;
        bool glyphSync = FALSE;
        std::map<std::string, Bomb*> UserBombs = { {"RemoteBomb", new Bomb()}, {"RemoteBomb2", new Bomb()}, {"RemoteBombCube", new Bomb()}, {"RemoteBombCube2", new Bomb()}};
        std::vector<int> multiplier_animation = { 1, 12, 23, 34, 45, 56, 67, 78, 88, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 79, 80, 81, 82, 83, 84, 85, 86, 87 };
        std::vector<int> multiplier_schedule = { 1, 8, 9, 10, 11, 12, 13, 14, 15, 2, 3, 4, 5, 6, 7 };
        uint64_t lastSchedule = 0;
        uint64_t lastAnimation = 0;
        bool doAnimationSync = true;
        bool exists = false;
        std::map<std::string, std::string> Equipment;
        DWORD attackTimer;
        float AttkUp;
        int Health;
        std::string Name;

        std::vector<DWORD*> PosPoint = {0, 0, 0};

        OtherPlayer_class(int PlayerNumber);
        uint64_t FindPosAddr();
        void ClearPointers();
        void FindWeapons();
        void FindArmor();
        void ClearWeapons();
        void SetWeapons();
        void SetArmor();
        bool checkExists();
        int spawnAction(std::vector<std::string> local, std::vector<std::string> server, bool shouldExist, bool isPrint);
        void setGlyphAddr(uint64_t GAddr);
        uint64_t ScanData(uint64_t offset);
        void setGlyph(float Position[3]);
        void setPosition(float Position[3]);
        void setRotation(float Rotation);
        void setAnimation(int Schedule, int Animation);
        std::vector<uint64_t> findWeaponAddr();
        void deleteActor();
        void changeData(rapidjson::Value& playerData, float time, float coords[]);
        bool changeEq(rapidjson::Value& playerData);
        void setIsEquipped(int IsEquipped);
        void restartAnimations();
        void changeName(std::string Name, int DisplayName);

    };

    ////////////////// QuestSync.cpp //////////////////

    class Quests_class
    {

        struct Quest
        {
            std::string Type;
            std::string Name;
            uint64_t Address;
            BYTE Value;
            bool beingChanged = false;
            bool changed = false;

            void updateValue()
            {
                if (beingChanged) return;

                BYTE newValue = Memory::read_bytes(this->Address, 1, __FUNCTION__)[0];

                if (Type == "L" && Value == 0 && newValue > 0x00)
                    changed = true;
                else if (newValue != Value && (newValue == 0x1 || newValue == 0x17 || newValue == 0x03))
                    changed = true;

                if (beingChanged) return;

                Value = newValue;
            }
        };

    public:
        std::map<std::string, Quest> QuestList;
        std::map<std::string, int> numberOfQuests;
        std::vector<std::string> changedQuests;
        int totalQuests = 0;
        std::vector<std::string> questsToChange;
        uint64_t addingKorokAddress;
        uint64_t addingBoolAddress;
        uint64_t addingIntAddress;
        uint64_t addingItemAddress;
        uint64_t IsGetPlayerStole2Address;
        uint64_t DungeonClearCounterAddress;
        int koroksToAdd = 0;
        uint64_t boolFlagAddress;
        uint64_t intFlagAddress;
        uint64_t itemFlagAddress;
        std::vector<std::string> boolsToChange;
        std::vector<std::string> intsToChange;
        std::vector<std::string> itemsToAdd;
        std::vector<std::string> serverQuests;
        std::shared_mutex QuestMutex;
        bool (*IsPaused)();

        rapidjson::Document readQuestFlags();
        void scanQuestMemory(std::vector<std::string> QuestsToSync);
        void readQuests();
        void setup(std::vector<std::string> questServerSettings, bool (*isPausedMethod)());
        std::vector<std::string> getChangedQuests();
        DTO::QuestDTO* getQuestDTO();
        bool updateQuests(bool eventStatus);
        void findAddingAddresses(uint64_t offset);
        void changeFlag();
        void findBoolFlagToChange();
        void findIntFlagToChange();
        void findItemFlagToChange();
        void resyncQuests();
        std::string findQuest(std::string QuestName);
    };

    ////////////////// multiplayerQuest.cpp //////////////////

    static uint64_t pingAddress;

    static class MultiplayerQuest
    {
    public:
        static std::vector<uint64_t> findMQuests(uint64_t offset);
        static void startMQuest(std::vector<uint64_t> questsAddr);
        static void changeMQuestSvName(std::string serverName, uint64_t offset);
        static void changeMQuestPing(int ping);
        static void findMQuestPingAddress(uint64_t offset);
        static void changeMultiplayerQuestStatus(uint64_t addr, bool status);
    };

    ////////////////// BombSyncer.cpp //////////////////
    class BombSyncer
    {
    public:
        std::map<std::string, uint64_t> Bombs;
        std::shared_mutex BombMutex;
        std::shared_mutex BombExplodeMutex;

        std::vector<std::vector<uint64_t>> BombAvailableAddresses = { {}, {}, {}, {} };
        std::shared_mutex OtherPlayerBombsMutex;
        std::vector<uint64_t> BombsToExplode = {};
        std::vector<uint64_t> BombsToClear = {};
    
        void FindBombs();
        uint64_t FindBombPosAddr(uint64_t baseAddr);
        std::tuple<std::vector<std::string>, std::vector<std::any>, std::vector<std::string>> GetBombPositions();
        std::string to_string_precision(float number, int precision);
    };

    static uint64_t MessageQuestAddr;
    static uint64_t MessageStringAddr;
    static DWORD LastMessageTime = -1;
    static std::vector<std::string> MessageQueue = {};
    ////////////////// MessagerService.cpp //////////////////
    static class MessagerService
    {
    public:
        static void StartMessagerService();
        static void DisplayMessage();
        static void AddMessage(std::string Message);
    };

}
