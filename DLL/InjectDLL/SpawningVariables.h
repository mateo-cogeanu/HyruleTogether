#pragma once

#include <shared_mutex>
#include <atomic>
#include <mutex>
#include <limits>
#include "KeyCodeActor.h"
#include "BotwEdit.h"
#include <map>
#include "ActorData.h"
#include "Memory.h"
#include "dllmain_Variables.h"
#include "Entities.h"
#include "Game.h"

// This stuff here was yoinked from BetterVR
// -----------------------------------------
	extern union FPR_t {
		double fpr;
		struct
		{
			double fp0;
			double fp1;
		};
		struct
		{
			uint64_t guint;
		};
		struct
		{
			uint64_t fp0int;
			uint64_t fp1int;
		};
	};

	extern struct PPCInterpreter_t {
		uint32_t instructionPointer;
		uint32_t gpr[32];
		FPR_t fpr[32];
		uint32_t fpscr;
		uint8_t crNew[32]; // 0 -> bit not set, 1 -> bit set (upper 7 bits of each byte must always be zero) (cr0 starts at index 0, cr1 at index 4 ..)
		uint8_t xer_ca;  // carry from xer
		uint8_t LSQE;
		uint8_t PSE;
		// thread remaining cycles
		int32_t remainingCycles; // if this value goes below zero, the next thread is scheduled
		int32_t skippedCycles; // if remainingCycles is set to zero to immediately end thread execution, this value holds the number of skipped cycles
		struct
		{
			uint32_t LR;
			uint32_t CTR;
			uint32_t XER;
			uint32_t UPIR;
			uint32_t UGQR[8];
		}sprNew;
	};
	// -----------------------------------------

	typedef void (*osLib_registerHLEFunctionType)(const char* libraryName, const char* functionName, void(*osFunction)(PPCInterpreter_t* hCPU));


#pragma pack(push, 1)
	extern struct TransferableData { // This is reversed compared to the gfx pack because we read as big endian.
		int f_r10;
		int f_r9;
		int f_r8;
		int f_r7;
		int f_r6;
		int f_r5;
		int f_r4;
		int f_r3;

		int ringPtr;

		int fnAddr;
		int dispatchState;

		// patch_SpawnActors.asm aligns the following .int fields to four
		// bytes after its two one-byte flags.  Because this native structure
		// is stored in reverse order, those implicit PPC bytes live here.
		byte bytepadding[2];
		bool interceptRegisters;

		bool enabled;
	};

	extern struct InstanceData {
		char name[152]; // We'll allocate all unused storage for use for name storage.. just in case of a really long actor name
		uint8_t actorStorage[104];
	};
#pragma pack(pop)

// The PPC assembler aligns SpawnDispatchState to four bytes after Enabled and
// InterceptRegisters. With that dispatch word the storage is 48
// bytes. memory_writeMemoryBE() reverses this complete structure, so the
// native representation must retain the corresponding two padding bytes.
static_assert(sizeof(TransferableData) == 48, "Spawn transfer layout must match patch_SpawnActors.asm");
static_assert(offsetof(TransferableData, fnAddr) == 36, "Spawn function address offset changed");
static_assert(offsetof(TransferableData, dispatchState) == 40, "Spawn dispatch-state offset changed");
static_assert(offsetof(TransferableData, bytepadding) == 44, "Spawn alignment padding changed");
static_assert(offsetof(TransferableData, interceptRegisters) == 46, "Spawn intercept flag offset changed");
static_assert(offsetof(TransferableData, enabled) == 47, "Spawn enabled flag offset changed");
static_assert(sizeof(InstanceData) == 256, "Spawn instance ring entry must remain 256 bytes");

	extern struct QueueActor {
		float PosX;
		float PosY;
		float PosZ;
		std::string Name;
	};

	extern struct QueueAnimation {
		int playerNumber;
		uint32_t actorAddress;
		uint32_t animation;
	};

	extern struct QueueActorDelete {
		int playerNumber;
		uint32_t actorAddress;
	};

	extern struct QueueEquipmentState {
		int playerNumber;
		uint32_t actorAddress;
		bool held;
		DWORD queuedAt;
	};

// ---------------------------------------------------------------------------------
// This is an example function call.
// - Feel free to expand / change -
// Note: This does not take into account stuff like actually setting desired params.
// ---------------------------------------------------------------------------------
std::map<char, std::vector<KeyCodeActor>> keyCodeMap;

std::shared_mutex keycode_mutex;
std::shared_mutex queue_mutex;
std::shared_mutex data_mutex;
std::mutex spawn_callback_mutex;

uint64_t spawn_request_sequence = 0;
uint64_t pending_spawn_sequence = 0;
int last_observed_dispatch_state = -1;
std::string pending_spawn_name;

enum class PendingDispatchKind
{
	None,
	Spawn,
	ActorDelete,
	Animation,
	EquipmentState
};

PendingDispatchKind pending_dispatch_kind = PendingDispatchKind::None;
int pending_animation_player = 0;
uint32_t pending_animation_actor = 0;
uint32_t pending_animation_hash = 0;
int pending_equipment_player = 0;
uint32_t pending_equipment_actor = 0;
bool pending_equipment_held = false;
int pending_delete_player = 0;
uint32_t pending_delete_actor = 0;
bool actor_spawn_template_ready = false;
InstanceData actor_spawn_template{};
int actor_spawn_template_r3 = 0;
int actor_spawn_template_r5 = 0;

struct CompletedAnimation
{
	uint32_t actorAddress;
	uint32_t animation;
};

std::map<int, CompletedAnimation> completedAnimations;
std::map<int, DWORD> lastAnimationPointerWarning;

struct PendingEquipmentChild
{
	std::string actorName;
	std::string slot;
	int playerNumber;
};

std::mutex equipment_child_mutex;
std::vector<PendingEquipmentChild> pendingEquipmentChildren;

std::map<char, bool> prevKeyStateMap; // Used for key press logic - keeps track of previous key state

std::vector<QueueActor> queuedActors;
std::vector<QueueActorDelete> queuedActorDeletes;
std::vector<QueueAnimation> queuedAnimations;
std::vector<QueueEquipmentState> queuedEquipmentStates;

const char* PendingDispatchLabel(PendingDispatchKind kind)
{
	switch (kind)
	{
	case PendingDispatchKind::Spawn: return "Spawn";
	case PendingDispatchKind::ActorDelete: return "Actor refresh";
	case PendingDispatchKind::Animation: return "Animation";
	case PendingDispatchKind::EquipmentState: return "Equipment state";
	default: return "Dispatch";
	}
}

// Setup raises every remote actor's delete flag before network data can enqueue
// replacements. Keep actor callbacks in cleanup mode until the grace period is
// complete so an old Jugador instance cannot be adopted as the current player.
std::atomic<bool> stalePlayerCleanupActive{false};
std::atomic<int> stalePlayerEraseCount{0};

bool TryParseRemotePlayerActor(const std::string& name, int& playerNumber)
{
	if (name.rfind("Jugador", 0) != 0 || name.size() <= 7)
		return false;

	int parsedNumber = 0;
	for (size_t i = 7; i < name.size(); ++i)
	{
		if (name[i] < '0' || name[i] > '9')
			return false;
		parsedNumber = parsedNumber * 10 + (name[i] - '0');
		if (parsedNumber > 32)
			return false;
	}

	if (parsedNumber < 1)
		return false;
	playerNumber = parsedNumber;
	return true;
}

bool TryParseEquipmentPlaceholder(const std::string& name, int& playerNumber)
{
	if (name.rfind("Jugador", 0) != 0 || name.size() <= 7)
		return false;

	size_t suffixStart = 7;
	int parsedNumber = 0;
	while (suffixStart < name.size() && name[suffixStart] >= '0' && name[suffixStart] <= '9')
	{
		parsedNumber = parsedNumber * 10 + (name[suffixStart] - '0');
		++suffixStart;
	}
	if (parsedNumber < 1 || parsedNumber > 32)
		return false;

	const std::string suffix = name.substr(suffixStart);
	if (suffix != "RightHandWeaponLongName" &&
		suffix != "LeftHandWeaponLongName" &&
		suffix != "BowWeaponLongName")
		return false;

	playerNumber = parsedNumber;
	return true;
}

