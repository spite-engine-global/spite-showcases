#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "CityLogistics.hpp"
#include "CityModel.hpp"
#include "CityPaths.hpp"
#include "CityRoutes.hpp"
#include "CityTrips.hpp"
#include "CityTransitions.hpp"
#include "animation/AnimationComponents.hpp"
#include "ecs/Query.hpp"
#include "ecs/SystemContext.hpp"
#include "renderruntime/LightComponents.hpp"
#include "renderruntime/RenderingComponents.hpp"

 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
namespace spite::city
{
    namespace
    {
        using namespace model;

        constexpr u32 NO_LANE = 0xFFFFFFFFu;
        constexpr u32 NO_SUBLANE = 0xFFFFFFFFu;
        constexpr float NO_LEADER = 1.0e9f;

         
        struct Entrant
        {
            u32 lane = 0;
            float s = 0.0f;
            u32 id = 0;

            bool operator<(const Entrant& other) const
            {
                return lane != other.lane ? lane < other.lane : s != other.s ? s < other.s : id < other.id;
            }
        };

        constexpr u32 groupOf(const u32 lane, const u32 sub) { return lane * MAX_LANES + sub; }

         
         
        struct Reservation
        {
            u32 group = 0;
            float from = 0.0f;
            float to = 0.0f;

            bool operator<(const Reservation& other) const
            {
                return group != other.group ? group < other.group : from < other.from;
            }
        };

         
         
         
         
         
         
         
        struct Traffic
        {
            std::vector<u32> lane;
            std::vector<u8> sub;
             
             
            std::vector<u8> ghostSub;
            std::vector<u8> ghostWish;  
            std::vector<u32> entrantLane;
            std::vector<float> s;
            std::vector<float> speed;
            std::vector<float> gap;
            std::vector<u32> next;
            std::vector<u32> prev;
            std::vector<u8> plan;
            std::vector<u32> nodeOf;
            std::vector<u32> moveIndex;
            std::vector<float> progress;
            std::vector<u32> order;  
            std::vector<u32> groupStart;
            std::vector<u32> groupCars;
            std::vector<float> groupS;
            std::vector<u32> edgeCars;  
            std::vector<Entrant> entrants;  
            std::vector<u32> entrantStart;  
            std::vector<u32> holderStart;  
            std::vector<u32> holders;
            std::vector<Reservation> reserved;
            std::vector<u64> park;  
        };

         
         
        class TrafficLease
        {
        public:
            TrafficLease()
            {
                const std::scoped_lock hold(pool().lock);
                if (pool().free.empty())
                {
                    m_traffic = std::make_unique<Traffic>();
                }
                else
                {
                    m_traffic = std::move(pool().free.back());
                    pool().free.pop_back();
                }
            }
            ~TrafficLease()
            {
                const std::scoped_lock hold(pool().lock);
                pool().free.push_back(std::move(m_traffic));
            }
            TrafficLease(const TrafficLease&) = delete;
            TrafficLease& operator=(const TrafficLease&) = delete;

            Traffic& get() { return *m_traffic; }

        private:
            struct Pool
            {
                std::mutex lock;
                std::vector<std::unique_ptr<Traffic>> free;
            };

            static Pool& pool()
            {
                static Pool shared;
                return shared;
            }

            std::unique_ptr<Traffic> m_traffic;
        };

         
         
        struct ParkClaim
        {
            Entity entity = Entity::undefined();
            u32 id = 0;
            u32 node = NO_ID;
            u32 edge = NO_ID;  
            u32 lotEdge = NO_ID;  
            bool away = false;  
            bool street = true;  
        };

         
         
        struct MoveRequest
        {
            Entity entity = Entity::undefined();
            u32 id = 0;
            u32 node = NO_ID;
            u32 code = NO_MOVE;
            u32 queuedAt = 0;
        };

         
        struct Join
        {
            u32 group = 0;
            float s = 0.0f;
        };

         
        struct Counters
        {
            std::atomic<u32> conflicts{0};
            std::atomic<u32> changes{0};
            std::mutex claimLock;
            std::vector<ParkClaim> claims;
            std::vector<MoveRequest> requests;
            std::vector<Join> joins;
        };

        struct Step
        {
            const CityStateComponent* city = nullptr;
            u32 step = 0;  
            float dt = 0.0f;
            u32 size = 0;
            u32 carCount = 0;
            u32 pedestrianCount = 0;
            const Traffic* traffic = nullptr;
            const CityMap* map = nullptr;
            const RoadGraph* graph = nullptr;
            const JunctionTable* junctions = nullptr;
            bool mapChanged = false;  
            PathService* paths = nullptr;
            LogisticsService* logistics = nullptr;
            Counters* counters = nullptr;
        };

         
        class Outbox
        {
        public:
            explicit Outbox(PathService& paths) : m_paths(paths) {}
            ~Outbox()
            {
                if (!m_requests.empty())
                {
                    m_paths.submit(m_requests);
                }
            }
            Outbox(const Outbox&) = delete;
            Outbox& operator=(const Outbox&) = delete;

            void add(const RouteRequest& request) { m_requests.push_back(request); }

        private:
            PathService& m_paths;
            std::vector<RouteRequest> m_requests;
        };

         
        class FreightOutbox
        {
        public:
            explicit FreightOutbox(LogisticsService& logistics) : m_logistics(logistics) {}
            ~FreightOutbox()
            {
                if (!m_requests.empty())
                {
                    m_logistics.submit(m_requests);
                }
            }
            FreightOutbox(const FreightOutbox&) = delete;
            FreightOutbox& operator=(const FreightOutbox&) = delete;

            void add(const LogisticsRequest& request) { m_requests.push_back(request); }

        private:
            LogisticsService& m_logistics;
            std::vector<LogisticsRequest> m_requests;
        };

         
        struct ChunkCounts
        {
            explicit ChunkCounts(Counters& counters) : total(counters) {}
            ~ChunkCounts()
            {
                total.changes.fetch_add(changes, std::memory_order_relaxed);
                if (!claims.empty() || !requests.empty() || !joins.empty())
                {
                    const std::scoped_lock hold(total.claimLock);
                    total.claims.insert(total.claims.end(), claims.begin(), claims.end());
                    total.requests.insert(total.requests.end(), requests.begin(), requests.end());
                    total.joins.insert(total.joins.end(), joins.begin(), joins.end());
                }
            }
            ChunkCounts(const ChunkCounts&) = delete;
            ChunkCounts& operator=(const ChunkCounts&) = delete;

            Counters& total;
            u32 changes = 0;
            std::vector<ParkClaim> claims;
            std::vector<MoveRequest> requests;
            std::vector<Join> joins;
        };

         
        struct Row
        {
            Entity entity;
            Outbox& outbox;
            ChunkCounts& counts;
            FreightOutbox* freight = nullptr;
        };

        constexpr u32 NEVER = 0xFFFFFFFFu;

        constexpr u32 REROUTE_STEPS = 20 * 60;  
        constexpr u32 STALE_STEPS = 5 * 60;  
        constexpr u32 PED_RETRY_STEPS = 5 * 60;  
        constexpr u32 PED_WAIT_STEPS = 120 * 60;  
        constexpr u64 GOAL_SALT = 0x60A1ull;
        constexpr u32 LANE_CHANGE_PERIOD = 4;  
        constexpr float SPREAD_GAP = 40.0f;  
        constexpr float SPREAD_BENEFIT = 2.0f * (CAR_LENGTH + MIN_GAP);  
        constexpr float MIN_TRAVEL = 0.005f;  
        constexpr float SLOT = CAR_LENGTH + MIN_GAP;  
        constexpr float MIN_YIELD = 3.0f;  

         
        constexpr float CLEARANCE = CAR_LENGTH + MIN_GAP + 1.0f;

         

        std::pair<const float*, const float*> groupRange(const Traffic& t, const u32 lane, const u32 sub)
        {
            const u32 group = groupOf(lane, sub);
            return {t.groupS.data() + t.groupStart[group], t.groupS.data() + t.groupStart[group + 1]};
        }

         
        bool groupHasCar(const Step& step, const u32 lane, const u32 sub, const float low, const float high)
        {
            const auto [begin, end] = groupRange(*step.traffic, lane, sub);
            const float* first = std::lower_bound(begin, end, low);
            return first != end && *first < high;
        }

        bool laneBlocked(const Step& step, const u32 lane, const u32 sub, const float s)
        {
            return groupHasCar(step, lane, sub, s - CLEARANCE, s + CLEARANCE);
        }

         
        template <typename T>
        bool sameBytes(const T& a, const T& b)
        {
            static_assert(sizeof(T) % 4 == 0 && std::is_trivially_copyable_v<T>);
            return std::memcmp(&a, &b, sizeof(T)) == 0;
        }
        static_assert(sizeof(CityCarComponent) == 32 * 4 && sizeof(CityPedestrianComponent) == 24 * 4,
                      "padding would make sameBytes compare indeterminate bytes");

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

         
        bool entrantNear(const Step& step, const u32 lane, const float s, const u32 below)
        {
            const std::vector<Entrant>& entrants = step.traffic->entrants;
            for (auto it = std::lower_bound(entrants.begin(), entrants.end(), Entrant{lane, s - CLEARANCE, 0});
                 it != entrants.end() && it->lane == lane && it->s < s + CLEARANCE; ++it)
            {
                if (it->id < below)
                {
                    return true;
                }
            }
            return false;
        }

         
        bool reservedNear(const Step& step, const u32 group, const float s)
        {
            const std::vector<Reservation>& kept = step.traffic->reserved;
            for (auto it = std::lower_bound(kept.begin(), kept.end(), Reservation{group, -NO_LEADER, 0.0f});
                 it != kept.end() && it->group == group && it->from < s + CLEARANCE; ++it)
            {
                if (it->to > s - CLEARANCE)
                {
                    return true;
                }
            }
            return false;
        }

         
         
        bool grantedBehind(const Step& step, const u32 group, const float s)
        {
            const Traffic& t = *step.traffic;
            const float* begin = t.groupS.data() + t.groupStart[group];
            const float* at = std::lower_bound(begin, t.groupS.data() + t.groupStart[group + 1], s);
            return at != begin && t.plan[t.groupCars[at - 1 - t.groupS.data()]] != PLAN_NONE;
        }

         
        u32 slideFrom(const CityCarComponent& car)
        {
            return car.slide > 0.0f ? std::min(car.sublane + 1, MAX_LANES - 1)
                                    : (car.sublane > 0 ? car.sublane - 1 : 0);
        }

         
        float slideRoom(const Step& step, const CityCarComponent& car, const u32 lane, const float s)
        {
            const Traffic& t = *step.traffic;
            const u32 group = groupOf(lane, slideFrom(car));
            const float* base = t.groupS.data();
            const float* end = base + t.groupStart[group + 1];
            for (const float* it = std::lower_bound(base + t.groupStart[group], end, s); it != end; ++it)
            {
                if (t.groupCars[it - base] != car.id)
                {
                    return *it - s - CAR_LENGTH - MIN_GAP;
                }
            }
            return NO_LEADER;
        }

         
        void clearMovement(CityCarComponent& car)
        {
            car.plan = PLAN_NONE;
            car.node = NO_ID;
            car.move = NO_MOVE;
            car.moveS = 0.0f;
        }

         

        u32 nodeKeyOf(const Step& step, const u32 node) { return step.graph->keyOf(node); }

         
        u32 alongOf(const Step& step, const u32 node, const u32 axis) { return step.graph->along(node, axis); }

         
        bool stopsFor(const Step& step, const u32 node, const u32 axis)
        {
            const GraphNode& n = step.graph->nodes[node];
            return n.signal &&
                (greenAxis(*step.city, n.x, n.z, step.step) != axis ||
                 static_cast<float>(phaseStepsLeft(*step.city, n.x, n.z, step.step)) * step.dt < AMBER);
        }

         
        u32 aheadNode(const Step& step, const CityCarComponent& car)
        {
            const u32 edge = step.graph->edgeUnder(car.axis, car.line, car.pos, car.dir);
            return edge == NO_ID ? NO_ID : car.dir > 0.0f ? step.graph->edges[edge].b : step.graph->edges[edge].a;
        }

         
        u32 nearestEnd(const GraphEdge& edge, const float pos)
        {
            return std::fabs(pos - static_cast<float>(edge.lo) * CELL) <=
                    std::fabs(pos - static_cast<float>(edge.hi) * CELL)
                ? edge.a
                : edge.b;
        }

