#pragma once

#include <algorithm>
#include <cstdlib>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "CityDesign.hpp"

 
 
 
 
 
 
 
 
 
 
namespace spite::city::model
{
    constexpr u32 NO_MOVE = 0xFFFFFFFFu;
    constexpr float WHEELBASE_HALF = 1.4f;  
    constexpr float MOVE_LEAD = 1.5f;  
    constexpr float TURN_ACCELERATION = 4.0f;  
    constexpr float MOVE_TURN_SPEED = 7.0f;  
    constexpr float MOVE_U_TURN_SPEED = 3.0f;  
     
     
    constexpr float SWEEP_HALF_LENGTH = 2.55f;
    constexpr float SWEEP_HALF_WIDTH = 1.02f;
    constexpr float SWEEP_CELL = 0.25f;  
    constexpr float SWEEP_STEP = 0.25f;  
     
     
    constexpr float SWEEP_MARGIN = 0.18f + 0.15f;

    constexpr u32 MOVE_STRAIGHT = 0;
    constexpr u32 MOVE_TURN = 1;
    constexpr u32 MOVE_U_TURN = 2;

     
    constexpr u32 moveCode(const u32 headingIn, const u32 subIn, const u32 headingOut, const u32 subOut)
    {
        return (headingIn & 3) | (subIn & 3) << 2 | (headingOut & 3) << 4 | (subOut & 3) << 6;
    }
    constexpr u32 moveHeadingIn(const u32 code) { return code & 3; }
    constexpr u32 moveSubIn(const u32 code) { return code >> 2 & 3; }
    constexpr u32 moveHeadingOut(const u32 code) { return code >> 4 & 3; }
    constexpr u32 moveSubOut(const u32 code) { return code >> 6 & 3; }

     
    inline float headingX(const u32 heading) { return static_cast<float>(HEADING_DX[heading & 3]); }
    inline float headingZ(const u32 heading) { return static_cast<float>(HEADING_DZ[heading & 3]); }
    inline float rightX(const u32 heading) { return -headingZ(heading); }
    inline float rightZ(const u32 heading) { return headingX(heading); }

     
     
     
     
    inline u32 straightSub(const GraphEdge& in, const GraphEdge& out, const u32 sub)
    {
        const float offset = in.offset(sub);
        u32 best = 0;
        for (u32 j = 1; j < out.lanes; ++j)
        {
            best = std::fabs(out.offset(j) - offset) < std::fabs(out.offset(best) - offset) ? j : best;
        }
        return best;
    }
    inline u32 rightSub(const u32 sub, const u32 lanesOut) { return std::min(sub, lanesOut - 1); }
    inline u32 leftSub(const u32 sub, const u32 lanesIn, const u32 lanesOut)
    {
        return lanesOut - 1 - std::min(lanesIn - 1 - std::min(sub, lanesIn - 1), lanesOut - 1);
    }

     
     
     
    inline bool turnsFrom(const u32 sub, const u32 lanes, const bool right)
    {
        const u32 fromEdge = right ? sub : lanes - 1 - sub;
        return fromEdge == 0 || (lanes >= 3 && fromEdge == 1);
    }

     
     
     
     
