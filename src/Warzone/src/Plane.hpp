 
 
 
 
 
#pragma once
#include "BuildingGen.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

#include "Tank.hpp"
#include "Weapons.hpp"

namespace warzone
{
    struct PlaneParams
    {
        float stallSpeed = 52.0f;    
        float maxSpeed = 190.0f;     
        float thrust = 15.0f;        
        float maxG = 7.0f;           
        float maxTurnRate = 0.95f;   
        float turnBleed = 0.035f;    
        float throttleRate = 0.6f;   
        float maxBank = 1.35f;       
        float bankStiffness = 9.0f;  
        float maxClimb = 0.82f;      
        float ceiling = 1500.0f;     
        float stallDrop = 0.45f;     
    };

    struct PlaneState
    {
        V3 pos;
        V3 dir{0, 0, 1};    
        float speed = 110.0f;
        float throttle = 0.65f;
        float roll = 0.0f;       
        float yawRate = 0.0f, pitchRate = 0.0f;
        bool stalled = false;
        float liftFrac = 1.0f;
    };

    inline float planeYaw(const V3& d) { return std::atan2(d.x, d.z); }
    inline float planePitch(const V3& d) { return std::asin(std::clamp(d.y, -1.0f, 1.0f)); }
    inline V3 dirFromAngles(float yaw, float pitch) { return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)}; }

     
    inline float turnLimit(const PlaneParams& p, float speed) { return std::min(p.maxTurnRate, p.maxG * GRAVITY / std::max(speed, 1.0f)); }

     
    inline void stepPlane(PlaneState& s, const PlaneParams& p, const V3& aim, float throttleIn, float dt)
    {
        s.throttle = std::clamp(s.throttle + std::clamp(throttleIn, -1.0f, 1.0f) * p.throttleRate * dt, 0.0f, 1.0f);
        const float yaw0 = planeYaw(s.dir), pitch0 = planePitch(s.dir);
        s.liftFrac = std::min(1.0f, (s.speed / p.stallSpeed) * (s.speed / p.stallSpeed));
        s.stalled = s.speed < p.stallSpeed;
         
        const float authority = std::clamp((s.speed - 0.8f * p.stallSpeed) / (0.4f * p.stallSpeed), 0.25f, 1.0f);
        V3 want = norm(aim);
        if (want.y > p.maxClimb)  
        {
            const float h = std::sqrt(want.x * want.x + want.z * want.z);
            const float k = h > 1e-4f ? std::sqrt(1.0f - p.maxClimb * p.maxClimb) / h : 0.0f;
            want = h > 1e-4f ? V3{want.x * k, p.maxClimb, want.z * k} : dirFromAngles(yaw0, std::asin(p.maxClimb));
        }
        if (want.y < -0.95f)
            want = norm(V3{want.x, -0.95f, want.z});
        s.dir = turnToward(s.dir, want, turnLimit(p, s.speed) * authority * dt);
         
        if (s.liftFrac < 1.0f)
        {
            const float sag = (1.0f - s.liftFrac) * GRAVITY / std::max(s.speed, 8.0f) + (s.stalled ? p.stallDrop : 0.0f);
            s.dir = turnToward(s.dir, V3{0, -1, 0}, sag * dt);
        }
        if (s.dir.y > p.maxClimb)
            s.dir = norm(V3{s.dir.x, p.maxClimb, s.dir.z});
         
        const float drag = p.thrust / (p.maxSpeed * p.maxSpeed);
        const float turn = std::acos(std::clamp(dot(s.dir, dirFromAngles(yaw0, pitch0)), -1.0f, 1.0f)) / std::max(dt, 1e-4f);
        s.speed += (s.throttle * p.thrust - drag * s.speed * s.speed - GRAVITY * s.dir.y - p.turnBleed * turn * s.speed) * dt;
        s.speed = std::max(s.speed, 15.0f);
        s.pos = s.pos + s.dir * (s.speed * dt);
        if (s.pos.y > p.ceiling)
        {
            s.pos.y = p.ceiling;
            if (s.dir.y > 0.0f)
                s.dir = norm(V3{s.dir.x, 0.0f, s.dir.z});
        }
         
        s.yawRate = dt > 0.0f ? wrapPi(planeYaw(s.dir) - yaw0) / dt : 0.0f;
        s.pitchRate = dt > 0.0f ? (planePitch(s.dir) - pitch0) / dt : 0.0f;
        const float bankWant = std::clamp(std::atan(s.yawRate * s.speed / GRAVITY), -p.maxBank, p.maxBank);
        s.roll += (bankWant - s.roll) * (1.0f - std::exp(-p.bankStiffness * dt));
    }

     
    inline bool isBombProp(Kind kind)
    {
        return kind == Kind::Yard || kind == Kind::Shed || kind == Kind::Kiosk ||
               kind == Kind::Station || kind == Kind::Garage || kind == Kind::Barn;
    }

    enum class TargetClass : uint8_t
    {
        Unit = 0,
        Building = 1,
        Prop = 2
    };
    inline const char* targetClassName(TargetClass c) { return c == TargetClass::Unit ? "UNIT" : c == TargetClass::Building ? "BUILDING" : "PROP"; }

    struct BombCandidate
    {
        int64_t id = -1;
        V3 pos;
        TargetClass cls = TargetClass::Unit;
    };

     
     
    inline int pickBombTarget(const V3& aim, std::span<const BombCandidate> c, float radius)
    {
        int best = -1;
        float bestD = 0.0f;
        for (int i = 0; i < static_cast<int>(c.size()); ++i)
        {
            const float dx = c[i].pos.x - aim.x, dz = c[i].pos.z - aim.z;
            const float d = std::sqrt(dx * dx + dz * dz);
            if (d > radius)
                continue;
            if (best < 0 || c[i].cls < c[best].cls || (c[i].cls == c[best].cls && d < bestD))
            {
                best = i;
                bestD = d;
            }
        }
        return best;
    }

     
    struct BombPrediction
    {
        float timeToImpact = 0.0f;  
        float miss = 1e9f;          
        V3 impact;
        bool inRange = false;       
    };

     
    inline BombPrediction predictBomb(const V3& pos, const V3& vel, const V3& target,
                                      float maxTime = 30.0f / BOMB_SPEED_SCALE, float step = 0.05f / BOMB_SPEED_SCALE)
    {
        Projectile b;
        b.kind = ProjectileKind::Bomb;
        b.pos = pos;
        b.vel = vel * BOMB_SPEED_SCALE;
        b.guided = true;
        b.target = target;
        BombPrediction r;
        float bestD = 1e9f;
        for (float t = 0.0f; t < maxTime; t += step)
        {
            const V3 before = b.pos;
            stepProjectile(b, step);
            const float d = len(b.pos - target);
            if (d < bestD)
            {
                bestD = d;
                r.timeToImpact = t + step;
                r.impact = b.pos;
            }
            if (b.pos.y <= target.y)
            {
                 
                const float k = before.y > b.pos.y ? (before.y - target.y) / (before.y - b.pos.y) : 1.0f;
                const V3 at = before + (b.pos - before) * std::clamp(k, 0.0f, 1.0f);
                const float dc = len(at - target);
                if (dc <= bestD)
                {
                    bestD = dc;
                    r.timeToImpact = t + step * k;
                    r.impact = at;
                }
                break;
            }
        }
        r.miss = bestD;
        r.inRange = bestD <= specOf(ProjectileKind::Bomb).blastRadius;
        return r;
    }
}  
