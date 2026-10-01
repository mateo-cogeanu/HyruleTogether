#pragma once
#include "Vec3fBE.h"
#include "Enemy.h"
#include "TestTelemetry.h"

namespace MemoryAccess
{

	class EnemyAccess 
	{
		std::map<int, Enemy*> EnemyList;
		std::shared_mutex Mutex;
        DWORD fixtureStarted = 0;
        bool fixtureApplied = false;


	public:
		EnemyAccess()
		{

			EnemyList = {};

		}

		void UpdateEnemyAddress(uint64_t baseAddress, bool created)
		{
			int hash = Memory::read_bigEndian4Bytes(baseAddress + 0x408, __FUNCTION__);
			if (hash == 0)
				return;

			Mutex.lock();

			// Process Enemy to obtain healthAddress and hash;
			if (!EnemyList.count(hash))
				EnemyList[hash] = new Enemy(created ? baseAddress : 0);
			else if (created || EnemyList[hash]->BaseAddress == baseAddress)
				EnemyList[hash]->SetAddress(created ? baseAddress : 0);

			Mutex.unlock();
		}

		void RemoveEnemyFromList(uint64_t baseAddress)
		{
			Mutex.lock();

			for (auto& pair : EnemyList) delete pair.second;
			EnemyList.clear();

			Mutex.unlock();
		}

		void SetServerData(DTO::EnemyDTO* serverData)
		{
			Mutex.lock();

			for (int i = 0; i < serverData->Health.size(); i++)
			{
				DataTypes::EnemyData svEnemy = serverData->Health[i];

				if (EnemyList.count(svEnemy.Hash) == 0)
					EnemyList[svEnemy.Hash] = new Enemy(0);

				EnemyList[svEnemy.Hash]->SetHealth(svEnemy.Health);
			}

			Mutex.unlock();
		}

		DTO::EnemyDTO* UpdateHealth(bool paused)
		{
			DTO::EnemyDTO* result = new DTO::EnemyDTO();
			result->Health = {};
			if (paused) return result;

			Mutex.lock();
            const bool fixture = TestTelemetry::enabled() && std::getenv("HYRULE_TEST_ENEMY_SOURCE");
            if (fixture && !paused && !fixtureStarted) fixtureStarted = GetTickCount();

			for (auto const& pair : EnemyList)
			{
				int LocalHash = pair.first;
				Enemy* LocalEnemy = pair.second;

				if (!LocalEnemy->IsSpawned)
					continue;

				if (!LocalEnemy->GetSetup())
					continue;

				if (fixture && !paused && !fixtureApplied && fixtureStarted &&
                    GetTickCount() - fixtureStarted >= 10000 && LocalHash == -988114952 &&
                    LocalEnemy->Health->get(__FUNCTION__) > 0) {
                    const int before = LocalEnemy->Health->get(__FUNCTION__);
                    const char* configuredDamage = std::getenv("HYRULE_TEST_ENEMY_DAMAGE");
                    const int damage = configuredDamage && std::string(configuredDamage) == "3" ? 3 : 6;
                    const int after = std::max(0, before - damage);
                    LocalEnemy->Health->set(after, __FUNCTION__);
                    fixtureApplied = true;
                    TestTelemetry::emit("enemy_fixture_source", LocalHash, [&](auto& json) {
                        json.Key("before"); json.Int(before); json.Key("after"); json.Int(after);
                        json.Key("damage"); json.Int(damage);
                    }, true);
                }
                const int health = LocalEnemy->GetHealth(__FUNCTION__);
				TestTelemetry::emit("enemy_live", LocalHash, [&](auto& json) {
					json.Key("actor"); json.Uint64(LocalEnemy->BaseAddress);
					json.Key("name"); json.String(LocalEnemy->EnemyType.c_str());
					json.Key("health"); json.Int(health);
					TestTelemetry::position(json, LocalEnemy->PrevPos->get(__FUNCTION__));
				});

				if (!LocalEnemy->IsUpdated || result->Health.size() >= 200)
					continue;
				
				EnemyData EnemyToAdd;
				EnemyToAdd.Hash = LocalHash;
				EnemyToAdd.Health = EnemyDamageDeltas ? LocalEnemy->Combat.takeUpdate() : LocalEnemy->CurrentHealth;
				result->Health.push_back(EnemyToAdd);
				LocalEnemy->IsUpdated = false;
			}

			Mutex.unlock();

			return result;
		}
	};

}
