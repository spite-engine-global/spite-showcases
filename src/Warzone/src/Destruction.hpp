 
 
 
#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "Core.hpp"
#include "PieceTable.hpp"
#include "Weapons.hpp"

namespace warzone
{
     
     
    class SpatialGrid
    {
    public:
        explicit SpatialGrid(float cell = 8.0f) : m_cell(cell) {}
        void insert(uint32_t item, const Box& b)
        {
            forCells(b, [&](int64_t key) { m_cells[key].push_back(item); });
            if (item >= m_boxes.size())
                m_boxes.resize(item + 1);
            m_boxes[item] = b;
        }
        void remove(uint32_t item)
        {
            if (item >= m_boxes.size())
                return;
            forCells(m_boxes[item],
                     [&](int64_t key)
                     {
                         auto it = m_cells.find(key);
                         if (it == m_cells.end())
                             return;
                         auto& v = it->second;
                         for (size_t i = 0; i < v.size(); ++i)
                             if (v[i] == item)
                             {
                                 v[i] = v.back();
                                 v.pop_back();
                                 break;
                             }
                     });
        }
         
        void query(const Box& b, std::vector<uint32_t>& out) const
        {
            out.clear();
            forCells(b,
                     [&](int64_t key)
                     {
                         auto it = m_cells.find(key);
                         if (it == m_cells.end())
                             return;
                         for (uint32_t item : it->second)
                             if (overlaps(m_boxes[item], b, 0.0f))
                                 out.push_back(item);
                     });
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
        }
        void querySphere(const V3& c, float r, std::vector<uint32_t>& out) const
        {
            query({{c.x - r, c.y - r, c.z - r}, {c.x + r, c.y + r, c.z + r}}, out);
            std::erase_if(out, [&](uint32_t i) { return distance(m_boxes[i], c) > r; });
        }
        size_t cellCount() const { return m_cells.size(); }

    private:
        template <typename F>
        void forCells(const Box& b, F&& f) const
        {
            const int x0 = static_cast<int>(std::floor(b.lo.x / m_cell)), x1 = static_cast<int>(std::floor(b.hi.x / m_cell));
            const int z0 = static_cast<int>(std::floor(b.lo.z / m_cell)), z1 = static_cast<int>(std::floor(b.hi.z / m_cell));
            for (int x = x0; x <= x1; ++x)
                for (int z = z0; z <= z1; ++z)
                    f((static_cast<int64_t>(x) << 32) ^ static_cast<uint32_t>(z));
        }
        float m_cell;
        std::unordered_map<int64_t, std::vector<uint32_t>> m_cells;
        std::vector<Box> m_boxes;
    };

     
     
    inline float materialStrength(Material m)
    {
        switch (m)
        {
        case Material::Concrete: return 120.0f;
        case Material::Masonry: return 80.0f;
        case Material::Steel: return 150.0f;
        case Material::Wood: return 40.0f;
        }
        return 100.0f;
    }
    inline float hitPoints(const Box& b, Material m) { return std::max(30.0f, b.volume() * materialStrength(m)); }
    inline float massOf(const Box& b, Material m)
    {
        const float density = m == Material::Steel ? 600.0f : m == Material::Wood ? 400.0f : 1800.0f;  
        return std::clamp(b.volume() * density, 20.0f, 20000.0f);
    }

     
    enum class PieceState : uint8_t
    {
        Intact,
        Scheduled,  
        Removed     
    };

    struct Blast
    {
        V3 centre;
        float radius = 4.0f;
        float damage = 500.0f;  
        float push = 12.0f;     
    };

    inline V3 nukeVelocity(const Box& box, const Blast& blast)
    {
        const float strength = std::clamp(1 - distance(box, blast.centre) / blast.radius, 0.0f, 1.0f);
        const V3 off = box.centre() - blast.centre;
        const V3 out = norm(V3{off.x, 0, off.z});
        return out * (blast.push * (0.1f + 0.9f * std::sqrt(strength))) + V3{0, 8 + 50 * strength, 0};
    }

    struct Removal
    {
        uint32_t piece;
        bool destroyed;  
        V3 velocity;     
    };

    struct StructureParams
    {
        float contactTol = 0.08f;     
        float minContactArea = 0.02f;
        float groundTol = 0.12f;      
        uint32_t waveFrames = 5;      
        uint32_t waveJitter = 4;      
        float spread = 0.5f;          
        int spreadDepth = 2;
        float looseShare = 0.3f;      
        uint32_t maxFallsPerFrame = 80;
    };

