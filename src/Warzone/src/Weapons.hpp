 
 
 
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Core.hpp"
#include "Vfx.hpp"

namespace warzone
{
    constexpr float GRAVITY = 9.81f;
    constexpr float SPEED_OF_SOUND = 343.0f;
    constexpr float BOMB_SPEED_SCALE = 3.0f;

    inline V3 cross(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    inline V3 lerp(const V3& a, const V3& b, float t) { return a + (b - a) * t; }

     
     
    inline bool segmentBox(const Box& b, const V3& p0, const V3& p1, float& tHit, V3* normal = nullptr)
    {
        const float o[3] = {p0.x, p0.y, p0.z};
        const float d[3] = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
        const float lo[3] = {b.lo.x, b.lo.y, b.lo.z};
        const float hi[3] = {b.hi.x, b.hi.y, b.hi.z};
        float t0 = 0.0f, t1 = 1.0f;
        int axis = -1;
        float sign = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            if (std::abs(d[i]) < 1e-9f)
            {
                if (o[i] < lo[i] || o[i] > hi[i])
                    return false;
                continue;
            }
            float ta = (lo[i] - o[i]) / d[i], tb = (hi[i] - o[i]) / d[i];
            float s = -1.0f;
            if (ta > tb)
            {
                std::swap(ta, tb);
                s = 1.0f;
            }
            if (ta > t0)
            {
                t0 = ta;
                axis = i;
                sign = s;
            }
            t1 = std::min(t1, tb);
            if (t0 > t1)
                return false;
        }
        tHit = t0;
        if (normal)
        {
            *normal = {0, 0, 0};
            if (axis == 0)
                normal->x = sign;
            else if (axis == 1)
                normal->y = sign;
            else if (axis == 2)
                normal->z = sign;
            else
                *normal = norm(p0 - p1);  
        }
        return true;
    }

     
    struct Obb
    {
        V3 centre;
        V3 axis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        V3 half{1, 1, 1};
        V3 toLocal(const V3& p) const
        {
            const V3 r = p - centre;
            return {dot(r, axis[0]), dot(r, axis[1]), dot(r, axis[2])};
        }
        V3 toWorldDir(const V3& v) const { return axis[0] * v.x + axis[1] * v.y + axis[2] * v.z; }
        Box localBox() const { return {{-half.x, -half.y, -half.z}, {half.x, half.y, half.z}}; }
        float radius() const { return len(half); }
    };

    inline bool segmentObb(const Obb& b, const V3& p0, const V3& p1, float& tHit, V3* normal = nullptr)
    {
        V3 n;
        if (!segmentBox(b.localBox(), b.toLocal(p0), b.toLocal(p1), tHit, &n))
            return false;
        if (normal)
            *normal = b.toWorldDir(n);
        return true;
    }

    inline float distance(const Obb& b, const V3& p) { return distance(b.localBox(), b.toLocal(p)); }

     
    enum class ProjectileKind : uint8_t
    {
        Shell,    
        Bullet,   
        Missile,  
        Bomb,     
        Autocannon,  
        Count
    };

    struct ProjectileSpec
    {
        float speed;         
        float gravity;       
        float drag;          
        float life;          
        float direct;        
        float blastRadius;   
        float blastDamage;   
        float push;          
        float turnRate;      
        float accel;         
        float maxSpeed;      
    };

    inline const ProjectileSpec& specOf(ProjectileKind k)
    {
        static const ProjectileSpec specs[] = {
             
            {420.0f, 1.0f, 0.0f, 8.0f, 650.0f, 4.5f, 900.0f, 13.0f, 0.0f, 0.0f, 0.0f},     
            {900.0f, 1.0f, 0.0f, 2.5f, 22.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},         
            {60.0f, 0.0f, 0.0f, 9.0f, 380.0f, 4.0f, 700.0f, 12.0f, 2.6f, 120.0f, 280.0f},  
            {0.0f, BOMB_SPEED_SCALE * BOMB_SPEED_SCALE, 0.05f * BOMB_SPEED_SCALE, 30.0f / BOMB_SPEED_SCALE,
             900.0f, 9.0f, 1800.0f, 18.0f, 0.9f * BOMB_SPEED_SCALE, 0.0f, 0.0f},  
            {960.0f, 1.0f, 0.0f, 3.0f, 55.0f, 1.5f, 220.0f, 5.0f, 0.0f, 0.0f, 0.0f},       
        };
        return specs[static_cast<int>(k)];
    }

