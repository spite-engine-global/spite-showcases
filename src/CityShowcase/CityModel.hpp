#pragma once

#include <algorithm>
#include <bit>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <vector>

#include "CityCatalog.hpp"
#include "CityDesign.hpp"
#include "CityJunctions.hpp"
#include "CityShowcase.hpp"
#include "base/InternedString.hpp"
#include "base/MathFunctions.hpp"
#include "renderruntime/LightComponents.hpp"
#include "world/TransformComponents.hpp"

 
 
namespace spite::city::model
{
    constexpr float CAR_HALF_WIDTH = 0.9f;
    constexpr float CAR_HALF_HEIGHT = 0.6f;
    constexpr float CAR_LENGTH = 2.0f * CAR_HALF_LENGTH;
    constexpr float MIN_GAP = 2.0f;  
    constexpr float ACCELERATION = 2.5f;  
    constexpr float BRAKING = 4.5f;  
     
     
    constexpr float AMBER = 4.5f;
    constexpr float MAX_SPEED = 40.0f;  
    constexpr float PED_HALF = 0.25f;
    constexpr float PED_HALF_HEIGHT = 0.9f;

    constexpr u64 SIGNAL_SALT = 0x5167A1ull;
    constexpr u64 ROUTE_SALT = 0x7A11ull;
    constexpr u64 PED_SALT = 0x9ED5ull;

     

    inline u32 signalSteps(const CityStateComponent& city) { return std::max(city.signalSteps, 1u); }

    inline u32 signalPhaseStep(const CityStateComponent& city, const u32 x, const u32 z, const u32 step)
    {
        const u32 cycle = 2 * signalSteps(city);
        const u32 offset = static_cast<u32>(mix(city.seed ^ SIGNAL_SALT ^ (static_cast<u64>(x) << 32 | z)) % cycle);
        return static_cast<u32>((static_cast<u64>(step) + offset) % cycle);
    }

     
    inline u32 greenAxis(const CityStateComponent& city, const u32 x, const u32 z, const u32 step)
    {
        return signalPhaseStep(city, x, z, step) / signalSteps(city);
    }

     
    inline u32 phaseStepsLeft(const CityStateComponent& city, const u32 x, const u32 z, const u32 step)
    {
        return signalSteps(city) - signalPhaseStep(city, x, z, step) % signalSteps(city);
    }

    inline bool signalLensOn(const CityStateComponent& city, const CitySignalComponent& lens, const u32 step)
    {
        bool green = greenAxis(city, lens.x, lens.z, step) == lens.axis;
        if (lens.pedestrian)
            green = !green;
        return lens.color == SIGNAL_GREEN ? green : lens.color == SIGNAL_RED && !green;
    }

    inline TransformComponent trafficLightPlace(const GraphNode& node, const u32 approach)
    {
        constexpr float sx[] = {-1.0f, 1.0f, 1.0f, -1.0f};
        constexpr float sz[] = {-1.0f, -1.0f, 1.0f, 1.0f};
        constexpr float yaw[] = {1.5707963f, 0.0f, -1.5707963f, 3.1415927f};
        const float x = std::max(node.walk[1], node.half[1] + 0.75f);
        const float z = std::max(node.walk[0], node.half[0] + 0.75f);
        return TransformComponent{.position = {static_cast<float>(node.x) * CELL + sx[approach] * x, 0.15f,
                                               static_cast<float>(node.z) * CELL + sz[approach] * z},
                                  .rotation = fromAxisAngle({0.0f, 1.0f, 0.0f}, yaw[approach])};
    }

     

    inline float timeOfDay(const CityStateComponent& city, const u32 step)
    {
        const u32 day = std::max(city.daySteps, 1u);
        const float t = city.dayStart + static_cast<float>(step % day) / static_cast<float>(day);
        return t - std::floor(t);
    }

     
    inline bool inWindow(const float t, const float from, const float to)
    {
        return from <= to ? (t >= from && t < to) : (t >= from || t < to);
    }

    inline u32 buildingLight(const CityBuildingComponent& building, const float t)
    {
        return !inWindow(t, building.dusk, building.dawn) ? LIGHT_DAY : building.windows != 0 ? LIGHT_LIT : LIGHT_DARK;
    }

    inline Vec4 lampTint(const u32 on)
    {
        return on != 0 ? Vec4(1.0f, 0.92f, 0.62f, 1.0f) : Vec4(0.3f, 0.3f, 0.33f, 1.0f);
    }

     
    inline u32 sunLevel(const float t)
    {
        const float elevation = std::sin(6.2831853f * (t - 0.25f));
        const float level = std::round((elevation + 0.1f) / 1.1f * static_cast<float>(SUN_LEVELS - 1));
        return static_cast<u32>(std::clamp(level, 0.0f, static_cast<float>(SUN_LEVELS - 1)));
    }

     
     
     

    constexpr float SUN_TILT = 0.6f;  
    constexpr float SUN_NOON_INTENSITY = 3.0f;
    constexpr float MOON_INTENSITY = 0.12f;
    constexpr float TWO_PI = 6.2831853f;

    inline u32 sunTurn(const float t)
    {
        return std::min(static_cast<u32>(t * static_cast<float>(SUN_TURNS)), SUN_TURNS - 1);
    }

     
    inline Vec3 towardSun(const u32 turn)
    {
        const float angle = TWO_PI * ((static_cast<float>(turn) + 0.5f) / static_cast<float>(SUN_TURNS) - 0.25f);
        return {std::cos(angle), std::sin(angle) * std::cos(SUN_TILT), -std::sin(angle) * std::sin(SUN_TILT)};
    }

     
    inline Quat lightRotation(const Vec3& travel)
    {
        const float yaw = std::atan2(-travel.x, -travel.z);
        const float pitch = std::asin(std::clamp(travel.y, -1.0f, 1.0f));
        return multiply(fromAxisAngle({0.0f, 1.0f, 0.0f}, yaw), fromAxisAngle({1.0f, 0.0f, 0.0f}, pitch));
    }

