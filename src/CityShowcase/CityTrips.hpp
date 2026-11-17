#pragma once

#include <algorithm>
#include <cmath>

#include "CityModel.hpp"

 
 
 
namespace spite::city::model
{
    constexpr u64 SCHEDULE_SALT = 0x5C4ED0ull;
    constexpr u64 HOME_SALT = 0x40E5ull;
    constexpr float MINUTE = 1.0f / 1440.0f;  
    constexpr float MORNING_FROM = 6.5f / 24.0f;  
    constexpr float MORNING_SPAN = 2.5f / 24.0f;
    constexpr float EVENING_FROM = 16.0f / 24.0f;  
    constexpr float EVENING_SPAN = 3.0f / 24.0f;
    constexpr float SHOP_CHANCE = 0.3f;  
    constexpr float MIN_PARK = 10.0f * MINUTE;  
    constexpr u32 COMMUTE_CELLS = 130;  
    constexpr u32 SHOP_CELLS = 160;  

     
    inline float morningTime(const u64 seed, const u32 id, const u32 day)
    {
        return MORNING_FROM + MORNING_SPAN * draw(seed ^ SCHEDULE_SALT ^ 1, id, day);
    }

    inline float eveningTime(const u64 seed, const u32 id, const u32 day)
    {
        return EVENING_FROM + EVENING_SPAN * draw(seed ^ SCHEDULE_SALT ^ 2, id, day);
    }

    inline bool eveningShopping(const u64 seed, const u32 id, const u32 day)
    {
        return draw(seed ^ SCHEDULE_SALT ^ 3, id, day) < SHOP_CHANCE;
    }

     
    inline float shopDwell(const u64 seed, const u32 id, const u32 day)
    {
        return (20.0f + 40.0f * draw(seed ^ SCHEDULE_SALT ^ 4, id, day)) * MINUTE;
    }

     

    inline double clockAt(const CityStateComponent& city, const u32 step)
    {
        return static_cast<double>(city.dayStart) + static_cast<double>(step) / std::max(city.daySteps, 1u);
    }

    inline u32 dayOf(const double clock) { return static_cast<u32>(std::max(std::floor(clock), 0.0)); }

     
    inline u32 stepAtClock(const CityStateComponent& city, const double clock, const u32 after)
    {
        const double steps = std::ceil((clock - static_cast<double>(city.dayStart)) * std::max(city.daySteps, 1u));
        return std::max(static_cast<u32>(std::clamp(steps, 0.0, 4.0e9)), after + 1);
    }

    inline u32 stepsOfDay(const CityStateComponent& city, const float span)
    {
        return std::max(static_cast<u32>(std::lround(static_cast<double>(span) * std::max(city.daySteps, 1u))), 1u);
    }

     

    inline u32 homeLotOf(const CityMap& map, const u32 lot, const u32 id, const u64 salt)
    {
        return map.reachable(lot) ? lot : map.pick(CATEGORY_HOME, mix(map.seed() ^ HOME_SALT ^ salt ^ id));
    }

    inline u32 nearLot(const CityMap& map, const u32 category, const u32 nearLotId, const u32 radius, const u64 key,
                       const bool weighted = false)
    {
        const LotRef* nearby = map.lot(nearLotId);
        return nearby ? map.pickNear(category, nearby->cx, nearby->cz, radius, key, weighted) : map.pick(category, key);
    }

    inline u32 workLotOf(const CityMap& map, const u32 lot, const u32 home, const u32 id, const u64 salt)
    {
        return map.reachable(lot)
            ? lot
            : nearLot(map, CATEGORY_WORK, home, COMMUTE_CELLS, mix(map.seed() ^ HOME_SALT ^ 1 ^ salt ^ id), true);
    }

     
     
    inline u32 tripNode(const CityMap& map, const u32 lotId, const u32 id, const u32 trip, const u32 avoid)
    {
        const LotRef* lot = map.reachable(lotId);
        if (!lot)
        {
            return NO_ID;
        }
        const u32 choice =
            static_cast<u32>(mix(map.seed() ^ SCHEDULE_SALT ^ 5 ^ (static_cast<u64>(id) << 32 | trip)) % 2);
        const u32 node = map.accessNode(*lot, choice);
        return node == avoid ? map.accessNode(*lot, 1 - choice) : node;
    }

     
     
