 
 
 
 
 
 
 
 
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "Core.hpp"

namespace warzone::vfx
{
     
     
    enum Card : uint8_t
    {
        DUST,    
        SMOKE,   
        FIRE,    
        FLASH,   
        SPARK,   
        PUFF,    
        SHOCK,   
        DEBRIS,  
        FLAME,   
        WASH,    
        CARD_KINDS
    };
     
    constexpr uint32_t POOL[CARD_KINDS] = {56, 48, 24, 14, 28, 110, 10, 20, 24, 14};

     
     
     
    struct Wind
    {
        float dirX = 0.8f, dirZ = 0.6f;  
        float speed = 3.5f;              
        float gust = 0.45f;              

        V3 at(float time, float x, float z, float height) const
        {
            const float g = 1.0f + gust * (0.55f * std::sin(0.23f * time + 0.011f * x + 0.017f * z) +
                                           0.30f * std::sin(0.61f * time - 0.021f * z + 1.7f) + 0.15f * std::sin(1.3f * time + 0.03f * x));
            const float h = std::clamp(height, 0.0f, 200.0f);
            const float shear = 0.5f + 0.5f * std::min(h / 70.0f, 1.8f);
            const float veer = 0.35f * std::min(h / 150.0f, 1.0f);  
            const float c = std::cos(veer), s = std::sin(veer);
            const float dx = dirX * c - dirZ * s, dz = dirX * s + dirZ * c;
            const float k = speed * std::max(g, 0.1f) * shear;
            return {dx * k, 0.0f, dz * k};
        }
    };

     
     
    struct Motion
    {
        V3 pos, vel;
        float age = 0, life = 1;
        float rise = 0;    
        float windK = 0;   
        float drag = 0.6f;
    };
    inline void step(Motion& m, float dt, const Wind& wind, float time, float ground)
    {
        m.age += dt;
         
        V3 target{};
        if (m.windK > 0.0f)
            target = wind.at(time, m.pos.x, m.pos.z, m.pos.y - ground);
        const float k = std::min(1.0f, (m.drag + m.windK) * dt);
        m.vel.x += (target.x - m.vel.x) * k;
        m.vel.z += (target.z - m.vel.z) * k;
        const float want = m.rise * std::max(0.0f, 1.0f - m.age / std::max(m.life, 0.1f));
        m.vel.y += (want - m.vel.y) * std::min(1.0f, 1.6f * dt);
        m.pos = m.pos + m.vel * dt;
    }

     
    enum Tier : uint8_t
    {
        NEAR,
        MID,
        FAR,
        DISTANT,
        TIERS
    };
     
    inline Tier tierFor(float dist, float radius)
    {
        const float k = std::clamp(std::sqrt(std::max(radius, 0.1f) / 3.0f), 0.6f, 2.2f);
        return dist < 140.0f * k ? NEAR : dist < 350.0f * k ? MID : dist < 800.0f * k ? FAR : DISTANT;
    }
     
    inline float distanceBoost(float dist) { return std::clamp(dist / 260.0f, 1.0f, 2.6f); }

     
    constexpr int MAX_CARDS = 24;
    constexpr int CARDS_BY_TIER[TIERS] = {18, 9, 4, 3};

    struct Tint
    {
        float r = 1, g = 1, b = 1, a = 1;
    };
    enum Stage : uint8_t
    {
        ST_SHOCK,
        ST_FIREBALL,
        ST_DEBRIS,
        ST_SKIRT,
        ST_COLUMN,
        ST_SPARKS,
        ST_MISC
    };
     
    struct Spec
    {
        Card card = PUFF;
        Stage stage = ST_MISC;
        V3 pos{}, vel{};
        float s0 = 1, s1 = 2, life = 1, delay = 0;
        float rise = 0, windK = 0, drag = 0.6f;
        Tint tint;
        bool loop = false;
        bool grounded = false;  
    };

     
    inline int priority(const Spec& s)
    {
        switch (s.stage)
        {
        case ST_SHOCK:
        case ST_FIREBALL:
            return 5;
        case ST_SPARKS:
        case ST_DEBRIS:
            return 3;
        case ST_SKIRT:
            return 2;
        case ST_COLUMN:
            return 1;
        default:
            return 1;
        }
    }

