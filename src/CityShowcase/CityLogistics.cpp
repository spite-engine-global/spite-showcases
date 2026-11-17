#include "CityLogistics.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>

#include "CityTrips.hpp"
#include "ecs/Query.hpp"
#include "ecs/SystemContext.hpp"

namespace spite::city
{
    namespace
    {
        using namespace model;

        constexpr u64 ORDER_SALT = 0x0D3Eull;
        constexpr u32 KEEP_PACKAGES = 4096;  
        constexpr u32 SUPPLY_SCAN = 16;  
        constexpr float HOME_ORDERS = 0.7f;  
        constexpr float HOME_PARCELS = 0.85f;  
        constexpr float SHOP_PARCELS = 0.3f;

        float unit(const u64 key) { return static_cast<float>(mix(key) >> 40) / 16777216.0f; }

        Vec3 lotCentre(const LotRef& lot)
        {
            return {static_cast<float>(lot.x0 + lot.x1) * 0.5f * CELL, 0.0f,
                    static_cast<float>(lot.z0 + lot.z1) * 0.5f * CELL};
        }

        float distance(const float x0, const float z0, const float x1, const float z1)
        {
            return std::hypot(x1 - x0, z1 - z0);
        }

        bool finished(const u32 state) { return state == PACKAGE_DELIVERED || state == PACKAGE_CANCELLED; }

         
        u32 capacityOf(const u32 special)
        {
            return special == SPECIAL_WAREHOUSE ? WAREHOUSE_CAPACITY : special == SPECIAL_DRONE_HUB ? HUB_CAPACITY : 0u;
        }
    }  

     

    Package* LogisticsService::find(const u32 id)
    {
        return id >= m_firstId && id - m_firstId < m_packages.size() ? &m_packages[id - m_firstId] : nullptr;
    }

    const Package* LogisticsService::package(const u32 id) const
    {
        return id >= m_firstId && id - m_firstId < m_packages.size() ? &m_packages[id - m_firstId] : nullptr;
    }

    Site* LogisticsService::findSite(const u32 lot)
    {
        const auto at = std::ranges::lower_bound(m_sites, lot, {}, &Site::lot);
        return at != m_sites.end() && at->lot == lot ? &*at : nullptr;
    }

    const Site* LogisticsService::site(const u32 lot) const
    {
        const auto at = std::ranges::lower_bound(m_sites, lot, {}, &Site::lot);
        return at != m_sites.end() && at->lot == lot ? &*at : nullptr;
    }

    std::vector<const Package*> LogisticsService::recent(const u32 count) const
    {
        std::vector<const Package*> out;
        for (auto it = m_packages.rbegin(); it != m_packages.rend() && out.size() < count; ++it)
        {
            out.push_back(&*it);
        }
        return out;
    }

    Package& LogisticsService::add(const u32 kind, const u32 origin, const u32 destination, const u32 units,
                                   const u32 step)
    {
        Package& package = m_packages.emplace_back();
        package.id = m_firstId + static_cast<u32>(m_packages.size()) - 1;
        package.kind = kind;
        package.origin = origin;
        package.destination = destination;
        package.units = units;
        package.orderedStep = step;
        ++m_stats.counts[kind][PACKAGE_WAITING];
        ++m_stats.ordered;
        return package;
    }

    void LogisticsService::setState(Package& package, const u32 state)
    {
        --m_stats.counts[package.kind][package.state];
        ++m_stats.counts[package.kind][state];
        package.state = state;
    }

    void LogisticsService::submit(const std::span<const LogisticsRequest> requests)
    {
        const std::scoped_lock hold(m_inboxLock);
        m_inbox.insert(m_inbox.end(), requests.begin(), requests.end());
    }

     

    void LogisticsService::begin(SystemContext& context, const CityStateComponent& city, const CityMap& map,
                                 const bool mapChanged)
    {
        const bool follows = m_started && m_seed == city.seed && m_size == city.size && m_cars == city.cars &&
            m_truckCount == city.trucks && m_droneCount == city.drones && city.step == m_lastStep + 1;
        m_lastStep = city.step;
        m_flying = 0;
        if (follows)
        {
            if (mapChanged)
            {
                rebuildSites(context, city, map);
            }
            return;
        }
        m_started = true;
        m_seed = city.seed;
        m_size = city.size;
        m_cars = city.cars;
        m_truckCount = city.trucks;
        m_droneCount = city.drones;
        m_epochStart = city.step;
        m_packages.clear();
        m_firstId = 1;
        m_supplyQueue.clear();
        m_departures.clear();
        m_sites.clear();
        m_trucks.assign(city.trucks, {});
        m_drones.assign(city.drones, {});
        m_views.assign(city.drones, {});
        m_stats = {};
        m_orderSerial = 0;
        {
            const std::scoped_lock hold(m_inboxLock);
            m_inbox.clear();
        }
        rebuildSites(context, city, map);
        const double perDay = m_stats.drones * PARCELS_PER_DRONE_DAY + city.trucks * FREIGHT_PER_TRUCK_DAY;
        m_orderDue = std::floor(perDay * BACKLOG_DAYS);
    }