    inline u32 movementOf(const RoadGraph& graph, const u32 node, const u32 headingIn, const u32 sub,
                          const u32 headingOut, const u32 subOut = NO_MOVE)
    {
        const GraphNode& n = graph.nodes[node];
        const u32 inEdge = n.edges[(headingIn + 2) & 3];
        const u32 outEdge = n.edges[headingOut & 3];
        if (inEdge == NO_ID || outEdge == NO_ID)
        {
            return NO_MOVE;
        }
        const GraphEdge& in = graph.edges[inEdge];
        const GraphEdge& out = graph.edges[outEdge];
        if (sub >= in.lanes)
        {
            return NO_MOVE;
        }
        const bool forced = n.edges[headingIn & 3] == NO_ID;
        const u32 turn = (headingOut - headingIn) & 3;
        if (turn == 0)
        {
            return moveCode(headingIn, sub, headingOut, straightSub(in, out, sub));
        }
        if (turn == 1)
        {
            const u32 own = rightSub(sub, out.lanes);
            return forced || turnsFrom(sub, in.lanes, true)
                ? moveCode(headingIn, sub, headingOut, subOut >= own && subOut < out.lanes ? subOut : own)
                : NO_MOVE;
        }
        if (turn == 3)
        {
            const u32 own = leftSub(sub, in.lanes, out.lanes);
            return forced || turnsFrom(sub, in.lanes, false)
                ? moveCode(headingIn, sub, headingOut, subOut <= own ? subOut : own)
                : NO_MOVE;
        }
        const bool deadEnd = forced && n.edges[(headingIn + 1) & 3] == NO_ID && n.edges[(headingIn + 3) & 3] == NO_ID;
        return deadEnd ? moveCode(headingIn, sub, headingOut, leftSub(sub, in.lanes, in.lanes)) : NO_MOVE;
    }

     
    inline std::vector<u32> movementsAt(const RoadGraph& graph, const u32 node)
    {
        std::vector<u32> codes;
        for (u32 headingIn = 0; headingIn < 4; ++headingIn)
        {
            const u32 inEdge = graph.nodes[node].edges[(headingIn + 2) & 3];
            if (inEdge == NO_ID)
            {
                continue;
            }
            for (u32 sub = 0; sub < graph.edges[inEdge].lanes; ++sub)
            {
                for (u32 turn = 0; turn < 4; ++turn)
                {
                    const u32 out = graph.nodes[node].edges[(headingIn + turn) & 3];
                    for (u32 subOut = 0; out != NO_ID && subOut < graph.edges[out].lanes; ++subOut)
                    {
                        const u32 code = movementOf(graph, node, headingIn, sub, (headingIn + turn) & 3, subOut);
                        if (code != NO_MOVE)
                        {
                            codes.push_back(code);
                        }
                    }
                }
            }
        }
        std::ranges::sort(codes);
        codes.erase(std::ranges::unique(codes).begin(), codes.end());
        return codes;
    }

     
    struct MovementPath
    {
        u32 kind = MOVE_STRAIGHT;
        float x0 = 0.0f;  
        float z0 = 0.0f;
        float fx = 1.0f;  
        float fz = 0.0f;
        float gx = 1.0f;  
        float gz = 0.0f;
        float nx = 0.0f;  
        float nz = 0.0f;
        float lead = 0.0f;  
        float radius = 0.0f;
        float angle = 0.0f;  
        float shift = 0.0f;  
        float length = 0.0f;
        float cap = 0.0f;  
    };

     
    inline float stopDistance(const GraphNode& n, const u32 heading) { return n.stop[1 - axisOfHeading(heading)]; }

    inline MovementPath movementPath(const RoadGraph& graph, const u32 node, const u32 code)
    {
        const GraphNode& n = graph.nodes[node];
        const u32 hIn = moveHeadingIn(code);
        const u32 hOut = moveHeadingOut(code);
        const GraphEdge& in = graph.edges[n.edges[(hIn + 2) & 3]];
        const GraphEdge& out = graph.edges[n.edges[hOut]];
        const float offsetIn = in.offset(moveSubIn(code));
        const float offsetOut = out.offset(moveSubOut(code));
        const float stopIn = stopDistance(n, hIn);
        const float stopOut = stopDistance(n, hOut);
        MovementPath path;
        path.fx = headingX(hIn);
        path.fz = headingZ(hIn);
        path.gx = headingX(hOut);
        path.gz = headingZ(hOut);
        path.x0 = -path.fx * stopIn + rightX(hIn) * offsetIn;
        path.z0 = -path.fz * stopIn + rightZ(hIn) * offsetIn;
        const float x1 = path.gx * stopOut + rightX(hOut) * offsetOut;
        const float z1 = path.gz * stopOut + rightZ(hOut) * offsetOut;
        const u32 turn = (hOut - hIn) & 3;
        if (turn == 0)
        {
            path.kind = MOVE_STRAIGHT;
            path.lead = path.length = stopIn + stopOut;
            path.shift = offsetOut - offsetIn;
            path.cap = std::min(in.limit(), out.limit());
            return path;
        }
        if (turn == 2)
        {
             
            path.kind = MOVE_U_TURN;
            path.radius = std::max((offsetIn + offsetOut) * 0.5f, 0.5f);
            path.nx = -rightX(hIn);
            path.nz = -rightZ(hIn);
            path.angle = 3.1415927f;
            path.lead = stopIn;
            path.length = stopIn + path.radius * path.angle + stopOut;
            path.cap = std::min(MOVE_U_TURN_SPEED, std::sqrt(TURN_ACCELERATION * path.radius));
            return path;
        }
         
        const float dx = x1 - path.x0;
        const float dz = z1 - path.z0;
        const float d0 = dx * path.fx + dz * path.fz;
        const float d1 = dx * path.gx + dz * path.gz;
        path.kind = MOVE_TURN;
        path.nx = path.gx;
        path.nz = path.gz;
        path.radius = std::max(std::min(d0, d1) - MOVE_LEAD, 1.0f);
        path.angle = 1.5707963f;
        path.lead = d0 - path.radius;
        path.length = path.lead + path.radius * path.angle + (d1 - path.radius);
        path.cap = std::min(MOVE_TURN_SPEED, std::sqrt(TURN_ACCELERATION * path.radius));
        return path;
    }

     
    inline void pathPoint(const MovementPath& path, const float s, float& x, float& z)
    {
        if (s <= path.lead || path.kind == MOVE_STRAIGHT)
        {
            x = path.x0 + path.fx * s;
            z = path.z0 + path.fz * s;
            if (path.kind == MOVE_STRAIGHT && path.shift != 0.0f)
            {
                 
                const float t = std::clamp((s - 0.25f * path.length) / (0.5f * path.length), 0.0f, 1.0f);
                const float lateral = path.shift * t * t * (3.0f - 2.0f * t);
                x += -path.fz * lateral;
                z += path.fx * lateral;
            }
            return;
        }
        const float ax = path.x0 + path.fx * path.lead;
        const float az = path.z0 + path.fz * path.lead;
        const float cx = ax + path.nx * path.radius;
        const float cz = az + path.nz * path.radius;
        const float theta = std::min((s - path.lead) / path.radius, path.angle);
        const float c = std::cos(theta);
        const float sn = std::sin(theta);
        x = cx - path.nx * path.radius * c + path.fx * path.radius * sn;
        z = cz - path.nz * path.radius * c + path.fz * path.radius * sn;
        const float past = s - path.lead - path.radius * path.angle;
        if (past > 0.0f)
        {
            x += path.gx * past;
            z += path.gz * past;
        }
    }

     
     