        void requestCarRoute(const Step& step, const Row& row, CityCarComponent& car, const u32 fromKey,
                             const u32 heading)
        {
            ++car.routeVersion;
            step.paths->car(car.id).expected = car.routeVersion;
            row.outbox.add({.entity = row.entity,
                            .kind = RouteKind::Car,
                            .id = car.id,
                            .version = car.routeVersion,
                            .from = fromKey,
                            .heading = heading,
                            .to = step.graph->key(car.destX, car.destZ),
                            .issuedStep = step.step});
        }

         
        void dropRoute(CityCarComponent& car, RouteSlot& slot)
        {
            car.routeState = ROUTE_NONE;
            car.routeIndex = 0;
            car.routeLength = 0;
            slot.expected = 0;
            slot.incomingVersion = 0;
        }

         
         
         
        void takeRoute(const Step& step, CityCarComponent& car, RouteSlot& slot, const u32 node, const u32 heading)
        {
            const RoadGraph& graph = *step.graph;
            const bool rerouting = car.routeState == ROUTE_REROUTING;
            const u32 key = nodeKeyOf(step, node);
            bool taken = false;
            if (!slot.incomingFailed)
            {
                const auto at = std::ranges::find(slot.incoming, key);
                const u32 index = static_cast<u32>(at - slot.incoming.begin());
                if (at != slot.incoming.end() && graph.routeValid(slot.incoming, index) &&
                    (index + 1 >= slot.incoming.size() || graph.nodes[node].degree == 1 ||
                     graph.headingBetween(key, slot.incoming[index + 1]) != ((heading + 2) & 3)))
                {
                    slot.active.swap(slot.incoming);
                    car.routeIndex = index;
                    car.routeLength = static_cast<u32>(slot.active.size());
                    car.routeState = ROUTE_READY;
                    taken = true;
                }
                else
                {
                    step.paths->countUnused();
                }
            }
            if (!taken)
            {
                car.routeState = rerouting ? ROUTE_READY : slot.incomingFailed ? ROUTE_FAILED : ROUTE_NONE;
            }
            slot.expected = 0;
            slot.incomingVersion = 0;
        }

         
         
        void claimPark(const Row& row, CityCarComponent& car, const u32 node, const u32 edge, const u32 lotEdge,
                       const bool away, const bool street)
        {
            car.axis = 2;
            car.sublane = 0;
            row.counts.claims.push_back({.entity = row.entity,
                                         .id = car.id,
                                         .node = node,
                                         .edge = edge,
                                         .lotEdge = lotEdge,
                                         .away = away,
                                         .street = street});
        }

         
        bool streetParking(const Step& step, const CityCarComponent& car, const u32 lotId)
        {
            const LotRef* lot = step.map->lot(lotId);
            return parksOnStreet(step.city->seed, car.id, lotId, lot ? lot->zone : ZONE_COMMERCIAL_LOW,
                                 isTruck(*step.city, car.id));
        }

         
         
        void relocateCar(const Step& step, const Row& row, CityCarComponent& car, RouteSlot& slot)
        {
            const CityMap& map = *step.map;
            dropRoute(car, slot);
            slot.active.clear();
            clearMovement(car);
            car.queuedAt = NEVER;
            car.slide = 0.0f;
            car.speed = 0.0f;
            car.sublane = 0;
            car.state = CAR_PARKED;
            if (isTruck(*step.city, car.id))
            {
                 
                const u32 base =
                    map.reachable(car.homeLot) ? car.homeLot : truckBase(map, truckIndex(*step.city, car.id));
                const LotRef* lot = map.reachable(base);
                car.parkUntil = NEVER;
                if (!lot)
                {
                    return;
                }
                const u32 node = map.accessNode(*lot, 0);
                car.homeLot = car.targetLot = base;
                claimPark(row, car, node, lot->edge, lot->edge, false, streetParking(step, car, base));
                car.destX = step.graph->nodes[node].x;
                car.destZ = step.graph->nodes[node].z;
                car.purpose = TRIP_IDLE;
                return;
            }
            car.homeLot = homeLotOf(map, car.homeLot, car.id, car.trips);
            const LotRef* home = map.reachable(car.homeLot);
            if (!home)
            {
                car.parkUntil = 0xFFFFFFFFu;  
                return;
            }
            const u32 node = map.accessNode(*home, 0);
            claimPark(row, car, node, home->edge, home->edge, false, streetParking(step, car, car.homeLot));
            car.destX = step.graph->nodes[node].x;
            car.destZ = step.graph->nodes[node].z;
            car.purpose = TRIP_HOME;
            car.targetLot = car.homeLot;
            assignTrip(car, nextTrip(car, *step.city, map, step.step));
        }

         
         
         
         
         
         
         
        void dispatchTruck(const Step& step, const Row& row, CityCarComponent& car)
        {
            const u32 index = truckIndex(*step.city, car.id);
            TruckSlot& slot = step.logistics->truck(index);
            if (slot.answered)
            {
                const TruckAnswer answer = slot.answer;
                slot.answered = false;
                slot.asked = false;
                car.purpose = answer.purpose;
                if (answer.purpose != TRIP_IDLE)
                {
                    car.targetLot = answer.lot;
                    car.destX = answer.x;
                    car.destZ = answer.z;
                }
                car.parkUntil = step.step + std::max(answer.dwell, 1u);
                return;
            }
            if (!slot.asked && row.freight)
            {
                slot.asked = true;
                row.freight->add({.kind = car.purpose == TRIP_FREIGHT ? REQUEST_TRUCK_AT_STOP : REQUEST_TRUCK_AT_BASE,
                                  .id = index,
                                  .lot = car.targetLot,
                                  .x = car.destX,
                                  .z = car.destZ,
                                  .base = car.homeLot});
            }
        }

        void stepParkedCar(const Step& step, const Row& row, CityCarComponent& car)
        {
            const u32 ahead = aheadNode(step, car);
            if (ahead == NO_ID)
            {
                return;  
            }
            if (car.state == CAR_PARKED && step.logistics && isTruck(*step.city, car.id) &&
                (car.parkUntil == NEVER || (car.purpose == TRIP_IDLE && step.step >= car.parkUntil)))
            {
                dispatchTruck(step, row, car);
                return;
            }
            RouteSlot& slot = step.paths->car(car.id);
            if (car.state == CAR_PARKED)
            {
                if (step.step < car.parkUntil)
                {
                    return;
                }
                car.state = CAR_AWAITING_ROUTE;
                car.routeState = ROUTE_PENDING;
                car.routeIndex = 0;
                car.routeLength = 0;
                requestCarRoute(step, row, car, nodeKeyOf(step, ahead), headingOf(car.axis, car.dir));
                return;
            }
            if (car.routeState == ROUTE_PENDING)
            {
                if (slot.incomingVersion != car.routeVersion)
                {
                    if (slot.expected != car.routeVersion)
                    {
                         
                        requestCarRoute(step, row, car, nodeKeyOf(step, ahead), headingOf(car.axis, car.dir));
                    }
                    return;
                }
                if (slot.incomingFailed || !step.graph->routeValid(slot.incoming, 0))
                {
                    car.routeState = ROUTE_FAILED;
                }
                else
                {
                    slot.active.swap(slot.incoming);
                    car.routeIndex = 0;
                    car.routeLength = static_cast<u32>(slot.active.size());
                    car.routeState = ROUTE_READY;
                }
                slot.expected = 0;
                slot.incomingVersion = 0;
            }
            const u32 lane = laneOf(step.size, car.axis, car.line, car.dir);
            const float s = car.dir * car.pos;
             
            if (laneBlocked(step, lane, 0, s) || entrantNear(step, lane, s, car.id) ||
                reservedNear(step, groupOf(lane, 0), s) || grantedBehind(step, groupOf(lane, 0), s))
            {
                return;
            }
            row.counts.joins.push_back({groupOf(lane, 0), s});
            car.state = CAR_DRIVING;
            car.sublane = 0;
            car.slide = 0.0f;
            clearMovement(car);
            car.queuedAt = NEVER;
            car.speed = ACCELERATION * step.dt;
            car.stoppedSince = step.step;
            if (car.routeState == ROUTE_FAILED)
            {
                car.routeState = ROUTE_NONE;  
            }
        }

         

         
         
        struct Intent
        {
            bool arrive = false;
            bool turn = false;
            bool uTurn = false;
            bool routed = false;  
            bool forced = false;  
            float td = 0.0f;
        };

        Intent intentOf(const Step& step, const CityCarComponent& car, const RouteSlot& slot, const u32 node)
        {
            const RoadGraph& graph = *step.graph;
            const GraphNode& n = graph.nodes[node];
            Intent intent;
            if (n.x == car.destX && n.z == car.destZ)
            {
                intent.arrive = true;
                return intent;
            }
            const u32 heading = headingOf(car.axis, car.dir);
            intent.forced = n.edges[heading] == NO_ID;
            const u32 key = graph.keyOf(node);
            const bool following = car.routeState == ROUTE_READY || car.routeState == ROUTE_REROUTING;
            if (following && car.routeIndex + 1 < slot.active.size() && slot.active[car.routeIndex] == key)
            {
                const u32 onward = slot.active[car.routeIndex + 1];
                const u32 way = graph.headingBetween(key, onward);
                if (way != NO_ID && n.edges[way] != NO_ID && graph.keyOf(graph.other(n.edges[way], node)) == onward)
                {
                    if (way == heading)
                    {
                        intent.routed = true;
                    }
                    else if (way == ((heading + 2) & 3))
                    {
                        intent.routed = intent.uTurn = n.degree == 1;
                    }
                    else
                    {
                        intent.routed = intent.turn = true;
                        intent.td = dirOfHeading(way);
                    }
                }
            }
            if (intent.routed)
            {
                return intent;
            }
             
             
            const float choice = draw(step.city->seed ^ ROUTE_SALT, car.id, car.turns);
            const u32 order[3] = {heading, choice < 0.5f ? (heading + 3) & 3 : (heading + 1) & 3,
                                  choice < 0.5f ? (heading + 1) & 3 : (heading + 3) & 3};
            u32 best = NO_ID;
            u32 bestDistance = 0;
            for (const u32 way : order)
            {
                const u32 edge = n.edges[way];
                if (edge == NO_ID)
                {
                    continue;
                }
                const GraphNode& farNode = graph.nodes[graph.other(edge, node)];
                const u32 distance = (farNode.x > car.destX ? farNode.x - car.destX : car.destX - farNode.x) +
                    (farNode.z > car.destZ ? farNode.z - car.destZ : car.destZ - farNode.z);
                if (best == NO_ID || distance < bestDistance)
                {
                    best = way;
                    bestDistance = distance;
                }
            }
            if (best == NO_ID)
            {
                intent.uTurn = true;
            }
            else if (best != heading)
            {
                intent.turn = true;
                intent.td = dirOfHeading(best);
            }
            return intent;
        }

         
         
         
         
         

         
         
        float requestReach(const float v, const float dt) { return v * v / (2.0f * BRAKING) + 3.0f * v * dt + 2.0f; }

         
        void takeIncoming(const Step& step, CityCarComponent& car, RouteSlot& slot, const u32 node)
        {
            if (car.routeState != ROUTE_PENDING && car.routeState != ROUTE_REROUTING)
            {
                return;
            }
            if (slot.incomingVersion == car.routeVersion)
            {
                takeRoute(step, car, slot, node, headingOf(car.axis, car.dir));
            }
            else if (slot.expected != car.routeVersion)
            {
                dropRoute(car, slot);  
            }
        }

         
        u32 wayOut(const Intent& intent, const CityCarComponent& car)
        {
            const u32 heading = headingOf(car.axis, car.dir);
            return intent.uTurn ? (heading + 2) & 3 : intent.turn ? headingOf(1 - car.axis, intent.td) : heading;
        }

         
         
