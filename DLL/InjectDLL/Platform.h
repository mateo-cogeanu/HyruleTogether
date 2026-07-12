#pragma once

// The original client was written as an injected Win32 DLL.  Keep the game
// code platform agnostic by providing the small subset of Win32 types and
// functions it uses when building as a Unix shared library.

#ifdef _WIN32
#define NOMINMAX
#define _WINSOCKAPI_
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <direct.h>
#include <tchar.h>
#else
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

using BYTE = std::uint8_t;
using byte = std::uint8_t;
using DWORD = std::uint32_t;
using BOOL = int;
using TCHAR = char;
using LPVOID = void*;
using LPCVOID = const void*;
using HMODULE = void*;
using HANDLE = std::intptr_t;
using SOCKET = int;
using SOCKADDR = struct sockaddr;
using SOCKADDR_IN = struct sockaddr_in;
using LPCWSTR = const wchar_t*;
using UINT32 = std::uint32_t;
using LPTHREAD_START_ROUTINE = DWORD (*)(LPVOID);

struct WSADATA {};
struct SYSTEM_INFO { void* lpMinimumApplicationAddress; void* lpMaximumApplicationAddress; };
struct MEMORY_BASIC_INFORMATION {
    void* BaseAddress{};
    std::size_t RegionSize{};
    DWORD State{};
    DWORD Protect{};
    DWORD Type{};
};

#ifndef TRUE
constexpr BOOL TRUE = 1;
#endif
#ifndef FALSE
constexpr BOOL FALSE = 0;
#endif
constexpr HANDLE INVALID_HANDLE_VALUE = -1;
constexpr SOCKET INVALID_SOCKET = -1;
constexpr DWORD MEM_COMMIT = 0x1000;
constexpr DWORD MEM_FREE = 0x10000;
constexpr DWORD MEM_PRIVATE = 0x20000;
constexpr DWORD MEM_MAPPED = 0x40000;
constexpr DWORD PAGE_NOACCESS = 0x01;
constexpr DWORD PAGE_GUARD = 0x100;
constexpr DWORD PAGE_NOCACHE = 0x200;
constexpr int GENERIC_ALL = 0;
constexpr int OPEN_EXISTING = 0;
constexpr DWORD PIPE_READMODE_MESSAGE = 0;
constexpr DWORD DLL_PROCESS_DETACH = 0;
constexpr DWORD DLL_PROCESS_ATTACH = 1;
constexpr DWORD DLL_THREAD_ATTACH = 2;
constexpr DWORD DLL_THREAD_DETACH = 3;
constexpr unsigned MB_OK = 0;

#ifndef __stdcall
#define __stdcall
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef __FUNCTION__
#define __FUNCTION__ __func__
#endif