    struct Plan
    {
        std::array<Spec, MAX_CARDS> cards;
        int count = 0;
        float lightPeak = 0, lightLife = 0;  
        float shake = 0;                     
        Spec& add(Card c, Stage st)
        {
            Spec& s = cards[std::min(count, MAX_CARDS - 1)];
            if (count < MAX_CARDS)
                ++count;
            s = Spec{};
            s.card = c;
            s.stage = st;
            return s;
        }
    };

     
     
     
    inline Plan explosion(float r, float dist, float agl, Tier tier, Rng& rng)
    {
        Plan p;
        const float boost = distanceBoost(dist);
        const bool air = agl > r * 2.0f;
        const float rr = std::max(r, 0.6f);
         
        const float linger = std::clamp(5.0f + rr * 1.1f, 6.0f, 14.0f) * (tier >= FAR ? 0.7f : 1.0f);
        auto jitter = [&](float a) { return rng.range(-a, a); };
        if (tier <= MID)
        {
            Spec& s = p.add(SHOCK, ST_SHOCK);
            s.pos = {0, 0.6f, 0};
            s.s0 = rr * 1.0f;
            s.s1 = rr * (tier == NEAR ? 6.0f : 4.5f);
            s.life = 0.38f;
            s.tint = {1.0f, 0.95f, 0.85f, 1.0f};
        }
        {
            Spec& s = p.add(FIRE, ST_FIREBALL);
            s.pos = {0, rr * 0.4f, 0};
            s.vel = {0, 1.5f, 0};
            s.s0 = rr * 1.5f * boost;
            s.s1 = rr * 3.4f * boost;
            s.life = tier >= FAR ? 1.3f : 1.1f;
            s.rise = 2.5f;
        }
        if (tier == NEAR)
        {
            Spec& s = p.add(FIRE, ST_FIREBALL);  
            s.pos = {jitter(rr * 0.2f), rr * 1.0f, jitter(rr * 0.2f)};
            s.vel = {0, 3.5f, 0};
            s.s0 = rr * 1.3f;
            s.s1 = rr * 3.0f;
            s.life = 1.5f;
            s.delay = 0.09f;
            s.rise = 4.5f;
        }
        if (tier <= MID)
        {
            const int n = tier == NEAR ? 2 : 1;
            for (int i = 0; i < n; ++i)
            {
                Spec& s = p.add(DEBRIS, ST_DEBRIS);
                s.pos = {jitter(rr * 0.3f), rr * 0.5f, jitter(rr * 0.3f)};
                s.s0 = rr * (1.6f + 0.4f * i);
                s.s1 = rr * (6.0f + 1.5f * i);
                s.life = 1.5f + 0.3f * i;
                s.delay = 0.03f * i;
                s.windK = 0.0f;
                s.drag = 1.2f;
                s.tint = {0.9f, 0.85f, 0.8f, 1.0f};
            }
        }
        if (!air)
        {
            const int n = tier == NEAR ? 5 : tier == MID ? 2 : 1;
            for (int i = 0; i < n; ++i)
            {
                const float a = (6.2832f * i) / n + jitter(0.5f);
                const float rad = rr * rng.range(0.35f, 1.0f);
                Spec& s = p.add(DUST, ST_SKIRT);
                s.pos = {std::cos(a) * rad * (n > 1 ? 1.0f : 0.0f), 0.5f, std::sin(a) * rad * (n > 1 ? 1.0f : 0.0f)};
                const float out = (n > 1 ? rng.range(4.0f, 9.0f) : 1.0f) * std::sqrt(rr / 3.0f);
                s.vel = {std::cos(a) * out, 0.5f, std::sin(a) * out};
                s.s0 = rr * 1.6f * boost;
                s.s1 = rr * 5.0f * boost;
                s.life = (4.5f + rng.range(0.0f, 1.5f)) * (tier >= FAR ? 0.8f : 1.0f);
                s.delay = 0.05f + 0.04f * i;
                s.windK = 0.5f;
                s.drag = 1.0f;
                s.grounded = true;
                s.tint = {0.95f, 0.92f, 0.9f, 1.0f};
            }
        }
        {
             
            const int n = tier == NEAR ? 5 : tier == MID ? 3 : tier == FAR ? 2 : 1;
            for (int i = 0; i < n; ++i)
            {
                const float f = n > 1 ? static_cast<float>(i) / (n - 1) : 0.5f;
                Spec& s = p.add(SMOKE, ST_COLUMN);
                s.pos = {jitter(rr * 0.25f), rr * (0.9f + 1.25f * i), jitter(rr * 0.25f)};
                s.vel = {jitter(0.4f), 2.6f + 1.6f * f, jitter(0.4f)};
                s.s0 = rr * (1.7f + 0.5f * f) * boost;
                s.s1 = rr * (4.2f + 1.8f * f) * boost * (n == 1 ? 1.4f : 1.0f);
                s.life = linger * (0.8f + 0.4f * f);
                s.delay = 0.14f + 0.26f * i;
                s.rise = 4.2f - 1.2f * f;
                s.windK = 0.35f + 0.25f * f;
                s.drag = 0.7f;
                const float dark = 0.38f + 0.35f * f;
                s.tint = {dark, dark, dark, 1.0f};
                s.grounded = true;
            }
        }
        if (tier == NEAR)
        {
            Spec& s = p.add(SPARK, ST_SPARKS);
            s.pos = {0, rr * 0.5f, 0};
            s.s0 = rr * 1.8f;
            s.s1 = rr * 3.2f;
            s.life = 0.7f;
            s.drag = 1.5f;
        }
         
        if (dist < 450.0f * std::clamp(std::sqrt(rr / 3.0f), 0.6f, 1.6f))
        {
            p.lightPeak = 7000.0f * std::clamp(rr / 3.0f, 0.4f, 2.5f);
            p.lightLife = 0.42f;
        }
        p.shake = std::clamp(rr / 4.5f, 0.3f, 1.5f);
        return p;
    }

     
     
