 
 
 
 
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

#include "Tank.hpp"

namespace warzone
{
    struct HeliParams
    {
        float maxForward = 72.0f;  
        float maxBack = 16.0f;
        float maxSide = 24.0f;
        float maxClimb = 14.0f;
        float maxSink = 12.0f;
        float accelForward = 12.0f;  
        float accelSide = 10.0f;
        float accelVertical = 11.0f;
        float hoverDecel = 9.0f;      
        float maxYawRate = 1.5f;      
        float yawAccel = 5.0f;        
        float yawFollow = 3.0f;       
        float maxAltitude = 650.0f;   
        float skidHeight = 0.0f;      
         
        float pitchPerAccel = 0.030f;  
        float pitchAtSpeed = 0.10f;    
        float rollPerAccel = 0.034f;
        float rollPerTurn = 0.010f;    
        float attitudeStiffness = 22.0f, attitudeDamping = 7.5f;
        float maxPitch = 0.36f, maxRoll = 0.5f;
        float rotorIdle = 14.0f, rotorFlight = 27.0f;  
        float tailRatio = 4.6f;
    };

    struct HeliInput
    {
        float forward = 0.0f;  
        float strafe = 0.0f;   
        float climb = 0.0f;    
        float yaw = 0.0f;      
        bool hasYawTarget = false;
        float yawTarget = 0.0f;  
    };

    struct HeliState
    {
        V3 pos, vel;
        float yaw = 0.0f, yawRate = 0.0f;  
        float pitch = 0.0f, roll = 0.0f, pitchVel = 0.0f, rollVel = 0.0f;  
        float accelForward = 0.0f, accelSide = 0.0f;
        float rotor = 0.0f, tail = 0.0f, rotorSpeed = 0.0f;
        bool grounded = true;
    };

    inline V3 heliForward(float yaw) { return {std::sin(yaw), 0.0f, std::cos(yaw)}; }
    inline V3 heliLeft(float yaw) { return {std::cos(yaw), 0.0f, -std::sin(yaw)}; }

     
    inline void stepHeli(HeliState& s, const HeliParams& p, const HeliInput& in, float groundHeight, float dt)
    {
        const V3 f = heliForward(s.yaw), l = heliLeft(s.yaw);
        float vf = dot(s.vel, f), vs = dot(s.vel, l), vy = s.vel.y;
        const float fwd = std::clamp(in.forward, -1.0f, 1.0f), side = std::clamp(in.strafe, -1.0f, 1.0f);
        const float climb = std::clamp(in.climb, -1.0f, 1.0f);
        auto axis = [&](float v, float stick, float maxPos, float maxNeg, float accel)
        {
            const float target = stick >= 0.0f ? stick * maxPos : stick * maxNeg;
            const bool centred = std::abs(stick) < 0.02f;
            const float rate = centred ? p.hoverDecel : (v * target < 0.0f ? accel + p.hoverDecel : accel);
            return approach(v, target, rate * dt);
        };
        const float vf0 = vf, vs0 = vs;
        if (s.grounded && climb <= 0.0f)
        {
             
            vf = approach(vf, 0.0f, 12.0f * dt);
            vs = approach(vs, 0.0f, 12.0f * dt);
        }
        else
        {
            vf = axis(vf, fwd, p.maxForward, p.maxBack, p.accelForward);
            vs = axis(vs, side, p.maxSide, p.maxSide, p.accelSide);
        }
        vy = axis(vy, climb, p.maxClimb, p.maxSink, p.accelVertical);
        s.accelForward = dt > 0.0f ? (vf - vf0) / dt : 0.0f;
        s.accelSide = dt > 0.0f ? (vs - vs0) / dt : 0.0f;
         
        float wantRate = std::clamp(in.yaw, -1.0f, 1.0f) * p.maxYawRate;
        if (in.hasYawTarget && !s.grounded)
            wantRate += wrapPi(in.yawTarget - s.yaw) * p.yawFollow;
        wantRate = std::clamp(wantRate, -p.maxYawRate, p.maxYawRate);
        s.yawRate = approach(s.yawRate, wantRate, p.yawAccel * dt);
        s.yaw = wrapPi(s.yaw + s.yawRate * dt);
         
        const V3 f2 = heliForward(s.yaw), l2 = heliLeft(s.yaw);
        s.vel = f2 * vf + l2 * vs + V3{0.0f, vy, 0.0f};
        s.pos = s.pos + s.vel * dt;
        const float floor = groundHeight + p.skidHeight;
        s.grounded = false;
        if (s.pos.y <= floor)
        {
            s.pos.y = floor;
            if (s.vel.y < 0.0f)
                s.vel.y = 0.0f;
            s.grounded = true;
        }
        if (s.pos.y > p.maxAltitude)
        {
            s.pos.y = p.maxAltitude;
            s.vel.y = std::min(s.vel.y, 0.0f);
        }
         
        const float pitchTarget = s.grounded ? 0.0f
                                             : std::clamp(s.accelForward * p.pitchPerAccel + vf / p.maxForward * p.pitchAtSpeed, -p.maxPitch, p.maxPitch);
        const float rollTarget = s.grounded ? 0.0f
                                            : std::clamp(s.accelSide * p.rollPerAccel + s.yawRate * vf * p.rollPerTurn, -p.maxRoll, p.maxRoll);
        s.pitchVel += ((pitchTarget - s.pitch) * p.attitudeStiffness - s.pitchVel * p.attitudeDamping) * dt;
        s.rollVel += ((rollTarget - s.roll) * p.attitudeStiffness - s.rollVel * p.attitudeDamping) * dt;
        s.pitch = std::clamp(s.pitch + s.pitchVel * dt, -p.maxPitch, p.maxPitch);
        s.roll = std::clamp(s.roll + s.rollVel * dt, -p.maxRoll, p.maxRoll);
         
        const float wantRotor = s.grounded && climb <= 0.0f ? p.rotorIdle : p.rotorFlight + 3.0f * std::max(0.0f, climb);
        s.rotorSpeed = approach(s.rotorSpeed, wantRotor, 8.0f * dt);
        s.rotor = wrapPi(s.rotor + s.rotorSpeed * dt);
        s.tail = wrapPi(s.tail + s.rotorSpeed * p.tailRatio * dt);
    }

     
    struct LockCandidate
    {
        int64_t id = -1;  
        V3 pos;
        bool unit = true;
    };
    constexpr int64_t BUILDING_LOCK = 1000000;

     
     
