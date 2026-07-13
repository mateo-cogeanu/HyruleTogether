#define _WINSOCKAPI_
#include <vector>
#include "Platform.h"
#include <string>
#include <iostream>
#include "dllmain_Variables.h"
#include "dllmain_Functions.h"
#include "Connectivity.h"
#include "Memory.h"
#include "LoggerService.h"

using namespace Main;

////////////////// Players definitions //////////////////

Memory::Link_class* Main::Link = new Memory::Link_class();
Memory::World_class* Main::World = new Memory::World_class();

Memory::OtherPlayer_class* Main::Jugador1 = new Memory::OtherPlayer_class(1);
Memory::OtherPlayer_class* Main::Jugador2 = new Memory::OtherPlayer_class(2);
Memory::OtherPlayer_class* Main::Jugador3 = new Memory::OtherPlayer_class(3);
Memory::OtherPlayer_class* Main::Jugador4 = new Memory::OtherPlayer_class(4);

Memory::OtherPlayer_class* Main::Jugadores[] = { Jugador1, Jugador2, Jugador3, Jugador4 };

Memory::BombSyncer* Main::BombSync = new Memory::BombSyncer();

////////////////// Parameter initialization //////////////////

// Native Unix builds are preloaded before Cemu reserves the emulated 4 GiB
// address space.  Resolve this lazily once the game starts.
uint64_t Main::baseAddr = 0;

std::vector < std::vector<float> > Main::Jugador1Queue = { {}, {}, {} };
std::vector < std::vector<float> > Main::Jugador2Queue = { {}, {}, {} };
std::vector < std::vector<float> > Main::Jugador3Queue = { {}, {}, {} };
std::vector < std::vector<float> > Main::Jugador4Queue = { {}, {}, {} };
std::vector < std::vector<float> > Main::JugadoresQueues[] = {Jugador1Queue, Jugador2Queue, Jugador3Queue, Jugador4Queue};

Connectivity::namedPipeClass* Main::namedPipe = new Connectivity::namedPipeClass();
Connectivity::Client* Main::client = new Connectivity::Client();

std::shared_mutex WeaponChangeMutex;
std::shared_mutex BombExplodeMutex;

float Main::targetFPS = 1000;
float Main::serializationRate = 20;
float Main::ping = 0;

int Main::playerNumber = 0;
bool Main::IsProp = false;
bool Main::IsPropHuntStopped = true;
bool Main::HidePlayer32 = true;
std::vector<byte> Main::FoundPlayers = {};
std::string Main::serverName = "";
std::vector<bool> Main::ConnectedPlayers = { false, false, false, false };

DWORD Main::t0 = GetTickCount();
DWORD Main::t1 = GetTickCount();

std::string Main::serverData = "";

bool Main::isEnemySync = false;
bool Main::isGlyphSync = false;
bool Main::isQuestSync = false;
bool Main::isHvsSR = false;
bool Main::isDeathSwap = false;

int Main::GlyphUpdateTime = 60;
int Main::GlyphDistance = 250;

std::vector<std::string> Main::questServerSettings;

bool Main::isPaused = true;
std::atomic<DWORD> Main::SpawnHookHeartbeat{0};

std::vector<float> Main::oldLocations[] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

HMODULE myhModule;

bool Main::QuestSyncReady = false;


DWORD __stdcall EjectThread(LPVOID lpParameter) {
    Sleep(100);
    FreeLibraryAndExitThread(myhModule, 0);
    return 0;
}

bool startServerLoop()
{

    FILE* fp;
#ifdef _WIN32
    freopen_s(&fp, "CONOUT$", "w", stdout); // output only
#endif
    std::cout << "Start of the console" << std::endl;

    CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Main::mainServerLoop, 0, 0, 0);
    return true;

}

