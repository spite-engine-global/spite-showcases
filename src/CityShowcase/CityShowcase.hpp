#pragma once

#include "base/InternedString.hpp"
#include "base/MathTypes.hpp"
#include "base/Schema.hpp"
#include "ecs/EcsTypes.hpp"

#include <vector>

 
 
 
namespace spite::city
{
    inline constexpr const char* MODULE_NAME = "CityShowcase";
    inline constexpr const char* MOVE_SYSTEM_NAME = "CityMove";
     
    inline constexpr const char* AVATAR_SYSTEM_NAME = "CityAvatars";
     
    inline constexpr const char* FIT_SYSTEM_NAME = "CityFitBuildings";
     
     
     
    inline constexpr const char* CAMERA_SYSTEM_NAME = "CityCamera";
     
     
    inline constexpr const char* TOOLS_SYSTEM_NAME = "CityTools";
     
     
    inline constexpr const char* HUD_SYSTEM_NAME = "CityHud";
     
     
    inline constexpr const char* SPEED_COMMAND = "citySpeed";
     
     
    inline constexpr const char* CAMERA_QUERY = "cityCamera";
     
     
    inline constexpr const char* CAMERA_COMMAND = "cityCameraSet";
     
     
    inline constexpr const char* TOOL_COMMAND = "cityTool";
     
     
    inline constexpr const char* SELECT_COMMAND = "citySelect";
     
     
    inline constexpr const char* INSPECT_QUERY = "cityInspect";
    inline constexpr u32 MAX_SPEED_STEPS = 8;  
     
     
    inline constexpr const char* LAMP_LIGHT_SYSTEM_NAME = "CityLampLights";
     
     
     
    inline constexpr const char* LAYOUT_COMMAND = "cityLayout";
     
     
    inline constexpr const char* GENERATE_COMMAND = "cityGenerate";
     
     
    inline constexpr const char* BUILD_ROAD_COMMAND = "cityBuildRoad";
     
    inline constexpr const char* REMOVE_ROAD_COMMAND = "cityRemoveRoad";
     
    inline constexpr const char* ZONE_COMMAND = "cityZone";
     
    inline constexpr const char* BULLDOZE_COMMAND = "cityBulldoze";
     
    inline constexpr const char* SAVE_COMMAND = "citySave";
     
    inline constexpr const char* LOAD_COMMAND = "cityLoad";
     
    inline constexpr const char* APPLY_DESIGN_COMMAND = "cityApplyDesign";
     
     
    inline constexpr const char* ROADS_QUERY = "cityRoads";
     
    inline constexpr const char* LOTS_QUERY = "cityLots";
     
     
    inline constexpr const char* STATUS_QUERY = "cityStatus";
     
     
    inline constexpr const char* TRAFFIC_QUERY = "cityTraffic";
     
    inline constexpr const char* PEDESTRIANS_QUERY = "cityPedestrians";
     
    inline constexpr const char* DAY_QUERY = "cityDay";
     
    inline constexpr const char* ZONES_QUERY = "cityZones";
     
     
    inline constexpr const char* PATHS_QUERY = "cityPaths";
     
     
    inline constexpr const char* ROUTE_QUERY = "cityRoute";
     
     
    inline constexpr const char* PATH_WORKERS_COMMAND = "cityPathWorkers";
     
     
    inline constexpr const char* ROAD_LANES_COMMAND = "cityRoadLanes";
     
     
    inline constexpr const char* ORDERS_QUERY = "cityOrders";
     
     
    inline constexpr const char* PACKAGE_QUERY = "cityPackage";

    inline constexpr u32 MIN_SIZE = 32;  
    inline constexpr u32 MAX_SIZE = 4096;
    inline constexpr u32 MAX_CARS = 4000000;
    inline constexpr u32 MAX_PEDESTRIANS = 4000000;
    inline constexpr u32 MAX_DRONES = 500000;
    inline constexpr u32 MAX_LAMP_LIGHTS = 1024;  
     
