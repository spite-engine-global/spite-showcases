#include "FormationsShowcase.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "application/input/InputSystem.hpp"
#include "assetruntime/AssetComponents.hpp"
#include "base/FlyBoost.hpp"
#include "base/MathFunctions.hpp"
#include "base/ServiceLocator.hpp"
#include "control/ControlServer.hpp"
#include "ecs/Query.hpp"
#include "ecs/World.hpp"
#include "engine/Module.hpp"
#include "profiling/GameZone.hpp"
#include "renderruntime/GameView.hpp"
#include "renderruntime/LightComponents.hpp"
#include "renderruntime/RenderingComponents.hpp"
#include "serialization/FormatWalk.hpp"
#include "world/TransformComponents.hpp"

 
 
 
 
 
namespace
{
    using namespace spite;
    using namespace spite::formations;

    constexpr const char* BOX_MESH = "cube2";  
    constexpr const char* SPHERE_MESH = "sphere";
    constexpr const char* CUBE_MATERIAL = "cube_mat_1";
    constexpr float PI = 3.14159265358979f;
    constexpr float TAU = 6.28318530717959f;
    constexpr float GOLDEN_ANGLE = PI * (3.0f - 2.2360679775f);
    constexpr float STEP_SECONDS = 1.0f / static_cast<float>(STEPS_PER_SECOND);
    constexpr float SPHERE_RADIUS = 13.0f;
    constexpr float GALAXY_RADIUS = 24.0f;
    constexpr u32 REFERENCE_COUNT = 10000;
    constexpr u32 FIXED_ENTITIES = 3;  
    constexpr const char* FORMATION_NAMES[FORMATION_COUNT] = {"cube", "sphere", "galaxy"};
    constexpr const char* PREDATOR_MODES[] = {"off", "wander", "at", "cursor"};
    constexpr const char* STATE_NAMES[SWARM_STATE_COUNT] = {"seeking", "settled", "fleeing", "regrouping"};

     
    constexpr float NEIGHBOUR_RADIUS = 1.2f;  
    constexpr float SEPARATION_RADIUS = 0.45f;
    constexpr u32 MAX_NEIGHBOURS = 12;
    constexpr float SEPARATION_WEIGHT = 40.0f;
    constexpr float ALIGNMENT_WEIGHT = 1.5f;
    constexpr float COHESION_WEIGHT = 1.0f;
    constexpr float LEADER_ALIGNMENT = 4.0f;
    constexpr float MAX_SPEED = 16.0f;
    constexpr float ARRIVE_GAIN = 3.0f;  
    constexpr float RESPONSE = 6.0f;  
    constexpr float SETTLE_DISTANCE = 0.3f;
    constexpr float UNSETTLE_DISTANCE = 1.0f;
    constexpr float FLEE_RADIUS = 5.0f;
    constexpr float FLEE_ACCELERATION = 90.0f;
    constexpr float PREDATOR_SIZE = 1.5f;
    constexpr float WANDER_RADIUS = 16.0f;
     
     
    constexpr float FLY_SPEED = 0.5f * 72.0f;
    constexpr float LOOK_PER_PIXEL = 0.004f;
    constexpr float BOOST_RATE = 4.5f;
    constexpr float BOOST_MAX = 20.0f;
    constexpr float MAX_FLY_SECONDS = 0.1f;
    constexpr float DRAIN_PER_SECOND = 0.5f;
    constexpr float RECOVER_PER_SECOND = 0.2f;
    constexpr u32 REGROUP_STEPS = 45;
    constexpr float SLOT_CELL = 1.0f;
    constexpr int SLOT_SEARCH_RINGS = 2;
    constexpr float CLAIM_REACH = 3.0f;  
    constexpr float LEADER_SHARE = 1.0f / 32.0f;

