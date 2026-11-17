#pragma once
#include <span>
#include "Weapons.hpp"

namespace warzone::giga
{
    constexpr float WALK_SPEED = 12, RUN_SPEED = 38;
    constexpr float JUMP_SPEED = 24, GRAVITY = 24;
    constexpr float NUKE_RADIUS = 400, NUKE_DAMAGE = 500000, COOLDOWN = 8;
    constexpr float PICKUP_RANGE = 18;

    inline float damage(bool immortal, float incoming) { return immortal ? 0.0f : std::max(0.0f, incoming); }
    inline float nukeDamage(float distance) { return splash(NUKE_DAMAGE, NUKE_RADIUS, distance); }
    inline bool ready(float cooldown) { return cooldown <= 0; }
    inline float tick(float cooldown, float dt) { return std::max(0.0f, cooldown - std::max(0.0f, dt)); }
    inline V3 throwVelocity(V3 aim, float speed = 110) { return norm(aim) * speed + V3{0, 20, 0}; }
    inline V3 carryOffset(float halfHeight) { return {0, 2.8f + halfHeight, 0}; }

    struct Target
    {
        V3 pos;
        bool enemy = true, alive = true, available = true;
    };

    inline int pick(V3 at, std::span<const Target> targets)
    {
        int best = -1;
        float nearest = PICKUP_RANGE * PICKUP_RANGE;
        for (int i = 0; i < static_cast<int>(targets.size()); ++i)
        {
            const auto& target = targets[i];
            const V3 delta = target.pos - at;
            const float distanceSquared = dot(delta, delta);
            if (target.enemy && target.alive && target.available && distanceSquared <= nearest && (best < 0 || distanceSquared < nearest))
            {
                best = i;
                nearest = distanceSquared;
            }
        }
        return best;
    }
}