void ResolveEquipmentActor(PPCInterpreter_t* hCPU)
{
	hCPU->instructionPointer = hCPU->sprNew.LR;
	if (!Game::GameInstance || hCPU->gpr[4] == 0)
		return;

	const uint64_t nameAddress = Main::baseAddr + hCPU->gpr[4];
	const std::string placeholder = Memory::read_string(nameAddress, 64, __FUNCTION__);
	int playerNumber = 0;
	if (!TryParseEquipmentPlaceholder(placeholder, playerNumber))
		return;

	const auto player = Instances::PlayerList.find(playerNumber);
	if (player == Instances::PlayerList.end())
		return;

	std::string resource;
	std::string slot;
	if (!player->second->Equipment->ResolveFactoryResource(placeholder, resource, slot))
		return;

	if (!resource.empty())
	{
		Memory::write_string(
			nameAddress, resource, static_cast<int>(placeholder.size() + 1), __FUNCTION__);
		std::lock_guard<std::mutex> childLock(equipment_child_mutex);
		pendingEquipmentChildren.erase(
			std::remove_if(
				pendingEquipmentChildren.begin(), pendingEquipmentChildren.end(),
				[playerNumber, &slot](const PendingEquipmentChild& child) {
					return child.playerNumber == playerNumber && child.slot == slot;
				}),
			pendingEquipmentChildren.end());
		pendingEquipmentChildren.push_back({resource, slot, playerNumber});
	}

	const std::string readback = Memory::read_string(nameAddress, 64, __FUNCTION__);
	std::stringstream stream;
	stream << "Player " << playerNumber << " factory " << slot
		<< " request at guest 0x" << std::hex << hCPU->gpr[4]
		<< ": placeholder=" << placeholder << ", requested="
		<< (resource.empty() ? "none" : resource) << ", readback=" << readback
		<< ", state="
		<< (player->second->LastIsEquipped.load(std::memory_order_acquire) ? "Hold" : "Equip")
		<< ".";
	Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
}

void LogEquipmentChildCreation(const std::string& name, uint32_t actorAddress)
{
	std::lock_guard<std::mutex> childLock(equipment_child_mutex);
	const auto pending = std::find_if(
		pendingEquipmentChildren.begin(), pendingEquipmentChildren.end(),
		[&name](const PendingEquipmentChild& child) { return child.actorName == name; });
	if (pending == pendingEquipmentChildren.end())
		return;

	std::stringstream stream;
	stream << "Player " << pending->playerNumber << " created " << pending->slot
		<< " child " << name << " at guest 0x" << std::hex << actorAddress << ".";
	Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
	pendingEquipmentChildren.erase(pending);
}

void DespawnStalePlayerActors()
{
	if (Instances::PlayerList.empty())
		return;

	stalePlayerEraseCount.store(0, std::memory_order_relaxed);
	stalePlayerCleanupActive.store(true, std::memory_order_release);
	size_t discardedRequests = 0;
	{
		std::unique_lock<std::shared_mutex> queueLock(queue_mutex);
		const size_t previousSize = queuedActors.size();
		queuedActors.erase(
			std::remove_if(queuedActors.begin(), queuedActors.end(), [](const QueueActor& actor) {
				int playerNumber = 0;
				return TryParseRemotePlayerActor(actor.Name, playerNumber);
			}),
			queuedActors.end());
		discardedRequests = previousSize - queuedActors.size();
		queuedAnimations.clear();
		queuedActorDeletes.clear();
		queuedEquipmentStates.clear();
		completedAnimations.clear();
	}
	Logging::LoggerService::LogInformation(
		"Despawning stale remote player actors before multiplayer synchronization; discarded " +
			std::to_string(discardedRequests) + " queued replacement request(s).",
		__FUNCTION__);

	for (const auto& player : Instances::PlayerList)
	{
		player.second->setAddress(0);
		player.second->Status->set(MemoryAccess::Player::DELETE_STATUS, __FUNCTION__);
	}

	// BOTW consumes the status flags from its actor update loop. The bounded
	// grace period runs before server sync starts, so it cannot race a valid new
	// spawn and does not depend on a particular PPC core reaching an HLE hook.
	const DWORD cleanupStart = GetTickCount();
	while (Main::IsCemuTitleActive() &&
		static_cast<DWORD>(GetTickCount() - cleanupStart) < 2000)
		Sleep(50);

	stalePlayerCleanupActive.store(false, std::memory_order_release);
	for (const auto& player : Instances::PlayerList)
	{
		player.second->setAddress(0);
		player.second->Status->set(0, __FUNCTION__);
	}

	Logging::LoggerService::LogInformation(
		"Stale remote player cleanup finished; erased " +
			std::to_string(stalePlayerEraseCount.load(std::memory_order_relaxed)) +
			" actor instance(s).",
		__FUNCTION__);
}

MemoryInstance* memInstance;

bool isSetup = false;

void setup(PPCInterpreter_t* hCPU, uint32_t startTrnsData, uint64_t baseAddress) {

	TransferableData trnsData;
	data_mutex.lock(); //////////////////////////////////////////////////
	memInstance->memory_readMemoryBE(startTrnsData, &trnsData, baseAddress); // Just make sure to intercept stuff..
	trnsData.interceptRegisters = true;
	memInstance->memory_writeMemoryBE(startTrnsData, trnsData, baseAddress);
	data_mutex.unlock(); //===============================================

	isSetup = true;
}

void queueActor(int playerNumber, float Position[3])
{
	const auto activePlayer = Instances::PlayerList.find(playerNumber);
	if (activePlayer != Instances::PlayerList.end() && activePlayer->second->baseAddr != 0)
	{
		activePlayer->second->SpawnPending.store(false, std::memory_order_release);
		activePlayer->second->SpawnCallbackExpected.store(false, std::memory_order_release);
		Logging::LoggerService::LogDebug(
			"Discarded queued player " + std::to_string(playerNumber) +
			" creation because its replacement actor already exists.",
			__FUNCTION__);
		return;
	}

	if (queuedActors.size() > 0)
		for (int i = 0; i < queuedActors.size(); i++)
			if (queuedActors[i].Name == "Jugador" + std::to_string(playerNumber))
				return;

	//for (int j = queuedActors.size() - 1; j >= 0; j--)
	//{
	//	if (queuedActors[j].Name == "Jugador" + std::to_string(playerNumber))
	//	{
	//		return;
	//	}
	//}

	QueueActor queueActor;
	std::string actorName = "Jugador";
	actorName.append(std::to_string(playerNumber));
	queueActor.Name = actorName;
	queueActor.PosX = Position[0];
	queueActor.PosY = Position[1];
	queueActor.PosZ = Position[2];

	queuedActors.push_back(queueActor);
	Logging::LoggerService::LogDebug("Queued actor " + actorName + " for the HLE spawn hook.", __FUNCTION__);
}

void queueActor(std::string actorName, float Position[3])
{

	QueueActor queueActor;
	queueActor.Name = actorName;
	queueActor.PosX = Position[0];
	queueActor.PosY = Position[1];
	queueActor.PosZ = Position[2];

	queuedActors.push_back(queueActor);

}

void queueRemoteAnimation(int playerNumber, uint64_t actorAddress, uint32_t animation)
{
	if (playerNumber < 1 || playerNumber > 32 || actorAddress == 0 ||
		actorAddress > std::numeric_limits<uint32_t>::max())
		return;

	const uint32_t guestActorAddress = static_cast<uint32_t>(actorAddress);
	std::unique_lock<std::shared_mutex> queueLock(queue_mutex);
	const auto completed = completedAnimations.find(playerNumber);
	if (completed != completedAnimations.end() &&
		completed->second.actorAddress == guestActorAddress &&
		completed->second.animation == animation)
		return;

	for (QueueAnimation& queued : queuedAnimations)
	{
		if (queued.playerNumber == playerNumber)
		{
			queued.actorAddress = guestActorAddress;
			queued.animation = animation;
			return;
		}
	}

	queuedAnimations.push_back({playerNumber, guestActorAddress, animation});
}

void queueRemoteEquipmentState(int playerNumber, uint64_t actorAddress, bool held)
{
	if (playerNumber < 1 || playerNumber > 32 || actorAddress == 0 ||
		actorAddress > std::numeric_limits<uint32_t>::max())
		return;

	const uint32_t guestActorAddress = static_cast<uint32_t>(actorAddress);
	std::unique_lock<std::shared_mutex> queueLock(queue_mutex);
	for (QueueEquipmentState& queued : queuedEquipmentStates)
	{
		if (queued.playerNumber == playerNumber)
		{
			queued.actorAddress = guestActorAddress;
			queued.held = held;
			queued.queuedAt = GetTickCount();
			return;
		}
	}
	queuedEquipmentStates.push_back({playerNumber, guestActorAddress, held, GetTickCount()});
}

void queueRemoteActorRefresh(int playerNumber, uint64_t actorAddress)
{
	if (playerNumber < 1 || playerNumber > 32 || actorAddress == 0 ||
		actorAddress > std::numeric_limits<uint32_t>::max())
		return;

	const uint32_t guestActorAddress = static_cast<uint32_t>(actorAddress);
	std::unique_lock<std::shared_mutex> queueLock(queue_mutex);
	for (const QueueActorDelete& queued : queuedActorDeletes)
	{
		if (queued.playerNumber == playerNumber && queued.actorAddress == guestActorAddress)
			return;
	}

	queuedAnimations.erase(
		std::remove_if(
			queuedAnimations.begin(),
			queuedAnimations.end(),
			[playerNumber](const QueueAnimation& queued) {
				return queued.playerNumber == playerNumber;
			}),
		queuedAnimations.end());
	completedAnimations.erase(playerNumber);
	queuedActorDeletes.push_back({playerNumber, guestActorAddress});
	Logging::LoggerService::LogDebug(
		"Queued player " + std::to_string(playerNumber) +
		" for a direct equipment refresh.",
		__FUNCTION__);
}

