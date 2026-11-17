#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "CityCatalog.hpp"
#include "CityShowcase.hpp"

 
 
 
 
namespace spite::city::model
{
    constexpr float CELL = 8.0f;  
    constexpr float STOP_GAP = 3.0f;  
    constexpr float CAR_HALF_LENGTH = 1.8f;
     
     
    constexpr float CROSSWALK_NEAR = 3.6f;
    constexpr float CROSSWALK_FAR = 0.6f;
    constexpr float STOP_MARGIN = 0.6f;
    constexpr float LANE_WIDTH = 3.5f;
    constexpr u32 MAX_LANES = 4;  
    constexpr u32 NO_ID = 0xFFFFFFFFu;

    constexpr u32 ROAD_STREET = 0;
    constexpr u32 ROAD_AVENUE = 1;
    constexpr u32 ROAD_BOULEVARD = 2;
    constexpr u32 ROAD_HIGHWAY = 3;
    constexpr u32 ROAD_CLASSES = 4;

     
     
     
     
    struct RoadClassSpec
    {
        const char* name;
        u32 lanes;
        float median;
        float shoulder;
        float sidewalk;
        float corridor;
        float limit;  
        u32 cells;
        float parking;
    };
    inline constexpr RoadClassSpec ROAD_CLASS_SPECS[ROAD_CLASSES] = {
        {"street", 1, 0.0f, 0.0f, 2.5f, 8.0f, 13.0f, 1, 2.0f},
        {"avenue", 2, 0.0f, 0.0f, 3.0f, 12.0f, 17.0f, 2, 2.0f},
        {"boulevard", 3, 1.5f, 0.0f, 4.0f, 16.0f, 22.0f, 2, 0.0f},
        {"highway", 3, 0.5f, 5.0f, 0.0f, 16.0f, 30.0f, 2, 0.0f}};
    constexpr float MAX_LIMIT = 30.0f;

    inline u32 roadClassOf(const std::string_view name)
    {
        for (u32 i = 0; i < ROAD_CLASSES; ++i)
        {
            if (name == ROAD_CLASS_SPECS[i].name)
            {
                return i;
            }
        }
        return ROAD_CLASSES;
    }

    inline u32 zoneOf(const std::string_view name)
    {
        for (u32 i = 0; i < ZONE_COUNT; ++i)
        {
            if (name == ZONE_NAMES[i])
            {
                return i;
            }
        }
        return ZONE_COUNT;
    }

     
    inline bool walkable(const u32 roadClass) { return roadClass != ROAD_HIGHWAY; }
    inline u32 clampLanes(const u32 lanes) { return std::clamp(lanes, 1u, MAX_LANES); }
     
    inline float carriageHalf(const u32 lanes, const float median)
    {
        return median + static_cast<float>(lanes) * LANE_WIDTH;
    }

     
     
    inline float laneOffset(const u32 lanes, const float median, const u32 sub)
    {
        return sub >= lanes ? carriageHalf(lanes, median) + (static_cast<float>(sub - lanes) + 0.5f) * LANE_WIDTH
                            : median + (static_cast<float>(lanes - sub) - 0.5f) * LANE_WIDTH;
    }

     

