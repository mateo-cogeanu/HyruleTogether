#pragma once
#include <algorithm>
#include <limits>

namespace DataTypes {
// Negotiated in the JSON handshake; older servers retain absolute-health mode.
inline bool EnemyDamageDeltas = false;
class EnemyCombat {
    int observed = -1, authoritative = -1, initial = -1, damage = 0;
    bool initialPending = false;
public:
    static constexpr int MaxHealth = 1000000;
    void receive(int health) {
        if (health >= 0 && health <= MaxHealth)
            authoritative = authoritative < 0 ? health : std::min(authoritative, health);
    }
    int observe(int health) {
        if (health < 0 || health > MaxHealth) return -1;
        if (observed < 0) {
            initial = health;
            initialPending = true;
        } else if (health < observed) {
            damage = std::min(MaxHealth, damage + observed - health);
        }
        // Record the value we write, so applying remote damage cannot echo as a hit.
        observed = authoritative < 0 ? health : std::min(health, authoritative);
        return observed;
    }
    template<class TryWrite> int synchronize(int health, TryWrite write) {
        for (int retry = 0; retry < 4; ++retry) {
            EnemyCombat next = *this;
            const int effective = next.observe(health);
            if (effective < 0) return -1;
            // A failed conditional write returns the newer game value. Retry
            // from the original accounting state so the intervening hit counts.
            if (effective == health || write(health, effective)) {
                *this = next;
                return effective;
            }
        }
        return -1; // Contention leaves all accounting pending for the next poll.
    }
    bool pending() const { return initialPending || damage > 0; }
    int takeUpdate() {
        if (initialPending) { initialPending = false; return initial; }
        const int delta = damage;
        damage = 0;
        return std::numeric_limits<int>::min() + delta;
    }
};
}