void resetRemoteAnimationDispatch(int playerNumber)
{
	std::unique_lock<std::shared_mutex> queueLock(queue_mutex);
	queuedAnimations.erase(
		std::remove_if(
			queuedAnimations.begin(),
			queuedAnimations.end(),
			[playerNumber](const QueueAnimation& queued) {
				return queued.playerNumber == playerNumber;
			}),
		queuedAnimations.end());
	completedAnimations.erase(playerNumber);
	queuedEquipmentStates.erase(
		std::remove_if(
			queuedEquipmentStates.begin(),
			queuedEquipmentStates.end(),
			[playerNumber](const QueueEquipmentState& queued) {
				return queued.playerNumber == playerNumber;
			}),
		queuedEquipmentStates.end());
}

void setupActor(PPCInterpreter_t* hCPU, TransferableData& trnsData, InstanceData& instData, uint32_t startRingBuffer, uint32_t endRingBuffer, uint64_t baseAddress) {
	QueueActor qAct = queuedActors[0];
	int queuedPlayer = 0;
	if (TryParseRemotePlayerActor(qAct.Name, queuedPlayer))
	{
		const auto activePlayer = Instances::PlayerList.find(queuedPlayer);
		if (activePlayer != Instances::PlayerList.end() && activePlayer->second->baseAddr != 0)
		{
			activePlayer->second->SpawnPending.store(false, std::memory_order_release);
			activePlayer->second->SpawnCallbackExpected.store(false, std::memory_order_release);
			queuedActors.erase(queuedActors.begin());
			Logging::LoggerService::LogDebug(
				"Cancelled player " + std::to_string(queuedPlayer) +
				" spawn dispatch because its replacement actor already exists.",
				__FUNCTION__);
			return;
		}
	}


	// Lets set any data that our params will reference:
	// -------------------------------------------------

	// Capture the complete actor-factory template exactly once from the first
	// proven-good natural call. After a direct equipment deletion, unrelated
	// factory calls can overwrite F_R3/F_R5/F_R7 with an old actor address, null,
	// or short-lived actor memory. Reuse the stable host copy for every synthetic
	// Jugador instead.
	if (!actor_spawn_template_ready)
	{
		actor_spawn_template_r3 = trnsData.f_r3;
		actor_spawn_template_r5 = trnsData.f_r5;
		data_mutex.lock_shared(); //////////////////////////////////////////////////
		memInstance->memory_readMemoryBE(
			trnsData.f_r7,
			&actor_spawn_template.actorStorage,
			baseAddress);
		data_mutex.unlock_shared(); //==============================================
		actor_spawn_template_ready = true;
	}
	trnsData.f_r3 = actor_spawn_template_r3;
	trnsData.f_r5 = actor_spawn_template_r5;
	memcpy(
		&instData.actorStorage,
		&actor_spawn_template.actorStorage,
		sizeof(instData.actorStorage));

	int actorStorageLocation = trnsData.ringPtr + sizeof(instData) - sizeof(instData.name) - sizeof(instData.actorStorage);
	int mubinLocation = trnsData.ringPtr + sizeof(instData) - sizeof(instData.name) - sizeof(instData.actorStorage) + (7 * 4); // The MubinIter lives inside the actor btw

	// Set actor pos to stored pos
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (15 * 4)], &qAct.PosX, sizeof(float));
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (16 * 4)], &qAct.PosY, sizeof(float));
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (17 * 4)], &qAct.PosZ, sizeof(float));

	// We want to make sure there's a fairly high traverseDist
	float traverseDist = 0.f; // Hmm... this kinda proves this isn't really used
	short traverseDistInt = (short)traverseDist;
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (18 * 4)], &traverseDist, sizeof(float));
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (37 * 2)], &traverseDistInt, sizeof(short));

	int null = 0;
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (11 * 4)], &null, sizeof(int)); // mLinkData

	// Might as well null out some other things
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (9 * 4)], &null, sizeof(int)); // mData
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (10 * 4)], &null, sizeof(int)); // mProc
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (7 * 4)], &null, sizeof(int)); // idk what this is
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (3 * 4)], &null, sizeof(int)); // or this, either



	// Not sure what these are, but they helps with traverseDist issues
	int traverseDistFixer = 0x043B0000;
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (2 * 4)], &traverseDistFixer, sizeof(int));
	int traverseDistFixer2 = 0x00000016;
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (1 * 4)], &traverseDistFixer2, sizeof(int));


	// Oh, and the HashId as well
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (14 * 4)], &null, sizeof(int));

	// And we can make mRevivalGameDataFlagHash an invalid handle
	int invalid = -1;
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (12 * 4)], &invalid, sizeof(int));
	// And whatever this is, too
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (13 * 4)], &invalid, sizeof(int));

	// We can also get rid of this junk
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (8 * 4)], &invalid, sizeof(int));




	// Set name!
	{ // Copy to name string storage... reversed
		int pos = sizeof(instData.name) - 1;
		for (char const& c : qAct.Name) {
			memcpy(instData.name + pos, &c, 1);
			pos--;
		}
		uint8_t nullByte = 0;
		memcpy(instData.name + pos, &nullByte, 1); // Null terminate!
	}

	// -------------------------------------------------

	// Set registers for params and stuff
	trnsData.f_r4 = trnsData.ringPtr + sizeof(instData) - sizeof(instData.name);
	trnsData.f_r6 = mubinLocation;
	trnsData.f_r7 = actorStorageLocation;
	trnsData.f_r8 = 0;
	trnsData.f_r9 = 1;
	trnsData.f_r10 = 0;

	// Preserve the original callback-register path for Cemu backends that
	// support it, but also persist every argument in PPC-visible memory. The
	// graphic-pack wrapper reloads these fields after the HLE callback, which
	// makes dispatch independent of native register write-back and PPC core.
	hCPU->gpr[3] = trnsData.f_r3;
	hCPU->gpr[4] = trnsData.f_r4;
	hCPU->gpr[5] = trnsData.f_r5;
	hCPU->gpr[6] = trnsData.f_r6;
	hCPU->gpr[7] = trnsData.f_r7;
	hCPU->gpr[8] = trnsData.f_r8;
	hCPU->gpr[9] = trnsData.f_r9;
	hCPU->gpr[10] = trnsData.f_r10;
	trnsData.fnAddr = 0x037b6040; // Address to call to
	// The PPC wrappers atomically change Ready to Claimed so exactly one core
	// dispatches this actor. The winning wrapper clears the state only after the
	// actor-factory call returns.
	trnsData.dispatchState = 1;
	pending_spawn_sequence = ++spawn_request_sequence;
	pending_spawn_name = qAct.Name;
	pending_dispatch_kind = PendingDispatchKind::Spawn;
	if (queuedPlayer != 0)
	{
		const auto activePlayer = Instances::PlayerList.find(queuedPlayer);
		if (activePlayer != Instances::PlayerList.end())
			activePlayer->second->SpawnCallbackExpected.store(false, std::memory_order_release);
	}
	last_observed_dispatch_state = -1;

	trnsData.enabled = true; // This tells the assembly patch to trigger one function call
	Logging::LoggerService::LogDebug("Submitted actor " + qAct.Name + " to BOTW's spawn function.", __FUNCTION__);
	std::stringstream dispatchDetails;
	dispatchDetails << "Spawn request " << pending_spawn_sequence << " arguments: function=0x"
		<< std::hex << static_cast<uint32_t>(trnsData.fnAddr)
		<< ", r3=0x" << static_cast<uint32_t>(trnsData.f_r3)
		<< ", r4=0x" << static_cast<uint32_t>(trnsData.f_r4)
		<< ", r5=0x" << static_cast<uint32_t>(trnsData.f_r5)
		<< ", r6=0x" << static_cast<uint32_t>(trnsData.f_r6)
		<< ", r7=0x" << static_cast<uint32_t>(trnsData.f_r7)
		<< ", r8=0x" << static_cast<uint32_t>(trnsData.f_r8)
		<< ", r9=0x" << static_cast<uint32_t>(trnsData.f_r9)
		<< ", r10=0x" << static_cast<uint32_t>(trnsData.f_r10)
		<< ", ring=0x" << static_cast<uint32_t>(trnsData.ringPtr) << ".";
	Logging::LoggerService::LogDebug(dispatchDetails.str(), __FUNCTION__);

	trnsData.interceptRegisters = false; // We don't want to intercept *this* function call

	// Write our actor data!
	data_mutex.lock(); ////////////////////////////////////////////////////
	memInstance->memory_writeMemoryBE(trnsData.ringPtr, instData, baseAddress);
	data_mutex.unlock(); //================================================

	trnsData.ringPtr += sizeof(InstanceData); // Move our ring ptr to the next slot!
	if (trnsData.ringPtr >= endRingBuffer) // If we're at the end of the ring....
trnsData.ringPtr = startRingBuffer; // move to the start!

// Gotta remove this actor from the queue!
queuedActors.erase(queuedActors.begin());
}

