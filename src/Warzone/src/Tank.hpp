 
 
 
 
#pragma once
#include <algorithm>
#include <cmath>

#include "Weapons.hpp"

namespace warzone
{
    constexpr float TAU = 6.28318530718f;

    inline float wrapPi(float a)
    {
        a = std::fmod(a + 3.14159265f, TAU);
        if (a < 0)
            a += TAU;
        return a - 3.14159265f;
    }
    inline float approach(float cur, float target, float maxStep)
    {
        return cur < target ? std::min(target, cur + maxStep) : std::max(target, cur - maxStep);
    }

    struct TankParams
    {
        float maxForward = 15.0f;    
        float maxReverse = 5.5f;
        float accel = 4.0f;          
        float brake = 10.0f;         
        float coast = 1.8f;          
        float turnRate = 0.80f;      
        float turnRateAtSpeed = 0.45f;  
        float yawAccel = 2.5f;       
        float turretRate = 0.60f;    
        float gunRate = 0.20f;       
        float minElevation = -0.10f;  
        float maxElevation = 0.33f;   
        float reload = 4.5f;         
        float recoilBack = 0.55f;    
        float recoilReturn = 2.6f;   
        float muzzleSpeed = 420.0f;  
        float trackHalfWidth = 1.6f;
        float wheelRadius = 0.38f;
        float sprocketRadius = 0.29f;
         
        float pitchStiffness = 38.0f, pitchDamping = 7.0f;
        float rollStiffness = 45.0f, rollDamping = 8.0f;
        float squat = 0.022f;        
        float firePitchKick = 0.9f;  
    };

    struct TankInput
    {
        float throttle = 0.0f;  
        float steer = 0.0f;     
        bool brake = false;
        bool fire = false;
        bool hasAim = false;
        float aimYaw = 0.0f;        
        float aimElevation = 0.0f;  
    };

    struct TankState
    {
        float speed = 0.0f;    
        float yawRate = 0.0f;  
        float turretYaw = 0.0f, elevation = 0.0f;
        float reload = 0.0f;   
        float recoil = 0.0f;   
        float wheelAngle = 0.0f, sprocketLeft = 0.0f, sprocketRight = 0.0f;
        float pitch = 0.0f, roll = 0.0f, pitchVel = 0.0f, rollVel = 0.0f;  
        float accelLong = 0.0f;
        bool firedThisStep = false;
        unsigned shots = 0;
    };

    inline float trackSpeed(const TankState& s, const TankParams& p, bool left)
    {
        return left ? s.speed - s.yawRate * p.trackHalfWidth : s.speed + s.yawRate * p.trackHalfWidth;
    }

     
    inline void stepDrive(TankState& s, const TankParams& p, const TankInput& in, float dt)
    {
        const float before = s.speed;
        const float throttle = std::clamp(in.throttle, -1.0f, 1.0f);
        const float target = throttle >= 0 ? throttle * p.maxForward : throttle * p.maxReverse;
        float rate = p.coast;
        if (in.brake)
            rate = p.brake;
        else if (std::abs(throttle) > 0.01f)
            rate = (s.speed * target < 0.0f || std::abs(target) < std::abs(s.speed)) ? p.brake : p.accel;
        s.speed = approach(s.speed, in.brake ? 0.0f : target, rate * dt);
        const float frac = std::min(1.0f, std::abs(s.speed) / p.maxForward);
        const float maxYaw = p.turnRate + (p.turnRateAtSpeed - p.turnRate) * frac;
        s.yawRate = approach(s.yawRate, std::clamp(in.steer, -1.0f, 1.0f) * maxYaw, p.yawAccel * dt);
        s.accelLong = dt > 0 ? (s.speed - before) / dt : 0.0f;
         
        s.wheelAngle = wrapPi(s.wheelAngle + s.speed / p.wheelRadius * dt);
        s.sprocketLeft = wrapPi(s.sprocketLeft + trackSpeed(s, p, true) / p.sprocketRadius * dt);
        s.sprocketRight = wrapPi(s.sprocketRight + trackSpeed(s, p, false) / p.sprocketRadius * dt);
    }

     
    inline void stepGun(TankState& s, const TankParams& p, const TankInput& in, float dt)
    {
        if (in.hasAim)
        {
            const float dy = wrapPi(in.aimYaw - s.turretYaw);
            s.turretYaw = wrapPi(s.turretYaw + std::clamp(dy, -p.turretRate * dt, p.turretRate * dt));
            const float e = std::clamp(in.aimElevation, p.minElevation, p.maxElevation);
            s.elevation = approach(s.elevation, e, p.gunRate * dt);
        }
        s.reload = std::max(0.0f, s.reload - dt);
        s.recoil = std::max(0.0f, s.recoil - s.recoil * p.recoilReturn * dt - 0.02f * dt);
        s.firedThisStep = false;
        if (in.fire && s.reload <= 0.0f)
        {
            s.reload = p.reload;
            s.recoil = 1.0f;
            s.firedThisStep = true;
            ++s.shots;
             
            s.pitchVel += p.firePitchKick * std::cos(s.turretYaw);
            s.rollVel += p.firePitchKick * 0.8f * std::sin(s.turretYaw);
        }
    }

     
    inline void stepSuspension(TankState& s, const TankParams& p, float dt)
    {
        const float pitchTarget = std::clamp(s.accelLong * p.squat, -0.08f, 0.08f);  
        const float rollTarget = std::clamp(s.yawRate * s.speed * 0.006f, -0.05f, 0.05f);  
        s.pitchVel += ((pitchTarget - s.pitch) * p.pitchStiffness - s.pitchVel * p.pitchDamping) * dt;
        s.rollVel += ((rollTarget - s.roll) * p.rollStiffness - s.rollVel * p.rollDamping) * dt;
        s.pitch = std::clamp(s.pitch + s.pitchVel * dt, -0.15f, 0.15f);
        s.roll = std::clamp(s.roll + s.rollVel * dt, -0.12f, 0.12f);
    }

     
     
    inline bool aimAngles(const V3& aimInTurretFrame, float muzzleSpeed, float& yaw, float& elevation)
    {
        yaw = std::atan2(aimInTurretFrame.x, aimInTurretFrame.z);
        const float dx = std::sqrt(aimInTurretFrame.x * aimInTurretFrame.x + aimInTurretFrame.z * aimInTurretFrame.z);
        if (!solveElevation(muzzleSpeed, GRAVITY, dx, aimInTurretFrame.y, elevation))
        {
            elevation = 0.7853981f;
            return false;
        }
        return true;
    }
}  
