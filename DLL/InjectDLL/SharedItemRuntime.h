#pragma once

// Included after the shared PPC dispatcher declarations in SpawningVariables.h.
// Game calls remain on the actor update thread; the network thread only queues values.

extern bool started;

bool ReadItemPack(uint32_t address, std::vector<ActorSpawnParams::Entry> &entries) {
    uint32_t proc = 0, header = 0;
    if (!address || !Memory::TryReadBigEndian4BytesOffset(address, proc) || proc ||
        !Memory::TryReadBigEndian4BytesOffset(uint64_t(address) + 4, header))
        return false;
    const unsigned count = header >> 16, size = header & 65535;
    if (!count || count > 24 || !size || size > 192)
        return false;
    std::vector<uint8_t> data;
    for (unsigned i = 0; i < size; i += 4) {
        uint32_t word = 0;
        if (!Memory::TryReadBigEndian4BytesOffset(uint64_t(address) + 8 + i, word))
            return false;
        for (int shift = 24; shift >= 0; shift -= 8)
            data.push_back((word >> shift) & 255);
    }
    data.resize(size);
    return ActorSpawnParams::decode(data, count, ReadProbeString, entries);
}

void CaptureSharedItem(const std::string &name, PPCInterpreter_t *hCPU) {
    if (!started || !SharedItems::worldInitialized.load(std::memory_order_acquire) ||
        (name.rfind("Item_", 0) && name.rfind("Weapon_", 0) && name != "Obj_FireWoodBundle"))
        return;
    std::vector<ActorSpawnParams::Entry> params;
    if (!ReadItemPack(hCPU->gpr[7], params))
        return;
    bool inventory = false, rawLife = false, hasLife = false, hasMatrix = false;
    for (auto &p : params) {
        // @PC is the player-equipment creation flag, not a world-drop value.
        // Its following raw-life create uses the same public factory as drops.
        if (p.key == "@PC" && p.type == 3 && p.value.size() == 1 && p.value[0] == 1) {
            std::lock_guard<std::mutex> lock(SharedItems::mutex);
            SharedItems::equipmentRequests[name] = GetTickCount();
            return;
        }
        if (!SharedItems::allowed.count(p.key) &&
            !(p.key == "@I" && p.type == 0 && p.value.size() == 4))
            return;
        if (p.key == "IsPlayerPut")
            inventory = p.value[0] == 1;
        if (p.key == "IsWeaponCreateByRawLife")
            rawLife = p.value[0] == 1;
        if (p.key == "Life")
            hasLife = p.value.size() == 4 &&
                      std::any_of(p.value.begin(), p.value.end(), [](uint8_t b) { return b != 0; });
        if (p.key == "@M")
            hasMatrix = true;
    }
    if (!(inventory || (rawLife && hasLife && hasMatrix)))
        return;
    std::lock_guard<std::mutex> lock(SharedItems::mutex);
    if (!SharedItems::expecting.empty()) {
        auto remote = SharedItems::items.find(SharedItems::expecting);
        if (remote != SharedItems::items.end() && remote->second.name == name)
            return;
    }
    // Raw-life creation is also used by enemy loot. Only capture this path
    // around an inventory pause; otherwise both clients could relay the same loot.
    if (!inventory && (!SharedItems::lastPaused || GetTickCount() - SharedItems::lastPaused > 3000))
        return;
    SharedItems::creator = hCPU->gpr[3];
    SharedItems::heap = hCPU->gpr[5];
    SharedItems::Item item;
    item.name = name;
    item.params = params;
    item.captured = GetTickCount();
    auto equipmentRequest = SharedItems::equipmentRequests.find(name);
    item.equipmentCandidate = equipmentRequest != SharedItems::equipmentRequests.end() &&
        GetTickCount() - equipmentRequest->second < 3000;
    item.owner = Main::playerNumber;
    item.id = std::to_string(GetTickCount()) + "-" + std::to_string(Main::playerNumber) + "-" +
              std::to_string(++SharedItems::sequence);
    if (SharedItems::captures.size() < 256)
        SharedItems::captures.push_back(item);
}