    inline float smoothStep(const float from, const float to, const float x)
    {
        const float u = std::clamp((x - from) / (to - from), 0.0f, 1.0f);
        return u * u * (3.0f - 2.0f * u);
    }

     
     
     
    inline float sunIntensityAt(const float height) { return SUN_NOON_INTENSITY * smoothStep(-0.03f, -0.01f, height); }

     
    inline float moonIntensityAt(const float sunHeight)
    {
        return MOON_INTENSITY * smoothStep(0.02f, -0.12f, sunHeight);
    }
    inline const Vec3 MOON_COLOR{0.55f, 0.65f, 1.0f};
    inline Vec3 moonTravel() { return normalize(Vec3(0.45f, -0.75f, 0.5f)); }

    constexpr float NIGHT_EV = -3.0f;  

     
    inline float cameraEv(const float t)
    {
        return NIGHT_EV * (1.0f - smoothStep(-0.12f, 0.1f, towardSun(sunTurn(t)).y));
    }

     
    inline bool turnSun(CitySunComponent& sun, DirectionalLightComponent& light, TransformComponent& transform,
                        const float t)
    {
        const u32 turn = sunTurn(t);
        if (turn == sun.turn)
        {
            return false;
        }
        sun.turn = turn;
        sun.level = sunLevel(t);
        const Vec3 toward = towardSun(turn);
        if (sun.moon != 0)
        {
            light.intensity = moonIntensityAt(toward.y);
            light.color = MOON_COLOR;
            transform.rotation = lightRotation(moonTravel());
        }
        else
        {
            light.intensity = sunIntensityAt(toward.y);
            light.color = Vec3(1.0f);
            transform.rotation = lightRotation(Vec3(-toward.x, -toward.y, -toward.z));
        }
        return true;
    }

     
     

    inline u32 laneCount(const u32 size) { return 4 * (size + 1); }

    inline u32 laneOf(const u32 size, const u32 axis, const u32 line, const float dir)
    {
        return (axis * (size + 1) + line) * 2 + (dir < 0.0f ? 1u : 0u);
    }

     
    inline float lateralOf(const u32 axis, const u32 line, const float dir, const float offset)
    {
        return static_cast<float>(line) * CELL + (axis == 0 ? 1.0f : -1.0f) * dir * offset;
    }

     
    inline float edgeCapacity(const GraphEdge& edge)
    {
        return edge.length() / (CAR_LENGTH + MIN_GAP) * static_cast<float>(edge.lanes);
    }

    inline u32 congestionLevel(const u32 cars, const float capacity)
    {
        const float share = static_cast<float>(cars) / std::max(capacity, 1.0f);
        return share < 0.2f ? 0u : share < 0.45f ? 1u : share < 0.7f ? 2u : 3u;
    }

    inline Vec4 congestionTint(const u32 level)
    {
        constexpr Vec4 TINTS[CONGESTION_LEVELS] = {{0.16f, 0.17f, 0.19f, 1.0f},
                                                   {0.15f, 0.55f, 0.22f, 1.0f},
                                                   {0.9f, 0.6f, 0.1f, 1.0f},
                                                   {0.85f, 0.1f, 0.08f, 1.0f}};
        return TINTS[std::min(level, CONGESTION_LEVELS - 1)];
    }

     
     
     
    struct RoadLoad
    {
        u32 cars = 0;  
        double capacity = 0.0;
        u32 levels[CONGESTION_LEVELS] = {};
        u32 busiest = 0;  

         
        double occupancy() const { return capacity > 0.0 ? 100.0 * cars / capacity : 0.0; }
        u32 directions() const { return levels[0] + levels[1] + levels[2] + levels[3]; }
    };

    class RoadLoadCounter
    {
    public:
        explicit RoadLoadCounter(const RoadGraph& graph) : m_graph(graph), m_edgeCars(graph.edges.size() * 2, 0) {}

        void add(const CityCarComponent& car)
        {
            if (car.state != CAR_DRIVING || car.axis > 1)
            {
                return;
            }
            const u32 edge = m_graph.edgeUnder(car.axis, car.line, car.pos, car.dir);
            if (edge != NO_ID)
            {
                ++m_edgeCars[static_cast<sizet>(edge) * 2 + (car.dir < 0.0f ? 1 : 0)];
            }
        }

         
        u32 carsOn(const u32 edge, const u32 reverse) const
        {
            return m_edgeCars[static_cast<sizet>(edge) * 2 + reverse];
        }

        RoadLoad load() const
        {
            RoadLoad load;
            for (sizet i = 0; i < m_edgeCars.size(); ++i)
            {
                const float capacity = edgeCapacity(m_graph.edges[i / 2]);
                load.cars += m_edgeCars[i];
                load.capacity += capacity;
                load.busiest = std::max(load.busiest, m_edgeCars[i]);
                ++load.levels[congestionLevel(m_edgeCars[i], capacity)];
            }
            return load;
        }

    private:
        const RoadGraph& m_graph;
        std::vector<u32> m_edgeCars;
    };

     
    struct Footprint
    {
        float x = 0.0f;
        float z = 0.0f;
        float halfX = 0.0f;
        float halfZ = 0.0f;
    };

    inline Footprint footprintOf(const u32 axis, const float lateral, const float pos)
    {
        return axis == 0 ? Footprint{pos, lateral, CAR_HALF_LENGTH, CAR_HALF_WIDTH}
                         : Footprint{lateral, pos, CAR_HALF_WIDTH, CAR_HALF_LENGTH};
    }

    inline bool overlap(const Footprint& a, const Footprint& b)
    {
        return std::fabs(a.x - b.x) < a.halfX + b.halfX && std::fabs(a.z - b.z) < a.halfZ + b.halfZ;
    }

     
    inline float offsetAt(const RoadGraph& graph, const u32 axis, const u32 line, const float pos, const float dir,
                          const u32 sub)
    {
        const u32 edge = graph.edgeUnder(axis, line, pos, dir);
        return edge == NO_ID ? laneOffset(1, 0.0f, sub) : graph.edges[edge].offset(sub);
    }

     
     
     
    inline float parkedOut(const GraphEdge& edge) { return 0.5f * ROAD_CLASS_SPECS[edge.roadClass].parking; }

     
     
     
    constexpr float LANE_CHANGE_SECONDS = 3.0f;
     