    struct BodyPose
    {
        float x = 0.0f;
        float z = 0.0f;
        float dirX = 1.0f;  
        float dirZ = 0.0f;
    };

    inline BodyPose bodyOnPath(const MovementPath& path, const float s)
    {
        float rx = 0.0f, rz = 0.0f, fx = 0.0f, fz = 0.0f;
        pathPoint(path, s - WHEELBASE_HALF, rx, rz);
        pathPoint(path, s + WHEELBASE_HALF, fx, fz);
        BodyPose pose;
        pose.x = (rx + fx) * 0.5f;
        pose.z = (rz + fz) * 0.5f;
        const float dx = fx - rx;
        const float dz = fz - rz;
        const float length = std::sqrt(dx * dx + dz * dz);
        pose.dirX = length > 1e-5f ? dx / length : path.fx;
        pose.dirZ = length > 1e-5f ? dz / length : path.fz;
        return pose;
    }

     
    inline bool bodiesOverlap(const BodyPose& a, const float aHalfLength, const float aHalfWidth, const BodyPose& b,
                              const float bHalfLength, const float bHalfWidth)
    {
        const float dx = b.x - a.x;
        const float dz = b.z - a.z;
        const float axes[4][2] = {{a.dirX, a.dirZ}, {-a.dirZ, a.dirX}, {b.dirX, b.dirZ}, {-b.dirZ, b.dirX}};
        for (const auto& axis : axes)
        {
            const float distance = std::fabs(dx * axis[0] + dz * axis[1]);
            const float ra = aHalfLength * std::fabs(a.dirX * axis[0] + a.dirZ * axis[1]) +
                aHalfWidth * std::fabs(-a.dirZ * axis[0] + a.dirX * axis[1]);
            const float rb = bHalfLength * std::fabs(b.dirX * axis[0] + b.dirZ * axis[1]) +
                bHalfWidth * std::fabs(-b.dirZ * axis[0] + b.dirX * axis[1]);
            if (distance >= ra + rb)
            {
                return false;
            }
        }
        return true;
    }

     

     
     
    using JunctionKey = std::array<u32, 6>;

    inline JunctionKey junctionKeyOf(const RoadGraph& graph, const u32 node)
    {
        const GraphNode& n = graph.nodes[node];
        JunctionKey key{};
        for (u32 heading = 0; heading < 4; ++heading)
        {
            const u32 edge = n.edges[heading];
            key[heading] = edge == NO_ID ? 0u : 1u + graph.edges[edge].roadClass * 8 + graph.edges[edge].lanes;
        }
        key[4] = static_cast<u32>(std::lround(n.stop[0] * 1000.0f));
        key[5] = static_cast<u32>(std::lround(n.stop[1] * 1000.0f));
        return key;
    }

    struct JunctionType
    {
        std::vector<u32> codes;  
        std::array<u8, 256> index{};  
        std::vector<MovementPath> paths;
        std::vector<float> leave;  
        std::vector<float> meet;  
        std::vector<float> follow;  
        bool freeFlow = true;  
        u32 startHits = 0;  
        u32 endHits = 0;  

