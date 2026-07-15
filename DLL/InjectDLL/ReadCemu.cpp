#include "Memory.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
    void SetPointerFailure(std::string* failureReason, const std::string& reason)
    {
        if (failureReason != nullptr)
            *failureReason = reason;
    }

    std::string HexAddress(uint64_t address)
    {
        std::stringstream stream;
        stream << "0x" << std::hex << address;
        return stream.str();
    }
}

uint64_t Memory::getBaseAddress()
{
    if (base_addr == 0)
    {
        Memory::memory_getBaseType memory_getBase = (Memory::memory_getBaseType)GetProcAddress(GetModuleHandle("Cemu.exe"), "memory_getBase");
        if (memory_getBase)
            base_addr = (uint64_t)memory_getBase();
    }

    return base_addr;
}

DWORD Memory::read_memory(uint64_t Addr, const char* caller)
{
    return *(DWORD*)(Addr);
}

void Memory::ValidateAddress(uint64_t address)
{

    if (address < base_addr)
        throw std::invalid_argument("");

}

int Memory::swap_Endian(int number)
{
    int byte0, byte1, byte2, byte3;
    byte0 = (number & 0x000000FF) >> 0;
    byte1 = (number & 0x0000FF00) >> 8;
    byte2 = (number & 0x00FF0000) >> 16;
    byte3 = (number & 0xFF000000) >> 24;
    return ((byte0 << 24) | (byte1 << 16) | (byte2 << 8) | (byte3 << 0));
}

std::vector<BYTE> Memory::read_bytes(uint64_t Addr, int bytes, const char* caller)
{
    
    try
    {

        std::vector<BYTE> bytesRead;

        for (int i = 0; i < bytes; i++)
        {
            bytesRead.push_back(*(BYTE*)(Addr + i));
        }

        return bytesRead;
    }
    catch (...)
    {

        char Message[100];

        strcpy_s(Message, caller);
        strcat_s(Message, " -> ");
        strcat_s(Message, __FUNCTION__);

        Logging::LoggerService::LogError("Failed to read bytes.", Message);

        exit(1);
    }

}

float Memory::read_bigEndianFloat(uint64_t Addr, const char* caller)
{
    try
    {
        float bigf;
        DWORD result = *(DWORD*)(Addr);
        int swapped_value = Memory::swap_Endian(int(result));
        memcpy(&bigf, &swapped_value, 4);
        return bigf;
    }
    catch (...)
    {

        char Message[100];

        strcpy_s(Message, caller);
        strcat_s(Message, " -> ");
        strcat_s(Message, __FUNCTION__);

        Logging::LoggerService::LogError("Failed to read float.", Message);

        exit(1);
    }
}

int Memory::read_bigEndian4Bytes(uint64_t Addr, const char* caller)
{
    try 
    {
        return Memory::swap_Endian(int(*(DWORD*)(Addr)));
    }
    catch (...)
    {

        char Message[100];

        strcpy_s(Message, caller);
        strcat_s(Message, " -> ");
        strcat_s(Message, __FUNCTION__);

        Logging::LoggerService::LogError("Failed to read 4 bytes.", Message);

        exit(1);
    }
}

int Memory::read_bigEndian4BytesOffset(uint64_t Offset, const char* caller)
{
    try
    {
        return Memory::swap_Endian(int(*(DWORD*)(Offset + getBaseAddress())));
    }
    catch (...)
    {

        char Message[100];

        strcpy_s(Message, caller);
        strcat_s(Message, " -> ");
        strcat_s(Message, __FUNCTION__);

        Logging::LoggerService::LogError("Failed to read 4 bytes.", Message);

        exit(1);
    }
}

std::string Memory::read_string(uint64_t Addr, int bytes, const char* caller)
{
    try
    {

        std::vector<BYTE> bytesRead = Memory::read_bytes(Addr, bytes, __FUNCTION__);

    std::vector<BYTE>::iterator endString = std::find(bytesRead.begin(), bytesRead.end(), 0x00);

        if (endString != bytesRead.end())
        {
            bytesRead.erase(endString, bytesRead.end());
        }

        std::string stringRead(bytesRead.begin(), bytesRead.end());

        return stringRead;
    }
    catch (...)
    {

        char Message[100];

        strcpy_s(Message, caller);
        strcat_s(Message, " -> ");
        strcat_s(Message, __FUNCTION__);

        Logging::LoggerService::LogError("Failed to read string.", Message);

        exit(1);
    }

}

