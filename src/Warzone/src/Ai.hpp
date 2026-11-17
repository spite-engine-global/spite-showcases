 
 
 
 
 
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <queue>
#include <vector>

#include "Core.hpp"
#include "Match.hpp"

namespace warzone
{
    constexpr float AI_PI = 3.14159265f;
    inline float wrapAngle(float a)
    {
        while (a > AI_PI)
            a -= 2 * AI_PI;
        while (a < -AI_PI)
            a += 2 * AI_PI;
        return a;
    }
    inline float yawTo(const V3& from, const V3& to) { return std::atan2(to.x - from.x, to.z - from.z); }
    inline V3 forwardOf(float yaw) { return {std::sin(yaw), 0.0f, std::cos(yaw)}; }
    inline float flatDist(const V3& a, const V3& b) { return std::hypot(a.x - b.x, a.z - b.z); }

     
     
     
    struct UnitGrid
    {
        float half = 1024.0f, cell = 64.0f;
        int n = 32;
        std::vector<int> head, next;
        std::vector<V3> pos;
        mutable std::vector<uint32_t> stamp;
        mutable uint32_t query = 0;

        int clampCell(float v) const { return std::clamp(static_cast<int>((v + half) / cell), 0, n - 1); }
        void build(float halfExtent, float cellSize, const std::vector<V3>& positions, const std::vector<uint8_t>& live)
        {
            half = halfExtent;
            cell = cellSize;
            n = std::max(1, static_cast<int>(std::ceil(2 * half / cell)));
            head.assign(static_cast<size_t>(n) * n, -1);
            next.assign(positions.size(), -1);
            pos = positions;
            stamp.resize(positions.size(), 0);
            for (int i = 0; i < static_cast<int>(positions.size()); ++i)
            {
                if (!live[i])
                    continue;
                const int c = clampCell(positions[i].z) * n + clampCell(positions[i].x);
                next[i] = head[c];
                head[c] = i;
            }
        }
         
        template <class F> void forEachIn(float x0, float z0, float x1, float z1, F&& f) const
        {
            const int i0 = clampCell(x0), i1 = clampCell(x1), j0 = clampCell(z0), j1 = clampCell(z1);
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i)
                    for (int u = head[static_cast<size_t>(j) * n + i]; u >= 0; u = next[u])
                        f(u);
        }
         
        template <class F> void forEachNearSegment(const V3& a, const V3& b, float pad, F&& f) const
        {
            if (head.empty())
                return;
            ++query;
            if (query == 0)
            {
                std::fill(stamp.begin(), stamp.end(), 0u);
                query = 1;
            }
            const float L = flatDist(a, b);
            const int steps = std::max(1, static_cast<int>(std::ceil(L / (cell * 0.5f))));
            for (int s = 0; s <= steps; ++s)
            {
                const float t = static_cast<float>(s) / steps;
                const float x = a.x + (b.x - a.x) * t, z = a.z + (b.z - a.z) * t;
                const float r = pad + cell * 0.25f;
                forEachIn(x - r, z - r, x + r, z + r,
                          [&](int u)
                          {
                              if (stamp[u] == query)
                                  return;
                              stamp[u] = query;
                              f(u);
                          });
            }
        }
         
