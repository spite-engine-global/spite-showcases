#pragma once

#include <array>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include "CityCatalog.hpp"
#include "CityModel.hpp"
#include "CityShowcase.hpp"
#include "ecs/EcsTypes.hpp"

namespace spite
{
    class SystemContext;
}

 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
namespace spite::city
{
    inline constexpr u32 PACKAGE_PARCEL = 0;  
    inline constexpr u32 PACKAGE_FREIGHT = 1;  
    inline constexpr u32 PACKAGE_SUPPLY = 2;  
    inline constexpr u32 PACKAGE_KINDS = 3;
    inline constexpr const char* PACKAGE_KIND_NAMES[PACKAGE_KINDS] = {"parcel", "freight", "supply"};

    inline constexpr u32 PACKAGE_WAITING = 0;
    inline constexpr u32 PACKAGE_LOADED = 1;
    inline constexpr u32 PACKAGE_IN_TRANSIT = 2;
    inline constexpr u32 PACKAGE_DELIVERED = 3;
    inline constexpr u32 PACKAGE_CANCELLED = 4;  
    inline constexpr u32 PACKAGE_STATES = 5;
    inline constexpr const char* PACKAGE_STATE_NAMES[PACKAGE_STATES] = {"waiting", "loaded", "inTransit", "delivered",
                                                                        "cancelled"};

    inline constexpr u32 VEHICLE_NONE = 0;
    inline constexpr u32 VEHICLE_TRUCK = 1;
    inline constexpr u32 VEHICLE_DRONE = 2;

     
    inline constexpr u32 STOP_PICKUP = 0;  
    inline constexpr u32 STOP_DROP_SUPPLY = 1;  
    inline constexpr u32 STOP_LOAD = 2;  
    inline constexpr u32 STOP_DELIVER = 3;  
    inline constexpr const char* STOP_NAMES[] = {"pick up", "drop supply", "load", "deliver"};

    inline constexpr u32 NO_PACKAGE = 0;  

     
    inline constexpr u32 TRUCK_LOAD_STEPS = 1200;  
    inline constexpr u32 TRUCK_DELIVER_STEPS = 600;  
    inline constexpr u32 TRUCK_IDLE_STEPS = 900;  
    inline constexpr u32 TRUCK_MAX_STOPS = 6;  
    inline constexpr u32 DRONE_LOAD_STEPS = 90;  
    inline constexpr u32 DRONE_DROP_STEPS = 90;  
    inline constexpr u32 DRONE_RETRY_STEPS = 600;  
    inline constexpr u32 DRONE_CHARGE_STEPS = 360;  
    inline constexpr u32 PARCEL_LINGER_STEPS = 1800;  
    inline constexpr float DRONE_CRUISE_SPEED = 18.0f;  
    inline constexpr float DRONE_CLIMB_SPEED = 5.0f;
    inline constexpr float DRONE_DESCENT_SPEED = 4.0f;
    inline constexpr float DRONE_DROP_HEIGHT = 1.5f;  
    inline constexpr float DRONE_RANGE = 1600.0f;  
     
     
    inline constexpr u32 DRONE_SECTORS = 8;
    inline constexpr float DRONE_BASE_ALTITUDE = 50.0f;
    inline constexpr float DRONE_LANE_GAP = 10.0f;
    inline constexpr float DRONE_CLEARANCE = 15.0f;
    inline constexpr u32 WAREHOUSE_CAPACITY = 80;  
    inline constexpr u32 HUB_CAPACITY = 40;
    inline constexpr u32 MAX_QUEUE = 48;  
     
    inline constexpr float PARCELS_PER_DRONE_DAY = 2.0f;
    inline constexpr float FREIGHT_PER_TRUCK_DAY = 16.0f;
    inline constexpr float BACKLOG_DAYS = 0.15f;  

    struct Package
    {
        u32 id = NO_PACKAGE;
        u32 kind = PACKAGE_PARCEL;
        u32 state = PACKAGE_WAITING;
        u32 origin = 0;  
        u32 destination = 0;  
        u32 units = 1;
        u32 vehicleKind = VEHICLE_NONE;
        u32 vehicle = 0;  
        u32 orderedStep = 0;
        u32 loadedStep = 0;
        u32 transitStep = 0;
        u32 deliveredStep = 0;
    };

     
     
    struct Site
    {
        u32 lot = 0;
        u32 special = SPECIAL_NONE;
        float x = 0.0f;  
        float z = 0.0f;
        u32 stock = 0;
        u32 capacity = 0;
        u32 incoming = 0;  
        u32 claimed = 0;  
        std::deque<u32> queue;  
        u32 drones = 0;  
        u32 trucks = 0;
        u32 shipped = 0;
        u32 received = 0;
        std::array<Vec3, 4> pads{};
    };

    struct TourStop
    {
        u32 lot = 0;
        u32 action = STOP_DELIVER;
        u32 package = NO_PACKAGE;  
    };

     
     