    constexpr float SLIDE_TILT_SPEED = 8.0f;

     
     
    inline float slideOffset(const GraphEdge& on, const CityCarComponent& car, float* across = nullptr)
    {
        const float r = std::fabs(car.slide);
        if (r <= 0.0f || car.state != CAR_DRIVING)
        {
            if (across)
            {
                *across = 0.0f;
            }
            return 0.0f;
        }
        const u32 from = car.slide > 0.0f ? car.sublane + 1 : (car.sublane > 0 ? car.sublane - 1 : 0);
        const float width = on.offset(from) - on.offset(car.sublane);
        if (across)
        {
            *across = width * 6.0f * r * (1.0f - r) / LANE_CHANGE_SECONDS;
        }
        return width * r * r * (3.0f - 2.0f * r);
    }

     
     
    inline float carOffset(const CityCarComponent& car, const RoadGraph& graph, float* across = nullptr)
    {
        const u32 edge = graph.edgeUnder(car.axis, car.line, car.pos, car.dir);
        if (across)
        {
            *across = 0.0f;
        }
        if (edge == NO_ID)
        {
            return laneOffset(1, 0.0f, car.sublane);
        }
        const GraphEdge& on = graph.edges[edge];
        return car.state != CAR_DRIVING ? on.half() + parkedOut(on)
                                        : on.offset(car.sublane) + slideOffset(on, car, across);
    }

     
    inline Quat yawRotation(const float yaw) { return Quat(0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)); }

     
    inline u32 carModel(const u32 id)
    {
        const u32 roll = static_cast<u32>(mix(id ^ 0xCA2ull) % 20);
        return roll < 8 ? 0u : roll < 13 ? 1u : roll < 17 ? 2u : 3u;
    }

     
    constexpr float CAR_MODEL_LENGTH = CAR_LENGTH + 0.9f;
    inline float carModelScale(const u32 model)
    {
        return std::min(1.0f, CAR_MODEL_LENGTH / catalog::CARS[model].length);
    }

     
    constexpr float HIDDEN_CAR = 0.001f;
    inline bool garaged(const CityCarComponent& car) { return car.state != CAR_DRIVING && car.sublane == PARK_GARAGE; }

     
     
    inline bool carMovement(const CityCarComponent& car, const RoadGraph& graph, u32& node, MovementPath& path)
    {
        node = car.plan == PLAN_MOVING ? graph.nodeOfKey(car.node) : NO_ID;
        if (node == NO_ID ||
            movementOf(graph, node, moveHeadingIn(car.move), moveSubIn(car.move), moveHeadingOut(car.move),
                       moveSubOut(car.move)) != car.move)
        {
            return false;
        }
        path = movementPath(graph, node, car.move);
        return true;
    }

     
     
    inline bool carPose(const CityCarComponent& car, const RoadGraph& graph, BodyPose& pose)
    {
        if (car.axis > 1)
        {
            return false;
        }
        u32 node = NO_ID;
        MovementPath path;
        if (carMovement(car, graph, node, path))
        {
            pose = bodyOnPath(path, car.moveS);
            pose.x += static_cast<float>(graph.nodes[node].x) * CELL;
            pose.z += static_cast<float>(graph.nodes[node].z) * CELL;
            return true;
        }
        float across = 0.0f;
        const float lateral = lateralOf(car.axis, car.line, car.dir, carOffset(car, graph, &across));
        pose.x = car.axis == 0 ? car.pos : lateral;
        pose.z = car.axis == 0 ? lateral : car.pos;
         
        const u32 heading = headingOf(car.axis, car.dir);
        const float forward = SLIDE_TILT_SPEED;
        const float side = -across;  
        const float fx = headingX(heading) * forward + rightX(heading) * side;
        const float fz = headingZ(heading) * forward + rightZ(heading) * side;
        const float length = std::sqrt(fx * fx + fz * fz);
        pose.dirX = fx / length;
        pose.dirZ = fz / length;
        return true;
    }

     
     
    inline void placeCar(const CityCarComponent& car, const RoadGraph& graph, TransformComponent& transform,
                         const bool model = false)
    {
        BodyPose pose;
        if (!carPose(car, graph, pose))
        {
            return;
        }
         
        transform.rotation = yawRotation(std::atan2(pose.dirX, pose.dirZ));
        if (model)
        {
            transform.position = Vec3(pose.x, 0.0f, pose.z);
            transform.scale = Vec3(garaged(car) ? HIDDEN_CAR : carModelScale(carModel(car.id)));
            return;
        }
        transform.position = {pose.x, CAR_HALF_HEIGHT, pose.z};
        transform.scale = garaged(car) ? Vec3(HIDDEN_CAR) : Vec3(CAR_HALF_WIDTH, CAR_HALF_HEIGHT, CAR_HALF_LENGTH);
    }

     

    inline bool isTruck(const CityStateComponent& city, const u32 id)
    {
        return id < city.cars && id + city.trucks >= city.cars;
    }

    inline u32 truckIndex(const CityStateComponent& city, const u32 id) { return id + city.trucks - city.cars; }

     
     
    inline u32 truckModel(const u32 index)
    {
        const u32 roll = static_cast<u32>(mix(index ^ 0x7A0Cull) % 10);
        return roll < 4 ? 0u : 1u;
    }

     
    inline u32 truckCapacity(const u32 index)
    {
        constexpr u32 CAPACITY[] = {4, 6, 8};
        return CAPACITY[truckModel(index)];
    }

     
     
     
    constexpr float TRUCK_DRAW_LENGTH[] = {CAR_LENGTH + 0.9f, CAR_LENGTH + 1.4f, CAR_LENGTH + 1.4f};
    inline Vec3 truckScale(const u32 model)
    {
        const float along = std::min(1.0f, TRUCK_DRAW_LENGTH[model] / catalog::TRUCKS[model].length);
        return {along, along, along};
    }

     
    inline float drawnHalfLength(const CityStateComponent& city, const u32 id)
    {
        if (isTruck(city, id))
        {
            return 0.5f * TRUCK_DRAW_LENGTH[truckModel(truckIndex(city, id))];
        }
        return 0.5f * std::min(CAR_MODEL_LENGTH, catalog::CARS[carModel(id)].length);
    }

     
    inline void placeVehicle(const CityCarComponent& car, const RoadGraph& graph, TransformComponent& transform,
                             const CityStateComponent& city)
    {
        const bool truck = isTruck(city, car.id);
        const bool model = (city.models & (truck ? MODEL_TRUCKS : MODEL_CARS)) != 0;
        placeCar(car, graph, transform, model);
        if (model && truck && !garaged(car))
        {
            transform.scale = truckScale(truckModel(truckIndex(city, car.id)));
        }
    }

    inline void placeJunction(const RoadGraph& graph, const u32 node, TransformComponent& transform)
    {
        const GraphNode& n = graph.nodes[node];
        transform.position = {static_cast<float>(n.x) * CELL, 0.06f, static_cast<float>(n.z) * CELL};
        transform.scale = {junctionHalf(graph, node, 0), 0.1f, junctionHalf(graph, node, 1)};
    }

     
    inline void edgeSpan(const RoadGraph& graph, const GraphEdge& edge, float& low, float& high)
    {
        low = static_cast<float>(edge.lo) * CELL + junctionHalf(graph, edge.a, edge.axis);
        high = static_cast<float>(edge.hi) * CELL - junctionHalf(graph, edge.b, edge.axis);
        if (high < low)
        {
            low = high = (low + high) * 0.5f;
        }
    }

     
    inline void placeSlab(const RoadGraph& graph, const GraphEdge& edge, TransformComponent& transform)
    {
        float low = 0.0f;
        float high = 0.0f;
        edgeSpan(graph, edge, low, high);
        const float along = (low + high) * 0.5f;
        const float across = static_cast<float>(edge.line) * CELL;
        const float halfLength = std::max((high - low) * 0.5f, 0.05f);
        const float halfWidth = std::max(ROAD_CLASS_SPECS[edge.roadClass].corridor, edge.half() + 1.0f);
        transform.position = edge.axis == 0 ? Vec3(along, 0.05f, across) : Vec3(across, 0.05f, along);
        transform.scale = edge.axis == 0 ? Vec3(halfLength, 0.1f, halfWidth) : Vec3(halfWidth, 0.1f, halfLength);
    }

     
    inline void placeLane(const RoadGraph& graph, const GraphEdge& edge, const u32 reverse,
                          TransformComponent& transform)
    {
        float low = 0.0f;
        float high = 0.0f;
        edgeSpan(graph, edge, low, high);
        const float dir = reverse != 0 ? -1.0f : 1.0f;
        const float width = static_cast<float>(edge.lanes) * LANE_WIDTH;
        const float lateral = lateralOf(edge.axis, edge.line, dir, edge.median() + width * 0.5f);
        const float along = (low + high) * 0.5f;
        const float halfLength = std::max((high - low) * 0.5f, 0.05f);
        const float halfWidth = width * 0.5f - 0.05f;
        transform.position = edge.axis == 0 ? Vec3(along, 0.16f, lateral) : Vec3(lateral, 0.16f, along);
        transform.scale = edge.axis == 0 ? Vec3(halfLength, 0.02f, halfWidth) : Vec3(halfWidth, 0.02f, halfLength);
    }

     
     
     
     

    inline bool bodyInBox(const float pos, const float centre, const float half)
    {
        return std::fabs(pos - centre) < half + CAR_HALF_LENGTH;
    }

    inline bool committedToBox(const float dir, const float pos, const float centre, const float half, const float stop)
    {
        const float u = dir * (pos - centre);
        return u > -stop + 0.02f && u < half + CAR_HALF_LENGTH;
    }

     
     
     
     

    inline bool turnsRight(const u32 axis, const float dir, const float td) { return td == (axis == 0 ? dir : -dir); }

    inline u32 edgeSublane(const u32 lanes, const bool right) { return right ? 0u : lanes - 1; }

     
     
    inline float turnTrigger(const GraphEdge& target, const u32 axis, const u32 k, const float td, const bool right)
    {
        const float offset = target.offset(edgeSublane(target.lanes, right));
        return static_cast<float>(k) * CELL + (axis == 0 ? -1.0f : 1.0f) * td * offset;
    }

     
     
     
     
     

    constexpr float BAY_TILE = 6.0f;  
    constexpr u32 BAY_SKIP = 2;
    constexpr float BAY_JITTER = 0.5f;  
    constexpr u32 CURB_BAYS = 64;
    constexpr float CURB_FILL = 0.6f;  

    struct ParkSpot
    {
        u32 index = 0;  
        float jitter = BAY_JITTER;
    };

     
    inline bool hasParkingLane(const GraphEdge& edge)
    {
        return ROAD_CLASS_SPECS[edge.roadClass].parking > 0.0f && edge.lanes == ROAD_CLASS_SPECS[edge.roadClass].lanes;
    }

     
    struct BayRow
    {
        float low = 0.0f;
        float pitch = BAY_TILE;
        u32 count = 0;
    };

    inline BayRow bayRow(const RoadGraph& graph, const GraphEdge& edge)
    {
        BayRow row;
        if (!hasParkingLane(edge))
        {
            return row;
        }
        float low = 0.0f;
        float high = 0.0f;
        edgeSpan(graph, edge, low, high);
        const u32 tiles = std::max(static_cast<u32>(std::lround((high - low) / BAY_TILE)), 1u);
        row.pitch = (high - low) / static_cast<float>(tiles);
        row.count = tiles > 2 * BAY_SKIP ? tiles - 2 * BAY_SKIP : 0;
        row.low = low + static_cast<float>(BAY_SKIP) * row.pitch;
        return row;
    }

    inline float bayCentre(const BayRow& row, const u32 bay)
    {
        return row.low + (static_cast<float>(bay) + 0.5f) * row.pitch;
    }

     
    inline u32 curbBayCount(const BayRow& row, const bool fromLo)
    {
        return std::min(fromLo ? (row.count + 1) / 2 : row.count / 2, CURB_BAYS);
    }

     
    inline u32 curbBay(const BayRow& row, const bool fromLo, const u32 bit)
    {
        return fromLo ? bit : row.count - 1 - bit;
    }

    inline u32 curbKey(const u32 node, const u32 side, const float dir)
    {
        return (node * 4 + side) * 2 + (dir < 0.0f ? 1 : 0);
    }

     
     
    inline float garageSlot(const RoadGraph& graph, const u32 edge, const u32 node)
    {
        const GraphEdge& e = graph.edges[edge];
        return std::min(graph.nodes[node].stop[1 - e.axis] + CAR_HALF_LENGTH + 0.2f, e.length() * 0.5f);
    }

     
     
     
    struct ParkPlace
    {
        u32 edge = NO_ID;
        float dir = 1.0f;
        float pos = 0.0f;
        bool fits = true;
        bool free = true;
    };

    inline ParkPlace curbSlotAt(const RoadGraph& graph, const u32 edge, const u32 node, const bool away,
                                const ParkSpot& spot, const u64* park)
    {
        const GraphEdge& e = graph.edges[edge];
        const float toward = node == e.b ? 1.0f : -1.0f;
        ParkPlace place{.edge = edge, .dir = away ? -toward : toward};
        const BayRow row = bayRow(graph, e);
        const bool fromLo = node == e.a;
        const u32 bays = curbBayCount(row, fromLo);
        place.fits = bays > 0;
        const float nodeAlong = static_cast<float>(graph.along(node, e.axis)) * CELL;
        if (!place.fits)
        {
            place.free = false;
            place.pos = nodeAlong - toward * garageSlot(graph, edge, node);
            return place;
        }
        const u64 taken = park ? park[curbKey(node, graph.headingAlong(edge, node), place.dir)] : 0;
        u32 pick = spot.index % bays;
        place.free = false;
        const u64 mask = bays >= 64 ? ~0ull : (1ull << bays) - 1;
        const u32 limit = static_cast<u32>(static_cast<float>(bays) * CURB_FILL + 0.4f);
        for (u32 i = 0; i < bays && static_cast<u32>(std::popcount(taken & mask)) < limit; ++i)
        {
            const u32 bit = (spot.index + i) % bays;
            if ((taken & (1ull << bit)) == 0)
            {
                pick = bit;
                place.free = true;
                break;
            }
        }
        const float jitter = spot.jitter * (static_cast<float>(mix(spot.index ^ 0x717ull) % 2001) / 1000.0f - 1.0f);
        place.pos = bayCentre(row, curbBay(row, fromLo, pick)) + jitter;
        return place;
    }

    constexpr u32 PARK_RINGS = 5;  

     
     
     
    inline ParkPlace parkPlace(const RoadGraph& graph, const u32 node, const u32 edge, const bool away,
                               const ParkSpot& spot, const u64* park)
    {
        const ParkPlace preferred = curbSlotAt(graph, edge, node, away, spot, park);
        if ((preferred.free && preferred.fits) || park == nullptr)
        {
            return preferred;
        }
        constexpr u32 MAX_NODES = 1 + 4 + 12 + 28 + 60;
        u32 nodes[MAX_NODES] = {node};
        u32 count = 1;
        u32 ringStart = 0;
        for (u32 ring = 0; ring < PARK_RINGS; ++ring)
        {
            const u32 ringEnd = count;
            for (u32 i = ringStart; i < ringEnd; ++i)
            {
                const u32 at = nodes[i];
                for (u32 heading = 0; heading < 4; ++heading)
                {
                    const u32 other = graph.nodes[at].edges[heading];
                    if (other == NO_ID || graph.edges[other].roadClass == ROAD_HIGHWAY)
                    {
                        continue;
                    }
                    for (const bool beyond : {false, true})
                    {
                        if (at == node && other == edge && beyond == away)
                        {
                            continue;
                        }
                        const ParkPlace place = curbSlotAt(graph, other, at, beyond, spot, park);
                        if (place.fits && place.free)
                        {
                            return place;
                        }
                    }
                    const u32 next = graph.other(other, at);
                    if (ring + 1 < PARK_RINGS && count < MAX_NODES &&
                        std::find(nodes, nodes + count, next) == nodes + count)
                    {
                        nodes[count++] = next;
                    }
                }
            }
            ringStart = ringEnd;
        }
        return preferred;
    }

     
    inline bool curbBits(const RoadGraph& graph, const u32 axis, const u32 line, const float pos, const float dir,
                         u32& key, u64& bits)
    {
        const u32 edge = graph.edgeUnder(axis, line, pos, dir);
        if (edge == NO_ID)
        {
            return false;
        }
        const GraphEdge& e = graph.edges[edge];
        const BayRow row = bayRow(graph, e);
        if (row.count == 0)
        {
            return false;
        }
        const float at = (pos - row.low) / row.pitch;
        if (at < -0.5f || at >= static_cast<float>(row.count) + 0.5f)
        {
            return false;
        }
        const u32 bay = std::min(static_cast<u32>(std::max(at, 0.0f)), row.count - 1);
        const bool fromLo = bay < curbBayCount(row, true);
        const u32 bit = fromLo ? bay : row.count - 1 - bay;
        if (bit >= CURB_BAYS)
        {
            return false;
        }
        const u32 end = fromLo ? e.a : e.b;
        key = curbKey(end, graph.headingAlong(edge, end), dir);
        bits = 1ull << bit;
        return true;
    }

     
     
    inline bool standsInBay(const RoadGraph& graph, const u32 axis, const u32 line, const float pos, const float dir)
    {
        const u32 edge = graph.edgeUnder(axis, line, pos, dir);
        if (edge == NO_ID)
        {
            return false;
        }
        const BayRow row = bayRow(graph, graph.edges[edge]);
        const float at = (pos - row.low) / row.pitch;
        return row.count > 0 && at >= 0.0f && at < static_cast<float>(row.count) &&
            std::fabs(pos - bayCentre(row, static_cast<u32>(at))) <= BAY_JITTER + 0.05f;
    }

     
     
     
     
    struct ParkChoice
    {
        u32 edge = NO_ID;
        float dir = 1.0f;
        float pos = 0.0f;
        bool garage = false;
    };

    inline ParkChoice chooseParking(const RoadGraph& graph, const u32 node, u32 edge, bool away, const u32 lotEdge,
                                    const ParkSpot& spot, u64* park, const bool street = true)
    {
        const bool lotHere = lotEdge != NO_ID && (graph.edges[lotEdge].a == node || graph.edges[lotEdge].b == node);
        if (edge == NO_ID || (graph.edges[edge].roadClass == ROAD_HIGHWAY && lotHere))
        {
            edge = lotEdge;
            away = false;
        }
        ParkChoice choice;
        if (edge == NO_ID)
        {
            choice.garage = true;
            return choice;
        }
        const ParkPlace place = parkPlace(graph, node, edge, away, spot, street ? park : nullptr);
        const GraphEdge& on = graph.edges[place.edge];
        choice.edge = place.edge;
        choice.dir = place.dir;
        choice.pos = place.pos;
        choice.garage = !street || !(place.free && place.fits) || on.roadClass == ROAD_HIGHWAY;
        if (!choice.garage)
        {
            u32 key = 0;
            u64 bits = 0;
            if (curbBits(graph, on.axis, on.line, place.pos, place.dir, key, bits))
            {
                park[key] |= bits;
            }
        }
        return choice;
    }

     
     
    constexpr float STREET_SHARE[ZONE_COUNT] = {0.06f, 0.15f, 0.25f, 0.2f, 0.12f, 0.12f};
    constexpr float TRUCK_STREET_SHARE = 0.3f;

    inline bool parksOnStreet(const u64 seed, const u32 id, const u32 lotId, const u32 zone, const bool truck)
    {
        const float share = truck ? TRUCK_STREET_SHARE : STREET_SHARE[std::min(zone, ZONE_COUNT - 1)];
        return static_cast<float>(mix(seed ^ 0x57EE7ull ^ (static_cast<u64>(id) << 32 | lotId)) >> 40) <
            share * static_cast<float>(1 << 24);
    }

     
     
     

    constexpr u32 WALK_ALONG = 1;
    constexpr u32 WALK_ROUND = 2;
    constexpr u32 WALK_CROSS = 3;

    inline Vec3 cornerPoint(const RoadGraph& graph, const u32 node, const u32 corner)
    {
        const GraphNode& n = graph.nodes[node];
        const float sx = corner == 0 || corner == 3 ? 1.0f : -1.0f;
        const float sz = corner < 2 ? 1.0f : -1.0f;
        return {static_cast<float>(n.x) * CELL + sx * n.walk[1], PED_HALF_HEIGHT,
                static_cast<float>(n.z) * CELL + sz * n.walk[0]};
    }

    inline float walkLength(const Vec3& a, const Vec3& b) { return std::hypot(b.x - a.x, b.z - a.z); }

     
     
    template <typename Fn>
    void forEachWalk(const RoadGraph& graph, const u32 node, const u32 corner, Fn&& fn)
    {
        const GraphNode& n = graph.nodes[node];
        for (const u32 heading : {corner, (corner + 1) & 3})
        {
            const u32 other = heading == corner ? (corner + 1) & 3 : corner;
            const u32 across = (2 * heading + 4 - other) & 3;  
            const u32 edge = n.edges[heading];
            if (edge == NO_ID)
            {
                fn(node, cornerBetween(heading, across), WALK_ROUND, NO_ID);
                continue;
            }
            if (!walkable(graph.edges[edge].roadClass))
            {
                continue;
            }
            fn(graph.other(edge, node), cornerBetween((heading + 2) & 3, other), WALK_ALONG, edge);
            fn(node, cornerBetween(heading, across), WALK_CROSS, edge);
        }
    }

     
    inline u32 walkKind(const RoadGraph& graph, const u32 node, const u32 corner, const u32 toNode, const u32 toCorner,
                        u32* edge = nullptr)
    {
        u32 kind = 0;
        if (node == NO_ID || toNode == NO_ID)
        {
            return 0;
        }
        forEachWalk(graph, node, corner,
                    [&](const u32 n, const u32 c, const u32 k, const u32 e)
                    {
                        if (kind == 0 && n == toNode && c == toCorner)
                        {
                            kind = k;
                            if (edge)
                            {
                                *edge = e;
                            }
                        }
                    });
        return kind;
    }

    inline u32 cornerKey(const RoadGraph& graph, const u32 node, const u32 corner)
    {
        return graph.keyOf(node) * 4 + corner;
    }

    constexpr float SIDEWALK_HEIGHT = 0.15f;  
     
     
    constexpr u32 PERSON_LANES = 2;
    constexpr float PERSON_LANE_FIRST = 0.28f;  
    constexpr float PERSON_LANE_WIDTH = 0.46f;

     
    inline u32 personLane(const u32 id) { return static_cast<u32>(mix(id ^ 0x9E0Bull) % PERSON_LANES); }
    constexpr u32 PERSON_SPOTS = 48;  
    constexpr float PERSON_SPOT_SPACING = 0.5f;  

     
    inline bool pedestrianIndoors(const CityPedestrianComponent& ped)
    {
        return ped.mode == PED_DWELLING || ped.mode == PED_AWAITING_ROUTE;
    }

    constexpr float HIDDEN_PEDESTRIAN = 0.001f;  

    inline Vec3 pedestrianBoxScale(const CityPedestrianComponent& ped)
    {
        return pedestrianIndoors(ped) ? Vec3(HIDDEN_PEDESTRIAN) : Vec3(PED_HALF, PED_HALF_HEIGHT, PED_HALF);
    }

     
     
     
    inline void placePedestrian(const CityPedestrianComponent& ped, const RoadGraph& graph,
                                TransformComponent& transform, const bool model = false)
    {
        const u32 node = graph.nodeAt(ped.x, ped.z);
        if (node == NO_ID)
        {
            return;
        }
        const Vec3 from = cornerPoint(graph, node, ped.corner & 3);
        Vec3 at = from;
        Vec3 to = from;
        const u32 toNode = graph.nodeAt(ped.toX, ped.toZ);
        const bool moving = ped.mode == PED_WALKING || ped.mode == PED_CROSSING || ped.mode == PED_MEDIAN;
        if (moving && toNode != NO_ID)
        {
            to = cornerPoint(graph, toNode, ped.toCorner & 3);
            const float t = std::clamp(ped.progress / std::max(walkLength(from, to), 1.0e-3f), 0.0f, 1.0f);
            at = {from.x + (to.x - from.x) * t, PED_HALF_HEIGHT, from.z + (to.z - from.z) * t};
        }
        if (!model)
        {
            transform.position = at;
            transform.scale = pedestrianBoxScale(ped);
            return;
        }
        const u64 hash = mix(ped.id ^ 0x9E0Bull);
        const float along = std::hypot(to.x - from.x, to.z - from.z);
        float dx = 0.0f;
        float dz = 0.0f;
        if (moving && ped.mode != PED_MEDIAN && along > 1.0e-3f)
        {
            const float offset = PERSON_LANE_FIRST + static_cast<float>(ped.lane % PERSON_LANES) * PERSON_LANE_WIDTH;
            dx = -(to.z - from.z) / along * offset;
            dz = (to.x - from.x) / along * offset;
        }
        else
        {
            const u64 spot = ped.crowdSpot == ~0u ? hash % PERSON_SPOTS : ped.crowdSpot;
            const float angle = static_cast<float>(spot) * 2.3999632f;
            const float radius = PERSON_SPOT_SPACING * std::sqrt(static_cast<float>(spot + 1));
            dx = std::cos(angle) * radius;
            dz = std::sin(angle) * radius;
        }
        const bool onRoad = ped.mode == PED_CROSSING || ped.mode == PED_MEDIAN;
        const float distanceToCorner = std::min(ped.progress, std::max(0.0f, along - ped.progress));
        if (moving && ped.mode != PED_MEDIAN && along > 1.0e-3f)
        {
            const float t = std::clamp(distanceToCorner / 1.0f, 0.0f, 1.0f);
            const float laneShare = t * t * (3.0f - 2.0f * t);
            const u64 spot = ped.crowdSpot == ~0u ? hash % PERSON_SPOTS : ped.crowdSpot;
            const float angle = static_cast<float>(spot) * 2.3999632f;
            const float radius = PERSON_SPOT_SPACING * std::sqrt(static_cast<float>(spot + 1));
            dx = std::cos(angle) * radius * (1.0f - laneShare) + dx * laneShare;
            dz = std::sin(angle) * radius * (1.0f - laneShare) + dz * laneShare;
        }
        const float ramp = std::clamp(distanceToCorner / 0.6f, 0.0f, 1.0f);
        const float height = onRoad ? SIDEWALK_HEIGHT - (SIDEWALK_HEIGHT - 0.01f) * ramp : SIDEWALK_HEIGHT;
        transform.position = {at.x + dx, height, at.z + dz};
        transform.scale = Vec3(1.0f);
        if (moving && (std::fabs(to.x - from.x) > 1.0e-3f || std::fabs(to.z - from.z) > 1.0e-3f))
        {
            transform.rotation = yawRotation(std::atan2(to.x - from.x, to.z - from.z));
        }
    }

     
     

    inline constexpr const char* PERSON_CLIPS[] = {"walk",  "walk2",      "walkfast", "walkslow", "stroll", "idle",
                                                   "idle2", "lookaround", "wait",     "phone",    "talk"};
    constexpr u32 PERSON_CLIP_COUNT = static_cast<u32>(std::size(PERSON_CLIPS));
    constexpr u32 PEOPLE_COUNT = static_cast<u32>(std::size(catalog::PEOPLE));

     
     
    inline InternedString personClip(const CityPedestrianComponent& ped, const InternedString skeleton)
    {
        static const std::array<InternedString, PEOPLE_COUNT> avatars = []
        {
            std::array<InternedString, PEOPLE_COUNT> names;
            for (u32 i = 0; i < PEOPLE_COUNT; ++i)
            {
                names[i] = InternedString(catalog::PEOPLE[i]);
            }
            return names;
        }();
        static const std::array<std::array<InternedString, PERSON_CLIP_COUNT>, PEOPLE_COUNT> clips = []
        {
            std::array<std::array<InternedString, PERSON_CLIP_COUNT>, PEOPLE_COUNT> names;
            for (u32 i = 0; i < PEOPLE_COUNT; ++i)
            {
                for (u32 c = 0; c < PERSON_CLIP_COUNT; ++c)
                {
                    names[i][c] = InternedString(std::format("{}.{}", catalog::PEOPLE[i], PERSON_CLIPS[c]));
                }
            }
            return names;
        }();
        const auto avatar = std::ranges::find(avatars, skeleton);
        if (avatar == avatars.end())
        {
            return {};
        }
        u32 clip = 0;
        switch (ped.mode)
        {
        case PED_WALKING:
        case PED_CROSSING:
            clip = ped.speed < 1.25f ? 4u : ped.speed < 1.5f ? ((ped.id & 1) != 0 ? 0u : 1u) : 2u;
            break;
        case PED_WAITING:
        case PED_MEDIAN:
            clip = ped.id % 3 == 0 ? 7u : 8u;
            break;
        default:
            {
                constexpr u32 STANDING[] = {5, 6, 9, 10, 8};
                clip = STANDING[mix(ped.id ^ 0x1D1Eull) % std::size(STANDING)];
                break;
            }
        }
        return clips[static_cast<sizet>(avatar - avatars.begin())][clip];
    }

     
     

    constexpr float LAMP_SPACING = 40.0f;
    constexpr float LAMP_CURB = 0.4f;  

    struct LampPlace
    {
        float x = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;  
    };

    inline std::vector<LampPlace> lampPlaces(const RoadGraph& graph)
    {
        std::vector<LampPlace> places;
        for (const GraphEdge& edge : graph.edges)
        {
            if (!walkable(edge.roadClass))
            {
                continue;
            }
            float low = 0.0f;
            float high = 0.0f;
            edgeSpan(graph, edge, low, high);
            const float length = high - low;
            const u32 count = length < 12.0f ? 0u : std::max(static_cast<u32>(length / LAMP_SPACING), 1u);
            const float across =
                edge.half() + (hasParkingLane(edge) ? ROAD_CLASS_SPECS[edge.roadClass].parking : 0.0f) + LAMP_CURB;
            for (u32 i = 0; i < count; ++i)
            {
                const float along = low + (static_cast<float>(i) + 0.5f) * length / static_cast<float>(count);
                const float side = (i + edge.line) % 2 == 0 ? 1.0f : -1.0f;
                const float lateral = static_cast<float>(edge.line) * CELL + side * across;
                 
                places.push_back(edge.axis == 0 ? LampPlace{along, lateral, side * 1.5707963f}
                                                : LampPlace{lateral, along, side > 0.0f ? 3.1415927f : 0.0f});
            }
        }
        return places;
    }

     
    inline Vec3 lampHead(const CityLampComponent& lamp)
    {
        if (lamp.kind != NIGHT_STREET)
            return {lamp.x, lamp.height, lamp.z};
        return {lamp.x + std::cos(lamp.yaw) * catalog::LAMP_ARM, catalog::LAMP_HEIGHT,
                lamp.z - std::sin(lamp.yaw) * catalog::LAMP_ARM};
    }

    inline SpotLightComponent nightLightStyle(const u32 kind, const u32 key)
    {
        SpotLightComponent light;
        light.color = {1.0f, 0.8f, 0.55f};
        light.intensity = 60.0f;
        light.radius = 30.0f;
        light.innerConeAngle = toRadians(35.0f);
        light.outerConeAngle = toRadians(62.0f);
        switch (kind)
        {
        case NIGHT_ENTRANCE:
            light.color = {1.0f, 0.64f, 0.32f};
            light.intensity = 18.0f;
            light.radius = 11.0f;
            light.innerConeAngle = toRadians(24.0f);
            light.outerConeAngle = toRadians(55.0f);
            break;
        case NIGHT_SHOP:
            light.color = (key & 1) ? Vec3(0.62f, 0.84f, 1.0f) : Vec3(1.0f, 0.86f, 0.65f);
            light.intensity = 38.0f;
            light.radius = 18.0f;
            light.innerConeAngle = toRadians(35.0f);
            light.outerConeAngle = toRadians(68.0f);
            break;
        case NIGHT_YARD:
            light.color = {0.66f, 0.82f, 1.0f};
            light.intensity = 95.0f;
            light.radius = 28.0f;
            break;
        case NIGHT_FACADE:
            light.color = (key & 1) ? Vec3(1.0f, 0.76f, 0.42f) : Vec3(0.72f, 0.86f, 1.0f);
            light.intensity = 55.0f;
            light.radius = 24.0f;
            light.innerConeAngle = toRadians(12.0f);
            light.outerConeAngle = toRadians(24.0f);
            break;
        }
        light.intensity *= 0.85f + static_cast<float>((key >> 8) & 255) * (0.3f / 255.0f);
        return light;
    }

    inline Vec3 nightLightDirection(const u32 kind, const float yaw)
    {
        const float outward = kind == NIGHT_FACADE ? -0.22f
            : kind == NIGHT_SHOP                   ? 0.55f
            : kind == NIGHT_ENTRANCE               ? 0.35f
                                                   : 0.0f;
        const float vertical = std::sqrt(1.0f - outward * outward) * (kind == NIGHT_FACADE ? 1.0f : -1.0f);
        return {std::cos(yaw) * outward, vertical, -std::sin(yaw) * outward};
    }

    inline std::vector<CityLampComponent> buildingNightLights(const DesignBuilding& building)
    {
        const u64 hash = mix(building.lot ^ 0x119A7ull);
        std::vector<CityLampComponent> sources;
        if (building.zone == ZONE_RESIDENTIAL_LOW && hash % 3 != 0)
            return sources;
        const float fx[] = {1.0f, 0.0f, -1.0f, 0.0f};
        const float fz[] = {0.0f, 1.0f, 0.0f, -1.0f};
        const u32 facing = building.rotation & 3;
        const float front = std::fabs(fx[facing]) * building.halfX + std::fabs(fz[facing]) * building.halfZ;
        const float jitter = static_cast<float>((hash >> 16) & 255) * (0.02f / 255.0f);
        const auto add = [&](const u32 kind, const float side)
        {
            const float offset = front + (kind == NIGHT_FACADE ? 0.8f : 0.12f);
            sources.push_back(CityLampComponent{
                .x = building.x + fx[facing] * offset - fz[facing] * side,
                .z = building.z + fz[facing] * offset + fx[facing] * side,
                .yaw = -static_cast<float>(facing) * 1.5707963f,
                .onAt = (kind == NIGHT_SHOP ? 0.72f : 0.75f) + jitter,
                .offAt = (kind == NIGHT_SHOP ? 0.05f : 0.24f) + jitter,
                .kind = kind,
                .height = kind == NIGHT_FACADE
                    ? 0.8f
                    : std::max(0.8f, std::min(kind == NIGHT_YARD ? 6.0f : 3.0f, building.height - 0.3f))});
        };
        const bool shop = building.zone == ZONE_COMMERCIAL_LOW || building.zone == ZONE_COMMERCIAL_HIGH;
        add(building.zone == ZONE_INDUSTRIAL ? NIGHT_YARD : shop ? NIGHT_SHOP : NIGHT_ENTRANCE, 0.0f);
        if (building.height >= 18.0f && building.zone != ZONE_INDUSTRIAL && hash % 2 == 0)
        {
            const float half = std::fabs(fz[facing]) * building.halfX + std::fabs(fx[facing]) * building.halfZ;
            add(NIGHT_FACADE, -half * 0.65f);
            add(NIGHT_FACADE, half * 0.65f);
        }
        return sources;
    }
}  

namespace spite
{
    class SystemContext;
}

namespace spite::city
{
     
    void moveCity(SystemContext& context);
}  