        u32 count() const { return static_cast<u32>(codes.size()); }
        u32 indexOf(const u32 code) const { return code < 256 && index[code] != 0xFF ? index[code] : NO_MOVE; }
        float leaveOf(const u32 holder, const u32 request) const { return leave[holder * count() + request]; }
        float meetOf(const u32 request, const u32 holder) const { return meet[request * count() + holder]; }
        float followOf(const u32 request, const u32 holder) const { return follow[request * count() + holder]; }
    };

    namespace detail
    {
         
         
        struct SweepGrid
        {
            float extent = 0.0f;
            u32 side = 0;

            u32 cellOf(const float x, const float z) const
            {
                const u32 i = static_cast<u32>(std::clamp((x + extent) / SWEEP_CELL, 0.0f, side - 1.0f));
                const u32 j = static_cast<u32>(std::clamp((z + extent) / SWEEP_CELL, 0.0f, side - 1.0f));
                return j * side + i;
            }

            template <typename Fn>
            void cover(const BodyPose& pose, const float halfLength, const float halfWidth, const Fn& fn) const
            {
                const float reachX = std::fabs(pose.dirX) * halfLength + std::fabs(pose.dirZ) * halfWidth;
                const float reachZ = std::fabs(pose.dirZ) * halfLength + std::fabs(pose.dirX) * halfWidth;
                const u32 lo = cellOf(pose.x - reachX, pose.z - reachZ);
                const u32 hi = cellOf(pose.x + reachX, pose.z + reachZ);
                for (u32 j = lo / side; j <= hi / side; ++j)
                {
                    for (u32 i = lo % side; i <= hi % side; ++i)
                    {
                        const float cx = (static_cast<float>(i) + 0.5f) * SWEEP_CELL - extent - pose.x;
                        const float cz = (static_cast<float>(j) + 0.5f) * SWEEP_CELL - extent - pose.z;
                        if (std::fabs(cx * pose.dirX + cz * pose.dirZ) <= halfLength &&
                            std::fabs(-cx * pose.dirZ + cz * pose.dirX) <= halfWidth)
                        {
                            fn(j * side + i);
                        }
                    }
                }
            }
        };
    }  