void SharedItemCreated(const std::string &name, uint32_t actor) {
    std::lock_guard<std::mutex> lock(SharedItems::mutex);
    if (!SharedItems::expecting.empty()) {
        auto it = SharedItems::items.find(SharedItems::expecting);
        if (it != SharedItems::items.end() && it->second.name == name) {
            SharedItems::bindActor(it->second, actor);
            TestTelemetry::emit(
                "item_spawned", -1,
                [&](auto &w) {
                    w.Key("id");
                    w.String(it->first.c_str());
                    w.Key("name");
                    w.String(name.c_str());
                    w.Key("actor");
                    w.Uint(actor);
                },
                true);
            SharedItems::expecting.clear();
            return;
        }
    }
    for (auto &item : SharedItems::captures)
        if (!item.actor && item.name == name && GetTickCount() - item.captured < 3000) {
            item.actor = actor;
            return;
        }
}

void SharedItemErased(uint32_t actor) {
    std::lock_guard<std::mutex> lock(SharedItems::mutex);
    SharedItems::captures.erase(std::remove_if(SharedItems::captures.begin(),
                                               SharedItems::captures.end(),
                                               [&](auto &item) { return item.actor == actor; }),
                                SharedItems::captures.end());
    for (auto &pair : SharedItems::items)
        if (pair.second.actor == actor) {
            // Erase alone also happens for distance unloading. Only an explicit
            // pickup/delete reason is allowed to publish a network tombstone.
            if (!pair.second.removed && pair.second.live && pair.second.deleteReason == 0) {
                auto delta = pair.second.position;
                const double distance = std::pow(delta.x() - SharedItems::localPosition.x(), 2) +
                                        std::pow(delta.y() - SharedItems::localPosition.y(), 2) +
                                        std::pow(delta.z() - SharedItems::localPosition.z(), 2);
                // Preserve distance-unloaded actors; a nearby normal deletion is a
                // consumed/destroyed pickup. Publish only after the erase completes.
                if (distance < 64 &&
                    Game::GameInstance->WorldReady.load(std::memory_order_acquire)) {
                    pair.second.removed = true;
                    pair.second.sent = false;
                }
            }
            pair.second.actor = 0;
            pair.second.live = false;
            pair.second.liveSince = 0;
            if (!pair.second.removed) SharedItems::spawnQueue.push_back(pair.first);
            TestTelemetry::emit(
                "item_erased", -1,
                [&](auto &w) {
                    w.Key("id");
                    w.String(pair.first.c_str());
                    w.Key("removed");
                    w.Bool(pair.second.removed);
                },
                true);
        }
}