    struct TruckAnswer
    {
        u32 purpose = TRIP_IDLE;
        u32 lot = 0;
        u32 x = 0;
        u32 z = 0;
        u32 dwell = 0;
    };

     
     
    struct TruckSlot
    {
        std::vector<TourStop> stops;
        u32 next = 0;  
        std::vector<u32> load;  
        bool onTour = false;
        bool asked = false;
        bool answered = false;
        TruckAnswer answer;
    };

     
     
    struct DroneSlot
    {
        bool asked = false;
        bool answered = false;
        u32 package = NO_PACKAGE;
        u32 lot = 0;
        Vec3 drop{0.0f};
        float altitude = 0.0f;
        float returnAltitude = 0.0f;
        u32 loadSteps = 0;
    };

    inline constexpr u32 REQUEST_TRUCK_AT_BASE = 0;  
    inline constexpr u32 REQUEST_TRUCK_AT_STOP = 1;  
    inline constexpr u32 REQUEST_DRONE_READY = 2;  
    inline constexpr u32 REQUEST_DRONE_DROPPED = 3;  

    struct LogisticsRequest
    {
        u32 kind = REQUEST_TRUCK_AT_BASE;
        u32 id = 0;  
        u32 lot = 0;  
        u32 x = 0;  
        u32 z = 0;
        u32 package = NO_PACKAGE;
        u32 base = 0;  
        float px = 0.0f;  
        float pz = 0.0f;
    };

     
    struct DroneView
    {
        Vec3 at{0.0f};
        float yaw = 0.0f;
        bool carrying = false;
        bool dropped = false;
        Vec3 drop{0.0f};
        u32 dropStep = 0;
    };

    struct LogisticsStats
    {
        u32 counts[PACKAGE_KINDS][PACKAGE_STATES] = {};
        u64 ordered = 0;
        u64 declined = 0;  
        u32 deliveredToday = 0;
        u32 day = 0;
        u32 trucksOut = 0;  
        u32 drones = 0;  
        u32 dronesFlying = 0;
        u32 sites[SPECIAL_KINDS] = {};
    };

    class LogisticsService
    {
    public:
        static constexpr const char* SPITE_SERVICE_NAME = "CityLogistics";

         
         
         
         
        void begin(SystemContext& context, const CityStateComponent& city, const model::CityMap& map, bool mapChanged);
         
        void end(const CityStateComponent& city, const model::CityMap& map, u32 step);

         
        TruckSlot& truck(const u32 index) { return m_trucks[index]; }
        DroneSlot& drone(const u32 id) { return m_drones[id]; }
        DroneView& view(const u32 id) { return m_views[id]; }
        const DroneView* findView(const u32 id) const { return id < m_views.size() ? &m_views[id] : nullptr; }
        void submit(std::span<const LogisticsRequest> requests);
        void countFlying(const u32 drones) { m_flying += drones; }

         
        const TruckSlot* findTruck(const u32 index) const
        {
            return index < m_trucks.size() ? &m_trucks[index] : nullptr;
        }
        const Package* package(u32 id) const;
        const Site* site(u32 lot) const;
        const std::vector<Site>& sites() const { return m_sites; }
         
        std::vector<const Package*> recent(u32 count) const;
        const LogisticsStats& stats() const { return m_stats; }
        u32 epochStart() const { return m_epochStart; }

    private:
        Package* find(u32 id);
        Site* findSite(u32 lot);
        Package& add(u32 kind, u32 origin, u32 destination, u32 units, u32 step);
        void setState(Package& package, u32 state);
        void rebuildSites(SystemContext& context, const CityStateComponent& city, const model::CityMap& map);
        void order(const CityStateComponent& city, const model::CityMap& map, u32 step);
        void restock(const model::CityMap& map, Site& store, u32 step);
        void returnLoad(TruckSlot& slot);
        TruckAnswer planTour(const CityStateComponent& city, const model::CityMap& map, u32 index,
                             const LogisticsRequest& request, u32 step);
        TruckAnswer atStop(const CityStateComponent& city, const model::CityMap& map, u32 index,
                           const LogisticsRequest& request, u32 step);
        TruckAnswer driveTo(const CityStateComponent& city, const model::CityMap& map, u32 index, u32 purpose, u32 lot,
                            const LogisticsRequest& request, u32 dwell) const;
        void answerDrone(const model::CityMap& map, const LogisticsRequest& request, u32 step);
        float clearance(float x0, float z0, float x1, float z1) const;

        std::mutex m_inboxLock;
        std::vector<LogisticsRequest> m_inbox;

         
        u64 m_seed = 0;
        u32 m_size = 0;
        u32 m_cars = 0;
        u32 m_truckCount = 0;
        u32 m_droneCount = 0;
        u32 m_lastStep = 0;
        bool m_started = false;
        u32 m_epochStart = 0;
        double m_orderDue = 0.0;  
        u64 m_orderSerial = 0;  