        u32 nextTurnSub(const Step& step, const CityCarComponent& car, const RouteSlot& slot, const u32 node,
                        const u32 out)
        {
            const RoadGraph& graph = *step.graph;
            const bool following = car.routeState == ROUTE_READY || car.routeState == ROUTE_REROUTING;
            const u32 edge = graph.nodes[node].edges[out];
            if (!following || edge == NO_ID || car.routeIndex + 2 >= slot.active.size() ||
                slot.active[car.routeIndex] != graph.keyOf(node) ||
                slot.active[car.routeIndex + 1] != graph.keyOf(graph.other(edge, node)))
            {
                return NO_MOVE;
            }
            const u32 then = graph.headingBetween(slot.active[car.routeIndex + 1], slot.active[car.routeIndex + 2]);
            if (then == NO_ID || axisOfHeading(then) == axisOfHeading(out))
            {
                return NO_MOVE;
            }
            return edgeSublane(graph.edges[edge].lanes,
                               turnsRight(axisOfHeading(out), dirOfHeading(out), dirOfHeading(then)));
        }

         
         
         
        u32 movementFor(const Step& step, const CityCarComponent& car, const RouteSlot& slot, const u32 node)
        {
            const u32 heading = headingOf(car.axis, car.dir);
            const u32 wanted = wayOut(intentOf(step, car, slot, node), car);
            for (const u32 way : {wanted, heading, (heading + 1) & 3, (heading + 3) & 3, (heading + 2) & 3})
            {
                const u32 code =
                    movementOf(*step.graph, node, heading, car.sublane, way, nextTurnSub(step, car, slot, node, way));
                if (code != NO_MOVE)
                {
                    return code;
                }
            }
            return NO_MOVE;
        }

         
         
        void enterBox(const Step& step, const Row& row, CityCarComponent& car, const u32 node)
        {
            const RoadGraph& graph = *step.graph;
            RouteSlot& slot = step.paths->car(car.id);
            takeIncoming(step, car, slot, node);
            const Intent intent = intentOf(step, car, slot, node);
            const u32 out = moveHeadingOut(car.move);
            const bool followed = intent.routed && wayOut(intent, car) == out;
            car.plan = PLAN_MOVING;
            car.wish = 0;
            car.queuedAt = NEVER;
            ++car.turns;
            if (followed)
            {
                ++car.routeIndex;
                return;
            }
            if (car.routeState == ROUTE_READY || car.routeState == ROUTE_REROUTING)
            {
                step.paths->countDeviation();
                dropRoute(car, slot);
            }
            if (car.routeState == ROUTE_PENDING)
            {
                return;  
            }
            const u32 beyond = graph.other(graph.nodes[node].edges[out], node);
            if (graph.nodes[beyond].x != car.destX || graph.nodes[beyond].z != car.destZ)
            {
                car.routeState = ROUTE_PENDING;
                requestCarRoute(step, row, car, nodeKeyOf(step, beyond), out);
            }
        }

         
         
        void parkAt(const Step& step, const Row& row, CityCarComponent& car, const u32 edgeId, const u32 node)
        {
            const RoadGraph& graph = *step.graph;
            RouteSlot& slot = step.paths->car(car.id);
            const ParkSpot spot = parkSpotOf(*step.city, *step.map, car.id);
            const u32 straight = graph.nodes[node].edges[headingOf(car.axis, car.dir)];
            const bool beyond = straight != NO_ID && curbSlotAt(graph, straight, node, true, spot, nullptr).fits;
            const LotRef* lot = step.map->reachable(car.targetLot);
            claimPark(row, car, node, beyond ? straight : edgeId, lot ? lot->edge : NO_ID, beyond,
                      streetParking(step, car, car.targetLot));
            car.state = CAR_PARKED;
            car.speed = 0.0f;
            car.slide = 0.0f;
            clearMovement(car);
            car.queuedAt = NEVER;
            ++car.trips;
            ++car.turns;
            dropRoute(car, slot);
            slot.active.clear();
            if (step.logistics && isTruck(*step.city, car.id))
            {
                car.parkUntil = NEVER;  
                return;
            }
            assignTrip(car, nextTrip(car, *step.city, *step.map, step.step));
        }

         
         
        bool leaderWaits(const Step& step, const CityCarComponent& car, const u32 node, const float stopS)
        {
            const Traffic& t = *step.traffic;
            const u32 leader = t.next[car.id];
            return leader != NO_ID && t.s[leader] <= stopS + 1e-3f &&
                !(t.plan[leader] == PLAN_GRANTED && t.nodeOf[leader] == node);
        }

         
         
        void askToEnter(const Step& step, const Row& row, CityCarComponent& car, const u32 node, const float toLine,
                        const float stopS, const bool canStop)
        {
            if (car.slide != 0.0f || toLine < -0.01f || toLine > requestReach(car.speed, step.dt) ||
                leaderWaits(step, car, node, stopS) || (stopsFor(step, node, car.axis) && canStop))
            {
                return;
            }
            RouteSlot& slot = step.paths->car(car.id);
            takeIncoming(step, car, slot, node);
            const u32 code = movementFor(step, car, slot, node);
            if (code == NO_MOVE)
            {
                return;
            }
            car.queuedAt = car.queuedAt == NEVER ? step.step : car.queuedAt;
            row.counts.requests.push_back(
                {.entity = row.entity, .id = car.id, .node = node, .code = code, .queuedAt = car.queuedAt});
        }

         
        bool earlier(const u32 a, const u32 b) { return static_cast<i32>(a - b) < 0; }

         
         
         
         
        float yieldRoom(const Step& step, const CityCarComponent& car, const u32 node, const u32 index,
                        const float progress)
        {
            const Traffic& t = *step.traffic;
            const JunctionType& type = step.junctions->at(node);
            float room = NO_LEADER;
            for (u32 i = t.holderStart[node]; i < t.holderStart[node + 1]; ++i)
            {
                const u32 other = t.holders[i];
                const u32 held = t.moveIndex[other];
                if (other == car.id || held == index || !earlier(t.order[other], car.grantOrder))
                {
                    continue;
                }
                const float leave = type.leaveOf(held, index);
                if (leave < 0.0f || leave < t.progress[other] - 0.01f)
                {
                    continue;  
                }
                const float follow = type.followOf(index, held);
                room = std::min(room,
                                follow >= 0.0f ? t.progress[other] - follow - progress
                                               : type.meetOf(index, held) - progress - 0.05f);
            }
            return std::max(room, 0.0f);
        }

         
         
         
        float movementRoom(const Step& step, const CityCarComponent& car, const u32 node, const MovementPath& path)
        {
            const RoadGraph& graph = *step.graph;
            const Traffic& t = *step.traffic;
            const u32 index = step.junctions->at(node).indexOf(car.move);
            const float s = t.s[car.id];
            float room = NO_LEADER;
            for (u32 ahead = t.next[car.id]; ahead != NO_ID && t.s[ahead] < s + path.length + CAR_LENGTH;
                 ahead = t.next[ahead])
            {
                if (t.plan[ahead] == PLAN_MOVING && t.nodeOf[ahead] == node && t.moveIndex[ahead] == index)
                {
                    room = t.s[ahead] - s - CAR_LENGTH - MIN_GAP;
                    break;
                }
            }
            const GraphNode& n = graph.nodes[node];
            const u32 out = moveHeadingOut(car.move);
            const u32 axisOut = axisOfHeading(out);
            const float dirOut = dirOfHeading(out);
            const u32 group =
                groupOf(laneOf(step.size, axisOut, axisOut == 0 ? n.z : n.x, dirOut), moveSubOut(car.move));
            const float centreOut = dirOut * static_cast<float>(alongOf(step, node, axisOut)) * CELL;
            const float* base = t.groupS.data();
            const float* end = base + t.groupStart[group + 1];
            for (const float* it = std::upper_bound(base + t.groupStart[group], end, centreOut); it != end; ++it)
            {
                if (t.plan[t.groupCars[it - base]] == PLAN_MOVING)
                {
                    continue;  
                }
                const BodyPose pose = bodyOnPath(path, car.moveS);
                const float gx = headingX(out);
                const float gz = headingZ(out);
                const float front = (pose.x + static_cast<float>(n.x) * CELL) * gx +
                    (pose.z + static_cast<float>(n.z) * CELL) * gz +
                    CAR_HALF_LENGTH * std::fabs(pose.dirX * gx + pose.dirZ * gz) +
                    CAR_HALF_WIDTH * std::fabs(pose.dirX * gz - pose.dirZ * gx);
                room = std::min(room, *it - CAR_HALF_LENGTH - front - MIN_GAP);
                break;
            }
            return std::max(room, 0.0f);
        }

         
        void stepMoving(const Step& step, CityCarComponent& car)
        {
            const RoadGraph& graph = *step.graph;
            u32 node = NO_ID;
            MovementPath path;
            if (!carMovement(car, graph, node, path))
            {
                return;  
            }
            const float v = car.speed;
            const float room =
                std::min(movementRoom(step, car, node, path),
                         yieldRoom(step, car, node, step.junctions->at(node).indexOf(car.move), car.moveS));
            float speed = std::min({v + ACCELERATION * step.dt, std::min(car.maxSpeed, MAX_SPEED), path.cap,
                                    std::sqrt(2.0f * BRAKING * room)});
            float travel = speed * step.dt;
            if (travel > room)
            {
                travel = room;
                speed = travel / step.dt;
            }
            if (room < MIN_TRAVEL || travel < 1e-5f)
            {
                if (v > 0.0f)
                {
                    car.stoppedSince = step.step;
                }
                car.speed = 0.0f;
                return;
            }
            car.speed = speed;
            car.moveS += travel;
            if (car.moveS < path.length)
            {
                return;
            }
            const GraphNode& n = graph.nodes[node];
            const u32 out = moveHeadingOut(car.move);
            car.axis = axisOfHeading(out);
            car.line = car.axis == 0 ? n.z : n.x;
            car.dir = dirOfHeading(out);
            car.pos = static_cast<float>(alongOf(step, node, car.axis)) * CELL +
                car.dir * (stopDistance(n, out) + car.moveS - path.length);
            car.sublane = moveSubOut(car.move);
            clearMovement(car);
        }

         
         
        void rerouteIfJammed(const Step& step, const Row& row, CityCarComponent& car, const u32 edge, const u32 node)
        {
            const u32 standing = step.step - car.stoppedSince;
            if (car.routeState != ROUTE_READY || car.routeIndex + 1 >= car.routeLength || standing < REROUTE_STEPS ||
                standing % REROUTE_STEPS != 0)
            {
                return;  
                         
            }
            const u32 cars = step.traffic->edgeCars[static_cast<sizet>(edge) * 2 + (car.dir < 0.0f ? 1 : 0)];
            if (congestionLevel(cars, edgeCapacity(step.graph->edges[edge])) < CONGESTION_LEVELS - 1)
            {
                return;
            }
            car.routeState = ROUTE_REROUTING;
            step.paths->countReroute();
            requestCarRoute(step, row, car, nodeKeyOf(step, node), headingOf(car.axis, car.dir));
        }

         

         
         
         
         
        bool laneChangeOk(const Step& step, const u32 lane, const u32 target, const float s, const float v,
                          const bool must, const u32 id, float& leaderGap)
        {
            const Traffic& t = *step.traffic;
            const u32 group = groupOf(lane, target);
            const float* base = t.groupS.data();
            const float* begin = base + t.groupStart[group];
            const float* end = base + t.groupStart[group + 1];
            const float* at = std::lower_bound(begin, end, s);
            const float* ahead = at;
            while (ahead != end && t.groupCars[ahead - base] == id)
            {
                ++ahead;  
            }
            leaderGap = ahead == end ? NO_LEADER : *ahead - s - CAR_LENGTH;
            if (leaderGap < MIN_GAP + (must ? 0.0f : CAR_LENGTH))
            {
                return false;
            }
            if (at != begin)
            {
                const float behind = s - at[-1] - CAR_LENGTH;
                if (behind < MIN_GAP)
                {
                    return false;
                }
                const u32 followerId = t.groupCars[at - 1 - base];
                const float follower = t.speed[followerId];
                if (t.plan[followerId] != PLAN_NONE ||
                    (follower > v && (follower * follower - v * v) > 2.0f * BRAKING * (behind - MIN_GAP)))
                {
                    return false;  
                }
            }
            return !reservedNear(step, group, s) && (target != 0 || !entrantNear(step, lane, s, 0xFFFFFFFFu));
        }

         
         
         
         
         
         
         
         