void PublishSharedItems(DTO::ClientDTO *data) {
    // Loading a save creates equipment through the same raw-life factory.
    // Arm capture only after a playable world was observed; retain that state
    // through inventory pauses, where actual drops are requested.
    const bool ready = Game::GameInstance->WorldReady.load(std::memory_order_acquire);
    auto position = data->PlayerData->Position;
    if (ready) SharedItems::worldInitialized.store(true, std::memory_order_release);
    else if (!std::isfinite(position.x()) || !std::isfinite(position.y()) ||
        !std::isfinite(position.z()) ||
        std::abs(position.x()) + std::abs(position.y()) + std::abs(position.z()) < 10)
        SharedItems::worldInitialized.store(false, std::memory_order_release);
    if (SharedItems::worldInitialized.load(std::memory_order_acquire) &&
        Game::GameInstance->IsPaused()) {
        std::lock_guard<std::mutex> lock(SharedItems::mutex);
        SharedItems::lastPaused = GetTickCount();
    }
    if (Game::GameInstance->WorldReady.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(SharedItems::mutex);
        SharedItems::localMap = data->PlayerData->Location.Map;
        SharedItems::localSection = data->PlayerData->Location.Section;
        SharedItems::localPosition = data->PlayerData->Position;
        for (auto it = SharedItems::captures.begin(); it != SharedItems::captures.end();) {
            if (!it->actor) {
                if (GetTickCount() - it->captured > 3000)
                    it = SharedItems::captures.erase(it);
                else
                    ++it;
                continue;
            }
            if (GetTickCount() - it->captured < 1000) {
                ++it;
                continue;
            }
            // The inventory factory also creates equipped weapon children.
            // Wii U v208 BaseProc::getConnectedCalcParent (0x0378af6c)
            // reads +0x68; setConnectedCalcParent queues its new parent at
            // +0x70. Neither a bound child nor a pending child is a world drop.
            uint32_t parent = 0, pendingParent = 0;
            if (!Memory::TryReadBigEndian4BytesOffset(uint64_t(it->actor) + 0x68, parent) ||
                !Memory::TryReadBigEndian4BytesOffset(uint64_t(it->actor) + 0x70, pendingParent)) {
                ++it;
                continue;
            }
            const bool equipped = it->equipmentCandidate &&
                SharedItems::equippedResourceMatches(it->name, data->PlayerData->Equipment);
            if (parent || pendingParent || equipped) {
                TestTelemetry::emit("item_attached_skipped", -1, [&](auto& w) {
                    w.Key("name"); w.String(it->name.c_str());
                    w.Key("actor"); w.Uint(it->actor);
                    w.Key("parent"); w.Uint(parent);
                    w.Key("pending_parent"); w.Uint(pendingParent);
                    w.Key("equipped"); w.Bool(equipped);
                }, true);
                it = SharedItems::captures.erase(it);
                continue;
            }
            std::vector<uint8_t> matrix;
            for (unsigned i = 0; i < 48; i += 4) {
                uint32_t word = 0;
                if (!Memory::TryReadBigEndian4BytesOffset(uint64_t(it->actor) + 0x1f8 + i, word)) {
                    matrix.clear();
                    break;
                }
                for (int shift = 24; shift >= 0; shift -= 8)
                    matrix.push_back((word >> shift) & 255);
            }
            auto m = std::find_if(it->params.begin(), it->params.end(),
                                  [](auto &p) { return p.key == "@M"; });
            if (matrix.size() == 48) {
                if (m == it->params.end())
                    it->params.push_back({"@M", 7, matrix});
                else
                    m->value = matrix;
            }
            // @I selects a local carry-box initialization path. A world pickup
            // on another machine must use normal initialization instead.
            it->params.erase(std::remove_if(it->params.begin(), it->params.end(),
                                            [](auto &p) { return p.key == "@I"; }),
                             it->params.end());
            it->map = data->PlayerData->Location.Map;
            it->section = data->PlayerData->Location.Section;
            if (SharedItems::validate(*it)) {
                SharedItems::items.emplace(it->id, *it);
                TestTelemetry::emit(
                    "item_published", -1,
                    [&](auto &w) {
                        w.Key("id");
                        w.String(it->id.c_str());
                        w.Key("name");
                        w.String(it->name.c_str());
                        w.Key("actor");
                        w.Uint(it->actor);
                    },
                    true);
            }
            it = SharedItems::captures.erase(it);
        }
        for (auto &pair : SharedItems::items)
            if (pair.second.actor && !pair.second.removed) {
                const auto actor = pair.second.actor;
                float pos[3] = {};
                bool valid = true;
                for (int j = 0; j < 3; ++j) {
                    uint32_t word = 0;
                    if (!Memory::TryReadBigEndian4BytesOffset(uint64_t(actor) + 0x1f8 + 12 + 16 * j,
                                                              word)) {
                        valid = false;
                        break;
                    }
                    memcpy(&pos[j], &word, 4);
                    if (!std::isfinite(pos[j]))
                        valid = false;
                }
                const double radiusSquared = double(pos[0])*pos[0] + double(pos[1])*pos[1] + double(pos[2])*pos[2];
                if (radiusSquared < 100 || radiusSquared > 1e12) valid = false;
                if (valid) {
                    if (!pair.second.liveSince) pair.second.liveSince = GetTickCount();
                    pair.second.live = true;
                    pair.second.position = Vec3f(pos);
                    auto &item = pair.second;
                    // Keep one update in flight per item; otherwise continuous
                    // motion could make every acknowledgement obsolete.
                    if (SharedItems::revisionsEnabled && !item.remote && item.sent &&
                        GetTickCount() - item.lastPosePublished >= 100 &&
                        item.revision < UINT32_MAX) {
                        auto matrix = std::find_if(item.params.begin(), item.params.end(),
                            [](auto &p) { return p.key == "@M"; });
                        if (matrix != item.params.end()) {
                            float distance = 0;
                            for (unsigned j = 0; j < 3; ++j) {
                                uint32_t bits = 0;
                                for (unsigned k = 0; k < 4; ++k)
                                    bits = (bits << 8) | matrix->value[12 + 16*j + k];
                                float previous; memcpy(&previous, &bits, 4);
                                distance += (previous-pos[j])*(previous-pos[j]);
                            }
                            if (distance >= .0025f) {
                                for (unsigned j = 0; j < 3; ++j) {
                                    uint32_t bits; memcpy(&bits, &pos[j], 4);
                                    for (unsigned k = 0; k < 4; ++k)
                                        matrix->value[12 + 16*j + k] = (bits >> (24-8*k)) & 255;
                                }
                                ++item.revision;
                                item.sent = false;
                                item.lastPosePublished = GetTickCount();
                                TestTelemetry::emit("item_pose_published", -1, [&](auto &w) {
                                    w.Key("id"); w.String(item.id.c_str());
                                    w.Key("revision"); w.Uint(item.revision);
                                    TestTelemetry::position(w, item.position);
                                }, true);
                            }
                        }
                    }
                }
                if (valid)
                    TestTelemetry::emit("item_live", int(actor), [&](auto &w) {
                        w.Key("id");
                        w.String(pair.first.c_str());
                        w.Key("name");
                        w.String(pair.second.name.c_str());
                        w.Key("actor");
                        w.Uint(actor);
                        w.Key("revision"); w.Uint(pair.second.revision);
                        TestTelemetry::position(w, Vec3f(pos));
                    });
            }
    }
    data->SharedItems = SharedItems::outgoing();
}

