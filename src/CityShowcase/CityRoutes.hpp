#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "CityModel.hpp"

 
 
 
 
namespace spite::city::model
{
    constexpr float RIGHT_TURN_COST = 3.0f;  
    constexpr float LEFT_TURN_COST = 6.0f;
    constexpr float U_TURN_COST = 12.0f;
    constexpr float SIGNAL_WAIT = 4.0f;  
    constexpr float CROSS_WAIT = 8.0f;  
    constexpr float WALK_SPEED = 1.4f;  
    constexpr float NOMINAL_PHASE = 15.0f;  
    constexpr float MEDIAN_WAIT = 20.0f;  
    constexpr float JAM_COST = 3.0f;  
    constexpr float MAX_OCCUPANCY = 2.0f;

     
    inline float edgeCost(const GraphEdge& edge, const u32 cars)
    {
        const float occupancy = std::min(static_cast<float>(cars) / std::max(edgeCapacity(edge), 1.0f), MAX_OCCUPANCY);
        return edge.length() / edge.limit() * (1.0f + JAM_COST * occupancy * occupancy);
    }

     
    struct SearchScratch
    {
        std::vector<float> cost;
        std::vector<u32> parent;
        std::vector<u32> stamp;
        std::vector<std::pair<float, u32>> heap;
        u32 generation = 0;

        void begin(const sizet states)
        {
            if (cost.size() < states)
            {
                cost.resize(states);
                parent.resize(states);
                stamp.assign(states, 0);
                generation = 0;
            }
            if (++generation == 0)
            {
                std::ranges::fill(stamp, 0u);
                generation = 1;
            }
            heap.clear();
        }
    };

    constexpr auto HEAP_ORDER = [](const std::pair<float, u32>& a, const std::pair<float, u32>& b)
    { return a.first > b.first; };

     
     
     
     
     
     