    void LogisticsService::rebuildSites(SystemContext& context, const CityStateComponent& city, const CityMap& map)
    {
        struct Building
        {
            float x, z, halfX, halfZ, height;
            bool hubModel;
        };
        std::map<u32, Building> buildings;
        m_heights.assign(static_cast<sizet>(city.size) * city.size, 0.0f);
        const InternedString hubModel(catalog::DRONE_HUB);
        context.query<ecs::Read<CityBuildingComponent>>()->forEachChunk(
            [&](Chunk* chunk)
            {
                const CityBuildingComponent* rows = chunk->readColumn<CityBuildingComponent>();
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    const CityBuildingComponent& b = rows[row];
                    buildings[b.lot] = {b.x, b.z, b.halfX, b.halfZ, b.height, b.model == hubModel};
                    const u32 cx0 = static_cast<u32>(std::max((b.x - b.halfX) / CELL, 0.0f));
                    const u32 cz0 = static_cast<u32>(std::max((b.z - b.halfZ) / CELL, 0.0f));
                    const u32 cx1 = std::min(static_cast<u32>(std::ceil((b.x + b.halfX) / CELL)), city.size);
                    const u32 cz1 = std::min(static_cast<u32>(std::ceil((b.z + b.halfZ) / CELL)), city.size);
                    for (u32 z = cz0; z < cz1; ++z)
                    {
                        for (u32 x = cx0; x < cx1; ++x)
                        {
                            float& top = m_heights[static_cast<sizet>(z) * city.size + x];
                            top = std::max(top, b.height);
                        }
                    }
                }
            });
        std::vector<Site> old = std::move(m_sites);
        m_sites.clear();
        for (const u32 special : {SPECIAL_DRONE_HUB, SPECIAL_TRUCK_DEPOT, SPECIAL_WAREHOUSE, SPECIAL_FACTORY})
        {
            for (const u32 lotId : map.specialLots(special))
            {
                const LotRef* lot = map.lot(lotId);
                const auto building = buildings.find(lotId);
                if (special == SPECIAL_DRONE_HUB && building == buildings.end())
                {
                    continue;  
                }
                Site site{.lot = lotId, .special = special};
                const Vec3 centre = lotCentre(*lot);
                site.x = building != buildings.end() ? building->second.x : centre.x;
                site.z = building != buildings.end() ? building->second.z : centre.z;
                if (special == SPECIAL_DRONE_HUB)
                {
                    const Building& b = building->second;
                    const float padY = b.hubModel ? catalog::HUB_PAD_HEIGHT : b.height + 0.05f;
                    for (u32 pad = 0; pad < 4; ++pad)
                    {
                        const float dx = std::min(std::fabs(catalog::HUB_PADS[pad][0]), std::max(b.halfX - 2.0f, 0.0f));
                        const float dz = std::min(std::fabs(catalog::HUB_PADS[pad][1]), std::max(b.halfZ - 2.0f, 0.0f));
                        site.pads[pad] = {b.x + std::copysign(dx, catalog::HUB_PADS[pad][0]), padY,
                                          b.z + std::copysign(dz, catalog::HUB_PADS[pad][1])};
                    }
                }
                const auto kept =
                    std::ranges::find_if(old, [&](const Site& s) { return s.lot == lotId && s.special == special; });
                if (kept != old.end())
                {
                    site.stock = kept->stock;
                    site.capacity = kept->capacity;
                    site.incoming = kept->incoming;
                    site.claimed = kept->claimed;
                    site.queue = std::move(kept->queue);
                    kept->queue.clear();
                    site.shipped = kept->shipped;
                    site.received = kept->received;
                    kept->lot = NO_ID;  
                }
                else
                {
                    site.capacity = capacityOf(special);
                    site.stock = site.capacity / 2;  
                }
                m_sites.push_back(std::move(site));
            }
        }
        std::ranges::sort(m_sites, {}, &Site::lot);
         
