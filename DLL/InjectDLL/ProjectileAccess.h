#pragma once

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

#include "ProjectileDTO.h"
#include "Vec3fBE.h"
#include "QuaternionBE.h"
#include "TestTelemetry.h"

namespace DataTypes
{
	class ProjectileAccess
	{
	public:
		enum class State : byte { Inactive, Processing, Active, Cancelled };

		ProjectileDTO Get(const char* caller)
		{
			std::lock_guard<std::mutex> lock(Mutex);
			ValidateBoundActorLocked(caller);
			ProjectileDTO result;
			result.Id = Id;
			result.Type = Type;
			result.Active = CurrentState == State::Active && BaseAddr != 0;
			if (result.Active)
			{
				result.Position = Position.get(caller);
				result.Rotation = Rotation.get(caller);
				float lengthSquared = 0;
				for (int axis = 0; axis < 3; ++axis) lengthSquared += result.Position[axis]*result.Position[axis];
				if (!std::isfinite(lengthSquared) || lengthSquared < 1 || lengthSquared > 1e12f) {
					RetiredRemoteId = Id;
					SetAddressLocked(0, caller);
					CurrentState = State::Inactive;
					result.Active = false;
					result.Position = Vec3f();
					result.Rotation = Quaternion();
				}
			}
			return result;
		}

		bool BeginLocal(uint64_t actorAddress, byte type, const std::string& actorName,
			const char* caller, Vec3f ownerPosition)
		{
			std::lock_guard<std::mutex> lock(Mutex);
			if (CurrentState == State::Active && BaseAddr == actorAddress)
				return false;
			// The body can exist before BOTW sets its world transform. Check the
			// ownership radius before publishing a generation or replacing a shot.
			uint64_t body = 0;
			if (!Memory::TryReadPointers(actorAddress, {0x3A0, 0x2C, 0, 0x14, 0, 0x5C}, body, false))
				return false;
			float distanceSquared = 0;
			for (int axis = 0; axis < 3; ++axis) {
				uint32_t bits = 0; float coordinate = 0;
				if (!Memory::TryReadBigEndian4BytesOffset(body + 0x120 + axis*4, bits)) return false;
				memcpy(&coordinate, &bits, sizeof(coordinate));
				const float delta = coordinate - ownerPosition[axis];
				distanceSquared += delta*delta;
			}
			if (!std::isfinite(distanceSquared) || distanceSquared > 144.0f)
				return false;
			// Track the newest shot. Older local arrows remain normal BOTW actors;
			// their remote replicas likewise continue under BOTW physics after the
			// next generation takes over transform streaming.
			SetAddressLocked(actorAddress, caller);
			if (BaseAddr == 0)
				return false;
			Id = NextLocalId.fetch_add(1, std::memory_order_relaxed);
			if (Id <= 0)
			{
				NextLocalId.store(2, std::memory_order_relaxed);
				Id = 1;
			}
			Type = type;
			ExpectedName = actorName;
			CurrentState = State::Active;
			return true;
		}

		void EndLocal(uint64_t actorAddress)
		{
			std::lock_guard<std::mutex> lock(Mutex);
			if (BaseAddr != actorAddress)
				return;
			SetAddressLocked(0, __FUNCTION__);
			CurrentState = State::Inactive;
		}

		void EndRemote(uint64_t actorAddress)
		{
			std::lock_guard<std::mutex> lock(Mutex);
			if (BaseAddr != actorAddress)
				return;
			RetiredRemoteId = Id;
			SetAddressLocked(0, __FUNCTION__);
			CurrentState = State::Inactive;
		}

		bool PrepareRemote(const ProjectileDTO& projectile, const std::string& actorName)
		{
			std::lock_guard<std::mutex> lock(Mutex);
			if (projectile.Id <= 0 || !projectile.Active)
				return false;
			if (projectile.Id == RetiredRemoteId)
				return false;
			if (Id == projectile.Id &&
				(CurrentState == State::Processing || CurrentState == State::Active))
				return false;
			Id = projectile.Id;
			Type = projectile.Type;
			ExpectedName = actorName;
			PendingPosition = projectile.Position;
			PendingRotation = projectile.Rotation;
			SetAddressLocked(0, __FUNCTION__);
			CurrentState = State::Processing;
			return true;
		}