    u64 nextRandom(u64& state)
    {
        u64 z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    float unit(u64& state) { return static_cast<float>(nextRandom(state) >> 40) / 16777216.0f; }

    struct Pose
    {
        float x, y, z, half;
    };

    struct P3
    {
        float x, y, z;
    };

     
    struct Frame
    {
        u32 count;
        float time;
        float cubeCy, cubeSy, cubeCx, cubeSx;
        float sphereCy, sphereSy, sphereCx, sphereSx;
        float galaxyCx, galaxySx;
        u32 latticeWidth, latticeLayers;
        float scale;
    };

    float formationScale(const u32 count)
    {
        return std::cbrt(static_cast<float>(count) / static_cast<float>(REFERENCE_COUNT));
    }

    Frame makeFrame(const u32 count, const float time)
    {
        Frame f{};
        f.count = count;
        f.time = time;
        const float cubeY = time * 0.45f + 0.6f, cubeX = -0.5f + 0.12f * std::sin(time * 0.4f);
        const float sphereY = time * 0.4f, sphereX = 0.35f, galaxyX = 0.5f;
        f.cubeCy = std::cos(cubeY), f.cubeSy = std::sin(cubeY), f.cubeCx = std::cos(cubeX), f.cubeSx = std::sin(cubeX);
        f.sphereCy = std::cos(sphereY), f.sphereSy = std::sin(sphereY), f.sphereCx = std::cos(sphereX);
        f.sphereSx = std::sin(sphereX);
        f.galaxyCx = std::cos(galaxyX), f.galaxySx = std::sin(galaxyX);
        f.latticeWidth = latticeWidthOf(count);
        f.latticeLayers = (count + f.latticeWidth * f.latticeWidth - 1) / (f.latticeWidth * f.latticeWidth);
        f.scale = formationScale(count);
        return f;
    }

     
    Pose rotate(const float x, const float y, const float z, const float cy, const float sy, const float cx,
                const float sx, const float half)
    {
        const float x1 = x * cy + z * sy;
        const float z1 = -x * sy + z * cy;
        return {x1, y * cx - z1 * sx, y * sx + z1 * cx, half};
    }

     
    Pose formationPose(const u32 formation, const Frame& f, const FormationCubeComponent& cube)
    {
        const u32 i = cube.index;
        if (formation == FORMATION_CUBE)
        {
            const LatticePoint p = latticePosition(i, f.latticeWidth, f.latticeLayers);
            return rotate(p.x, p.y, p.z, f.cubeCy, f.cubeSy, f.cubeCx, f.cubeSx, 0.32f);
        }
        if (formation == FORMATION_SPHERE)
        {
            const float radius = SPHERE_RADIUS * f.scale;
            return rotate(cube.sphereX * radius, cube.sphereY * radius, cube.sphereZ * radius, f.sphereCy, f.sphereSy,
                          f.sphereCx, f.sphereSx, 0.2f);
        }
        const float r = cube.radius, a = cube.angle + f.time * 0.35f / (r + 0.15f);
        const float galaxyRadius = GALAXY_RADIUS * f.scale;
        const float thickness = 0.4f + (1.0f - r) * 2.6f;
        return rotate(std::cos(a) * r * galaxyRadius, cube.lift * thickness, std::sin(a) * r * galaxyRadius, 1.0f, 0.0f,
                      f.galaxyCx, f.galaxySx, 0.12f + 0.1f * (1.0f - r));
    }

    P3 predatorAt(const FormationsStateComponent& state)
    {
        if (state.predatorMode != PREDATOR_WANDER)
        {
            return {state.predatorX, state.predatorY, state.predatorZ};
        }
        const float t = static_cast<float>(state.step) * STEP_SECONDS, r = WANDER_RADIUS * formationScale(state.count);
        return {r * std::sin(t * 0.29f), 0.45f * r * std::sin(t * 0.41f + 1.0f), r * std::cos(t * 0.23f)};
    }

     
    Vec4 agentTint(const FormationCubeComponent& cube, const SwarmAgentComponent& agent)
    {
        constexpr P3 COLORS[SWARM_STATE_COUNT] = {
            {0.25f, 0.55f, 1.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.15f, 0.1f}, {1.0f, 0.65f, 0.15f}};
        const float grey = cube.role == ROLE_LEADER ? std::min(1.0f, cube.shade + 0.35f) : cube.shade;
        const float mix = agent.state == SWARM_SETTLED ? 0.0f : 0.7f;
        const float light = 0.5f + 0.5f * std::floor(std::clamp(agent.energy, 0.0f, 1.0f) * 4.0f) / 4.0f;
        const P3 c = COLORS[std::min(agent.state, SWARM_STATE_COUNT - 1)];
        return {(grey + (c.x - grey) * mix) * light, (grey + (c.y - grey) * mix) * light,
                (grey + (c.z - grey) * mix) * light, 1.0f};
    }

     

    u32 cellHash(const int x, const int y, const int z, const u32 mask)
    {
        return (static_cast<u32>(x) * 73856093u ^ static_cast<u32>(y) * 19349663u ^ static_cast<u32>(z) * 83492791u) &
            mask;
    }

    int cellOf(const float v, const float inverseCell) { return static_cast<int>(std::floor(v * inverseCell)); }

     
     
    struct SpatialHash
    {
        std::vector<u32> start;  
        std::vector<u32> entries;
        std::vector<u32> bucketOf;  
        u32 mask = 0;
        float inverseCell = 1.0f;

        void reset(const u32 points, const float cell)
        {
            u32 buckets = 1024;
            while (buckets < points)
            {
                buckets <<= 1;
            }
            mask = buckets - 1;
            inverseCell = 1.0f / cell;
            start.assign(buckets + 1, 0);
            entries.resize(points);
            bucketOf.assign(points, NO_SLOT);
        }

        void place(const u32 point, const P3& p)
        {
            bucketOf[point] =
                cellHash(cellOf(p.x, inverseCell), cellOf(p.y, inverseCell), cellOf(p.z, inverseCell), mask);
        }

        void build()
        {
            std::fill(start.begin(), start.end(), 0u);
            for (const u32 bucket : bucketOf)
            {
                if (bucket != NO_SLOT)
                {
                    ++start[bucket + 1];
                }
            }
            for (sizet b = 1; b < start.size(); ++b)
            {
                start[b] += start[b - 1];
            }
            std::vector<u32>& cursor = fill;
            cursor.assign(start.begin(), start.end() - 1);
            for (u32 point = 0; point < bucketOf.size(); ++point)
            {
                if (bucketOf[point] != NO_SLOT)
                {
                    entries[cursor[bucketOf[point]]++] = point;
                }
            }
        }

        std::vector<u32> fill;
    };

     
    struct Swarm
    {
        std::mutex lock;
        u32 count = 0;
        std::vector<P3> position, velocity, target, targetVelocity;
        std::vector<float> targetHalf;
        std::vector<u8> present, owned, leader;
        std::vector<u32> proposal, slot;  
        std::vector<u64> bids;
        SpatialHash cubes, freeSlots;
         
        struct Neighbour
        {
            P3 position, velocity;
            u32 index;
            float alignWeight;
        };
        std::vector<Neighbour> sorted;

        void resize(const u32 n)
        {
            count = n;
            position.resize(n), velocity.resize(n), target.resize(n), targetVelocity.resize(n), targetHalf.resize(n);
            present.assign(n, 0), owned.assign(n, 0), leader.resize(n), proposal.resize(n), slot.resize(n);
            bids.resize(n), sorted.resize(n);
        }
    };

    Swarm& swarm()
    {
        static Swarm instance;
        return instance;
    }

     
    u32 nearestFreeSlot(const Swarm& s, const P3& p, float& bestDistance)
    {
        const SpatialHash& h = s.freeSlots;
        const int cx = cellOf(p.x, h.inverseCell), cy = cellOf(p.y, h.inverseCell), cz = cellOf(p.z, h.inverseCell);
        const float cell = 1.0f / h.inverseCell;
        u32 best = NO_SLOT;
        bestDistance = 3.4e38f;
        for (int ring = 0; ring <= SLOT_SEARCH_RINGS; ++ring)
        {
            for (int dz = -ring; dz <= ring; ++dz)
            {
                for (int dy = -ring; dy <= ring; ++dy)
                {
                    for (int dx = -ring; dx <= ring; ++dx)
                    {
                        if (std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) != ring)
                        {
                            continue;
                        }
                        const u32 bucket = cellHash(cx + dx, cy + dy, cz + dz, h.mask);
                        for (u32 e = h.start[bucket]; e < h.start[bucket + 1]; ++e)
                        {
                            const u32 slot = h.entries[e];
                            const P3& t = s.target[slot];
                            const float ex = t.x - p.x, ey = t.y - p.y, ez = t.z - p.z;
                            const float d = ex * ex + ey * ey + ez * ez;
                            if (d < bestDistance || (d == bestDistance && slot < best))
                            {
                                bestDistance = d, best = slot;
                            }
                        }
                    }
                }
            }
             
            const float reach = static_cast<float>(ring) * cell;
            if (best != NO_SLOT && bestDistance <= reach * reach)
            {
                break;
            }
        }
        return best;
    }

     
     
