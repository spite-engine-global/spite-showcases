#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

#include "CityModel.hpp"
#include "CityShowcase.hpp"
#include "ecs/EcsTypes.hpp"

namespace spite
{
    class SystemContext;
}

 
 
 
 
 
 
 
namespace spite::city
{
     
    inline constexpr u32 ROUTE_DELAY_STEPS = 4;

    enum class RouteKind : u8
    {
        Car,
        Pedestrian
    };

     
     
    struct RouteCosts
    {
        std::shared_ptr<const model::RoadGraph> graph;
        std::vector<u16> edgeCars;
    };

     
     
    struct RouteRequest
    {
        Entity entity = Entity::undefined();
        RouteKind kind = RouteKind::Car;
        u32 id = 0;
        u32 version = 0;
        u32 from = 0;
        u32 heading = 0;
        u32 to = 0;
        u32 issuedStep = 0;
        u32 epoch = 0;
        u64 issuedNs = 0;
        std::shared_ptr<const RouteCosts> costs;

        u32 dueStep() const { return issuedStep + ROUTE_DELAY_STEPS; }
    };

     
     
     
     
    struct RouteSlot
    {
        std::vector<u32> active;
        std::vector<u32> incoming;
        u32 expected = 0;
        u32 incomingVersion = 0;
        bool incomingFailed = false;
    };

    struct PathStats
    {
        u32 workers = 0;
        u64 issued = 0;
        u64 completed = 0;
        u64 failed = 0;
        u64 stale = 0;
        u64 unused = 0;
        u64 reroutes = 0;
        u64 deviations = 0;
        double issuedPerSecond = 0.0;
        double completedPerSecond = 0.0;
        double failedPerSecond = 0.0;
        double stalePerSecond = 0.0;
        u32 queued = 0;
        u32 inFlight = 0;
        u32 latencySamples = 0;
        double latencyMeanMs = 0.0;
        double latencyP95Ms = 0.0;
        u64 stalledSteps = 0;
        double stallMs = 0.0;
        double meanCarEdges = 0.0;
        double meanPedestrianEdges = 0.0;
        double meanSearchMicros = 0.0;
        double meanSettled = 0.0;
    };

    class PathService
    {
    public:
        static constexpr const char* SPITE_SERVICE_NAME = "CityPaths";

         
        explicit PathService(u32 workers);
        ~PathService();
        PathService(const PathService&) = delete;
        PathService& operator=(const PathService&) = delete;

        static u32 defaultWorkers();
         
        void setWorkers(u32 workers);
        u32 workers() const { return static_cast<u32>(m_threads.size()); }

         
         
         
        void sync(u32 size, u32 cars, u32 pedestrians, u64 seed, u32 step);
         
         
        void drain(const SystemContext& context, u32 step);
         
         
        void publish(std::span<const u32> edgeCars, const std::shared_ptr<const model::RoadGraph>& graph);
         
         
        struct WalkerSnapshot
        {
            std::vector<float> gap;  
            std::vector<u8> flags;  
            std::vector<u32> crowdSpots;
            u32 builtAt = 0xFFFFFFFFu;
        };

        WalkerSnapshot& walkers() { return m_walkers; }
        const WalkerSnapshot& walkers() const { return m_walkers; }

         
        const std::shared_ptr<const model::CityMap>& map() const { return m_map; }
         
        const std::shared_ptr<const model::JunctionTable>& junctions() const { return m_junctions; }
        u32 mapVersion() const { return m_mapVersion; }
        void setMap(std::shared_ptr<const model::CityMap> map, const u32 version)
        {
            m_junctions = std::make_shared<const model::JunctionTable>(map->graph());
            m_map = std::move(map);
            m_mapVersion = version;
        }

         
        RouteSlot& car(const u32 id) { return m_cars[id]; }
        RouteSlot& pedestrian(const u32 id) { return m_pedestrians[id]; }
        const RouteSlot& car(const u32 id) const { return m_cars[id]; }
        const RouteSlot& pedestrian(const u32 id) const { return m_pedestrians[id]; }
         
         
        const RouteSlot* findCar(const u32 id) const { return id < m_cars.size() ? &m_cars[id] : nullptr; }
        const RouteSlot* findPedestrian(const u32 id) const
        {
            return id < m_pedestrians.size() ? &m_pedestrians[id] : nullptr;
        }
         
        void submit(std::span<RouteRequest> requests);
        void countReroute() { m_reroutes.fetch_add(1, std::memory_order_relaxed); }
        void countDeviation() { m_deviations.fetch_add(1, std::memory_order_relaxed); }
        void countUnused() { m_unused.fetch_add(1, std::memory_order_relaxed); }

        PathStats stats() const;

    private:
        struct Result
        {
            RouteRequest request;
            bool found = false;
            u32 micros = 0;
            u32 settled = 0;
            u64 doneNs = 0;
            std::vector<u32> nodes;
        };

        struct RateSample
        {
            u64 ns = 0;
            u64 issued = 0;
            u64 completed = 0;
            u64 failed = 0;
            u64 stale = 0;
        };

        void stopWorkers();
        void workerLoop(const std::stop_token& stop);
        static Result search(const RouteRequest& request);

         
        std::vector<std::jthread> m_threads;
        mutable std::mutex m_queueLock;
        std::condition_variable_any m_wake;
        std::deque<RouteRequest> m_queue;
        u32 m_busy = 0;

         
         
        std::mutex m_resultLock;
        std::condition_variable m_done;
        std::vector<Result> m_results;
        std::vector<std::pair<u32, u32>> m_outstanding;  
        u32 m_epoch = 1;  

         
        std::shared_ptr<const RouteCosts> m_costs;
        std::shared_ptr<const model::CityMap> m_map;
        std::shared_ptr<const model::JunctionTable> m_junctions;
        u32 m_mapVersion = 0;

         
        u32 m_size = 0;
        u32 m_carCount = 0;
        u32 m_pedestrianCount = 0;
        u64 m_seed = 0;
        u32 m_lastStep = 0;
        WalkerSnapshot m_walkers;
        std::vector<RouteSlot> m_cars;
        std::vector<RouteSlot> m_pedestrians;

         
        std::atomic<u64> m_issued{0};
        std::atomic<u64> m_reroutes{0};
        std::atomic<u64> m_deviations{0};
        std::atomic<u64> m_unused{0};
        u64 m_completed = 0;
        u64 m_failed = 0;
        u64 m_stale = 0;
        u64 m_carEdges = 0;
        u64 m_carRoutes = 0;
        u64 m_pedestrianEdges = 0;
        u64 m_pedestrianRoutes = 0;
        u64 m_searchMicros = 0;
        u64 m_settled = 0;
        u64 m_stalledSteps = 0;
        u64 m_stallNs = 0;
        std::vector<RateSample> m_rates;
        std::vector<float> m_latencies;  
        sizet m_latencyNext = 0;
    };
}  