        std::deque<Package> m_packages;  
        u32 m_firstId = 1;
        std::deque<u32> m_supplyQueue;  
        std::vector<std::pair<u32, u32>> m_departures;  
        std::vector<Site> m_sites;  
        std::vector<float> m_heights;  
        std::vector<TruckSlot> m_trucks;
        std::vector<DroneSlot> m_drones;
        std::vector<DroneView> m_views;
        u32 m_flying = 0;
        LogisticsStats m_stats;
    };
}  

namespace spite::city::model
{
     

     
    inline u32 truckBase(const CityMap& map, const u32 index)
    {
        const std::vector<u32>& depots = map.specialLots(SPECIAL_TRUCK_DEPOT);
        const std::vector<u32>& warehouses = map.specialLots(SPECIAL_WAREHOUSE);
        const sizet total = depots.size() + warehouses.size();
        if (total == 0)
        {
            return NO_ID;
        }
        const sizet at = index % total;
        return at < depots.size() ? depots[at] : warehouses[at - depots.size()];
    }

     
     
    inline ParkSpot parkSpotOf(const CityStateComponent& city, const CityMap& map, const u32 id)
    {
        if (!isTruck(city, id))
        {
            return {.index = static_cast<u32>(mix(id ^ 0xBA2Cull) >> 40)};
        }
        const u32 bases =
            static_cast<u32>(map.specialLots(SPECIAL_TRUCK_DEPOT).size() + map.specialLots(SPECIAL_WAREHOUSE).size());
        return {.index = truckIndex(city, id) / std::max(bases, 1u), .jitter = 0.0f};
    }

     

     
    inline u32 droneSector(const float x0, const float z0, const float x1, const float z1)
    {
        const float angle = std::atan2(x1 - x0, z1 - z0) + 3.1415927f;  
        return std::min(static_cast<u32>(angle / (6.2831853f / static_cast<float>(DRONE_SECTORS))), DRONE_SECTORS - 1);
    }

     
     
    inline float droneAltitude(const float x0, const float z0, const float x1, const float z1, const float tallest)
    {
        float altitude = DRONE_BASE_ALTITUDE + static_cast<float>(droneSector(x0, z0, x1, z1)) * DRONE_LANE_GAP;
        while (altitude < tallest + DRONE_CLEARANCE)
        {
            altitude += static_cast<float>(DRONE_SECTORS) * DRONE_LANE_GAP;
        }
        return altitude;
    }

     
    inline Vec3 dropPoint(const LotRef& lot, const RoadGraph& graph)
    {
        const u32 axis = lot.edge != NO_ID ? graph.edges[lot.edge].axis : 0;
        const float along = static_cast<float>(axis == 0 ? lot.x0 + lot.x1 : lot.z0 + lot.z1) * 0.5f * CELL;
        const float boundary =
            static_cast<float>(lot.side > 0 ? (axis == 0 ? lot.z0 : lot.x0) : (axis == 0 ? lot.z1 : lot.x1)) * CELL;
        const float across = boundary - static_cast<float>(lot.side) * 1.5f;
        return axis == 0 ? Vec3(along, SIDEWALK_HEIGHT, across) : Vec3(across, SIDEWALK_HEIGHT, along);
    }

    inline bool droneFlying(const u32 phase) { return phase >= DRONE_TAKEOFF && phase <= DRONE_LANDING; }

    inline bool droneCruising(const u32 phase) { return phase == DRONE_CRUISE || phase == DRONE_RETURN; }

     
    inline void placeDrone(const CityDroneComponent& drone, TransformComponent& transform, const bool model = false)
    {
        transform.rotation = yawRotation(drone.yaw);
        if (model)
        {
            transform.position = {drone.x, drone.y, drone.z};
            transform.scale = Vec3(1.0f);
            return;
        }
        transform.position = {drone.x, drone.y + 0.3f, drone.z};
        transform.scale = {0.6f, 0.3f, 0.6f};
    }

    constexpr float PARCEL_PARK_Y = -100000.0f;

     
     
    inline TransformComponent parcelPlace(const DroneView& view, const u32 step, const bool model)
    {
        const float lift = model ? 0.0f : catalog::PACKAGE_SIZE * 0.5f;
        const Vec3 scale = model ? Vec3(1.0f) : Vec3(catalog::PACKAGE_SIZE * 0.5f);
        if (view.carrying)
        {
            return {.position = {view.at.x,
                                 view.at.y + catalog::DRONE_PACKAGE_ANCHOR - catalog::PACKAGE_SIZE * 0.5f + lift,
                                 view.at.z},
                    .rotation = yawRotation(view.yaw),
                    .scale = scale};
        }
        if (view.dropped && step < view.dropStep + PARCEL_LINGER_STEPS)
        {
            return {.position = {view.drop.x, view.drop.y + lift, view.drop.z}, .scale = scale};
        }
        return {.position = {0.0f, PARCEL_PARK_Y, 0.0f}, .scale = scale};
    }
}  

namespace spite::city
{
     
    void stepDrones(SystemContext& context, const CityStateComponent& city, LogisticsService& logistics, u32 step,
                    float dt);
     
    void placeParcels(SystemContext& context, const CityStateComponent& city, const LogisticsService& logistics,
                      u32 step);
}  