    inline JunctionType buildJunctionType(const RoadGraph& graph, const u32 node)
    {
        JunctionType type;
        type.codes = movementsAt(graph, node);
        type.index.fill(0xFF);
        const u32 count = type.count();
        for (u32 m = 0; m < count; ++m)
        {
            type.index[type.codes[m]] = static_cast<u8>(m);
            type.paths.push_back(movementPath(graph, node, type.codes[m]));
        }
        type.leave.assign(static_cast<sizet>(count) * count, -1.0f);
        type.meet.assign(static_cast<sizet>(count) * count, -1.0f);
        type.follow.assign(static_cast<sizet>(count) * count, -1.0f);
        if (count == 0)
        {
            return type;
        }
        const GraphNode& n = graph.nodes[node];
        detail::SweepGrid grid;
        grid.extent = std::max(n.stop[0], n.stop[1]) + 2.0f * SWEEP_HALF_LENGTH + 1.0f;
        grid.side = static_cast<u32>(std::ceil(2.0f * grid.extent / SWEEP_CELL));
        const sizet cells = static_cast<sizet>(grid.side) * grid.side;
        const float halfLength = SWEEP_HALF_LENGTH + SWEEP_MARGIN;
        const float halfWidth = SWEEP_HALF_WIDTH + SWEEP_MARGIN;
         
         
        std::vector<float> first(cells * count, -1.0f);
        std::vector<float> last(cells * count, -1.0f);
        std::vector<std::vector<u32>> covered(count);
        for (u32 m = 0; m < count; ++m)
        {
            const MovementPath& path = type.paths[m];
            float* row = last.data() + cells * m;
            float* firstRow = first.data() + cells * m;
            const u32 samples = static_cast<u32>(std::ceil(path.length / SWEEP_STEP));
            for (u32 k = 0; k <= samples; ++k)
            {
                const float s = std::min(static_cast<float>(k) * SWEEP_STEP, path.length);
                grid.cover(bodyOnPath(path, s), halfLength, halfWidth,
                           [&](const u32 cell)
                           {
                               if (row[cell] < 0.0f)
                               {
                                   covered[m].push_back(cell);
                                   firstRow[cell] = s;
                               }
                               row[cell] = std::max(row[cell], s);
                           });
            }
        }
         
         
        for (u32 a = 0; a < count; ++a)
        {
            for (u32 b = 0; b < count; ++b)
            {
                if (a == b)
                {
                    continue;
                }
                const float* row = last.data() + cells * a;
                const float* firstRow = first.data() + cells * a;
                float leave = -1.0f;
                float meet = -1.0f;
                for (const u32 cell : covered[b])
                {
                    leave = std::max(leave, row[cell]);
                    meet = firstRow[cell] >= 0.0f && (meet < 0.0f || firstRow[cell] < meet) ? firstRow[cell] : meet;
                }
                type.leave[static_cast<sizet>(a) * count + b] = leave;
                type.meet[static_cast<sizet>(a) * count + b] = meet;
                const u32 ca = type.codes[a];
                const u32 cb = type.codes[b];
                const bool sameEntry = (ca & 15) == (cb & 15);
                if (leave >= 0.0f && !sameEntry)
                {
                    type.freeFlow = false;
                }
            }
        }
         
         
         
        for (u32 a = 0; a < count; ++a)
        {
            for (u32 b = 0; b < count; ++b)
            {
                if (a == b || (type.codes[a] & 15) != (type.codes[b] & 15))
                {
                    continue;
                }
                float follow = 0.0f;
                for (float sb = 0.0f; sb <= type.paths[b].length; sb += SWEEP_STEP)
                {
                    const BodyPose ahead = bodyOnPath(type.paths[b], sb);
                    float sa = sb;
                    while (bodiesOverlap(bodyOnPath(type.paths[a], sa), halfLength, halfWidth, ahead, halfLength,
                                         halfWidth))
                    {
                        sa -= SWEEP_STEP;
                    }
                    follow = std::max(follow, sb - sa);
                }
                type.follow[static_cast<sizet>(a) * count + b] = follow + SWEEP_STEP;
            }
        }
         
        const auto standing = [&](const u32 m, const bool atEnd, const auto& sameLane)
        {
            const MovementPath& path = type.paths[m];
            std::vector<u8> body(cells, 0);
            grid.cover(bodyOnPath(path, atEnd ? path.length : 0.0f), SWEEP_HALF_LENGTH + 0.18f,
                       SWEEP_HALF_WIDTH + 0.18f, [&](const u32 cell) { body[cell] = 1; });
            u32 hits = 0;
            for (u32 other = 0; other < count; ++other)
            {
                if (sameLane(type.codes[m], type.codes[other]))
                {
                    continue;
                }
                hits += std::ranges::any_of(covered[other], [&](const u32 cell) { return body[cell] != 0; }) ? 1 : 0;
            }
            return hits;
        };
        for (u32 m = 0; m < count; ++m)
        {
            type.startHits += standing(m, false, [](const u32 a, const u32 b) { return (a & 15) == (b & 15); });
            type.endHits += standing(m, true, [](const u32 a, const u32 b) { return (a >> 4) == (b >> 4); });
        }
        return type;
    }

     
     
    class JunctionTable
    {
    public:
        explicit JunctionTable(const RoadGraph& graph)
        {
            m_typeOf.resize(graph.nodes.size());
            std::map<JunctionKey, u32> local;
            for (u32 node = 0; node < graph.nodes.size(); ++node)
            {
                const JunctionKey key = junctionKeyOf(graph, node);
                const auto found = local.find(key);
                if (found != local.end())
                {
                    m_typeOf[node] = found->second;
                    continue;
                }
                const u32 id = static_cast<u32>(m_types.size());
                m_types.push_back(sharedType(graph, node, key));
                local.emplace(key, id);
                m_typeOf[node] = id;
            }
        }

        const JunctionType& at(const u32 node) const { return *m_types[m_typeOf[node]]; }
        u32 typeCount() const { return static_cast<u32>(m_types.size()); }
        const JunctionType& type(const u32 id) const { return *m_types[id]; }

    private:
        static std::shared_ptr<const JunctionType> sharedType(const RoadGraph& graph, const u32 node,
                                                              const JunctionKey& key)
        {
            struct Cache
            {
                std::mutex lock;
                std::map<JunctionKey, std::shared_ptr<const JunctionType>> types;
            };
            static Cache cache;
            {
                const std::scoped_lock hold(cache.lock);
                const auto found = cache.types.find(key);
                if (found != cache.types.end())
                {
                    return found->second;
                }
            }
            auto built = std::make_shared<const JunctionType>(buildJunctionType(graph, node));
            const std::scoped_lock hold(cache.lock);
            return cache.types.emplace(key, std::move(built)).first->second;
        }

        std::vector<u32> m_typeOf;
        std::vector<std::shared_ptr<const JunctionType>> m_types;
    };
}  
