 
 
 
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Core.hpp"

namespace warzone
{
    enum class VehicleType : uint8_t
    {
        Tank,
        Amp,
        Heli,
        Plane,
        Count
    };
    enum class Domain : uint8_t
    {
        Ground,
        Air
    };
    inline Domain domainOf(VehicleType t) { return t == VehicleType::Heli || t == VehicleType::Plane ? Domain::Air : Domain::Ground; }
    inline const char* typeName(VehicleType t)
    {
        constexpr const char* names[] = {"TANK", "AMP", "HELICOPTER", "AIRPLANE"};
        return names[static_cast<int>(t)];
    }

    struct SpawnPoint
    {
        const char* name;
        float x, z;   
        float yaw;    
        Domain domain;
    };

    struct MatchRules
    {
        float duration = 300.0f;         
        float playerRespawn = 4.0f;      
        float targetRespawn = 8.0f;      
        float safeRadius = 120.0f;       
        float heliAltitude = 90.0f;      
        float planeAltitude = 260.0f;
        float planeSpeed = 130.0f;       
        int killScore = 100;
        int suicidePenalty = 50;
    };

     
    inline V3 spawnPosition(const SpawnPoint& p, VehicleType t, float ground, const MatchRules& r)
    {
        const float up = t == VehicleType::Plane ? r.planeAltitude : t == VehicleType::Heli ? r.heliAltitude : 0.0f;
        return {p.x, ground + up, p.z};
    }

    inline float nearestEnemy(const SpawnPoint& p, std::span<const V3> enemies)
    {
        float best = 1e9f;
        for (const V3& e : enemies)
            best = std::min(best, std::sqrt((e.x - p.x) * (e.x - p.x) + (e.z - p.z) * (e.z - p.z)));
        return best;
    }

     
     
    inline int pickSpawn(std::span<const SpawnPoint> points, Domain d, std::span<const V3> enemies, float safeRadius, int preferred = -1)
    {
        if (preferred >= 0 && preferred < static_cast<int>(points.size()) && points[preferred].domain == d &&
            nearestEnemy(points[preferred], enemies) >= safeRadius)
            return preferred;
        int best = -1;
        float bestD = -1.0f;
        for (int i = 0; i < static_cast<int>(points.size()); ++i)
        {
            if (points[i].domain != d)
                continue;
            const float n = nearestEnemy(points[i], enemies);
            if (n > bestD + 1e-3f)
            {
                best = i;
                bestD = n;
            }
        }
        return best;
    }

     
    inline int cycleSpawn(std::span<const SpawnPoint> points, Domain d, int from, int step)
    {
        const int n = static_cast<int>(points.size());
        for (int k = 1; k <= n; ++k)
        {
            const int i = (((from + step * k) % n) + n) % n;
            if (points[i].domain == d)
                return i;
        }
        return from;
    }

     
    struct Combatant
    {
        std::string name;
        int kills = 0, deaths = 0, suicides = 0;
        bool player = false;
        int score(const MatchRules& r) const { return kills * r.killScore - suicides * r.suicidePenalty; }
    };

     
    inline std::vector<int> scoreboard(std::span<const Combatant> c, const MatchRules& r)
    {
        std::vector<int> order(c.size());
        for (int i = 0; i < static_cast<int>(c.size()); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(),
                         [&](int a, int b)
                         {
                             const int sa = c[a].score(r), sb = c[b].score(r);
                             if (sa != sb)
                                 return sa > sb;
                             if (c[a].kills != c[b].kills)
                                 return c[a].kills > c[b].kills;
                             if (c[a].deaths != c[b].deaths)
                                 return c[a].deaths < c[b].deaths;
                             return c[a].name < c[b].name;
                         });
        return order;
    }

     
    inline void recordKill(std::span<Combatant> c, int victim, int attacker)
    {
        if (victim < 0 || victim >= static_cast<int>(c.size()))
            return;
        ++c[victim].deaths;
        if (attacker < 0 || attacker >= static_cast<int>(c.size()) || attacker == victim)
            ++c[victim].suicides;
        else
            ++c[attacker].kills;
    }

     
    struct MatchClock
    {
        float left = 0.0f;
        bool running = false, over = false;
        void start(float duration)
        {
            left = duration;
            running = true;
            over = false;
        }
         
        bool step(float dt)
        {
            if (!running || over)
                return false;
            left = std::max(0.0f, left - dt);
            if (left <= 0.0f)
            {
                over = true;
                return true;
            }
            return false;
        }
    };

     
    struct RespawnTimer
    {
        float t = -1.0f;
        void arm(float delay) { t = delay; }
        bool armed() const { return t >= 0.0f; }
        bool step(float dt)
        {
            if (t < 0.0f)
                return false;
            t -= dt;
            if (t <= 0.0f)
            {
                t = -1.0f;
                return true;
            }
            return false;
        }
    };
}  