    class Structure
    {
    public:
        struct Node
        {
            Box box;
            float hp = 1, maxHp = 1;
            PieceState state = PieceState::Intact;
            bool grounded = false;
            uint16_t building = 0;
            uint16_t needed = 1;  
            uint32_t supFirst = 0, supCount = 0, depFirst = 0, depCount = 0;
            uint32_t fallFrame = 0;
        };

        StructureParams params;

         
        void build(const std::vector<Box>& boxes, const std::vector<float>& hp, const std::vector<uint16_t>& building,
                   const std::vector<float>& groundY)
        {
            const size_t n = boxes.size();
            m_nodes.assign(n, {});
            m_grid = SpatialGrid(8.0f);
            for (uint32_t i = 0; i < n; ++i)
            {
                m_nodes[i].box = boxes[i];
                m_nodes[i].hp = m_nodes[i].maxHp = hp[i];
                m_nodes[i].building = building[i];
                m_nodes[i].grounded = boxes[i].lo.y <= groundY[i] + params.groundTol;
                m_grid.insert(i, boxes[i]);
            }
            std::vector<std::vector<uint32_t>> sup(n), dep(n);
            std::vector<uint32_t> near;
            for (uint32_t i = 0; i < n; ++i)
            {
                if (m_nodes[i].grounded)
                    continue;
                const Box& b = boxes[i];
                 
                m_grid.query({{b.lo.x, b.lo.y - params.contactTol, b.lo.z}, {b.hi.x, b.lo.y + params.contactTol, b.hi.z}}, near);
                for (uint32_t j : near)
                    if (j != i && std::abs(boxes[j].hi.y - b.lo.y) <= params.contactTol && footprintOverlap(boxes[j], b) >= params.minContactArea &&
                        building[j] == building[i])
                        sup[i].push_back(j);
                if (sup[i].empty())
                {
                     
                    m_grid.query({b.lo - V3{0.05f, 0.05f, 0.05f}, b.hi + V3{0.05f, 0.05f, 0.05f}}, near);
                    for (uint32_t j : near)
                        if (j != i && building[j] == building[i] && boxes[j].lo.y < b.hi.y - params.contactTol &&
                            boxes[j].hi.y > b.lo.y + params.contactTol)
                            sup[i].push_back(j);
                }
                for (uint32_t j : sup[i])
                    dep[j].push_back(i);
            }
            m_sup.clear();
            m_dep.clear();
            for (uint32_t i = 0; i < n; ++i)
            {
                auto& node = m_nodes[i];
                node.supFirst = static_cast<uint32_t>(m_sup.size());
                node.supCount = static_cast<uint32_t>(sup[i].size());
                m_sup.insert(m_sup.end(), sup[i].begin(), sup[i].end());
                node.depFirst = static_cast<uint32_t>(m_dep.size());
                node.depCount = static_cast<uint32_t>(dep[i].size());
                m_dep.insert(m_dep.end(), dep[i].begin(), dep[i].end());
                node.needed = node.supCount >= 4 ? 2 : 1;
                if (!node.grounded && node.supCount == 0)
                    ++m_unsupported;
            }
            m_queue.clear();
        }

        size_t size() const { return m_nodes.size(); }
        const Node& node(uint32_t i) const { return m_nodes[i]; }
        std::span<const uint32_t> supporters(uint32_t i) const { return {m_sup.data() + m_nodes[i].supFirst, m_nodes[i].supCount}; }
        std::span<const uint32_t> dependents(uint32_t i) const { return {m_dep.data() + m_nodes[i].depFirst, m_nodes[i].depCount}; }
        uint32_t unsupportedAtBuild() const { return m_unsupported; }
        size_t pending() const { return m_queue.size() + m_late.size(); }
        uint32_t intactSupporters(uint32_t i) const
        {
            uint32_t c = 0;
            for (uint32_t s : supporters(i))
                c += m_nodes[s].state == PieceState::Intact ? 1u : 0u;
            return c;
        }
         
        void overlapSphere(const V3& c, float r, std::vector<uint32_t>& out) const
        {
            m_grid.querySphere(c, r, out);
            std::erase_if(out, [&](uint32_t i) { return m_nodes[i].state != PieceState::Intact; });
        }

         
         