bool setupAnimation(PPCInterpreter_t* hCPU, TransferableData& trnsData,
	uint32_t startTrnsData, uint32_t startRingBuffer, uint32_t endRingBuffer,
	uint64_t baseAddress)
{
	const QueueAnimation queued = queuedAnimations.front();
	const auto player = Instances::PlayerList.find(queued.playerNumber);
	if (player == Instances::PlayerList.end() ||
		player->second->baseAddr != queued.actorAddress)
	{
		queuedAnimations.erase(queuedAnimations.begin());
		return false;
	}

	const auto completed = completedAnimations.find(queued.playerNumber);
	if (completed != completedAnimations.end() &&
		completed->second.actorAddress == queued.actorAddress &&
		completed->second.animation == queued.animation)
	{
		queuedAnimations.erase(queuedAnimations.begin());
		return false;
	}

	uint32_t animationController = 0;
	std::string pointerFailure;
	if (!Memory::TryReadBigEndian4BytesOffset(
			static_cast<uint64_t>(queued.actorAddress) + 0x394,
			animationController,
			&pointerFailure) ||
		animationController == 0)
	{
		const DWORD now = GetTickCount();
		const auto previousWarning = lastAnimationPointerWarning.find(queued.playerNumber);
		if (previousWarning == lastAnimationPointerWarning.end() ||
			float(now - previousWarning->second) / 1000.0f >= 2.0f)
		{
			lastAnimationPointerWarning[queued.playerNumber] = now;
			Logging::LoggerService::LogWarning(
				"Player " + std::to_string(queued.playerNumber) +
				" animation controller is not ready" +
				(pointerFailure.empty() ? "." : ": " + pointerFailure + "."),
				__FUNCTION__);
		}
		queuedAnimations.erase(queuedAnimations.begin());
		return false;
	}

	const uint32_t stringLocation = static_cast<uint32_t>(trnsData.ringPtr);
	const uint32_t safeStringLocation = stringLocation + 64;
	const std::string animationName = "Anim_" + std::to_string(queued.animation);
	if (animationName.size() + 1 > 64)
	{
		queuedAnimations.erase(queuedAnimations.begin());
		return false;
	}

	{
		std::unique_lock<std::shared_mutex> dataLock(data_mutex);
		for (size_t i = 0; i < animationName.size(); ++i)
			memInstance->memory_writeMemory(
				stringLocation + static_cast<uint32_t>(i),
				static_cast<byte>(animationName[i]),
				baseAddress);
		memInstance->memory_writeMemory(
			stringLocation + static_cast<uint32_t>(animationName.size()),
			static_cast<byte>(0),
			baseAddress);

		// Wii U sead::SafeString stores the guest C string followed by its
		// vtable. EventHoverNullASPlayBase uses this exact vtable when it calls
		// the live AS controller.
		memInstance->memory_writeMemoryBE(safeStringLocation, stringLocation, baseAddress);
		memInstance->memory_writeMemoryBE(safeStringLocation + 4, uint32_t(0x10263910), baseAddress);
		memInstance->memory_writeMemoryBE(startTrnsData - 4, -1.0f, baseAddress);
		memInstance->memory_writeMemoryBE(startTrnsData - 8, -1.0f, baseAddress);
	}

	trnsData.f_r3 = static_cast<int>(animationController);
	trnsData.f_r4 = static_cast<int>(safeStringLocation);
	trnsData.f_r5 = 0;
	trnsData.f_r6 = 0;
	trnsData.f_r7 = 1;
	trnsData.f_r8 = 0;
	trnsData.f_r9 = 0;
	trnsData.f_r10 = 0;
	hCPU->gpr[3] = animationController;
	hCPU->gpr[4] = safeStringLocation;
	hCPU->gpr[5] = 0;
	hCPU->gpr[6] = 0;
	hCPU->gpr[7] = 1;
	hCPU->gpr[8] = 0;
	hCPU->gpr[9] = 0;
	hCPU->gpr[10] = 0;
	hCPU->fpr[1].fpr = -1.0;
	hCPU->fpr[2].fpr = -1.0;

	trnsData.fnAddr = 0x0370dcf8;
	trnsData.dispatchState = 1;
	trnsData.enabled = true;
	trnsData.interceptRegisters = false;
	pending_spawn_sequence = ++spawn_request_sequence;
	pending_spawn_name = animationName;
	pending_dispatch_kind = PendingDispatchKind::Animation;
	pending_animation_player = queued.playerNumber;
	pending_animation_actor = queued.actorAddress;
	pending_animation_hash = queued.animation;
	last_observed_dispatch_state = -1;

	Logging::LoggerService::LogDebug(
		"Submitted direct AS animation " + animationName + " for player " +
		std::to_string(queued.playerNumber) + ".",
		__FUNCTION__);

	trnsData.ringPtr += sizeof(InstanceData);
	if (trnsData.ringPtr >= static_cast<int>(endRingBuffer))
		trnsData.ringPtr = startRingBuffer;
	queuedAnimations.erase(queuedAnimations.begin());
	return true;
}

bool setupEquipmentState(PPCInterpreter_t* hCPU, TransferableData& trnsData,
	uint32_t startRingBuffer, uint32_t endRingBuffer, uint64_t baseAddress)
{
	const QueueEquipmentState queued = queuedEquipmentStates.front();
	const auto player = Instances::PlayerList.find(queued.playerNumber);
	if (player == Instances::PlayerList.end() ||
		player->second->baseAddr != queued.actorAddress)
	{
		queuedEquipmentStates.erase(queuedEquipmentStates.begin());
		return false;
	}

	// ChangeWeaponEquipState::oneShot_ notifies the actor after its equipment
	// children have finished creation. Avoid racing the callback that queued us.
	if (static_cast<DWORD>(GetTickCount() - queued.queuedAt) < 100)
		return false;

	uint32_t animationController = 0;
	std::string pointerFailure;
	if (!Memory::TryReadBigEndian4BytesOffset(
			static_cast<uint64_t>(queued.actorAddress) + 0x394,
			animationController,
			&pointerFailure) ||
		animationController == 0)
	{
		Logging::LoggerService::LogWarning(
			"Player " + std::to_string(queued.playerNumber) +
			" equipment-state controller is not ready" +
			(pointerFailure.empty() ? "." : ": " + pointerFailure + "."),
			__FUNCTION__);
		queuedEquipmentStates.erase(queuedEquipmentStates.begin());
		return false;
	}

	const uint32_t stringLocation = static_cast<uint32_t>(trnsData.ringPtr);
	const uint32_t safeStringLocation = stringLocation + 64;
	const uint32_t actorState = queued.held ? 0u : 1u;
	{
		std::unique_lock<std::shared_mutex> dataLock(data_mutex);
		memInstance->memory_writeMemory(stringLocation, static_cast<byte>(0), baseAddress);
		memInstance->memory_writeMemoryBE(safeStringLocation, stringLocation, baseAddress);
		memInstance->memory_writeMemoryBE(
			safeStringLocation + 4, uint32_t(0x10263910), baseAddress);
		memInstance->memory_writeMemoryBE(
			queued.actorAddress + 0xb94, actorState, baseAddress);
	}

	// Wii U ChangeWeaponEquipState::oneShot_ writes actor+0xb94 (Hold=0,
	// Equip=1), then calls this controller notification with message 0x2c and
	// an empty sead::SafeString for NPC-profile actors. Dispatch that exact
	// operation instead of relying on the unscheduled MultiplayerEvent flow.
	trnsData.f_r3 = static_cast<int>(animationController);
	trnsData.f_r4 = 0x2c;
	trnsData.f_r5 = static_cast<int>(safeStringLocation);
	trnsData.f_r6 = 0;
	trnsData.f_r7 = 0;
	trnsData.f_r8 = 0;
	trnsData.f_r9 = 0;
	trnsData.f_r10 = 0;
	hCPU->gpr[3] = animationController;
	hCPU->gpr[4] = 0x2c;
	hCPU->gpr[5] = safeStringLocation;
	for (int reg = 6; reg <= 10; ++reg)
		hCPU->gpr[reg] = 0;

	trnsData.fnAddr = 0x0370ee34;
	trnsData.dispatchState = 1;
	trnsData.enabled = true;
	trnsData.interceptRegisters = false;
	pending_spawn_sequence = ++spawn_request_sequence;
	pending_spawn_name = queued.held ? "Hold" : "Equip";
	pending_dispatch_kind = PendingDispatchKind::EquipmentState;
	pending_equipment_player = queued.playerNumber;
	pending_equipment_actor = queued.actorAddress;
	pending_equipment_held = queued.held;
	last_observed_dispatch_state = -1;

	std::stringstream stream;
	stream << "Submitted direct equipment state for player " << queued.playerNumber
		<< ": actor=0x" << std::hex << queued.actorAddress
		<< ", controller=0x" << animationController
		<< ", state=" << (queued.held ? "Hold(0)" : "Equip(1)")
		<< ", readback="
		<< Memory::read_bigEndian4BytesOffset(queued.actorAddress + 0xb94, __FUNCTION__)
		<< ".";
	Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);

	trnsData.ringPtr += sizeof(InstanceData);
	if (trnsData.ringPtr >= static_cast<int>(endRingBuffer))
		trnsData.ringPtr = startRingBuffer;
	queuedEquipmentStates.erase(queuedEquipmentStates.begin());
	return true;
}