    struct Projectile
    {
        ProjectileKind kind = ProjectileKind::Shell;
        V3 pos, vel;
        float age = 0.0f;
        uint32_t owner = ~0u;   
        bool guided = false;
        V3 target;              
        bool alive = true;
    };

     
    inline V3 turnToward(const V3& from, const V3& to, float maxAngle)
    {
        const float c = std::clamp(dot(from, to), -1.0f, 1.0f);
        const float angle = std::acos(c);
        if (angle <= maxAngle || angle < 1e-6f)
            return to;
        V3 axis = cross(from, to);
        if (len(axis) < 1e-6f)
            axis = std::abs(from.y) < 0.9f ? cross(from, V3{0, 1, 0}) : cross(from, V3{1, 0, 0});
        axis = norm(axis);
         
        const float s = std::sin(maxAngle), k = std::cos(maxAngle);
        return norm(from * k + cross(axis, from) * s + axis * (dot(axis, from) * (1 - k)));
    }

     
    inline void stepProjectile(Projectile& p, float dt)
    {
        const ProjectileSpec& s = specOf(p.kind);
        p.age += dt;
        float speed = len(p.vel);
        if (p.guided && speed > 1e-3f)
        {
            const V3 dir = p.vel * (1.0f / speed);
            V3 want = norm(p.target - p.pos);
            if (p.kind == ProjectileKind::Bomb)
            {
                 
                 
                const float t = len(p.target - p.pos) / speed;
                want = norm(p.target + V3{0, 0.5f * GRAVITY * s.gravity * t * t, 0} - p.pos);
            }
            const V3 turned = turnToward(dir, want, s.turnRate * dt);
            if (p.kind == ProjectileKind::Missile)
                speed = std::min(s.maxSpeed, speed + s.accel * dt);
            p.vel = turned * speed;
        }
        if (s.drag > 0.0f)
            p.vel = p.vel * std::max(0.0f, 1.0f - s.drag * dt);
         
        const bool fallFree = !(p.kind == ProjectileKind::Missile && p.age < s.life * 0.8f);
        if (fallFree)
            p.vel.y -= GRAVITY * s.gravity * dt;
        p.pos = p.pos + p.vel * dt;
    }

     
     
    inline bool solveElevation(float v, float g, float dx, float dy, float& angle)
    {
        if (dx < 1e-3f)
        {
            angle = dy >= 0 ? 1.5707963f : -1.5707963f;
            return true;
        }
        const float v2 = v * v;
        const float disc = v2 * v2 - g * (g * dx * dx + 2.0f * dy * v2);
        if (disc < 0.0f)
            return false;
        angle = std::atan((v2 - std::sqrt(disc)) / (g * dx));
        return true;
    }

     
     
    struct Armor
    {
        float front = 0.6f;  
        float side = 1.0f;
        float rear = 1.5f;
        float top = 1.6f;    
    };

     
    inline float armorFactor(const Armor& a, const V3& shotDirLocal)
    {
        if (shotDirLocal.y < -0.7f)
            return a.top;
         
        const float fwd = -shotDirLocal.z;
        const float h = std::sqrt(shotDirLocal.x * shotDirLocal.x + shotDirLocal.z * shotDirLocal.z) + 1e-6f;
        const float c = fwd / h;  
        if (c > 0.707f)
            return a.front;
        if (c < -0.707f)
            return a.rear;
        return a.side;
    }

    inline float splash(float damage, float radius, float d)
    {
        if (radius <= 0.0f || d >= radius)
            return 0.0f;
        const float f = 1.0f - std::max(d, 0.0f) / radius;
        return damage * f * f;
    }

    struct Health
    {
        float hp = 1000.0f, maxHp = 1000.0f;
        bool dead = false;
         
        bool apply(float dmg)
        {
            if (dead || dmg <= 0.0f)
                return false;
            hp = std::max(0.0f, hp - dmg);
            if (hp <= 0.0f)
            {
                dead = true;
                return true;
            }
            return false;
        }
    };

     
     
    inline float shakeAt(float strength, float radius, float distance)
    {
        const float ref = std::max(radius, 0.5f) * 6.0f;
        const float k = distance / ref;
        const float s = strength / (1.0f + k * k);
        return s < 0.01f ? 0.0f : std::min(s, 1.0f);
    }

     
    inline float soundGain(float distance, float reference)
    {
        if (distance <= reference)
            return 1.0f;
        const float g = reference / distance;
        return g < 0.02f ? 0.0f : g;
    }
    inline float soundDelay(float distance) { return std::max(0.0f, distance) / SPEED_OF_SOUND; }
}  