// Apply received position corrections at the actor-update hook, never from
// the socket thread. This is the single-body path already used for arrows.
// Shift both swept centers with the transform so the next Havok integration
// does not restore the old location. Preserve their time components.
bool MoveSharedItemBody(SharedItems::Item &item, const float desired[3], uint64_t &body) {
    if (Memory::read_string(Main::baseAddr + item.actor + 0x10,
        item.name.size()+1, __FUNCTION__) != item.name) return false;
    if (!Memory::TryReadPointers(item.actor, {0x3A0, 0x2C, 0, 0x14, 0, 0x5C}, body)) return false;
    float current[3][3] = {};
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(desired[axis]) || std::abs(desired[axis]) > 1000000) return false;
        for (unsigned vector = 0; vector < 3; ++vector) {
            uint32_t bits = 0;
            if (!Memory::TryReadBigEndian4BytesOffset(body + 0x120 + vector*16 + axis*4, bits))
                return false;
            memcpy(&current[vector][axis], &bits, 4);
            if (!std::isfinite(current[vector][axis]) ||
                std::abs(current[vector][axis]) > 1000000) return false;
        }
    }
    // Reject an uninitialized body or an incompatible motion layout.
    for (unsigned vector = 1; vector < 3; ++vector)
        for (unsigned axis = 0; axis < 3; ++axis)
            if (std::abs(current[vector][axis] - current[0][axis]) > 100) return false;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const float delta = desired[axis] - current[0][axis];
        for (unsigned vector = 0; vector < 3; ++vector)
            Memory::write_bigEndianFloat(Main::baseAddr + body + 0x120 + vector*16 + axis*4,
                current[vector][axis] + delta, __FUNCTION__);
    }
    return true;
}