bool setupActorDelete(PPCInterpreter_t* hCPU, TransferableData& trnsData)
{
	const QueueActorDelete queued = queuedActorDeletes.front();
	const auto player = Instances::PlayerList.find(queued.playerNumber);
	if (player == Instances::PlayerList.end() ||
		player->second->baseAddr != queued.actorAddress)
	{
		queuedActorDeletes.erase(queuedActorDeletes.begin());
		return false;
	}

	trnsData.f_r3 = static_cast<int>(queued.actorAddress);
	// The actor-interceptor patch reserves delete reason 0x7f for this direct
	// refresh and bypasses its early deleteLater HLE callback. BOTW normalizes it
	// to reason 0 and reports completion through the real ActorErase hook.
	trnsData.f_r4 = 0x7f;
	trnsData.f_r5 = 0;
	trnsData.f_r6 = 0;
	trnsData.f_r7 = 0;
	trnsData.f_r8 = 0;
	trnsData.f_r9 = 0;
	trnsData.f_r10 = 0;
	hCPU->gpr[3] = queued.actorAddress;
	hCPU->gpr[4] = 0x7f;
	for (int reg = 5; reg <= 10; ++reg)
		hCPU->gpr[reg] = 0;

	// ksys::act::BaseProc::deleteLater(DeleteReason::_0). This is the same
	// Wii U entry intercepted by patch_UKL_ActorInterceptor.asm, so the native
	// actor bookkeeping is cleared as soon as the game accepts the request.
	trnsData.fnAddr = 0x0378a374;
	trnsData.dispatchState = 1;
	trnsData.enabled = true;
	trnsData.interceptRegisters = false;
	pending_spawn_sequence = ++spawn_request_sequence;
	pending_spawn_name = "Jugador" + std::to_string(queued.playerNumber);
	pending_dispatch_kind = PendingDispatchKind::ActorDelete;
	pending_delete_player = queued.playerNumber;
	pending_delete_actor = queued.actorAddress;
	last_observed_dispatch_state = -1;

	Logging::LoggerService::LogDebug(
		"Submitted direct equipment refresh for player " +
		std::to_string(queued.playerNumber) + ".",
		__FUNCTION__);
	queuedActorDeletes.erase(queuedActorDeletes.begin());
	return true;
}

void mainFn(PPCInterpreter_t* hCPU, uint32_t startTrnsData, uint32_t startRingBuffer, uint32_t endRingBuffer, uint64_t baseAddress) {
	hCPU->instructionPointer = hCPU->sprNew.LR; // Tell it where to return to - REQUIRED
	// Multiple emulated PPC cores can enter this native HLE callback at once.
	// Serialize the transfer transaction so a second callback cannot publish an
	// older snapshot over a newly prepared request.
	std::lock_guard<std::mutex> callbackLock(spawn_callback_mutex);

	// This is the stuff I'm currently using to test different values for potential Link coords
	//MemoryInstance::floatBE* pos = reinterpret_cast<MemoryInstance::floatBE*>(memInstance->baseAddr + 0x10263910);
	//Console::LogPrint((float)*pos);

	if (!isSetup) {
		setup(hCPU, startTrnsData, baseAddress);
		return;
	}

	//queueActors();

	data_mutex.lock_shared(); /////////////////////////////////////////////////////////////////////
	// Get our transferrable data
	TransferableData trnsData;
	memInstance->memory_readMemoryBE(startTrnsData, &trnsData, baseAddress);
	data_mutex.unlock_shared(); //==================================================================
	const bool invalidRingRange = startRingBuffer >= endRingBuffer ||
		static_cast<uint64_t>(endRingBuffer) - startRingBuffer < sizeof(InstanceData);
	const bool invalidRingPointer = !invalidRingRange &&
		(trnsData.ringPtr < static_cast<int>(startRingBuffer) ||
		 static_cast<uint64_t>(static_cast<uint32_t>(trnsData.ringPtr)) + sizeof(InstanceData) > endRingBuffer ||
		 (static_cast<uint32_t>(trnsData.ringPtr) - startRingBuffer) % sizeof(InstanceData) != 0);
	if (invalidRingRange || invalidRingPointer) {
		Logging::LoggerService::LogError(
			"Rejected invalid spawn ring layout (start=" + std::to_string(startRingBuffer) +
			", end=" + std::to_string(endRingBuffer) +
			", pointer=" + std::to_string(trnsData.ringPtr) + ").",
			__FUNCTION__);
		if (invalidRingRange)
			return;
		trnsData.ringPtr = startRingBuffer;
	}
	// A PPC wrapper has not consumed the previous transaction yet. Leave every
	// byte untouched; any wrapper may safely dispatch the shared arguments.
	if (trnsData.enabled) {
		if (trnsData.dispatchState != last_observed_dispatch_state) {
			last_observed_dispatch_state = trnsData.dispatchState;
			const std::string dispatchType = PendingDispatchLabel(pending_dispatch_kind);
			if (trnsData.dispatchState == 1) {
				Logging::LoggerService::LogDebug(
					dispatchType + " request " + std::to_string(pending_spawn_sequence) +
					" (" + pending_spawn_name + ") is PPC-visible and awaiting an atomic claim.",
					__FUNCTION__);
			} else if (trnsData.dispatchState == 2) {
				Logging::LoggerService::LogDebug(
					dispatchType + " request " + std::to_string(pending_spawn_sequence) +
					" (" + pending_spawn_name + ") was atomically claimed by a PPC wrapper.",
					__FUNCTION__);
			} else if (trnsData.dispatchState != 0) {
				Logging::LoggerService::LogError(
					dispatchType + " request " + std::to_string(pending_spawn_sequence) +
					" has invalid PPC dispatch state " + std::to_string(trnsData.dispatchState) + ".",
					__FUNCTION__);
			}
		}
		return;
	}
	if (pending_spawn_sequence != 0) {
		const std::string dispatchType = PendingDispatchLabel(pending_dispatch_kind);
		Logging::LoggerService::LogDebug(
			dispatchType + " request " + std::to_string(pending_spawn_sequence) +
			" (" + pending_spawn_name + ") returned from BOTW.",
			__FUNCTION__);
		if (pending_dispatch_kind == PendingDispatchKind::Animation)
		{
			std::unique_lock<std::shared_mutex> queueLock(queue_mutex);
			completedAnimations[pending_animation_player] = {
				pending_animation_actor,
				pending_animation_hash};
		}
		else if (pending_dispatch_kind == PendingDispatchKind::Spawn)
		{
			int spawnedPlayer = 0;
			if (TryParseRemotePlayerActor(pending_spawn_name, spawnedPlayer))
			{
				const auto player = Instances::PlayerList.find(spawnedPlayer);
				if (player != Instances::PlayerList.end() &&
					player->second->SpawnPending.load(std::memory_order_acquire))
				{
					player->second->SpawnCallbackExpected.store(true, std::memory_order_release);
					Logging::LoggerService::LogDebug(
						"Spawn dispatch completed for player " + std::to_string(spawnedPlayer) +
						"; one matching actor callback is now expected.",
						__FUNCTION__);
				}
			}
		}
		else if (pending_dispatch_kind == PendingDispatchKind::ActorDelete)
		{
			// deleteLater has returned to the wrapper, so the old actor must no
			// longer block its replacement. The real erase hook is asynchronous and
			// is not emitted by every native Cemu build; finalize our host-side
			// lifecycle here as well. If OnActorErase already ran, the address check
			// makes this a no-op.
			const auto player = Instances::PlayerList.find(pending_delete_player);
			if (player != Instances::PlayerList.end() &&
				player->second->baseAddr == pending_delete_actor)
			{
				player->second->SpawnPending.store(false, std::memory_order_release);
				player->second->SpawnCallbackExpected.store(false, std::memory_order_release);
				player->second->InvalidateNativeAnimationControls();
				resetRemoteAnimationDispatch(pending_delete_player);
				player->second->setAddress(0);
				Logging::LoggerService::LogDebug(
					"Equipment refresh delete completed for player " +
					std::to_string(pending_delete_player) +
					"; replacement spawn enabled.",
					__FUNCTION__);
			}
		}
		else if (pending_dispatch_kind == PendingDispatchKind::EquipmentState)
		{
			std::stringstream equipmentStream;
			equipmentStream << "Direct equipment state returned for player "
				<< pending_equipment_player << " at actor 0x" << std::hex
				<< pending_equipment_actor << ": "
				<< (pending_equipment_held ? "Hold" : "Equip") << ".";
			Logging::LoggerService::LogInformation(equipmentStream.str(), __FUNCTION__);
		}
		pending_spawn_sequence = 0;
		pending_spawn_name.clear();
		pending_dispatch_kind = PendingDispatchKind::None;
		pending_animation_player = 0;
		pending_animation_actor = 0;
		pending_animation_hash = 0;
		pending_equipment_player = 0;
		pending_equipment_actor = 0;
		pending_equipment_held = false;
		pending_delete_player = 0;
		pending_delete_actor = 0;
		last_observed_dispatch_state = -1;
	}
	// Natural actor-factory calls are needed only until the first valid template
	// is captured. Keep that template stable across equipment reloads instead of
	// replacing it with registers from an unrelated actor.
	trnsData.interceptRegisters = !actor_spawn_template_ready;

	queue_mutex.lock(); ////////////////////////////////////////////
	// Actual actor spawning - just read from queue here.
	if (queuedActors.size() >= 1) {
		InstanceData instData;
		data_mutex.lock_shared();
		memInstance->memory_readMemoryBE(static_cast<uint32_t>(trnsData.ringPtr), &instData, baseAddress);
		data_mutex.unlock_shared();
		setupActor(hCPU, trnsData, instData, startRingBuffer, endRingBuffer, baseAddress);
	}
	else if (!queuedActorDeletes.empty()) {
		setupActorDelete(hCPU, trnsData);
	}
	else if (!queuedEquipmentStates.empty()) {
		setupEquipmentState(
			hCPU,
			trnsData,
			startRingBuffer,
			endRingBuffer,
			baseAddress);
	}
	else if (!queuedAnimations.empty()) {
		setupAnimation(
			hCPU,
			trnsData,
			startTrnsData,
			startRingBuffer,
			endRingBuffer,
			baseAddress);
	}
	queue_mutex.unlock(); //========================================

	// Publish a dispatch transaction only after every argument is visible. The
	// PPC hook checks Enabled first, so writing that byte last is the commit.
	const bool dispatchReady = trnsData.enabled;
	trnsData.enabled = false;
	data_mutex.lock(); ////////////////////////////////////////////////////////////////////
	memInstance->memory_writeMemoryBE(startTrnsData, trnsData, baseAddress);
	if (dispatchReady) {
		// The PPC wrapper observes Enabled before claiming DispatchState. Keep
		// every argument and the ready state globally visible before publishing
		// that byte, including on ARM hosts with a weaker memory model.
		std::atomic_thread_fence(std::memory_order_release);
		const byte enabled = 1;
		memInstance->memory_writeMemory(startTrnsData, enabled, baseAddress);
	}
	data_mutex.unlock(); //==================================================================
}