    struct Trip
    {
        u32 purpose = TRIP_WORK;
        u32 targetLot = 0;
        u32 destX = 0;
        u32 destZ = 0;
        u32 departStep = 0;
        u32 homeLot = 0;
        u32 workLot = 0;
    };

     
     
     
     
    inline Trip nextTrip(const CityCarComponent& car, const CityStateComponent& city, const CityMap& map,
                         const u32 step)
    {
        const double now = clockAt(city, step);
        const u32 day = dayOf(now);
        Trip trip;
        trip.homeLot = homeLotOf(map, car.homeLot, car.id, car.trips);
        trip.workLot = workLotOf(map, car.workLot, trip.homeLot, car.id, car.trips);
        double depart = now;
        if (car.purpose == TRIP_WORK)
        {
            depart = std::max(day + static_cast<double>(eveningTime(city.seed, car.id, day)),
                              now + static_cast<double>(MIN_PARK));
            trip.purpose = eveningShopping(city.seed, car.id, day) ? TRIP_SHOP : TRIP_HOME;
            trip.targetLot = trip.homeLot;
            if (trip.purpose == TRIP_SHOP)
            {
                trip.targetLot =
                    nearLot(map, CATEGORY_SHOP, trip.homeLot, SHOP_CELLS,
                            mix(city.seed ^ SCHEDULE_SALT ^ 6 ^ (static_cast<u64>(car.id) << 32 | car.trips)));
            }
        }
        else if (car.purpose == TRIP_SHOP)
        {
            depart = now + static_cast<double>(shopDwell(city.seed, car.id, day));
            trip.purpose = TRIP_HOME;
            trip.targetLot = trip.homeLot;
        }
        else
        {
            depart = day + static_cast<double>(morningTime(city.seed, car.id, day));
            if (depart < now + static_cast<double>(MIN_PARK))
            {
                depart = day + 1 + static_cast<double>(morningTime(city.seed, car.id, day + 1));
            }
            trip.purpose = TRIP_WORK;
            trip.targetLot = trip.workLot;
        }
        const RoadGraph& graph = map.graph();
        const u32 node = tripNode(map, trip.targetLot, car.id, car.trips, graph.nodeAt(car.destX, car.destZ));
        trip.destX = node != NO_ID ? graph.nodes[node].x : car.destX;
        trip.destZ = node != NO_ID ? graph.nodes[node].z : car.destZ;
        trip.departStep = stepAtClock(city, depart, step);
        return trip;
    }

     
    inline void assignTrip(CityCarComponent& car, const Trip& trip)
    {
        car.purpose = trip.purpose;
        car.targetLot = trip.targetLot;
        car.destX = trip.destX;
        car.destZ = trip.destZ;
        car.parkUntil = trip.departStep;
        car.homeLot = trip.homeLot;
        car.workLot = trip.workLot;
    }

     
     
     
     
     

    constexpr u32 ERRAND_CELLS = 8;  
    constexpr u32 DRIVEN_CELLS = 60;  
    constexpr u32 WORK_WALK_CELLS = 10;  
    constexpr u32 LUNCH_CELLS = 16;
    constexpr u32 CAR_WALK_CELLS = 4;  
    constexpr float WORKER_SHARE = 0.65f;
    constexpr float CAR_OUTING_SHARE = 0.45f;

     
     
     
    constexpr float LATE_FROM = 0.5f / 24.0f;
    constexpr float LATE_TO = 5.5f / 24.0f;
    constexpr float NIGHT_OWL_SHARE = 0.12f;

    inline bool lateWalkEnds(const CityStateComponent& city, const u32 id, const u32 step)
    {
        return inWindow(timeOfDay(city, step), LATE_FROM, LATE_TO) &&
            draw(city.seed ^ SCHEDULE_SALT ^ PED_SALT ^ 47, id, dayOf(clockAt(city, step))) >= NIGHT_OWL_SHARE;
    }

    inline bool isWorker(const u64 seed, const u32 id)
    {
        return draw(seed ^ SCHEDULE_SALT ^ PED_SALT ^ 11, id, 0) < WORKER_SHARE;
    }

     
     
    inline u32 pedWorkLot(const CityMap& map, const u32 home, const u32 id, const u64 seed)
    {
        const u64 key = mix(seed ^ SCHEDULE_SALT ^ PED_SALT ^ 12 ^ id);
        return map.pickWeighted(CATEGORY_WORK, key);
    }

     
     
     
    struct Walk
    {
        u32 purpose = TRIP_HOME;
        u32 targetLot = NO_LOT;
        u32 dwellUntil = 0;
        u32 homeLot = 0;
        u32 workLot = NO_LOT;
        u32 carLot = NO_LOT;
        u32 relocateLot = NO_LOT;
    };