        void changeLane(const Step& step, ChunkCounts& counts, CityCarComponent& car, const GraphEdge& here,
                        const u32 node, const u32 lane, const float sCentre, const float s, float& gap)
        {
            const u32 lanes = here.lanes;
            const bool stale = car.sublane >= lanes;
            const u32 wish = car.wish;
            car.wish = 0;
            if (car.slide != 0.0f || (!stale && wish == 0 && ((step.step >> 1) + car.id) % LANE_CHANGE_PERIOD != 0))
            {
                car.wish = car.slide != 0.0f ? 0 : wish;
                return;
            }
            const RoadGraph& graph = *step.graph;
            const float ahead = sCentre - s;
            if (!stale && ahead < graph.nodes[node].stop[1 - car.axis] + CAR_LENGTH)
            {
                return;  
            }
            const float behindStop =
                graph.nodes[graph.other(static_cast<u32>(&here - graph.edges.data()), node)].stop[1 - car.axis];
            if (here.length() - ahead < behindStop + CAR_LENGTH)
            {
                return;
            }
            const bool up = (step.step & 1) != 0;
            if (stale)
            {
                float leaderGap = 0.0f;
                if (!up && laneChangeOk(step, lane, car.sublane - 1, s, car.speed, true, car.id, leaderGap))
                {
                    --car.sublane;
                    car.slide = 1.0f;
                    gap = leaderGap;
                    ++counts.changes;
                    counts.joins.push_back({groupOf(lane, car.sublane), s});
                }
                return;
            }
            u32 need = NO_SUBLANE;
            const Intent intent = intentOf(step, car, step.paths->car(car.id), node);
            if (!intent.arrive && intent.turn && intent.routed && !intent.forced &&
                !turnsFrom(car.sublane, lanes, turnsRight(car.axis, car.dir, intent.td)))
            {
                need = edgeSublane(lanes, turnsRight(car.axis, car.dir, intent.td));
            }
            if (need == car.sublane)
            {
                return;
            }
            const u32 toward = need == NO_SUBLANE ? NO_SUBLANE : need > car.sublane ? car.sublane + 1 : car.sublane - 1;
            if (need == NO_SUBLANE ? (gap > SPREAD_GAP || (up ? car.sublane + 1 >= lanes : car.sublane == 0))
                                   : (need > car.sublane) != up)
            {
                car.wish = toward == NO_SUBLANE ? 0 : toward + 1;
                return;
            }
            const u32 target = up ? car.sublane + 1 : car.sublane - 1;
            float leaderGap = 0.0f;
            if (!laneChangeOk(step, lane, target, s, car.speed, need != NO_SUBLANE, car.id, leaderGap) ||
                (need == NO_SUBLANE && leaderGap < gap + SPREAD_BENEFIT))
            {
                car.wish = toward == NO_SUBLANE ? 0 : toward + 1;
                return;
            }
            car.slide = target < car.sublane ? 1.0f : -1.0f;
            car.sublane = target;
            gap = leaderGap;
            ++counts.changes;
            counts.joins.push_back({groupOf(lane, target), s});
        }

         

        void stepCar(const Step& step, const Row& row, CityCarComponent& car)
        {
            if (car.id >= step.carCount)
            {
                return;
            }
            if (car.state != CAR_DRIVING)
            {
                stepParkedCar(step, row, car);
                return;
            }
            if (car.plan == PLAN_MOVING)
            {
                stepMoving(step, car);
                return;
            }
            const RoadGraph& graph = *step.graph;
            const u32 edgeId = graph.edgeUnder(car.axis, car.line, car.pos, car.dir);
            if (edgeId == NO_ID)
            {
                return;  
            }
            const GraphEdge& here = graph.edges[edgeId];
            const u32 lanes = here.lanes;
            car.sublane = std::min(car.sublane, MAX_LANES - 1);
            const u32 lane = laneOf(step.size, car.axis, car.line, car.dir);
            const float s = car.dir * car.pos;
            const float v = car.speed;
            float gap = step.traffic->gap[car.id];

             
            const u32 node = car.dir > 0.0f ? here.b : here.a;
            const GraphNode& n = graph.nodes[node];
            const u32 heading = headingOf(car.axis, car.dir);
            const float sCentre = car.dir * static_cast<float>(alongOf(step, node, car.axis)) * CELL;
            const float stopS = sCentre - stopDistance(n, heading);
            if (car.plan == PLAN_GRANTED &&
                (car.node != nodeKeyOf(step, node) || step.junctions->at(node).indexOf(car.move) == NO_MOVE))
            {
                clearMovement(car);  
            }
            if (car.plan == PLAN_NONE && (lanes > 1 || car.sublane >= lanes))
            {
                changeLane(step, row.counts, car, here, node, lane, sCentre, s, gap);
            }
            if (n.x == car.destX && n.z == car.destZ && s >= stopS - 0.05f)
            {
                parkAt(step, row, car, edgeId, node);
                return;
            }
            const float toLine = std::max(stopS - s, 0.0f);
            const bool canStop = v * v / (2.0f * BRAKING) <= toLine + 0.5f;
            std::optional<float> redLine;  
            if (s <= stopS + 1e-3f && n.signal && greenAxis(*step.city, n.x, n.z, step.step) != car.axis)
            {
                redLine = stopS;
            }
            if (car.plan == PLAN_GRANTED &&
                ((stopsFor(step, node, car.axis) && canStop) || leaderWaits(step, car, node, stopS)))
            {
                 
                 
                clearMovement(car);
            }
            float room = gap - MIN_GAP;
            if (car.slide != 0.0f)
            {
                 
                room = std::min(room, slideRoom(step, car, lane, s));
                const float left = std::fabs(car.slide) - step.dt / LANE_CHANGE_SECONDS;
                car.slide = left <= 0.0f ? 0.0f : std::copysign(left, car.slide);
            }
            float cap = NO_LEADER;
            if (car.plan == PLAN_GRANTED)
            {
                 
                const float pathCap = movementPath(graph, node, car.move).cap;
                cap = std::sqrt(pathCap * pathCap + 2.0f * BRAKING * toLine);
                room = std::min(room, yieldRoom(step, car, node, step.junctions->at(node).indexOf(car.move), -toLine));
            }
            else
            {
                room = std::min(room, toLine);
                if (car.sublane >= lanes)
                {
                     
                     
                    if (v == 0.0f && toLine < 0.05f && step.step - car.stoppedSince >= STALE_STEPS &&
                        !laneBlocked(step, lane, lanes - 1, s))
                    {
                        car.slide = car.sublane == lanes ? 1.0f : 0.0f;
                        car.sublane = lanes - 1;
                    }
                }
                else if (n.x != car.destX || n.z != car.destZ)
                {
                    askToEnter(step, row, car, node, stopS - s, stopS, canStop);
                }
            }
            room = std::max(room, 0.0f);
            float speed = std::min({v + ACCELERATION * step.dt, std::min(car.maxSpeed, MAX_SPEED), here.limit(),
                                    std::sqrt(2.0f * BRAKING * room), cap});
            float travel = speed * step.dt;
            if (travel > room)
            {
                travel = room;
                speed = travel / step.dt;
            }
            if (room < MIN_TRAVEL || travel < 1e-5f)
            {
                 
                 
                if (v > 0.0f)
                {
                    car.stoppedSince = step.step;
                }
                else
                {
                    rerouteIfJammed(step, row, car, edgeId, node);
                }
                car.speed = 0.0f;
                return;
            }
            const float next = s + travel;
            car.speed = speed;
            if (redLine && next > *redLine + 1e-3f)
            {
                ++car.redRuns;
            }
            if (car.plan == PLAN_GRANTED && next >= stopS)
            {
                car.pos = car.dir * stopS;
                car.moveS = next - stopS;
                enterBox(step, row, car, node);
                return;
            }
            car.pos = car.dir * next;
        }

         

        void checkCar(const Step& step, const Row& row, CityCarComponent& car)
        {
            if (car.id >= step.carCount)
            {
                return;
            }
            const CityMap& map = *step.map;
            const RoadGraph& graph = *step.graph;
            RouteSlot& slot = step.paths->car(car.id);
            if (isTruck(*step.city, car.id))
            {
                 
                if (!map.reachable(car.homeLot))
                {
                    const u32 base = truckBase(map, truckIndex(*step.city, car.id));
                    car.homeLot = base != NO_ID ? base : car.homeLot;
                }
                if (car.purpose != TRIP_IDLE && !map.reachable(car.targetLot) && map.reachable(car.homeLot))
                {
                    car.purpose = TRIP_DEPOT;
                    car.targetLot = car.homeLot;
                    car.destX = car.destZ = 0xFFFFFFFFu;  
                }
            }
            else if (!map.reachable(car.homeLot) || !map.reachable(car.workLot) || !map.reachable(car.targetLot))
            {
                car.homeLot = homeLotOf(map, car.homeLot, car.id, car.trips);
                car.workLot = workLotOf(map, car.workLot, car.homeLot, car.id, car.trips);
                if (!map.reachable(car.targetLot))
                {
                    car.targetLot = car.purpose == TRIP_WORK ? car.workLot : car.homeLot;
                    car.purpose = car.purpose == TRIP_WORK ? TRIP_WORK : TRIP_HOME;
                }
            }
            if (graph.nodeAt(car.destX, car.destZ) == NO_ID)
            {
                const u32 node = tripNode(map, car.targetLot, car.id, car.trips, NO_ID);
                if (node != NO_ID)
                {
                    car.destX = graph.nodes[node].x;
                    car.destZ = graph.nodes[node].z;
                }
            }
            if (car.state == CAR_DRIVING && car.plan != PLAN_NONE)
            {
                u32 node = NO_ID;
                MovementPath path;
                if (car.plan == PLAN_GRANTED || !carMovement(car, graph, node, path))
                {
                     
                     
                    clearMovement(car);
                }
            }
            if (graph.edgeUnder(car.axis, car.line, car.pos, car.dir) == NO_ID)
            {
                relocateCar(step, row, car, slot);
                return;
            }
            if (car.state == CAR_PARKED && !garaged(car) && !standsInBay(graph, car.axis, car.line, car.pos, car.dir))
            {
                 
                const u32 edge = graph.edgeUnder(car.axis, car.line, car.pos, car.dir);
                const LotRef* lot = map.reachable(car.targetLot);
                claimPark(row, car, nearestEnd(graph.edges[edge], car.pos), edge, lot ? lot->edge : NO_ID, false, true);
                return;
            }
            const bool following = car.routeState == ROUTE_READY || car.routeState == ROUTE_REROUTING;
            if (following && !graph.routeValid(slot.active, car.routeIndex))
            {
                step.paths->countReroute();
                dropRoute(car, slot);
                slot.active.clear();
                const u32 ahead = aheadNode(step, car);
                if (car.state == CAR_DRIVING && ahead != NO_ID)
                {
                    car.routeState = ROUTE_PENDING;
                    requestCarRoute(step, row, car, nodeKeyOf(step, ahead), headingOf(car.axis, car.dir));
                }
            }
        }

         

        void requestWalk(const Step& step, const Row& row, CityPedestrianComponent& ped)
        {
            const CityMap& map = *step.map;
            const LotRef* lot = map.reachable(ped.targetLot);
            const u32 from = step.graph->nodeAt(ped.x, ped.z);
            if (!lot || from == NO_ID)
            {
                ped.routeState = ROUTE_FAILED;
                ped.mode = PED_DWELLING;
                ped.dwellUntil = step.step + PED_RETRY_STEPS;
                assignWalk(ped, nextWalk(ped, *step.city, map, step.step), map);
                return;
            }
             
            const u32 choice =
                static_cast<u32>(mix(step.city->seed ^ GOAL_SALT ^ (static_cast<u64>(ped.id) << 32 | ped.legs) ^
                                     (static_cast<u64>(ped.routeVersion) << 8)) %
                                 2);
            const u32 node = map.accessNode(*lot, choice);
            ++ped.routeVersion;
            step.paths->pedestrian(ped.id).expected = ped.routeVersion;
            row.outbox.add({.entity = row.entity,
                            .kind = RouteKind::Pedestrian,
                            .id = ped.id,
                            .version = ped.routeVersion,
                            .from = cornerKey(*step.graph, from, ped.corner & 3),
                            .to = cornerKey(*step.graph, node, map.accessCorner(*lot, node)),
                            .issuedStep = step.step});
        }

         
        void arriveWalk(const Step& step, CityPedestrianComponent& ped, RouteSlot& slot)
        {
            ++ped.legs;
            assignWalk(ped, nextWalk(ped, *step.city, *step.map, step.step), *step.map);
            ped.mode = PED_DWELLING;
            ped.routeState = ROUTE_NONE;
            ped.routeIndex = 0;
            ped.routeLength = 0;
            slot.expected = 0;
            slot.active.clear();
        }

         
        void askAgain(const Step& step, CityPedestrianComponent& ped)
        {
            ped.mode = PED_DWELLING;
            ped.dwellUntil = step.step;
            ped.routeState = ROUTE_NONE;
            ped.progress = 0.0f;
            ped.toX = ped.x;
            ped.toZ = ped.z;
            ped.toCorner = ped.corner;
        }

         
         