void readInstruction()
{
    bool started = false;

    while (!started)
    {
        TCHAR chBuff[BUFF_SIZE]{};
        DWORD read = 0;
        bool response = false;

        if (!ReadFile(namedPipe->hPipe, chBuff, (BUFF_SIZE - 1) * sizeof(TCHAR), &read, nullptr)) {
            Logging::LoggerService::LogError("Launcher IPC closed while waiting for an instruction", __FUNCTION__);
            return;
        }
        chBuff[std::min<DWORD>(read, BUFF_SIZE - 1)] = '\0';
        const std::string instruction(chBuff, strnlen(chBuff, BUFF_SIZE));

        if (instruction.find("!connect") != std::string::npos)
        {
            response = Main::connectToServer(instruction);
            if (response)
                Logging::LoggerService::LogInformation("Connected to server successfully");
            else
                Logging::LoggerService::LogError("Server connection failed: " + Main::getConnectionError(), __FUNCTION__);
        }
        else if (instruction.find("!startServerLoop") != std::string::npos)
        {
            Logging::LoggerService::LogInformation("Start server loop requested...");

            if (!started)
            {
                response = true;
                started = true;
            }
            else
            {
                return;
            }
        }

        std::string reply = response ? "Succeeded" : "Failed";
        if (!response && instruction.find("!connect") != std::string::npos && !Main::getConnectionError().empty())
            reply += ": " + Main::getConnectionError();
        DWORD written = 0;
        if (!WriteFile(namedPipe->hPipe, reply.c_str(), static_cast<DWORD>(reply.size() + 1), &written, nullptr)) {
            Logging::LoggerService::LogError("Launcher IPC closed before the instruction reply was delivered", __FUNCTION__);
            return;
        }

    }

    startServerLoop();
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH: {
        //AllocConsole();
        Main::SetupAssemblyPatches();
        using MilkBarHooksReadyType = void (*)();
        if (auto milkbarHooksReady = reinterpret_cast<MilkBarHooksReadyType>(
                GetProcAddress(GetModuleHandleA("Cemu.exe"), "milkbar_markHooksReady")))
            milkbarHooksReady();
        Logging::LoggerService::StartLoggerService();
        namedPipe->createServer();
        CreateThread(0, 0, (LPTHREAD_START_ROUTINE)readInstruction, 0, 0, 0);
        break;
    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

#ifndef _WIN32
// LD_PRELOAD and DYLD_INSERT_LIBRARIES load the client before Cemu enters its
// main loop.  Do as little work as possible in the loader callback and start
// setup on a detached thread, mirroring DLL_PROCESS_ATTACH on Windows.
__attribute__((constructor)) static void MilkBarAttach()
{
    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        // Start logging before resolving Cemu hooks. Previously a bootstrap
        // failure produced no LatestLog.txt and the launcher eventually
        // terminated an otherwise healthy Cemu after its IPC timeout.
        Logging::LoggerService::StartLoggerService();
        Logging::LoggerService::LogInformation("Native client bootstrap started", __FUNCTION__);

        auto memoryGetBase = reinterpret_cast<Memory::memory_getBaseType>(
            GetProcAddress(GetModuleHandleA("Cemu.exe"), "memory_getBase"));
        if (!memoryGetBase) {
            Logging::LoggerService::LogError("Cemu export memory_getBase is unavailable", __FUNCTION__);
            std::fprintf(stderr, "Milk Bar: Cemu export memory_getBase is unavailable\n");
            return 1;
        }
        Logging::LoggerService::LogInformation("Resolved Cemu memory export", __FUNCTION__);
        for (int attempt = 0; attempt < 1200 && Main::baseAddr == 0; ++attempt) {
            Main::baseAddr = reinterpret_cast<uint64_t>(memoryGetBase());
            Sleep(100);
        }
        if (Main::baseAddr == 0) {
            Logging::LoggerService::LogError("Cemu did not initialize emulated memory", __FUNCTION__);
            std::fprintf(stderr, "Milk Bar: Cemu did not initialize emulated memory\n");
            return 1;
        }
        Logging::LoggerService::LogInformation("Resolved Cemu emulated-memory base", __FUNCTION__);
        using MilkBarHLEReadyType = bool (*)();
        auto milkbarHLEReady = reinterpret_cast<MilkBarHLEReadyType>(
            GetProcAddress(GetModuleHandleA("Cemu.exe"), "milkbar_isHLEReady"));
        if (!milkbarHLEReady) {
            Logging::LoggerService::LogError("Cemu export milkbar_isHLEReady is unavailable", __FUNCTION__);
            std::fprintf(stderr, "Milk Bar: Cemu export milkbar_isHLEReady is unavailable\n");
            return 1;
        }
        Logging::LoggerService::LogInformation("Resolved Cemu HLE-readiness export", __FUNCTION__);
        for (int attempt = 0; attempt < 1200 && !milkbarHLEReady(); ++attempt) {
            Sleep(100);
        }
        if (!milkbarHLEReady()) {
            Logging::LoggerService::LogError("Cemu did not finish HLE initialization", __FUNCTION__);
            std::fprintf(stderr, "Milk Bar: Cemu did not finish HLE initialization\n");
            return 1;
        }
        Logging::LoggerService::LogInformation("Cemu HLE initialization is ready", __FUNCTION__);
        try {
            Main::SetupAssemblyPatches();
        } catch (const std::exception& exception) {
            Logging::LoggerService::LogError(
                std::string("HLE hook installation failed: ") + exception.what(), __FUNCTION__);
            std::fprintf(stderr, "Milk Bar: HLE hook installation failed: %s\n", exception.what());
            return 1;
        } catch (...) {
            Logging::LoggerService::LogError("HLE hook installation failed with an unknown exception", __FUNCTION__);
            std::fprintf(stderr, "Milk Bar: HLE hook installation failed\n");
            return 1;
        }
        Logging::LoggerService::LogInformation("Installed native multiplayer HLE hooks", __FUNCTION__);
        // Cemu resumes title startup as soon as the hook-ready flag is set.
        // Create the local instance first so early TimeMgr/actor callbacks do
        // not race the launcher IPC handshake and dereference a null instance.
        Main::PrepareGameInstance();
        Logging::LoggerService::LogInformation("Prepared local game instance for startup callbacks", __FUNCTION__);
        using MilkBarHooksReadyType = void (*)();
        auto milkbarHooksReady = reinterpret_cast<MilkBarHooksReadyType>(
            GetProcAddress(GetModuleHandleA("Cemu.exe"), "milkbar_markHooksReady"));
        if (!milkbarHooksReady) {
            Logging::LoggerService::LogError("Cemu export milkbar_markHooksReady is unavailable", __FUNCTION__);
            std::fprintf(stderr, "Milk Bar: Cemu export milkbar_markHooksReady is unavailable\n");
            return 1;
        }
        milkbarHooksReady();
        Logging::LoggerService::LogInformation("Connecting to launcher IPC", __FUNCTION__);
        try {
            namedPipe->createServer();
            Logging::LoggerService::LogInformation("Connected to launcher IPC", __FUNCTION__);
            readInstruction();
        } catch (const std::exception& exception) {
            Logging::LoggerService::LogError(exception.what(), __FUNCTION__);
        }
        return 0;
    }, nullptr, 0, nullptr);
}
#endif
