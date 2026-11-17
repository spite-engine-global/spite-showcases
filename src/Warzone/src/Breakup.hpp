 
 
 
 
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Core.hpp"
#include "Match.hpp"

namespace warzone
{
    enum class ThrownPhase : uint8_t
    {
        Flying,
        Resting,
        Sinking,
        Gone
    };

    struct PieceParams
    {
        float bounce = 0.3f;        
        float friction = 0.35f;     
        float restSpeed = 1.5f;     
        int maxBounces = 6;
        float restSeconds = 40.0f;  
        float sinkSeconds = 3.0f;   
        float sinkDepth = 2.5f;
    };

    struct ThrownState
    {
        V3 pos, vel;
        float radius = 1.0f;  
        float age = 0.0f, phaseAge = 0.0f;
        float spin = 1.0f;    
        int bounces = 0;
        ThrownPhase phase = ThrownPhase::Flying;
    };

     
    template <class G> bool stepPiece(ThrownState& p, float dt, G&& ground, const PieceParams& k = {})
    {
        p.age += dt;
        p.phaseAge += dt;
        switch (p.phase)
        {
        case ThrownPhase::Flying:
        {
            p.vel.y -= 9.81f * dt;
            p.vel = p.vel * (1.0f - 0.02f * dt);  
            p.pos = p.pos + p.vel * dt;
            const float floor = ground(p.pos.x, p.pos.z) + p.radius;
            if (p.pos.y > floor)
                return false;
            p.pos.y = floor;
            if (p.vel.y < 0.0f)
                p.vel.y = -p.vel.y * k.bounce;
            p.vel.x *= k.friction;
            p.vel.z *= k.friction;
            p.spin *= 0.45f;
            ++p.bounces;
            if (len(p.vel) < k.restSpeed || p.bounces >= k.maxBounces)
            {
                p.vel = {};
                p.spin = 0.0f;
                p.phase = ThrownPhase::Resting;
                p.phaseAge = 0.0f;
            }
            return true;
        }
        case ThrownPhase::Resting:
            p.pos.y = ground(p.pos.x, p.pos.z) + p.radius;  
            if (p.phaseAge > k.restSeconds)
            {
                p.phase = ThrownPhase::Sinking;
                p.phaseAge = 0.0f;
            }
            return false;
        case ThrownPhase::Sinking:
            p.pos.y -= k.sinkDepth / k.sinkSeconds * dt;
            if (p.phaseAge > k.sinkSeconds)
                p.phase = ThrownPhase::Gone;
            return false;
        default:
            return false;
        }
    }

     
    inline int breakupDetail(float cameraDistance) { return cameraDistance < 350.0f ? 2 : cameraDistance < 900.0f ? 1 : 0; }

    struct Launch
    {
        V3 vel;
        float spinRate;  
    };
     
    inline Launch launchFor(VehicleType t, int part, const V3& vehicleVel, Rng& rng)
    {
        const float a = rng.range(-3.14159f, 3.14159f);
        const V3 out{std::sin(a), 0.0f, std::cos(a)};
        switch (t)
        {
        case VehicleType::Tank:
        case VehicleType::Amp:
            if (part == 0)  
                return {vehicleVel + out * rng.range(3.0f, 7.0f) + V3{0, rng.range(14.0f, 19.0f), 0}, rng.range(2.0f, 5.0f)};
            return {vehicleVel + out * rng.range(5.0f, 10.0f) + V3{0, rng.range(6.0f, 11.0f), 0}, rng.range(4.0f, 9.0f)};
        case VehicleType::Heli:  
            return {vehicleVel + out * rng.range(10.0f, 16.0f) + V3{0, rng.range(5.0f, 9.0f), 0}, rng.range(10.0f, 16.0f)};
        default:  
            return {vehicleVel * rng.range(0.55f, 0.8f) + V3{rng.range(-12.0f, 12.0f), rng.range(-4.0f, 10.0f), rng.range(-12.0f, 12.0f)},
                    rng.range(1.5f, 6.0f)};
        }
    }

     
    inline std::vector<size_t> overCap(const std::vector<float>& ages, size_t cap)
    {
        std::vector<size_t> idx(ages.size());
        for (size_t i = 0; i < idx.size(); ++i)
            idx[i] = i;
        if (ages.size() <= cap)
            return {};
        std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return ages[a] > ages[b]; });
        idx.resize(ages.size() - cap);
        return idx;
    }
}  