// A wrapper function to set the params in a more cpp-friendly way
void mainFn(PPCInterpreter_t* hCPU) {

	hCPU->instructionPointer = hCPU->sprNew.LR; // Tell it where to return to - REQUIRED
	// This callback is reached from the game's actor update path.  On Unix it
	// provides a pause-safe heartbeat without relying on the legacy BCML event
	// flow that continuously rewrote the custom notPaused game-data flag.
	Main::SpawnHookHeartbeat.store(GetTickCount(), std::memory_order_relaxed);
	mainFn(hCPU, hCPU->gpr[3], hCPU->gpr[4], hCPU->gpr[5], Main::baseAddr);
}

enum Weather : uint32_t
{
	BlueSky,
	Cloudy,
	Rain,
	HeavyRain,
	Snow,
	HeavySnow,
	Thunderstorm,
	ThunderRain,
	BlueSkyRain
};

Weather newWeather = (Weather)0;

void WeatherFn(PPCInterpreter_t* hCPU) {

	hCPU->instructionPointer = hCPU->sprNew.LR;
	if (!Game::GameInstance || !Game::GameInstance->World)
		return;

	Game::GameInstance->World->LocalWeather = (int)(Weather)hCPU->gpr[26];
	newWeather = (Weather)Game::GameInstance->World->Weather;

	memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 0, (char)newWeather, Main::baseAddr);
	memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 1, (char)newWeather, Main::baseAddr);
	memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 2, (char)newWeather, Main::baseAddr);
	memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 3, (char)newWeather, Main::baseAddr);
	memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 4, (char)newWeather, Main::baseAddr);
	memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 5, (char)newWeather, Main::baseAddr);
	memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 6, (char)newWeather, Main::baseAddr);
	memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 7, (char)newWeather, Main::baseAddr);
	memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 8, (char)newWeather, Main::baseAddr);
}

int LastCreated = 0;
bool started = false;

void OnActorCreate(PPCInterpreter_t* hCPU)
{
	hCPU->instructionPointer = hCPU->sprNew.LR;
	if (!Game::GameInstance)
		return;

	std::string name = Memory::read_string(Main::baseAddr + hCPU->gpr[3] + 0x10, 100, __FUNCTION__);
	LogEquipmentChildCreation(name, hCPU->gpr[3]);
	if (name.rfind("Weapon_", 0) == 0)
	{
		std::stringstream weaponStream;
		weaponStream << "Observed weapon actor " << name << " at guest 0x"
			<< std::hex << hCPU->gpr[3] << ".";
		Logging::LoggerService::LogInformation(weaponStream.str(), __FUNCTION__);
	}
	static std::atomic<int> actorHookSamples{0};
	int sample = actorHookSamples.fetch_add(1, std::memory_order_relaxed);
	if (sample < 32)
		Logging::LoggerService::LogDebug("Actor hook " + std::to_string(sample) + ": " + name, __FUNCTION__);

	std::vector<std::string> BombChoices = { "CustomRemoteBomb", "CustomRemoteBomb2", "CustomRemoteBombCube", "CustomRemoteBombCube2" };

	if (name.rfind("Enemy_Bokoblin_Junior") != std::string::npos)
	{
		std::stringstream stream;
		stream << "Bokoblin: " << std::hex << hCPU->gpr[3];
		Logging::LoggerService::LogDebug(stream.str());
	}

	if (name.rfind("GameROMPlayer") != std::string::npos)
	{
		std::stringstream stream;
		stream << "Link: " << std::hex << hCPU->gpr[3];
		Logging::LoggerService::LogDebug(stream.str());
		Game::GameInstance->setAddress(hCPU->gpr[3]);
		CreateThread(0, 0, Main::ShowConnectionMessage, 0, 0, 0);
	}

	if (name.rfind("Jugador", 0) == 0)
	{
		int spawnedPlayer = 0;
		if (!TryParseRemotePlayerActor(name, spawnedPlayer))
		{
			Logging::LoggerService::LogWarning(
				"Ignoring malformed remote player actor name: " + name,
				__FUNCTION__);
			return;
		}
		auto player = Instances::PlayerList.find(spawnedPlayer);
		if (player == Instances::PlayerList.end())
		{
			Logging::LoggerService::LogWarning(
				"Ignoring remote player actor before its slot is configured: " + name,
				__FUNCTION__);
			return;
		}

		if (stalePlayerCleanupActive.load(std::memory_order_acquire))
		{
			player->second->Status->set(MemoryAccess::Player::DELETE_STATUS, __FUNCTION__);
			std::stringstream cleanupStream;
			cleanupStream << "Marked stale player " << spawnedPlayer << " at 0x"
				<< std::hex << Main::baseAddr + hCPU->gpr[3] << " for despawn.";
			Logging::LoggerService::LogDebug(cleanupStream.str(), __FUNCTION__);
			return;
		}

		// The actor factory can emit a delayed duplicate create callback after a
		// direct equipment deletion, even when the deleted and replacement actors
		// reuse the same guest address. Address checks cannot distinguish those
		// generations. Adopt an actor only after the matching synthetic spawn call
		// has returned and published this one-shot token.
		if (!player->second->SpawnCallbackExpected.exchange(false, std::memory_order_acq_rel))
		{
			std::stringstream staleCreateStream;
			staleCreateStream << "Ignored unpaired player " << spawnedPlayer
				<< " create callback at guest 0x" << std::hex << hCPU->gpr[3]
				<< "; no completed spawn dispatch is awaiting adoption.";
			Logging::LoggerService::LogWarning(staleCreateStream.str(), __FUNCTION__);
			return;
		}

		std::stringstream stream;
		stream << "Spawned player " << spawnedPlayer << " at: " << std::hex << Main::baseAddr + hCPU->gpr[3] << ". Setting up addresses.";
		Logging::LoggerService::LogDebug(stream.str(), __FUNCTION__);

		player->second->InvalidateNativeAnimationControls();
		player->second->setAddress(hCPU->gpr[3]);
		player->second->SpawnPending.store(false, std::memory_order_release);
		// A replacement may reuse the same guest address. Never let the previous
		// actor's completed AS request suppress the first animation on this one.
		resetRemoteAnimationDispatch(spawnedPlayer);
		// BOTW resolves equipment children during actor creation. The factory hook
		// records whether it replaced this actor's unique placeholders in time. Keep
		// the established one-shot reload only as a fallback when that hook was not
		// observed. Consume before writing so a newer concurrent equipment update
		// remains pending for its own reload.
		const bool resolvedDuringFactory =
			player->second->Equipment->ConsumeFactoryResolution();
		const bool refreshRequested = player->second->Equipment->ConsumeActorRefresh();
		const bool requiresEquipmentReload =
			refreshRequested && !resolvedDuringFactory &&
			player->second->Model.ModelType == 0;
		if (requiresEquipmentReload)
			player->second->EquipmentRefreshPending.store(true, std::memory_order_release);
		player->second->Equipment->SetWeapons(hCPU->gpr[3]);
		player->second->Equipment->SetArmor();
		player->second->Bumii->setAddress(hCPU->gpr[3]);
		queueRemoteEquipmentState(
			spawnedPlayer,
			hCPU->gpr[3],
			player->second->LastIsEquipped.load(std::memory_order_acquire));

		Logging::LoggerService::LogDebug("Player " + std::to_string(spawnedPlayer) + " setup correctly.", __FUNCTION__);
		if (requiresEquipmentReload)
		{
			Logging::LoggerService::LogDebug(
				"Equipment factory resolution was not observed for player " +
				std::to_string(spawnedPlayer) +
				"; scheduling the existing controlled actor reload.",
				__FUNCTION__);
			queueRemoteActorRefresh(spawnedPlayer, hCPU->gpr[3]);
		}
		else if (refreshRequested && resolvedDuringFactory)
		{
			Logging::LoggerService::LogDebug(
				"Initial equipment for player " + std::to_string(spawnedPlayer) +
				" was resolved during child actor creation; no redundant reload is required.",
				__FUNCTION__);
			player->second->EquipmentRefreshPending.store(false, std::memory_order_release);
		}
		else
		{
			player->second->EquipmentRefreshPending.store(false, std::memory_order_release);
		}
	}

	if (name.rfind("Enemy_", 0) == 0)
		Game::GameInstance->EnemyService->UpdateEnemyAddress(Main::baseAddr + hCPU->gpr[3], true);

	if (!started) return;

	if (name == "RemoteBomb")
	{
		Game::GameInstance->Bomb->setAddress(hCPU->gpr[3], __FUNCTION__);
		Game::GameInstance->Bomb->changeState(Normal);
	}
	else if (name == "RemoteBomb2")
	{
		Game::GameInstance->Bomb2->setAddress(hCPU->gpr[3], __FUNCTION__);
		Game::GameInstance->Bomb2->changeState(Normal);
	}
	else if (name == "RemoteBombCube")
	{
		Game::GameInstance->BombCube->setAddress(hCPU->gpr[3], __FUNCTION__);
		Game::GameInstance->BombCube->changeState(Normal);
	}
	else if (name == "RemoteBombCube2")
	{
		Game::GameInstance->BombCube2->setAddress(hCPU->gpr[3], __FUNCTION__);
		Game::GameInstance->BombCube2->changeState(Normal);
	}

	int BombType = -1;
	
	for (int i = 0; i < 4; i++)
	{
		if (name == BombChoices[i])
			BombType = i;
	}

	if (BombType == -1)
		return;

	for (int j = 1; j < 33; j++)
	{
		if (!Instances::PlayerList[j]->connected)
			continue;

		BombAccess* Bomb = new BombAccess();

		if (BombType == 0)
			Bomb = Instances::PlayerList[j]->Bomb;
		else if (BombType == 1)
			Bomb = Instances::PlayerList[j]->Bomb2;
		else if (BombType == 2)
			Bomb = Instances::PlayerList[j]->BombCube;
		else if (BombType == 3)
			Bomb = Instances::PlayerList[j]->BombCube2;

		if (Bomb->getStatus() == Processing)
		{
			Bomb->setAddress(hCPU->gpr[3], __FUNCTION__);
			Bomb->changeState(Normal);
			std::stringstream stream;
			stream << "Assigned " << name << " to player " << std::to_string(j) << " at: " << std::hex << hCPU->gpr[3];
			Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
			return;
		}
	}
}