        void startLeg(const Step& step, CityPedestrianComponent& ped, RouteSlot& slot)
        {
            ped.crowdSpot = ~0u;
            if (slot.active.size() <= ped.routeIndex)
            {
                askAgain(step, ped);  
                return;
            }
            if (ped.routeIndex + 1 >= slot.active.size())
            {
                arriveWalk(step, ped, slot);
                return;
            }
            if (lateWalkEnds(*step.city, ped.id, step.step))
            {
                 
                ped.purpose = TRIP_HOME;
                ped.carLot = NO_LOT;
                arriveWalk(step, ped, slot);
                relocatePedestrian(ped, *step.map, ped.homeLot);
                return;
            }
            const RoadGraph& graph = *step.graph;
            const u32 onward = slot.active[ped.routeIndex + 1];
            const u32 toNode = graph.nodeOfKey(onward / 4);
            const u32 kind = walkKind(graph, graph.nodeAt(ped.x, ped.z), ped.corner & 3, toNode, onward & 3);
            if (kind == 0)
            {
                askAgain(step, ped);  
                return;
            }
            ped.toX = graph.nodes[toNode].x;
            ped.toZ = graph.nodes[toNode].z;
            ped.toCorner = onward & 3;
            ped.progress = 0.0f;
            ped.lane = personLane(ped.id);
            ped.mode = kind == WALK_CROSS ? PED_WAITING : PED_WALKING;
            ped.dwellUntil = kind == WALK_CROSS ? step.step : ped.dwellUntil;  
        }

         
        void reachCorner(CityPedestrianComponent& ped)
        {
            ped.x = ped.toX;
            ped.z = ped.toZ;
            ped.corner = ped.toCorner;
            ped.progress = 0.0f;
            ++ped.routeIndex;
        }

         
         
         
         
         
         
        constexpr u32 WALK_SNAPSHOT_STEPS =
            8;  
        constexpr float FOLLOW_STOP = 0.5f;  
        constexpr float FOLLOW_FREE = 0.9f;  
        constexpr float START_ZONE = 0.55f;  
        constexpr float SIDE_BEHIND = 0.7f;  
        constexpr float SIDE_AHEAD = 1.0f;  
        constexpr u8 WALK_SIDE_FREE = 1;
        constexpr u8 WALK_START_BLOCKED = 2;

        struct WalkEntry
        {
            u64 key = 0;
            float progress = 0.0f;
            u32 id = 0;
            u8 lane = 0;
            u8 moving = 0;
            bool crossing = false;
        };

        void snapshotWalkers(SystemContext& context, const Step& step)
        {
            PathService::WalkerSnapshot& w = step.paths->walkers();
            if (w.gap.size() == step.pedestrianCount && w.builtAt != NEVER && step.step >= w.builtAt &&
                step.step - w.builtAt < WALK_SNAPSHOT_STEPS && step.step % WALK_SNAPSHOT_STEPS != 0)
            {
                return;
            }
            w.gap.assign(step.pedestrianCount, NO_LEADER);
            w.flags.assign(step.pedestrianCount, 0);
            w.crowdSpots.assign(step.pedestrianCount, ~0u);
            w.builtAt = step.step;
            const RoadGraph& graph = *step.graph;
            std::vector<WalkEntry> entries;
            std::vector<CityPedestrianComponent> standing;
            std::vector<Vec3> movingPlaces;
            std::mutex lock;
            context.query<ecs::Read<CityPedestrianComponent>>()->forEachChunkParallelConst(
                [&](const Chunk* chunk, u32)
                {
                    const CityPedestrianComponent* rows = chunk->readColumn<CityPedestrianComponent>();
                    std::vector<WalkEntry> local;
                    std::vector<CityPedestrianComponent> localStanding;
                    std::vector<Vec3> localPlaces;
                    for (sizet row = 0; row < chunk->size(); ++row)
                    {
                        const CityPedestrianComponent& ped = rows[row];
                        const bool moving =
                            ped.mode == PED_WALKING || ped.mode == PED_CROSSING || ped.mode == PED_MEDIAN;
                        if (ped.id >= step.pedestrianCount || (!moving && ped.mode != PED_WAITING))
                        {
                            continue;
                        }
                        const u32 from = graph.nodeAt(ped.x, ped.z);
                        const u32 to = graph.nodeAt(ped.toX, ped.toZ);
                        if (from == NO_ID || to == NO_ID)
                        {
                            continue;
                        }
                        const u64 key = static_cast<u64>(cornerKey(graph, from, ped.corner & 3)) << 32 |
                            cornerKey(graph, to, ped.toCorner & 3);
                        local.push_back({key, moving ? ped.progress : 0.0f, ped.id,
                                         static_cast<u8>(ped.lane % PERSON_LANES), static_cast<u8>(moving ? 1 : 0),
                                         ped.mode != PED_WALKING});
                        if (ped.mode == PED_WAITING || ped.mode == PED_MEDIAN)
                            localStanding.push_back(ped);
                        else
                        {
                            TransformComponent placed;
                            placePedestrian(ped, graph, placed, true);
                            localPlaces.push_back(placed.position);
                        }
                    }
                    if (!local.empty())
                    {
                        const std::scoped_lock hold(lock);
                        entries.insert(entries.end(), local.begin(), local.end());
                        standing.insert(standing.end(), localStanding.begin(), localStanding.end());
                        movingPlaces.insert(movingPlaces.end(), localPlaces.begin(), localPlaces.end());
                    }
                });
            std::sort(entries.begin(), entries.end(),
                      [](const WalkEntry& a, const WalkEntry& b)
                      {
                          return a.key != b.key          ? a.key < b.key
                              : a.progress != b.progress ? a.progress < b.progress
                                                         : a.id < b.id;
                      });
            const sizet total = entries.size();
            for (sizet begin = 0; begin < total;)
            {
                sizet end = begin;
                while (end < total && entries[end].key == entries[begin].key)
                {
                    ++end;
                }
                for (sizet i = begin; i < end; ++i)
                {
                    const WalkEntry& e = entries[i];
                    u8 flags = 0;
                    if (e.moving != 0)
                    {
                        bool sideBlocked = false;
                        bool found = false;
                        for (sizet j = i + 1; j < end && entries[j].progress - e.progress < SIDE_AHEAD; ++j)
                        {
                            if (entries[j].moving == 0)
                            {
                                continue;
                            }
                            if (entries[j].lane == e.lane)
                            {
                                if (!found)
                                {
                                    w.gap[e.id] = entries[j].progress - e.progress;
                                    found = true;
                                }
                            }
                            else
                            {
                                sideBlocked = true;
                            }
                        }
                        for (sizet j = i; j > begin && e.progress - entries[j - 1].progress < SIDE_BEHIND; --j)
                        {
                            sideBlocked = sideBlocked || (entries[j - 1].moving != 0 && entries[j - 1].lane != e.lane);
                        }
                        flags = sideBlocked ? 0 : WALK_SIDE_FREE;
                    }
                    else
                    {
                         
                         
                        bool blocked = false;
                        for (sizet j = begin; j < end; ++j)
                        {
                            blocked = blocked ||
                                (j != i && entries[j].lane == e.lane &&
                                 ((entries[j].moving != 0 && (e.crossing || entries[j].progress < START_ZONE)) ||
                                  (entries[j].moving == 0 && entries[j].id < e.id)));
                        }
                        flags = blocked ? WALK_START_BLOCKED : 0;
                    }
                    w.flags[e.id] = flags;
                }
                begin = end;
            }
             
             
            std::unordered_map<u64, std::vector<Vec3>> occupied;
            const auto cell = [](float value) { return static_cast<i32>(std::floor(value)); };
            const auto key = [](i32 x, i32 z) { return (u64(static_cast<u32>(x)) << 32) | static_cast<u32>(z); };
            const auto reserve = [&](const Vec3& at) { occupied[key(cell(at.x), cell(at.z))].push_back(at); };
            for (const Vec3& at : movingPlaces)
                reserve(at);
            const auto clear = [&](const Vec3& at)
            {
                for (i32 x = cell(at.x) - 1; x <= cell(at.x) + 1; ++x)
                    for (i32 z = cell(at.z) - 1; z <= cell(at.z) + 1; ++z)
                        if (const auto found = occupied.find(key(x, z)); found != occupied.end())
                            for (const Vec3& other : found->second)
                                if (std::hypot(at.x - other.x, at.z - other.z) < 0.85f)
                                    return false;
                return true;
            };
            std::ranges::sort(standing, {}, &CityPedestrianComponent::id);
            for (CityPedestrianComponent ped : standing)
            {
                TransformComponent placed;
                if (ped.crowdSpot != ~0u)
                    placePedestrian(ped, graph, placed, true);
                if (ped.crowdSpot == ~0u || !clear(placed.position))
                    for (u32 spot = 0; spot < step.pedestrianCount * 4; ++spot)
                    {
                        ped.crowdSpot = spot;
                        placePedestrian(ped, graph, placed, true);
                        if (clear(placed.position))
                            break;
                    }
                w.crowdSpots[ped.id] = ped.crowdSpot;
                reserve(placed.position);
            }
        }

         
         
        float followScale(const float gap)
        {
            return std::clamp((gap - FOLLOW_STOP) / (FOLLOW_FREE - FOLLOW_STOP), 0.0f, 1.0f);
        }

