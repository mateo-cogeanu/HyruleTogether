#pragma once

#include <cstdint>
#include <map>
#include <vector>

struct QueueAnimation
{
    int playerNumber;
    uint32_t actorAddress;
    uint32_t animation;
};

struct CompletedAnimation
{
    uint32_t actorAddress;
    uint32_t animation;
};

// Caller holds queue_mutex. Always replace a waiting request before consulting
// the completed cache: the latest snapshot can return to the previous AS while
// a different AS is still waiting to run.
inline void queueAnimationUpdate(std::vector<QueueAnimation>& queue,
    const std::map<int, CompletedAnimation>& completed, QueueAnimation update)
{
    for (auto& waiting : queue)
    {
        if (waiting.playerNumber == update.playerNumber)
        {
            waiting = update;
            return;
        }
    }
    const auto previous = completed.find(update.playerNumber);
    if (previous != completed.end() &&
        previous->second.actorAddress == update.actorAddress &&
        previous->second.animation == update.animation)
        return;
    queue.push_back(update);
}

// The cached AS stops describing the actor as soon as another AS is submitted.
// Invalidate it before releasing queue_mutex so snapshots received during the
// PPC call can restore the previous AS rather than being discarded as repeats.
inline void beginAnimationDispatch(std::map<int, CompletedAnimation>& completed,
    int playerNumber)
{
    completed.erase(playerNumber);
}