void OnActorErase(PPCInterpreter_t* hCPU)
{

	hCPU->instructionPointer = hCPU->sprNew.LR;
	if (!Game::GameInstance)
		return;

	std::string name = Memory::read_string(Main::baseAddr + hCPU->gpr[3] + 0x10, 100, __FUNCTION__);

	std::vector<std::string> BombChoices = {"CustomRemoteBomb", "CustomRemoteBomb2", "CustomRemoteBombCube", "CustomRemoteBombCube2"};
	std::vector<std::string> RealBombChoices = { "RemoteBomb", "RemoteBomb2", "RemoteBombCube", "RemoteBombCube2" };

	if (name.rfind("Jugador", 0) == 0) {
		int spawnedPlayer = 0;
		if (!TryParseRemotePlayerActor(name, spawnedPlayer))
			return;
		auto player = Instances::PlayerList.find(spawnedPlayer);
		if (player == Instances::PlayerList.end())
			return;
		const bool cleanupActive = stalePlayerCleanupActive.load(std::memory_order_acquire);
		// A direct refresh can complete host-side before Cemu emits its delayed
		// erase callback. Never let that stale callback clear a replacement actor
		// that has already been adopted for the same player slot.
		if (!cleanupActive && player->second->baseAddr != hCPU->gpr[3])
		{
			std::stringstream staleEraseStream;
			staleEraseStream << "Ignored stale erase callback for player " << spawnedPlayer
				<< " at guest address 0x" << std::hex << hCPU->gpr[3] << ".";
			Logging::LoggerService::LogDebug(staleEraseStream.str(), __FUNCTION__);
			return;
		}
		//Instances::PlayerList[spawnedPlayer]->Delete->set(false, __FUNCTION__);
		if (cleanupActive)
		{
			player->second->Status->set(MemoryAccess::Player::DELETE_STATUS, __FUNCTION__);
			stalePlayerEraseCount.fetch_add(1, std::memory_order_relaxed);
		}
		else
			player->second->Status->set(0, __FUNCTION__);
		player->second->SpawnPending.store(false, std::memory_order_release);
		player->second->SpawnCallbackExpected.store(false, std::memory_order_release);
		player->second->InvalidateNativeAnimationControls();
		resetRemoteAnimationDispatch(spawnedPlayer);
		player->second->setAddress(0);
		std::stringstream stream;
		stream << "Player " << spawnedPlayer << " erased.";
		Logging::LoggerService::LogDebug(stream.str(), __FUNCTION__);
	}

	if (name.rfind("Enemy_", 0) == 0)
		Game::GameInstance->EnemyService->UpdateEnemyAddress(Main::baseAddr + hCPU->gpr[3], false);

	if (!started) return;

	if (name == "RemoteBomb")
	{
		Game::GameInstance->Bomb->setAddress(0, __FUNCTION__);
		Game::GameInstance->Bomb->changeState(Cancelled);
	}
	else if (name == "RemoteBomb2")
	{
		Game::GameInstance->Bomb2->setAddress(0, __FUNCTION__);
		Game::GameInstance->Bomb2->changeState(Cancelled);
	}
	else if (name == "RemoteBombCube")
	{
		Game::GameInstance->BombCube->setAddress(0, __FUNCTION__);
		Game::GameInstance->BombCube->changeState(Cancelled);
	}
	else if (name == "RemoteBombCube2")
	{
		Game::GameInstance->BombCube2->setAddress(0, __FUNCTION__);
		Game::GameInstance->BombCube2->changeState(Cancelled);
	}

	int BombType = -1;

	for (int i = 0; i < 4; i++)
	{
		if (name == BombChoices[i])
			BombType = i;
	}

	if (BombType == -1)
		return;

	for (int j = 1; j < 33; j++)
	{
		if (!Instances::PlayerList[j]->connected)
			continue;

		BombAccess* Bomb = new BombAccess();

		if (BombType == 0)
			Bomb = Instances::PlayerList[j]->Bomb;
		else if (BombType == 1)
			Bomb = Instances::PlayerList[j]->Bomb2;
		else if (BombType == 2)
			Bomb = Instances::PlayerList[j]->BombCube;
		else if (BombType == 3)
			Bomb = Instances::PlayerList[j]->BombCube2;

		if (Bomb->BaseAddr == hCPU->gpr[3])
		{
			Bomb->setAddress(0, __FUNCTION__);
			Bomb->changeState(Deallocated);
			std::stringstream stream;
			stream << "Bomb " << name << " of player " << std::to_string(j) << " despawned.";
			Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
			return;
		}
	}

	//for (int i = 0; i < 4; i++)
	//{

	//	if (std::string(name).rfind(BombChoices[i], 0) == 0)
	//	{

	//		for (int j = 0; j < Main::BombSync->BombAvailableAddresses[i].size(); j++)
	//		{

	//			if (Main::BombSync->BombAvailableAddresses[i][j] == hCPU->gpr[4])
	//			{

	//				Main::BombSync->OtherPlayerBombsMutex.lock();
	//				Main::BombSync->BombAvailableAddresses[i].erase(Main::BombSync->BombAvailableAddresses[i].begin() + j);
	//				Main::BombSync->OtherPlayerBombsMutex.unlock();
	//				std::cout << BombChoices[i] << " cleared" << std::endl;

	//			}

	//		}

	//	}

	//	for (int i = 0; i < 4; i++)
	//	{

	//		for (int j = 0; j < RealBombChoices.size(); j++)
	//		{

	//			if (Main::Jugadores[i]->UserBombs[RealBombChoices[j]]->BaseAddress == hCPU->gpr[3])
	//			{

	//				Main::BombSync->BombExplodeMutex.lock();

	//				Main::Jugadores[i]->UserBombs[RealBombChoices[j]]->Exists = false;
	//				Main::Jugadores[i]->UserBombs[RealBombChoices[j]]->BaseAddress = 0;

	//				//std::cout << "Bomb " << RealBombChoices[j] << " for Jugador " << i + 1 << " despawned" << std::endl;

	//				Main::BombSync->BombExplodeMutex.unlock();

	//			}

	//		}

	//	}

	//}

}

