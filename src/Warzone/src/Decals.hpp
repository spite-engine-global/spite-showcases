 
 
 
 
 
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Core.hpp"

namespace warzone::decal
{
     
    constexpr int ATLAS_N = 4;
    enum Kind : uint8_t
    {
        SCORCH,  
        BULLET,  
        SOOT,    
        PATCH,   
        EMBER,   
        KINDS
    };
    constexpr int FIRST[KINDS] = {0, 5, 9, 12, 14};
    constexpr int COUNT[KINDS] = {5, 4, 3, 2, 2};
    static_assert(FIRST[EMBER] + COUNT[EMBER] == ATLAS_N * ATLAS_N, "the kinds fill the atlas");

     
    inline int frameFor(Kind k, uint32_t seed) { return FIRST[k] + static_cast<int>(hash32(seed) % static_cast<uint32_t>(COUNT[k])); }

     
     
    inline float scorchSize(float blastRadius) { return std::clamp(blastRadius * 2.5f, 1.5f, 26.0f); }
     
     
    constexpr float CRATER_MIN_RADIUS = 3.0f;
    inline float craterSize(float blastRadius)
    {
        if (blastRadius < CRATER_MIN_RADIUS)
            return 0.0f;
        return std::clamp(1.2f * blastRadius + 0.6f * (blastRadius - CRATER_MIN_RADIUS), 3.2f, 14.0f);
    }
     
    constexpr float CRATER_RIM = 0.035f;
    inline float bulletMarkSize(float roll01) { return 0.35f + 0.4f * std::clamp(roll01, 0.0f, 1.0f); }
     
    inline float sootSize(float blastRadius, float roll01)
    {
        const float r = std::max(blastRadius, 0.0f);
        return r < 0.5f ? 0.9f + 0.6f * std::clamp(roll01, 0.0f, 1.0f) : std::clamp(r * 1.7f, 1.6f, 9.0f);
    }
     
    inline float wreckPatchSize(float halfX, float halfZ) { return std::clamp(2.0f * std::max(halfX, halfZ) * 1.5f + 1.5f, 4.0f, 24.0f); }
     
