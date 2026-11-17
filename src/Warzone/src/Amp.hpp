 
 
 
#pragma once
#include <algorithm>
#include <cmath>

#include "Tank.hpp"

namespace warzone
{
    struct AutocannonParams
    {
        float rate = 8.0f;            
        float heatPerRound = 0.045f;  
        float coolRate = 0.42f;       
        float coolDelay = 0.2f;       
        float recoverAt = 0.35f;      
        float baseSpread = 0.0022f;   
        float heatSpread = 0.011f;    
        float moveSpread = 0.006f;    
        float recoilBack = 0.14f;     
        float recoilReturn = 16.0f;   
        unsigned ammo = 900;
    };

    struct AutocannonState
    {
        float heat = 0.0f;
        bool overheated = false;
        float cooldown = 0.0f;    
        float sinceShot = 10.0f;  
        float recoil = 0.0f;      
        unsigned ammo = 900, fired = 0;
    };

     
    inline int stepAutocannon(AutocannonState& s, const AutocannonParams& p, bool trigger, float dt)
    {
        s.sinceShot += dt;
         
        s.cooldown = trigger ? s.cooldown - dt : std::max(0.0f, s.cooldown - dt);
        s.recoil = std::max(0.0f, s.recoil - s.recoil * p.recoilReturn * dt - 0.5f * dt);
        if (s.sinceShot > p.coolDelay || s.overheated)
            s.heat = std::max(0.0f, s.heat - p.coolRate * dt);
        if (s.overheated && s.heat <= p.recoverAt)
            s.overheated = false;
        int rounds = 0;
        if (!trigger || s.overheated || s.ammo == 0)
        {
            s.cooldown = std::max(0.0f, s.cooldown);
            return 0;
        }
         
        float next = s.cooldown;  
        const float interval = 1.0f / p.rate;
        while (next <= 1e-6f && s.ammo > 0 && !s.overheated)
        {
            ++rounds;
            --s.ammo;
            ++s.fired;
            s.heat += p.heatPerRound;
            s.recoil = 1.0f;
            s.sinceShot = 0.0f;
            next += interval;
            if (s.heat >= 1.0f)
            {
                s.heat = 1.0f;
                s.overheated = true;
            }
            if (rounds >= 4)
                break;
        }
        s.cooldown = std::max(-interval, next);
        return rounds;
    }

     
    inline float spreadOf(const AutocannonState& s, const AutocannonParams& p, float speedFraction)
    {
        return p.baseSpread + p.heatSpread * s.heat * s.heat + p.moveSpread * std::clamp(speedFraction, 0.0f, 1.0f);
    }

     
    inline V3 scatter(const V3& dir, float halfAngle, float u, float v)
    {
        const V3 helper = std::abs(dir.y) < 0.95f ? V3{0, 1, 0} : V3{1, 0, 0};
        const V3 a = norm(cross(dir, helper)), b = cross(a, dir);
        const float r = halfAngle * std::sqrt(u), phi = v * TAU;
        return norm(dir + a * (r * std::cos(phi)) + b * (r * std::sin(phi)));
    }

     
    inline TankParams ampDriveParams()
    {
        TankParams p;
        p.maxForward = 19.0f;  
        p.maxReverse = 8.0f;
        p.accel = 6.5f;
        p.brake = 13.0f;
        p.coast = 2.2f;
        p.turnRate = 1.15f;
        p.turnRateAtSpeed = 0.62f;
        p.yawAccel = 4.5f;
        p.turretRate = 1.75f;  
        p.gunRate = 1.1f;
        p.minElevation = -0.14f;
        p.maxElevation = 0.95f;  
        p.reload = 0.0f;         
        p.recoilBack = 0.14f;
        p.muzzleSpeed = 960.0f;
        p.trackHalfWidth = 1.45f;
        p.pitchStiffness = 44.0f;
        p.pitchDamping = 6.0f;
        p.rollStiffness = 50.0f;
        p.rollDamping = 7.0f;
        p.squat = 0.026f;
        p.firePitchKick = 0.05f;
        return p;
    }
}  