    void claimSlots(SystemContext& context, Swarm& s, const FormationsStateComponent& state, const float scale)
    {
        const float reach = CLAIM_REACH * scale;
        s.freeSlots.reset(s.count, SLOT_CELL);
        for (u32 slot = 0; slot < s.count; ++slot)
        {
            if (s.present[slot] && !s.owned[slot])
            {
                s.freeSlots.place(slot, s.target[slot]);
            }
        }
        s.freeSlots.build();
        std::fill(s.bids.begin(), s.bids.end(), ~0ull);
        context.parallelFor(s.count, 256,
                            [&](const u32 begin, const u32 end)
                            {
                                for (u32 i = begin; i < end; ++i)
                                {
                                    s.proposal[i] = NO_SLOT;
                                    if (!s.present[i] || s.slot[i] != NO_SLOT)
                                    {
                                        continue;
                                    }
                                     
                                    const P3 p = s.position[i], hint = s.target[i];
                                    const float hx = hint.x - p.x, hy = hint.y - p.y, hz = hint.z - p.z;
                                    if (hx * hx + hy * hy + hz * hz > reach * reach)
                                    {
                                        continue;
                                    }
                                    float distance = 0.0f;
                                    const u32 slot = nearestFreeSlot(s, s.position[i], distance);
                                    if (slot == NO_SLOT)
                                    {
                                        continue;
                                    }
                                    s.proposal[i] = slot;
                                    u32 bits;
                                    std::memcpy(&bits, &distance, sizeof(bits));
                                    const u64 bid = static_cast<u64>(bits) << 32 | i;
                                    std::atomic_ref<u64> cell(s.bids[slot]);
                                    u64 current = cell.load(std::memory_order_relaxed);
                                    while (bid < current &&
                                           !cell.compare_exchange_weak(current, bid, std::memory_order_relaxed))
                                    {
                                    }
                                }
                            });
        for (u32 i = 0; i < s.count; ++i)
        {
            const u32 slot = s.proposal[i];
            if (slot != NO_SLOT && static_cast<u32>(s.bids[slot]) == i)
            {
                s.slot[i] = slot;
                s.owned[slot] = 1;
            }
        }
        if (state.phaseStep < CLAIM_STEPS)
        {
            return;
        }
        u32 slot = 0;
        for (u32 i = 0; i < s.count; ++i)
        {
            if (!s.present[i] || s.slot[i] != NO_SLOT)
            {
                continue;
            }
            while (slot < s.count && (!s.present[slot] || s.owned[slot]))
            {
                ++slot;
            }
            if (slot == s.count)
            {
                return;
            }
            s.slot[i] = slot;
            s.owned[slot] = 1;
        }
    }

    struct Steering
    {
        P3 predator;
        bool hunting;
        float scale;
    };

    void steer(const Swarm& s, const Steering& k, const FormationCubeComponent& cube, const u32 formation,
               TransformComponent& transform, SwarmAgentComponent& agent)
    {
        const u32 i = cube.index;
        const P3 p = s.position[i], v = s.velocity[i];
        const u32 slot = s.slot[i];
        agent.slot = slot;
        agent.slotFormation = formation;

         
         
        float sx = 0, sy = 0, sz = 0, ax = 0, ay = 0, az = 0, cx = 0, cy = 0, cz = 0, alignWeight = 0;
        u32 neighbours = 0;
        const SpatialHash& h = s.cubes;
        const int hx = cellOf(p.x, h.inverseCell), hy = cellOf(p.y, h.inverseCell), hz = cellOf(p.z, h.inverseCell);
        const int search = agent.state == SWARM_SETTLED ? -1 : 1;  
        for (int dz = -search; dz <= search && neighbours < MAX_NEIGHBOURS; ++dz)
        {
            for (int dy = -search; dy <= search && neighbours < MAX_NEIGHBOURS; ++dy)
            {
                for (int dx = -search; dx <= search && neighbours < MAX_NEIGHBOURS; ++dx)
                {
                    const u32 bucket = cellHash(hx + dx, hy + dy, hz + dz, h.mask);
                    for (u32 e = h.start[bucket]; e < h.start[bucket + 1] && neighbours < MAX_NEIGHBOURS; ++e)
                    {
                        const Swarm::Neighbour& n = s.sorted[e];
                        const P3 q = n.position;
                        const float ex = p.x - q.x, ey = p.y - q.y, ez = p.z - q.z;
                        const float d2 = ex * ex + ey * ey + ez * ez;
                        if (n.index == i || d2 >= NEIGHBOUR_RADIUS * NEIGHBOUR_RADIUS || d2 < 1e-12f)
                        {
                            continue;
                        }
                        ++neighbours;
                        if (d2 < SEPARATION_RADIUS * SEPARATION_RADIUS)
                        {
                            const float d = std::sqrt(d2), push = (SEPARATION_RADIUS - d) / (SEPARATION_RADIUS * d);
                            sx += ex * push, sy += ey * push, sz += ez * push;
                        }
                        const float w = n.alignWeight;
                        const P3 u = n.velocity;
                        ax += u.x * w, ay += u.y * w, az += u.z * w, alignWeight += w;
                        cx += q.x, cy += q.y, cz += q.z;
                    }
                }
            }
        }

         
        const u32 goal = slot != NO_SLOT ? slot : i;
        const P3 t = s.target[goal], tv = s.targetVelocity[goal];
        const float gx = t.x - p.x, gy = t.y - p.y, gz = t.z - p.z;
        const float goalDistance = std::sqrt(gx * gx + gy * gy + gz * gz);

        float px = 0, py = 0, pz = 0, predatorDistance = 3.4e38f;
        if (k.hunting)
        {
            px = p.x - k.predator.x, py = p.y - k.predator.y, pz = p.z - k.predator.z;
            predatorDistance = std::sqrt(px * px + py * py + pz * pz);
        }
        const float fleeRadius = FLEE_RADIUS * k.scale;

         
        const u32 before = agent.state;
        if (predatorDistance < fleeRadius)
        {
            agent.state = SWARM_FLEEING;
        }
        else if (agent.state == SWARM_FLEEING)
        {
            agent.state = SWARM_REGROUPING;
        }
        else if (agent.state == SWARM_REGROUPING)
        {
            if (agent.stateSteps >= REGROUP_STEPS && agent.energy > 0.5f)
            {
                agent.state = SWARM_SEEKING;
            }
        }
        else if (slot == NO_SLOT || goalDistance > UNSETTLE_DISTANCE)
        {
            agent.state = SWARM_SEEKING;
        }
        else if (goalDistance < SETTLE_DISTANCE)
        {
            agent.state = SWARM_SETTLED;
        }
        agent.stateSteps = agent.state == before ? std::min(agent.stateSteps + 1, 0xFFFFu) : 0;
        const bool fleeing = agent.state == SWARM_FLEEING;
        agent.energy =
            std::clamp(agent.energy + (fleeing ? -DRAIN_PER_SECOND : RECOVER_PER_SECOND) * STEP_SECONDS, 0.0f, 1.0f);

        const float maxSpeed = MAX_SPEED * k.scale * (0.55f + 0.45f * agent.energy) * (fleeing ? 1.6f : 1.0f);
        const float pull = fleeing ? 0.2f : agent.state == SWARM_REGROUPING ? 0.5f : slot == NO_SLOT ? 0.6f : 1.0f;
        float dvx = tv.x + gx * ARRIVE_GAIN, dvy = tv.y + gy * ARRIVE_GAIN, dvz = tv.z + gz * ARRIVE_GAIN;
        const float desired = std::sqrt(dvx * dvx + dvy * dvy + dvz * dvz);
        if (desired > maxSpeed)
        {
            const float f = maxSpeed / desired;
            dvx *= f, dvy *= f, dvz *= f;
        }
        float accX = (dvx - v.x) * RESPONSE * pull, accY = (dvy - v.y) * RESPONSE * pull,
              accZ = (dvz - v.z) * RESPONSE * pull;
        const float flock = agent.state == SWARM_SETTLED ? 0.15f : 1.0f;
        accX += sx * SEPARATION_WEIGHT * flock, accY += sy * SEPARATION_WEIGHT * flock,
            accZ += sz * SEPARATION_WEIGHT * flock;
        if (neighbours > 0)
        {
            const float inv = 1.0f / static_cast<float>(neighbours),
                        alignScale = ALIGNMENT_WEIGHT * flock / alignWeight;
            accX += (ax - v.x * alignWeight) * alignScale + (cx * inv - p.x) * COHESION_WEIGHT * flock;
            accY += (ay - v.y * alignWeight) * alignScale + (cy * inv - p.y) * COHESION_WEIGHT * flock;
            accZ += (az - v.z * alignWeight) * alignScale + (cz * inv - p.z) * COHESION_WEIGHT * flock;
        }
        if (fleeing && predatorDistance > 1e-6f)
        {
            const float f = FLEE_ACCELERATION * k.scale * (1.0f - predatorDistance / fleeRadius) / predatorDistance;
            accX += px * f, accY += py * f, accZ += pz * f;
        }

        float nvx = v.x + accX * STEP_SECONDS, nvy = v.y + accY * STEP_SECONDS, nvz = v.z + accZ * STEP_SECONDS;
        const float speed = std::sqrt(nvx * nvx + nvy * nvy + nvz * nvz);
        if (speed > maxSpeed)
        {
            const float f = maxSpeed / speed;
            nvx *= f, nvy *= f, nvz *= f;
        }
        agent.velocity = Vec3(nvx, nvy, nvz);
        transform.position = Vec3(p.x + nvx * STEP_SECONDS, p.y + nvy * STEP_SECONDS, p.z + nvz * STEP_SECONDS);
        const float half = transform.scale.x + (s.targetHalf[goal] - transform.scale.x) * 0.1f;
        transform.scale = Vec3(half);
    }