		bool TryAssignRemote(const std::string& actorName, int generation,
			uint64_t actorAddress, const char* caller, bool& stale)
		{
			std::lock_guard<std::mutex> lock(Mutex);
			stale = false;
			if (Id != generation || ExpectedName != actorName ||
				(CurrentState != State::Processing && CurrentState != State::Cancelled))
			{
				stale = true;
				return true;
			}
			if (CurrentState == State::Cancelled)
			{
				stale = true;
				ExpectedName.clear();
				return true;
			}
			SetAddressLocked(actorAddress, caller);
			if (BaseAddr == 0)
			{
				CurrentState = State::Cancelled;
				stale = true;
				return true;
			}
			Position.set(PendingPosition, caller);
			Rotation.set(PendingRotation, caller);
			CurrentState = State::Active;
			return true;
		}

		void UpdateRemote(const ProjectileDTO& projectile, const char* caller, int slot = -1)
		{
			std::lock_guard<std::mutex> lock(Mutex);
			if (projectile.Id != Id)
				return;
			ValidateBoundActorLocked(caller);
			if (!projectile.Active)
			{
				if (CurrentState == State::Processing)
					CurrentState = State::Cancelled;
				else if (CurrentState == State::Active)
				{
					Position.set(Vec3f(0.0f, -10000.0f, 0.0f), caller);
					SetAddressLocked(0, caller);
					CurrentState = State::Inactive;
				}
				return;
			}
			PendingPosition = projectile.Position;
			PendingRotation = projectile.Rotation;
			if (CurrentState == State::Active && BaseAddr != 0)
			{
				Position.set(projectile.Position, caller);
				Rotation.set(projectile.Rotation, caller);
				TestTelemetry::emit("projectile_applied", slot, [&](auto& json) {
					json.Key("arrow_id"); json.Int(Id);
					json.Key("arrow_type"); json.Int(Type);
					json.Key("actor"); json.Uint64(BaseAddr);
					TestTelemetry::position(json, Position.get(caller), "arrow_position");
				});
			}
		}

		uint64_t Address() const
		{
			std::lock_guard<std::mutex> lock(Mutex);
			return BaseAddr;
		}

		int Generation() const
		{
			std::lock_guard<std::mutex> lock(Mutex);
			return Id;
		}

		void Reset(const char* caller, bool hideActor)
		{
			std::lock_guard<std::mutex> lock(Mutex);
			if (hideActor && CurrentState == State::Active && BaseAddr != 0)
				Position.set(Vec3f(0.0f, -10000.0f, 0.0f), caller);
			SetAddressLocked(0, caller);
			Id = 0;
			RetiredRemoteId = 0;
			Type = 0;
			ExpectedName.clear();
			CurrentState = State::Inactive;
		}

	private:
		void ValidateBoundActorLocked(const char* caller)
		{
			if (CurrentState != State::Active || BaseAddr == 0) return;
			uint64_t body = 0;
			if (!Memory::TryReadPointers(BaseAddr, {0x3A0, 0x2C, 0, 0x14, 0, 0x5C}, body, true) ||
				body != BodyAddr || Memory::read_string(Memory::getBaseAddress() + BaseAddr + 0x10,
					ExpectedName.size() + 1, caller) != ExpectedName) {
				RetiredRemoteId = Id;
				SetAddressLocked(0, caller);
				CurrentState = State::Inactive;
			}
		}

		void SetAddressLocked(uint64_t addr, const char* caller)
		{
			BaseAddr = 0;
			BodyAddr = 0;
			if (addr == 0)
			{
				Position.setAddress(0, caller);
				Rotation.setAddress(0, caller);
				return;
			}
			uint64_t transformAddress = 0;
			// Wii U v208: Physics::InstanceSet -> first RigidBodySet -> first
			// RigidBody -> hkpRigidBody. 0x4C is a resource-handle array;
			// 0x50 is the character controller and is null for arrows.
			if (!Memory::TryReadPointers(addr, {0x3A0, 0x2C, 0, 0x14, 0, 0x5C}, transformAddress, true))
				return;
			// Havok motion-state transform translation and current swept rotation.
			Position.setAddress(transformAddress + 0x120, caller, true);
			Rotation.setAddress(transformAddress + 0x160, caller, false);
			BodyAddr = transformAddress;
			BaseAddr = addr;
		}

		mutable std::mutex Mutex;
		Vec3fBE Position{0, "ProjectilePosition"};
		QuaternionBE Rotation{0, "ProjectileRotation"};
		Vec3f PendingPosition;
		Quaternion PendingRotation;
		uint64_t BaseAddr = 0;
		uint64_t BodyAddr = 0;
		int Id = 0;
		int RetiredRemoteId = 0;
		byte Type = 0;
		State CurrentState = State::Inactive;
		std::string ExpectedName;
		inline static std::atomic<int> NextLocalId{1};
	};
}