        template <class P> int nearest(const V3& p, float radius, P&& pred) const
        {
            int best = -1;
            float bd = radius * radius;
            forEachIn(p.x - radius, p.z - radius, p.x + radius, p.z + radius,
                      [&](int u)
                      {
                          const float dx = pos[u].x - p.x, dz = pos[u].z - p.z, d = dx * dx + dz * dz;
                          if (d <= bd && pred(u))
                          {
                              bd = d;
                              best = u;
                          }
                      });
            return best;
        }
    };

    struct NearestThree
    {
        struct Entry { int i; float d; };
        std::array<Entry, 3> entries{};
        size_t count = 0;
        void add(int i, float d)
        {
            size_t k = 0;
            while (k < count && entries[k].d <= d) ++k;
            if (k == entries.size()) return;
            count = std::min(count + 1, entries.size());
            for (size_t j = count - 1; j > k; --j) entries[j] = entries[j-1];
            entries[k] = {i, d};
        }
    };

     
    struct NavGrid
    {
        float half = 1024.0f, cell = 8.0f;
        int n = 256;
        std::vector<uint8_t> blocked;
         
        mutable std::vector<float> m_g;
        mutable std::vector<int> m_parent;
        mutable std::vector<uint32_t> m_seen, m_closed;
        mutable uint32_t m_gen = 0;

        void init(float halfExtent, float cellSize)
        {
            half = halfExtent;
            cell = cellSize;
            n = static_cast<int>(std::ceil(2 * half / cell));
            blocked.assign(static_cast<size_t>(n) * n, 0);
        }
        int ci(float v) const { return static_cast<int>(std::floor((v + half) / cell)); }
        bool inside(int i, int j) const { return i >= 0 && j >= 0 && i < n && j < n; }
        bool cellBlocked(int i, int j) const { return !inside(i, j) || blocked[static_cast<size_t>(j) * n + i]; }
        bool isBlocked(float x, float z) const { return cellBlocked(ci(x), ci(z)); }
        V3 centre(int i, int j) const { return {-half + (i + 0.5f) * cell, 0.0f, -half + (j + 0.5f) * cell}; }
        void blockRect(float x0, float z0, float x1, float z1, float margin)
        {
            const int i0 = std::max(0, ci(x0 - margin)), i1 = std::min(n - 1, ci(x1 + margin));
            const int j0 = std::max(0, ci(z0 - margin)), j1 = std::min(n - 1, ci(z1 + margin));
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i)
                    blocked[static_cast<size_t>(j) * n + i] = 1;
        }
         
         
        void blockCentres(float x0, float z0, float x1, float z1, float margin)
        {
            const int i0 = std::max(0, static_cast<int>(std::ceil((x0 - margin + half) / cell - 0.5f)));
            const int i1 = std::min(n - 1, static_cast<int>(std::floor((x1 + margin + half) / cell - 0.5f)));
            const int j0 = std::max(0, static_cast<int>(std::ceil((z0 - margin + half) / cell - 0.5f)));
            const int j1 = std::min(n - 1, static_cast<int>(std::floor((z1 + margin + half) / cell - 0.5f)));
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i)
                    blocked[static_cast<size_t>(j) * n + i] = 1;
        }
         
        bool lineClear(const V3& a, const V3& b) const
        {
            const float L = flatDist(a, b);
            const int steps = std::max(1, static_cast<int>(std::ceil(L / (cell * 0.5f))));
            for (int s = 0; s <= steps; ++s)
            {
                const float t = static_cast<float>(s) / steps;
                if (isBlocked(a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t))
                    return false;
            }
            return true;
        }
         
        bool nearestFree(int& i, int& j, int r = 12) const
        {
            if (!cellBlocked(i, j))
                return true;
            for (int k = 1; k <= r; ++k)
                for (int dj = -k; dj <= k; ++dj)
                    for (int di = -k; di <= k; ++di)
                        if ((std::abs(di) == k || std::abs(dj) == k) && !cellBlocked(i + di, j + dj))
                        {
                            i += di;
                            j += dj;
                            return true;
                        }
            return false;
        }
         
         
         
        bool findPath(const V3& from, const V3& to, std::vector<V3>& out, int maxExpand = 12000) const
        {
            out.clear();
            if (lineClear(from, to))
            {
                out.push_back(to);
                return true;
            }
            int si = ci(from.x), sj = ci(from.z), gi = ci(to.x), gj = ci(to.z);
            si = std::clamp(si, 0, n - 1), sj = std::clamp(sj, 0, n - 1), gi = std::clamp(gi, 0, n - 1), gj = std::clamp(gj, 0, n - 1);
            if (!nearestFree(si, sj) || !nearestFree(gi, gj))
                return false;
            const size_t N = static_cast<size_t>(n) * n;
            if (m_g.size() != N)
            {
                m_g.assign(N, 1e30f);
                m_parent.assign(N, -1);
                m_seen.assign(N, 0);
                m_closed.assign(N, 0);
                m_gen = 0;
            }
            if (++m_gen == 0)
            {
                std::fill(m_seen.begin(), m_seen.end(), 0u);
                std::fill(m_closed.begin(), m_closed.end(), 0u);
                m_gen = 1;
            }
            const uint32_t gen = m_gen;
            auto gOf = [&](int c) { return m_seen[c] == gen ? m_g[c] : 1e30f; };
            auto setG = [&](int c, float v, int par)
            {
                m_seen[c] = gen;
                m_g[c] = v;
                m_parent[c] = par;
            };
            auto parentOf = [&](int c) { return m_seen[c] == gen ? m_parent[c] : -1; };
            auto h = [&](int i, int j)
            {
                const float dx = static_cast<float>(std::abs(i - gi)), dz = static_cast<float>(std::abs(j - gj));
                return (dx + dz) + (1.41421356f - 2.0f) * std::min(dx, dz);
            };
            using Node = std::pair<float, int>;
            std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
            const int s = sj * n + si, goal = gj * n + gi;
            setG(s, 0.0f, -1);
            open.push({h(si, sj), s});
            int best = s, expanded = 0;
            float bestH = h(si, sj);
            bool found = false;
            while (!open.empty() && expanded < maxExpand)
            {
                const int c = open.top().second;
                open.pop();
                if (m_closed[c] == gen)
                    continue;
                m_closed[c] = gen;
                ++expanded;
                if (c == goal)
                {
                    found = true;
                    best = c;
                    break;
                }
                const int i = c % n, j = c / n;
                const float hc = h(i, j);
                if (hc < bestH)
                {
                    bestH = hc;
                    best = c;
                }
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di)
                    {
                        if (!di && !dj)
                            continue;
                        const int ni = i + di, nj = j + dj;
                        if (cellBlocked(ni, nj) || (di && dj && (cellBlocked(i + di, j) || cellBlocked(i, j + dj))))
                            continue;
                        const int nc = nj * n + ni;
                        const float ng = gOf(c) + (di && dj ? 1.41421356f : 1.0f);
                        if (ng < gOf(nc))
                        {
                            setG(nc, ng, c);
                            open.push({ng + h(ni, nj), nc});
                        }
                    }
            }
            std::vector<V3> cells;
            for (int c = best; c >= 0 && c != s; c = parentOf(c))
                cells.push_back(centre(c % n, c / n));
            std::reverse(cells.begin(), cells.end());
            if (found && !cells.empty())
                cells.back() = isBlocked(to.x, to.z) ? cells.back() : to;
             
            V3 at = from;
            size_t k = 0;
            while (k < cells.size())
            {
                size_t far = k;
                for (size_t m = cells.size(); m-- > k + 1;)
                    if (lineClear(at, cells[m]))
                    {
                        far = m;
                        break;
                    }
                out.push_back(cells[far]);
                at = cells[far];
                k = far + 1;
            }
            return found;
        }
    };

     
    inline bool canEngage(VehicleType shooter, VehicleType target)
    {
        const bool air = domainOf(target) == Domain::Air;
        switch (shooter)
        {
        case VehicleType::Tank:
            return !air;  
        case VehicleType::Amp:
            return target != VehicleType::Plane;  
        case VehicleType::Heli:
            return target != VehicleType::Plane;
        case VehicleType::Plane:
            return !air;  
        default:
            return false;
        }
    }
     
    inline float& engageScale()
    {
        static float s = 1.0f;
        return s;
    }
    inline float engageRange(VehicleType t)
    {
        constexpr float r[] = {900.0f, 550.0f, 1000.0f, 1800.0f};
        return r[static_cast<int>(t)] * engageScale();
    }
     
    constexpr float HUNT_RANGE = 1400.0f;

    struct Candidate
    {
        int index;
        V3 pos;
        VehicleType type;
        bool visible;
    };
     
    inline int pickTarget(VehicleType self, const V3& at, const std::vector<Candidate>& c)
    {
        int best = -1;
        float bd = engageRange(self);
        for (const auto& k : c)
        {
            if (!k.visible || !canEngage(self, k.type))
                continue;
            const float d = len(k.pos - at);
            if (d < bd)
            {
                bd = d;
                best = k.index;
            }
        }
        return best;
    }

     
    struct AiParams
    {
        float maxSpeed, accel, turnRate;  
        float fireInterval;               
        int burst;                        
        float burstGap;                   
        float spread;                     
        float cruiseAgl;                  
        float standoff;                   
        float thinkInterval;              
    };
    inline const AiParams& aiParams(VehicleType t)
    {
        static const AiParams p[] = {
            {11.0f, 3.0f, 0.55f, 7.0f, 1, 0.0f, 0.006f, 0.0f, 450.0f, 0.8f},    
            {15.0f, 5.0f, 0.9f, 2.6f, 6, 0.12f, 0.010f, 0.0f, 280.0f, 0.6f},    
            {55.0f, 8.0f, 0.8f, 3.5f, 1, 0.0f, 0.0f, 75.0f, 480.0f, 0.7f},      
            {140.0f, 10.0f, 0.42f, 9.0f, 1, 0.0f, 0.0f, 230.0f, 0.0f, 1.0f},    
        };
        return p[static_cast<int>(t)];
    }

     
    enum class PlanePhase : uint8_t
    {
        Cruise,
        Attack,
        Egress
    };

    struct AiState
    {
        VehicleType type = VehicleType::Tank;
        V3 pos;
        float yaw = 0.0f, speed = 0.0f, vy = 0.0f;
        V3 vel;
         
        std::vector<V3> path;
        size_t pathIdx = 0;
        V3 goal;
        bool hasGoal = false;
        float stuck = 0.0f;
         
        int target = -1;  
        V3 targetPos;
        float fireTimer = 0.0f, burstTimer = 0.0f;
        int burstLeft = 0;
        float orbit = 0.0f;  
        PlanePhase phase = PlanePhase::Cruise;
        float phaseTimer = 0.0f;
        bool dropped = false;
        bool runIn = false;  
        uint32_t seed = 1;
    };

     
    struct AiFire
    {
        bool fire = false;
        V3 aim;  
    };

     
    inline float bombReleaseDistance(float v, float h) { return v * std::sqrt(2.0f * std::max(h, 1.0f) / 9.81f); }

     
    inline float turnToward(float yaw, float want, float rate, float dt)
    {
        const float d = wrapAngle(want - yaw);
        const float m = rate * dt;
        return wrapAngle(yaw + std::clamp(d, -m, m));
    }

     
     
    inline AiFire stepGround(AiState& s, float dt, bool targetVisible)
    {
        const AiParams& p = aiParams(s.type);
        AiFire f;
        float wantSpeed = 0.0f, wantYaw = s.yaw;
        const bool engaged = s.target >= 0 && targetVisible;
        const float dTarget = s.target >= 0 ? flatDist(s.pos, s.targetPos) : 1e9f;
        if (s.pathIdx < s.path.size())
        {
            const V3& wp = s.path[s.pathIdx];
            if (flatDist(s.pos, wp) < 6.0f)
                ++s.pathIdx;
            if (s.pathIdx < s.path.size())
            {
                wantYaw = yawTo(s.pos, s.path[s.pathIdx]);
                const float turn = std::abs(wrapAngle(wantYaw - s.yaw));
                wantSpeed = p.maxSpeed * std::clamp(1.2f - turn, 0.15f, 1.0f);
            }
        }
        if (engaged && dTarget < p.standoff)
            wantSpeed = 0.0f;  
        if (engaged && wantSpeed < 0.5f)
            wantYaw = yawTo(s.pos, s.targetPos);  
        s.yaw = turnToward(s.yaw, wantYaw, p.turnRate, dt);
        s.speed += std::clamp(wantSpeed - s.speed, -p.accel * 2.0f * dt, p.accel * dt);
        const V3 fwd = forwardOf(s.yaw);
        s.vel = fwd * s.speed;
        s.pos = s.pos + s.vel * dt;
         
        s.fireTimer -= dt;
        s.burstTimer -= dt;
        if (engaged && dTarget < engageRange(s.type))
        {
            if (s.burstLeft > 0 && s.burstTimer <= 0.0f)
            {
                --s.burstLeft;
                s.burstTimer = p.burstGap;
                f.fire = true;
            }
            else if (s.burstLeft == 0 && s.fireTimer <= 0.0f)
            {
                s.fireTimer = p.fireInterval;
                s.burstLeft = p.burst - 1;
                s.burstTimer = p.burstGap;
                f.fire = true;
            }
            f.aim = s.targetPos;
        }
        else
            s.burstLeft = 0;
        return f;
    }

     
     
    inline AiFire stepHeli(AiState& s, float dt, bool targetVisible, float groundY)
    {
        const AiParams& p = aiParams(s.type);
        AiFire f;
        const bool engaged = s.target >= 0 && targetVisible;
        V3 dest = s.hasGoal ? s.goal : s.pos, destVel{};
        float faceYaw;
        if (engaged)
        {
            const float rate = 0.06f * ((s.seed & 1) ? 1.0f : -1.0f);
            s.orbit = wrapAngle(s.orbit + rate * dt);
            dest = s.targetPos + V3{std::sin(s.orbit), 0.0f, std::cos(s.orbit)} * p.standoff;
            destVel = V3{std::cos(s.orbit), 0.0f, -std::sin(s.orbit)} * (rate * p.standoff);  
            faceYaw = yawTo(s.pos, s.targetPos);
        }
        else
            faceYaw = flatDist(s.pos, dest) > 5.0f ? yawTo(s.pos, dest) : s.yaw;
        V3 to = dest - s.pos;
        to.y = 0.0f;
        const float d = len(to);
         
        const float want = std::min(p.maxSpeed, 0.7f * std::sqrt(2.0f * p.accel * d));
        V3 wantVel = (d > 1e-3f ? to * (want / d) : V3{}) + destVel;
        if (const float wl = len(wantVel); wl > p.maxSpeed)
            wantVel = wantVel * (p.maxSpeed / wl);
        V3 dv = wantVel - V3{s.vel.x, 0.0f, s.vel.z};
        const float dl = len(dv), maxDv = p.accel * dt;
        if (dl > maxDv)
            dv = dv * (maxDv / dl);
        s.vel.x += dv.x;
        s.vel.z += dv.z;
        const float wantY = groundY + p.cruiseAgl;
        s.vy += std::clamp((wantY - s.pos.y) * 0.8f - s.vy, -6.0f * dt, 6.0f * dt);
        s.vel.y = s.vy;
        s.pos = s.pos + s.vel * dt;
        s.pos.y = std::max(s.pos.y, groundY + 12.0f);
        s.yaw = turnToward(s.yaw, faceYaw, p.turnRate, dt);
        s.speed = std::hypot(s.vel.x, s.vel.z);
        s.fireTimer -= dt;
        if (engaged && len(s.targetPos - s.pos) < engageRange(s.type) && std::abs(wrapAngle(faceYaw - s.yaw)) < 0.25f && s.fireTimer <= 0.0f)
        {
            s.fireTimer = p.fireInterval;
            f.fire = true;
            f.aim = s.targetPos;
        }
        return f;
    }

     
     
     
    inline AiFire stepPlane(AiState& s, float dt, bool hasTarget, float groundY)
    {
        const AiParams& p = aiParams(s.type);
        AiFire f;
        s.phaseTimer += dt;
        float wantYaw = s.yaw;
        float wantY = std::max(groundY, 15.0f) + p.cruiseAgl;
        switch (s.phase)
        {
        case PlanePhase::Cruise:
            if (s.hasGoal)
                wantYaw = yawTo(s.pos, s.goal);
            if (hasTarget && s.target >= 0)
            {
                s.phase = PlanePhase::Attack;
                s.phaseTimer = 0.0f;
                s.dropped = false;
                s.runIn = false;
            }
            break;
        case PlanePhase::Attack:
        {
            if (!hasTarget || s.target < 0 || s.phaseTimer > 40.0f)
            {
                s.phase = PlanePhase::Cruise;
                break;
            }
            const float d = flatDist(s.pos, s.targetPos);
            const float release = bombReleaseDistance(s.speed, s.pos.y - s.targetPos.y);
            const float toward = yawTo(s.pos, s.targetPos);
            if (!s.runIn)
            {
                 
                if (d < release + 500.0f)
                {
                    wantYaw = std::abs(wrapAngle(toward - s.yaw)) < AI_PI / 2 ? wrapAngle(toward + AI_PI) : s.yaw;
                    break;
                }
                s.runIn = true;
            }
            wantYaw = toward;
            const bool lined = std::abs(wrapAngle(wantYaw - s.yaw)) < 0.18f;
            if (lined && d < release + 60.0f && d > release - 250.0f && s.fireTimer <= 0.0f)
            {
                f.fire = true;
                f.aim = s.targetPos;
                s.fireTimer = p.fireInterval;
                s.dropped = true;
                s.phase = PlanePhase::Egress;
                s.phaseTimer = 0.0f;
            }
            else if (d < release - 250.0f)
            {
                s.runIn = false;
                s.phase = PlanePhase::Egress;  
                s.phaseTimer = 0.0f;
            }
            break;
        }
        case PlanePhase::Egress:
            wantY += 60.0f;
            if (s.phaseTimer > 9.0f)
            {
                s.phase = PlanePhase::Cruise;
                s.phaseTimer = 0.0f;
            }
            break;
        }
        s.fireTimer -= dt;
        s.yaw = turnToward(s.yaw, wantYaw, p.turnRate, dt);
        s.speed += std::clamp(p.maxSpeed - s.speed, -p.accel * dt, p.accel * dt);
        const float climb = std::clamp((wantY - s.pos.y) * 0.25f, -25.0f, 25.0f);
        s.vy += std::clamp(climb - s.vy, -12.0f * dt, 12.0f * dt);
        const V3 fwd = forwardOf(s.yaw);
        s.vel = V3{fwd.x * s.speed, s.vy, fwd.z * s.speed};
        s.pos = s.pos + s.vel * dt;
        s.pos.y = std::max(s.pos.y, groundY + 40.0f);
        return f;
    }
}  