void ApplySharedItemPositions() {
    if (!SharedItems::revisionsEnabled) return;
    for (auto &pair : SharedItems::items) {
        auto &item = pair.second;
        if (item.removed || !item.actor || item.map != SharedItems::localMap ||
            (item.map != "MainField" && item.section != SharedItems::localSection)) continue;
        // Only the isolated harness opts into moving an actual dropped wood
        // body. Subsequent publication and peer readback use production paths.
        if (!item.remote && item.name == "Obj_FireWoodBundle" && item.liveSince &&
            !item.motionFixtureApplied && GetTickCount() - item.liveSince >= 2000 &&
            TestTelemetry::enabled() && std::getenv("HYRULE_TEST_ITEM_MOTION")) {
            float desired[3] = {item.position.x()+2, item.position.y()+1, item.position.z()};
            uint64_t body = 0;
            if (MoveSharedItemBody(item, desired, body)) {
                item.motionFixtureApplied = true;
                TestTelemetry::emit("item_motion_fixture", -1, [&](auto &w) {
                    w.Key("id"); w.String(item.id.c_str());
                    TestTelemetry::position(w, item.position, "before");
                    TestTelemetry::position(w, Vec3f(desired), "requested");
                }, true);
            }
        }
        // The peer's independent physics may drift even after the owner has
        // stopped moving. Keep correcting the replica against the latest pose,
        // including revision zero, rather than requiring another owner change.
        if (!item.remote || !item.live || GetTickCount() - item.captured < 100 ||
            (item.appliedRevision >= item.revision &&
             GetTickCount() - item.lastPoseApplied < 100)) continue;
        auto matrix = std::find_if(item.params.begin(), item.params.end(),
            [](auto &p) { return p.key == "@M"; });
        if (matrix == item.params.end()) continue;
        float desired[3];
        for (unsigned axis = 0; axis < 3; ++axis) {
            uint32_t bits = 0;
            for (unsigned k = 0; k < 4; ++k)
                bits = (bits << 8) | matrix->value[12+16*axis+k];
            memcpy(&desired[axis], &bits, 4);
        }
        uint64_t body = 0;
        if (!MoveSharedItemBody(item, desired, body)) continue;
        const bool newRevision = item.appliedRevision < item.revision;
        item.appliedRevision = item.revision;
        item.lastPoseApplied = GetTickCount();
        TestTelemetry::emit("item_pose_applied", int(item.actor), [&](auto &w) {
            w.Key("id"); w.String(item.id.c_str());
            w.Key("revision"); w.Uint(item.revision);
            w.Key("body"); w.Uint64(body);
            TestTelemetry::position(w, Vec3f(desired));
        }, newRevision);
    }
}

bool setupSharedItem(TransferableData &trns, uint32_t start, uint32_t end) {
    if (!Game::GameInstance->WorldReady.load(std::memory_order_acquire))
        return false;
    std::lock_guard<std::mutex> lock(SharedItems::mutex);
    ApplySharedItemPositions();
    while (!SharedItems::deleteQueue.empty()) {
        const auto id = SharedItems::deleteQueue.front();
        SharedItems::deleteQueue.pop_front();
        const auto it = SharedItems::items.find(id);
        if (it == SharedItems::items.end() || !it->second.actor || !it->second.removed)
            continue;
        trns.f_r3 = it->second.actor;
        trns.f_r4 = 0x7f;
        trns.f_r5 = trns.f_r6 = trns.f_r7 = trns.f_r8 = trns.f_r9 = trns.f_r10 = 0;
        trns.fnAddr = 0x0378a374;
        trns.enabled = true;
        trns.interceptRegisters = false;
        trns.dispatchState = 1;
        pending_spawn_sequence = ++spawn_request_sequence;
        pending_spawn_name = it->second.name;
        pending_dispatch_kind = PendingDispatchKind::ItemDelete;
        return true;
    }
    if (!SharedItems::expecting.empty() && GetTickCount() - SharedItems::expectedSince > 5000) {
        auto failed = SharedItems::items.find(SharedItems::expecting);
        if (failed != SharedItems::items.end() && !failed->second.removed &&
            failed->second.attempts < 3)
            SharedItems::spawnQueue.push_back(failed->first);
        TestTelemetry::emit(
            "item_spawn_timeout", -1,
            [&](auto &w) {
                w.Key("id");
                w.String(SharedItems::expecting.c_str());
            },
            true);
        SharedItems::expecting.clear();
    }
    if (!SharedItems::expecting.empty() || SharedItems::spawnQueue.empty() ||
        !actor_spawn_template_ready)
        return false;
    const auto id = SharedItems::spawnQueue.front();
    auto it = SharedItems::items.find(id);
    if (it == SharedItems::items.end() || it->second.removed || it->second.actor) {
        SharedItems::spawnQueue.pop_front();
        return false;
    }
    auto &item = it->second;
    if (item.map != SharedItems::localMap ||
        (item.map != "MainField" && item.section != SharedItems::localSection)) {
        SharedItems::spawnQueue.pop_front();
        SharedItems::spawnQueue.push_back(id);
        return false;
    }
    // Keep distance-unloaded identities pending until their scene and nearby
    // actor area are active again. Do not continuously respawn far-away drops.
    auto matrix = std::find_if(item.params.begin(), item.params.end(),
        [](auto &p) { return p.key == "@M"; });
    if (matrix == item.params.end()) return false;
    double distance = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        uint32_t bits = 0;
        for (unsigned k = 0; k < 4; ++k)
            bits = (bits << 8) | matrix->value[12+16*axis+k];
        float coordinate; memcpy(&coordinate, &bits, 4);
        const double delta = coordinate - SharedItems::localPosition[axis];
        distance += delta*delta;
    }
    if (distance > 10000) {
        SharedItems::spawnQueue.pop_front();
        SharedItems::spawnQueue.push_back(id);
        return false;
    }
    constexpr unsigned storage = 1024;
    if (end - start < storage)
        return false;
    uint32_t base = trns.ringPtr;
    if (uint64_t(base) + storage > end)
        base = start;
    std::vector<uint8_t> bytes(storage, 0);
    auto putWord = [&](size_t at, uint32_t v) {
        for (int i = 3; i >= 0; --i) {
            bytes[at + i] = v & 255;
            v >>= 8;
        }
    };
    size_t dataAt = 8, keyAt = 256, nameAt = 896;
    for (auto &p : item.params) {
        putWord(dataAt, base + keyAt);
        dataAt += 4;
        bytes[dataAt++] = p.type;
        std::copy(p.value.begin(), p.value.end(), bytes.begin() + dataAt);
        dataAt += p.value.size();
        if (keyAt + p.key.size() + 1 > nameAt)
            return false;
        std::copy(p.key.begin(), p.key.end(), bytes.begin() + keyAt);
        keyAt += p.key.size() + 1;
    }
    putWord(4, (uint32_t(item.params.size()) << 16) | uint32_t(dataAt - 8));
    std::copy(item.name.begin(), item.name.end(), bytes.begin() + nameAt);
    Memory::write_bytes(Main::baseAddr + base, bytes, __FUNCTION__);
    trns.f_r3 = actor_spawn_template_r3;
    trns.f_r4 = base + nameAt;
    trns.f_r5 = actor_spawn_template_r5;
    trns.f_r6 = 0;
    trns.f_r7 = base;
    trns.f_r8 = 0;
    trns.f_r9 = 2;
    trns.f_r10 = 0;
    trns.fnAddr = 0x037b5e8c;
    trns.enabled = true;
    trns.interceptRegisters = false;
    trns.dispatchState = 1;
    trns.ringPtr = base + storage;
    if (trns.ringPtr >= int(end))
        trns.ringPtr = start;
    pending_spawn_sequence = ++spawn_request_sequence;
    pending_spawn_name = item.name;
    pending_dispatch_kind = PendingDispatchKind::Spawn;
    SharedItems::expecting = id;
    SharedItems::expectedSince = GetTickCount();
    ++item.attempts;
    SharedItems::spawnQueue.pop_front();
    return true;
}