    inline u64 mix(u64 z)
    {
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    inline u64 nextRandom(u64& state) { return mix(state += 0x9E3779B97F4A7C15ull); }

    inline float uniform(u64& state, const float low, const float high)
    {
        return low + (high - low) * (static_cast<float>(nextRandom(state) >> 40) / 16777216.0f);
    }

     
    inline float draw(const u64 seed, const u32 a, const u32 b)
    {
        return static_cast<float>(mix(seed ^ (static_cast<u64>(a) << 32 | b)) >> 40) / 16777216.0f;
    }

    inline Vec4 hsv(const float hue, const float saturation, const float value)
    {
        const float h = (hue - std::floor(hue)) * 6.0f;
        const auto channel = [&](const float n)
        {
            const float k = std::fmod(n + h, 6.0f);
            return value * (1.0f - saturation * std::clamp(std::min(k, 4.0f - k), 0.0f, 1.0f));
        };
        return {channel(5.0f), channel(3.0f), channel(1.0f), 1.0f};
    }

     

     
    struct DesignEdge
    {
        u32 x0 = 0;
        u32 z0 = 0;
        u32 x1 = 0;
        u32 z1 = 0;
        u32 roadClass = ROAD_STREET;
        u32 lanes = 1;

        u32 axis() const { return z0 == z1 ? 0u : 1u; }
        u32 line() const { return axis() == 0 ? z0 : x0; }
        u32 lo() const { return axis() == 0 ? x0 : z0; }
        u32 hi() const { return axis() == 0 ? x1 : z1; }
        bool operator==(const DesignEdge&) const = default;
    };

     
    struct DesignLot
    {
        u32 id = 0;
        u32 x0 = 0;
        u32 z0 = 0;
        u32 x1 = 0;
        u32 z1 = 0;
        u32 zone = ZONE_RESIDENTIAL_LOW;
        u32 special = SPECIAL_NONE;
        u32 axis = 0;
        u32 line = 0;

         
        int side() const { return (axis == 0 ? z0 : x0) > line ? 1 : -1; }
         
        u32 frontCell() const { return axis == 0 ? (x0 + x1 - 1) / 2 : (z0 + z1 - 1) / 2; }
        bool contains(const u32 cx, const u32 cz) const { return cx >= x0 && cx < x1 && cz >= z0 && cz < z1; }
         
        u32 centre2X() const { return x0 + x1; }
        u32 centre2Z() const { return z0 + z1; }
        bool operator==(const DesignLot&) const = default;
    };

    struct DesignBuilding
    {
        u32 lot = 0;
        u32 zone = ZONE_RESIDENTIAL_LOW;
        std::string model;
        float x = 0.0f;
        float z = 0.0f;
        float halfX = 0.0f;
        float halfZ = 0.0f;
        u32 rotation = 0;
        float height = 0.0f;
        Vec4 color{1.0f};
        float dusk = 0.75f;
        float dawn = 0.25f;
        u32 windows = 0;
        bool operator==(const DesignBuilding&) const = default;
    };

     
    struct ZoneOverride
    {
        u32 x0 = 0;
        u32 z0 = 0;
        u32 x1 = 0;
        u32 z1 = 0;
        u32 zone = 0;
        u32 special = SPECIAL_NONE;
        bool operator==(const ZoneOverride&) const = default;
    };

     
    struct CityDesign
    {
        u64 seed = 0;
        u32 size = 0;
        std::vector<DesignEdge> edges;
        std::vector<DesignLot> lots;
        std::vector<DesignBuilding> buildings;
        std::vector<ZoneOverride> overrides;

        u32 nextLotId() const { return lots.empty() ? 0 : std::ranges::max(lots, {}, &DesignLot::id).id + 1; }
        bool operator==(const CityDesign&) const = default;
    };

     

    struct RoadCells
    {
        u32 size = 0;
        std::vector<u8> roadClass[2];
        std::vector<u8> lanes[2];

        void reset(const u32 cells)
        {
            size = cells;
            for (u32 axis = 0; axis < 2; ++axis)
            {
                roadClass[axis].assign(static_cast<sizet>(cells + 1) * cells, 0);
                lanes[axis].assign(static_cast<sizet>(cells + 1) * cells, 0);
            }
        }
        sizet index(const u32 line, const u32 cell) const { return static_cast<sizet>(line) * size + cell; }
        bool has(const u32 axis, const u32 line, const u32 cell) const
        {
            return line <= size && cell < size && roadClass[axis][index(line, cell)] != 0;
        }
        void set(const u32 axis, const u32 line, const u32 from, const u32 to, const u32 cls, const u32 laneCount)
        {
            for (u32 cell = from; cell < to && cell < size; ++cell)
            {
                roadClass[axis][index(line, cell)] = static_cast<u8>(cls + 1);
                lanes[axis][index(line, cell)] = static_cast<u8>(clampLanes(laneCount));
            }
        }
        void clear(const u32 axis, const u32 line, const u32 from, const u32 to)
        {
            for (u32 cell = from; cell < to && cell < size; ++cell)
            {
                roadClass[axis][index(line, cell)] = 0;
                lanes[axis][index(line, cell)] = 0;
            }
        }
        void add(const DesignEdge& edge)
        {
            set(edge.axis(), edge.line(), edge.lo(), edge.hi(), edge.roadClass, edge.lanes);
        }
    };

    inline RoadCells cellsOf(const u32 size, const std::span<const DesignEdge> edges)
    {
        RoadCells cells;
        cells.reset(size);
        for (const DesignEdge& edge : edges)
        {
            cells.add(edge);
        }
        return cells;
    }

     
     
    inline std::vector<DesignEdge> edgesOf(const RoadCells& cells)
    {
        std::vector<DesignEdge> edges;
        const u32 size = cells.size;
        for (u32 axis = 0; axis < 2; ++axis)
        {
            for (u32 line = 0; line <= size; ++line)
            {
                u32 start = NO_ID;
                for (u32 p = 0; p <= size; ++p)
                {
                    const bool left = p > 0 && cells.has(axis, line, p - 1);
                    const bool right = p < size && cells.has(axis, line, p);
                    if (!left && !right)
                    {
                        continue;
                    }
                    bool node = left != right;
                    if (!node)
                    {
                        const sizet a = cells.index(line, p - 1);
                        const sizet b = cells.index(line, p);
                        node = cells.roadClass[axis][a] != cells.roadClass[axis][b] ||
                            cells.lanes[axis][a] != cells.lanes[axis][b] ||
                            (line > 0 && cells.has(1 - axis, p, line - 1)) || cells.has(1 - axis, p, line);
                    }
                    if (!node)
                    {
                        continue;
                    }
                    if (left && start != NO_ID)
                    {
                        const sizet first = cells.index(line, start);
                        const u32 cls = cells.roadClass[axis][first] - 1u;
                        const u32 lanes = cells.lanes[axis][first];
                        edges.push_back(axis == 0 ? DesignEdge{start, line, p, line, cls, lanes}
                                                  : DesignEdge{line, start, line, p, cls, lanes});
                    }
                    start = right ? p : NO_ID;
                }
            }
        }
        return edges;
    }

     

     
    constexpr int HEADING_DX[4] = {1, 0, -1, 0};
    constexpr int HEADING_DZ[4] = {0, 1, 0, -1};

    inline u32 headingOf(const u32 axis, const float dir)
    {
        return axis == 0 ? (dir > 0.0f ? 0u : 2u) : (dir > 0.0f ? 1u : 3u);
    }
    inline u32 axisOfHeading(const u32 heading) { return heading & 1; }
    inline float dirOfHeading(const u32 heading) { return heading < 2 ? 1.0f : -1.0f; }

     
    inline u32 cornerBetween(const u32 a, const u32 b) { return ((a + 1) & 3) == b ? a : b; }

    struct GraphNode
    {
        u32 x = 0;
        u32 z = 0;
        u32 edges[4] = {NO_ID, NO_ID, NO_ID, NO_ID};  
        float half[2] = {0.0f, 0.0f};  
         
         
        float stop[2] = {0.0f, 0.0f};
         
         
        float walk[2] = {0.0f, 0.0f};
        u32 degree = 0;
         
         
        bool signal = false;
    };

    struct GraphEdge
    {
        u32 axis = 0;
        u32 line = 0;
        u32 lo = 0;
        u32 hi = 0;
        u32 roadClass = ROAD_STREET;
        u32 lanes = 1;
        u32 a = NO_ID;  
        u32 b = NO_ID;  

        float median() const { return ROAD_CLASS_SPECS[roadClass].median; }
        float half() const { return carriageHalf(lanes, median()); }
        float limit() const { return ROAD_CLASS_SPECS[roadClass].limit; }
         
        float walkLine() const
        {
            const RoadClassSpec& spec = ROAD_CLASS_SPECS[roadClass];
            return half() + (lanes == spec.lanes ? spec.parking : 0.0f) + spec.sidewalk * 0.5f;
        }
        float length() const { return static_cast<float>(hi - lo) * CELL; }
        float offset(const u32 sub) const { return laneOffset(lanes, median(), sub); }
    };

     
     
    class RoadGraph
    {
    public:
        static constexpr float OVERRUN = 4.0f;  
        u32 size = 0;
        std::vector<GraphNode> nodes;
        std::vector<GraphEdge> edges;
        std::vector<u32> nodeAtKey;  
        std::vector<u32> edgeAtCell[2];  

        u32 key(const u32 x, const u32 z) const { return z * (size + 1) + x; }
        u32 keyOf(const u32 node) const { return key(nodes[node].x, nodes[node].z); }
        u32 nodeAt(const u32 x, const u32 z) const { return x <= size && z <= size ? nodeAtKey[key(x, z)] : NO_ID; }
        u32 nodeOfKey(const u32 nodeKey) const { return nodeKey < nodeAtKey.size() ? nodeAtKey[nodeKey] : NO_ID; }
        u32 edgeAt(const u32 axis, const u32 line, const u32 cell) const
        {
            return line <= size && cell < size ? edgeAtCell[axis][static_cast<sizet>(line) * size + cell] : NO_ID;
        }
         
         
         
        u32 edgeUnder(const u32 axis, const u32 line, const float pos, const float dir) const
        {
            const u32 edge = cellEdge(axis, line, pos, dir);
            return edge != NO_ID ? edge : cellEdge(axis, line, pos - dir * OVERRUN, dir);
        }
        u32 cellEdge(const u32 axis, const u32 line, const float pos, const float dir) const
        {
            const float c = pos / CELL;
            const float cell = dir > 0.0f ? std::floor(c) : std::ceil(c) - 1.0f;
            return cell < 0.0f || cell >= static_cast<float>(size) ? NO_ID : edgeAt(axis, line, static_cast<u32>(cell));
        }
        u32 other(const u32 edge, const u32 node) const
        {
            return edges[edge].a == node ? edges[edge].b : edges[edge].a;
        }
         
        u32 headingAlong(const u32 edge, const u32 node) const
        {
            const GraphEdge& e = edges[edge];
            return e.axis == 0 ? (e.a == node ? 0u : 2u) : (e.a == node ? 1u : 3u);
        }
         
        u32 along(const u32 node, const u32 axis) const { return axis == 0 ? nodes[node].x : nodes[node].z; }
         
        u32 headingBetween(const u32 fromKey, const u32 toKey) const
        {
            const int dx = static_cast<int>(toKey % (size + 1)) - static_cast<int>(fromKey % (size + 1));
            const int dz = static_cast<int>(toKey / (size + 1)) - static_cast<int>(fromKey / (size + 1));
            if ((dx != 0) == (dz != 0))
            {
                return NO_ID;
            }
            return dx > 0 ? 0u : dz > 0 ? 1u : dx < 0 ? 2u : 3u;
        }
         
        u32 edgeBetween(const u32 fromKey, const u32 toKey) const
        {
            const u32 from = nodeOfKey(fromKey);
            const u32 heading = from == NO_ID ? NO_ID : headingBetween(fromKey, toKey);
            if (heading == NO_ID)
            {
                return NO_ID;
            }
            const u32 edge = nodes[from].edges[heading];
            return edge != NO_ID && keyOf(other(edge, from)) == toKey ? edge : NO_ID;
        }
         
        bool routeValid(const std::span<const u32> route, const u32 index) const
        {
            for (sizet i = index; i + 1 < route.size(); ++i)
            {
                if (edgeBetween(route[i], route[i + 1]) == NO_ID)
                {
                    return false;
                }
            }
            return true;
        }
    };

     
     
    inline float junctionHalf(const RoadGraph& graph, const u32 node, const u32 axis)
    {
        float half = 0.0f;
        float own = 0.0f;
        for (u32 heading = 0; heading < 4; ++heading)
        {
            const u32 edge = graph.nodes[node].edges[heading];
            if (edge != NO_ID)
            {
                const float corridor =
                    std::max(ROAD_CLASS_SPECS[graph.edges[edge].roadClass].corridor, graph.edges[edge].half() + 1.0f);
                (axisOfHeading(heading) != axis ? half : own) =
                    std::max(axisOfHeading(heading) != axis ? half : own, corridor);
            }
        }
        return half > 0.0f ? half : own;
    }

     
    inline u32 widestClassAt(const RoadGraph& graph, const u32 node)
    {
        u32 widest = ROAD_STREET;
        for (const u32 edge : graph.nodes[node].edges)
        {
            widest = edge != NO_ID ? std::max(widest, graph.edges[edge].roadClass) : widest;
        }
        return widest;
    }

     
     
    struct CrosswalkSpan
    {
        float inner = 0.0f;
        float outer = 0.0f;
    };

    inline CrosswalkSpan crosswalkSpan(const RoadGraph& graph, const u32 node, const u32 axis)
    {
        const float square = junctionHalf(graph, node, axis);
        const float corridor = ROAD_CLASS_SPECS[widestClassAt(graph, node)].corridor;
        return {square * (1.0f - CROSSWALK_NEAR / corridor), square * (1.0f - CROSSWALK_FAR / corridor)};
    }

    inline std::shared_ptr<const RoadGraph> buildGraph(const u32 size, const std::span<const DesignEdge> source)
    {
        auto graph = std::make_shared<RoadGraph>();
        graph->size = size;
        std::vector<DesignEdge> sorted(source.begin(), source.end());
        std::ranges::sort(sorted,
                          [](const DesignEdge& a, const DesignEdge& b)
                          {
                              return a.axis() != b.axis() ? a.axis() < b.axis()
                                  : a.line() != b.line()  ? a.line() < b.line()
                                                          : a.lo() < b.lo();
                          });
        graph->nodeAtKey.assign(static_cast<sizet>(size + 1) * (size + 1), NO_ID);
        std::vector<u32> keys;
        keys.reserve(sorted.size() * 2);
        for (const DesignEdge& edge : sorted)
        {
            keys.push_back(graph->key(edge.x0, edge.z0));
            keys.push_back(graph->key(edge.x1, edge.z1));
        }
        std::ranges::sort(keys);
        keys.erase(std::ranges::unique(keys).begin(), keys.end());
        graph->nodes.resize(keys.size());
        for (u32 id = 0; id < keys.size(); ++id)
        {
            graph->nodeAtKey[keys[id]] = id;
            graph->nodes[id].x = keys[id] % (size + 1);
            graph->nodes[id].z = keys[id] / (size + 1);
        }
        for (u32 axis = 0; axis < 2; ++axis)
        {
            graph->edgeAtCell[axis].assign(static_cast<sizet>(size + 1) * size, NO_ID);
        }
        graph->edges.reserve(sorted.size());
        for (const DesignEdge& source : sorted)
        {
            GraphEdge edge{.axis = source.axis(),
                           .line = source.line(),
                           .lo = source.lo(),
                           .hi = source.hi(),
                           .roadClass = std::min(source.roadClass, ROAD_CLASSES - 1),
                           .lanes = clampLanes(source.lanes),
                           .a = graph->nodeAtKey[graph->key(source.x0, source.z0)],
                           .b = graph->nodeAtKey[graph->key(source.x1, source.z1)]};
            const u32 id = static_cast<u32>(graph->edges.size());
            GraphNode& a = graph->nodes[edge.a];
            GraphNode& b = graph->nodes[edge.b];
            a.edges[edge.axis == 0 ? 0 : 1] = id;
            b.edges[edge.axis == 0 ? 2 : 3] = id;
            a.half[edge.axis] = std::max(a.half[edge.axis], edge.half());
            b.half[edge.axis] = std::max(b.half[edge.axis], edge.half());
            a.walk[edge.axis] = std::max(a.walk[edge.axis], edge.walkLine());
            b.walk[edge.axis] = std::max(b.walk[edge.axis], edge.walkLine());
            for (u32 cell = edge.lo; cell < edge.hi && cell < size; ++cell)
            {
                graph->edgeAtCell[edge.axis][static_cast<sizet>(edge.line) * size + cell] = id;
            }
            graph->edges.push_back(edge);
        }
        for (GraphNode& node : graph->nodes)
        {
            node.degree = 0;
            for (const u32 edge : node.edges)
            {
                node.degree += edge != NO_ID ? 1 : 0;
            }
            const bool axis0 = node.edges[0] != NO_ID || node.edges[2] != NO_ID;
            const bool axis1 = node.edges[1] != NO_ID || node.edges[3] != NO_ID;
            node.signal = axis0 && axis1;
            for (float& walk : node.walk)
            {
                walk = std::max(walk, 3.25f);  
            }
        }
        for (u32 id = 0; id < graph->nodes.size(); ++id)
        {
            for (u32 axis = 0; axis < 2; ++axis)
            {
                 
                graph->nodes[id].stop[1 - axis] =
                    std::max(graph->nodes[id].half[1 - axis] + STOP_GAP,
                             crosswalkSpan(*graph, id, axis).outer + STOP_MARGIN + CAR_HALF_LENGTH);
            }
        }
        return graph;
    }

     
    struct RoadAt
    {
        u32 node = NO_ID;
        u32 edge = NO_ID;
    };

    inline RoadAt roadAt(const RoadGraph& graph, const u32 x, const u32 z)
    {
        RoadAt at;
        at.node = graph.nodeAt(x, z);
        if (at.node != NO_ID)
        {
            return at;
        }
        for (const u32 cell : {x, x - 1})
        {
            if (at.edge == NO_ID && cell < graph.size)
            {
                at.edge = graph.edgeAt(0, z, cell);
            }
        }
        for (const u32 cell : {z, z - 1})
        {
            if (at.edge == NO_ID && cell < graph.size)
            {
                at.edge = graph.edgeAt(1, x, cell);
            }
        }
        return at;
    }

     
    inline u32 lotEdge(const RoadGraph& graph, const DesignLot& lot)
    {
        return graph.edgeAt(lot.axis, lot.line, lot.frontCell());
    }

     
    inline u32 lotSideHeading(const u32 axis, const int side)
    {
        return axis == 0 ? (side > 0 ? 1u : 3u) : (side > 0 ? 0u : 2u);
    }

     

    constexpr u32 CATEGORY_HOME = 0;  
    constexpr u32 CATEGORY_WORK = 1;  
    constexpr u32 CATEGORY_SHOP = 2;  
    constexpr u32 CATEGORIES = 3;

    inline bool inCategory(const u32 zone, const u32 category)
    {
        switch (category)
        {
        case CATEGORY_HOME:
            return zone == ZONE_RESIDENTIAL_LOW || zone == ZONE_RESIDENTIAL_HIGH;
        case CATEGORY_WORK:
            return zone >= ZONE_COMMERCIAL_LOW && zone < ZONE_COUNT;
        default:
            return zone == ZONE_COMMERCIAL_LOW || zone == ZONE_COMMERCIAL_HIGH;
        }
    }

    constexpr u64 ZONE_SALT = 0x2073ull;
    constexpr u64 LOT_SALT = 0x1075ull;
    constexpr u64 BUILDING_SALT = 0xB111ull;
    constexpr u64 STREET_SALT = 0x57EEull;

     
    inline float valueNoise(const u64 seed, const float x, const float z, const float scale)
    {
        const float fx = x / scale;
        const float fz = z / scale;
        const u32 ix = static_cast<u32>(std::floor(fx));
        const u32 iz = static_cast<u32>(std::floor(fz));
        const float tx = fx - std::floor(fx);
        const float tz = fz - std::floor(fz);
        const auto at = [&](const u32 a, const u32 b) { return 2.0f * draw(seed, a, b) - 1.0f; };
        const float sx = tx * tx * (3.0f - 2.0f * tx);
        const float sz = tz * tz * (3.0f - 2.0f * tz);
        const float low = at(ix, iz) + (at(ix + 1, iz) - at(ix, iz)) * sx;
        const float high = at(ix, iz + 1) + (at(ix + 1, iz + 1) - at(ix, iz + 1)) * sx;
        return low + (high - low) * sz;
    }

     
    inline float zoneRadius(const u64 seed, const u32 size, const float cx, const float cz)
    {
        const float half = static_cast<float>(size) * 0.5f;
        return std::hypot(cx - half, cz - half) / half + 0.1f * valueNoise(seed ^ ZONE_SALT, cx, cz, 24.0f);
    }

     
    inline const ZoneOverride* overrideAt(const std::span<const ZoneOverride> overrides, const float cx, const float cz)
    {
        for (auto it = overrides.rbegin(); it != overrides.rend(); ++it)
        {
            if (cx >= static_cast<float>(it->x0) && cx < static_cast<float>(it->x1) &&
                cz >= static_cast<float>(it->z0) && cz < static_cast<float>(it->z1))
            {
                return &*it;
            }
        }
        return nullptr;
    }

     
     
     
    inline u32 zoneRule(const u64 seed, const u32 size, const float cx, const float cz, const u32 roadClass,
                        const std::span<const ZoneOverride> overrides)
    {
        if (const ZoneOverride* over = overrideAt(overrides, cx, cz))
        {
            return over->zone;
        }
        const float r = zoneRadius(seed, size, cx, cz);
        const bool arterial = roadClass == ROAD_AVENUE || roadClass == ROAD_BOULEVARD;
        const float pick = draw(seed ^ ZONE_SALT ^ 1, static_cast<u32>(cx), static_cast<u32>(cz));
        if (r < 0.14f)
        {
            return ZONE_COMMERCIAL_HIGH;
        }
        if (r < 0.3f)
        {
            return arterial ? ZONE_COMMERCIAL_LOW : ZONE_RESIDENTIAL_HIGH;
        }
        if (r < 0.85f)
        {
            return arterial ? (pick < 0.45f ? ZONE_COMMERCIAL_LOW : ZONE_RESIDENTIAL_HIGH) : ZONE_RESIDENTIAL_LOW;
        }
        return pick < 0.12f ? ZONE_SPECIAL : ZONE_INDUSTRIAL;
    }

    inline u32 specialOf(const u64 seed, const u32 lotId, const u32 zone)
    {
        const u32 roll = static_cast<u32>(mix(seed ^ LOT_SALT ^ 7 ^ lotId) % 8);
        if (zone == ZONE_SPECIAL)
        {
            return 1 + roll % 4;
        }
        if (zone == ZONE_INDUSTRIAL && roll < 2)
        {
            return roll == 0 ? SPECIAL_FACTORY : SPECIAL_WAREHOUSE;
        }
        return SPECIAL_NONE;
    }

     
     
    constexpr u32 HUB_BLOCK = 40;
    constexpr u32 HUB_REACH = 8;
    constexpr u32 HUB_CELLS = 4;
    constexpr u64 HUB_SALT = 0x4B0Bull;

    inline u32 hubBlocks(const u32 size) { return (size + HUB_BLOCK - 1) / HUB_BLOCK; }

    inline u32 hubBlockOf(const u32 size, const float cx, const float cz)
    {
        const u32 blocks = hubBlocks(size);
        return std::min(static_cast<u32>(std::max(cz, 0.0f)) / HUB_BLOCK, blocks - 1) * blocks +
            std::min(static_cast<u32>(std::max(cx, 0.0f)) / HUB_BLOCK, blocks - 1);
    }

     
    inline bool nearHubSpot(const u64 seed, const u32 size, const float cx, const float cz)
    {
        const u32 block = hubBlockOf(size, cx, cz);
        const u32 blocks = hubBlocks(size);
        const float jitter = static_cast<float>(HUB_BLOCK) * 0.25f;
        const float spotX = (static_cast<float>(block % blocks) + 0.5f) * static_cast<float>(HUB_BLOCK) +
            (draw(seed ^ HUB_SALT, block, 0) * 2.0f - 1.0f) * jitter;
        const float spotZ = (static_cast<float>(block / blocks) + 0.5f) * static_cast<float>(HUB_BLOCK) +
            (draw(seed ^ HUB_SALT, block, 1) * 2.0f - 1.0f) * jitter;
        return std::fabs(cx - spotX) + std::fabs(cz - spotZ) <= static_cast<float>(HUB_REACH);
    }

     
    inline void lotShape(const u32 zone, const u64 hash, u32& width, u32& depth)
    {
        const u32 roll = static_cast<u32>(hash % 2);
        switch (zone)
        {
        case ZONE_RESIDENTIAL_LOW:
            width = 1;
            depth = 2 + roll;
            break;
        case ZONE_RESIDENTIAL_HIGH:
        case ZONE_COMMERCIAL_LOW:
            width = 2 + roll;
            depth = 3;
            break;
        case ZONE_COMMERCIAL_HIGH:
            width = 3 + roll;
            depth = 3 + roll;
            break;
        default:
            width = 4;
            depth = 4;
            break;
        }
    }

     
     
     
     
    inline u32 fallbackZone(const u32 zone)
    {
        constexpr u32 FALLBACK[ZONE_COUNT] = {ZONE_COMMERCIAL_LOW,   ZONE_COMMERCIAL_HIGH, ZONE_RESIDENTIAL_LOW,
                                              ZONE_RESIDENTIAL_HIGH, ZONE_COMMERCIAL_LOW,  ZONE_COMMERCIAL_LOW};
        return FALLBACK[std::min(zone, ZONE_COUNT - 1)];
    }

    inline const catalog::BuildingModel* modelFor(const DesignLot& lot, const float height, const bool fallback = false)
    {
        const u32 width = lot.axis == 0 ? lot.x1 - lot.x0 : lot.z1 - lot.z0;
        const u32 depth = lot.axis == 0 ? lot.z1 - lot.z0 : lot.x1 - lot.x0;
        const catalog::BuildingModel* best = nullptr;
        for (const catalog::BuildingModel& model : catalog::BUILDINGS)
        {
            const bool use = fallback         ? model.zone == fallbackZone(lot.zone) && model.special == SPECIAL_NONE
                : lot.special != SPECIAL_NONE ? model.special == lot.special
                                              : model.zone == lot.zone;
            if (!use || model.width > width || model.depth > depth)
            {
                continue;
            }
            const u32 area = model.width * model.depth;
            const u32 bestArea = best ? best->width * best->depth : 0;
            if (!best || area > bestArea ||
                (area == bestArea && std::fabs(model.height - height) < std::fabs(best->height - height)))
            {
                best = &model;
            }
        }
        return best || fallback ? best : modelFor(lot, height, true);
    }

     
     
    inline std::array<float, 4> turnedExtent(const catalog::BuildingModel& model, const u32 rotation)
    {
        switch (rotation & 3)
        {
        case 0:
            return {model.minZ, model.maxZ, -model.maxX, -model.minX};
        case 2:
            return {-model.maxZ, -model.minZ, model.minX, model.maxX};
        case 3:
            return {-model.maxX, -model.minX, -model.maxZ, -model.minZ};
        default:
            return {model.minX, model.maxX, model.minZ, model.maxZ};
        }
    }

     
     
     
    inline DesignBuilding buildingFor(const u64 seed, const u32 size, const DesignLot& lot)
    {
        u64 rng = mix(seed ^ BUILDING_SALT ^ (static_cast<u64>(lot.id) << 8 | lot.zone));
        DesignBuilding building;
        building.lot = lot.id;
        building.zone = lot.zone;
        const float x0 = static_cast<float>(lot.x0) * CELL;
        const float z0 = static_cast<float>(lot.z0) * CELL;
        const float x1 = static_cast<float>(lot.x1) * CELL;
        const float z1 = static_cast<float>(lot.z1) * CELL;
        const float inset = lot.zone == ZONE_RESIDENTIAL_LOW ? uniform(rng, 1.5f, 3.0f) : uniform(rng, 0.8f, 2.0f);
        building.x = (x0 + x1) * 0.5f;
        building.z = (z0 + z1) * 0.5f;
        building.halfX = std::max((x1 - x0) * 0.5f - inset, 1.0f);
        building.halfZ = std::max((z1 - z0) * 0.5f - inset, 1.0f);
        building.rotation = (lotSideHeading(lot.axis, lot.side()) + 2) & 3;
        float hue = 0.0f;
        float windowsChance = 0.6f;
        switch (lot.zone)
        {
        case ZONE_RESIDENTIAL_LOW:
            building.height = uniform(rng, 3.0f, 11.0f);
            hue = uniform(rng, 0.02f, 0.1f);
            windowsChance = 0.7f;
            break;
        case ZONE_RESIDENTIAL_HIGH:
            building.height = uniform(rng, 14.0f, 42.0f);
            hue = uniform(rng, 0.27f, 0.38f);
            windowsChance = 0.65f;
            break;
        case ZONE_COMMERCIAL_LOW:
            building.height = uniform(rng, 5.0f, 12.0f);
            hue = uniform(rng, 0.5f, 0.55f);
            windowsChance = 0.8f;
            break;
        case ZONE_COMMERCIAL_HIGH:
            {
                 
                const float core =
                    std::clamp(1.0f - zoneRadius(seed, size, building.x / CELL, building.z / CELL) / 0.14f, 0.0f, 1.0f);
                building.height =
                    (30.0f + 130.0f * core * uniform(rng, 0.3f, 1.0f)) * (nextRandom(rng) % 20 == 0 ? 1.5f : 1.0f);
                hue = uniform(rng, 0.55f, 0.63f);
                windowsChance = 0.75f;
                break;
            }
        case ZONE_INDUSTRIAL:
            building.height = uniform(rng, 7.0f, 16.0f);
            hue = uniform(rng, 0.1f, 0.15f);
            windowsChance = 0.2f;
            break;
        default:
            building.height = uniform(rng, 9.0f, 18.0f);
            hue = uniform(rng, 0.75f, 0.8f);
            windowsChance = 0.5f;
            break;
        }
        building.color = hsv(hue, uniform(rng, 0.35f, 0.75f), uniform(rng, 0.6f, 0.92f));
        building.dusk = uniform(rng, 0.74f, 0.82f);
        building.dawn = uniform(rng, 0.2f, 0.28f);
        building.windows = uniform(rng, 0.0f, 1.0f) < windowsChance ? 1u : 0u;
        if (const catalog::BuildingModel* model = modelFor(lot, building.height))
        {
            const std::array<float, 4> extent = turnedExtent(*model, building.rotation);
            building.model = model->name;
             
            building.halfX = std::min((extent[1] - extent[0]) * 0.5f, (x1 - x0) * 0.5f);
            building.halfZ = std::min((extent[3] - extent[2]) * 0.5f, (z1 - z0) * 0.5f);
            building.height = model->height;
        }
        return building;
    }

     
    class Occupancy
    {
    public:
        Occupancy(const u32 size, const std::span<const DesignEdge> edges, const std::span<const DesignLot> lots) :
            m_size(size), m_taken(static_cast<sizet>(size) * size, 0)
        {
            for (const DesignEdge& edge : edges)
            {
                const u32 c = ROAD_CLASS_SPECS[std::min(edge.roadClass, ROAD_CLASSES - 1)].cells;
                const u32 lo = edge.lo() > c ? edge.lo() - c : 0;
                const u32 across = edge.line() > c ? edge.line() - c : 0;
                if (edge.axis() == 0)
                {
                    mark(lo, across, edge.hi() + c, edge.line() + c);
                }
                else
                {
                    mark(across, lo, edge.line() + c, edge.hi() + c);
                }
            }
            for (const DesignLot& lot : lots)
            {
                mark(lot.x0, lot.z0, lot.x1, lot.z1);
            }
        }

        bool free(const u32 x0, const u32 z0, const u32 x1, const u32 z1) const
        {
            if (x1 > m_size || z1 > m_size || x0 >= x1 || z0 >= z1)
            {
                return false;
            }
            for (u32 z = z0; z < z1; ++z)
            {
                for (u32 x = x0; x < x1; ++x)
                {
                    if (m_taken[static_cast<sizet>(z) * m_size + x] != 0)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        void mark(const u32 x0, const u32 z0, const u32 x1, const u32 z1)
        {
            for (u32 z = z0; z < std::min(z1, m_size); ++z)
            {
                for (u32 x = x0; x < std::min(x1, m_size); ++x)
                {
                    m_taken[static_cast<sizet>(z) * m_size + x] = 1;
                }
            }
        }

    private:
        u32 m_size;
        std::vector<u8> m_taken;
    };

     
     
    template <typename Wanted>
    u32 fillLots(CityDesign& design, Wanted&& wanted, const bool withBuildings = true)
    {
        Occupancy occupancy(design.size, design.edges, design.lots);
        u32 nextId = design.nextLotId();
        u32 added = 0;
        std::vector<u8> hubbed(static_cast<sizet>(hubBlocks(design.size)) * hubBlocks(design.size), 0);
        for (const DesignLot& lot : design.lots)
        {
            if (lot.special == SPECIAL_DRONE_HUB)
            {
                hubbed[hubBlockOf(design.size, static_cast<float>(lot.centre2X()) * 0.5f,
                                  static_cast<float>(lot.centre2Z()) * 0.5f)] = 1;
            }
        }
        for (u32 index = 0; index < design.edges.size(); ++index)
        {
            const DesignEdge edge = design.edges[index];
            if (!walkable(edge.roadClass) || !wanted(edge))
            {
                continue;
            }
            const u32 axis = edge.axis();
            const u32 line = edge.line();
            const u32 c = ROAD_CLASS_SPECS[edge.roadClass].cells;
            for (const int side : {-1, 1})
            {
                if (side < 0 && line < c + 2)
                {
                    continue;
                }
                 
                 
                const auto place = [&](const u32 p, const u32 zone, const u32 special, const u32 width, const u32 depth,
                                       const u32 minimum, const u32 minimumWidth)
                {
                    bool placed = false;
                    u32 frontage = 0;
                    for (u32 w = width; w >= minimumWidth && !placed; --w)
                    {
                        for (u32 d = depth; d >= minimum && !placed; --d)
                        {
                            if (p + w > edge.hi())
                            {
                                continue;
                            }
                            const u32 a0 = side > 0 ? line + c : line - c - d;
                            const u32 a1 = a0 + d;
                            DesignLot lot{.id = nextId, .zone = zone, .axis = axis, .line = line};
                            if (axis == 0)
                            {
                                lot.x0 = p;
                                lot.x1 = p + w;
                                lot.z0 = a0;
                                lot.z1 = a1;
                            }
                            else
                            {
                                lot.x0 = a0;
                                lot.x1 = a1;
                                lot.z0 = p;
                                lot.z1 = p + w;
                            }
                            if (!occupancy.free(lot.x0, lot.z0, lot.x1, lot.z1))
                            {
                                continue;
                            }
                            occupancy.mark(lot.x0, lot.z0, lot.x1, lot.z1);
                            lot.special = special != SPECIAL_NONE ? special : specialOf(design.seed, lot.id, zone);
                            design.lots.push_back(lot);
                            if (withBuildings && draw(design.seed ^ LOT_SALT ^ 3, lot.id, 0) >= 0.04f)
                            {
                                design.buildings.push_back(buildingFor(design.seed, design.size, lot));
                            }
                            ++nextId;
                            ++added;
                            frontage = w;
                            placed = true;
                        }
                    }
                    return frontage;
                };
                u32 p = edge.lo();
                while (p < edge.hi())
                {
                    const float frontAcross =
                        side > 0 ? static_cast<float>(line + c) + 1.0f : static_cast<float>(line - c) - 1.0f;
                    const float cx = axis == 0 ? static_cast<float>(p) + 1.0f : frontAcross;
                    const float cz = axis == 0 ? frontAcross : static_cast<float>(p) + 1.0f;
                    const ZoneOverride* over = overrideAt(design.overrides, cx, cz);
                     
                    const float hubAcross = side > 0 ? static_cast<float>(line + c + HUB_CELLS / 2)
                                                     : static_cast<float>(line - c - HUB_CELLS / 2);
                    const float hubX = axis == 0 ? static_cast<float>(p + HUB_CELLS / 2) : hubAcross;
                    const float hubZ = axis == 0 ? hubAcross : static_cast<float>(p + HUB_CELLS / 2);
                    u8& hub = hubbed[hubBlockOf(design.size, hubX, hubZ)];
                    if (!over && hub == 0 && nearHubSpot(design.seed, design.size, hubX, hubZ))
                    {
                        if (const u32 frontage =
                                place(p, ZONE_SPECIAL, SPECIAL_DRONE_HUB, HUB_CELLS, HUB_CELLS, HUB_CELLS, HUB_CELLS))
                        {
                            hub = 1;
                            p += frontage;
                            continue;
                        }
                    }
                    const u32 zone = zoneRule(design.seed, design.size, cx, cz, edge.roadClass, design.overrides);
                    u32 width = 2;
                    u32 depth = 2;
                    lotShape(zone, mix(design.seed ^ LOT_SALT ^ (static_cast<u64>(p) << 32 | line) ^ axis), width,
                             depth);
                    const u32 frontage = place(p, zone, over ? over->special : SPECIAL_NONE, width, depth, 2,
                                               zone == ZONE_RESIDENTIAL_LOW ? 1u : 2u);
                    p += frontage != 0 ? frontage : 1;
                }
            }
        }
        return added;
    }

     

     
    inline std::vector<u32> arterialLines(const u64 seed, const u32 axis, const u32 r0, const u32 r1)
    {
        u64 rng = mix(seed ^ STREET_SALT ^ (axis + 1));
        std::vector<u32> lines;
        u32 p = r0;
        while (true)
        {
            p += 10 + static_cast<u32>(nextRandom(rng) % 7);
            if (p + 8 > r1)
            {
                break;
            }
            lines.push_back(p);
        }
        return lines;
    }

     
     
     
     
    inline CityDesign generateDesign(const u64 seed, const u32 size)
    {
        CityDesign design;
        design.seed = seed;
        design.size = size;
        const u32 margin = std::max(4u, size / 64);
        const u32 r0 = margin;
        const u32 r1 = size - margin;
        RoadCells cells;
        cells.reset(size);
        for (u32 axis = 0; axis < 2; ++axis)
        {
            cells.set(axis, r0, r0, r1, ROAD_HIGHWAY, ROAD_CLASS_SPECS[ROAD_HIGHWAY].lanes);
            cells.set(axis, r1, r0, r1, ROAD_HIGHWAY, ROAD_CLASS_SPECS[ROAD_HIGHWAY].lanes);
        }
        std::vector<u32> arterials[2];
        for (u32 axis = 0; axis < 2; ++axis)
        {
            arterials[axis] = arterialLines(seed, axis, r0, r1);
        }
        for (u32 axis = 0; axis < 2; ++axis)
        {
            const std::vector<u32>& lines = arterials[axis];
            for (sizet i = 0; i < lines.size(); ++i)
            {
                 
                bool boulevard = false;
                for (const u32 mark : {size / 2, size / 4, size * 3 / 4})
                {
                    if (mark != size / 2 && lines.size() < 5)
                    {
                        continue;
                    }
                    const auto nearest = std::ranges::min_element(lines, {}, [mark](const u32 l)
                                                                  { return l > mark ? l - mark : mark - l; });
                    boulevard = boulevard || *nearest == lines[i];
                }
                const u32 cls = boulevard ? ROAD_BOULEVARD : ROAD_AVENUE;
                cells.set(axis, lines[i], r0, r1, cls, ROAD_CLASS_SPECS[cls].lanes);
            }
        }
        for (u32 axis = 0; axis < 2; ++axis)
        {
            const std::vector<u32>& across = arterials[1 - axis];
            if (across.size() < 2)
            {
                continue;
            }
            std::vector<u32> bounds{r0};
            bounds.insert(bounds.end(), arterials[axis].begin(), arterials[axis].end());
            bounds.push_back(r1);
            u64 rng = mix(seed ^ STREET_SALT ^ (axis + 7));
            for (sizet i = 0; i + 1 < bounds.size(); ++i)
            {
                const u32 gap = bounds[i + 1] - bounds[i];
                if (gap < 14)
                {
                    continue;
                }
                const u32 line = bounds[i] + gap / 2 - 1 + static_cast<u32>(nextRandom(rng) % 3);
                cells.set(axis, line, across.front(), across.back(), ROAD_STREET, 1);
            }
        }
         
        {
            const std::vector<DesignEdge> grid = edgesOf(cells);
            const std::shared_ptr<const RoadGraph> graph = buildGraph(size, grid);
            std::vector<u32> degree(graph->nodes.size());
            for (u32 node = 0; node < graph->nodes.size(); ++node)
            {
                degree[node] = graph->nodes[node].degree;
            }
            for (u32 id = 0; id < graph->edges.size(); ++id)
            {
                const GraphEdge& edge = graph->edges[id];
                if (edge.roadClass != ROAD_STREET || degree[edge.a] < 3 || degree[edge.b] < 3 ||
                    draw(seed ^ STREET_SALT, id, 1) >= 0.08f)
                {
                    continue;
                }
                --degree[edge.a];
                --degree[edge.b];
                cells.clear(edge.axis, edge.line, edge.lo, edge.hi);
            }
        }
        design.edges = edgesOf(cells);
        fillLots(design, [](const DesignEdge&) { return true; });
        return design;
    }

     

    inline std::string removeLotsWhere(CityDesign& design, const auto& doomed)
    {
        std::vector<u32> gone;
        std::erase_if(design.lots,
                      [&](const DesignLot& lot)
                      {
                          if (doomed(lot))
                          {
                              gone.push_back(lot.id);
                              return true;
                          }
                          return false;
                      });
        std::erase_if(design.buildings,
                      [&](const DesignBuilding& b) { return std::ranges::find(gone, b.lot) != gone.end(); });
        return {};
    }

     
     
    inline std::string buildRoad(CityDesign& design, u32 x0, u32 z0, u32 x1, u32 z1, const u32 roadClass)
    {
        if (x0 > x1)
        {
            std::swap(x0, x1);
        }
        if (z0 > z1)
        {
            std::swap(z0, z1);
        }
        if ((x0 == x1) == (z0 == z1))
        {
            return "the road must run along x or along z and have a length";
        }
        if (x1 > design.size || z1 > design.size)
        {
            return std::format("the road must stay within the {} x {} lattice", design.size, design.size);
        }
        if (roadClass >= ROAD_CLASSES)
        {
            return "class must be street, avenue, boulevard or highway";
        }
        const DesignEdge road{x0, z0, x1, z1, roadClass, ROAD_CLASS_SPECS[roadClass].lanes};
        RoadCells cells = cellsOf(design.size, design.edges);
        cells.add(road);
        design.edges = edgesOf(cells);
        const u32 c = ROAD_CLASS_SPECS[roadClass].cells;
        const auto lo = [c](const u32 v) { return v > c ? v - c : 0u; };
        const u32 rx0 = lo(x0);
        const u32 rz0 = lo(z0);
        const u32 rx1 = x1 + c;
        const u32 rz1 = z1 + c;
        removeLotsWhere(design, [&](const DesignLot& lot)
                        { return lot.x0 < rx1 && rx0 < lot.x1 && lot.z0 < rz1 && rz0 < lot.z1; });
        fillLots(design,
                 [&](const DesignEdge& edge)
                 {
                     return edge.axis() == road.axis() && edge.line() == road.line() && edge.lo() < road.hi() &&
                         road.lo() < edge.hi();
                 });
        return {};
    }

     
    inline std::string removeRoad(CityDesign& design, const u32 x, const u32 z, DesignEdge* removed = nullptr)
    {
        const std::shared_ptr<const RoadGraph> graph = buildGraph(design.size, design.edges);
        const RoadAt at = roadAt(*graph, x, z);
        if (at.node != NO_ID)
        {
            return std::format("({}, {}) is a junction: name a point on the edge to remove", x, z);
        }
        if (at.edge == NO_ID)
        {
            return std::format("no road at ({}, {})", x, z);
        }
        const GraphEdge& edge = graph->edges[at.edge];
        if (removed)
        {
            const GraphNode& a = graph->nodes[edge.a];
            const GraphNode& b = graph->nodes[edge.b];
            *removed = {a.x, a.z, b.x, b.z, edge.roadClass, edge.lanes};
        }
        removeLotsWhere(design, [&](const DesignLot& lot) { return lotEdge(*graph, lot) == at.edge; });
        RoadCells cells = cellsOf(design.size, design.edges);
        cells.clear(edge.axis, edge.line, edge.lo, edge.hi);
        design.edges = edgesOf(cells);
        return {};
    }

     
    inline std::string setRoadLanes(CityDesign& design, const u32 x, const u32 z, const u32 lanes, u32& previous)
    {
        const std::shared_ptr<const RoadGraph> graph = buildGraph(design.size, design.edges);
        const RoadAt at = roadAt(*graph, x, z);
        if (at.edge == NO_ID)
        {
            return std::format("no road edge at ({}, {})", x, z);
        }
        const GraphEdge& edge = graph->edges[at.edge];
        previous = edge.lanes;
        RoadCells cells = cellsOf(design.size, design.edges);
        cells.set(edge.axis, edge.line, edge.lo, edge.hi, edge.roadClass, lanes);
        design.edges = edgesOf(cells);
        return {};
    }

    inline bool centreIn(const DesignLot& lot, const u32 x0, const u32 z0, const u32 x1, const u32 z1)
    {
        return lot.centre2X() >= 2 * x0 && lot.centre2X() < 2 * x1 && lot.centre2Z() >= 2 * z0 &&
            lot.centre2Z() < 2 * z1;
    }

    inline bool overlaps(const DesignLot& lot, const u32 x0, const u32 z0, const u32 x1, const u32 z1)
    {
        return lot.x0 < x1 && x0 < lot.x1 && lot.z0 < z1 && z0 < lot.z1;
    }

     
     
     
     
    inline u32 zoneRect(CityDesign& design, const u32 x0, const u32 z0, const u32 x1, const u32 z1, const u32 zone,
                        const u32 special = SPECIAL_NONE)
    {
        design.overrides.push_back({x0, z0, x1, z1, zone, special});
        u32 changed = 0;
        for (DesignLot& lot : design.lots)
        {
            if (!overlaps(lot, x0, z0, x1, z1))
            {
                continue;
            }
            const auto building = std::ranges::find(design.buildings, lot.id, &DesignBuilding::lot);
            if (building != design.buildings.end() && lot.zone == zone &&
                (special == SPECIAL_NONE || lot.special == special))
            {
                continue;
            }
            lot.zone = zone;
            lot.special = special != SPECIAL_NONE ? special : specialOf(design.seed, lot.id, zone);
            if (building != design.buildings.end())
            {
                *building = buildingFor(design.seed, design.size, lot);
            }
            else
            {
                design.buildings.push_back(buildingFor(design.seed, design.size, lot));
            }
            ++changed;
        }
        return changed;
    }

     
    inline u32 bulldoze(CityDesign& design, const u32 x0, const u32 z0, const u32 x1, const u32 z1)
    {
        std::vector<u32> lots;
        for (const DesignLot& lot : design.lots)
        {
            if (overlaps(lot, x0, z0, x1, z1))
            {
                lots.push_back(lot.id);
            }
        }
        return static_cast<u32>(std::erase_if(design.buildings, [&](const DesignBuilding& b)
                                              { return std::ranges::find(lots, b.lot) != lots.end(); }));
    }

     

    struct LotRef
    {
        u32 id = 0;
        u32 zone = 0;
        u32 edge = NO_ID;  
        int side = 1;
        u32 cx = 0;  
        u32 cz = 0;
        u32 special = SPECIAL_NONE;
        u32 x0 = 0;  
        u32 z0 = 0;
        u32 x1 = 0;
        u32 z1 = 0;
        float jobs = 1.0f;  
    };

     
     
     
     
    inline float zoneJobs(const u32 zone)
    {
        constexpr float JOBS[ZONE_COUNT] = {0.2f, 0.4f, 1.8f, 16.0f, 0.6f, 0.6f};
        return JOBS[std::min(zone, ZONE_COUNT - 1)];
    }

    class CityMap
    {
    public:
        static constexpr u32 BUCKET = 32;  

        CityMap(const u64 seed, const u32 size, std::shared_ptr<const RoadGraph> roads,
                const std::span<const DesignLot> lots) :
            m_seed(seed), m_size(size), m_graph(std::move(roads)), m_buckets((size + BUCKET - 1) / BUCKET)
        {
            m_lots.reserve(lots.size());
            for (const DesignLot& lot : lots)
            {
                m_lots.push_back(
                    {.id = lot.id,
                     .zone = lot.zone,
                     .edge = lotEdge(*m_graph, lot),
                     .side = lot.side(),
                     .cx = lot.centre2X() / 2,
                     .cz = lot.centre2Z() / 2,
                     .special = lot.special,
                     .x0 = lot.x0,
                     .z0 = lot.z0,
                     .x1 = lot.x1,
                     .z1 = lot.z1,
                     .jobs = static_cast<float>((lot.x1 - lot.x0) * (lot.z1 - lot.z0)) * zoneJobs(lot.zone)});
            }
            std::ranges::sort(m_lots, {}, &LotRef::id);
            for (u32 category = 0; category < CATEGORIES; ++category)
            {
                m_bucketLots[category].resize(static_cast<sizet>(m_buckets) * m_buckets);
            }
            for (u32 index = 0; index < m_lots.size(); ++index)
            {
                const LotRef& lot = m_lots[index];
                if (lot.edge == NO_ID)
                {
                    continue;
                }
                for (u32 category = 0; category < CATEGORIES; ++category)
                {
                    if (inCategory(lot.zone, category))
                    {
                        m_category[category].push_back(index);
                        auto& weights = m_categoryWeights[category];
                        weights.push_back((weights.empty() ? 0.0 : weights.back()) + std::max(lot.jobs, 1.0f));
                        m_bucketLots[category][bucketOf(lot.cx, lot.cz)].push_back(index);
                    }
                }
                if (lot.special < SPECIAL_KINDS)
                {
                    m_special[lot.special].push_back(lot.id);
                }
            }
        }

         
        const std::vector<u32>& specialLots(const u32 special) const
        {
            return m_special[std::min(special, SPECIAL_KINDS - 1)];
        }

        u64 seed() const { return m_seed; }
        u32 size() const { return m_size; }
        const RoadGraph& graph() const { return *m_graph; }
        const std::shared_ptr<const RoadGraph>& graphPtr() const { return m_graph; }
        const std::vector<LotRef>& lots() const { return m_lots; }
        u32 count(const u32 category) const { return static_cast<u32>(m_category[category].size()); }

        const LotRef* lot(const u32 id) const
        {
            const auto at = std::ranges::lower_bound(m_lots, id, {}, &LotRef::id);
            return at != m_lots.end() && at->id == id ? &*at : nullptr;
        }
         
        const LotRef* reachable(const u32 id) const
        {
            const LotRef* found = lot(id);
            return found && found->edge != NO_ID ? found : nullptr;
        }

         
        u32 pick(const u32 category, const u64 key) const
        {
            const std::vector<u32>& list = m_category[category];
            return list.empty() ? NO_ID : m_lots[list[mix(key) % list.size()]].id;
        }

         
         
        u32 pickWeighted(const u32 category, const u64 key) const
        {
            const auto& weights = m_categoryWeights[category];
            if (weights.empty())
                return NO_ID;
            const double share = double(mix(key) >> 11) * (1.0 / 9007199254740992.0);
            const auto at = std::upper_bound(weights.begin(), weights.end(), share * weights.back());
            const sizet index = std::min<sizet>(at - weights.begin(), weights.size() - 1);
            return m_lots[m_category[category][index]].id;
        }

         
         
        std::optional<u32> findNear(const u32 category, const u32 cx, const u32 cz, const u32 radius, const u64 key,
                                    const bool weighted = false) const
        {
            const u32 b0x = (cx > radius ? cx - radius : 0) / BUCKET;
            const u32 b0z = (cz > radius ? cz - radius : 0) / BUCKET;
            const u32 b1x = std::min((cx + radius) / BUCKET, m_buckets - 1);
            const u32 b1z = std::min((cz + radius) / BUCKET, m_buckets - 1);
            std::optional<u32> best;
            u64 bestHash = 0;
            float bestScore = 0.0f;
            for (u32 bz = b0z; bz <= b1z; ++bz)
            {
                for (u32 bx = b0x; bx <= b1x; ++bx)
                {
                    for (const u32 index : m_bucketLots[category][static_cast<sizet>(bz) * m_buckets + bx])
                    {
                        const LotRef& lot = m_lots[index];
                        const u32 d =
                            (lot.cx > cx ? lot.cx - cx : cx - lot.cx) + (lot.cz > cz ? lot.cz - cz : cz - lot.cz);
                        if (d > radius)
                        {
                            continue;
                        }
                         
                         
                        const u64 hash = mix(key ^ (static_cast<u64>(lot.id) * 0x9E3779B97F4A7C15ull));
                        if (weighted)
                        {
                            const float score =
                                -std::log((static_cast<float>(hash >> 40) + 0.5f) * (1.0f / 16777216.0f)) /
                                std::max(lot.jobs, 1.0f);
                            if (!best || score < bestScore)
                            {
                                best = lot.id;
                                bestScore = score;
                            }
                        }
                        else if (!best || hash < bestHash)
                        {
                            best = lot.id;
                            bestHash = hash;
                        }
                    }
                }
            }
            return best;
        }

        u32 pickNear(const u32 category, const u32 cx, const u32 cz, const u32 radius, const u64 key,
                     const bool weighted = false) const
        {
            const std::optional<u32> nearby = findNear(category, cx, cz, radius, key, weighted);
            return nearby ? *nearby : pick(category, key);
        }

         
        u32 nearest(const u32 category, const u32 cx, const u32 cz) const
        {
            u32 best = NO_ID;
            u32 bestDistance = 0xFFFFFFFFu;
            for (const u32 index : m_category[category])
            {
                const LotRef& lot = m_lots[index];
                const u32 d = (lot.cx > cx ? lot.cx - cx : cx - lot.cx) + (lot.cz > cz ? lot.cz - cz : cz - lot.cz);
                if (d < bestDistance)
                {
                    best = lot.id;
                    bestDistance = d;
                }
            }
            return best;
        }

         
        u32 accessNode(const LotRef& lot, const u32 choice) const
        {
            const GraphEdge& edge = m_graph->edges[lot.edge];
            return choice == 0 ? edge.a : edge.b;
        }

         
        u32 accessCorner(const LotRef& lot, const u32 node) const
        {
            return cornerBetween(m_graph->headingAlong(lot.edge, node),
                                 lotSideHeading(m_graph->edges[lot.edge].axis, lot.side));
        }

    private:
        sizet bucketOf(const u32 cx, const u32 cz) const
        {
            return static_cast<sizet>(std::min(cz / BUCKET, m_buckets - 1)) * m_buckets +
                std::min(cx / BUCKET, m_buckets - 1);
        }

        u64 m_seed = 0;
        u32 m_size = 0;
        std::shared_ptr<const RoadGraph> m_graph;
        u32 m_buckets = 1;
        std::vector<LotRef> m_lots;
        std::vector<u32> m_category[CATEGORIES];
        std::vector<double> m_categoryWeights[CATEGORIES];
        std::vector<std::vector<u32>> m_bucketLots[CATEGORIES];
        std::vector<u32> m_special[SPECIAL_KINDS];
    };
}  
