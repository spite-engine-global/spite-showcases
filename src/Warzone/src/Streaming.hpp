 
 
#pragma once
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <cmath>
#include <span>
#include <string>
#include <vector>

#include "BuildingGen.hpp"

namespace warzone
{
    struct StreamParams
    {
        float radiusIn = 800.0f;     
        float radiusOut = 950.0f;   
        uint32_t pieceBudget = 26000;  
        float budgetSlack = 1.2f;
    };

    inline float groundDistance(const Box& b, float x, float z)
    {
        const float dx = std::max({b.lo.x - x, 0.0f, x - b.hi.x});
        const float dz = std::max({b.lo.z - z, 0.0f, z - b.hi.z});
        return std::sqrt(dx * dx + dz * dz);
    }

     
     
     
    inline uint32_t chooseByOrder(std::span<const uint32_t> order, std::span<const float> dist, std::span<const uint32_t> pieces,
                                  std::span<uint8_t> resident, const StreamParams& p)
    {
        uint32_t used = 0;
        for (uint32_t i : order)
        {
            const bool was = resident[i] != 0;
            const float limit = was ? p.radiusOut : p.radiusIn;
            const uint32_t cap = was ? static_cast<uint32_t>(p.pieceBudget * p.budgetSlack) : p.pieceBudget;
            const bool want = pieces[i] > 0 && dist[i] <= limit && used + pieces[i] <= cap;
            resident[i] = want ? 1 : 0;
            if (want)
                used += pieces[i];
        }
        return used;
    }