    enum Outing : u32
    {
        OUTING_COMMUTE = 0,
        OUTING_ERRAND_MORNING,
        OUTING_ERRAND_AFTERNOON,
        OUTING_STROLL,
        OUTINGS
    };

     
     
    inline double outingStart(const u64 seed, const u32 id, const u32 day, const Outing kind)
    {
        constexpr float FROM[OUTINGS] = {7.0f / 24.0f, 8.0f / 24.0f, 12.0f / 24.0f, 16.0f / 24.0f};
        constexpr float SPAN[OUTINGS] = {2.0f / 24.0f, 4.0f / 24.0f, 6.5f / 24.0f, 6.0f / 24.0f};
        constexpr float CHANCE[OUTINGS] = {1.0f, 0.95f, 0.9f, 0.85f};
        const u64 base = seed ^ SCHEDULE_SALT ^ PED_SALT;
        if (draw(base ^ (30 + kind), id, day) >= CHANCE[kind])
        {
            return -1.0;
        }
        return static_cast<double>(day) + FROM[kind] + SPAN[kind] * draw(base ^ (20 + kind), id, day);
    }

    inline u32 carLotNear(const CityMap& map, const u32 target, const u64 key)
    {
        const LotRef* at = map.lot(target);
        if (!at)
        {
            return NO_LOT;
        }
        for (const u32 category : {CATEGORY_SHOP, CATEGORY_HOME, CATEGORY_WORK})
        {
            const std::optional<u32> found = map.findNear(category, at->cx, at->cz, CAR_WALK_CELLS, key);
            if (found && *found != target)
            {
                return *found;
            }
        }
        return NO_LOT;
    }