    void moveCubes(SystemContext& context)
    {
        FormationsStateComponent* state = nullptr;
        TransformComponent* predatorTransform = nullptr;
        context.query<ecs::Write<FormationsStateComponent>, ecs::Write<TransformComponent>>()->forEachChunk(
            [&](Chunk* chunk)
            {
                if (!chunk->entities().empty())
                {
                    state = chunk->writeColumn<FormationsStateComponent>();
                    predatorTransform = chunk->writeColumn<TransformComponent>();
                }
            });
        if (!state)
        {
            return;
        }
        ++state->step;
        if (++state->phaseStep >= TRANSITION_STEPS + HOLD_STEPS)
        {
            state->previous = state->formation;
            state->formation = (state->formation + 1) % FORMATION_COUNT;
            state->phaseStep = 0;
        }
        const P3 predator = predatorAt(*state);
        state->predatorX = predator.x, state->predatorY = predator.y, state->predatorZ = predator.z;
        const float scale = formationScale(state->count);
        predatorTransform->position = Vec3(predator.x, predator.y, predator.z);
        predatorTransform->scale = Vec3(state->predatorMode == PREDATOR_OFF ? 0.001f : PREDATOR_SIZE * scale);

        Swarm& s = swarm();
        const std::scoped_lock hold(s.lock);
        s.resize(state->count);
        const u32 formation = state->formation;
        const Frame now = makeFrame(state->count, static_cast<float>(state->step) * STEP_SECONDS);
        const Frame next = makeFrame(state->count, static_cast<float>(state->step + 1) * STEP_SECONDS);
        const auto cubes = context.query<ecs::Read<FormationCubeComponent>, ecs::Write<TransformComponent>,
                                         ecs::Write<SwarmAgentComponent>, ecs::Write<RenderTintComponent>>();
        s.cubes.reset(s.count, NEIGHBOUR_RADIUS);
        std::atomic<u32> unclaimed = 0;

         
        cubes->forEachChunkParallel(
            [&](Chunk* chunk, u32)
            {
                const FormationCubeComponent* cube = chunk->readColumn<FormationCubeComponent>();
                const TransformComponent* transforms = chunk->readColumn<TransformComponent>();
                const SwarmAgentComponent* agents = chunk->readColumn<SwarmAgentComponent>();
                u32 free = 0;
                for (sizet row = 0; row < chunk->entities().size(); ++row)
                {
                    const u32 i = cube[row].index;
                    if (i >= s.count)
                    {
                        continue;
                    }
                    const Vec3& p = transforms[row].position;
                    const Vec3& v = agents[row].velocity;
                    s.position[i] = {p.x, p.y, p.z};
                    s.velocity[i] = {v.x, v.y, v.z};
                    s.present[i] = 1;
                    s.leader[i] = cube[row].role == ROLE_LEADER;
                    s.cubes.place(i, s.position[i]);
                    const Pose t = formationPose(formation, now, cube[row]);
                    const Pose u = formationPose(formation, next, cube[row]);
                    s.target[i] = {t.x, t.y, t.z};
                    s.targetVelocity[i] = {(u.x - t.x) / STEP_SECONDS, (u.y - t.y) / STEP_SECONDS,
                                           (u.z - t.z) / STEP_SECONDS};
                    s.targetHalf[i] = t.half;
                    const SwarmAgentComponent& agent = agents[row];
                    const bool holds = agent.slotFormation == formation && agent.slot < s.count;
                    s.slot[i] = holds ? agent.slot : NO_SLOT;
                    if (holds)
                    {
                        s.owned[agent.slot] = 1;  
                    }
                    else
                    {
                        ++free;
                    }
                }
                unclaimed.fetch_add(free, std::memory_order_relaxed);
            });
        s.cubes.build();
        const u32 hashed = s.cubes.start.back();
        context.parallelFor(hashed, 1024,
                            [&](const u32 begin, const u32 end)
                            {
                                for (u32 e = begin; e < end; ++e)
                                {
                                    const u32 j = s.cubes.entries[e];
                                    s.sorted[e] = {s.position[j], s.velocity[j], j,
                                                   s.leader[j] ? LEADER_ALIGNMENT : 1.0f};
                                }
                            });
        if (unclaimed.load() > 0)
        {
            claimSlots(context, s, *state, scale);
        }

        const Steering k{.predator = predator, .hunting = state->predatorMode != PREDATOR_OFF, .scale = scale};
        cubes->forEachChunkParallel(
            [&](Chunk* chunk, u32)
            {
                const FormationCubeComponent* cube = chunk->readColumn<FormationCubeComponent>();
                TransformComponent* transforms = chunk->writeColumn<TransformComponent>();
                SwarmAgentComponent* agents = chunk->writeColumn<SwarmAgentComponent>();
                RenderTintComponent* tints = chunk->mutableColumn<RenderTintComponent>();
                for (sizet row = 0; row < chunk->entities().size(); ++row)
                {
                    if (cube[row].index >= s.count)
                    {
                        continue;
                    }
                    steer(s, k, cube[row], formation, transforms[row], agents[row]);
                    const Vec4 tint = agentTint(cube[row], agents[row]);
                    if (std::memcmp(&tints[row].color, &tint, sizeof(Vec4)) != 0)
                    {
                        tints[row].color = tint;
                        chunk->markModified<RenderTintComponent>(row);
                    }
                }
            });
    }

     

    const FormationsStateComponent* findState(const World world, Entity* entity = nullptr)
    {
        for (const Chunk* chunk : world.getChunksContaining(Aspect(getComponentId<FormationsStateComponent>())))
        {
            if (!chunk->entities().empty())
            {
                if (entity)
                {
                    *entity = chunk->entities()[0];
                }
                return &chunk->readColumn<FormationsStateComponent>()[0];
            }
        }
        return nullptr;
    }