inline DWORD GetTickCount() {
    using namespace std::chrono;
    return static_cast<DWORD>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

inline void Sleep(DWORD milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

inline HANDLE CreateThread(void*, std::size_t, LPTHREAD_START_ROUTINE routine,
                           LPVOID parameter, DWORD, DWORD*) {
    try {
        std::thread([routine, parameter] { routine(parameter); }).detach();
        return 1;
    } catch (...) {
        return 0;
    }
}

inline BOOL CloseHandle(HANDLE) { return TRUE; }
inline void FreeLibraryAndExitThread(HMODULE, DWORD code) { pthread_exit(reinterpret_cast<void*>(code)); }

inline HMODULE GetModuleHandle(const char*) { return RTLD_DEFAULT; }
inline HMODULE GetModuleHandleA(const char*) { return RTLD_DEFAULT; }
inline void* GetProcAddress(HMODULE, const char* symbol) { return dlsym(RTLD_DEFAULT, symbol); }

inline void GetSystemInfo(SYSTEM_INFO* info) {
    info->lpMinimumApplicationAddress = reinterpret_cast<void*>(0x1000);
#if INTPTR_MAX == INT64_MAX
    info->lpMaximumApplicationAddress = reinterpret_cast<void*>(0x0000ffffffffffffULL);
#else
    info->lpMaximumApplicationAddress = reinterpret_cast<void*>(0xfffffff0UL);
#endif
}

inline int MessageBoxW(void*, LPCWSTR message, LPCWSTR, unsigned) {
    std::fwprintf(stderr, L"Milk Bar: %ls\n", message ? message : L"Unknown error");
    return 0;
}

namespace MilkBarPlatform {
struct MemoryRegion {
    std::uintptr_t start{};
    std::uintptr_t end{};
    bool readable{};
    bool mapped{};
};

#ifdef __linux__
inline std::vector<MemoryRegion> ReadMemoryMap() {
    std::vector<MemoryRegion> regions;
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        unsigned long long start = 0, end = 0;
        char permissions[5]{};
        if (std::sscanf(line.c_str(), "%llx-%llx %4s", &start, &end, permissions) == 3)
            regions.push_back({static_cast<std::uintptr_t>(start), static_cast<std::uintptr_t>(end), permissions[0] == 'r', true});
    }
    return regions;
}
#endif
} // namespace MilkBarPlatform

inline std::size_t VirtualQuery(LPCVOID address, MEMORY_BASIC_INFORMATION* result, std::size_t) {
    const auto requested = reinterpret_cast<std::uintptr_t>(address);
#ifdef __linux__
    const auto regions = MilkBarPlatform::ReadMemoryMap();
    for (const auto& region : regions) {
        if (requested >= region.start && requested < region.end) {
            result->BaseAddress = reinterpret_cast<void*>(region.start);
            result->RegionSize = region.end - region.start;
            result->State = MEM_COMMIT;
            result->Protect = region.readable ? 0 : PAGE_NOACCESS;
            result->Type = MEM_MAPPED;
            return sizeof(*result);
        }
        if (requested < region.start) {
            result->BaseAddress = const_cast<void*>(address);
            result->RegionSize = region.start - requested;
            result->State = MEM_FREE;
            result->Protect = PAGE_NOACCESS;
            result->Type = 0;
            return sizeof(*result);
        }
    }
#elif defined(__APPLE__)
    mach_vm_address_t regionAddress = requested;
    mach_vm_size_t regionSize = 0;
    natural_t depth = 0;
    vm_region_submap_info_data_64_t info{};
    mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
    const kern_return_t status = mach_vm_region_recurse(mach_task_self(), &regionAddress,
        &regionSize, &depth, reinterpret_cast<vm_region_recurse_info_t>(&info), &count);
    if (status == KERN_SUCCESS) {
        if (regionAddress > requested) {
            result->BaseAddress = const_cast<void*>(address);
            result->RegionSize = regionAddress - requested;
            result->State = MEM_FREE;
            result->Protect = PAGE_NOACCESS;
            result->Type = 0;
        } else {
            result->BaseAddress = reinterpret_cast<void*>(regionAddress);
            result->RegionSize = regionSize;
            result->State = MEM_COMMIT;
            result->Protect = (info.protection & VM_PROT_READ) ? 0 : PAGE_NOACCESS;
            result->Type = info.is_submap ? MEM_MAPPED : MEM_PRIVATE;
        }
        return sizeof(*result);
    }
#endif
    return 0;
}

inline int WSAStartup(int, WSADATA*) { return 0; }
inline int WSACleanup() { return 0; }
inline int closesocket(SOCKET socketFd) { return ::close(socketFd); }
#define MAKEWORD(a, b) 0

inline BOOL ReadFile(HANDLE handle, void* buffer, DWORD length, DWORD* readCount, void*) {
    const auto count = ::read(static_cast<int>(handle), buffer, length);
    if (readCount) *readCount = count > 0 ? static_cast<DWORD>(count) : 0;
    return count > 0;
}

inline BOOL WriteFile(HANDLE handle, const void* buffer, DWORD length, DWORD* writtenCount, void*) {
    const auto count = ::write(static_cast<int>(handle), buffer, length);
    if (writtenCount) *writtenCount = count > 0 ? static_cast<DWORD>(count) : 0;
    return count == static_cast<ssize_t>(length);
}

inline BOOL SetNamedPipeHandleState(HANDLE, DWORD*, void*, void*) { return TRUE; }

inline std::string MilkBarDataDirectory() {
    if (const char* overridePath = std::getenv("MILKBAR_DATA_DIR")) return overridePath;
#ifdef __APPLE__
    if (const char* home = std::getenv("HOME")) return std::string(home) + "/Library/Application Support/MilkBarLauncher";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) return std::string(xdg) + "/MilkBarLauncher";
    if (const char* home = std::getenv("HOME")) return std::string(home) + "/.local/share/MilkBarLauncher";
#endif
    return ".";
}

inline int _mkdir(const char* path) { return ::mkdir(path, 0755); }
inline int localtime_s(std::tm* output, const std::time_t* input) { return localtime_r(input, output) ? 0 : 1; }
inline int freopen_s(FILE** output, const char* filename, const char* mode, FILE* stream) {
    *output = std::freopen(filename, mode, stream);
    return *output ? 0 : 1;
}
inline int _dupenv_s(char** output, std::size_t* size, const char* name) {
    std::string value;
    if (std::strcmp(name, "APPDATA") == 0) value = MilkBarDataDirectory();
    else if (const char* environment = std::getenv(name)) value = environment;
    *output = static_cast<char*>(std::malloc(value.size() + 1));
    if (!*output) return 1;
    std::memcpy(*output, value.c_str(), value.size() + 1);
    if (size) *size = value.size() + 1;
    return 0;
}

template <std::size_t N>
inline int strcpy_s(char (&destination)[N], const char* source) {
    std::snprintf(destination, N, "%s", source ? source : "");
    return 0;
}
template <std::size_t N>
inline int strcat_s(char (&destination)[N], const char* source) {
    const auto used = std::strlen(destination);
    if (used < N) std::snprintf(destination + used, N - used, "%s", source ? source : "");
    return 0;
}
#endif
