#include "CityPaths.hpp"

#include <algorithm>
#include <chrono>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "CityRoutes.hpp"
#include "ecs/SystemContext.hpp"

namespace spite::city
{
    namespace
    {
        constexpr sizet LATENCY_WINDOW = 4096;  
        constexpr sizet RATE_SAMPLES = 256;
        constexpr double RATE_WINDOW_NS = 1.0e9;

        u64 nowNs()
        {
            return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
        }
    }  

    u32 PathService::defaultWorkers() { return std::max(1u, std::thread::hardware_concurrency() / 4); }

    PathService::PathService(const u32 workers) { setWorkers(workers); }

    PathService::~PathService() { stopWorkers(); }

    void PathService::stopWorkers()
    {
        for (std::jthread& thread : m_threads)
        {
            thread.request_stop();
        }
        {
            const std::scoped_lock hold(m_queueLock);
        }
        m_wake.notify_all();
        m_threads.clear();  
    }

    void PathService::setWorkers(const u32 workers)
    {
        stopWorkers();
        const u32 count = workers == 0 ? defaultWorkers() : std::min(workers, 256u);
        for (u32 i = 0; i < count; ++i)
        {
            m_threads.emplace_back([this](const std::stop_token& stop) { workerLoop(stop); });
        }
    }