    std::vector<Entity> layoutEntities(const World world)
    {
        std::vector<Entity> entities;
        for (const Chunk* chunk : world.getChunksContaining(Aspect(getComponentId<FormationsSlotComponent>())))
        {
            entities.insert(entities.end(), chunk->entities().begin(), chunk->entities().end());
        }
        return entities;
    }

     
    SwarmAgentComponent settledAgent(const FormationsStateComponent& state, const FormationCubeComponent& cube)
    {
        return SwarmAgentComponent{.slot = cube.index, .slotFormation = state.formation, .state = SWARM_SETTLED};
    }

     
    void settleCubes(const World world)
    {
        const FormationsStateComponent* state = findState(world);
        if (!state)
        {
            return;
        }
        const Frame frame = makeFrame(state->count, static_cast<float>(state->step) * STEP_SECONDS);
        for (const Chunk* chunk : world.getChunksContaining(Aspect(getComponentId<FormationCubeComponent>())))
        {
            const FormationCubeComponent* cubes = chunk->readColumn<FormationCubeComponent>();
            for (sizet row = 0; row < chunk->entities().size(); ++row)
            {
                const Entity entity = chunk->entities()[row];
                const Pose pose = formationPose(state->formation, frame, cubes[row]);
                TransformComponent& transform = world.getComponent<TransformComponent>(entity);
                transform.position = Vec3(pose.x, pose.y, pose.z);
                transform.scale = Vec3(pose.half);
                SwarmAgentComponent& agent = world.getComponent<SwarmAgentComponent>(entity);
                agent = settledAgent(*state, cubes[row]);
                world.getComponent<RenderTintComponent>(entity).color = agentTint(cubes[row], agent);
            }
        }
    }

    enum FlyAction : u32
    {
        FLY_RIGHT,
        FLY_LEFT,
        FLY_FORWARD,
        FLY_BACK,
        FLY_UP,
        FLY_DOWN,
        FLY_BOOST,
        FLY_LOOK,
        FLY_ACTION_COUNT
    };
    constexpr const char* FLY_ACTION_NAMES[] = {"FormationsRight", "FormationsLeft", "FormationsForward",
                                                "FormationsBack",  "FormationsUp",   "FormationsDown",
                                                "FormationsBoost", "FormationsLook"};
    static_assert(std::size(FLY_ACTION_NAMES) == FLY_ACTION_COUNT);

     
     
    struct FormationsSession
    {
        explicit FormationsSession(InputSystem* inputSystem) : input(inputSystem)
        {
            if (!input)
            {
                return;
            }
            context = input->createContext({.name = InternedString("Formations")});
            for (u32 i = 0; i < FLY_ACTION_COUNT; ++i)
            {
                actions[i] = InternedString(FLY_ACTION_NAMES[i]);
            }
            const auto bindKey = [this](const FlyAction action, const KeyCode key)
            {
                input->addBinding(context,
                                  {.action = actions[action],
                                   .control = {.device = InputControlDevice::Keyboard, .code = static_cast<u16>(key)},
                                   .trigger = InputActionType::Axis});
            };
            bindKey(FLY_RIGHT, KeyCode::D);
            bindKey(FLY_LEFT, KeyCode::A);
            bindKey(FLY_FORWARD, KeyCode::W);
            bindKey(FLY_BACK, KeyCode::S);
            bindKey(FLY_UP, KeyCode::E);
            bindKey(FLY_DOWN, KeyCode::Q);
            bindKey(FLY_BOOST, KeyCode::LSHIFT);
            bindKey(FLY_BOOST, KeyCode::RSHIFT);
            input->addBinding(
                context,
                {.action = actions[FLY_LOOK],
                 .control = {.device = InputControlDevice::MouseButton, .code = static_cast<u16>(MouseButton::Right)},
                 .trigger = InputActionType::Axis});
        }

        ~FormationsSession()
        {
            if (input)
            {
                input->destroyContext(context);
            }
        }

        FormationsSession(const FormationsSession&) = delete;
        FormationsSession& operator=(const FormationsSession&) = delete;

        bool down(const FlyAction action) const { return input->isActionDown(actions[action]); }

        InputSystem* input = nullptr;  
        InputContextId context = INVALID_INPUT_CONTEXT_ID;
        InternedString actions[FLY_ACTION_COUNT];
        FlyLook look;
        bool oriented = false;
        FlyBoost boost;
    };

     
     
     
     
    void flyAndAim(SystemContext& context)
    {
        ServiceLocator& services = context.services();
        if (!services.contains<FormationsSession>())
        {
            return;
        }
        WriteService<FormationsSession> session = services.write<FormationsSession>();
        if (!session->input)
        {
            return;
        }
        Chunk* stateChunk = nullptr;
        context.query<ecs::Read<FormationsStateComponent>>()->forEachChunk(
            [&](Chunk* chunk)
            {
                if (!chunk->entities().empty())
                {
                    stateChunk = chunk;
                }
            });
        if (!stateChunk)
        {
            return;
        }
        const FormationsStateComponent& state = *stateChunk->readColumn<FormationsStateComponent>();
        const InputSystem& input = *session->input;
        const float seconds = std::min(context.deltaTime, MAX_FLY_SECONDS);
        const auto axis = [&](const FlyAction positive, const FlyAction negative)
        { return (session->down(positive) ? 1.0f : 0.0f) - (session->down(negative) ? 1.0f : 0.0f); };
        const Vec3 direction(axis(FLY_RIGHT, FLY_LEFT), axis(FLY_UP, FLY_DOWN), axis(FLY_FORWARD, FLY_BACK));
        const MouseState mouse = input.getMouseState();
        const bool looking = session->down(FLY_LOOK) && (mouse.deltaX != 0.0f || mouse.deltaY != 0.0f);
        const float boost = session->boost.advance(session->down(FLY_BOOST), seconds, BOOST_RATE, BOOST_MAX);
        const bool flying = direction != Vec3(0.0f, 0.0f, 0.0f);
        const bool aiming = state.predatorMode == PREDATOR_CURSOR;
        if (!flying && !looking && !aiming)
        {
            return;
        }
        context.query<ecs::Read<CameraComponent>, ecs::Read<TransformComponent>>()->forEachChunk(
            [&](Chunk* chunk)
            {
                if (chunk->entities().empty())
                {
                    return;
                }
                TransformComponent transform = chunk->readColumn<TransformComponent>()[0];
                if (!session->oriented)
                {
                    session->look = flyLookOf(transform.rotation);
                    session->oriented = true;
                }
                if (looking)
                {
                    session->look.yaw -= mouse.deltaX * LOOK_PER_PIXEL;
                    session->look.pitch =
                        std::clamp(session->look.pitch - mouse.deltaY * LOOK_PER_PIXEL, -FLY_MAX_PITCH, FLY_MAX_PITCH);
                    transform.rotation = flyRotation(session->look);
                }
                if (flying)
                {
                    transform.position = transform.position +
                        flyMove(transform.rotation, direction,
                                FLY_SPEED * formationScale(state.count) * seconds * boost);
                }
                if (looking || flying)
                {
                    chunk->mutableColumn<TransformComponent>()[0] = transform;
                    chunk->markModified<TransformComponent>(0);
                }
                if (!aiming)
                {
                    return;
                }
                if (const auto cursor = gameViewPointer(services, input); cursor && cursor->inside)
                {
                    if (const std::optional<Vec3> hit = cursorPlanePoint(
                            transform.position, transform.rotation, chunk->readColumn<CameraComponent>()[0].fov,
                            cursor->aspect, cursor->ndc, Vec3(0.0f, 0.0f, 0.0f)))
                    {
                        FormationsStateComponent& written = stateChunk->mutableColumn<FormationsStateComponent>()[0];
                        if (written.predatorX != hit->x || written.predatorY != hit->y || written.predatorZ != hit->z)
                        {
                            written.predatorX = hit->x, written.predatorY = hit->y, written.predatorZ = hit->z;
                            stateChunk->markModified<FormationsStateComponent>(0);
                        }
                    }
                }
            });
    }
    void placePredator(const World world, const Entity stateEntity)
    {
        const FormationsStateComponent& state = world.getComponent<FormationsStateComponent>(stateEntity);
        const P3 at = predatorAt(state);
        TransformComponent& transform = world.getComponent<TransformComponent>(stateEntity);
        transform.position = Vec3(at.x, at.y, at.z);
        transform.scale =
            Vec3(state.predatorMode == PREDATOR_OFF ? 0.001f : PREDATOR_SIZE * formationScale(state.count));
    }