    inline constexpr u32 MODEL_CARS = 1;
    inline constexpr u32 MODEL_PEOPLE = 2;
    inline constexpr u32 MODEL_DRONES = 4;
    inline constexpr u32 MODEL_TRUCKS = 8;
    inline constexpr u32 MODEL_PARCELS = 16;

     
    inline constexpr u32 CAR_DRIVING = 0;
    inline constexpr u32 CAR_PARKED = 1;
    inline constexpr u32 CAR_AWAITING_ROUTE = 2;
     
     
    inline constexpr u32 PARK_GARAGE = 9;
     
     
    inline constexpr u32 PLAN_NONE = 0;
    inline constexpr u32 PLAN_GRANTED = 1;
    inline constexpr u32 PLAN_MOVING = 2;
     
    inline constexpr u32 PED_WALKING = 0;
    inline constexpr u32 PED_WAITING = 1;
    inline constexpr u32 PED_CROSSING = 2;
    inline constexpr u32 PED_DWELLING = 3;
    inline constexpr u32 PED_AWAITING_ROUTE = 4;
     
    inline constexpr u32 PED_MEDIAN = 5;
     
     
    inline constexpr u32 ROUTE_NONE = 0;
    inline constexpr u32 ROUTE_PENDING = 1;
    inline constexpr u32 ROUTE_READY = 2;
    inline constexpr u32 ROUTE_FAILED = 3;
    inline constexpr u32 ROUTE_REROUTING = 4;
     
     
    inline constexpr u32 ZONE_RESIDENTIAL_LOW = 0;
    inline constexpr u32 ZONE_RESIDENTIAL_HIGH = 1;
    inline constexpr u32 ZONE_COMMERCIAL_LOW = 2;
    inline constexpr u32 ZONE_COMMERCIAL_HIGH = 3;
    inline constexpr u32 ZONE_INDUSTRIAL = 4;
    inline constexpr u32 ZONE_SPECIAL = 5;
    inline constexpr u32 ZONE_COUNT = 6;
    inline constexpr const char* ZONE_NAMES[ZONE_COUNT] = {"residentialLow", "residentialHigh", "commercialLow",
                                                           "commercialHigh", "industrial",      "special"};
     
    inline constexpr u32 SPECIAL_NONE = 0;
    inline constexpr u32 SPECIAL_DRONE_HUB = 1;
    inline constexpr u32 SPECIAL_TRUCK_DEPOT = 2;
    inline constexpr u32 SPECIAL_WAREHOUSE = 3;
    inline constexpr u32 SPECIAL_FACTORY = 4;
    inline constexpr u32 SPECIAL_KINDS = 5;
     
    inline constexpr const char* SPECIAL_ZONE_NAMES[SPECIAL_KINDS] = {"none", "droneHub", "truckDepot", "warehouse",
                                                                      "factory"};
     
    inline constexpr u32 TRIP_HOME = 0;
    inline constexpr u32 TRIP_WORK = 1;
    inline constexpr u32 TRIP_SHOP = 2;
    inline constexpr u32 TRIP_PURPOSES = 3;
     
    inline constexpr u32 TRIP_LUNCH = 6;
    inline constexpr u32 TRIP_STROLL = 7;
    inline constexpr u32 TRIP_BACK = 8;
    inline constexpr u32 TRIP_CAR = 9;
    inline constexpr u32 PED_PURPOSES = 10;
    inline constexpr const char* PED_PURPOSE_NAMES[PED_PURPOSES] = {
        "home", "work", "shop", "freight", "depot", "idle", "lunch", "stroll", "back to work", "to car"};
    inline constexpr u32 NO_LOT = 0xFFFFFFFFu;
     
     
    inline constexpr u32 TRIP_FREIGHT = 3;
    inline constexpr u32 TRIP_DEPOT = 4;
    inline constexpr u32 TRIP_IDLE = 5;
     
    inline constexpr u32 LIGHT_DAY = 0;
    inline constexpr u32 LIGHT_DARK = 1;
    inline constexpr u32 LIGHT_LIT = 2;
     