        bool raycast(const V3& p0, const V3& p1, uint32_t& piece, float& t, V3* normal = nullptr) const
        {
            const float length = len(p1 - p0);
            const int chunks = std::max(1, static_cast<int>(std::ceil(length / 16.0f)));
            std::vector<uint32_t> near;
            float best = 2.0f;
            for (int c = 0; c < chunks; ++c)
            {
                const V3 a = lerp(p0, p1, static_cast<float>(c) / chunks), b = lerp(p0, p1, static_cast<float>(c + 1) / chunks);
                m_grid.query({{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}, {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}},
                             near);
                for (uint32_t i : near)
                {
                    float th;
                    V3 n;
                    if (m_nodes[i].state == PieceState::Intact && segmentBox(m_nodes[i].box, p0, p1, th, &n) && th < best)
                    {
                        best = th;
                        piece = i;
                        if (normal)
                            *normal = n;
                    }
                }
                if (best <= static_cast<float>(c + 1) / chunks)
                    break;
            }
            t = best;
            return best <= 1.0f;
        }

         
        void applyBlast(const Blast& blast, uint32_t frame, std::vector<Removal>& out)
        {
            std::vector<uint32_t> hit;
            overlapSphere(blast.centre, blast.radius, hit);
            std::vector<std::pair<uint32_t, float>> spreadQueue;  
            for (uint32_t i : hit)
            {
                const float d = distance(m_nodes[i].box, blast.centre);
                const float f = 1.0f - d / blast.radius;
                damage(i, blast.damage * f * f, blast, f, frame, out, spreadQueue, 0);
            }
             
            for (int depth = 1; depth <= params.spreadDepth && !spreadQueue.empty(); ++depth)
            {
                auto current = std::move(spreadQueue);
                spreadQueue.clear();
                for (auto [i, dmg] : current)
                    damage(i, dmg, blast, 0.0f, frame, out, spreadQueue, depth);
            }
        }

         
        void nukePiece(uint32_t i, const Blast& blast, uint32_t frame, std::vector<Removal>& out)
        {
            Node& n = m_nodes[i];
            if (n.state == PieceState::Removed || distance(n.box, blast.centre) > blast.radius)
                return;
            remove(i, frame);
            out.push_back({i, false, nukeVelocity(n.box, blast)});
        }

         
        void remove(uint32_t i, uint32_t frame)
        {
            if (m_nodes[i].state == PieceState::Removed)
                return;
            m_nodes[i].state = PieceState::Removed;
            m_grid.remove(i);
            propagate(i, frame);
        }

         
        void tick(uint32_t frame, std::vector<Removal>& out)
        {
            uint32_t budget = params.maxFallsPerFrame;
            size_t keep = 0;
            for (size_t q = 0; q < m_queue.size(); ++q)
            {
                const uint32_t i = m_queue[q];
                Node& n = m_nodes[i];
                if (n.state != PieceState::Scheduled)
                    continue;  
                if (n.fallFrame > frame || budget == 0)
                {
                    m_queue[keep++] = i;
                    continue;
                }
                --budget;
                n.state = PieceState::Removed;
                m_grid.remove(i);
                out.push_back({i, false, {}});
                propagate(i, frame);
            }
            m_queue.resize(keep);
            for (uint32_t i : m_late)
                m_queue.push_back(i);
            m_late.clear();
        }

    private:
        void damage(uint32_t i, float dmg, const Blast& blast, float falloff, uint32_t frame, std::vector<Removal>& out,
                    std::vector<std::pair<uint32_t, float>>& spreadQueue, int depth)
        {
            Node& n = m_nodes[i];
            if (n.state == PieceState::Removed || dmg <= 0.0f)
                return;
            n.hp -= dmg;
            const V3 dir = norm(n.box.centre() - blast.centre + V3{0, 0.3f, 0});
            const V3 vel = dir * (blast.push * std::max(falloff, 0.15f));
            if (n.hp <= 0.0f)
            {
                const float overflow = -n.hp;
                n.hp = 0.0f;
                n.state = PieceState::Removed;
                m_grid.remove(i);
                out.push_back({i, true, vel});
                if (depth < params.spreadDepth && overflow > 1.0f)
                {
                    for (uint32_t s : supporters(i))
                        spreadQueue.push_back({s, overflow * params.spread});
                    for (uint32_t s : dependents(i))
                        spreadQueue.push_back({s, overflow * params.spread});
                }
                propagate(i, frame);
            }
            else if (depth == 0 && dmg >= params.looseShare * n.maxHp)
            {
                n.state = PieceState::Removed;  
                m_grid.remove(i);
                out.push_back({i, false, vel});
                propagate(i, frame);
            }
        }