    std::vector<Entity> authorLayout(const World world, const u64 seed, const u32 count)
    {
        SPITE_GAME_ZONE("Formations.authorLayout");
        SPITE_GAME_COUNTER("formations.requestedCubes", count);
        std::vector<Entity> created;
        created.reserve(count + FIXED_ENTITIES);
        FormationsStateComponent initial{.seed = seed, .count = count};
        {
            SPITE_GAME_ZONE("Formations.createFixedEntities");
            const P3 predator = predatorAt(initial);
            initial.predatorX = predator.x, initial.predatorY = predator.y, initial.predatorZ = predator.z;
            const Entity stateEntity = world.createEntity();
            world.addComponent<FormationsStateComponent>(stateEntity, initial);
            world.addComponent<TransformComponent>(stateEntity);
            world.addComponent<TransformMatrixComponent>(stateEntity);
            world.addComponent<MeshComponent>(
                stateEntity, MeshComponent{.meshHandle = {InternedString(SPHERE_MESH), AssetType::MESH}});
            world.addComponent<MaterialComponent>(
                stateEntity, MaterialComponent{.materialHandle = {InternedString(CUBE_MATERIAL), AssetType::MATERIAL}});
            world.addComponent<RenderTintComponent>(stateEntity,
                                                    RenderTintComponent{.color = Vec4(0.9f, 0.05f, 0.05f, 1.0f)});
            placePredator(world, stateEntity);
            created.push_back(stateEntity);

            const Entity sun = world.createEntity();
            world.addComponent<TransformComponent>(
                sun,
                TransformComponent{.position = {0.0f, 10.0f, 0.0f},
                                   .rotation = multiply(fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.5f),
                                                        fromAxisAngle({1.0f, 0.0f, 0.0f}, -0.9f))});
            world.addComponent<TransformMatrixComponent>(sun);
            world.addComponent<DirectionalLightComponent>(sun, DirectionalLightComponent{.intensity = 2.5f});
            created.push_back(sun);

            const Entity camera = world.createEntity();
            world.addComponent<TransformComponent>(
                camera,
                TransformComponent{.position = {0.0f, 6.0f * formationScale(count), 72.0f * formationScale(count)},
                                   .rotation = fromAxisAngle({1.0f, 0.0f, 0.0f}, -0.08f)});
            world.addComponent<TransformMatrixComponent>(camera);
            world.addComponent<CameraComponent>(camera, CameraComponent{.fov = toRadians(50.0f)});
            created.push_back(camera);
        }

