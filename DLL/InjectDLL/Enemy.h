#pragma once
#include "Vec3fBE.h"
#include "Vec3f_Operations.h"
#include "EnemyCombat.h"

namespace DataTypes
{
	class Enemy
	{

	public:

		uint64_t BaseAddress;
		BigEndian<int>* Health;
		Vec3fBE* PrevPos;
		int CurrentHealth = -1;
        EnemyCombat Combat;
		~Enemy() { delete Health; delete PrevPos; }
		std::string EnemyType;
		bool IsSpawned = false;
		bool IsUpdated = false;
		bool IsSetup = false;

		Enemy(uint64_t baseAddress)
		{
			Health = new BigEndian<int>(0, __FUNCTION__);
			PrevPos = new Vec3fBE(0, __FUNCTION__);
			SetAddress(baseAddress);
		}

		void SetHealth(int newHealth) 
		{
            if (EnemyDamageDeltas) Combat.receive(newHealth);
			if (newHealth >= 0 && (CurrentHealth == -1 || newHealth < CurrentHealth))
				CurrentHealth = newHealth;
		}

		int GetHealth(const char* caller)
		{
			if (!IsSpawned || !IsSetup) return CurrentHealth;
			int MemoryHealth = this->Health->get(__FUNCTION__);
			if (MemoryHealth < 0) return CurrentHealth;
            if (EnemyDamageDeltas) {
                const int effective = Combat.synchronize(MemoryHealth, [&](int& expected, int desired) {
                    auto encode = [](int value) {
                        const uint32_t bits = static_cast<uint32_t>(value);
                        const uint8_t bytes[] = {uint8_t(bits >> 24), uint8_t(bits >> 16), uint8_t(bits >> 8), uint8_t(bits)};
                        uint32_t encoded; memcpy(&encoded, bytes, 4); return encoded;
                    };
                    uint32_t encodedExpected = encode(expected);
                    const uint32_t encodedDesired = encode(desired);
                    auto* healthWord = reinterpret_cast<uint32_t*>(BaseAddress + 0x540);
#ifdef _WIN32
                    const uint32_t actual = InterlockedCompareExchange(
                        reinterpret_cast<volatile LONG*>(healthWord), encodedDesired, encodedExpected);
                    const bool applied = actual == encodedExpected;
                    encodedExpected = actual;
#else
                    const bool applied = __atomic_compare_exchange_n(healthWord, &encodedExpected,
                        encodedDesired, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
#endif
                    const auto* bytes = reinterpret_cast<const uint8_t*>(&encodedExpected);
                    const uint32_t bits = (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
                        (uint32_t(bytes[2]) << 8) | bytes[3];
                    memcpy(&expected, &bits, 4);
                    return applied;
                });
                if (effective < 0) return CurrentHealth;
                CurrentHealth = effective;
                IsUpdated = Combat.pending();
                return effective;
            }

			if (MemoryHealth > CurrentHealth && CurrentHealth != -1)
			{
				this->Health->set(CurrentHealth, __FUNCTION__);
				MemoryHealth = CurrentHealth;
			}
			else if (CurrentHealth != MemoryHealth)
			{
				CurrentHealth = MemoryHealth;
				IsUpdated = true;
			}

			return MemoryHealth;
		}

		void SetAddress(uint64_t baseAddress)
		{

			if (baseAddress == 0)
			{
				// The erase callback may run after health storage is invalid.
				BaseAddress = 0;
				//this->Health = new BigEndian<int>(0, __FUNCTION__);
				//this->PrevPos = new Vec3fBE(0, __FUNCTION__);
				this->Health->setAddress(0, __FUNCTION__, false);
				this->PrevPos->setAddress(0, __FUNCTION__, false);
				IsSpawned = false;
				this->IsSetup = false;
			}
			else 
			{
				BaseAddress = baseAddress;
				IsSpawned = true;
				this->Health->setAddress(baseAddress + 0x540, __FUNCTION__);
				this->PrevPos->setAddress(baseAddress + 0x28C, __FUNCTION__);
				this->EnemyType = Memory::read_string(baseAddress + 0x10, 100, __FUNCTION__);
			}
		}

		bool GetSetup()
		{
			if (!IsSpawned || BaseAddress == 0) return false;
			if (this->IsSetup)
				return true;

			this->IsSetup = !Helper::Vec3f_Operations::Equals(this->PrevPos->get(__FUNCTION__), Vec3f());

			return this->IsSetup;
		}
	};
}