void Memory::write_bigEndianFloat(uint64_t Addr, float value, const char* caller)
{
    try
    {

        int val_int;
        memcpy(&val_int, &value, 4);
        *(DWORD*)(Addr) = (DWORD)Memory::swap_Endian(val_int);
    }
    catch (...)
    {

        char Message[100];

        strcpy_s(Message, caller);
        strcat_s(Message, " -> ");
        strcat_s(Message, __FUNCTION__);

        Logging::LoggerService::LogError("Failed to write float.", Message);

        exit(1);
    }

}

void Memory::write_bigEndian4Bytes(uint64_t Addr, int value, const char* caller)
{
    try
    {
        *(DWORD*)(Addr) = (DWORD)Memory::swap_Endian(value);
    }
    catch (...)
    {

        char Message[100];

        strcpy_s(Message, caller);
        strcat_s(Message, " -> ");
        strcat_s(Message, __FUNCTION__);

        Logging::LoggerService::LogError("Failed to write 4 bytes.", Message);

        exit(1);
    }

}

void Memory::write_byte(uint64_t Addr, BYTE byte, const char* caller)
{
    try
    {
        *(BYTE*)(Addr) = byte;
    }
    catch (...)
    {

        char Message[100];

        strcpy_s(Message, caller);
        strcat_s(Message, " -> ");
        strcat_s(Message, __FUNCTION__);

        Logging::LoggerService::LogError("Failed to write byte.", Message);

        exit(1);
    }

}

void Memory::write_bytes(uint64_t Addr, std::vector<BYTE> bytes, const char* caller)
{
    try
    {
        for (int i = 0; i < bytes.size(); i++)
        {
            *(BYTE*)(Addr + i) = bytes[i];
        }
    }
    catch (...)
    {

        char Message[100];

        strcpy_s(Message, caller);
        strcat_s(Message, " -> ");
        strcat_s(Message, __FUNCTION__);

        Logging::LoggerService::LogError("Failed to write bytes.", Message);

        exit(1);
    }

}

void Memory::write_string(uint64_t Addr, std::string string, int bytes, const char* caller)
{
    try
    {
        if (bytes == 0)
            bytes = string.size();

        if (bytes != 0)
        {
            for (int i = 0; i < bytes; i++)
            {
                BYTE byteToWrite = 0x00;

                if (i < string.size())
                    byteToWrite = string[i];

                *(BYTE*)(Addr + i) = byteToWrite;
            }
        }
    }
    catch (...)
    {

        char Message[100];

        strcpy_s(Message, caller);
        strcat_s(Message, " -> ");
        strcat_s(Message, __FUNCTION__);

        Logging::LoggerService::LogError("Failed to write string.", Message);

        exit(1);
    }

}

std::vector<BYTE> Memory::getNop(int length)
{
    std::vector<BYTE> result;

    for (int i = 0; i < length; i++)
    {
        result.push_back(0x90);
    }

    return result;
}

std::string Memory::hexStr(std::vector<BYTE> data)
{
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < data.size(); i++)
    {
        ss << std::setw(2) << std::setfill('0') << (int)data[i];
    }

    return ss.str();
}

std::string Memory::extractLocName(uint64_t Addr, int bytes)
{
    std::string locString = Memory::read_string(Addr, bytes, __FUNCTION__);
    std::string result = "";

    for (int i = 0; i < locString.size(); i++)
    {
        if (locString[i] == ' ')
        {
            if (i == 0)
            {
                result = " ";
            }

            break;
        }

        result += locString[i];
    }

    return result;
}

bool Memory::CompareSignatures(std::vector<BYTE> First, std::vector<BYTE> Second, std::vector<int> WildCards)
{
    for (int i = 0; i < First.size(); i++)
    {
        if (std::find(WildCards.begin(), WildCards.end(), i) != WildCards.end()) continue;

        if (First[i] != Second[i]) return false;
    }

    return true;
}

uint64_t Memory::ReadPointers(uint64_t InitialAddress, std::vector<int> readingOffsets, bool IncludeBaseAddress)
{
    uint64_t response = InitialAddress;

    for (int i = 0; i < readingOffsets.size(); i++)
    {
        try 
        {
            response = read_bigEndian4BytesOffset(response + readingOffsets[i], __FUNCTION__);
            if (response == 0) return 0;
        }
        catch (...)
        {
            std::cout << "[WRN] Couldn't read pointer at position " << i << " tried to read offset " << std::hex << response + readingOffsets[i] << std::endl;
            return 0;
        }
    }

    return response + (IncludeBaseAddress ? getBaseAddress() : 0);
}