void OnSharedItemDeleteLater(PPCInterpreter_t *hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;
    std::lock_guard<std::mutex> lock(SharedItems::mutex);
    for (auto &item : SharedItems::captures)
        if (item.actor == hCPU->gpr[3]) {
            TestTelemetry::emit(
                "item_capture_delete", -1,
                [&](auto &w) {
                    w.Key("name");
                    w.String(item.name.c_str());
                    w.Key("reason");
                    w.Uint(hCPU->gpr[4]);
                    w.Key("stack");
                    w.StartArray();
                    for (unsigned off = 0; off < 32; off += 4) {
                        uint32_t word = 0;
                        Memory::TryReadBigEndian4BytesOffset(uint64_t(hCPU->gpr[1]) + off, word);
                        w.Uint(word);
                    }
                    w.EndArray();
                },
                true);
        }
    for (auto &pair : SharedItems::items)
        if (pair.second.actor == hCPU->gpr[3]) {
            pair.second.deleteReason = hCPU->gpr[4];
            TestTelemetry::emit(
                "item_delete_request", -1,
                [&](auto &w) {
                    w.Key("id");
                    w.String(pair.first.c_str());
                    w.Key("reason");
                    w.Uint(hCPU->gpr[4]);
                    w.Key("stack");
                    w.StartArray();
                    for (unsigned off = 0; off < 32; off += 4) {
                        uint32_t word = 0;
                        Memory::TryReadBigEndian4BytesOffset(uint64_t(hCPU->gpr[1]) + off, word);
                        w.Uint(word);
                    }
                    w.EndArray();
                },
                true);
        }
}