    inline bool wantsBulletMark(float distanceToViewer, float roll01) { return distanceToViewer < 260.0f && roll01 < 0.4f - 0.0008f * distanceToViewer; }

     
    struct Basis
    {
        V3 x, y, z;  
    };
     
     
    inline Basis surfaceBasis(const V3& normalIn, float yaw)
    {
        const auto crossv = [](const V3& a, const V3& b) { return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
        const V3 n = norm(normalIn);
        if (std::abs(n.y) > 0.8f)  
        {
            const V3 ref{std::cos(yaw), 0, std::sin(yaw)};
            const V3 x = norm(ref - n * dot(ref, n));
            return {x, crossv(n, x), n};
        }
        const V3 up = norm(V3{0, 1, 0} - n * n.y);  
        const V3 right = crossv(up, n);
        const float tilt = std::clamp(yaw, -3.14159f, 3.14159f) * 0.08f;
        const float c = std::cos(tilt), s = std::sin(tilt);
        return {right * c + up * s, up * c - right * s, n};
    }
     
     
     
    inline bool overlapsSidewalk(float x, float z, float radius, float x0, float z0, float x1, float z1)
    {
        return x + radius >= x0 - 3 && x - radius <= x1 + 3 && z + radius >= z0 - 3 && z - radius <= z1 + 3 &&
               !(x - radius > x0 + 0.5f && x + radius < x1 - 0.5f && z - radius > z0 + 0.5f && z + radius < z1 - 0.5f);
    }
    constexpr float GROUND_LIFT = 0.1f;
    inline float groundLift(float size, float slope, uint32_t sequence) { return GROUND_LIFT + 0.0035f * static_cast<float>(sequence % 8u) + std::min(0.5f, 0.1f * size * slope); }

     
     
    struct Slot
    {
        float born = -1.0f, life = 0.0f, size = 0.0f;
        uint8_t frame = 0;
        bool used = false;
    };
    struct Pool
    {
        std::vector<Slot> slots;
        uint32_t placed = 0, recycled = 0;
        void init(uint32_t n) { slots.assign(n, Slot{}); }
         
        int acquire(float now, float life, float size, uint8_t frame)
        {
            if (slots.empty())
                return -1;
            int pick = -1;
            for (size_t i = 0; i < slots.size(); ++i)
                if (!slots[i].used || now - slots[i].born >= slots[i].life)
                {
                    pick = static_cast<int>(i);
                    break;
                }
            if (pick < 0)
            {
                pick = 0;
                for (size_t i = 1; i < slots.size(); ++i)
                    if (slots[i].born < slots[pick].born)
                        pick = static_cast<int>(i);
                ++recycled;
            }
            slots[pick] = {now, life, size, frame, true};
            ++placed;
            return pick;
        }
         
        float alpha(int i, float now, float fadeFraction = 0.25f) const
        {
            const Slot& s = slots[static_cast<size_t>(i)];
            if (!s.used || s.life <= 0.0f)
                return 0.0f;
            const float u = (now - s.born) / s.life;
            if (u >= 1.0f)
                return 0.0f;
            const float start = 1.0f - std::clamp(fadeFraction, 0.01f, 1.0f);
            return u <= start ? 1.0f : (1.0f - u) / (1.0f - start);
        }
        uint32_t live(float now) const
        {
            uint32_t n = 0;
            for (const auto& s : slots)
                n += s.used && now - s.born < s.life;
            return n;
        }
    };

     
     
    enum class Stage : uint8_t
    {
        Burning,
        Smouldering,
        Cold
    };
    constexpr float BURN_SECONDS = 70.0f;       
    constexpr float SMOULDER_SECONDS = 150.0f;  
    inline Stage stageAt(float age, float burn = BURN_SECONDS, float smoulder = SMOULDER_SECONDS)
    {
        return age < burn ? Stage::Burning : age < burn + smoulder ? Stage::Smouldering : Stage::Cold;
    }
    struct Smoulder
    {
        float interval = 0.0f;  
        float size = 0.0f;      
        float grey = 0.0f;      
        float alpha = 0.0f;
        float ember = 0.0f;     
    };
     
    inline Smoulder smoulderAt(float age, float burn = BURN_SECONDS, float smoulder = SMOULDER_SECONDS)
    {
        Smoulder s;
        if (stageAt(age, burn, smoulder) != Stage::Smouldering)
            return s;
        const float u = std::clamp((age - burn) / std::max(smoulder, 1e-3f), 0.0f, 1.0f);  
        s.interval = 1.1f + 3.2f * u;
        s.size = 0.8f - 0.4f * u;
        s.grey = 0.55f + 0.3f * u;
        s.alpha = 0.55f * (1.0f - 0.8f * u);
         
        const float flick = 0.75f + 0.25f * std::sin(age * 2.3f) * std::sin(age * 0.7f + 1.3f);
        s.ember = std::clamp((1.0f - u) * (1.0f - u) * flick, 0.0f, 1.0f);
        return s;
    }
     
     
    struct Rgb
    {
        float r, g, b;
    };
    inline Rgb charTint(uint32_t seed, uint32_t mesh)
    {
        const uint32_t h = hash32(seed * 2654435761u + mesh * 40503u + 99u);
        const float v = 0.10f + 0.10f * static_cast<float>(h & 255u) / 255.0f;                
        const float warm = 0.06f * (static_cast<float>((h >> 8) & 255u) / 255.0f - 0.3f);      
        Rgb c{v * (1.0f + 2.0f * warm * 5.0f), v * (1.0f + warm * 2.5f), v * (1.0f - warm * 4.0f)};
        const uint32_t pick = (h >> 16) % 8u;
        if (pick == 0)  
            c = {v * 1.7f, v * 1.0f, v * 0.7f};
        else if (pick == 1)  
            c = {v * 0.55f, v * 0.55f, v * 0.55f};
        return c;
    }
     
    inline Rgb tintAt(const Rgb& charColour, float age, float rise = 1.8f)
    {
        const float u = std::clamp(age / std::max(rise, 1e-3f), 0.0f, 1.0f);
        const float k = 1.0f - (1.0f - u) * (1.0f - u);
        return {1.0f + (charColour.r - 1.0f) * k, 1.0f + (charColour.g - 1.0f) * k, 1.0f + (charColour.b - 1.0f) * k};
    }

     
    struct Scrap
    {
        V3 pos, vel;
        float spin = 0.0f, yaw = 0.0f, roll = 0.0f, scale = 1.0f;
        float age = 0.0f;
        bool landed = false;
        uint8_t model = 0;
    };
     
    inline std::vector<Scrap> scrapPlan(uint32_t seed, const V3& centre, float vehicleSize, int count)
    {
        std::vector<Scrap> out;
        Rng rng{seed * 7919u + 13u};
        for (int i = 0; i < count; ++i)
        {
            Scrap s;
            const float a = rng.range(0.0f, 6.2832f), sp = rng.range(3.5f, 11.0f) * (0.7f + 0.3f * vehicleSize);
            s.pos = centre + V3{std::cos(a) * 0.8f, rng.range(0.2f, 1.2f), std::sin(a) * 0.8f};
            s.vel = {std::cos(a) * sp, rng.range(4.0f, 10.0f), std::sin(a) * sp};
            s.spin = rng.range(-8.0f, 8.0f);
            s.yaw = rng.range(0.0f, 6.2832f);
            s.scale = rng.range(0.35f, 0.9f) * vehicleSize;
            s.model = static_cast<uint8_t>(i);
            out.push_back(s);
        }
        return out;
    }
     
     
    inline bool stepScrap(Scrap& s, float dt, float ground, float gravity = 9.81f)
    {
        if (s.landed)
        {
            s.age += dt;
            return false;
        }
        s.age += dt;
        s.vel.y -= gravity * dt;
        s.pos += s.vel * dt;
        s.yaw += s.spin * dt;
        s.roll += s.spin * dt * 0.7f;
        if (s.pos.y <= ground + 0.15f * s.scale)
        {
            s.pos.y = ground + 0.15f * s.scale;
            if (s.vel.y < -2.0f || len(V3{s.vel.x, 0, s.vel.z}) > 2.5f)
            {
                s.vel = V3{s.vel.x * 0.35f, -s.vel.y * 0.25f, s.vel.z * 0.35f};
                s.spin *= 0.4f;
            }
            else
            {
                s.vel = {};
                s.spin = 0.0f;
                s.landed = true;
                return true;
            }
        }
        return false;
    }
}