        void stepPedestrian(const Step& step, const Row& row, CityPedestrianComponent& ped)
        {
            if (ped.id >= step.pedestrianCount)
            {
                return;
            }
            if ((ped.mode == PED_WAITING || ped.mode == PED_MEDIAN) && ped.id < step.paths->walkers().crowdSpots.size())
                ped.crowdSpot = step.paths->walkers().crowdSpots[ped.id];
            const RoadGraph& graph = *step.graph;
            RouteSlot& slot = step.paths->pedestrian(ped.id);
            if (ped.mode == PED_DWELLING)
            {
                if (step.step >= ped.dwellUntil)
                {
                    ped.mode = PED_AWAITING_ROUTE;
                    ped.routeState = ROUTE_PENDING;
                    requestWalk(step, row, ped);
                }
                return;
            }
            if (ped.mode == PED_AWAITING_ROUTE)
            {
                if (slot.incomingVersion != ped.routeVersion)
                {
                    if (slot.expected != ped.routeVersion)
                    {
                        requestWalk(step, row, ped);  
                    }
                    return;
                }
                slot.expected = 0;
                slot.incomingVersion = 0;
                const u32 here = graph.nodeAt(ped.x, ped.z);
                if (slot.incomingFailed || slot.incoming.empty() || here == NO_ID ||
                    slot.incoming.front() != cornerKey(graph, here, ped.corner & 3))
                {
                    ped.routeState = ROUTE_FAILED;
                    ped.mode = PED_DWELLING;
                    ped.dwellUntil = step.step + PED_RETRY_STEPS;
                    return;
                }
                slot.active.swap(slot.incoming);
                ped.routeState = ROUTE_READY;
                ped.routeIndex = 0;
                ped.routeLength = static_cast<u32>(slot.active.size());
                startLeg(step, ped, slot);
                return;
            }
            const float travel = ped.speed * step.dt;
            if (travel <= 0.0f)
            {
                return;
            }
            const u32 from = graph.nodeAt(ped.x, ped.z);
            const u32 to = graph.nodeAt(ped.toX, ped.toZ);
            if (from == NO_ID || to == NO_ID)
            {
                askAgain(step, ped);  
                return;
            }
            const float length =
                walkLength(cornerPoint(graph, from, ped.corner & 3), cornerPoint(graph, to, ped.toCorner & 3));
            if (ped.mode == PED_WAITING || ped.mode == PED_MEDIAN || ped.mode == PED_CROSSING)
            {
                u32 crossed = NO_ID;
                if (walkKind(graph, from, ped.corner & 3, to, ped.toCorner & 3, &crossed) != WALK_CROSS)
                {
                    askAgain(step, ped);
                    return;
                }
                const GraphNode& n = graph.nodes[from];
                 
                const bool halves =
                    n.signal && std::ceil(length / travel) + 2.0f > static_cast<float>(signalSteps(*step.city));
                const float leg = halves ? length * 0.5f : length;
                if (ped.mode != PED_CROSSING)
                {
                    if (step.step - ped.dwellUntil >= PED_WAIT_STEPS)
                    {
                         
                        askAgain(step, ped);
                        return;
                    }
                     
                    const u32 walkAxis = 1 - graph.edges[crossed].axis;
                    const u32 legSteps = static_cast<u32>(std::ceil(leg / travel));
                    if (n.signal &&
                        (greenAxis(*step.city, n.x, n.z, step.step) != walkAxis ||
                         phaseStepsLeft(*step.city, n.x, n.z, step.step) < legSteps + 2))
                    {
                        return;
                    }
                    const PathService::WalkerSnapshot& walkers = step.paths->walkers();
                    if (ped.mode == PED_WAITING && ped.id < walkers.flags.size() &&
                        (walkers.flags[ped.id] & WALK_START_BLOCKED) != 0)
                    {
                        return;  
                    }
                    ped.progress = ped.mode == PED_WAITING ? 0.0f : ped.progress;
                    ped.mode = PED_CROSSING;
                    return;
                }
                const bool firstHalf = ped.progress < leg;
                ped.progress += travel;
                if (halves && firstHalf && ped.progress >= leg)
                {
                    ped.progress = leg;
                    ped.mode = PED_MEDIAN;
                    ped.dwellUntil = step.step;
                    return;
                }
                if (ped.progress >= length)
                {
                    reachCorner(ped);
                    ++ped.crossings;
                    startLeg(step, ped, slot);
                }
                return;
            }
            const PathService::WalkerSnapshot& walkers = step.paths->walkers();
            float scale = 1.0f;
            if (ped.mode == PED_WALKING && ped.id < walkers.gap.size())
            {
                scale = followScale(walkers.gap[ped.id]);
                if (scale < 1.0f && step.step % WALK_SNAPSHOT_STEPS == 0 &&
                    (walkers.flags[ped.id] & WALK_SIDE_FREE) != 0)
                {
                    ped.lane = (ped.lane + 1) % PERSON_LANES;  
                }
            }
            ped.progress += travel * scale;
            if (ped.progress >= length)
            {
                reachCorner(ped);
                startLeg(step, ped, slot);
            }
        }

         
        void checkPedestrian(const Step& step, CityPedestrianComponent& ped)
        {
            if (ped.id >= step.pedestrianCount)
            {
                return;
            }
            const CityMap& map = *step.map;
            const RoadGraph& graph = *step.graph;
            ped.homeLot = homeLotOf(map, ped.homeLot, ped.id, PED_SALT ^ ped.legs);
            if (!map.reachable(ped.targetLot))
            {
                ped.targetLot = ped.homeLot;
                ped.purpose = TRIP_HOME;
            }
            if (!map.reachable(ped.carLot))
            {
                ped.carLot = NO_LOT;
            }
            const u32 from = graph.nodeAt(ped.x, ped.z);
            const bool moving = ped.mode == PED_WALKING || ped.mode == PED_WAITING || ped.mode == PED_CROSSING ||
                ped.mode == PED_MEDIAN;
            if (from != NO_ID &&
                (!moving ||
                 walkKind(graph, from, ped.corner & 3, graph.nodeAt(ped.toX, ped.toZ), ped.toCorner & 3) != 0))
            {
                return;
            }
            RouteSlot& slot = step.paths->pedestrian(ped.id);
            slot.expected = 0;
            slot.active.clear();
            if (from != NO_ID)
            {
                askAgain(step, ped);
                return;
            }
            const LotRef* home = map.reachable(ped.homeLot);
            if (!home)
            {
                return;
            }
            const u32 node = map.accessNode(*home, 0);
            ped.x = graph.nodes[node].x;
            ped.z = graph.nodes[node].z;
            ped.corner = map.accessCorner(*home, node);
            askAgain(step, ped);
        }

         

         
         
        bool heldMovement(const Step& step, const CityCarComponent& car, u32& node, u32& index)
        {
            node = car.plan == PLAN_NONE ? NO_ID : step.graph->nodeOfKey(car.node);
            index = node == NO_ID ? NO_MOVE : step.junctions->at(node).indexOf(car.move);
            return index != NO_MOVE;
        }

         
         
        void exitPoint(const Step& step, const u32 node, const u32 code, u32& group, float& centre, float& exitS)
        {
            const GraphNode& n = step.graph->nodes[node];
            const u32 out = moveHeadingOut(code);
            const u32 axis = axisOfHeading(out);
            const float dir = dirOfHeading(out);
            group = groupOf(laneOf(step.size, axis, axis == 0 ? n.z : n.x, dir), moveSubOut(code));
            centre = dir * static_cast<float>(alongOf(step, node, axis)) * CELL;
            exitS = centre + stopDistance(n, out);
        }

         
         
        u32 holdJunctions(SystemContext& context, const Step& step, Traffic& t)
        {
            t.reserved.clear();
            std::atomic<u32> conflicts{0};
            std::mutex lock;
            context.parallelFor(static_cast<u32>(step.graph->nodes.size()), 256,
                                [&](const u32 begin, const u32 end)
                                {
                                    std::vector<Reservation> kept;
                                    std::vector<BodyPose> bodies;
                                    u32 overlaps = 0;
                                    for (u32 node = begin; node < end; ++node)
                                    {
                                        const u32 first = t.holderStart[node];
                                        const u32 last = t.holderStart[node + 1];
                                        if (first == last)
                                        {
                                            continue;
                                        }
                                        const JunctionType& type = step.junctions->at(node);
                                        u32 into[16] = {};  
                                        bodies.clear();
                                        for (u32 i = first; i < last; ++i)
                                        {
                                            const u32 car = t.holders[i];
                                            ++into[type.codes[t.moveIndex[car]] >> 4];
                                            if (t.plan[car] == PLAN_MOVING)
                                            {
                                                bodies.push_back(
                                                    bodyOnPath(type.paths[t.moveIndex[car]], t.progress[car]));
                                            }
                                        }
                                        for (u32 exit = 0; exit < 16; ++exit)
                                        {
                                            if (into[exit] == 0)
                                            {
                                                continue;
                                            }
                                            Reservation reservation;
                                            float centre = 0.0f;
                                            float exitS = 0.0f;
                                            exitPoint(step, node, exit << 4, reservation.group, centre, exitS);
                                            reservation.from = exitS - CAR_LENGTH;
                                            reservation.to = exitS + static_cast<float>(into[exit]) * SLOT;
                                            kept.push_back(reservation);
                                        }
                                        constexpr float REACH = 2.0f * (CAR_HALF_LENGTH + CAR_HALF_WIDTH);
                                        for (sizet a = 0; a < bodies.size(); ++a)
                                        {
                                            for (sizet b = a + 1; b < bodies.size(); ++b)
                                            {
                                                overlaps += std::fabs(bodies[a].x - bodies[b].x) < REACH &&
                                                        std::fabs(bodies[a].z - bodies[b].z) < REACH &&
                                                        bodiesOverlap(bodies[a], CAR_HALF_LENGTH, CAR_HALF_WIDTH,
                                                                      bodies[b], CAR_HALF_LENGTH, CAR_HALF_WIDTH)
                                                    ? 1
                                                    : 0;
                                            }
                                        }
                                    }
                                    conflicts.fetch_add(overlaps, std::memory_order_relaxed);
                                    if (!kept.empty())
                                    {
                                        const std::scoped_lock hold(lock);
                                        t.reserved.insert(t.reserved.end(), kept.begin(), kept.end());
                                    }
                                });
             
            std::sort(
                t.reserved.begin(), t.reserved.end(), [](const Reservation& a, const Reservation& b)
                { return a.group != b.group ? a.group < b.group
                      : a.from != b.from    ? a.from < b.from
                                            : a.to < b.to; });
            return conflicts.load(std::memory_order_relaxed);
        }

         
         