        void propagate(uint32_t removed, uint32_t frame)
        {
            for (uint32_t d : dependents(removed))
            {
                Node& n = m_nodes[d];
                if (n.state != PieceState::Intact || n.grounded)
                    continue;
                if (intactSupporters(d) < std::min<uint32_t>(n.needed, n.supCount))
                {
                    n.state = PieceState::Scheduled;
                    n.fallFrame = frame + params.waveFrames + hash32(d) % (params.waveJitter + 1);
                    m_late.push_back(d);
                }
            }
        }

        std::vector<Node> m_nodes;
        std::vector<uint32_t> m_sup, m_dep;
        std::vector<uint32_t> m_queue, m_late;
        SpatialGrid m_grid;
        uint32_t m_unsupported = 0;
    };

     
     
     
    enum class DebrisState : uint8_t
    {
        Dynamic,
        Static,
        Fading,
        Gone
    };

    struct DebrisParams
    {
        uint32_t cap = 600;
        float sleepToStatic = 0.6f;
        float minDynamicSeconds = 1.5f;   
        float maxDynamicSeconds = 12.0f;  
        float staticSeconds = 40.0f;
        float fadeSeconds = 2.5f;
    };

    class DebrisSet
    {
    public:
        struct Item
        {
            uint64_t id;
            uint64_t order;  
            DebrisState state = DebrisState::Dynamic;
            float age = 0, asleep = 0, stateTime = 0;
        };
        struct Event
        {
            uint64_t id;
            DebrisState to;
        };

        DebrisParams params;

         
        void add(uint64_t id, std::vector<Event>& events)
        {
            m_items.push_back({id, m_next++});
            m_index[id] = m_items.size() - 1;
            enforceCap(events);
        }
         
        template <typename AwakeFn>
        void update(float dt, AwakeFn awake, std::vector<Event>& events)
        {
            for (auto& it : m_items)
            {
                it.age += dt;
                it.stateTime += dt;
                switch (it.state)
                {
                case DebrisState::Dynamic:
                    it.asleep = awake(it.id) || it.age < params.minDynamicSeconds ? 0.0f : it.asleep + dt;
                    if (it.asleep >= params.sleepToStatic || it.age >= params.maxDynamicSeconds)
                        setState(it, DebrisState::Static, events);
                    break;
                case DebrisState::Static:
                    if (it.stateTime >= params.staticSeconds)
                        setState(it, DebrisState::Fading, events);
                    break;
                case DebrisState::Fading:
                    if (it.stateTime >= params.fadeSeconds)
                        setState(it, DebrisState::Gone, events);
                    break;
                case DebrisState::Gone:
                    break;
                }
            }
            compact();
        }
         
        float fade(uint64_t id) const
        {
            auto it = m_index.find(id);
            if (it == m_index.end())
                return 1.0f;
            const Item& item = m_items[it->second];
            return item.state == DebrisState::Fading ? std::min(1.0f, item.stateTime / params.fadeSeconds) : 0.0f;
        }
        uint32_t live() const
        {
            uint32_t n = 0;
            for (auto& it : m_items)
                n += it.state == DebrisState::Dynamic || it.state == DebrisState::Static ? 1u : 0u;
            return n;
        }
        uint32_t count(DebrisState s) const
        {
            uint32_t n = 0;
            for (auto& it : m_items)
                n += it.state == s ? 1u : 0u;
            return n;
        }
        const std::vector<Item>& items() const { return m_items; }
        void clear()
        {
            m_items.clear();
            m_index.clear();
        }

    private:
        void setState(Item& it, DebrisState s, std::vector<Event>& events)
        {
            it.state = s;
            it.stateTime = 0;
            events.push_back({it.id, s});
        }
        void enforceCap(std::vector<Event>& events)
        {
            uint32_t liveCount = live();
            for (auto& it : m_items)  
            {
                if (liveCount <= params.cap)
                    break;
                if (it.state == DebrisState::Dynamic || it.state == DebrisState::Static)
                {
                    setState(it, DebrisState::Fading, events);
                    --liveCount;
                }
            }
        }
        void compact()
        {
            size_t w = 0;
            for (size_t r = 0; r < m_items.size(); ++r)
                if (m_items[r].state != DebrisState::Gone)
                    m_items[w++] = m_items[r];
            if (w != m_items.size())
            {
                m_items.resize(w);
                m_index.clear();
                for (size_t i = 0; i < m_items.size(); ++i)
                    m_index[m_items[i].id] = i;
            }
        }
        std::vector<Item> m_items;
        std::unordered_map<uint64_t, size_t> m_index;
        uint64_t m_next = 0;
    };
}  