void remoteBomb_onAICalc(PPCInterpreter_t* hCPU)
{
	hCPU->instructionPointer = hCPU->sprNew.LR;
	if (!Game::GameInstance)
		return;

	unsigned int aiPtr = hCPU->gpr[3];
	unsigned int actPtr;
	std::vector<std::string> BombChoices = { "CustomRemoteBomb", "CustomRemoteBomb2", "CustomRemoteBombCube", "CustomRemoteBombCube2" };

	memInstance->memory_readMemoryBE(aiPtr, &actPtr, Main::baseAddr);

	std::string name = Memory::read_string(Main::baseAddr + actPtr + 0x10, 50, __FUNCTION__);

	bool explode = Memory::read_bytes(aiPtr + 0x78 + Main::baseAddr, 1, __FUNCTION__)[0] == 0x1 ? true : false;
	bool cancel = Memory::read_bytes(aiPtr + 0x9C + Main::baseAddr, 1, __FUNCTION__)[0] == 0x1 ? true : false;

	if(explode || cancel)
	{
		if (name == "RemoteBomb")
		{
			Game::GameInstance->Bomb->setAddress(0, __FUNCTION__);
			Game::GameInstance->Bomb->changeState(explode ? Exploded : Cancelled); // Assuming it can only explode or cancel, not both
		}
		else if (name == "RemoteBomb2")
		{
			Game::GameInstance->Bomb2->setAddress(0, __FUNCTION__);
			Game::GameInstance->Bomb2->changeState(explode ? Exploded : Cancelled);
		}
		else if (name == "RemoteBombCube")
		{
			Game::GameInstance->BombCube->setAddress(0, __FUNCTION__);
			Game::GameInstance->BombCube->changeState(explode ? Exploded : Cancelled);
		}
		else if (name == "RemoteBombCube2")
		{
			Game::GameInstance->BombCube2->setAddress(0, __FUNCTION__);
			Game::GameInstance->BombCube2->changeState(explode ? Exploded : Cancelled);
		}
	}

	int BombType = -1;

	for (int i = 0; i < 4; i++)
	{
		if (name == BombChoices[i])
			BombType = i;
	}

	if (BombType == -1)
		return;

	for (int j = 1; j < 33; j++)
	{
		if (!Instances::PlayerList[j]->connected)
			continue;

		BombAccess* Bomb = new BombAccess();

		if (BombType == 0)
			Bomb = Instances::PlayerList[j]->Bomb;
		else if (BombType == 1)
			Bomb = Instances::PlayerList[j]->Bomb2;
		else if (BombType == 2)
			Bomb = Instances::PlayerList[j]->BombCube;
		else if (BombType == 3)
			Bomb = Instances::PlayerList[j]->BombCube2;

		if (Bomb->getStatus() == Deallocated)
			continue;

		if (Bomb->BaseAddr == actPtr)
		{
			BombStatus status = Bomb->getStatus();
			if (status == Exploded)
			{
				Memory::write_byte(aiPtr + 0x78 + Main::baseAddr, 0x1, __FUNCTION__);
				Bomb->changeState(Deallocated);
				std::stringstream stream;
				stream << "Bomb " << name << " of player " << std::to_string(j) << " exploded.";
				Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
				Bomb->setAddress(0, __FUNCTION__);
				return;
			}
			else if (status == Cancelled)
			{
				Memory::write_byte(aiPtr + 0x9C + Main::baseAddr, 0x1, __FUNCTION__);
				Bomb->changeState(Deallocated);
				std::stringstream stream;
				stream << "Bomb " << name << " of player " << std::to_string(j) << " erased.";
				Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
				Bomb->setAddress(0, __FUNCTION__);
				return;
			}
		}
	}
}

void remoteBomb_onAICalc2(PPCInterpreter_t* hCPU)
{

	std::vector<std::string> RealBombChoices = { "RemoteBomb", "RemoteBomb2", "RemoteBombCube", "RemoteBombCube2" };

	hCPU->instructionPointer = hCPU->sprNew.LR;
	
	unsigned int aiPtr = hCPU->gpr[3];
	unsigned int actPtr;
	memInstance->memory_readMemoryBE(aiPtr, &actPtr, Main::baseAddr);

	std::string name = Memory::read_string(Main::baseAddr + actPtr + 0x10, 50, __FUNCTION__);

	bool explode = Memory::read_bytes(aiPtr + 0x78 + Main::baseAddr, 1, __FUNCTION__)[0] == 0x1 ? true : false;
	bool cancel = Memory::read_bytes(aiPtr + 0x9C + Main::baseAddr, 1, __FUNCTION__)[0] == 0x1 ? true : false;

	Main::BombSync->BombExplodeMutex.lock();

	for (int i = 0; i < 4; i++)
	{
		if (name.rfind(RealBombChoices[i], 0) == 0)
		{

			if (explode)
			{
				Main::BombSync->BombMutex.lock();
				Main::BombSync->Bombs[name] = 0;
				Main::BombSync->BombMutex.unlock();
			}
			else if (cancel)
			{
				Main::BombSync->BombMutex.lock();
				Main::BombSync->Bombs[name] = -1;
				Main::BombSync->BombMutex.unlock();
			}
		}
	}

	Main::BombSync->BombExplodeMutex.unlock();

	Main::BombSync->BombMutex.lock();
	for (int i = 0; i < Main::BombSync->BombsToExplode.size(); i++)
		if (Main::BombSync->BombsToExplode[i] == actPtr)
		{
			Memory::write_byte(aiPtr + 0x78 + Main::baseAddr, 0x1, __FUNCTION__);
			Main::BombSync->BombsToExplode.erase(Main::BombSync->BombsToExplode.begin() + i);
		}
	
	for (int i = 0; i < Main::BombSync->BombsToClear.size(); i++)
		if (Main::BombSync->BombsToClear[i] == actPtr)
		{
			Memory::write_byte(aiPtr + 0x9C + Main::baseAddr, 0x1, __FUNCTION__);
			Main::BombSync->BombsToClear.erase(Main::BombSync->BombsToClear.begin() + i);
		}
	Main::BombSync->BombMutex.unlock();
}

void timemgr_OnInit(PPCInterpreter_t* hCPU)
{
	hCPU->instructionPointer = hCPU->sprNew.LR;
	if (!Game::GameInstance || !Game::GameInstance->World)
		return;

	Game::GameInstance->World->ReadableTime = new BigEndian<float>(hCPU->gpr[3] + 0x98 + Main::baseAddr, __FUNCTION__);
	Game::GameInstance->World->WritableTime = new BigEndian<float>(hCPU->gpr[3] + 0xA0 + Main::baseAddr, __FUNCTION__);

	//Game::GameInstance->World->ReadableTime->setAddress(hCPU->gpr[3] + 0x98 + Main::baseAddr, __FUNCTION__, true);
	//Game::GameInstance->World->WritableTime->setAddress(hCPU->gpr[3] + 0xA0 + Main::baseAddr, __FUNCTION__, true);

	//std::stringstream stream;
	//stream << "Time offset: " << std::hex << hCPU->gpr[3];

	//Logging::LoggerService::LogDebug(stream.str());
}

void init() {
	osLib_registerHLEFunctionType osLib_registerHLEFunction = (osLib_registerHLEFunctionType)GetProcAddress(GetModuleHandleA("Cemu.exe"), "osLib_registerHLEFunction");
	if (!osLib_registerHLEFunction)
		throw std::runtime_error("Cemu is missing the Milk Bar HLE hooks; run scripts/patch-cemu.sh before building Cemu");
	osLib_registerHLEFunction("spawnactors", "fnCallMain", static_cast<void (*) (PPCInterpreter_t*)>(&mainFn));
	osLib_registerHLEFunction("spawnactors", "ResolveEquipmentActor", static_cast<void (*) (PPCInterpreter_t*)>(&ResolveEquipmentActor));
	osLib_registerHLEFunction("multiplayer", "WeatherSync", static_cast<void (*) (PPCInterpreter_t*)>(&WeatherFn));
	osLib_registerHLEFunction("ukl_actorinterceptor", "OnActorCreate", static_cast<void (*) (PPCInterpreter_t*)>(&OnActorCreate));
	Logging::LoggerService::LogInformation(
		"Equipment synchronization runtime: corrected-controller-polarity-v9.",
		__FUNCTION__);
	// Clear native actor state only after BOTW actually erases the actor. The
	// earlier deleteLater hook can run inside the shared PPC dispatcher and must
	// not recursively mutate lifecycle state before that call returns.
	osLib_registerHLEFunction("ukl_actorinterceptor", "OnActorErase", static_cast<void (*) (PPCInterpreter_t*)>(&OnActorErase));
	osLib_registerHLEFunction("ukl_remotebombaiinterceptor", "OnCalc", &remoteBomb_onAICalc);
	osLib_registerHLEFunction("ukl_timemgrinterceptor", "OnInit", &timemgr_OnInit);
}