        const Frame frame = makeFrame(count, 0.0f);
        u64 rng = seed;
        {
            SPITE_GAME_ZONE("Formations.assignSlots");
            for (u32 slot = 0; slot < created.size(); ++slot)
            {
                world.addComponent<FormationsSlotComponent>(created[slot], FormationsSlotComponent{.index = slot});
            }
        }
        {
            SPITE_GAME_ZONE("Formations.createCubes");
            const std::vector<Entity> cubes = [&]
            {
                SPITE_GAME_ZONE("Formations.allocateCubeIds");
                return world.createHuskEntities(count);
            }();
            const ComponentID aspect[] = {
                getComponentId<TransformComponent>(),  getComponentId<TransformMatrixComponent>(),
                getComponentId<MeshComponent>(),       getComponentId<MaterialComponent>(),
                getComponentId<RenderTintComponent>(), getComponentId<FormationCubeComponent>(),
                getComponentId<SwarmAgentComponent>(), getComponentId<FormationsSlotComponent>()};
            const MeshComponent mesh{.meshHandle = {InternedString(BOX_MESH), AssetType::MESH}};
            const MaterialComponent material{.materialHandle = {InternedString(CUBE_MATERIAL), AssetType::MATERIAL}};
            SPITE_GAME_ZONE("Formations.registerCubes");
            const u32 batchSize = sceneLoading(world) ? 4096 : count;
            for (u32 base = 0; base < count; base += batchSize)
            {
                const u32 end = std::min(base + batchSize, count);
                world.registerEntities(
                    std::span(cubes).subspan(base, end - base), aspect,
                    [&]
                    {
                        SPITE_GAME_ZONE("Formations.fillCubes");
                        for (u32 i = base; i < end; ++i)
                        {
                            FormationCubeComponent cube{.index = i};
                            cube.shade = 0.12f + unit(rng) * 0.5f;
                            cube.role = unit(rng) < LEADER_SHARE ? ROLE_LEADER : ROLE_FOLLOWER;
                            cube.radius = 0.04f + std::pow(unit(rng), 0.85f);
                            const float arm = static_cast<float>(i % 3) * TAU / 3.0f;
                            cube.angle = arm + cube.radius * 4.2f + (unit(rng) + unit(rng) + unit(rng) - 1.5f) * 0.45f;
                            cube.lift = unit(rng) * 2.0f - 1.0f;
                            const float sy = 1.0f - (static_cast<float>(i) + 0.5f) / static_cast<float>(count) * 2.0f;
                            const float ring = std::sqrt(std::max(0.0f, 1.0f - sy * sy)),
                                        golden = static_cast<float>(i) * GOLDEN_ANGLE;
                            cube.sphereX = std::cos(golden) * ring;
                            cube.sphereY = sy;
                            cube.sphereZ = std::sin(golden) * ring;
                            const Pose pose = formationPose(initial.formation, frame, cube);
                            const SwarmAgentComponent agent = settledAgent(initial, cube);
                            const Entity entity = cubes[i];
                            world.getComponent<TransformComponent>(entity) =
                                TransformComponent{.position = {pose.x, pose.y, pose.z}, .scale = Vec3(pose.half)};
                            world.getComponent<MeshComponent>(entity) = mesh;
                            world.getComponent<MaterialComponent>(entity) = material;
                            world.getComponent<RenderTintComponent>(entity) =
                                RenderTintComponent{.color = agentTint(cube, agent)};
                            world.getComponent<FormationCubeComponent>(entity) = cube;
                            world.getComponent<SwarmAgentComponent>(entity) = agent;
                            world.getComponent<FormationsSlotComponent>(entity) =
                                FormationsSlotComponent{.index = FIXED_ENTITIES + i};
                        }
                    });
                sceneLoadCheckpoint(world, "showcase.loadGeometry", end, count);
            }
            created.insert(created.end(), cubes.begin(), cubes.end());
        }
        return created;
    }

    std::string layoutArgsOf(const FormationsStateComponent& state)
    {
        return std::format(R"({{"seed":{},"count":{}}})", state.seed, state.count);
    }

     
    std::vector<u64> reviveIds(const World world, const u32 count)
    {
        std::vector<u64> ids(count + FIXED_ENTITIES, 0);
        for (const Chunk* chunk : world.getChunksContaining(Aspect(getComponentId<FormationsSlotComponent>())))
        {
            const auto slots = chunk->readColumn<FormationsSlotComponent>();
            for (sizet row = 0; row < chunk->entities().size(); ++row)
            {
                if (slots[row].index < ids.size())
                {
                    ids[slots[row].index] = chunk->entities()[row].id();
                }
            }
        }
        return ids;
    }

    std::string clearCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = control.world(args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const std::vector<Entity> existing = layoutEntities(*world);
        const FormationsStateComponent* state = findState(*world);
        if (existing.empty() || !state)
        {
            return "the world has no formations layout: nothing to clear";
        }
        control.recordInverse(LAYOUT_COMMAND, layoutArgsOf(*state), reviveIds(*world, state->count));
        world->destroyEntities(existing);
        out.writeU32("cleared", static_cast<u32>(existing.size()));
        return {};
    }

     
    std::string layoutCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        SPITE_GAME_ZONE("Formations.layoutCommand");
        const World* world = control.world(args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const auto read = [&args](const char* name, const u64 fallback) -> std::optional<u64>
        { return args.json(name) ? args.integer(name) : std::optional<u64>(fallback); };
        const std::optional<u64> seed = read("seed", 1);
        const std::optional<u64> count = read("count", DEFAULT_COUNT);
        const std::optional<u64> replace = read("replace", 0);
        if (!seed)
        {
            return "seed must be an integer";
        }
        if (!count || *count < 1 || *count > MAX_COUNT)
        {
            return std::format("count must be 1..{}", MAX_COUNT);
        }
        if (!replace || *replace > 1)
        {
            return "replace must be 0 or 1";
        }
        const std::vector<Entity> existing = [&]
        {
            SPITE_GAME_ZONE("Formations.findExisting");
            return layoutEntities(*world);
        }();
        const FormationsStateComponent* old = findState(*world);
        if (!existing.empty() && *replace == 0)
        {
            return "the world has a formations layout already: run formationsClear or formationsLayout with replace=1";
        }
        std::string restore;
        std::vector<u64> revives;
        if (old)
        {
            restore = layoutArgsOf(*old);
            revives = reviveIds(*world, old->count);
        }
        {
            SPITE_GAME_ZONE("Formations.destroyExisting");
            world->destroyEntities(existing);
        }
        const std::vector<Entity> created = authorLayout(*world, *seed, static_cast<u32>(*count));
        {
            SPITE_GAME_ZONE("Formations.recordCreated");
            for (u32 key = 0; key < created.size(); ++key)
            {
                control.recordCreated(key, created[key]);
            }
        }
        control.recordInverse(CLEAR_COMMAND, "{}");
        if (!restore.empty())
        {
            control.recordInverse(LAYOUT_COMMAND, restore, revives);
        }
        out.writeU64("state", created.front().id());
        out.writeU64("seed", *seed);
        out.writeU32("count", static_cast<u32>(*count));
        out.writeU32("replaced", static_cast<u32>(existing.size()));
        out.writeU32("entities", static_cast<u32>(created.size()));
        control.auditLayout(*world, created, out);
        return {};
    }

     
    const World* showcaseWorld(ControlServer& control, const ControlArgs& args)
    {
        const World* world = args.string("world") ? control.world(args) : control.world("play");
        return world ? world : control.world(args);
    }

    std::string setCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = showcaseWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const std::optional<std::string> name = args.string("name");
        u32 formation = FORMATION_COUNT;
        for (u32 index = 0; name && index < FORMATION_COUNT; ++index)
        {
            if (*name == FORMATION_NAMES[index])
            {
                formation = index;
            }
        }
        if (formation == FORMATION_COUNT)
        {
            return "name must be cube, sphere or galaxy";
        }
        Entity entity;
        if (!findState(*world, &entity))
        {
            return "the world has no formations layout: run formationsLayout";
        }
        FormationsStateComponent& state = world->getComponent<FormationsStateComponent>(entity);
        const bool instant = args.json("instant") && args.integer("instant").value_or(0) == 1;
        state.previous = instant ? formation : state.formation;
        state.formation = formation;
        state.phaseStep = instant ? TRANSITION_STEPS : 0;
        if (instant)
        {
            settleCubes(*world);
        }
        out.writeString("formation", FORMATION_NAMES[formation]);
        return {};
    }

    std::string statusQuery(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = showcaseWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const FormationsStateComponent* state = findState(*world);
        if (!state)
        {
            return "the world has no formations layout: run formationsLayout";
        }
        out.writeU64("seed", state->seed);
        out.writeU32("count", state->count);
        out.writeString("formation", FORMATION_NAMES[state->formation]);
        out.writeString("previous", FORMATION_NAMES[state->previous]);
        out.writeString("phase", state->phaseStep < TRANSITION_STEPS ? "transition" : "hold");
        out.writeU32("phaseStep", state->phaseStep);
        out.writeU32("step", state->step);
        out.writeString("predator", PREDATOR_MODES[std::min(state->predatorMode, PREDATOR_CURSOR)]);
        out.writeF32("predatorX", state->predatorX);
        out.writeF32("predatorY", state->predatorY);
        out.writeF32("predatorZ", state->predatorZ);
        u32 states[SWARM_STATE_COUNT] = {};
        u32 leaders = 0, claimed = 0, duplicates = 0;
        std::vector<u8> taken(state->count, 0);
        for (const Chunk* chunk : world->getChunksContaining(Aspect(getComponentId<SwarmAgentComponent>())))
        {
            const SwarmAgentComponent* agents = chunk->readColumn<SwarmAgentComponent>();
            const FormationCubeComponent* cubes = chunk->readColumn<FormationCubeComponent>();
            for (sizet row = 0; row < chunk->entities().size(); ++row)
            {
                const SwarmAgentComponent& agent = agents[row];
                ++states[std::min(agent.state, SWARM_STATE_COUNT - 1)];
                leaders += cubes && cubes[row].role == ROLE_LEADER ? 1 : 0;
                if (agent.slotFormation == state->formation && agent.slot < state->count)
                {
                    ++claimed;
                    duplicates += taken[agent.slot];
                    taken[agent.slot] = 1;
                }
            }
        }
        for (u32 index = 0; index < SWARM_STATE_COUNT; ++index)
        {
            out.writeU32(STATE_NAMES[index], states[index]);
        }
        out.writeU32("leaders", leaders);
        out.writeU32("claimed", claimed);  
        out.writeU32("duplicateSlots", duplicates);  
        return {};
    }

    std::optional<float> number(const ControlArgs& args, const char* name)
    {
        const std::optional<std::string> text = args.json(name);
        float value = 0.0f;
        if (!text || std::from_chars(text->data(), text->data() + text->size(), value).ec != std::errc{})
        {
            return std::nullopt;
        }
        return value;
    }

     
    std::string predatorCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = showcaseWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const std::optional<std::string> mode = args.string("mode");
        u32 predatorMode = PREDATOR_CURSOR + 1;
        for (u32 index = 0; mode && index <= PREDATOR_CURSOR; ++index)
        {
            if (*mode == PREDATOR_MODES[index])
            {
                predatorMode = index;
            }
        }
        if (predatorMode > PREDATOR_CURSOR)
        {
            return "mode must be wander, off, at or cursor";
        }
        Entity entity;
        if (!findState(*world, &entity))
        {
            return "the world has no formations layout: run formationsLayout";
        }
        FormationsStateComponent& state = world->getComponent<FormationsStateComponent>(entity);
        if (predatorMode == PREDATOR_AT)
        {
            const std::optional<float> x = number(args, "x"), y = number(args, "y"), z = number(args, "z");
            if (!x || !y || !z)
            {
                return "mode=at needs numbers x, y and z";
            }
            state.predatorX = *x, state.predatorY = *y, state.predatorZ = *z;
        }
        state.predatorMode = predatorMode;
        placePredator(*world, entity);
        out.writeString("predator", PREDATOR_MODES[predatorMode]);
        out.writeF32("x", state.predatorX);
        out.writeF32("y", state.predatorY);
        out.writeF32("z", state.predatorZ);
        return {};
    }

    bool load(const ModuleContext& context)
    {
        registerComponent<FormationsStateComponent>(context.owner.id());
        registerComponent<FormationCubeComponent>(context.owner.id());
        registerComponent<FormationsSlotComponent>(context.owner.id());
        registerComponent<SwarmAgentComponent>(context.owner.id());

#if !defined(SPITE_SHIPPING) || defined(SPITE_SHOWCASE_BUNDLE)
        context.control.registerUndoableCommand(
            LAYOUT_COMMAND, context.owner.toView(),
            "authors the formations showcase: a seeded cloud of cubes, a sun and a camera; the reply carries audit "
            "(entities, unrooted, unnamed) and, when some are, warnings (hierarchyAudit lists them)",
            {optionalArg("seed", ArgKind::Integer, "the per-cube constants' generator seed", "1"),
             optionalArg("count", ArgKind::Integer, "cubes (1 and up; memory is the limit)", "100000"),
             optionalArg("replace", ArgKind::Integer, "1: delete the world's existing layout first (one undo step)",
                         "0"),
             WORLD_ARG},
            [&control = context.control](const ControlArgs& args, IFormatWriter& out)
            { return layoutCommand(control, args, out); });
        context.control.registerUndoableCommand(
            CLEAR_COMMAND, context.owner.toView(),
            "deletes every entity of the formations layout (undo authors it again)", {},
            [&control = context.control](const ControlArgs& args, IFormatWriter& out)
            { return clearCommand(control, args, out); });
        context.control.registerCommand(
            SET_COMMAND, context.owner.toView(),
            "morphs to a named formation now: cube, sphere or galaxy (instant=1: already there)",
            {requiredArg("name", ArgKind::String, "cube, sphere or galaxy"),
             optionalArg("instant", ArgKind::Integer, "1: no morph, the hold begins", "0"), WORLD_ARG},
            [&control = context.control](const ControlArgs& args, IFormatWriter& out)
            { return setCommand(control, args, out); });
        context.control.registerCommand(
            PREDATOR_COMMAND, context.owner.toView(),
            "the predator sphere the cubes flee: wander (a path through the formation), off, at x y z, or cursor "
            "(under the "
            "mouse cursor in the play camera's view, on the plane through the formation centre)",
            {requiredArg("mode", ArgKind::String, "wander, off, at or cursor"),
             optionalArg("x", ArgKind::Number, "mode=at: the point", "0"),
             optionalArg("y", ArgKind::Number, "mode=at: the point", "0"),
             optionalArg("z", ArgKind::Number, "mode=at: the point", "0"), WORLD_ARG},
            [&control = context.control](const ControlArgs& args, IFormatWriter& out)
            { return predatorCommand(control, args, out); });
        context.control.registerQuery(
            STATUS_QUERY, context.owner.toView(),
            "the showcase's current and previous formation, phase (transition or hold), steps in the phase, total "
            "steps, the predator, the cubes per swarm state (seeking, settled, fleeing, regrouping), leaders, and the "
            "cubes holding a slot of the current formation",
            {WORLD_ARG}, [&control = context.control](const ControlArgs& args, IFormatWriter& out)
            { return statusQuery(control, args, out); });
#endif
        return true;
    }

    void attachWorld(const ModuleContext& context, const World world)
    {
        ServiceLocator& services = world.services();
        services.registerService(std::make_shared<FormationsSession>(
                                     services.contains<InputSystem>() ? &services.get<InputSystem>() : nullptr),
                                 context.owner);
        world.registerSystem({.moduleName = context.owner.toView().data(),
                              .name = FLY_SYSTEM_NAME,
                              .fn = &flyAndAim,
                              .update = SystemPhaseDescriptor{.reads = {getComponentId<CameraComponent>()},
                                                              .writes = {getComponentId<TransformComponent>(),
                                                                         getComponentId<FormationsStateComponent>()}}});
        world.registerSystem(
            {.moduleName = context.owner.toView().data(),
             .name = MOVE_SYSTEM_NAME,
             .fn = &moveCubes,
             .fixedUpdate = SystemPhaseDescriptor{
                 .reads = {getComponentId<FormationCubeComponent>()},
                 .writes = {getComponentId<FormationsStateComponent>(), getComponentId<TransformComponent>(),
                            getComponentId<SwarmAgentComponent>(), getComponentId<RenderTintComponent>()}}});
    }
}  

#ifdef SPITE_SHIPPING
spite::Entity spiteAuthorFormations(const spite::World world, const spite::u32 count)
{
    return authorLayout(world, 1, count)[2];
}
#endif

#ifdef SPITE_SHIPPING
extern "C" const spite::ModuleDescriptor* spiteFormationsShowcase()
#else
SPITE_MODULE_EXPORT const spite::ModuleDescriptor* SPITE_MODULE_ENTRY()
#endif
{
    static const spite::ModuleDescriptor descriptor{.name = MODULE_NAME, .load = &load, .attachWorld = &attachWorld};
    return &descriptor;
}