bool Memory::TryReadBigEndian4BytesOffset(uint64_t Offset, uint32_t& Value, std::string* FailureReason)
{
    Value = 0;

    // Wii U pointers are unsigned 32-bit emulated addresses. The legacy reader
    // returns int, which sign-extends pointers at or above 0x80000000 when they
    // are assigned to uint64_t and can turn the next link into a host SIGSEGV.
    constexpr uint64_t PPC_ADDRESS_MAX = std::numeric_limits<uint32_t>::max();
    if (Offset > PPC_ADDRESS_MAX - sizeof(uint32_t) + 1)
    {
        SetPointerFailure(FailureReason, "PPC read offset " + HexAddress(Offset) + " is outside the 32-bit address space");
        return false;
    }

    const uint64_t memoryBase = getBaseAddress();
    if (memoryBase == 0 || memoryBase > std::numeric_limits<uintptr_t>::max() - Offset)
    {
        SetPointerFailure(FailureReason, "Cemu emulated-memory base is unavailable");
        return false;
    }

    const uintptr_t hostAddress = static_cast<uintptr_t>(memoryBase + Offset);
    MEMORY_BASIC_INFORMATION memoryInfo{};
    const DWORD rejectedProtection = PAGE_GUARD | PAGE_NOCACHE | PAGE_NOACCESS;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(hostAddress), &memoryInfo, sizeof(memoryInfo)) == 0 ||
        !(memoryInfo.State & MEM_COMMIT) || (memoryInfo.Protect & rejectedProtection))
    {
        SetPointerFailure(FailureReason, "PPC read offset " + HexAddress(Offset) + " is not backed by readable host memory");
        return false;
    }

    const uintptr_t regionStart = reinterpret_cast<uintptr_t>(memoryInfo.BaseAddress);
    if (hostAddress < regionStart || memoryInfo.RegionSize < sizeof(uint32_t) ||
        hostAddress - regionStart > memoryInfo.RegionSize - sizeof(uint32_t))
    {
        SetPointerFailure(FailureReason, "PPC read offset " + HexAddress(Offset) + " crosses a host memory-region boundary");
        return false;
    }

    std::array<uint8_t, sizeof(uint32_t)> bytes{};
    memcpy(bytes.data(), reinterpret_cast<const void*>(hostAddress), bytes.size());
    Value = (static_cast<uint32_t>(bytes[0]) << 24) |
        (static_cast<uint32_t>(bytes[1]) << 16) |
        (static_cast<uint32_t>(bytes[2]) << 8) |
        static_cast<uint32_t>(bytes[3]);
    return true;
}

bool Memory::TryReadPointers(uint64_t InitialAddress, const std::vector<int>& readingOffsets, uint64_t& Result,
    bool IncludeBaseAddress, std::string* FailureReason)
{
    Result = 0;
    uint64_t response = InitialAddress;

    for (size_t i = 0; i < readingOffsets.size(); ++i)
    {
        const int64_t signedOffset = readingOffsets[i];
        uint64_t readOffset = response;
        if (signedOffset < 0)
        {
            const uint64_t magnitude = static_cast<uint64_t>(-signedOffset);
            if (readOffset < magnitude)
            {
                SetPointerFailure(FailureReason, "pointer link " + std::to_string(i) + " underflowed its signed offset");
                return false;
            }
            readOffset -= magnitude;
        }
        else
        {
            const uint64_t magnitude = static_cast<uint64_t>(signedOffset);
            if (readOffset > std::numeric_limits<uint64_t>::max() - magnitude)
            {
                SetPointerFailure(FailureReason, "pointer link " + std::to_string(i) + " overflowed its signed offset");
                return false;
            }
            readOffset += magnitude;
        }

        uint32_t pointerValue = 0;
        std::string readFailure;
        if (!TryReadBigEndian4BytesOffset(readOffset, pointerValue, FailureReason == nullptr ? nullptr : &readFailure))
        {
            if (FailureReason != nullptr)
                *FailureReason = "pointer link " + std::to_string(i) + " failed: " + readFailure;
            return false;
        }
        if (pointerValue == 0)
        {
            SetPointerFailure(FailureReason,
                "pointer link " + std::to_string(i) + " is null after reading " + HexAddress(readOffset));
            return false;
        }

        // Preserve the pointer's unsigned PPC ABI value instead of sign
        // extending it through the legacy int return type.
        response = static_cast<uint64_t>(pointerValue);
    }

    if (IncludeBaseAddress)
    {
        const uint64_t memoryBase = getBaseAddress();
        if (memoryBase == 0 || memoryBase > std::numeric_limits<uint64_t>::max() - response)
        {
            SetPointerFailure(FailureReason, "resolved pointer cannot be converted to a host address");
            return false;
        }
        response += memoryBase;
    }

    Result = response;
    if (FailureReason != nullptr)
        FailureReason->clear();
    return true;
}