    void PathService::workerLoop(const std::stop_token& stop)
    {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);  
#endif
        while (true)
        {
            RouteRequest request;
            {
                std::unique_lock lock(m_queueLock);
                m_wake.wait(lock, stop, [this] { return !m_queue.empty(); });
                if (stop.stop_requested())
                {
                    return;
                }
                request = std::move(m_queue.front());
                m_queue.pop_front();
                ++m_busy;
            }
            Result result = search(request);
            result.request.costs.reset();
            {
                const std::scoped_lock hold(m_resultLock);
                if (result.request.epoch == m_epoch)
                {
                    const u32 due = result.request.dueStep();
                    const auto at = std::ranges::find(m_outstanding, due, &std::pair<u32, u32>::first);
                    if (at != m_outstanding.end() && --at->second == 0)
                    {
                        m_outstanding.erase(at);
                    }
                    m_results.push_back(std::move(result));
                }
            }
            m_done.notify_all();
            const std::scoped_lock hold(m_queueLock);
            --m_busy;
        }
    }

    PathService::Result PathService::search(const RouteRequest& request)
    {
        Result result;
        result.request = request;
        const u64 begin = nowNs();
        if (request.costs && request.costs->graph)
        {
            const RouteCosts& costs = *request.costs;
            if (request.kind == RouteKind::Car)
            {
                result.found = model::findCarRoute(
                    *costs.graph, [&costs](const u32 edge, const u32 reverse)
                    { return static_cast<u32>(costs.edgeCars[static_cast<sizet>(edge) * 2 + reverse]); }, request.from,
                    request.heading, request.to, result.nodes, &result.settled);
            }
            else
            {
                result.found =
                    model::findPedRoute(*costs.graph, request.from, request.to, result.nodes, &result.settled);
            }
        }
        result.doneNs = nowNs();
        result.micros = static_cast<u32>((result.doneNs - begin) / 1000);
        return result;
    }

    void PathService::submit(const std::span<RouteRequest> requests)
    {
        if (requests.empty())
        {
            return;
        }
        const u64 now = nowNs();
        for (RouteRequest& request : requests)
        {
            request.epoch = m_epoch;
            request.issuedNs = now;
            request.costs = m_costs;
        }
        {
            const std::scoped_lock hold(m_resultLock);
            for (const RouteRequest& request : requests)
            {
                const u32 due = request.dueStep();
                const auto at = std::ranges::lower_bound(m_outstanding, due, {}, &std::pair<u32, u32>::first);
                if (at != m_outstanding.end() && at->first == due)
                {
                    ++at->second;
                }
                else
                {
                    m_outstanding.insert(at, {due, 1});
                }
            }
        }
        {
            const std::scoped_lock hold(m_queueLock);
            m_queue.insert(m_queue.end(), requests.begin(), requests.end());
        }
        m_issued.fetch_add(requests.size(), std::memory_order_relaxed);
        if (requests.size() == 1)
        {
            m_wake.notify_one();
        }
        else
        {
            m_wake.notify_all();
        }
    }

    void PathService::publish(const std::span<const u32> edgeCars, const std::shared_ptr<const model::RoadGraph>& graph)
    {
        const auto clamped = [](const u32 cars) { return static_cast<u16>(std::min(cars, 0xFFFFu)); };
        if (m_costs && m_costs->graph == graph && m_costs->edgeCars.size() == edgeCars.size() &&
            std::equal(m_costs->edgeCars.begin(), m_costs->edgeCars.end(), edgeCars.begin(),
                       [&](const u16 kept, const u32 cars) { return kept == clamped(cars); }))
        {
            return;  
        }
        auto costs = std::make_shared<RouteCosts>();
        costs->graph = graph;
        costs->edgeCars.resize(edgeCars.size());
        std::ranges::transform(edgeCars, costs->edgeCars.begin(), clamped);
        m_costs = std::move(costs);
    }

    void PathService::sync(const u32 size, const u32 cars, const u32 pedestrians, const u64 seed, const u32 step)
    {
        const bool follows = step == m_lastStep + 1;
        m_lastStep = step;
        if (size == m_size && cars == m_carCount && pedestrians == m_pedestrianCount && seed == m_seed && follows)
        {
            return;
        }
        m_size = size;
        m_carCount = cars;
        m_pedestrianCount = pedestrians;
        m_seed = seed;
        {
            const std::scoped_lock hold(m_queueLock);
            m_queue.clear();
        }
        {
            const std::scoped_lock hold(m_resultLock);
            ++m_epoch;  
            m_results.clear();
            m_outstanding.clear();
        }
        m_cars.assign(cars, {});
        m_pedestrians.assign(pedestrians, {});
    }

    void PathService::drain(const SystemContext& context, const u32 step)
    {
        std::vector<Result> due;
        {
            std::unique_lock lock(m_resultLock);
            const auto ready = [&] { return m_outstanding.empty() || m_outstanding.front().first > step; };
            if (!ready())
            {
                const u64 begin = nowNs();
                m_done.wait(lock, ready);
                ++m_stalledSteps;
                m_stallNs += nowNs() - begin;
            }
            const auto later = std::ranges::partition(m_results, [step](const Result& result)
                                                      { return result.request.dueStep() > step; });
            due.assign(std::make_move_iterator(later.begin()), std::make_move_iterator(later.end()));
            m_results.erase(later.begin(), later.end());
        }
         
         
        std::ranges::sort(due,
                          [](const Result& a, const Result& b)
                          {
                              return a.request.kind != b.request.kind ? a.request.kind < b.request.kind
                                  : a.request.id != b.request.id      ? a.request.id < b.request.id
                                                                      : a.request.version < b.request.version;
                          });
        for (Result& result : due)
        {
            const RouteRequest& request = result.request;
            std::vector<RouteSlot>& slots = request.kind == RouteKind::Car ? m_cars : m_pedestrians;
            if (request.id >= slots.size() || !context.isEntityValid(request.entity) ||
                slots[request.id].expected != request.version)
            {
                ++m_stale;
                continue;
            }
            RouteSlot& slot = slots[request.id];
            slot.incomingFailed = !result.found;
            slot.incomingVersion = request.version;
            slot.incoming.swap(result.nodes);
            m_searchMicros += result.micros;
            m_settled += result.settled;
            if (!result.found)
            {
                ++m_failed;
                continue;
            }
            ++m_completed;
            (request.kind == RouteKind::Car ? m_carEdges : m_pedestrianEdges) += slot.incoming.size() - 1;
            ++(request.kind == RouteKind::Car ? m_carRoutes : m_pedestrianRoutes);
            const float ms = static_cast<float>(static_cast<double>(result.doneNs - request.issuedNs) * 1.0e-6);
            if (m_latencies.size() < LATENCY_WINDOW)
            {
                m_latencies.push_back(ms);
            }
            else
            {
                m_latencies[m_latencyNext] = ms;
                m_latencyNext = (m_latencyNext + 1) % LATENCY_WINDOW;
            }
        }
        if (m_rates.size() == RATE_SAMPLES)
        {
            m_rates.erase(m_rates.begin());
        }
        m_rates.push_back({nowNs(), m_issued.load(std::memory_order_relaxed), m_completed, m_failed, m_stale});
    }

    PathStats PathService::stats() const
    {
        PathStats out;
        out.workers = workers();
        out.issued = m_issued.load(std::memory_order_relaxed);
        out.completed = m_completed;
        out.failed = m_failed;
        out.stale = m_stale;
        out.unused = m_unused.load(std::memory_order_relaxed);
        out.reroutes = m_reroutes.load(std::memory_order_relaxed);
        out.deviations = m_deviations.load(std::memory_order_relaxed);
        out.stalledSteps = m_stalledSteps;
        out.stallMs = static_cast<double>(m_stallNs) * 1.0e-6;
        {
            const std::scoped_lock hold(m_queueLock);
            out.queued = static_cast<u32>(m_queue.size());
            out.inFlight = m_busy;
        }
        if (!m_rates.empty())
        {
             
            const RateSample& latest = m_rates.back();
            const RateSample* base = &m_rates.front();
            for (const RateSample& sample : m_rates)
            {
                if (static_cast<double>(latest.ns - sample.ns) >= RATE_WINDOW_NS)
                {
                    base = &sample;
                }
            }
            const double seconds = std::max(static_cast<double>(latest.ns - base->ns) * 1.0e-9, 1.0e-3);
            out.issuedPerSecond = static_cast<double>(latest.issued - base->issued) / seconds;
            out.completedPerSecond = static_cast<double>(latest.completed - base->completed) / seconds;
            out.failedPerSecond = static_cast<double>(latest.failed - base->failed) / seconds;
            out.stalePerSecond = static_cast<double>(latest.stale - base->stale) / seconds;
        }
        out.latencySamples = static_cast<u32>(m_latencies.size());
        if (!m_latencies.empty())
        {
            std::vector<float> millis(m_latencies);
            double sum = 0.0;
            for (const float ms : millis)
            {
                sum += ms;
            }
            const sizet p95 = std::min(millis.size() - 1, millis.size() * 95 / 100);
            std::ranges::nth_element(millis, millis.begin() + p95);
            out.latencyMeanMs = sum / static_cast<double>(millis.size());
            out.latencyP95Ms = millis[p95];
        }
        out.meanCarEdges = m_carRoutes ? static_cast<double>(m_carEdges) / static_cast<double>(m_carRoutes) : 0.0;
        out.meanPedestrianEdges =
            m_pedestrianRoutes ? static_cast<double>(m_pedestrianEdges) / static_cast<double>(m_pedestrianRoutes) : 0.0;
        const u64 searched = m_completed + m_failed;
        out.meanSearchMicros = searched ? static_cast<double>(m_searchMicros) / static_cast<double>(searched) : 0.0;
        out.meanSettled = searched ? static_cast<double>(m_settled) / static_cast<double>(searched) : 0.0;
        return out;
    }
}  