    template <typename CarsOn>
    bool findCarRoute(const RoadGraph& graph, CarsOn&& carsOn, const u32 fromKey, const u32 heading, const u32 toKey,
                      std::vector<u32>& route, u32* expanded = nullptr)
    {
        thread_local SearchScratch scratch;
        route.clear();
        const u32 from = graph.nodeOfKey(fromKey);
        const u32 to = graph.nodeOfKey(toKey);
        if (from == NO_ID || to == NO_ID || heading > 3)
        {
            return false;
        }
        scratch.begin(graph.nodes.size() * 4);
        const u32 targetX = graph.nodes[to].x;
        const u32 targetZ = graph.nodes[to].z;
        const auto heuristic = [&](const u32 node)
        {
            const GraphNode& n = graph.nodes[node];
            return static_cast<float>((n.x > targetX ? n.x - targetX : targetX - n.x) +
                                      (n.z > targetZ ? n.z - targetZ : targetZ - n.z)) *
                CELL / MAX_LIMIT;
        };
        const u32 start = from * 4 + heading;
        scratch.cost[start] = 0.0f;
        scratch.parent[start] = start;
        scratch.stamp[start] = scratch.generation;
        scratch.heap.push_back({heuristic(from), start});
        u32 settled = 0;
        while (!scratch.heap.empty())
        {
            std::ranges::pop_heap(scratch.heap, HEAP_ORDER);
            const auto [priority, state] = scratch.heap.back();
            scratch.heap.pop_back();
            const u32 node = state >> 2;
            if (priority > scratch.cost[state] + heuristic(node) + 1.0e-3f)
            {
                continue;  
            }
            ++settled;
            if (node == to)
            {
                for (u32 at = state;; at = scratch.parent[at])
                {
                    route.push_back(graph.keyOf(at >> 2));
                    if (at == start)
                    {
                        break;
                    }
                }
                std::ranges::reverse(route);
                if (expanded)
                {
                    *expanded = settled;
                }
                return true;
            }
            const GraphNode& n = graph.nodes[node];
            for (const u32 turn : {0u, 1u, 3u, 2u})
            {
                if (turn == 2 && n.degree != 1)
                {
                    continue;
                }
                const u32 next = (state + turn) & 3;
                const u32 edge = n.edges[next];
                if (edge == NO_ID)
                {
                    continue;
                }
                const float cost = scratch.cost[state] + (n.signal ? SIGNAL_WAIT : 0.0f) +
                    (turn == 1       ? RIGHT_TURN_COST
                         : turn == 3 ? LEFT_TURN_COST
                         : turn == 2 ? U_TURN_COST
                                     : 0.0f) +
                    edgeCost(graph.edges[edge], carsOn(edge, next >= 2 ? 1u : 0u));
                const u32 target = graph.other(edge, node) * 4 + next;
                if (scratch.stamp[target] != scratch.generation || cost < scratch.cost[target])
                {
                    scratch.stamp[target] = scratch.generation;
                    scratch.cost[target] = cost;
                    scratch.parent[target] = state;
                    scratch.heap.push_back({cost + heuristic(target >> 2), target});
                    std::ranges::push_heap(scratch.heap, HEAP_ORDER);
                }
            }
        }
        if (expanded)
        {
            *expanded = settled;
        }
        return false;
    }

     
     
     
    inline bool findPedRoute(const RoadGraph& graph, const u32 fromKey, const u32 toKey, std::vector<u32>& route,
                             u32* expanded = nullptr)
    {
        thread_local SearchScratch scratch;
        route.clear();
        const u32 fromNode = graph.nodeOfKey(fromKey / 4);
        const u32 toNode = graph.nodeOfKey(toKey / 4);
        if (fromNode == NO_ID || toNode == NO_ID)
        {
            return false;
        }
        const u32 from = fromNode * 4 + (fromKey & 3);
        const u32 to = toNode * 4 + (toKey & 3);
        scratch.begin(graph.nodes.size() * 4);
        const GraphNode& goal = graph.nodes[toNode];
        const auto heuristic = [&](const u32 state)
        {
            const GraphNode& n = graph.nodes[state >> 2];
            const float cells = static_cast<float>((n.x > goal.x ? n.x - goal.x : goal.x - n.x) +
                                                   (n.z > goal.z ? n.z - goal.z : goal.z - n.z));
             
            return 0.9f * std::max(cells * CELL - 40.0f, 0.0f) / WALK_SPEED;
        };
        scratch.cost[from] = 0.0f;
        scratch.parent[from] = from;
        scratch.stamp[from] = scratch.generation;
        scratch.heap.push_back({heuristic(from), from});
        u32 settled = 0;
        while (!scratch.heap.empty())
        {
            std::ranges::pop_heap(scratch.heap, HEAP_ORDER);
            const auto [priority, state] = scratch.heap.back();
            scratch.heap.pop_back();
            if (priority > scratch.cost[state] + heuristic(state) + 1.0e-3f)
            {
                continue;
            }
            ++settled;
            if (state == to)
            {
                for (u32 at = state;; at = scratch.parent[at])
                {
                    route.push_back(graph.keyOf(at >> 2) * 4 + (at & 3));
                    if (at == from)
                    {
                        break;
                    }
                }
                std::ranges::reverse(route);
                if (expanded)
                {
                    *expanded = settled;
                }
                return true;
            }
            const u32 node = state >> 2;
            const Vec3 at = cornerPoint(graph, node, state & 3);
            forEachWalk(graph, node, state & 3,
                        [&](const u32 toNodeId, const u32 toCorner, const u32 kind, u32)
                        {
                            const float walk = walkLength(at, cornerPoint(graph, toNodeId, toCorner)) / WALK_SPEED;
                            const float edge = kind != WALK_CROSS
                                ? walk
                                : walk + (graph.nodes[node].signal ? CROSS_WAIT : 0.0f) +
                                    (walk > NOMINAL_PHASE ? MEDIAN_WAIT : 0.0f);
                            const u32 target = toNodeId * 4 + toCorner;
                            const float cost = scratch.cost[state] + edge;
                            if (scratch.stamp[target] != scratch.generation || cost < scratch.cost[target])
                            {
                                scratch.stamp[target] = scratch.generation;
                                scratch.cost[target] = cost;
                                scratch.parent[target] = state;
                                scratch.heap.push_back({cost + heuristic(target), target});
                                std::ranges::push_heap(scratch.heap, HEAP_ORDER);
                            }
                        });
        }
        if (expanded)
        {
            *expanded = settled;
        }
        return false;
    }
}  