    inline int pickLockCandidate(const V3& eye, const V3& dir, std::span<const LockCandidate> c, float maxAngle, float maxRange)
    {
        int best = -1;
        float bestAngle = 1e9f;
        bool bestUnit = false;
        const float cosMax = std::cos(maxAngle);
        for (int i = 0; i < static_cast<int>(c.size()); ++i)
        {
            const V3 d = c[i].pos - eye;
            const float dist = len(d);
            if (dist < 1.0f || dist > maxRange)
                continue;
            const float cs = dot(d, dir) / dist;
            if (cs < cosMax)
                continue;
            const float angle = std::acos(std::clamp(cs, -1.0f, 1.0f));
            if (best < 0 || (c[i].unit && !bestUnit) || (c[i].unit == bestUnit && angle < bestAngle))
            {
                best = i;
                bestAngle = angle;
                bestUnit = c[i].unit;
            }
        }
        return best;
    }

    struct LockParams
    {
        float lockTime = 1.2f;   
        float grace = 0.35f;     
    };

    struct LockState
    {
        int64_t id = -1;
        float progress = 0.0f;  
        float away = 0.0f;      
        bool locked = false;
    };

     
    inline void stepLock(LockState& s, const LockParams& p, int64_t candidate, float dt)
    {
        if (candidate >= 0 && candidate == s.id)
        {
            s.away = 0.0f;
            s.progress = std::min(1.0f, s.progress + dt / p.lockTime);
            s.locked = s.progress >= 1.0f;
            return;
        }
        if (s.id >= 0 && s.away + dt <= p.grace)
        {
            s.away += dt;  
            return;
        }
        s = LockState{};
        if (candidate >= 0)
        {
            s.id = candidate;
            s.progress = std::min(1.0f, dt / p.lockTime);
        }
    }
}  
