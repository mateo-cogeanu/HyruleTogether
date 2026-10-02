#include "Player.h"
#include "TestTelemetry.h"

using namespace MemoryAccess;

namespace Main
{
	bool IsCemuTitleActive();
}

#ifndef _WIN32
bool Player::ResolveNativeAnimationControls()
{
	if (AnimationControlsResolved.load(std::memory_order_acquire))
		return true;
	std::lock_guard<std::recursive_mutex> animationControlLock(AnimationControlMutex);
	if (AnimationControlsResolved.load(std::memory_order_acquire))
		return true;
	if (ArchiveAnimAddr == 0 || ArchiveAttackAddr == 0 || ArchiveHoldAddr == 0)
		return false;
	if (LastAnimationControlScan != 0 &&
		float(GetTickCount() - LastAnimationControlScan) / 1000.0f < 2.0f)
		return AnimAddr != 0;
	LastAnimationControlScan = GetTickCount();

	const std::string prefix = "Jugador" + std::to_string(PlayerNumber) + "_";
	const std::string normal = prefix + "animationthing";
	const std::string attack = prefix + "AttackAnimation";
	const std::string hold = prefix + "Hold";
	std::vector<int> signature(prefix.begin(), prefix.end());
	auto copies = Memory::PatternScanMultiple(
		signature, Memory::getBaseAddress(), 8, 0, false, 0, 0);

	uint64_t liveNormal = 0;
	uint64_t liveAttack = 0;
	uint64_t liveHold = 0;
	int normalCopies = 0;
	int attackCopies = 0;
	int holdCopies = 0;
	for (uint64_t copy : copies)
	{
		if (Memory::read_string(copy, normal.size(), __FUNCTION__) == normal)
		{
			liveNormal = std::max(liveNormal, copy);
			normalCopies++;
		}
		else if (Memory::read_string(copy, attack.size(), __FUNCTION__) == attack)
		{
			liveAttack = std::max(liveAttack, copy);
			attackCopies++;
		}
		else if (Memory::read_string(copy, hold.size(), __FUNCTION__) == hold)
		{
			liveHold = std::max(liveHold, copy);
			holdCopies++;
		}
	}

	// One copy is the archive payload. A second, higher copy proves that Cemu
	// deserialized the parameter block. The normal placeholder may already have
	// been replaced by EventFlow before this player connects, so use any intact
	// control as an anchor and preserve the archive block's relative offsets.
	bool normalWasMissing = AnimAddr == 0;
	if (normalCopies >= 2)
		AnimAddr = liveNormal;
	if (attackCopies >= 2)
		AttackAddr = liveAttack;
	if (holdCopies >= 2)
		HoldAddr = liveHold;
	auto projectLiveAddress = [](uint64_t liveAnchor, uint64_t archiveAnchor, uint64_t archiveTarget)
	{
		return uint64_t(int64_t(liveAnchor) +
			(int64_t(archiveTarget) - int64_t(archiveAnchor)));
	};
	if (attackCopies >= 2)
	{
		AnimAddr = projectLiveAddress(liveAttack, ArchiveAttackAddr, ArchiveAnimAddr);
		HoldAddr = projectLiveAddress(liveAttack, ArchiveAttackAddr, ArchiveHoldAddr);
	}
	else if (holdCopies >= 2)
	{
		AnimAddr = projectLiveAddress(liveHold, ArchiveHoldAddr, ArchiveAnimAddr);
		AttackAddr = projectLiveAddress(liveHold, ArchiveHoldAddr, ArchiveAttackAddr);
	}
	else if (normalCopies >= 2)
	{
		AttackAddr = projectLiveAddress(liveNormal, ArchiveAnimAddr, ArchiveAttackAddr);
		HoldAddr = projectLiveAddress(liveNormal, ArchiveAnimAddr, ArchiveHoldAddr);
	}

	bool allRequiredControls = AnimAddr != 0 && AttackAddr != 0;
	AnimationControlsResolved.store(allRequiredControls, std::memory_order_release);
	if (normalWasMissing && AnimAddr != 0)
	{
		std::stringstream stream;
		stream << "Resolved live player " << PlayerNumber
			<< " EventFlow controls: normal=0x" << std::hex << AnimAddr
			<< ", attack=0x" << AttackAddr << ", hold=0x" << HoldAddr << ".";
		Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
	}
	else if (!AnimationControlScanLogged)
	{
		std::stringstream stream;
		stream << "Waiting for live player " << PlayerNumber
			<< " EventFlow controls (normal copies=" << normalCopies
			<< ", attack copies=" << attackCopies
			<< ", hold copies=" << holdCopies << ").";
		Logging::LoggerService::LogDebug(stream.str(), __FUNCTION__);
		AnimationControlScanLogged = true;
	}
	return AnimAddr != 0;
}
#endif