    inline void sortByDistance(std::span<const float> dist, std::vector<uint32_t>& order)
    {
        order.resize(dist.size());
        std::iota(order.begin(), order.end(), 0u);
        std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return dist[a] != dist[b] ? dist[a] < dist[b] : a < b; });
    }

     
    inline uint32_t chooseResident(std::span<const float> dist, std::span<const uint32_t> pieces, std::span<uint8_t> resident,
                                   const StreamParams& p, std::vector<uint32_t>& order)
    {
        sortByDistance(dist, order);
        return chooseByOrder(order, dist, pieces, resident, p);
    }

    struct BuildingGrid
    {
        static constexpr float CELL = 256.0f;
        int x0 = 0, z0 = 0, nx = 0, nz = 0;
        std::vector<std::vector<uint32_t>> cells;
        void build(std::span<const Box> boxes)
        {
            if (boxes.empty()) return;
            int x1 = 0, z1 = 0;
            x0 = z0 = 0;
            for (const auto& b : boxes)
            {
                x0 = std::min(x0, static_cast<int>(std::floor(b.lo.x / CELL)));
                z0 = std::min(z0, static_cast<int>(std::floor(b.lo.z / CELL)));
                x1 = std::max(x1, static_cast<int>(std::floor(b.hi.x / CELL)));
                z1 = std::max(z1, static_cast<int>(std::floor(b.hi.z / CELL)));
            }
            nx = x1 - x0 + 1; nz = z1 - z0 + 1;
            cells.assign(static_cast<size_t>(nx) * nz, {});
            for (uint32_t i = 0; i < boxes.size(); ++i)
                for (int z = static_cast<int>(std::floor(boxes[i].lo.z / CELL)); z <= static_cast<int>(std::floor(boxes[i].hi.z / CELL)); ++z)
                    for (int x = static_cast<int>(std::floor(boxes[i].lo.x / CELL)); x <= static_cast<int>(std::floor(boxes[i].hi.x / CELL)); ++x)
                        cells[(z-z0)*nx+x-x0].push_back(i);
        }
        void candidates(float x, float z, float radius, std::vector<uint32_t>& out) const
        {
            out.clear();
            for (int j = std::max(z0, static_cast<int>(std::floor((z-radius)/CELL))); j <= std::min(z0+nz-1, static_cast<int>(std::floor((z+radius)/CELL))); ++j)
                for (int i = std::max(x0, static_cast<int>(std::floor((x-radius)/CELL))); i <= std::min(x0+nx-1, static_cast<int>(std::floor((x+radius)/CELL))); ++i)
                    out.insert(out.end(), cells[(j-z0)*nx+i-x0].begin(), cells[(j-z0)*nx+i-x0].end());
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
        }
    };

     
     
    inline uint32_t chooseDetail(std::span<const uint32_t> order, std::span<const float> dist, std::span<const uint32_t> pieces,
                                 std::span<const uint8_t> resident, std::span<uint8_t> detail, std::vector<uint32_t>& scratch,
                                 const StreamParams& p)
    {
        scratch.assign(pieces.begin(), pieces.end());
        for (size_t i = 0; i < scratch.size(); ++i)
            if (!resident[i])
                scratch[i] = 0;
        return chooseByOrder(order, dist, scratch, detail, p);
    }

     
     
    struct TierParams
    {
        float lowIn = 220.0f, lowOut = 190.0f;
    };
    inline uint8_t chooseTier(bool detail, float dist, uint8_t previous, const TierParams& t)
    {
        if (detail)
            return 1;
        return dist > (previous == 2 ? t.lowOut : t.lowIn) ? 2 : 0;
    }

     
     
     
     
     
    inline const char* standInStyle(Kind kind)
    {
        switch (kind)
        {
        case Kind::Apartment:
        case Kind::Shop:
        case Kind::Hall:
        case Kind::School:
            return "apartment";
        case Kind::Tower:
            return "tower";
        case Kind::Factory:
            return "factory";
        case Kind::Warehouse:
            return "warehouse";
        case Kind::Yard:
        case Kind::Station:
            return "yard";
        default:
            return "house";
        }
    }
    inline bool isPatch(Kind kind) { return kind == Kind::Yard || kind == Kind::Station; }
    inline std::string standInModel(Kind kind, int floors, const Box& bounds, uint32_t id)
    {
        if (isPatch(kind))
            return "wz_lod_yard";
        const float unit = kind == Kind::Factory || kind == Kind::Warehouse ? 6.0f : 4.0f;
        const int nx = static_cast<int>(std::lround((bounds.hi.x - bounds.lo.x) / unit));
        const int nz = static_cast<int>(std::lround((bounds.hi.z - bounds.lo.z) / unit));
        return std::string("wz_lod_") + standInStyle(kind) + "_" + std::to_string(nx) + "x" + std::to_string(nz) + "x" + std::to_string(floors) +
               ((id * 2654435761u >> 16) & 1u ? "_b" : "_a");
    }

     
     
    struct StandIn
    {
        V3 base;   
        V3 size;
        bool shown = false;
    };
    inline float nominalTop(Kind kind, int floors, const Box& bounds)
    {
        const float lo = bounds.lo.y, h = bounds.hi.y - bounds.lo.y;
        switch (kind)
        {
        case Kind::Factory:
            return std::min(bounds.hi.y, lo + SLAB_T + 8.0f + PIECES[P_ROOF].sy);
        case Kind::Warehouse:
            return std::min(bounds.hi.y, lo + SLAB_T + 4.0f + PIECES[P_ROOF].sy);
        case Kind::Yard:
        case Kind::Station:
            return lo + std::max(1.2f, 0.35f * h);
        default:
            return std::min(bounds.hi.y, lo + SLAB_T + floors * STOREY + PIECES[P_PARAPET].sy);
        }
    }
    inline StandIn standIn(Kind kind, int floors, const Box& bounds, float intactTop, uint32_t intact)
    {
        StandIn s;
        if (intact == 0)
            return s;
        const float top = std::min(nominalTop(kind, floors, bounds), std::max(intactTop, bounds.lo.y + 0.5f));
        const float inset = isPatch(kind) ? 0.9f : 0.985f;  
        s.base = {(bounds.lo.x + bounds.hi.x) * 0.5f, bounds.lo.y, (bounds.lo.z + bounds.hi.z) * 0.5f};
        s.size = {(bounds.hi.x - bounds.lo.x) * inset, std::max(0.5f, top - bounds.lo.y), (bounds.hi.z - bounds.lo.z) * inset};
        s.shown = true;
        return s;
    }
}  