    inline Plan smallBlast(float r, float dist, Tier tier, Rng& rng)
    {
        Plan p;
        const float boost = distanceBoost(dist);
        if (tier <= MID)
        {
            Spec& s = p.add(FIRE, ST_FIREBALL);
            s.s0 = r * 1.1f * boost;
            s.s1 = r * 2.2f * boost;
            s.life = 0.35f;
            s.vel = {0, 0.8f, 0};
        }
        if (tier == NEAR)
        {
            Spec& s = p.add(SPARK, ST_SPARKS);
            s.s0 = r * 1.8f;
            s.s1 = r * 2.6f;
            s.life = 0.4f;
            s.vel = {0, 0.6f, 0};
        }
        {
            Spec& s = p.add(DUST, ST_SKIRT);
            s.pos = {0, r * 0.4f, 0};
            s.vel = {rng.range(-0.5f, 0.5f), 1.4f, rng.range(-0.5f, 0.5f)};
            s.s0 = r * 1.2f * boost;
            s.s1 = r * 3.6f * boost * (tier >= FAR ? 1.3f : 1.0f);
            s.life = tier >= FAR ? 2.0f : 2.6f;
            s.windK = 0.4f;
            s.grounded = true;
        }
        p.shake = 0.12f;
        return p;
    }

     
     
     
    struct Governor
    {
        int budget = 40;
        int spent = 0;
        void newFrame() { spent = 0; }
        Tier admit(Tier want)
        {
            while (want < DISTANT && spent + CARDS_BY_TIER[want] > budget)
                want = static_cast<Tier>(want + 1);
            spent += CARDS_BY_TIER[want];
            return want;
        }
    };

     
     
    struct Pending
    {
        Spec spec;
        V3 origin;
        float due = 0;
    };
    template <size_t N>
    struct DelayQueue
    {
        std::array<Pending, N> q;
        size_t n = 0;
        uint32_t dropped = 0;
        bool push(const Spec& s, const V3& origin, float due)
        {
            if (n < N)
            {
                q[n++] = {s, origin, due};
                return true;
            }
            size_t worst = 0;
            for (size_t i = 1; i < n; ++i)
                if (priority(q[i].spec) < priority(q[worst].spec) || (priority(q[i].spec) == priority(q[worst].spec) && q[i].due > q[worst].due))
                    worst = i;
            ++dropped;
            if (priority(q[worst].spec) >= priority(s))
                return false;
            q[worst] = {s, origin, due};
            return true;
        }
         
