 
 
#pragma once
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "Core.hpp"
#include "PieceTable.hpp"

namespace warzone
{
    enum class Kind : uint8_t
    {
        House,
        Apartment,
        Tower,
        Factory,
        Yard,
        Row,        
        Shop,
        Garage,
        Shed,       
        Kiosk,
        Warehouse,  
        Office,
        Hall,       
        School,     
        Station,    
        Barn,       
        Count
    };
    constexpr int KIND_COUNT = static_cast<int>(Kind::Count);
    inline const char* kindName(Kind k)
    {
        static const char* names[KIND_COUNT] = {"House",  "Apartment", "Tower",  "Factory", "Yard",   "Row House", "Shop", "Garage",
                                                "Shed",   "Kiosk",     "Warehouse", "Office", "Hall",  "School",    "Station", "Barn"};
        return names[static_cast<int>(k)];
    }

     
    enum class Zone : uint8_t
    {
        City,        
        Suburb,      
        Industrial,  
        Village      
    };
    struct Block
    {
        float x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        Zone zone = Zone::City;
        uint8_t city = 0;
    };
    struct CityInfo
    {
        std::string name;
        float cx = 200.0f, cz = -150.0f, radius = 260.0f;
        int maxTowers = 3;
        int density = 0;  
    };

     
    struct PieceSpec
    {
        uint16_t type = 0;
        uint16_t building = 0;
        uint16_t part = 0;  
        uint8_t floor = 0;
        uint8_t yaw = 0;
        V3 pos;
        V3 scale{1, 1, 1};
    };

    struct BuildingSpec
    {
        uint16_t id = 0;
        Kind kind = Kind::House;
        uint8_t floors = 0;
        uint16_t parts = 0;
        uint32_t first = 0, count = 0;  
        uint8_t city = 0;
        Box bounds;
        std::string name;
    };

    struct Town
    {
        uint32_t seed = 0;
        std::vector<BuildingSpec> buildings;
        std::vector<PieceSpec> pieces;
    };

    inline V3 pieceSize(const PieceSpec& p)
    {
        const auto& t = PIECES[p.type];
        V3 s{t.sx * p.scale.x, t.sy * p.scale.y, t.sz * p.scale.z};
        if (p.yaw & 1)
            std::swap(s.x, s.z);
        return s;
    }
    inline Box pieceBox(const PieceSpec& p)
    {
        const V3 s = pieceSize(p);
        return {{p.pos.x - s.x * 0.5f, p.pos.y, p.pos.z - s.z * 0.5f}, {p.pos.x + s.x * 0.5f, p.pos.y + s.y, p.pos.z + s.z * 0.5f}};
    }

    constexpr float BAY = 4.0f;
    constexpr float WALL_H = 3.2f;
    constexpr float SLAB_T = 0.3f;
    constexpr float STOREY = WALL_H + SLAB_T;
    constexpr uint32_t GENERATOR_VERSION = 2;

    class TownBuilder
    {
    public:
        TownBuilder(Town& town, Rng& rng) : m_town(town), m_rng(rng) {}