    inline Walk nextWalk(const CityPedestrianComponent& ped, const CityStateComponent& city, const CityMap& map,
                         const u32 step)
    {
        const u64 base = city.seed ^ SCHEDULE_SALT ^ PED_SALT;
        const double now = clockAt(city, step);
        const u32 day = dayOf(now);
        const auto dwell = [&](const float from, const float to, const u64 key)
        { return step + stepsOfDay(city, (from + (to - from) * draw(base ^ key, ped.id, ped.legs)) * MINUTE); };
        const auto at = [&](const double clock) { return stepAtClock(city, clock, step); };
        Walk walk;
        walk.homeLot = homeLotOf(map, ped.homeLot, ped.id, PED_SALT ^ ped.legs);
        walk.workLot = map.reachable(ped.workLot) ? ped.workLot : pedWorkLot(map, walk.homeLot, ped.id, city.seed);
        walk.carLot = map.reachable(ped.carLot) ? ped.carLot : NO_LOT;
        const auto goHome = [&]
        {
            walk.purpose = walk.carLot != NO_LOT ? TRIP_CAR : TRIP_HOME;
            walk.targetLot = walk.carLot != NO_LOT ? walk.carLot : walk.homeLot;
        };
        const auto leaveWork = [&]
        {
            goHome();
            walk.dwellUntil =
                at(std::max(day + (16.5 + 2.0 * draw(base ^ 43, ped.id, day)) / 24.0, now + 10.0 * MINUTE));
            return walk;
        };
        switch (ped.purpose)
        {
        case TRIP_SHOP:
        case TRIP_STROLL:
            goHome();
            walk.dwellUntil =
                dwell(ped.purpose == TRIP_SHOP ? 5.0f : 10.0f, ped.purpose == TRIP_SHOP ? 20.0f : 35.0f, 40);
            return walk;
        case TRIP_WORK:
            {
                const double tod = now - day;
                if (tod < 13.75 / 24.0)
                {
                    const LotRef* work = map.lot(walk.workLot);
                    const u64 key = mix(base ^ 41 ^ (static_cast<u64>(ped.id) << 32 | day));
                    const std::optional<u32> shop =
                        work ? map.findNear(CATEGORY_SHOP, work->cx, work->cz, LUNCH_CELLS, key) : std::nullopt;
                    if (shop)
                    {
                        walk.purpose = TRIP_LUNCH;
                        walk.targetLot = *shop;
                        walk.dwellUntil =
                            at(std::max(day + (11.75 + 1.5 * draw(base ^ 42, ped.id, day)) / 24.0, now + MINUTE));
                        return walk;
                    }
                }
                return leaveWork();
            }
        case TRIP_BACK:
            return leaveWork();
        case TRIP_LUNCH:
            walk.purpose = TRIP_BACK;
            walk.targetLot = walk.workLot;
            walk.dwellUntil = dwell(10.0f, 25.0f, 44);
            return walk;
        default:
            break;
        }
         
        if (ped.purpose == TRIP_CAR)
        {
            walk.relocateLot = walk.homeLot;
            walk.carLot = NO_LOT;
        }
        const bool worker = isWorker(city.seed, ped.id);
        double bestStart = 1.0e18;
        Outing best = OUTINGS;
        u32 bestDay = day;
        for (u32 d = day; d <= day + 1 && best == OUTINGS; ++d)
        {
            for (u32 kind = 0; kind < OUTINGS; ++kind)
            {
                if ((kind == OUTING_COMMUTE) != worker && kind != OUTING_STROLL)
                {
                    continue;
                }
                const double start = outingStart(city.seed, ped.id, d, static_cast<Outing>(kind));
                if (start >= now + MINUTE && start < bestStart)
                {
                    bestStart = start;
                    best = static_cast<Outing>(kind);
                    bestDay = d;
                }
            }
        }
        if (best == OUTINGS)
        {
            walk.purpose = TRIP_HOME;
            walk.targetLot = walk.homeLot;
            walk.dwellUntil = at(now + 1.0);
            return walk;
        }
        const LotRef* home = map.lot(walk.homeLot);
        const u64 key = mix(base ^ 45 ^ (static_cast<u64>(ped.id) << 32 | bestDay * 8 + best));
        bool byCar = draw(base ^ 46, ped.id, bestDay * 8 + best) < CAR_OUTING_SHARE;
        if (best == OUTING_COMMUTE)
        {
            const LotRef* work = map.lot(walk.workLot);
            byCar = byCar ||
                (home && work &&
                 (home->cx > work->cx ? home->cx - work->cx : work->cx - home->cx) +
                         (home->cz > work->cz ? home->cz - work->cz : work->cz - home->cz) >
                     WORK_WALK_CELLS);
            walk.purpose = TRIP_WORK;
            walk.targetLot = walk.workLot;
        }
        else
        {
            std::optional<u32> shop = home
                ? map.findNear(CATEGORY_SHOP, home->cx, home->cz, byCar ? DRIVEN_CELLS : ERRAND_CELLS, key)
                : std::nullopt;
            if (!shop && !byCar && home)
            {
                byCar = true;  
                shop = map.findNear(CATEGORY_SHOP, home->cx, home->cz, DRIVEN_CELLS, key);
            }
            walk.purpose = best == OUTING_STROLL ? TRIP_STROLL : TRIP_SHOP;
            walk.targetLot = shop ? *shop : home ? map.nearest(CATEGORY_SHOP, home->cx, home->cz) : NO_LOT;
        }
        walk.carLot = byCar ? carLotNear(map, walk.targetLot, key) : NO_LOT;
        if (walk.carLot != NO_LOT)
        {
            walk.relocateLot = walk.carLot;
        }
        walk.dwellUntil = at(bestStart);
        return walk;
    }

     
    inline void relocatePedestrian(CityPedestrianComponent& ped, const CityMap& map, const u32 lotId)
    {
        const LotRef* lot = map.reachable(lotId);
        if (!lot)
        {
            return;
        }
        const u32 node = map.accessNode(*lot, static_cast<u32>(mix(ped.id ^ 0xACCull) % 2));
        const GraphNode& n = map.graph().nodes[node];
        ped.x = ped.toX = n.x;
        ped.z = ped.toZ = n.z;
        ped.corner = ped.toCorner = map.accessCorner(*lot, node);
        ped.progress = 0.0f;
    }

    inline void assignWalk(CityPedestrianComponent& ped, const Walk& walk, const CityMap& map)
    {
        ped.purpose = walk.purpose;
        ped.targetLot = walk.targetLot;
        ped.dwellUntil = walk.dwellUntil;
        ped.homeLot = walk.homeLot;
        ped.workLot = walk.workLot;
        ped.carLot = walk.carLot;
        if (walk.relocateLot != NO_LOT)
        {
            relocatePedestrian(ped, map, walk.relocateLot);
        }
    }
}  