        template <class F>
        int pop(float now, int maxSpawns, F&& f)
        {
            int done = 0;
            size_t keep = 0;
            for (size_t i = 0; i < n; ++i)
            {
                if (q[i].due <= now && done < maxSpawns)
                {
                    f(q[i]);
                    ++done;
                }
                else
                    q[keep++] = q[i];
            }
            n = keep;
            return done;
        }
    };

     
     
    struct LightState
    {
        float age = -1, life = 0.5f, peak = 0, dist = 0;
    };
    inline float lightScore(float age, float life, float peak, float dist)
    {
        if (age < 0.0f)
            return -1.0f;
        const float k = std::max(0.0f, 1.0f - age / std::max(life, 1e-3f));
        return peak * k * k / (dist * dist + 400.0f);
    }
    template <class C>  
    int pickLight(const C& l, float peak, float dist)
    {
        int best = -1;
        float bestScore = lightScore(0.0f, 1.0f, peak, dist);
        for (size_t i = 0; i < l.size(); ++i)
        {
            if (l[i].age < 0.0f)
                return static_cast<int>(i);
            const float sc = lightScore(l[i].age, l[i].life, l[i].peak, l[i].dist);
            if (sc < bestScore)
            {
                bestScore = sc;
                best = static_cast<int>(i);
            }
        }
        return best;
    }

     
    struct Plume
    {
        float interval = 0.25f;  
        float s0 = 2.5f, s1 = 12.0f, life = 7.0f, rise = 3.6f, windK = 0.9f;
        Tint tint;
        float flameScale = 1.0f;
    };
     
     
     
    inline Plume wreckPlume(Tier tier, float left, float total, float size)
    {
        Plume p;
        const float u = std::clamp(left / std::max(total, 1e-3f), 0.0f, 1.0f);  
        static constexpr float INTERVAL[TIERS] = {0.24f, 0.5f, 1.1f, 2.4f};
        static constexpr float GROW[TIERS] = {1.0f, 1.2f, 1.7f, 2.4f};
        p.interval = INTERVAL[tier] * (0.8f + 0.5f * (1.0f - u));
        p.s0 = 2.2f * size * GROW[tier] * 0.5f + 1.0f;
        p.s1 = (9.0f + 5.0f * u) * size * GROW[tier];
        p.life = 7.5f * (0.8f + 0.4f * GROW[tier] * 0.5f);
        p.rise = 3.2f + 1.6f * u;
        p.windK = 0.7f;
        const float c = 0.30f + 0.4f * (1.0f - u);  
        p.tint = {c, c, c, 0.55f + 0.45f * u};
        p.flameScale = 0.35f + 0.65f * std::clamp(u / 0.3f, 0.0f, 1.0f);
        return p;
    }
     
    inline float partPuffInterval(bool flying, Tier tier)
    {
        static constexpr float MUL[TIERS] = {1.0f, 2.0f, 4.0f, 8.0f};
        return (flying ? 0.05f : 0.6f) * MUL[tier];
    }
     
    inline float fadeAlpha(float u, float inEnd, float outStart)
    {
        const float a = std::clamp(u / std::max(inEnd, 1e-3f), 0.0f, 1.0f);
        const float b = std::clamp((1.0f - u) / std::max(1.0f - outStart, 1e-3f), 0.0f, 1.0f);
        return a * a * (3 - 2 * a) * b * b * (3 - 2 * b);
    }

     
    struct Muzzle
    {
        float flashS0 = 1, flashS1 = 2, flashLife = 0.1f;
        int smokePuffs = 0;       
        int groundDust = 0;       
        bool shock = false;       
        float lightPeak = 0, lightLife = 0.1f;
    };
     