        void city(uint8_t c, const std::string& prefix)
        {
            m_city = c;
            m_prefix = prefix;
        }
        BuildingSpec& begin(Kind kind, int floors)
        {
            BuildingSpec b;
            b.city = m_city;
            b.id = static_cast<uint16_t>(m_town.buildings.size());
            b.kind = kind;
            b.floors = static_cast<uint8_t>(floors);
            b.first = static_cast<uint32_t>(m_town.pieces.size());
            int& n = m_kindCount[m_city][static_cast<int>(kind)];
            char name[64];
            std::snprintf(name, sizeof(name), "%s%s%s %02d", m_prefix.c_str(), m_prefix.empty() ? "" : " ", kindName(kind), ++n);
            b.name = name;
            m_town.buildings.push_back(b);
            m_part = 0;
            return m_town.buildings.back();
        }
        void end()
        {
            auto& b = m_town.buildings.back();
            b.count = static_cast<uint32_t>(m_town.pieces.size()) - b.first;
            Box box{{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
            uint16_t parts = 0;
            for (uint32_t i = b.first; i < b.first + b.count; ++i)
            {
                const Box pb = pieceBox(m_town.pieces[i]);
                box.lo = {std::min(box.lo.x, pb.lo.x), std::min(box.lo.y, pb.lo.y), std::min(box.lo.z, pb.lo.z)};
                box.hi = {std::max(box.hi.x, pb.hi.x), std::max(box.hi.y, pb.hi.y), std::max(box.hi.z, pb.hi.z)};
                parts = std::max<uint16_t>(parts, static_cast<uint16_t>(m_town.pieces[i].part + 1));
            }
            b.bounds = box;
            b.parts = parts;
            if (b.count == 0)
                m_town.buildings.pop_back();
        }
        void part(uint16_t p) { m_part = p; }
        void add(uint16_t type, V3 pos, uint8_t yaw, uint8_t floor, V3 scale = {1, 1, 1})
        {
            PieceSpec s;
            s.type = type;
            s.building = m_town.buildings.back().id;
            s.part = m_part;
            s.floor = floor;
            s.yaw = yaw;
            s.pos = pos;
            s.scale = scale;
            m_town.pieces.push_back(s);
        }

        struct FrameParams
        {
            float x0, z0, y0;
            int nx, nz, floors;
            uint16_t wall;
            float windows;     
            int shopSide;      
            int doorEdge;      
            bool waterTank, antenna;
            float interior;    
            uint8_t shopMask = 0;  
            bool stairs = true;
            uint8_t roof = 0;      
        };

         
        void frame(const FrameParams& p)
        {
            const float x1 = p.x0 + p.nx * BAY, z1 = p.z0 + p.nz * BAY;
            part(0);
            for (int i = 0; i < p.nx; ++i)
                for (int j = 0; j < p.nz; ++j)
                    add(P_SLAB, {p.x0 + i * BAY + BAY * 0.5f, p.y0, p.z0 + j * BAY + BAY * 0.5f}, 0, 0);
            const int doorBay = m_rng.irange(0, (p.doorEdge < 2 ? p.nx : p.nz) - 1);
            for (int f = 0; f < p.floors; ++f)
            {
                const float base = p.y0 + SLAB_T + f * STOREY;
                const uint8_t fl = static_cast<uint8_t>(f);
                part(static_cast<uint16_t>(1 + 3 * f));
                for (int i = 0; i <= p.nx; ++i)
                    for (int j = 0; j <= p.nz; ++j)
                        add(P_COLUMN, {p.x0 + i * BAY, base, p.z0 + j * BAY}, 0, fl);
                if (p.stairs)
                    add(P_STAIRCASE, {p.x0 + (p.nx - 1) * BAY + BAY * 0.5f, base, p.z0 + (p.nz - 1) * BAY + BAY * 0.5f},
                        static_cast<uint8_t>((f & 1) * 2), fl);
                part(static_cast<uint16_t>(2 + 3 * f));
                for (int edge = 0; edge < 4; ++edge)
                {
                    const int bays = edge < 2 ? p.nx : p.nz;
                    for (int k = 0; k < bays; ++k)
                    {
                        V3 c;
                        uint8_t yaw;
                        if (edge < 2)
                        {
                            c = {p.x0 + k * BAY + BAY * 0.5f, base, edge == 0 ? p.z0 : z1};
                            yaw = edge == 0 ? 0 : 2;
                        }
                        else
                        {
                            c = {edge == 2 ? p.x0 : x1, base, p.z0 + k * BAY + BAY * 0.5f};
                            yaw = edge == 2 ? 1 : 3;
                        }
                        if (f == 0 && edge == p.doorEdge && k == doorBay)
                        {
                            add(P_BEAM, {c.x, base + WALL_H - PIECES[P_BEAM].sy, c.z}, yaw, fl);
                            continue;
                        }
                        if (f == 0 && (edge == p.shopSide || ((p.shopMask >> edge) & 1)))
                        {
                            add(P_SHUTTER, c, yaw, fl);
                            continue;
                        }
                        if (m_rng.chance(p.windows))
                        {
                            add(P_PARAPET, c, yaw, fl);
                            add(P_BEAM, {c.x, base + WALL_H - PIECES[P_BEAM].sy, c.z}, yaw, fl);
                        }
                        else
                            add(p.wall, c, yaw, fl);
                    }
                }
                for (int i = 1; i < p.nx; ++i)
                    for (int j = 0; j < p.nz; ++j)
                        if (m_rng.chance(p.interior))
                            add(P_WALL_MODULAR, {p.x0 + i * BAY, base, p.z0 + j * BAY + BAY * 0.5f}, 1, fl);
                for (int j = 1; j < p.nz; ++j)
                    for (int i = 0; i < p.nx; ++i)
                        if (m_rng.chance(p.interior))
                            add(P_WALL_MODULAR, {p.x0 + i * BAY + BAY * 0.5f, base, p.z0 + j * BAY}, 0, fl);
                part(static_cast<uint16_t>(3 + 3 * f));
                for (int i = 0; i < p.nx; ++i)
                    for (int j = 0; j < p.nz; ++j)
                        add(P_SLAB, {p.x0 + i * BAY + BAY * 0.5f, base + WALL_H, p.z0 + j * BAY + BAY * 0.5f}, 0,
                            static_cast<uint8_t>(f + 1));
            }
             
            const float roof = p.y0 + SLAB_T + p.floors * STOREY;
            const uint8_t rf = static_cast<uint8_t>(p.floors);
            part(static_cast<uint16_t>(1 + 3 * p.floors));
            auto bayCentre = [&](int i, int j) { return V3{p.x0 + i * BAY + BAY * 0.5f, roof, p.z0 + j * BAY + BAY * 0.5f}; };
            if (p.roof == 0)
            {
                for (int i = 0; i < p.nx; ++i)
                {
                    add(P_PARAPET, {p.x0 + i * BAY + BAY * 0.5f, roof, p.z0 + 0.2f}, 0, rf);
                    add(P_PARAPET, {p.x0 + i * BAY + BAY * 0.5f, roof, z1 - 0.2f}, 2, rf);
                }
                for (int j = 0; j < p.nz; ++j)
                {
                    add(P_PARAPET, {p.x0 + 0.2f, roof, p.z0 + j * BAY + BAY * 0.5f}, 1, rf);
                    add(P_PARAPET, {x1 - 0.2f, roof, p.z0 + j * BAY + BAY * 0.5f}, 3, rf);
                }
            }
            else if (p.roof == 2)
            {
                const uint8_t ridge = p.nz >= p.nx ? 0 : 1;
                for (int i = 0; i < p.nx; ++i)
                    for (int j = 0; j < p.nz; ++j)
                        add(P_ROOF, bayCentre(i, j), ridge, rf, {1.0f, 1.0f, BAY / PIECES[P_ROOF].sz});
            }
            if (p.roof != 2)
            {
                if (p.waterTank)
                    add(P_WATER_TANK, bayCentre(m_rng.irange(0, p.nx - 1), 0), 0, rf);
                const int vents = m_rng.irange(0, std::max(1, p.nx * p.nz / 6));
                for (int v = 0; v < vents; ++v)
                    add(P_VENT, bayCentre(m_rng.irange(0, p.nx - 1), m_rng.irange(0, p.nz - 1)) + V3{0.3f, 0, 0},
                        static_cast<uint8_t>(m_rng.irange(0, 1)), rf);
                if (p.antenna)
                    add(P_ANTENNA, bayCentre(p.nx / 2, p.nz / 2), 0, rf);
            }
        }

         
        void factory(float x0, float z0, float y0, int nx, int nz, int tiers = 2, int doorEdge = 0)
        {
            constexpr float B = 6.0f, TIER = 4.0f;
            const V3 slab6{1.5f, 1.0f, 1.5f};
            const float x1 = x0 + nx * B, z1 = z0 + nz * B;
            part(0);
            for (int i = 0; i < nx; ++i)
                for (int j = 0; j < nz; ++j)
                    add(P_SLAB, {x0 + i * B + B * 0.5f, y0, z0 + j * B + B * 0.5f}, 0, 0, slab6);
            const float base = y0 + SLAB_T;
            part(1);
            const V3 tall{1.4f, tiers * TIER / PIECES[P_COLUMN].sy, 1.4f};
            for (int i = 0; i <= nx; ++i)
                for (int j = 0; j <= nz; ++j)
                    add(P_COLUMN, {x0 + i * B, base, z0 + j * B}, 0, 0, tall);
            const int shutterBay = m_rng.irange(0, (doorEdge < 2 ? nx : nz) - 1);
            for (int tier = 0; tier < tiers; ++tier)
            {
                part(static_cast<uint16_t>(2 + tier));
                const float y = base + tier * TIER;
                for (int edge = 0; edge < 4; ++edge)
                {
                    const int bays = edge < 2 ? nx : nz;
                    for (int k = 0; k < bays; ++k)
                    {
                        const bool alongX = edge < 2;
                        const V3 c = alongX ? V3{x0 + k * B + B * 0.5f, y, edge == 0 ? z0 : z1}
                                            : V3{edge == 2 ? x0 : x1, y, z0 + k * B + B * 0.5f};
                        const uint8_t yaw = static_cast<uint8_t>(alongX ? (edge == 0 ? 0 : 2) : (edge == 2 ? 1 : 3));
                        if (tier == 0 && edge == doorEdge && k == shutterBay)
                            add(P_SHUTTER, c, yaw, 0, {1.5f, TIER / PIECES[P_SHUTTER].sy, 1.0f});
                        else
                            add(P_HANGAR_WALL, c, yaw, static_cast<uint8_t>(tier));
                    }
                }
            }
            part(static_cast<uint16_t>(2 + tiers));
            const float roof = base + tiers * TIER;
            for (int i = 0; i < nx; ++i)
                for (int j = 0; j < nz; ++j)
                    add(P_ROOF, {x0 + i * B + B * 0.5f, roof, z0 + j * B + B * 0.5f}, 0, 1, {1.5f, 1.0f, 1.0f});
            part(static_cast<uint16_t>(3 + tiers));
            for (int v = 0; v < (tiers == 2 ? 2 : 1); ++v)
                add(P_VENT, {x0 + m_rng.irange(0, nx - 1) * B + B * 0.5f, roof + PIECES[P_ROOF].sy, z0 + m_rng.irange(0, nz - 1) * B + B * 0.5f},
                    0, 2);
        }

         
        void hut(V3 pos)
        {
            part(0);
            add(P_GABLE_WALL, pos, 0, 0);
        }

         
        void canopy(float x0, float z0, float y0, int nx, int nz, float height)
        {
            const V3 tall{1.0f, height / PIECES[P_COLUMN].sy, 1.0f};
            part(0);
            for (int i = 0; i <= nx; ++i)
                for (int j = 0; j <= nz; ++j)
                    add(P_COLUMN, {x0 + i * BAY, y0, z0 + j * BAY}, 0, 0, tall);
            part(1);
            for (int i = 0; i < nx; ++i)
                for (int j = 0; j < nz; ++j)
                    add(P_SLAB, {x0 + i * BAY + BAY * 0.5f, y0 + height, z0 + j * BAY + BAY * 0.5f}, 0, 1);
        }

         
        void fenceRun(float x, float z, float length, bool alongX, float y0, int gate = -1)
        {
            part(0);
            const int n = static_cast<int>(length / 3.0f);
            for (int k = 0; k < n; ++k)
            {
                const float c = 1.5f + 3.0f * k;
                add(k == gate ? P_GATE : P_FENCE, alongX ? V3{x + c, y0, z} : V3{x, y0, z + c}, alongX ? 0 : 1, 0);
            }
        }
         
        template <typename R>
        void fenceRing(const R& r, float y0)
        {
            const float w = r.x1 - r.x0, d = r.z1 - r.z0;
            const int side = m_rng.irange(0, 3);
            auto gateAt = [&](int s, float len) { return side == s ? m_rng.irange(0, std::max(0, static_cast<int>(len / 3.0f) - 1)) : -1; };
            fenceRun(r.x0, r.z0, w, true, y0, gateAt(0, w));
            fenceRun(r.x0, r.z1, w, true, y0, gateAt(1, w));
            fenceRun(r.x0, r.z0, d, false, y0, gateAt(2, d));
            fenceRun(r.x1, r.z0, d, false, y0, gateAt(3, d));
        }

         
        template <typename R>
        void yard(const R& r, float y0, int count, std::vector<Box>& taken)
        {
            static const uint16_t kinds[] = {P_CONTAINER, P_CONTAINER, P_STORAGE_TANK, P_PIPE_RACK, P_GENERATOR, P_METAL_CRATE,
                                             P_CARGO_CRATE, P_DRUM, P_DRUM, P_SECURITY_BLOCK, P_ROAD_BARRIER, P_GABLE_WALL};
            part(0);
            for (int n = 0; n < count; ++n)
            {
                for (int attempt = 0; attempt < 12; ++attempt)
                {
                    PieceSpec s;
                    s.type = kinds[m_rng.irange(0, static_cast<int>(std::size(kinds)) - 1)];
                    s.yaw = static_cast<uint8_t>(m_rng.irange(0, 3));
                    const V3 sz = pieceSize(s);
                    if (sz.x + 1 > r.x1 - r.x0 || sz.z + 1 > r.z1 - r.z0)
                        continue;
                    s.pos = {m_rng.range(r.x0 + sz.x * 0.5f + 0.5f, r.x1 - sz.x * 0.5f - 0.5f), y0,
                             m_rng.range(r.z0 + sz.z * 0.5f + 0.5f, r.z1 - sz.z * 0.5f - 0.5f)};
                    const Box b = pieceBox(s);
                    bool clash = false;
                    for (const Box& t : taken)
                        clash = clash || overlaps(b, t, 0.6f);
                    if (clash)
                        continue;
                    taken.push_back(b);
                    add(s.type, s.pos, s.yaw, 0);
                    break;
                }
            }
        }

    private:
        Town& m_town;
        Rng& m_rng;
        uint16_t m_part = 0;
        uint8_t m_city = 0;
        std::string m_prefix;
        int m_kindCount[64][KIND_COUNT] = {};
    };

    struct TownOptions
    {
        V3 centre{200.0f, 0.0f, -150.0f};  
        float radius = 260.0f;
        int maxTowers = 3;
    };

     
    struct Rect
    {
        float x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        int level = 0;
        float w() const { return x1 - x0; }
        float d() const { return z1 - z0; }
    };
     
    inline void splitOut(const Rect& r, const Rect& hole, std::vector<Rect>& out, float minSide = 4.6f)
    {
        if (hole.x1 <= r.x0 || hole.x0 >= r.x1 || hole.z1 <= r.z0 || hole.z0 >= r.z1)
        {
            out.push_back(r);
            return;
        }
        auto push = [&](float a0, float b0, float a1, float b1)
        {
            if (a1 - a0 >= minSide && b1 - b0 >= minSide)
                out.push_back({a0, b0, a1, b1, r.level});
        };
        push(r.x0, r.z0, std::min(hole.x0, r.x1), r.z1);
        push(std::max(hole.x1, r.x0), r.z0, r.x1, r.z1);
        const float ax = std::max(r.x0, hole.x0), bx = std::min(r.x1, hole.x1);
        push(ax, r.z0, bx, std::min(hole.z0, r.z1));
        push(ax, std::max(hole.z1, r.z0), bx, r.z1);
    }
    inline void carve(std::vector<Rect>& list, const Rect& hole)
    {
        std::vector<Rect> out;
        for (const Rect& r : list)
            splitOut(r, hole, out);
        list = std::move(out);
    }

     
    struct CityState
    {
        int blocksLeft = 0;
        int hallLimit = 0, schoolLimit = 0, stationLimit = 0;
        int halls = 0, schools = 0, stations = 0;
    };

     
     
     
    template <typename GroundFn>
    class BlockPlanner
    {
    public:
        BlockPlanner(TownBuilder& tb, Rng& rng, GroundFn& ground) : m_tb(tb), m_rng(rng), m_ground(ground) {}

        void run(const Block& blk, Kind kind, int dens, bool core, float dist, CityState& cs)
        {
            m_zone = blk.zone;
            m_dens = std::clamp(dens, 0, 3);
            m_core = core;
            m_dist = dist;
            m_backRows = 0.05f + 0.15f * m_dens;
            m_in = {blk.x0 + 2.0f, blk.z0 + 2.0f, blk.x1 - 2.0f, blk.z1 - 2.0f};
            m_taken.clear();
            --cs.blocksLeft;
            if (m_in.w() < 6.0f || m_in.d() < 6.0f)
                return;
            std::vector<Rect> free{m_in};
            Rect yardR;
            bool yardHalf = false;
            if (m_zone == Zone::Village)
            {
                farm(m_in, cs);
                free.clear();
            }
            else if (!special(free, kind, cs))
            {
                switch (kind)
                {
                case Kind::Tower:
                    tower(free);
                    break;
                case Kind::Apartment:
                    apartment(free);
                    break;
                case Kind::Factory:
                    factory(free);
                    break;
                case Kind::Yard:
                    yardHalf = splitYard(free, yardR);
                    break;
                default:
                    break;
                }
            }
            std::vector<Rect> stack(free.rbegin(), free.rend());
            while (!stack.empty())
            {
                const Rect r = stack.back();
                stack.pop_back();
                fillRect(r, stack);
            }
            if (yardHalf)
                fencedYard(yardR, m_rng.irange(8, 12), m_zone == Zone::Industrial ? 0.6f : 0.25f);
            if (m_zone != Zone::Village)
            {
                const int clutter = kind == Kind::Factory ? m_rng.irange(3, 5) : m_rng.irange(1, 2);
                m_tb.begin(Kind::Yard, 0);
                m_tb.yard(m_in, ground(m_in), clutter, m_taken);
                m_tb.end();
            }
        }

    private:
        TownBuilder& m_tb;
        Rng& m_rng;
        GroundFn& m_ground;
        Zone m_zone = Zone::City;
        int m_dens = 0;
        bool m_core = false;
        float m_dist = 0;
        float m_backRows = 0.6f;  
        Rect m_in;
        std::vector<Box> m_taken;

        float ground(const Rect& r) { return m_ground((r.x0 + r.x1) * 0.5f, (r.z0 + r.z1) * 0.5f); }
        static Rect around(float x0, float z0, float x1, float z1, float g) { return {x0 - g, z0 - g, x1 + g, z1 + g}; }
        void take(float x0, float z0, float x1, float z1, float y0, float height)
        {
            m_taken.push_back({{x0 - 0.6f, y0 - 1.0f, z0 - 0.6f}, {x1 + 0.6f, y0 + height, z1 + 0.6f}});
        }
        TownBuilder::FrameParams params(uint16_t wall, float windows, float interior, int doorEdge)
        {
            TownBuilder::FrameParams p{};
            p.wall = wall, p.windows = windows, p.shopSide = -1, p.doorEdge = doorEdge, p.interior = interior;
            return p;
        }

         
        void frameAt(Kind kind, float x0, float z0, int nx, int nz, int floors, TownBuilder::FrameParams p, std::vector<Rect>* free = nullptr)
        {
            p.x0 = x0, p.z0 = z0, p.nx = nx, p.nz = nz, p.floors = floors;
            p.y0 = m_ground(x0 + nx * BAY * 0.5f, z0 + nz * BAY * 0.5f);
            m_tb.begin(kind, floors);
            m_tb.frame(p);
            m_tb.end();
            take(x0, z0, x0 + nx * BAY, z0 + nz * BAY, p.y0, floors * STOREY + 8.0f);
            if (free)
                carve(*free, around(x0, z0, x0 + nx * BAY, z0 + nz * BAY, 1.2f));
        }

         
        void tower(std::vector<Rect>& free)
        {
            const float w = m_in.w(), h = m_in.d(), cz = (m_in.z0 + m_in.z1) * 0.5f;
            if (m_dens > 0)
            {
                 
                const int shape = m_rng.irange(0, 2);
                const int nx = std::min(shape == 1 ? 3 : 4, static_cast<int>((w - 4.0f) / BAY));
                const int nz = std::min(shape == 2 ? 3 : 4, static_cast<int>((h - 2.0f) / BAY));
                if (nx < 2 || nz < 2)
                    return;
                const int floors = shape == 0 ? m_rng.irange(13, 16) : m_rng.irange(11, 14);
                const bool left = m_rng.chance(0.5f);
                const float x0 = left ? m_in.x0 + 2.0f : m_in.x1 - 2.0f - nx * BAY, z0 = cz - nz * BAY * 0.5f;
                auto p = params(m_rng.chance(0.5f) ? P_WALL_CONCRETE : P_WALL_MODULAR, 0.7f, 0.2f, 0);
                p.antenna = true;
                frameAt(Kind::Tower, x0, z0, nx, nz, floors, p, &free);
                const Rect rest = left ? Rect{x0 + nx * BAY + 3.0f, m_in.z0, m_in.x1, m_in.z1} : Rect{m_in.x0, m_in.z0, x0 - 3.0f, m_in.z1};
                if (rest.w() >= 3 * BAY + 2.0f && h >= 3 * BAY + 2.0f && m_rng.chance(0.3f))
                {
                    const int f2 = m_rng.irange(9, 12);
                    frameAt(Kind::Tower, left ? rest.x1 - 3 * BAY - 1.0f : rest.x0 + 1.0f, cz - 1.5f * BAY, 3, 3, f2,
                            params(P_WALL_MODULAR, 0.65f, 0.25f, 0), &free);
                }
                return;
            }
            if (w < 3 * BAY + 6.0f || h < 3 * BAY + 2.0f)
                return;
            auto p = params(m_rng.chance(0.5f) ? P_WALL_CONCRETE : P_WALL_MODULAR, 0.65f, 0.25f, 0);
            p.antenna = true;
            frameAt(Kind::Tower, m_in.x0 + 2.0f, cz - 1.5f * BAY, 3, 3, m_rng.irange(9, 12), p, &free);
        }

        void apartment(std::vector<Rect>& free)
        {
            const float w = m_in.w(), h = m_in.d(), cx = (m_in.x0 + m_in.x1) * 0.5f, cz = (m_in.z0 + m_in.z1) * 0.5f;
            if (m_core)
            {
                 
                const int maxBays = std::clamp(static_cast<int>((w - 4.0f) / BAY), 3, 8);
                if (h < 3 * BAY + 2.0f || w < 3 * BAY + 4.0f)
                    return;
                const int rows = h >= 32.0f && m_rng.chance(0.35f) ? 2 : 1;
                const float depth = rows * 3 * BAY + (rows - 1) * 6.0f;
                for (int r = 0; r < rows; ++r)
                {
                    const int floors = m_dist < 0.45f ? m_rng.irange(6, 9) : m_rng.irange(4, 6);
                    const int nx = m_rng.irange(std::max(std::min(6, maxBays), maxBays - 3), maxBays);
                    const float x0 = std::clamp(cx - nx * BAY * 0.5f + m_rng.range(-2.0f, 2.0f), m_in.x0 + 1.0f, std::max(m_in.x0 + 1.0f, m_in.x1 - 1.0f - nx * BAY));
                    const float z0 = m_in.z0 + (h - depth) * 0.5f + r * (3 * BAY + 6.0f);
                    auto p = params(P_WALL_CONCRETE, 0.55f, 0.2f, 1);
                    p.shopSide = m_rng.chance(0.6f) ? 0 : -1;
                    p.waterTank = m_rng.chance(0.5f);
                    frameAt(Kind::Apartment, x0, z0, nx, 3, floors, p, &free);
                }
                return;
            }
            if (w < 6 * BAY + 4.0f || h < 3 * BAY + 2.0f)
                return;
            auto p = params(P_WALL_CONCRETE, 0.55f, 0.2f, 1);
            p.shopSide = m_rng.chance(0.6f) ? 0 : -1;
            p.waterTank = m_rng.chance(0.5f);
            frameAt(Kind::Apartment, cx - 6 * BAY * 0.5f + m_rng.range(-3.0f, 3.0f), cz - 3 * BAY * 0.5f, 6, 3, m_rng.irange(4, 6), p, &free);
        }

        void factory(std::vector<Rect>& free)
        {
            int nx = m_dens > 0 ? 5 : 4, nz = m_dens > 0 ? 3 : 2;
            while (nx > 2 && m_in.w() < nx * 6.0f + 4.0f)
                --nx;
            while (nz > 1 && m_in.d() < nz * 6.0f + 4.0f)
                --nz;
            if (m_in.w() < nx * 6.0f + 4.0f || m_in.d() < nz * 6.0f + 4.0f)
                return;
            const float x0 = m_in.x0 + 1.5f, z0 = m_in.z0 + 1.5f;
            const float y0 = m_ground(x0 + nx * 3.0f, z0 + nz * 3.0f);
            m_tb.begin(Kind::Factory, 1);
            m_tb.factory(x0, z0, y0, nx, nz);
            m_tb.end();
            take(x0, z0, x0 + nx * 6.0f, z0 + nz * 6.0f, y0, 12.0f);
            carve(free, around(x0, z0, x0 + nx * 6.0f, z0 + nz * 6.0f, 1.8f));
        }

         
        bool splitYard(std::vector<Rect>& free, Rect& yardR)
        {
            const float w = m_in.w(), h = m_in.d();
            if (w < 16.0f && h < 16.0f)
                return false;
            const float f = m_rng.range(0.42f, 0.58f);
            const bool first = m_rng.chance(0.5f);
            Rect a, b;  
            if (w >= h)
                a = {m_in.x0, m_in.z0, m_in.x0 + w * f - 0.6f, m_in.z1}, b = {m_in.x0 + w * f + 0.6f, m_in.z0, m_in.x1, m_in.z1};
            else
                a = {m_in.x0, m_in.z0, m_in.x1, m_in.z0 + h * f - 0.6f}, b = {m_in.x0, m_in.z0 + h * f + 0.6f, m_in.x1, m_in.z1};
            yardR = first ? a : b;
            free.assign(1, first ? b : a);
            take(yardR.x0, yardR.z0, yardR.x1, yardR.z1, ground(yardR), 4.0f);
            return true;
        }

         
        void fencedYard(const Rect& r, int props, float fenceChance)
        {
            if (r.w() < 6.0f || r.d() < 6.0f)
                return;
            const float y0 = ground(r);
            m_tb.begin(Kind::Yard, 0);
            Rect inner = r;
            if (m_rng.chance(fenceChance) && r.w() >= 9.0f && r.d() >= 9.0f)
            {
                m_tb.fenceRing(Rect{r.x0 + 0.4f, r.z0 + 0.4f, r.x1 - 0.4f, r.z1 - 0.4f}, y0);
                inner = {r.x0 + 1.5f, r.z0 + 1.5f, r.x1 - 1.5f, r.z1 - 1.5f};
            }
            std::vector<Box> none;
            m_tb.yard(inner, y0, props, none);
            m_tb.end();
        }

         
        bool want(int placed, int limit, const CityState& cs)
        {
            return placed < limit && m_rng.chance(static_cast<float>(limit - placed) / static_cast<float>(std::max(1, cs.blocksLeft + 1)));
        }
        bool special(std::vector<Rect>& free, Kind kind, CityState& cs)
        {
            if (kind != Kind::House && kind != Kind::Yard)  
                return false;
            const float w = m_in.w(), h = m_in.d();
            const bool town = m_zone == Zone::City || m_zone == Zone::Suburb;
            if (town && w >= 38.0f && h >= 26.0f && want(cs.schools, cs.schoolLimit, cs))
            {
                ++cs.schools;
                school(free);
                return true;
            }
            if (town && w >= 22.0f && h >= 24.0f && want(cs.halls, cs.hallLimit, cs))
            {
                ++cs.halls;
                church(free);
                return true;
            }
            if (w >= 26.0f && h >= 22.0f && want(cs.stations, cs.stationLimit, cs))
            {
                ++cs.stations;
                station(free);
                return true;
            }
            if (town && kind == Kind::House && w >= 36.0f && h >= 26.0f && m_rng.chance(0.04f + 0.03f * m_dens))
            {
                courtyard(free);
                return true;
            }
            return false;
        }

         
        void church(std::vector<Rect>& free)
        {
            const int nx = m_rng.chance(0.5f) ? 2 : 3, nz = m_rng.irange(3, 4);
            const float total = nz * BAY + 0.6f + BAY;
            const float x0 = m_rng.range(m_in.x0 + 1.5f, std::max(m_in.x0 + 1.5f, m_in.x1 - 1.5f - nx * BAY));
            const bool north = m_rng.chance(0.5f);
            const float z0 = north ? m_in.z0 + 1.5f : m_in.z1 - 1.5f - total;
            const float naveZ = north ? z0 + BAY + 0.6f : z0, towerZ = north ? z0 : z0 + nz * BAY + 0.6f;
            auto nave = params(P_WALL_CONCRETE, 0.6f, 0.0f, north ? 0 : 1);
            nave.stairs = false;
            nave.roof = 2;
            frameAt(Kind::Hall, x0, naveZ, nx, nz, 2, nave, &free);
            frameAt(Kind::Hall, x0 + (nx / 2) * BAY, towerZ, 1, 1, m_rng.irange(4, 5), params(P_WALL_CONCRETE, 0.35f, 0.0f, north ? 0 : 1), &free);
        }

         
        void station(std::vector<Rect>& free)
        {
            const bool north = m_rng.chance(0.5f), west = m_rng.chance(0.5f);
            const float x0 = west ? m_in.x0 + 1.5f : m_in.x1 - 1.5f - 2 * BAY, z0 = north ? m_in.z0 + 1.5f : m_in.z1 - 1.5f - 2 * BAY;
            const float y0 = m_ground(x0 + BAY, z0 + BAY);
            m_tb.begin(Kind::Station, 0);
            m_tb.canopy(x0, z0, y0, 2, 2, 4.6f);
            m_tb.part(0);
            m_tb.add(P_GENERATOR, {x0 + 2.0f, y0, z0 + 4.0f}, 0, 0);
            m_tb.add(P_GENERATOR, {x0 + 6.0f, y0, z0 + 4.0f}, 0, 0);
            m_tb.end();
            take(x0, z0, x0 + 2 * BAY, z0 + 2 * BAY, y0, 6.0f);
            carve(free, around(x0, z0, x0 + 2 * BAY, z0 + 2 * BAY, 1.2f));
            auto shop = params(P_WALL_MODULAR, 0.4f, 0.0f, -1);
            shop.shopMask = north ? 1 : 2;  
            shop.stairs = false;
            const float sz = north ? z0 + 2 * BAY + 3.0f : z0 - 3.0f - BAY;
            if (sz > m_in.z0 && sz + BAY < m_in.z1)
                frameAt(Kind::Kiosk, x0, sz, 2, 1, 1, shop, &free);
        }

         
        void courtyard(std::vector<Rect>& free)
        {
            const float w = m_in.w(), h = m_in.d();
            const int depth = h >= 40.0f ? 3 : 2;
            const int floors = m_rng.irange(4, 6) + (m_core && m_rng.chance(0.4f) ? 1 : 0) + (m_dens >= 3 ? 1 : 0);
            const bool u = m_rng.chance(0.5f), flipZ = m_rng.chance(0.5f);
            const int nxN = std::min(12, static_cast<int>((w - 1.2f) / BAY));
            const int nzW = static_cast<int>((h - depth * BAY - 2.4f) / BAY);
            if (nxN < 6 || nzW < 2)
                return;
            const float xN = m_in.x0 + (w - nxN * BAY) * 0.5f;
            const float zN = flipZ ? m_in.z1 - 0.6f - depth * BAY : m_in.z0 + 0.6f;
            auto north = params(P_WALL_CONCRETE, 0.55f, 0.2f, flipZ ? 1 : 0);
            north.shopSide = m_rng.chance(0.5f) ? (flipZ ? 1 : 0) : -1;
            frameAt(Kind::Apartment, xN, zN, nxN, depth, floors, north, &free);
            const float zW = flipZ ? zN - 0.6f - nzW * BAY : zN + depth * BAY + 0.6f;
            auto wing = params(P_WALL_CONCRETE, 0.55f, 0.2f, 2);
            frameAt(Kind::Apartment, xN, zW, 2, nzW, floors, wing, &free);
            Rect court{xN + 2 * BAY + 0.6f, zW, xN + nxN * BAY, zW + nzW * BAY};
            if (u)
            {
                wing.doorEdge = 3;
                frameAt(Kind::Apartment, xN + (nxN - 2) * BAY, zW, 2, nzW, floors, wing, &free);
                court.x1 = xN + (nxN - 2) * BAY - 0.6f;
            }
            else
                court.x1 = std::min(court.x1, court.x0 + 14.0f);  
            if (court.w() >= 6.0f && court.d() >= 6.0f)
            {
                carve(free, court);
                fencedYard(court, 2, 0.0f);
            }
        }

         
        void school(std::vector<Rect>& free)
        {
            const float w = m_in.w(), h = m_in.d();
            const bool flipX = m_rng.chance(0.5f), flipZ = m_rng.chance(0.5f);
            const int nxA = std::min(10, static_cast<int>((w - 1.2f) / BAY)), nzB = std::min(5, static_cast<int>((h - 2 * BAY - 3.0f) / BAY));
            if (nxA < 6 || nzB < 2)
                return;
            const int floors = m_rng.irange(2, 3) + (m_dens >= 3 ? 1 : 0);
            const float xA = flipX ? m_in.x1 - 0.6f - nxA * BAY : m_in.x0 + 0.6f;
            const float zA = flipZ ? m_in.z1 - 0.6f - 2 * BAY : m_in.z0 + 0.6f;
            frameAt(Kind::School, xA, zA, nxA, 2, floors, params(P_WALL_CONCRETE, 0.7f, 0.15f, flipZ ? 1 : 0), &free);
            const float zB = flipZ ? zA - 0.6f - nzB * BAY : zA + 2 * BAY + 0.6f;
            const float xB = flipX ? xA + (nxA - 2) * BAY : xA;
            frameAt(Kind::School, xB, zB, 2, nzB, floors, params(P_WALL_CONCRETE, 0.7f, 0.15f, flipX ? 3 : 2), &free);
            Rect yard{flipX ? xA : xA + 2 * BAY + 0.6f, zB, flipX ? xB - 0.6f : xA + nxA * BAY, zB + nzB * BAY};
            if (yard.w() >= 9.0f && yard.d() >= 9.0f)
            {
                carve(free, yard);
                fencedYard(yard, 3, 1.0f);
            }
        }

         
        void farm(const Rect& r, CityState& cs)
        {
            const int cols = r.w() >= 44.0f ? 2 : 1, rows = r.d() >= 34.0f ? 2 : 1;
            int roles[4] = {0, 1, 2, 3};  
            for (int i = 3; i > 0; --i)
                std::swap(roles[i], roles[m_rng.irange(0, i)]);
            const bool hasChurch = r.w() / cols >= 17.0f && r.d() / rows >= 26.0f && want(cs.halls, cs.hallLimit, cs);
            if (hasChurch)
                ++cs.halls;
            int cell = 0;
            for (int j = 0; j < rows; ++j)
                for (int i = 0; i < cols; ++i, ++cell)
                {
                    const float cw = r.w() / cols, cd = r.d() / rows;
                    const Rect c{r.x0 + i * cw + 0.4f, r.z0 + j * cd + 0.4f, r.x0 + (i + 1) * cw - 0.4f, r.z0 + (j + 1) * cd - 0.4f};
                    const int role = cols * rows == 1 ? 0 : roles[cell];
                    std::vector<Rect> free{c};
                    if (hasChurch && cell == 0)
                    {
                        const Rect keep = m_in;
                        m_in = c;
                        church(free);
                        m_in = keep;
                    }
                    else if (role == 0 || (role == 3 && m_rng.chance(0.6f)))
                        farmhouse(c, free, role == 0);
                    else if (role == 1)
                        barn(c, free);
                    else
                    {
                        fencedYard(c, m_rng.irange(3, 6), 1.0f);
                        continue;
                    }
                    sheds(free, m_rng.irange(1, 2));
                }
        }
        void farmhouse(const Rect& c, std::vector<Rect>& free, bool big)
        {
            const int nx = std::min(static_cast<int>((c.w() - 3.0f) / BAY), m_rng.irange(2, big ? 3 : 2));
            const int nz = std::min(static_cast<int>((c.d() - 3.0f) / BAY), m_rng.irange(2, big ? 3 : 2));
            if (nx < 1 || nz < 1)
                return;
            const float x0 = m_rng.range(c.x0 + 1.0f, std::max(c.x0 + 1.0f, c.x1 - 1.0f - nx * BAY));
            const float z0 = m_rng.range(c.z0 + 1.0f, std::max(c.z0 + 1.0f, c.z1 - 1.0f - nz * BAY));
            auto p = params(P_WALL_MODULAR, 0.4f, 0.25f, m_rng.irange(0, 3));
            p.roof = m_rng.chance(0.75f) ? 2 : 0;
            p.waterTank = p.roof == 0 && m_rng.chance(0.4f);
            frameAt(Kind::House, x0, z0, nx, nz, 2, p, &free);
        }
        void barn(const Rect& c, std::vector<Rect>& free)
        {
            const bool alongX = c.w() >= c.d();
            const float len = alongX ? c.w() : c.d(), across = alongX ? c.d() : c.w();
            const int n = std::min(static_cast<int>((len - 2.0f) / 6.0f), m_rng.irange(2, 4));
            if (n < 1 || across < 8.0f)
                return;
            const float u0 = (alongX ? c.x0 : c.z0) + m_rng.range(1.0f, std::max(1.0f, len - 1.0f - n * 6.0f));
            const float v0 = (alongX ? c.z0 : c.x0) + m_rng.range(1.0f, std::max(1.0f, across - 7.0f));
            const float y0 = ground(c);
            m_tb.begin(Kind::Barn, 1);
            for (int k = 0; k < n; ++k)
                m_tb.hut(alongX ? V3{u0 + 3.0f + 6.0f * k, y0, v0 + 3.0f} : V3{v0 + 3.0f, y0, u0 + 3.0f + 6.0f * k});
            m_tb.end();
            const Rect box = alongX ? Rect{u0, v0, u0 + n * 6.0f, v0 + 6.0f} : Rect{v0, u0, v0 + 6.0f, u0 + n * 6.0f};
            take(box.x0, box.z0, box.x1, box.z1, y0, 5.0f);
            carve(free, around(box.x0, box.z0, box.x1, box.z1, 1.0f));
        }
         
        void sheds(const std::vector<Rect>& free, int count)
        {
            for (int n = 0; n < count && !free.empty(); ++n)
                for (int attempt = 0; attempt < 8; ++attempt)
                {
                    const Rect& r = free[m_rng.irange(0, static_cast<int>(free.size()) - 1)];
                    const bool hut = m_rng.chance(0.55f);
                    const int gw = m_rng.irange(1, 2), gd = m_rng.irange(1, 2);
                    const float sx = hut ? 6.0f : gw * BAY + 0.6f, sz = hut ? 6.0f : gd * BAY + 0.6f;
                    if (r.w() < sx + 1.0f || r.d() < sz + 1.0f)
                        continue;
                    const float x0 = m_rng.range(r.x0 + 0.5f, r.x1 - 0.5f - sx), z0 = m_rng.range(r.z0 + 0.5f, r.z1 - 0.5f - sz);
                    const Box b{{x0, -1e3f, z0}, {x0 + sx, 1e3f, z0 + sz}};
                    bool clash = false;
                    for (const Box& t : m_taken)
                        clash = clash || overlaps(b, t, 0.3f);
                    if (clash)
                        continue;
                    const float y0 = m_ground(x0 + sx * 0.5f, z0 + sz * 0.5f);
                    if (hut)
                    {
                        m_tb.begin(Kind::Shed, 1);
                        m_tb.hut({x0 + 3.0f, y0, z0 + 3.0f});
                        m_tb.end();
                        take(x0, z0, x0 + 6.0f, z0 + 6.0f, y0, 5.0f);
                    }
                    else
                    {
                        auto p = params(P_WALL_MODULAR, 0.0f, 0.0f, -1);
                        p.stairs = false;
                        p.roof = m_rng.chance(0.5f) ? 1 : 0;
                        p.shopMask = static_cast<uint8_t>(1u << m_rng.irange(0, 3));
                        frameAt(Kind::Garage, x0 + 0.3f, z0 + 0.3f, gw, gd, 1, p);
                    }
                    break;
                }
        }

         
        void fillRect(const Rect& r, std::vector<Rect>& stack)
        {
            if (r.w() < 5.0f || r.d() < 5.0f || r.level > 4)
                return;
            const float db[4] = {r.z0 - m_in.z0, m_in.z1 - r.z1, r.x0 - m_in.x0, m_in.x1 - r.x1};
            const float dmin = std::min({db[0], db[1], db[2], db[3]});
            if (dmin > 7.0f)
            {
                sheds(std::vector<Rect>{r}, m_rng.irange(0, 2));
                return;
            }
            int front = 0;
            float best = -1.0f;
            for (int e = 0; e < 4; ++e)
            {
                if (db[e] > dmin + 1.5f)
                    continue;
                const float along = e < 2 ? r.w() : r.d();
                if (along > best + 0.5f || (std::abs(along - best) <= 0.5f && m_rng.chance(0.5f)))
                    front = e, best = along;
            }
            const bool alongX = front < 2;
            const bool industrial = m_zone == Zone::Industrial;
            const float setbackMax = m_zone == Zone::Suburb ? 2.0f : 0.8f;
            const float across = alongX ? r.d() : r.w();
            const float unit = industrial ? 6.0f : BAY;
            const int maxD = static_cast<int>((across - 1.2f - setbackMax) / unit);
            if (maxD < 1)
                return;
            const int D = std::min(maxD, industrial ? m_rng.irange(1, 2) : m_rng.chance(0.8f) ? 2 : 3);
            const float stripSetback = m_rng.range(0.0f, 0.5f);
            float u = (alongX ? r.x0 : r.z0) + 0.3f;
            const float uEnd = (alongX ? r.x1 : r.z1) - 0.3f;
            bool first = true;
            while (uEnd - u >= (industrial ? 6.5f : 4.6f))
            {
                const float used = industrial ? industrialLot(r, front, u, uEnd, D, stripSetback)
                                              : urbanLot(r, front, u, uEnd, D, first, stripSetback, setbackMax);
                if (used <= 0.0f)
                    break;
                u += used;
                first = false;
            }
            const float band = D * unit + 1.2f + setbackMax + 2.4f;
            if (across - band >= 4.6f && (r.level == 0 || m_rng.chance(m_backRows)))
            {
                Rect b = r;
                b.level = r.level + 1;
                if (front == 0)
                    b.z0 += band;
                else if (front == 1)
                    b.z1 -= band;
                else if (front == 2)
                    b.x0 += band;
                else
                    b.x1 -= band;
                stack.push_back(b);
            }
        }

        struct Lot
        {
            Kind kind = Kind::Row;
            int w = 1, d = 2, floors = 2;
            uint16_t wall = P_WALL_MODULAR;
            float windows = 0.4f, interior = 0.2f, gap = 0.0f;
            bool shop = false, stairs = true, tank = false, empty = false;
            uint8_t roof = 0;
        };

         
        float urbanLot(const Rect& r, int front, float u, float uEnd, int D, bool first, float stripSetback, float setbackMax)
        {
            const float room = uEnd - u;
            const int A = static_cast<int>((room - 0.6f) / BAY);
            if (A < 1)
                return 0.0f;
            const bool alongX = front < 2;
            const bool suburb = m_zone == Zone::Suburb;
            const int extra = (m_core || m_dens >= 3) && m_rng.chance(m_dens >= 3 ? 0.4f : 0.25f) ? 1 : 0;
            const float emptyP = 0.34f - 0.07f * m_dens;
            const float roll = m_rng.f01();
            auto dd = [&](int lo, int hi) { return std::max(1, std::min(D, m_rng.irange(lo, hi))); };
            Lot l;
            auto shop = [&]()
            {
                l.kind = Kind::Shop, l.w = std::min(A, m_rng.irange(2, 3)), l.d = dd(2, 2), l.floors = (m_rng.chance(0.4f) ? 2 : 1) + extra;
                l.wall = m_rng.chance(0.5f) ? P_WALL_CONCRETE : P_WALL_MODULAR, l.windows = 0.55f, l.shop = true, l.interior = 0.15f;
            };
            auto row = [&]()
            {
                l.kind = Kind::Row, l.w = std::min(A, m_rng.chance(0.25f) ? 1 : m_rng.chance(0.7f) ? 2 : 3), l.d = dd(2, 3), l.floors = (m_rng.chance(0.25f) ? 3 : 2) + extra, l.windows = 0.35f, l.interior = 0.1f;
                l.roof = m_rng.chance(0.45f) ? 2 : m_rng.chance(0.35f) ? 1 : 0;
            };
            auto garage = [&]()
            {
                l.kind = Kind::Garage, l.w = std::min(A, m_rng.irange(1, 2)), l.d = dd(1, 2), l.floors = 1;
                l.wall = m_rng.chance(0.5f) ? P_WALL_CONCRETE : P_WALL_MODULAR, l.windows = 0.0f, l.shop = true, l.stairs = false, l.interior = 0.0f;
                l.roof = m_rng.chance(0.5f) ? 1 : 0, l.gap = m_rng.range(0.0f, 1.5f);
            };
            auto kiosk = [&]()
            {
                l.kind = Kind::Kiosk, l.w = 1, l.d = 1, l.floors = 1, l.windows = 0.3f, l.shop = true, l.stairs = false, l.interior = 0.0f;
                l.gap = m_rng.range(0.0f, 1.0f);
            };
            auto apartment = [&]()
            {
                l.kind = Kind::Apartment, l.w = std::min(A, 3), l.d = dd(2, 2), l.floors = m_rng.irange(3, 4) + extra;
                l.wall = P_WALL_CONCRETE, l.windows = 0.55f, l.shop = m_rng.chance(0.4f), l.tank = m_rng.chance(0.3f);
            };
            auto house = [&]()
            {
                l.kind = Kind::House, l.w = std::min(A, m_rng.irange(2, 3)), l.d = dd(2, 3), l.floors = m_rng.chance(0.35f) ? 3 : 2;
                l.windows = 0.45f, l.interior = 0.25f, l.roof = m_rng.chance(0.55f) ? 2 : 0, l.tank = l.roof == 0 && m_rng.chance(0.25f);
                l.gap = m_rng.range(1.2f, 4.0f);
            };
            const bool corner = first || room - (std::min(A, 3) * BAY + 0.6f) < 4.6f;
            if (m_rng.chance(emptyP))
                l.empty = true, l.w = m_rng.irange(1, 2);
            else if (suburb)
            {
                if (roll < 0.40f)
                    house();
                else if (roll < 0.48f)
                    row();
                else if (roll < 0.64f)
                    garage();
                else if (roll < 0.69f)
                    corner ? shop() : house();
                else if (roll < 0.73f)
                    kiosk();
                else if (roll < 0.83f)
                    l.kind = Kind::Shed;
                else if (roll < 0.86f)
                    apartment();
                else
                    house();
            }
            else
            {
                if (roll < 0.24f)
                    shop();
                else if (roll < 0.58f)
                    row();
                else if (roll < 0.63f)
                    apartment();
                else if (roll < 0.76f)
                    garage();
                else if (roll < 0.82f)
                    kiosk();
                else if (roll < 0.93f)
                    l.kind = Kind::Shed;
                else
                    row();
            }
            if (l.kind == Kind::Shed)
            {
                if (room < 7.0f)
                    return 0.0f;
                const float off = 0.3f + (suburb ? m_rng.range(0.0f, setbackMax) : stripSetback);
                const float a = u + 0.5f;  
                float x0, z0;
                if (alongX)
                    x0 = a, z0 = front == 0 ? r.z0 + off : r.z1 - off - 6.0f;
                else
                    z0 = a, x0 = front == 2 ? r.x0 + off : r.x1 - off - 6.0f;
                const float y0 = m_ground(x0 + 3.0f, z0 + 3.0f);
                m_tb.begin(Kind::Shed, 1);
                m_tb.hut({x0 + 3.0f, y0, z0 + 3.0f});
                m_tb.end();
                take(x0, z0, x0 + 6.0f, z0 + 6.0f, y0, 5.0f);
                return 7.0f + m_rng.range(0.0f, 1.5f);
            }
            const int w = std::max(1, std::min(l.w, A));
            const float extent = w * BAY + 0.6f;
            if (l.empty)
                return extent;
            const float setback = suburb ? m_rng.range(0.0f, setbackMax) : stripSetback;
            const int d = std::max(1, std::min(l.d, D));
            const int nx = alongX ? w : d, nz = alongX ? d : w;
            const float off = 0.3f + setback;
            float x0, z0;
            if (alongX)
            {
                x0 = u + 0.3f;
                z0 = front == 0 ? r.z0 + off : r.z1 - off - nz * BAY;
            }
            else
            {
                z0 = u + 0.3f;
                x0 = front == 2 ? r.x0 + off : r.x1 - off - nx * BAY;
            }
            auto p = params(l.wall, l.windows, l.interior, l.kind == Kind::Garage || l.kind == Kind::Kiosk ? -1 : front);
            p.waterTank = l.tank, p.stairs = l.stairs, p.roof = l.roof;
            if (l.shop)
            {
                p.shopMask = static_cast<uint8_t>(1u << front);
                if (corner && l.kind == Kind::Shop)
                    p.shopMask |= static_cast<uint8_t>(1u << (alongX ? (first ? 2 : 3) : (first ? 0 : 1)));
            }
            frameAt(l.kind, x0, z0, nx, nz, l.floors, p);
            return extent + l.gap;
        }

         
        float industrialLot(const Rect& r, int front, float u, float uEnd, int D, float stripSetback)
        {
            const float room = uEnd - u;
            const bool alongX = front < 2;
            const float roll = m_rng.f01();
            const float off = 0.6f + stripSetback;
             
            auto origin = [&](float depthM, float& x0, float& z0)
            {
                if (alongX)
                {
                    x0 = u;
                    z0 = front == 0 ? r.z0 + off : r.z1 - off - depthM;
                }
                else
                {
                    z0 = u;
                    x0 = front == 2 ? r.x0 + off : r.x1 - off - depthM;
                }
            };
            if (roll < 0.42f)
            {
                const int wb = std::min(static_cast<int>((room - 1.0f) / 6.0f), m_rng.irange(1, 3));
                if (wb < 1)
                    return 0.0f;
                const int db = std::min(D, m_rng.irange(1, 2));
                const float wM = wb * 6.0f + 1.0f;
                float x0, z0;
                origin(db * 6.0f + 1.0f, x0, z0);
                x0 += 0.5f, z0 += 0.5f;
                const int nx = alongX ? wb : db, nz = alongX ? db : wb;
                const float y0 = m_ground(x0 + nx * 3.0f, z0 + nz * 3.0f);
                m_tb.begin(Kind::Warehouse, 0);
                m_tb.factory(x0, z0, y0, nx, nz, 1, front);
                m_tb.end();
                take(x0, z0, x0 + nx * 6.0f, z0 + nz * 6.0f, y0, 8.0f);
                return wM + m_rng.range(0.6f, 2.5f);
            }
            const int A = static_cast<int>((room - 0.6f) / BAY);
            if (roll < 0.62f)
            {
                if (A < 2)
                    return 0.0f;
                const int w = std::min(A, m_rng.irange(2, 3)), d = std::min(D * 2 - 1, 2);
                const int nx = alongX ? w : d, nz = alongX ? d : w;
                float x0, z0;
                origin(d * BAY + 0.6f, x0, z0);
                frameAt(Kind::Office, x0 + 0.3f, z0 + 0.3f, nx, nz, m_rng.irange(1, 2),
                        params(m_rng.chance(0.5f) ? P_WALL_CONCRETE : P_WALL_MODULAR, 0.6f, 0.2f, front));
                return w * BAY + 0.6f + m_rng.range(0.6f, 3.0f);
            }
            if (roll < 0.76f)
            {
                if (A < 1)
                    return 0.0f;
                const int w = std::min(A, m_rng.irange(1, 2));
                const int nx = alongX ? w : 1, nz = alongX ? 1 : w;
                float x0, z0;
                origin(BAY + 0.6f, x0, z0);
                auto p = params(P_WALL_MODULAR, 0.0f, 0.0f, -1);
                p.stairs = false;
                p.shopMask = static_cast<uint8_t>(1u << front);
                p.roof = m_rng.chance(0.5f) ? 1 : 0;
                frameAt(Kind::Garage, x0 + 0.3f, z0 + 0.3f, nx, nz, 1, p);
                return w * BAY + 0.6f + m_rng.range(0.0f, 1.5f);
            }
            if (roll < 0.90f)
            {
                const float len = std::min(room, m_rng.range(10.0f, 18.0f));
                if (len < 8.0f)
                    return 0.0f;
                const float depth = std::min(D * 6.0f + 3.0f, 14.0f);
                float x0, z0;
                origin(depth, x0, z0);
                const Rect yr = alongX ? Rect{x0, z0, x0 + len, z0 + depth} : Rect{x0, z0, x0 + depth, z0 + len};
                take(yr.x0, yr.z0, yr.x1, yr.z1, ground(yr), 4.0f);
                fencedYard(yr, m_rng.irange(3, 6), 0.8f);
                return len + 0.6f;
            }
            return m_rng.range(6.0f, 12.0f);
        }
    };

     
    template <typename GroundFn>
    Town generateTown(uint32_t seed, std::span<const Block> blocks, GroundFn groundY, const TownOptions& opt = {},
                      std::span<const CityInfo> cities = {})
    {
        Town town;
        town.seed = seed;
        Rng rng(seed);
        TownBuilder tb(town, rng);
        BlockPlanner<GroundFn> planner(tb, rng, groundY);
        int towersAt[64] = {}, factoriesAt[64] = {};
        CityState state[64];
        for (const Block& b : blocks)
            ++state[std::min<uint8_t>(b.city, 63)].blocksLeft;
        for (CityState& s : state)
        {
            const int n = s.blocksLeft;
            s.hallLimit = n >= 8 ? 1 + n / 25 : 0;
            s.schoolLimit = n >= 20 ? n / 30 + 1 : 0;
            s.stationLimit = n >= 10 ? n / 40 + 1 : 0;
        }
        for (size_t bi = 0; bi < blocks.size(); ++bi)
        {
            const Block& blk = blocks[bi];
            const float cx = (blk.x0 + blk.x1) * 0.5f, cz = (blk.z0 + blk.z1) * 0.5f;
            const uint8_t ci = std::min<uint8_t>(blk.city, 63);
            const bool hasCity = ci < cities.size();
            tb.city(ci, hasCity ? cities[ci].name : std::string());
            const float ccx = hasCity ? cities[ci].cx : opt.centre.x, ccz = hasCity ? cities[ci].cz : opt.centre.z;
            const float rad = hasCity ? cities[ci].radius : opt.radius;
            const int maxTowers = hasCity ? cities[ci].maxTowers : opt.maxTowers;
            int& towers = towersAt[ci];
            int& factories = factoriesAt[ci];
            const float d = std::sqrt((cx - ccx) * (cx - ccx) + (cz - ccz) * (cz - ccz)) / rad;
            const int dens = hasCity ? cities[ci].density : 0;
            const bool dense = dens > 0, core = dens > 1;
            const float roll = rng.f01();
            Kind kind;
            switch (blk.zone)
            {
            case Zone::Suburb:
                kind = roll < 0.82f ? Kind::House : roll < 0.92f ? Kind::Apartment : Kind::Yard;
                break;
            case Zone::Industrial:
                kind = dense ? (roll < 0.5f ? Kind::Factory : roll < 0.92f ? Kind::Yard : Kind::Apartment)
                             : (roll < 0.45f ? Kind::Factory : roll < 0.78f ? Kind::Yard : Kind::Apartment);
                break;
            case Zone::Village:
                kind = Kind::House;
                break;
            default:
                if (core && d < 0.25f)
                    kind = roll < 0.9f && towers < maxTowers ? Kind::Tower : Kind::Apartment;
                else if (core && d < 0.45f)
                    kind = roll < 0.2f && towers < maxTowers ? Kind::Tower : roll < 0.6f ? Kind::Apartment : roll < 0.8f ? Kind::House : Kind::Yard;
                else if (core)
                    kind = d < 0.75f ? (roll < 0.15f ? Kind::Apartment : roll < 0.4f ? Kind::House : roll < 0.5f ? Kind::Factory : Kind::Yard)
                                     : (roll < 0.25f ? Kind::House : roll < 0.35f ? Kind::Factory : Kind::Yard);
                else if (d < 0.3f)  
                    kind = (roll < 0.55f || (dense && towers < 2)) && towers < maxTowers ? Kind::Tower : Kind::Apartment;
                else if (d < 0.65f)
                    kind = roll < (dense ? 0.35f : 0.45f) ? Kind::Apartment : roll < 0.9f ? Kind::House : Kind::Factory;
                else
                    kind = roll < 0.5f ? Kind::House : roll < 0.75f ? Kind::Factory : Kind::Yard;
                if (kind == Kind::Factory && factories >= (dense ? 6 : 4))
                    kind = Kind::House;
                break;
            }
            towers += kind == Kind::Tower;
            factories += kind == Kind::Factory;
            planner.run(blk, kind, dens, core, d, state[ci]);
        }
        return town;
    }
}  