        for (Site& gone : old)
        {
            for (const u32 id : gone.queue)
            {
                if (Package* package = find(id); package && !finished(package->state))
                {
                    setState(*package, PACKAGE_CANCELLED);
                }
            }
        }
        std::erase_if(m_supplyQueue,
                      [&](const u32 id)
                      {
                          Package* package = find(id);
                          if (!package || finished(package->state))
                          {
                              return true;
                          }
                          if (findSite(package->origin) && findSite(package->destination))
                          {
                              return false;
                          }
                          if (Site* store = findSite(package->destination))
                          {
                              store->incoming -= std::min(store->incoming, package->units);
                          }
                          setState(*package, PACKAGE_CANCELLED);
                          return true;
                      });
         
        m_stats.drones = 0;
        context.query<ecs::Read<CityDroneComponent>>()->forEachChunk(
            [&](Chunk* chunk)
            {
                const CityDroneComponent* rows = chunk->readColumn<CityDroneComponent>();
                m_stats.drones += static_cast<u32>(chunk->size());
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    if (Site* hub = findSite(rows[row].hub))
                    {
                        ++hub->drones;
                    }
                }
            });
        context.query<ecs::Read<CityCarComponent>>()->forEachChunk(
            [&](Chunk* chunk)
            {
                const CityCarComponent* rows = chunk->readColumn<CityCarComponent>();
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    if (isTruck(city, rows[row].id))
                    {
                        if (Site* base = findSite(rows[row].homeLot))
                        {
                            ++base->trucks;
                        }
                    }
                }
            });
        std::ranges::fill(m_stats.sites, 0u);
        for (const Site& site : m_sites)
        {
            ++m_stats.sites[site.special];
        }
    }

    void LogisticsService::end(const CityStateComponent& city, const CityMap& map, const u32 step)
    {
         
        const auto later = [](const std::pair<u32, u32>& a, const std::pair<u32, u32>& b) { return a > b; };
        while (!m_departures.empty() && m_departures.front().first <= step)
        {
            std::ranges::pop_heap(m_departures, later);
            const u32 id = m_departures.back().second;
            m_departures.pop_back();
            if (Package* package = find(id); package && package->state == PACKAGE_LOADED)
            {
                setState(*package, PACKAGE_IN_TRANSIT);
            }
        }
        const u32 day = dayOf(clockAt(city, step));
        if (day != m_stats.day)
        {
            m_stats.day = day;
            m_stats.deliveredToday = 0;
        }
        order(city, map, step);

        std::vector<LogisticsRequest> requests;
        {
            const std::scoped_lock hold(m_inboxLock);
            requests.swap(m_inbox);
        }
        std::ranges::sort(requests, [](const LogisticsRequest& a, const LogisticsRequest& b)
                          { return a.kind != b.kind ? a.kind < b.kind : a.id < b.id; });
        for (const LogisticsRequest& request : requests)
        {
            switch (request.kind)
            {
            case REQUEST_TRUCK_AT_BASE:
            case REQUEST_TRUCK_AT_STOP:
                if (request.id < m_trucks.size())
                {
                    TruckSlot& slot = m_trucks[request.id];
                    slot.answer = request.kind == REQUEST_TRUCK_AT_BASE ? planTour(city, map, request.id, request, step)
                                                                        : atStop(city, map, request.id, request, step);
                    slot.answered = true;
                }
                break;
            case REQUEST_DRONE_READY:
                if (request.id < m_drones.size())
                {
                    answerDrone(map, request, step);
                }
                break;
            default:
                if (Package* package = find(request.package); package && !finished(package->state) &&
                    package->vehicleKind == VEHICLE_DRONE && package->vehicle == request.id)
                {
                    setState(*package, PACKAGE_DELIVERED);
                    package->deliveredStep = step;
                    ++m_stats.deliveredToday;
                }
                break;
            }
        }
        m_stats.trucksOut = static_cast<u32>(std::ranges::count_if(m_trucks, &TruckSlot::onTour));
        m_stats.dronesFlying = m_flying;
        while (m_packages.size() > KEEP_PACKAGES && finished(m_packages.front().state))
        {
            m_packages.pop_front();
            ++m_firstId;
        }
    }

     

    void LogisticsService::order(const CityStateComponent& city, const CityMap& map, const u32 step)
    {
        const double perDay = m_stats.drones * PARCELS_PER_DRONE_DAY + city.trucks * FREIGHT_PER_TRUCK_DAY;
        if (perDay <= 0.0 || m_sites.empty())
        {
            return;
        }
        m_orderDue += perDay / std::max(city.daySteps, 1u);
        const RoadGraph& graph = map.graph();
        while (m_orderDue >= 1.0)
        {
            m_orderDue -= 1.0;
            const u64 key = city.seed ^ ORDER_SALT ^ (m_orderSerial++ * 0x9E3779B97F4A7C15ull);
            const bool home = unit(key) < HOME_ORDERS;
            const LotRef* consumer = map.reachable(map.pick(home ? CATEGORY_HOME : CATEGORY_SHOP, mix(key ^ 1)));
            if (!consumer)
            {
                ++m_stats.declined;
                continue;
            }
            const Vec3 drop = dropPoint(*consumer, graph);
            Site* hub = nullptr;
            Site* warehouse = nullptr;
            float hubDistance = DRONE_RANGE;
            float warehouseDistance = 1.0e30f;
            for (Site& site : m_sites)
            {
                const float d = distance(site.x, site.z, drop.x, drop.z);
                if (site.queue.size() >= MAX_QUEUE)
                {
                    continue;  
                }
                if (site.special == SPECIAL_DRONE_HUB && site.drones > 0 && d < hubDistance)
                {
                    hub = &site;
                    hubDistance = d;
                }
                else if (site.special == SPECIAL_WAREHOUSE && d < warehouseDistance)
                {
                    warehouse = &site;
                    warehouseDistance = d;
                }
            }
            const bool small = unit(key ^ 2) < (home ? HOME_PARCELS : SHOP_PARCELS);
            const bool byDrone = hub && (small || !warehouse || city.trucks == 0);
            Site* origin = byDrone ? hub : city.trucks > 0 ? warehouse : nullptr;
            if (!origin || origin->queue.size() >= MAX_QUEUE)
            {
                ++m_stats.declined;
                continue;
            }
            const Package& package =
                add(byDrone ? PACKAGE_PARCEL : PACKAGE_FREIGHT, origin->lot, consumer->id, 1, step);
            origin->queue.push_back(package.id);
        }
    }

     
    void LogisticsService::restock(const CityMap&, Site& store, const u32 step)
    {
        if (m_truckCount == 0 || store.stock + store.incoming >= store.capacity / 2)
        {
            return;
        }
        const Site* factory = nullptr;
        float best = 1.0e30f;
        for (const Site& site : m_sites)
        {
            const float d = distance(site.x, site.z, store.x, store.z);
            if (site.special == SPECIAL_FACTORY && d < best)
            {
                factory = &site;
                best = d;
            }
        }
        if (!factory)
        {
            return;
        }
        const u32 units = store.capacity - store.stock - store.incoming;
        const Package& package = add(PACKAGE_SUPPLY, factory->lot, store.lot, units, step);
        m_supplyQueue.push_back(package.id);
        store.incoming += units;
    }

     

    void LogisticsService::returnLoad(TruckSlot& slot)
    {
        for (const u32 id : slot.load)
        {
            Package* package = find(id);
            if (!package || finished(package->state))
            {
                continue;
            }
            package->vehicleKind = VEHICLE_NONE;
            if (package->kind == PACKAGE_SUPPLY)
            {
                setState(*package, PACKAGE_WAITING);
                m_supplyQueue.push_front(id);
                continue;
            }
            Site* origin = findSite(package->origin);
            if (!origin)
            {
                setState(*package, PACKAGE_CANCELLED);
                continue;
            }
            setState(*package, PACKAGE_WAITING);
            origin->queue.push_front(id);
            ++origin->stock;
        }
        slot.load.clear();
        for (u32 i = slot.next; i < slot.stops.size(); ++i)
        {
            const TourStop& stop = slot.stops[i];
            if (stop.action == STOP_PICKUP)
            {
                if (Package* package = find(stop.package); package && package->state == PACKAGE_WAITING)
                {
                    package->vehicleKind = VEHICLE_NONE;
                    m_supplyQueue.push_front(stop.package);
                }
            }
            else if (stop.action == STOP_LOAD)
            {
                if (Site* warehouse = findSite(stop.lot))
                {
                    warehouse->claimed -= std::min(warehouse->claimed, stop.package);
                }
            }
        }
        slot.stops.clear();
        slot.next = 0;
        slot.onTour = false;
    }

    TruckAnswer LogisticsService::driveTo(const CityStateComponent& city, const CityMap& map, const u32 index,
                                          const u32 purpose, const u32 lot, const LogisticsRequest& request,
                                          const u32 dwell) const
    {
        const LotRef* target = map.reachable(lot);
        if (!target)
        {
            return {.purpose = TRIP_IDLE, .lot = request.base, .dwell = TRUCK_IDLE_STEPS};
        }
        const RoadGraph& graph = map.graph();
        const u32 choice = static_cast<u32>(mix(city.seed ^ (static_cast<u64>(index) << 32 | lot)) % 2);
        u32 node = map.accessNode(*target, choice);
        if (graph.nodes[node].x == request.x && graph.nodes[node].z == request.z)
        {
            node = map.accessNode(*target, 1 - choice);
        }
        return {.purpose = purpose, .lot = lot, .x = graph.nodes[node].x, .z = graph.nodes[node].z, .dwell = dwell};
    }

    TruckAnswer LogisticsService::planTour(const CityStateComponent& city, const CityMap& map, const u32 index,
                                           const LogisticsRequest& request, const u32 step)
    {
        TruckSlot& slot = m_trucks[index];
        returnLoad(slot);
        const Site* base = findSite(request.base);
        const u32 idle =
            TRUCK_IDLE_STEPS + static_cast<u32>(mix(city.seed ^ index ^ (static_cast<u64>(step) << 20)) % 600);
        if (!base)
        {
            return {.purpose = TRIP_IDLE, .lot = request.base, .dwell = idle};
        }
         
        float fromX = base->x;
        float fromZ = base->z;
        u32 store = NO_ID;
        for (u32 i = 0; i < std::min<sizet>(m_supplyQueue.size(), SUPPLY_SCAN); ++i)
        {
            Package* package = find(m_supplyQueue[i]);
            if (!package || package->state != PACKAGE_WAITING || !findSite(package->origin) ||
                !findSite(package->destination))
            {
                continue;
            }
            package->vehicleKind = VEHICLE_TRUCK;
            package->vehicle = city.cars - city.trucks + index;
            slot.stops.push_back({package->origin, STOP_PICKUP, package->id});
            slot.stops.push_back({package->destination, STOP_DROP_SUPPLY, package->id});
            store = package->destination;
            const Site* to = findSite(store);
            fromX = to->x;
            fromZ = to->z;
            m_supplyQueue.erase(m_supplyQueue.begin() + i);
            break;
        }
         
        Site* warehouse = nullptr;
        float best = 1.0e30f;
        for (Site& site : m_sites)
        {
            if (site.special != SPECIAL_WAREHOUSE || site.stock == 0 || site.queue.size() <= site.claimed)
            {
                continue;
            }
            const float d = site.lot == store ? -1.0f : distance(site.x, site.z, fromX, fromZ);
            if (d < best)
            {
                warehouse = &site;
                best = d;
            }
        }
        if (warehouse)
        {
            const u32 capacity = truckCapacity(index);
            warehouse->claimed += capacity;
            slot.stops.push_back({warehouse->lot, STOP_LOAD, capacity});  
        }
        if (slot.stops.empty())
        {
            return {.purpose = TRIP_IDLE, .lot = request.base, .dwell = idle};
        }
        slot.onTour = true;
        return driveTo(city, map, index, TRIP_FREIGHT, slot.stops[0].lot, request, 60);
    }

    TruckAnswer LogisticsService::atStop(const CityStateComponent& city, const CityMap& map, const u32 index,
                                         const LogisticsRequest& request, const u32 step)
    {
        TruckSlot& slot = m_trucks[index];
        if (slot.next >= slot.stops.size() || slot.stops[slot.next].lot != request.lot)
        {
             
            return driveTo(city, map, index, TRIP_DEPOT, request.base, request, 60);
        }
        const u32 truck = city.cars - city.trucks + index;
        const TourStop stop = slot.stops[slot.next];
        Site* site = findSite(stop.lot);
        u32 dwell = TRUCK_LOAD_STEPS;
        const auto load = [&](Package& package)
        {
            setState(package, PACKAGE_LOADED);
            package.vehicleKind = VEHICLE_TRUCK;
            package.vehicle = truck;
            package.loadedStep = step;
            package.transitStep = step + dwell + 1;
            m_departures.emplace_back(package.transitStep, package.id);
            std::ranges::push_heap(m_departures, [](const auto& a, const auto& b) { return a > b; });
            slot.load.push_back(package.id);
        };
        switch (stop.action)
        {
        case STOP_PICKUP:
            if (Package* package = find(stop.package); package && package->state == PACKAGE_WAITING)
            {
                load(*package);
                if (site)
                {
                    site->shipped += package->units;
                }
            }
            break;
        case STOP_DROP_SUPPLY:
            if (Package* package = find(stop.package); package && !finished(package->state))
            {
                setState(*package, PACKAGE_DELIVERED);
                package->deliveredStep = step;
                std::erase(slot.load, package->id);
                if (site)
                {
                    site->stock = std::min(site->stock + package->units, site->capacity);
                    site->incoming -= std::min(site->incoming, package->units);
                    site->received += package->units;
                }
            }
            break;
        case STOP_LOAD:
            if (site)
            {
                site->claimed -= std::min(site->claimed, stop.package);
                 
                std::vector<u32> lots;
                std::deque<u32> left;
                u32 taken = 0;
                for (const u32 id : site->queue)
                {
                    Package* package = find(id);
                    if (!package || finished(package->state))
                    {
                        continue;
                    }
                    const bool known = std::ranges::find(lots, package->destination) != lots.end();
                    if (taken >= stop.package || site->stock == 0 || (!known && lots.size() >= TRUCK_MAX_STOPS) ||
                        !map.reachable(package->destination))
                    {
                        left.push_back(id);
                        continue;
                    }
                    if (!known)
                    {
                        lots.push_back(package->destination);
                    }
                    load(*package);
                    --site->stock;
                    ++site->shipped;
                    ++taken;
                }
                site->queue = std::move(left);
                restock(map, *site, step);
                 
                float x = site->x;
                float z = site->z;
                std::vector<TourStop> deliveries;
                while (!lots.empty())
                {
                    auto nearest = lots.begin();
                    float best = 1.0e30f;
                    for (auto it = lots.begin(); it != lots.end(); ++it)
                    {
                        const Vec3 at = lotCentre(*map.lot(*it));
                        const float d = distance(x, z, at.x, at.z);
                        if (d < best)
                        {
                            best = d;
                            nearest = it;
                        }
                    }
                    const Vec3 at = lotCentre(*map.lot(*nearest));
                    x = at.x;
                    z = at.z;
                    deliveries.push_back({*nearest, STOP_DELIVER, NO_PACKAGE});
                    lots.erase(nearest);
                }
                slot.stops.insert(slot.stops.begin() + slot.next + 1, deliveries.begin(), deliveries.end());
            }
            break;
        default:
            dwell = TRUCK_DELIVER_STEPS;
            std::erase_if(slot.load,
                          [&](const u32 id)
                          {
                              Package* package = find(id);
                              if (!package || package->kind != PACKAGE_FREIGHT || package->destination != stop.lot)
                              {
                                  return false;
                              }
                              if (!finished(package->state))
                              {
                                  setState(*package, PACKAGE_DELIVERED);
                                  package->deliveredStep = step;
                                  ++m_stats.deliveredToday;
                              }
                              return true;
                          });
            break;
        }
        ++slot.next;
         
        while (slot.next < slot.stops.size() && !map.reachable(slot.stops[slot.next].lot))
        {
            const TourStop& gone = slot.stops[slot.next];
            std::erase_if(slot.load,
                          [&](const u32 id)
                          {
                              Package* package = find(id);
                              const bool dropped = package &&
                                  (gone.action == STOP_DELIVER ? package->destination == gone.lot
                                                               : package->id == gone.package);
                              if (dropped && !finished(package->state))
                              {
                                  setState(*package, PACKAGE_CANCELLED);
                              }
                              return dropped;
                          });
            if (gone.action == STOP_PICKUP)
            {
                if (Package* package = find(gone.package); package && !finished(package->state))
                {
                    setState(*package, PACKAGE_CANCELLED);
                    if (Site* store = findSite(package->destination))
                    {
                        store->incoming -= std::min(store->incoming, package->units);
                    }
                }
            }
            ++slot.next;
        }
        if (slot.next < slot.stops.size())
        {
            return driveTo(city, map, index, TRIP_FREIGHT, slot.stops[slot.next].lot, request, dwell);
        }
        return driveTo(city, map, index, TRIP_DEPOT, request.base, request, dwell);
    }

     

    float LogisticsService::clearance(const float x0, const float z0, const float x1, const float z1) const
    {
        const float length = distance(x0, z0, x1, z1);
        const u32 samples = static_cast<u32>(length / 4.0f) + 1;
        float tallest = 0.0f;
        for (u32 i = 0; i <= samples; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(samples);
            const float x = x0 + (x1 - x0) * t;
            const float z = z0 + (z1 - z0) * t;
            if (x < 0.0f || z < 0.0f)
            {
                continue;
            }
            const u32 cx = static_cast<u32>(x / CELL);
            const u32 cz = static_cast<u32>(z / CELL);
            if (cx < m_size && cz < m_size)
            {
                tallest = std::max(tallest, m_heights[static_cast<sizet>(cz) * m_size + cx]);
            }
        }
        return tallest;
    }

    void LogisticsService::answerDrone(const CityMap& map, const LogisticsRequest& request, const u32 step)
    {
        DroneSlot& slot = m_drones[request.id];
        slot.answered = true;
        slot.package = NO_PACKAGE;
        Site* hub = findSite(request.lot);
        if (!hub || hub->special != SPECIAL_DRONE_HUB)
        {
            return;
        }
        while (!hub->queue.empty() && hub->stock > 0)
        {
            Package* package = find(hub->queue.front());
            hub->queue.pop_front();
            if (!package || finished(package->state))
            {
                continue;
            }
            const LotRef* consumer = map.reachable(package->destination);
            if (!consumer)
            {
                setState(*package, PACKAGE_CANCELLED);
                continue;
            }
            --hub->stock;
            ++hub->shipped;
            setState(*package, PACKAGE_LOADED);
            package->vehicleKind = VEHICLE_DRONE;
            package->vehicle = request.id;
            package->loadedStep = step;
            package->transitStep = step + 1 + DRONE_LOAD_STEPS;
            m_departures.emplace_back(package->transitStep, package->id);
            std::ranges::push_heap(m_departures, [](const auto& a, const auto& b) { return a > b; });
            const Vec3 drop = dropPoint(*consumer, map.graph());
            const float tallest = clearance(request.px, request.pz, drop.x, drop.z);
            slot.package = package->id;
            slot.lot = package->destination;
            slot.drop = drop;
            slot.altitude = droneAltitude(request.px, request.pz, drop.x, drop.z, tallest);
            slot.returnAltitude = droneAltitude(drop.x, drop.z, request.px, request.pz, tallest);
            slot.loadSteps = DRONE_LOAD_STEPS;
            restock(map, *hub, step);
            return;
        }
    }

     

    namespace
    {
         
        bool approach(float& value, const float target, const float travel)
        {
            if (std::fabs(target - value) <= travel)
            {
                value = target;
                return true;
            }
            value += target > value ? travel : -travel;
            return false;
        }

         
        bool flyTo(CityDroneComponent& drone, const float x, const float z, const float travel)
        {
            const float dx = x - drone.x;
            const float dz = z - drone.z;
            const float length = std::hypot(dx, dz);
            if (length <= travel)
            {
                drone.x = x;
                drone.z = z;
                return true;
            }
            drone.x += dx / length * travel;
            drone.z += dz / length * travel;
            drone.yaw = std::atan2(dx, dz);
            return false;
        }

        void stepDrone(CityDroneComponent& drone, DroneSlot& slot, std::vector<LogisticsRequest>& outbox,
                       const u32 step, const float dt)
        {
            switch (drone.phase)
            {
            case DRONE_CHARGING:
                if (step >= drone.phaseUntil)
                {
                    drone.phase = DRONE_WAITING;
                }
                break;
            case DRONE_WAITING:
                if (drone.package != NO_PACKAGE)
                {
                    if (step >= drone.phaseUntil)
                    {
                        drone.phase = DRONE_TAKEOFF;
                    }
                    break;
                }
                if (slot.answered)
                {
                    slot.answered = false;
                    slot.asked = false;
                    if (slot.package == NO_PACKAGE)
                    {
                        drone.phase = DRONE_CHARGING;
                        drone.phaseUntil = step + DRONE_RETRY_STEPS;
                        break;
                    }
                    drone.package = slot.package;
                    drone.targetLot = slot.lot;
                    drone.toX = slot.drop.x;
                    drone.toY = slot.drop.y;
                    drone.toZ = slot.drop.z;
                    drone.altitude = slot.altitude;
                    drone.returnAltitude = slot.returnAltitude;
                    drone.phaseUntil = step + slot.loadSteps;
                    drone.yaw = std::atan2(drone.toX - drone.x, drone.toZ - drone.z);
                    break;
                }
                if (!slot.asked)
                {
                    slot.asked = true;
                    outbox.push_back(
                        {.kind = REQUEST_DRONE_READY, .id = drone.id, .lot = drone.hub, .px = drone.x, .pz = drone.z});
                }
                break;
            case DRONE_TAKEOFF:
                if (approach(drone.y, drone.altitude, DRONE_CLIMB_SPEED * dt))
                {
                    drone.phase = DRONE_CRUISE;
                }
                break;
            case DRONE_CRUISE:
                if (flyTo(drone, drone.toX, drone.toZ, DRONE_CRUISE_SPEED * dt))
                {
                    drone.phase = DRONE_DESCENT;
                }
                break;
            case DRONE_DESCENT:
                if (approach(drone.y, drone.toY + DRONE_DROP_HEIGHT, DRONE_DESCENT_SPEED * dt))
                {
                    drone.phase = DRONE_DROP;
                    drone.phaseUntil = step + DRONE_DROP_STEPS;
                }
                break;
            case DRONE_DROP:
                if (step >= drone.phaseUntil)
                {
                    outbox.push_back({.kind = REQUEST_DRONE_DROPPED,
                                      .id = drone.id,
                                      .lot = drone.targetLot,
                                      .package = drone.package});
                    drone.dropped = drone.package;
                    drone.dropX = drone.x;
                    drone.dropY = drone.toY;
                    drone.dropZ = drone.z;
                    drone.dropStep = step;
                    drone.package = NO_PACKAGE;
                    ++drone.deliveries;
                    drone.phase = DRONE_CLIMB;
                }
                break;
            case DRONE_CLIMB:
                if (approach(drone.y, drone.returnAltitude, DRONE_CLIMB_SPEED * dt))
                {
                    drone.phase = DRONE_RETURN;
                }
                break;
            case DRONE_RETURN:
                if (flyTo(drone, drone.padX, drone.padZ, DRONE_CRUISE_SPEED * dt))
                {
                    drone.phase = DRONE_LANDING;
                }
                break;
            default:
                if (approach(drone.y, drone.padY, DRONE_DESCENT_SPEED * dt))
                {
                    const float flight = 2.0f * distance(drone.padX, drone.padZ, drone.toX, drone.toZ);
                    drone.phase = DRONE_CHARGING;
                    drone.phaseUntil =
                        step + DRONE_CHARGE_STEPS + static_cast<u32>(flight / DRONE_CRUISE_SPEED / dt / 3.0f);
                }
                break;
            }
        }

        bool sameRotation(const Quat& a, const Quat& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }

        void writeIfMoved(Chunk& chunk, const sizet row, TransformComponent& transform,
                          const TransformComponent& placed)
        {
            if (!(placed.position == transform.position) || !(placed.scale == transform.scale) ||
                !sameRotation(placed.rotation, transform.rotation))
            {
                transform = placed;
                chunk.markModified<TransformComponent>(row);
            }
        }
    }  

    void stepDrones(SystemContext& context, const CityStateComponent& city, LogisticsService& logistics, const u32 step,
                    const float dt)
    {
        static_assert(sizeof(CityDroneComponent) == 25 * 4, "padding would make the row comparison indeterminate");
        const bool models = (city.models & MODEL_DRONES) != 0;
        std::atomic<u32> flying{0};
        context.query<ecs::Write<CityDroneComponent>, ecs::Write<TransformComponent>>()->forEachChunkParallel(
            [&](Chunk* chunk, u32)
            {
                CityDroneComponent* rows = chunk->mutableColumn<CityDroneComponent>();
                TransformComponent* transforms = chunk->mutableColumn<TransformComponent>();
                std::vector<LogisticsRequest> outbox;
                u32 air = 0;
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    CityDroneComponent& drone = rows[row];
                    if (drone.id >= city.drones)
                    {
                        continue;
                    }
                    const CityDroneComponent before = drone;
                    stepDrone(drone, logistics.drone(drone.id), outbox, step, dt);
                    logistics.view(drone.id) = {.at = {drone.x, drone.y, drone.z},
                                                .yaw = drone.yaw,
                                                .carrying = drone.package != NO_PACKAGE,
                                                .dropped = drone.dropped != NO_PACKAGE,
                                                .drop = {drone.dropX, drone.dropY, drone.dropZ},
                                                .dropStep = drone.dropStep};
                    air += droneFlying(drone.phase) ? 1u : 0u;
                    if (std::memcmp(&before, &drone, sizeof(drone)) == 0)
                    {
                        continue;
                    }
                    chunk->markModified<CityDroneComponent>(row);
                    TransformComponent placed = transforms[row];
                    placeDrone(drone, placed, models);
                    writeIfMoved(*chunk, row, transforms[row], placed);
                }
                if (!outbox.empty())
                {
                    logistics.submit(outbox);
                }
                flying.fetch_add(air, std::memory_order_relaxed);
            });
        logistics.countFlying(flying.load(std::memory_order_relaxed));
    }

    void placeParcels(SystemContext& context, const CityStateComponent& city, const LogisticsService& logistics,
                      const u32 step)
    {
        const bool models = (city.models & MODEL_PARCELS) != 0;
        context.query<ecs::Read<CityParcelComponent>, ecs::Write<TransformComponent>>()->forEachChunkParallel(
            [&](Chunk* chunk, u32)
            {
                const CityParcelComponent* rows = chunk->readColumn<CityParcelComponent>();
                TransformComponent* transforms = chunk->mutableColumn<TransformComponent>();
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    const DroneView* view = logistics.findView(rows[row].drone);
                    if (!view)
                    {
                        continue;
                    }
                    writeIfMoved(*chunk, row, transforms[row], parcelPlace(*view, step, models));
                }
            });
    }
}  