    inline Muzzle muzzle(int kind, float dist, Tier tier)
    {
        Muzzle m;
        const float b = distanceBoost(dist);
        if (kind == 2)
        {
            m.flashS0 = 3.4f * b;
            m.flashS1 = 5.0f * b;
            m.flashLife = 0.20f;
            m.smokePuffs = tier == NEAR ? 3 : tier == MID ? 2 : 1;
            m.groundDust = tier == NEAR ? 4 : tier == MID ? 2 : 0;
            m.shock = tier <= MID;
            m.lightPeak = dist < 500.0f ? 5000.0f : 0.0f;
            m.lightLife = 0.18f;
        }
        else if (kind == 1)
        {
            m.flashS0 = 1.6f * b;
            m.flashS1 = 2.4f * b;
            m.flashLife = 0.09f;
            m.smokePuffs = tier == NEAR ? 1 : 0;
            m.lightPeak = dist < 160.0f ? 1400.0f : 0.0f;
            m.lightLife = 0.07f;
        }
        else
        {
            m.flashS0 = 0.9f * b;
            m.flashS1 = 1.4f * b;
            m.flashLife = 0.08f;
        }
        return m;
    }

    enum Surface : uint8_t
    {
        GROUND,
        PIECE,
        VEHICLE
    };
    struct Impact
    {
        float dustS0 = 0.5f, dustS1 = 2.2f, dustLife = 1.6f;
        bool dust = true;
        bool sparks = false;
        float sparkSize = 1.0f;
        bool debris = false;  
        float debrisSize = 1.5f;
    };
     
    inline Impact impact(Surface s, float size, float dist, Tier tier, Rng& rng)
    {
        Impact i;
        const float b = distanceBoost(dist);
        i.dustS0 = 0.5f * size * b;
        i.dustS1 = (s == GROUND ? 2.8f : 2.2f) * size * b;
        i.dustLife = s == GROUND ? 1.9f : 1.5f;
        i.dust = s != VEHICLE || tier <= MID;
        const float sparkChance = s == VEHICLE ? 1.0f : s == PIECE ? 0.5f : 0.25f;
        i.sparks = tier <= MID && rng.chance(sparkChance);
        i.sparkSize = (s == VEHICLE ? 1.6f : 1.0f) * size;
        i.debris = s == PIECE && tier == NEAR && rng.chance(0.5f);
        i.debrisSize = 1.2f * size;
        return i;
    }

     
     
    inline void tracerShape(float dist, float length, float width, float& outLength, float& outWidth)
    {
        outWidth = std::clamp(std::max(width, dist * 0.0018f), width, 4.0f);
        outLength = std::clamp(std::max(length, dist * 0.025f), length, 60.0f);
    }

    struct Wash
    {
        bool active = false;
        float interval = 0.2f;
        int cards = 1;
        float s0 = 3, s1 = 10, life = 2.8f, speed = 8, radiusMin = 3, radiusMax = 9;
        float strength = 0;
    };
     
     
    inline Wash rotorWash(float agl, float rotor, Tier tier)
    {
        Wash w;
        if (rotor < 0.5f || agl > 30.0f)
            return w;
        const float strength = std::clamp(1.0f - agl / 30.0f, 0.0f, 1.0f) * std::min(1.0f, rotor);
        static constexpr float MUL[TIERS] = {1.0f, 1.8f, 3.5f, 7.0f};
        w.active = strength > 0.02f;
        w.strength = strength;
         
        w.interval = (0.22f + agl * 0.01f) * MUL[tier] / (0.4f + 0.6f * strength);
        w.cards = 1;
        const float boost = tier == NEAR ? 1.0f : tier == MID ? 1.3f : 1.8f;
        w.s0 = (3.0f + 3.0f * strength) * boost;
        w.s1 = (9.0f + 8.0f * strength) * boost;
        w.life = 2.4f + 0.7f * strength;
        w.speed = 6.0f + 6.0f * strength;
        w.radiusMin = 3.0f;
        w.radiusMax = 9.0f + 4.0f * strength;
        return w;
    }

     
    inline float trailInterval(bool missile, Tier tier)
    {
        static constexpr float MUL[TIERS] = {1.0f, 1.5f, 2.5f, 4.0f};
        return (missile ? 0.03f : 0.06f) * MUL[tier];
    }
}  