void Player::PThread()
{
	float FunctionTime = 0; // In milliseconds
	DWORD LastSpawn = 0;
	const int SPAWN_LIMITER = 3;
	bool failedToDelete = false;
	DWORD LastEquipmentRetry = 0;
	bool spawnWaitLogged = false;

	std::stringstream startStream;
	startStream << "Player " << this->PlayerNumber << " thread starting...";
	Logging::LoggerService::LogInformation(startStream.str(), __FUNCTION__);

	while (RunThread.load(std::memory_order_acquire) && Main::IsCemuTitleActive())
	{
		try 
		{
			if (!connected || isFar) {
				Sleep(100);
				continue;
			}

#ifndef _WIN32
			// EventFlow may deserialize the controls before the actor callback.
			// Resolve eagerly when possible, but do not delay the spawn request if
			// the live copies do not exist yet.
			bool animationControlsReady = ResolveNativeAnimationControls();
#endif

			DWORD TimerStart = GetTickCount();

			ActionEnum action = ActSkip;

			bool shouldExist = false;

			CharacterLocation LocalLocation = GameInstance->Location->LastKnown;

			std::string reasonToSpawn = "";

			if (LocalLocation.Map == this->Location->Map && (LocalLocation.Map == "MainField" || LocalLocation.Section == this->Location->Section) && !this->isFar)
			{
				shouldExist = true;
			}

			bool exists = this->baseAddr != 0;

			// Equipment strings are applied to the existing actor. Recreating it
			// here depended on a dead delete EventFlow, produced duplicate actors,
			// and discarded the live animation controller we need to drive.
			if (exists && this->Equipment->SetupFailed &&
				!this->EquipmentRefreshPending.load(std::memory_order_acquire) &&
				(LastEquipmentRetry == 0 ||
				 float(GetTickCount() - LastEquipmentRetry) / 1000.0f >= 1.0f))
			{
				LastEquipmentRetry = GetTickCount();
				if (this->baseAddr != 0 &&
					!this->EquipmentRefreshPending.load(std::memory_order_acquire))
				{
					this->Equipment->SetWeapons(this->baseAddr);
					if (this->Model.ModelType == 0)
						this->Equipment->SetArmor();
					else if (this->Model.ModelType == 1)
						this->Equipment->SetModel(this->Model.Model);
				}
			}

			if (failedToDelete)
			{
				action = ActCreate;
				reasonToSpawn += "Failed to delete. ";
				failedToDelete = false;
			}
			
			if (!exists && shouldExist)
			{
				action = ActCreate;
				reasonToSpawn += "Actor should exist.";
			}

			if (exists && !shouldExist)
				action = ActDelete;

			if (this->SpawnPending.load(std::memory_order_acquire))
			{
				action = ActSkip;
				// Queuing and asynchronous creation can both outlast ten seconds.
				// A timeout does not cancel BOTW's original request; resubmitting it
				// creates an unpaired actor when both callbacks eventually arrive.
				if (!spawnWaitLogged && GetTickCount() - this->SpawnRequestedAt > 10000)
				{
					spawnWaitLogged = true;
					Logging::LoggerService::LogWarning(
						"Remote actor creation is delayed; awaiting the original request without resubmitting.",
						__FUNCTION__);
				}
			}
			else
				spawnWaitLogged = false;

			if (action == ActDelete)
			{
				Logging::LoggerService::LogDebug("Selected action: Delete", __FUNCTION__);
				//this->Delete->set(true, __FUNCTION__);
				this->Status->set(Player::DELETE_STATUS, __FUNCTION__);

				while (RunThread.load(std::memory_order_acquire) &&
					Main::IsCemuTitleActive() && this->baseAddr != 0)
					Sleep(50);
				if (!RunThread.load(std::memory_order_acquire) || !Main::IsCemuTitleActive())
					break;

				//this->Delete->set(false, __FUNCTION__);
				this->Status->set(0, __FUNCTION__);

				continue;
			}

			if (action == ActCreate)
			{
				if (LastSpawn != 0 && float(GetTickCount() - LastSpawn) / 1000 < SPAWN_LIMITER)
				{
					Sleep(300);
					continue;
				}

				Logging::LoggerService::LogDebug("Selected action: Create. Reason: " + reasonToSpawn, __FUNCTION__);

				if(GameInstance->IsPaused())
				{
					LastSpawn = GetTickCount();
					Logging::LoggerService::LogWarning("Create request failed: Game is paused.");
					continue;
				}

				if (this->baseAddr != 0)
					this->Status->set(Player::DELETE_STATUS, __FUNCTION__);
					//this->Delete->set(true, __FUNCTION__);

				DWORD deleteTime = GetTickCount();

				while (RunThread.load(std::memory_order_acquire) && Main::IsCemuTitleActive() &&
					(this->baseAddr != 0 || GameInstance->IsPaused()))
				{
					if (GameInstance->IsPaused())
						deleteTime = GetTickCount();

					// If it's been more than 5 seconds since you tried to delete, we can assume the actor is deleted
					if (float(GetTickCount() - deleteTime) / 1000 > 5)
					{
						this->setAddress(0);
						std::stringstream stream;
						stream << "Could not delete player " << this->PlayerNumber << ". Old actor could still be visible in the world.";
						Logging::LoggerService::LogWarning(stream.str(), __FUNCTION__);
						failedToDelete = true;
						this->Status->set(0, __FUNCTION__);
						LastSpawn = GetTickCount();
						continue;
					}

					Sleep(100);
				}
				if (!RunThread.load(std::memory_order_acquire) || !Main::IsCemuTitleActive())
					break;

				//this->Delete->set(false, __FUNCTION__);
				this->Status->set(0, __FUNCTION__);

				if (GameInstance->IsPaused())
				{
					LastSpawn = GetTickCount();
					Logging::LoggerService::LogWarning("Create request failed: Game is paused.");
					continue;
				}

				if (this->Model.ModelType == 0)
				{
					// Armor and equipment addresses belong to a live actor. The
					// OnActorCreate callback stages them before the controlled reload.
				}
				else if (this->Model.ModelType == 1)
				{
					Logging::LoggerService::LogDebug("Setting up " + std::to_string(this->PlayerNumber) + " model...", __FUNCTION__);
					//this->Bumii->WriteMiiData(1033785125, 598924800);
					this->Equipment->SetModel(this->Model.Model);
				}
				else
				{
					Logging::LoggerService::LogDebug("Setting up " + std::to_string(this->PlayerNumber) + " bumii...", __FUNCTION__);
					this->Bumii->WriteMiiData();
				}

				if (GameInstance->IsPaused())
				{
					LastSpawn = GetTickCount();
					Logging::LoggerService::LogWarning("Create request failed: Game is paused.");
					continue;
				}

				// An asynchronous OnActorCreate callback may arrive after this loop
				// selected ActCreate but before the request is submitted. Let that
				// callback win instead of queuing a duplicate synthetic actor.
				if (this->baseAddr != 0)
				{
					this->SpawnPending.store(false, std::memory_order_release);
					Logging::LoggerService::LogDebug(
						"Cancelled stale create decision because the replacement actor already exists.",
						__FUNCTION__);
					continue;
				}

				this->SpawnRequestedAt = GetTickCount();
				this->SpawnCallbackExpected.store(false, std::memory_order_release);
				this->SpawnPending.store(true, std::memory_order_release);
				GameInstance->RequestCreate(this->PlayerNumber, this->LastServerPosition);
				this->Exists->set(false, __FUNCTION__);

				LastSpawn = GetTickCount();

				continue;
			}

			if (!Main::IsCemuTitleActive())
				break;

#ifndef _WIN32
			if (!animationControlsReady)
			{
				Sleep(250);
				continue;
			}
#endif

			if (this->baseAddr == 0)
			{
				Sleep(100);
				continue;
			}

			const float updateRate = this->LowLatency ? 120.0f : 60.0f;
			const float frameTime = 1000.0f / updateRate;
			this->Teleport(Helper::Extrapolation::Next(this->Position->LastKnown, this->Speed, (frameTime - FunctionTime) / 1000.0f));
			if (TestTelemetry::enabled())
			{
				TestTelemetry::applied(this->PlayerNumber, this->baseAddr, this->Position->LastKnown);
				TestTelemetry::emit("npc_equipment", this->PlayerNumber, [&](auto& json) {
					json.Key("actor"); json.Uint64(this->baseAddr);
					uint32_t state = 0, vtable = 0, getter = 0;
					if (Memory::TryReadBigEndian4BytesOffset(this->baseAddr + 0xb94, state))
					{ json.Key("state"); json.Uint(state); }
					if (Memory::TryReadBigEndian4BytesOffset(this->baseAddr + 0xe8, vtable) &&
						vtable && Memory::TryReadBigEndian4BytesOffset(vtable + 0x314, getter))
					{ json.Key("getter"); json.Uint(getter); }
					// Verified v208 NPC getter: addi r3,r3,0x930; blr.
					// Each 16-byte entry contains the actor-link data and attachment flags.
					if (getter == 0x0202eaac)
					{
						json.Key("children"); json.StartArray();
						for (uint32_t slot = 0; slot < 3; ++slot)
						{
							json.StartArray();
							for (uint32_t field = 0; field < 16; field += 4)
							{
								uint32_t value = 0;
								if (Memory::TryReadBigEndian4BytesOffset(
									this->baseAddr + 0x930 + slot * 16 + field, value))
									json.Uint(value);
								else json.Null();
							}
							json.EndArray();
						}
						json.EndArray();
					}
					json.Key("transition_words"); json.StartArray();
					for (uint32_t offset = 0xb98; offset <= 0xbb8; offset += 4)
					{
						uint32_t value = 0;
						if (Memory::TryReadBigEndian4BytesOffset(this->baseAddr + offset, value))
							json.Uint(value);
						else json.Null();
					}
					json.EndArray();
				});
			}
			this->Bomb->reset();
			this->Bomb2->reset();
			this->BombCube->reset();
			this->BombCube2->reset();

			FunctionTime = float(GetTickCount() - TimerStart);

			if (frameTime - FunctionTime > 0)
				Sleep(static_cast<DWORD>(frameTime - FunctionTime));
		}
		catch (const std::exception& ex)
		{
			std::stringstream stream;
			stream << "Exception thrown: " << ex.what();
			Logging::LoggerService::LogError(stream.str(), __FUNCTION__);
			exit(1);
		}
		catch (...)
		{
			Logging::LoggerService::LogError("Catched unknown exception.", __FUNCTION__);
			exit(1);
		}
	}
	RunThread.store(false, std::memory_order_release);

	std::stringstream endStream;
	endStream << "Player " << this->PlayerNumber << " thread stopping...";
	Logging::LoggerService::LogInformation(endStream.str(), __FUNCTION__);
}