        void snapshotTraffic(SystemContext& context, Query& cars, Traffic& t, const Step& step)
        {
            const RoadGraph& graph = *step.graph;
            const u32 lanes = laneCount(step.size);
            const u32 groups = lanes * MAX_LANES;
            const sizet nodes = graph.nodes.size();
            t.lane.assign(step.carCount, NO_LANE);
            t.sub.assign(step.carCount, 0);
            t.ghostSub.assign(step.carCount, 0xFF);
            t.ghostWish.assign(step.carCount, 0);
            t.entrantLane.assign(step.carCount, NO_LANE);
            t.s.resize(step.carCount);
            t.speed.assign(step.carCount, 0.0f);
            t.gap.assign(step.carCount, NO_LEADER);
            t.next.assign(step.carCount, NO_ID);
            t.prev.assign(step.carCount, NO_ID);
            t.plan.assign(step.carCount, static_cast<u8>(PLAN_NONE));
            t.nodeOf.assign(step.carCount, NO_ID);
            t.moveIndex.assign(step.carCount, NO_MOVE);
            t.progress.assign(step.carCount, -1.0f);
            t.order.assign(step.carCount, 0);
            t.park.assign(nodes * 8, 0);
            cars.forEachChunkParallelConst(
                [&](const Chunk* chunk, u32)
                {
                    const CityCarComponent* rows = chunk->readColumn<CityCarComponent>();
                    for (sizet row = 0; row < chunk->size(); ++row)
                    {
                        const CityCarComponent& car = rows[row];
                        if (car.id >= step.carCount || car.axis > 1 || car.line > step.size)
                        {
                            continue;
                        }
                        const u32 lane = laneOf(step.size, car.axis, car.line, car.dir);
                        t.s[car.id] = car.dir * car.pos;
                        if (car.state == CAR_DRIVING)
                        {
                            t.lane[car.id] = lane;
                            t.sub[car.id] = static_cast<u8>(std::min(car.sublane, MAX_LANES - 1));
                            if (car.slide != 0.0f && car.plan == PLAN_NONE)
                            {
                                t.ghostSub[car.id] = static_cast<u8>(slideFrom(car));
                            }
                            else if (car.wish != 0 && car.wish <= MAX_LANES && car.plan == PLAN_NONE)
                            {
                                t.ghostSub[car.id] = static_cast<u8>(car.wish - 1);
                                t.ghostWish[car.id] = 1;
                            }
                            t.speed[car.id] = car.speed;
                            u32 node = NO_ID;
                            u32 index = NO_MOVE;
                            if (heldMovement(step, car, node, index))
                            {
                                t.plan[car.id] = static_cast<u8>(car.plan);
                                t.nodeOf[car.id] = node;
                                t.moveIndex[car.id] = index;
                                 
                                t.progress[car.id] = car.plan == PLAN_MOVING ? car.moveS
                                                                             : car.dir * car.pos -
                                        (car.dir * static_cast<float>(graph.along(node, car.axis)) * CELL -
                                         stopDistance(graph.nodes[node], headingOf(car.axis, car.dir)));
                                t.order[car.id] = car.grantOrder;
                                t.s[car.id] += car.plan == PLAN_MOVING ? car.moveS : 0.0f;
                            }
                        }
                        else
                        {
                            u32 key = 0;
                            u64 bits = 0;
                            if (!garaged(car) && curbBits(graph, car.axis, car.line, car.pos, car.dir, key, bits))
                            {
                                std::atomic_ref<u64>(t.park[key]).fetch_or(bits, std::memory_order_relaxed);
                            }
                            if (car.state == CAR_AWAITING_ROUTE &&
                                (car.routeState != ROUTE_PENDING ||
                                 step.paths->car(car.id).incomingVersion == car.routeVersion))
                            {
                                t.entrantLane[car.id] = lane;  
                            }
                        }
                    }
                });
             
             
            t.groupStart.assign(groups + 1, 0);
            t.holderStart.assign(nodes + 1, 0);
            t.entrantStart.assign(lanes + 1, 0);
            const auto bump = [](std::vector<u32>& counts, const sizet at)
            { return std::atomic_ref<u32>(counts[at]).fetch_add(1, std::memory_order_relaxed); };
            context.parallelFor(step.carCount, 4096,
                                [&](const u32 begin, const u32 end)
                                {
                                    for (u32 id = begin; id < end; ++id)
                                    {
                                        if (t.lane[id] != NO_LANE)
                                        {
                                            bump(t.groupStart, groupOf(t.lane[id], t.sub[id]) + 1);
                                            if (t.ghostSub[id] != 0xFF)
                                            {
                                                bump(t.groupStart, groupOf(t.lane[id], t.ghostSub[id]) + 1);
                                            }
                                        }
                                        else if (t.entrantLane[id] != NO_LANE)
                                        {
                                            bump(t.entrantStart, t.entrantLane[id] + 1);
                                        }
                                        if (t.nodeOf[id] != NO_ID)
                                        {
                                            bump(t.holderStart, t.nodeOf[id] + 1);
                                        }
                                    }
                                });
            for (u32 group = 0; group < groups; ++group)
            {
                t.groupStart[group + 1] += t.groupStart[group];
            }
            for (sizet node = 0; node < nodes; ++node)
            {
                t.holderStart[node + 1] += t.holderStart[node];
            }
            for (u32 lane = 0; lane < lanes; ++lane)
            {
                t.entrantStart[lane + 1] += t.entrantStart[lane];
            }
            t.groupCars.resize(t.groupStart[groups]);
            t.groupS.resize(t.groupStart[groups]);
            t.holders.resize(t.holderStart[nodes]);
            t.entrants.resize(t.entrantStart[lanes]);
            {
                std::vector<u32> cursor(t.groupStart.begin(), t.groupStart.end() - 1);
                std::vector<u32> holderCursor(t.holderStart.begin(), t.holderStart.end() - 1);
                std::vector<u32> entrantCursor(t.entrantStart.begin(), t.entrantStart.end() - 1);
                context.parallelFor(step.carCount, 4096,
                                    [&](const u32 begin, const u32 end)
                                    {
                                        for (u32 id = begin; id < end; ++id)
                                        {
                                            if (t.lane[id] != NO_LANE)
                                            {
                                                t.groupCars[bump(cursor, groupOf(t.lane[id], t.sub[id]))] = id;
                                                if (t.ghostSub[id] != 0xFF)
                                                {
                                                    t.groupCars[bump(cursor, groupOf(t.lane[id], t.ghostSub[id]))] = id;
                                                }
                                            }
                                            else if (t.entrantLane[id] != NO_LANE)
                                            {
                                                t.entrants[bump(entrantCursor, t.entrantLane[id])] = {t.entrantLane[id],
                                                                                                      t.s[id], id};
                                            }
                                            if (t.nodeOf[id] != NO_ID)
                                            {
                                                t.holders[bump(holderCursor, t.nodeOf[id])] = id;
                                            }
                                        }
                                    });
            }
             
            t.edgeCars.assign(graph.edges.size() * 2, 0);
            context.parallelFor(
                lanes, 4,
                [&](const u32 begin, const u32 end)
                {
                    for (u32 lane = begin; lane < end; ++lane)
                    {
                        std::sort(t.entrants.begin() + t.entrantStart[lane],
                                  t.entrants.begin() + t.entrantStart[lane + 1]);
                        const u32 axis = lane / 2 / (step.size + 1);
                        const u32 line = lane / 2 % (step.size + 1);
                        const float dir = (lane & 1) != 0 ? -1.0f : 1.0f;
                        for (u32 sub = 0; sub < MAX_LANES; ++sub)
                        {
                            const u32 group = groupOf(lane, sub);
                            const auto first = t.groupCars.begin() + t.groupStart[group];
                            const auto last = t.groupCars.begin() + t.groupStart[group + 1];
                            std::sort(first, last, [&t](const u32 a, const u32 b)
                                      { return t.s[a] < t.s[b] || (t.s[a] == t.s[b] && a < b); });
                            for (u32 i = t.groupStart[group]; i < t.groupStart[group + 1]; ++i)
                            {
                                const u32 id = t.groupCars[i];
                                t.groupS[i] = t.s[id];
                                if (t.sub[id] != sub)
                                {
                                    continue;  
                                }
                                 
                                 
                                u32 j = i + 1;
                                while (j < t.groupStart[group + 1] && t.ghostWish[t.groupCars[j]] != 0 &&
                                       t.sub[t.groupCars[j]] != sub && t.s[t.groupCars[j]] - t.s[id] < SLOT)
                                {
                                    ++j;
                                }
                                if (j < t.groupStart[group + 1])
                                {
                                    const u32 ahead = t.groupCars[j];
                                    t.gap[id] = t.s[ahead] - t.s[id] - CAR_LENGTH;
                                    t.next[id] = ahead;
                                    if (t.sub[ahead] == sub)
                                    {
                                        t.prev[ahead] = id;
                                    }
                                }
                                const u32 edge = graph.edgeUnder(axis, line, dir * t.s[id], dir);
                                if (edge != NO_ID)
                                {
                                    ++t.edgeCars[static_cast<sizet>(edge) * 2 + (lane & 1)];
                                }
                            }
                        }
                    }
                });
            const u32 conflicts = holdJunctions(context, step, t);
            if (conflicts > 0)
            {
                step.counters->conflicts.fetch_add(conflicts, std::memory_order_relaxed);
            }
        }

         
         
         
         
        bool exitHasRoom(const Step& step, const Traffic& t, const std::vector<Join>& joins, const u32 node,
                         const u32 code, const u32 granted)
        {
            const RoadGraph& graph = *step.graph;
            u32 group = 0;
            float centre = 0.0f;
            float exitS = 0.0f;
            exitPoint(step, node, code, group, centre, exitS);
            const u32 out = moveHeadingOut(code);
            const u32 edge = graph.nodes[node].edges[out];
            float limit = centre + graph.edges[edge].length() - stopDistance(graph.nodes[graph.other(edge, node)], out);
            const float* base = t.groupS.data();
            const float* end = base + t.groupStart[group + 1];
            for (const float* it = std::upper_bound(base + t.groupStart[group], end, centre); it != end; ++it)
            {
                const u32 id = t.groupCars[it - base];
                if (t.plan[id] != PLAN_MOVING)
                {
                     
                    const float stops =
                        std::min(t.speed[id] * t.speed[id] / (2.0f * BRAKING), std::max(t.gap[id] - MIN_GAP, 0.0f));
                    limit = std::min(limit, *it + stops - SLOT);
                    break;
                }
            }
            for (auto it = std::lower_bound(joins.begin(), joins.end(), group,
                                            [](const Join& join, const u32 g) { return join.group < g; });
                 it != joins.end() && it->group == group; ++it)
            {
                if (it->s > centre)
                {
                    limit = std::min(limit, it->s - SLOT);
                    break;
                }
            }
            return exitS + static_cast<float>(granted) * SLOT <= limit + 1e-3f;
        }

         
         
         
         
         
         
        u32 grantMovements(SystemContext& context, const Step& step, const Traffic& t)
        {
            std::vector<MoveRequest>& requests = step.counters->requests;
            std::sort(requests.begin(), requests.end(),
                      [](const MoveRequest& a, const MoveRequest& b)
                      {
                          return a.node != b.node        ? a.node < b.node
                              : a.queuedAt != b.queuedAt ? a.queuedAt < b.queuedAt
                                                         : a.id < b.id;
                      });
            std::vector<Join>& joins = step.counters->joins;
            std::sort(joins.begin(), joins.end(),
                      [](const Join& a, const Join& b) { return a.group != b.group ? a.group < b.group : a.s < b.s; });
            u32 refused = 0;
            std::vector<u32> granted;
            std::vector<u32> waiting;
            for (sizet first = 0; first < requests.size();)
            {
                const u32 node = requests[first].node;
                sizet last = first;
                while (last < requests.size() && requests[last].node == node)
                {
                    ++last;
                }
                const JunctionType& type = step.junctions->at(node);
                const u32* holders = t.holders.data() + t.holderStart[node];
                const u32 holderCount = t.holderStart[node + 1] - t.holderStart[node];
                granted.clear();
                waiting.clear();
                for (sizet r = first; r < last; ++r)
                {
                    const MoveRequest& request = requests[r];
                    const u32 m = type.indexOf(request.code);
                    if (m == NO_MOVE)
                    {
                        continue;
                    }
                    bool meets = false;
                    u32 into = 0;  
                    for (u32 h = 0; h < holderCount && !meets; ++h)
                    {
                        const u32 holder = holders[h];
                        const u32 held = t.moveIndex[holder];
                        into += (type.codes[held] >> 4) == (request.code >> 4) ? 1 : 0;
                         
                        const bool sameEntry = ((type.codes[held] ^ request.code) & 15) == 0;
                        const float leave = sameEntry ? -1.0f : type.leaveOf(held, m);
                        meets =
                            leave >= 0.0f && leave >= t.progress[holder] - 0.01f && type.meetOf(m, held) < MIN_YIELD;
                    }
                    for (const u32 other : granted)
                    {
                        into += (type.codes[other] >> 4) == (request.code >> 4) ? 1 : 0;
                        const bool blocks =
                            other != m && type.leaveOf(other, m) >= 0.0f && type.meetOf(m, other) < MIN_YIELD;
                        meets = meets || blocks;
                    }
                    for (const u32 other : waiting)
                    {
                        meets =
                            meets || (other != m && (type.leaveOf(other, m) >= 0.0f || type.leaveOf(m, other) >= 0.0f));
                    }
                    if (meets)
                    {
                        waiting.push_back(m);
                        ++refused;
                        continue;
                    }
                    if (!type.freeFlow && !exitHasRoom(step, t, joins, node, request.code, into))
                    {
                        ++refused;
                        continue;
                    }
                    CityCarComponent& car = context.getComponent<CityCarComponent>(request.entity);
                    car.plan = PLAN_GRANTED;
                    car.node = step.graph->keyOf(node);
                    car.move = request.code;
                    car.queuedAt = NEVER;
                    car.grantOrder = step.step * 64 + std::min(static_cast<u32>(granted.size()), 63u);
                    granted.push_back(m);
                }
                first = last;
            }
            requests.clear();
            joins.clear();
            return refused;
        }

         
        std::shared_ptr<const CityMap> mapOf(SystemContext& context, const CityStateComponent& city, PathService& paths,
                                             bool& built)
        {
            const std::shared_ptr<const CityMap>& kept = paths.map();
            built = !kept || paths.mapVersion() != city.designVersion || kept->seed() != city.seed ||
                kept->size() != city.size;
            if (!built)
            {
                return kept;
            }
            std::vector<DesignEdge> edges;
            context.query<ecs::Read<CityRoadComponent>>()->forEachChunk(
                [&](Chunk* chunk)
                {
                    const CityRoadComponent* rows = chunk->readColumn<CityRoadComponent>();
                    for (sizet row = 0; row < chunk->size(); ++row)
                    {
                        const CityRoadComponent& road = rows[row];
                        const bool straight = (road.x0 == road.x1) != (road.z0 == road.z1);
                        if (straight && road.x0 <= road.x1 && road.z0 <= road.z1 && road.x1 <= city.size &&
                            road.z1 <= city.size)
                        {
                            edges.push_back({road.x0, road.z0, road.x1, road.z1, road.roadClass, road.lanes});
                        }
                    }
                });
            std::vector<DesignLot> lots;
            context.query<ecs::Read<CityLotComponent>>()->forEachChunk(
                [&](Chunk* chunk)
                {
                    const CityLotComponent* rows = chunk->readColumn<CityLotComponent>();
                    for (sizet row = 0; row < chunk->size(); ++row)
                    {
                        const CityLotComponent& lot = rows[row];
                        lots.push_back(
                            {lot.id, lot.x0, lot.z0, lot.x1, lot.z1, lot.zone, lot.special, lot.axis, lot.line});
                    }
                });
            auto map = std::make_shared<const CityMap>(city.seed, city.size, buildGraph(city.size, edges), lots);
            paths.setMap(map, city.designVersion);
            return map;
        }
    }  

    static void stepCity(SystemContext& context);

     
     