    inline constexpr u32 CONGESTION_LEVELS = 4;

     
     
     
     
     
     
     
     
     
     
     
    struct CityStateComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityState";
        u64 seed = 0;
        u32 size = 0;
        u32 cars = 0;
        u32 drones = 0;
        u32 pedestrians = 0;
        u32 people = 0;
        u32 lampLights = 0;
        u32 models = 0;
        u32 step = 0;
         
        u32 signalSteps = 900;
        u32 daySteps = 14400;  
        float dayStart = 0.0f;  
        u32 boxConflicts = 0;
        u32 boxHeld = 0;
        u32 laneChanges = 0;
        u32 designVersion = 0;
        u32 speed = 1;
        u32 trucks = 0;

        SPITE_SCHEMA(CityStateComponent, seed, size, cars, drones, pedestrians, people, lampLights, models, step,
                     signalSteps, daySteps, dayStart, boxConflicts, boxHeld, laneChanges, designVersion, speed, trucks)
    };

     
     
    struct CityMarkerComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityMarker";
        u32 slot = 0;

        SPITE_SCHEMA(CityMarkerComponent, slot)
    };

     
     
     
     
    struct CityBuildingComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityBuilding";
        u32 lot = 0;
        u32 zone = ZONE_RESIDENTIAL_LOW;
        InternedString model{};
        float x = 0.0f;
        float z = 0.0f;
        float halfX = 0.0f;
        float halfZ = 0.0f;
        u32 rotation = 0;
        float height = 0.0f;
        Vec4 dayColor{1.0f};
        float dusk = 0.75f;
        float dawn = 0.25f;
        u32 windows = 0;
        u32 light = LIGHT_DAY;

        SPITE_SCHEMA(CityBuildingComponent, lot, zone, model, x, z, halfX, halfZ, rotation, height, dayColor, dusk,
                     dawn, windows, light)
    };

     
     
     
    struct CityRoadComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityRoad";
        u32 x0 = 0;
        u32 z0 = 0;
        u32 x1 = 0;
        u32 z1 = 0;
        u32 roadClass = 0;
        u32 lanes = 1;

        SPITE_SCHEMA(CityRoadComponent, x0, z0, x1, z1, roadClass, lanes)
    };

     
    struct CityJunctionComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityJunction";
        u32 x = 0;
        u32 z = 0;

        SPITE_SCHEMA(CityJunctionComponent, x, z)
    };

     
     
    struct CityLotComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityLot";
        u32 id = 0;
        u32 x0 = 0;
        u32 z0 = 0;
        u32 x1 = 0;
        u32 z1 = 0;
        u32 zone = ZONE_RESIDENTIAL_LOW;
        u32 special = SPECIAL_NONE;
        u32 axis = 0;
        u32 line = 0;

        SPITE_SCHEMA(CityLotComponent, id, x0, z0, x1, z1, zone, special, axis, line)
    };

     
     
    struct CityZoneOverrideComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityZoneOverride";
        u32 x0 = 0;
        u32 z0 = 0;
        u32 x1 = 0;
        u32 z1 = 0;
        u32 zone = 0;
        u32 special = SPECIAL_NONE;  

        SPITE_SCHEMA(CityZoneOverrideComponent, x0, z0, x1, z1, zone, special)
    };

     
    struct CityGroundComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityGround";
        u32 size = 0;

        SPITE_SCHEMA(CityGroundComponent, size)
    };

     
     
     
    struct CityLaneComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityLane";
        u32 axis = 0;
        u32 line = 0;
        u32 from = 0;
        u32 reverse = 0;
        u32 cars = 0;
        u32 level = 0;

        SPITE_SCHEMA(CityLaneComponent, axis, line, from, reverse, cars, level)
    };

    inline constexpr u32 NIGHT_STREET = 0;
    inline constexpr u32 NIGHT_ENTRANCE = 1;
    inline constexpr u32 NIGHT_SHOP = 2;
    inline constexpr u32 NIGHT_YARD = 3;
    inline constexpr u32 NIGHT_FACADE = 4;

     
     
    struct CityLampComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityLamp";
        float x = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
        float onAt = 0.76f;
        float offAt = 0.24f;
        u32 on = 0;
        u32 glow = 0;  
        u32 kind = NIGHT_STREET;
        float height = 3.0f;

        SPITE_SCHEMA(CityLampComponent, x, z, yaw, onAt, offAt, on, glow, kind, height)
    };

     
    struct CityLampLightComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityLampLight";
        u32 lamp = 0xFFFFFFFFu;  
        u32 target = 0xFFFFFFFFu;  
        Vec3 targetAt{0.0f};  
        float level = 0.0f;  
        u32 kind = NIGHT_STREET;
        u32 targetKind = NIGHT_STREET;
        float targetYaw = 0.0f;

        SPITE_SCHEMA(CityLampLightComponent, lamp, target, targetAt, level, kind, targetKind, targetYaw)
    };

     
     
     
    struct CitySunComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CitySun";
        u32 level = 0;
        u32 turn = 0xFFFFFFFFu;
        u32 moon = 0;

        SPITE_SCHEMA(CitySunComponent, level, turn, moon)
    };
    inline constexpr u32 SUN_LEVELS = 16;
    inline constexpr u32 SUN_TURNS = 1440;  

     
     
    inline constexpr u32 PIECE_ROAD = 0;
    inline constexpr u32 PIECE_TRAFFIC_LIGHT = 1;
    inline constexpr u32 PIECE_TREE = 2;
    inline constexpr u32 SIGNAL_RED = 0;
    inline constexpr u32 SIGNAL_AMBER = 1;
    inline constexpr u32 SIGNAL_GREEN = 2;
    inline constexpr u32 SIGNAL_POST = 3;
    struct CitySignalComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CitySignal";
        u32 x = 0;
        u32 z = 0;
        u32 axis = 0;
        u32 color = SIGNAL_POST;
        u32 pedestrian = 0;
        u32 on = 0;
        SPITE_SCHEMA(CitySignalComponent, x, z, axis, color, pedestrian, on)
    };

    struct CityPieceComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityPiece";
        u32 kind = PIECE_ROAD;

        SPITE_SCHEMA(CityPieceComponent, kind)
    };

     
     
    struct CityCameraComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityCamera";
        Vec3 focus{0.0f};

        SPITE_SCHEMA(CityCameraComponent, focus)
    };

     
     
     
     
     
     
     
     
     
     
     
     
     
     
     
     
     
    struct CityCarComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityCar";
        u32 id = 0;
        u32 axis = 0;
        u32 line = 0;
        float dir = 1.0f;
        float pos = 0.0f;
        float speed = 0.0f;  
        float maxSpeed = 12.0f;
        u32 turns = 0;
        u32 state = CAR_DRIVING;
        u32 destX = 0;
        u32 destZ = 0;
        u32 trips = 0;
        u32 parkUntil = 0;
        u32 homeLot = 0;
        u32 workLot = 0;
        u32 purpose = TRIP_WORK;
        u32 targetLot = 0;
        u32 routeState = ROUTE_NONE;
        u32 routeVersion = 0;
        u32 routeIndex = 0;
        u32 routeLength = 0;
        u32 stoppedSince = 0;
        u32 redRuns = 0;
        u32 sublane = 0;
        u32 plan = PLAN_NONE;
        u32 node = 0xFFFFFFFFu;
        u32 move = 0xFFFFFFFFu;
        float moveS = 0.0f;
        u32 queuedAt = 0xFFFFFFFFu;
        u32 grantOrder = 0;
        float slide = 0.0f;
        u32 wish = 0;

        SPITE_SCHEMA(CityCarComponent, id, axis, line, dir, pos, speed, maxSpeed, turns, state, destX, destZ, trips,
                     parkUntil, homeLot, workLot, purpose, targetLot, routeState, routeVersion, routeIndex, routeLength,
                     stoppedSince, redRuns, sublane, plan, node, move, moveS, queuedAt, grantOrder, slide, wish)
    };

     
     
     
     
     
     
     
     
     
    struct CityPedestrianComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityPedestrian";
        u32 id = 0;
        u32 x = 0;
        u32 z = 0;
        u32 corner = 0;
        u32 toX = 0;
        u32 toZ = 0;
        u32 toCorner = 0;
        float speed = 1.4f;
        u32 mode = PED_WALKING;
        float progress = 0.0f;
        u32 legs = 0;
        u32 crossings = 0;
        u32 homeLot = 0;
        u32 targetLot = 0;
        u32 purpose = TRIP_SHOP;
        u32 dwellUntil = 0;
        u32 routeState = ROUTE_NONE;
        u32 routeVersion = 0;
        u32 routeIndex = 0;
        u32 routeLength = 0;
        u32 workLot = NO_LOT;
        u32 carLot = NO_LOT;
        u32 lane = 0;  
        u32 crowdSpot = ~0u;  

        SPITE_SCHEMA(CityPedestrianComponent, id, x, z, corner, toX, toZ, toCorner, speed, mode, progress, legs,
                     crossings, homeLot, targetLot, purpose, dwellUntil, routeState, routeVersion, routeIndex,
                     routeLength, workLot, carLot, lane, crowdSpot)
    };

     
     
     
    inline constexpr u32 NO_PEDESTRIAN = 0xFFFFFFFFu;

    struct CityAvatarComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityAvatar";
        u32 index = 0;  
        u32 pedestrian = NO_PEDESTRIAN;

        SPITE_SCHEMA(CityAvatarComponent, index, pedestrian)
    };

     
     
     
    struct AvatarCandidate
    {
        float d2 = 0.0f;  
        u32 id = 0;
        Entity entity = Entity::undefined();
    };

    struct CityAvatarPool
    {
        u32 frames = 0;
        Entity first = Entity::undefined();  
        std::vector<Entity> avatars;
        std::vector<Entity> pedestrianEntity;  
        std::vector<u32> pedestrianOf;
        std::vector<u32> slotOf;
        std::vector<float> slotD2;  
        std::vector<float> retiring;
        std::vector<bool> appearing;
        std::vector<u32> seen;  
        std::vector<AvatarCandidate> candidates;  
        u32 scan = 0;
        u32 assigned = 0;
        u32 acquired = 0;  
        u32 released = 0;
        u32 nearBoxes = 0;  
    };

     
     
    inline constexpr u32 DRONE_CHARGING = 0;
    inline constexpr u32 DRONE_WAITING = 1;
    inline constexpr u32 DRONE_TAKEOFF = 2;
    inline constexpr u32 DRONE_CRUISE = 3;
    inline constexpr u32 DRONE_DESCENT = 4;
    inline constexpr u32 DRONE_DROP = 5;
    inline constexpr u32 DRONE_CLIMB = 6;
    inline constexpr u32 DRONE_RETURN = 7;
    inline constexpr u32 DRONE_LANDING = 8;
    inline constexpr u32 DRONE_PHASES = 9;

     
     
     
     
     
    struct CityDroneComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityDrone";
        u32 id = 0;
        u32 hub = 0;
        u32 pad = 0;
        u32 phase = DRONE_CHARGING;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
        float padX = 0.0f;
        float padY = 0.0f;
        float padZ = 0.0f;
        float toX = 0.0f;
        float toY = 0.0f;
        float toZ = 0.0f;
        float altitude = 0.0f;
        float returnAltitude = 0.0f;
        u32 package = 0;
        u32 targetLot = 0;
        u32 phaseUntil = 0;
        u32 deliveries = 0;
        float dropX = 0.0f;
        float dropY = 0.0f;
        float dropZ = 0.0f;
        u32 dropStep = 0;
        u32 dropped = 0;  

        SPITE_SCHEMA(CityDroneComponent, id, hub, pad, phase, x, y, z, yaw, padX, padY, padZ, toX, toY, toZ, altitude,
                     returnAltitude, package, targetLot, phaseUntil, deliveries, dropX, dropY, dropZ, dropStep, dropped)
    };

     
     
    struct CityParcelComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "CityParcel";
        u32 drone = 0;

        SPITE_SCHEMA(CityParcelComponent, drone)
    };
}  