    void moveCity(SystemContext& context)
    {
        u32 speed = 0;
        bool found = false;
        context.query<ecs::Read<CityStateComponent>>()->forEachChunk(
            [&](Chunk* chunk)
            {
                if (!found && !chunk->empty())
                {
                    found = true;
                    speed = std::min(chunk->readColumn<CityStateComponent>()[0].speed, MAX_SPEED_STEPS);
                }
            });
        if (!found)
        {
            return;
        }
        const float playback = static_cast<float>(speed);
        context.query<ecs::Read<CityAvatarComponent>, ecs::Write<AnimationPlayerComponent>>()->forEachChunk(
            [playback](Chunk* chunk)
            {
                AnimationPlayerComponent* players = chunk->mutableColumn<AnimationPlayerComponent>();
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    if (players[row].speed != playback)
                    {
                        players[row].speed = playback;
                        chunk->markModified<AnimationPlayerComponent>(row);
                    }
                }
            });
        for (u32 i = 0; i < speed; ++i)
        {
            stepCity(context);
        }
    }

     
     
    static void resolveParking(SystemContext& context, const Step& step, Traffic& t)
    {
        std::vector<ParkClaim>& claims = step.counters->claims;
        std::sort(claims.begin(), claims.end(), [](const ParkClaim& a, const ParkClaim& b) { return a.id < b.id; });
        for (const ParkClaim& claim : claims)
        {
            const ParkChoice choice =
                chooseParking(*step.graph, claim.node, claim.edge, claim.away, claim.lotEdge,
                              parkSpotOf(*step.city, *step.map, claim.id), t.park.data(), claim.street);
            CityCarComponent& car = context.getComponent<CityCarComponent>(claim.entity);
            const bool wasGaraged = garaged(car);
            const GraphEdge& on = step.graph->edges[choice.edge != NO_ID ? choice.edge : claim.edge];
            car.axis = on.axis;
            car.line = on.line;
            car.dir = choice.dir;
            car.pos = choice.pos;
            car.sublane = choice.garage ? PARK_GARAGE : 0;
            TransformComponent& transform = context.getComponent<TransformComponent>(claim.entity);
            const TransformComponent before = transform;
            placeVehicle(car, *step.graph, transform, *step.city);
            if (wasGaraged != garaged(car))
                queueCityFade(context, claim.entity, !garaged(car), before, transform);
        }
        claims.clear();
    }


    static void stepCity(SystemContext& context)
    {
        Chunk* stateChunk = nullptr;
        context.query<ecs::Write<CityStateComponent>>()->forEachChunk(
            [&stateChunk](Chunk* chunk)
            {
                if (!stateChunk && !chunk->empty())
                {
                    stateChunk = chunk;  
                }
            });
        if (!stateChunk)
        {
            return;
        }
        const CityStateComponent city = stateChunk->readColumn<CityStateComponent>()[0];
        if (city.size < MIN_SIZE || city.size > MAX_SIZE)
        {
            return;
        }
        if (!context.services().contains<PathService>())
        {
            return;
        }
        WriteService<PathService> paths = context.services().write<PathService>();
        paths->sync(city.size, city.cars, city.pedestrians, city.seed, city.step);
        paths->drain(context, city.step);
        bool mapChanged = false;
        const std::shared_ptr<const CityMap> map = mapOf(context, city, paths.get(), mapChanged);
        LogisticsService* logistics = nullptr;
        std::optional<WriteService<LogisticsService>> logisticsLock;
        if (context.services().contains<LogisticsService>())
        {
            logisticsLock.emplace(context.services().write<LogisticsService>());
            logistics = &logisticsLock->get();
            logistics->begin(context, city, *map, mapChanged);
        }
        TrafficLease lease;
        Traffic& t = lease.get();
        Counters counters;
        const Step step{.city = &city,
                        .step = city.step,
                        .dt = context.deltaTime,
                        .size = city.size,
                        .carCount = city.cars,
                        .pedestrianCount = city.pedestrians,
                        .traffic = &t,
                        .map = map.get(),
                        .graph = &map->graph(),
                        .junctions = paths->junctions().get(),
                        .mapChanged = mapChanged,
                        .paths = &paths.get(),
                        .logistics = logistics,
                        .counters = &counters};

        const auto cars = context.query<ecs::Write<CityCarComponent>, ecs::Write<TransformComponent>>();
        if (mapChanged)
        {
            cars->forEachChunkParallel(
                [&step](Chunk* chunk, u32)
                {
                    CityCarComponent* rows = chunk->mutableColumn<CityCarComponent>();
                    TransformComponent* transforms = chunk->mutableColumn<TransformComponent>();
                    Outbox outbox(*step.paths);
                    ChunkCounts counts(*step.counters);
                    for (sizet row = 0; row < chunk->size(); ++row)
                    {
                        const CityCarComponent before = rows[row];
                        checkCar(step, {chunk->entities()[row], outbox, counts}, rows[row]);
                        if (!sameBytes(before, rows[row]))
                        {
                            chunk->markModified<CityCarComponent>(row);
                        }
                        TransformComponent placed = transforms[row];
                        placeVehicle(rows[row], *step.graph, placed, *step.city);
                        writeIfMoved(*chunk, row, transforms[row], placed);
                    }
                });
        }
        snapshotTraffic(context, *cars, t, step);
        resolveParking(context, step, t);  
        paths->publish(t.edgeCars, map->graphPtr());
        cars->forEachChunkParallel(
            [&step, &context](Chunk* chunk, u32)
            {
                CityCarComponent* rows = chunk->mutableColumn<CityCarComponent>();
                TransformComponent* transforms = chunk->mutableColumn<TransformComponent>();
                Outbox outbox(*step.paths);
                ChunkCounts counts(*step.counters);
                std::optional<FreightOutbox> freight;
                if (step.logistics)
                {
                    freight.emplace(*step.logistics);
                }
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    const CityCarComponent before = rows[row];
                    stepCar(step, {chunk->entities()[row], outbox, counts, freight ? &*freight : nullptr}, rows[row]);
                    if (sameBytes(before, rows[row]))
                    {
                        continue;
                    }
                    chunk->markModified<CityCarComponent>(row);
                    TransformComponent placed = transforms[row];
                    placeVehicle(rows[row], *step.graph, placed, *step.city);
                    if (garaged(before) != garaged(rows[row]))
                        queueCityFade(context, chunk->entities()[row], !garaged(rows[row]), transforms[row], placed);
                    writeIfMoved(*chunk, row, transforms[row], placed);
                }
            });

        resolveParking(context, step, t);  
        const u32 refused = grantMovements(context, step, t);

        snapshotWalkers(context, step);
        context.query<ecs::Write<CityPedestrianComponent>, ecs::Write<TransformComponent>>()->forEachChunkParallel(
            [&step, &context](Chunk* chunk, u32)
            {
                CityPedestrianComponent* rows = chunk->mutableColumn<CityPedestrianComponent>();
                TransformComponent* transforms = chunk->mutableColumn<TransformComponent>();
                Outbox outbox(*step.paths);
                ChunkCounts counts(*step.counters);
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    const CityPedestrianComponent before = rows[row];
                    if (step.mapChanged)
                    {
                        checkPedestrian(step, rows[row]);
                    }
                    stepPedestrian(step, {chunk->entities()[row], outbox, counts}, rows[row]);
                    if (sameBytes(before, rows[row]))
                    {
                        continue;
                    }
                    chunk->markModified<CityPedestrianComponent>(row);
                    TransformComponent placed = transforms[row];
                    placePedestrian(rows[row], *step.graph, placed);
                    if (pedestrianIndoors(before) != pedestrianIndoors(rows[row]))
                        queueCityFade(context, chunk->entities()[row], !pedestrianIndoors(rows[row]), transforms[row],
                                      placed);
                    writeIfMoved(*chunk, row, transforms[row], placed);
                }
            });

         
        if (logistics)
        {
            stepDrones(context, city, *logistics, city.step, step.dt);
            placeParcels(context, city, *logistics, city.step);
            logistics->end(city, *map, city.step);
        }

         
        context.query<ecs::Write<CityLaneComponent>, ecs::Write<RenderTintComponent>>()->forEachChunkParallel(
            [&](Chunk* chunk, u32)
            {
                const RoadGraph& graph = *step.graph;
                CityLaneComponent* lanes = chunk->mutableColumn<CityLaneComponent>();
                RenderTintComponent* tints = chunk->mutableColumn<RenderTintComponent>();
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    CityLaneComponent& lane = lanes[row];
                    const u32 edge = lane.axis < 2 ? graph.edgeAt(lane.axis, lane.line, lane.from) : NO_ID;
                    if (edge == NO_ID || graph.edges[edge].lo != lane.from)
                    {
                        continue;
                    }
                    const u32 count = t.edgeCars[static_cast<sizet>(edge) * 2 + (lane.reverse != 0 ? 1 : 0)];
                    const u32 level = congestionLevel(count, edgeCapacity(graph.edges[edge]));
                    if (count == lane.cars && level == lane.level)
                    {
                        continue;
                    }
                    lane.cars = count;
                    chunk->markModified<CityLaneComponent>(row);
                    if (level != lane.level)
                    {
                        lane.level = level;
                        tints[row].color = congestionTint(level);
                        chunk->markModified<RenderTintComponent>(row);
                    }
                }
            });

         
        const float t1 = timeOfDay(city, city.step + 1);
        context.query<ecs::Write<CityBuildingComponent>>()->forEachChunkParallel(
            [t1](Chunk* chunk, u32)
            {
                CityBuildingComponent* buildings = chunk->mutableColumn<CityBuildingComponent>();
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    const u32 light = buildingLight(buildings[row], t1);
                    if (light != buildings[row].light)
                    {
                        buildings[row].light = light;
                        chunk->markModified<CityBuildingComponent>(row);
                    }
                }
            });
        context.query<ecs::Write<CityLampComponent>, ecs::Write<RenderTintComponent>>()->forEachChunkParallel(
            [t1](Chunk* chunk, u32)
            {
                CityLampComponent* lamps = chunk->mutableColumn<CityLampComponent>();
                RenderTintComponent* tints = chunk->mutableColumn<RenderTintComponent>();
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    const u32 on = inWindow(t1, lamps[row].onAt, lamps[row].offAt) ? 1u : 0u;
                    if (on != lamps[row].on)
                    {
                        lamps[row].on = on;
                        tints[row].color = lamps[row].glow != 0 ? Vec4(1.0f, 1.0f, 1.0f, 0.0f) : lampTint(on);
                        chunk->markModified<CityLampComponent>(row);
                        chunk->markModified<RenderTintComponent>(row);
                    }
                }
            });
        context
            .query<ecs::Write<CitySunComponent>, ecs::Write<DirectionalLightComponent>,
                   ecs::Write<TransformComponent>>()
            ->forEachChunk(
                [t1](Chunk* chunk)
                {
                    CitySunComponent* suns = chunk->mutableColumn<CitySunComponent>();
                    DirectionalLightComponent* lights = chunk->mutableColumn<DirectionalLightComponent>();
                    TransformComponent* transforms = chunk->mutableColumn<TransformComponent>();
                    for (sizet row = 0; row < chunk->size(); ++row)
                    {
                        if (turnSun(suns[row], lights[row], transforms[row], t1))
                        {
                            chunk->markModified<CitySunComponent>(row);
                            chunk->markModified<DirectionalLightComponent>(row);
                            chunk->markModified<TransformComponent>(row);
                        }
                    }
                });

         
         
        const float nightEv = cameraEv(t1);
        context.query<ecs::Read<CityCameraComponent>, ecs::Write<CameraComponent>>()->forEachChunk(
            [nightEv](Chunk* chunk)
            {
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    if (std::fabs(chunk->readColumn<CameraComponent>()[row].exposure - nightEv) > 0.01f)
                    {
                        chunk->mutableColumn<CameraComponent>()[row].exposure = nightEv;
                        chunk->markModified<CameraComponent>(row);
                    }
                }
            });

        CityStateComponent& written = stateChunk->mutableColumn<CityStateComponent>()[0];
        ++written.step;
        written.boxConflicts += counters.conflicts.load(std::memory_order_relaxed);
        written.boxHeld = refused;
        written.laneChanges += counters.changes.load(std::memory_order_relaxed);
        stateChunk->markModified<CityStateComponent>(0);
    }
}  
