#include "CityPlay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "CityLogistics.hpp"
#include "CityModel.hpp"
#include "CityPaths.hpp"
#include "CityTrips.hpp"
#include "application/input/InputSystem.hpp"
#include "assetruntime/AssetComponents.hpp"
#include "assetruntime/ModelSpawner.hpp"
#include "base/ContentRoot.hpp"
#include "base/FlyBoost.hpp"
#include "base/Logging.hpp"
#include "base/MathFunctions.hpp"
#include "base/ServiceLocator.hpp"
#include "control/ControlServer.hpp"
#include "ecs/CommandBuffer.hpp"
#include "ecs/Query.hpp"
#include "ecs/SystemContext.hpp"
#include "engine/Module.hpp"
#include "renderruntime/GameView.hpp"
#include "renderruntime/RenderingComponents.hpp"
#include "serialization/FormatWalk.hpp"
#include "serialization/JsonFormat.hpp"
#include "ui/UiComponents.hpp"
#include "ui/UiContext.hpp"
#include "ui/UiDocument.hpp"
#include "ui/UiScope.hpp"
#include "uiview/UiFrame.hpp"
#include "uiview/UiStyleSheet.hpp"
#include "uiview/UiViewContext.hpp"
#include "world/EntityTransformManager.hpp"
#include "world/TransformComponents.hpp"

 
 
 
 
 
 
 
 
namespace spite::city
{
    namespace
    {
        using namespace model;

         

        constexpr float MIN_DISTANCE = 15.0f;
        constexpr float MAX_DISTANCE = 2000.0f;
        constexpr float MIN_TILT = 0.15f;  
        constexpr float MAX_TILT = 1.5f;
        constexpr float ORBIT_PER_PIXEL = 0.005f;
        constexpr float ROTATE_RATE = 1.6f;  
        constexpr float PAN_PER_DISTANCE = 1.1f;  
        constexpr float PAN_BOOST = 3.0f;  
        constexpr float ZOOM_PER_NOTCH = 0.15f;
        constexpr float EDGE_NDC = 0.99f;  
        constexpr float DAMPING = 10.0f;  
        constexpr float GROUND_CLEARANCE = 2.0f;  
        constexpr float MAX_SECONDS = 0.1f;
        constexpr float FOCUS_MARGIN = 300.0f;  

        constexpr u32 MODE_ORBIT = 0;
        constexpr u32 MODE_FLY = 1;

         
        constexpr float FLY_MAX_PITCH = 1.55f;
        constexpr float FLY_SPEED_PER_ALTITUDE = 0.5f;
        constexpr float FLY_MIN_SPEED = 15.0f;
        constexpr float LOOK_PER_PIXEL = 0.004f;
        constexpr float BOOST_RATE = 4.5f;
        constexpr float BOOST_MAX = 20.0f;

        struct Rig
        {
            Vec3 focus{0.0f};
            float yaw = 0.0f;
            float tilt = 0.8f;
            float distance = 400.0f;
        };

        Quat lookRotation(const float yaw, const float pitch)
        {
            return multiply(fromAxisAngle(Vec3(0.0f, 1.0f, 0.0f), yaw), fromAxisAngle(Vec3(1.0f, 0.0f, 0.0f), pitch));
        }

        Vec3 rigPosition(const Rig& rig)
        {
            const float c = std::cos(rig.tilt);
            return rig.focus + Vec3(c * std::sin(rig.yaw), std::sin(rig.tilt), c * std::cos(rig.yaw)) * rig.distance;
        }

        Quat rigRotation(const Rig& rig) { return lookRotation(rig.yaw, -rig.tilt); }

         
         
        Rig rigOf(const TransformComponent& camera)
        {
            const Vec3 forward = rotate(camera.rotation, Vec3(0.0f, 0.0f, -1.0f));
            Rig rig;
            rig.yaw = std::atan2(-forward.x, -forward.z);
            rig.tilt = std::clamp(std::asin(std::clamp(-forward.y, -1.0f, 1.0f)), MIN_TILT, MAX_TILT);
            const float height = std::max(camera.position.y, GROUND_CLEARANCE);
            rig.distance = std::clamp(height / std::sin(rig.tilt), MIN_DISTANCE, MAX_DISTANCE);
            const float ahead = rig.distance * std::cos(rig.tilt);
            rig.focus = Vec3(camera.position.x - ahead * std::sin(rig.yaw), 0.0f,
                             camera.position.z - ahead * std::cos(rig.yaw));
            return rig;
        }

        constexpr float CLICK_NDC = 0.01f;  

        enum Action : u32
        {
            ACT_RIGHT,
            ACT_LEFT,
            ACT_FORWARD,
            ACT_BACK,
            ACT_ROTATE_LEFT,  
            ACT_ROTATE_RIGHT,  
            ACT_BOOST,
            ACT_ORBIT,  
            ACT_CLICK,
            ACT_TOGGLE_FLY,
            ACT_CANCEL,
            ACT_COUNT
        };
        constexpr const char* ACTION_NAMES[ACT_COUNT] = {
            "CityRight", "CityLeft",  "CityForward", "CityBack",      "CityRotateLeft", "CityRotateRight",
            "CityBoost", "CityOrbit", "CityClick",   "CityToggleFly", "CityCancel"};

        constexpr std::string_view CLASS_NAMES[ROAD_CLASSES] = {"street", "avenue", "boulevard", "highway"};

         

        enum Kind : u32
        {
            KIND_NONE,
            KIND_CAR,
            KIND_PERSON,
            KIND_DRONE,
            KIND_BUILDING,
            KIND_LOT,
            KIND_ROAD,
            KIND_JUNCTION,
            KIND_LAMP,
            KIND_TREE,
            KIND_TRAFFIC_LIGHT,
            KIND_GROUND,
            KIND_PACKAGE
        };
        constexpr const char* KIND_NAMES[] = {"none",         "car",    "person",   "drone", "building",
                                              "lot",          "road",   "junction", "lamp",  "tree",
                                              "trafficLight", "ground", "package"};

         
         
        struct Selection
        {
            u32 kind = KIND_NONE;
            Entity entity = Entity::undefined();
            u32 id = 0;
            u32 axis = 0;
            u32 x = 0;
            u32 z = 0;
            Vec3 at{0.0f};
        };

         
        constexpr u32 MARKER_ROUTE = 0;
        constexpr u32 MARKER_CORNERS = 192;
        constexpr u32 MARKER_COUNT = MARKER_CORNERS + 24;
        constexpr float MARKER_PARK_Y = -100000.0f;
        constexpr float MARKER_GROW_DISTANCE = 150.0f;  
        constexpr float FLIGHT_LINE_DROP = 1.5f;  
        struct Marker
        {
            bool shown = false;
            TransformComponent transform;
            Vec4 color{1.0f};
        };

         
        struct Feedback
        {
            std::mutex lock;
            std::string text;
            u64 revision = 0;
        };

         
        struct CityPlay
        {
            CityPlay(InputSystem* inputSystem, ControlServer& controlServer, const InternedString ownerName,
                     std::string name, const World playWorld) :
                input(inputSystem), control(&controlServer), owner(ownerName), worldName(std::move(name)),
                world(playWorld), feedback(std::make_shared<Feedback>())
            {
                if (!input)
                {
                    return;
                }
                context = input->createContext({.name = InternedString("City")});
                for (u32 i = 0; i < ACT_COUNT; ++i)
                {
                    actions[i] = InternedString(ACTION_NAMES[i]);
                }
                const auto bind = [this](const Action action, const InputControlDevice device, const u16 code)
                {
                    input->addBinding(context,
                                      {.action = actions[action],
                                       .control = {.device = device, .code = code},
                                       .trigger = InputActionType::Axis});
                };
                const auto key = [&](const Action action, const KeyCode code)
                { bind(action, InputControlDevice::Keyboard, static_cast<u16>(code)); };
                key(ACT_RIGHT, KeyCode::D);
                key(ACT_RIGHT, KeyCode::RIGHT);
                key(ACT_LEFT, KeyCode::A);
                key(ACT_LEFT, KeyCode::LEFT);
                key(ACT_FORWARD, KeyCode::W);
                key(ACT_FORWARD, KeyCode::UP);
                key(ACT_BACK, KeyCode::S);
                key(ACT_BACK, KeyCode::DOWN);
                key(ACT_ROTATE_LEFT, KeyCode::Q);
                key(ACT_ROTATE_RIGHT, KeyCode::E);
                key(ACT_BOOST, KeyCode::LSHIFT);
                key(ACT_BOOST, KeyCode::RSHIFT);
                key(ACT_TOGGLE_FLY, KeyCode::F);
                key(ACT_CANCEL, KeyCode::ESCAPE);
                bind(ACT_ORBIT, InputControlDevice::MouseButton, static_cast<u16>(MouseButton::Right));
                bind(ACT_ORBIT, InputControlDevice::MouseButton, static_cast<u16>(MouseButton::Middle));
                bind(ACT_CLICK, InputControlDevice::MouseButton, static_cast<u16>(MouseButton::Left));
            }

            ~CityPlay()
            {
                if (input)
                {
                    if (captured)
                    {
                        input->setCursorCaptured(false);
                    }
                    input->destroyContext(context);
                }
            }

            CityPlay(const CityPlay&) = delete;
            CityPlay& operator=(const CityPlay&) = delete;

            bool down(const Action action) const { return input && input->isActionDown(actions[action]); }

             
            bool pressed(const Action action)
            {
                const bool now = down(action);
                const bool edge = now && !held[action];
                held[action] = now;
                return edge;
            }

             
            void issue(const std::string_view command, const std::string& args, std::string done = {})
            {
                const std::string withWorld = worldName.empty()
                    ? args
                    : (args.empty() ? std::format(R"("world":"{}")", worldName)
                                    : std::format(R"({},"world":"{}")", args, worldName));
                std::string request = std::format(R"({{"id":0,"command":"{}","args":{{{}}}}})", command, withWorld);
                std::weak_ptr<Feedback> weak = feedback;
                control->runAtFrameStart(
                    owner.toView(), std::move(request),
                    [weak, command = std::string(command), done = std::move(done)](const std::string& reply)
                    {
                        const std::shared_ptr<Feedback> shown = weak.lock();
                        if (!shown)
                        {
                            return;
                        }
                        std::string text = done;
                        if (reply.find(R"("ok":true)") == std::string::npos)
                        {
                            const size_t at = reply.find(R"("error":")");
                            text = std::format("{} refused: {}", command,
                                               at == std::string::npos
                                                   ? reply
                                                   : reply.substr(at + 9, reply.find('"', at + 9) - at - 9));
                        }
                        if (text.empty())
                        {
                            return;
                        }
                        std::lock_guard guard(shown->lock);
                        shown->text = std::move(text);
                        ++shown->revision;
                    });
            }

            u64 cornerRevision = ~0ull;
            float cornerAge = 0.0f;
            InputSystem* input = nullptr;  
            ControlServer* control = nullptr;
            InternedString owner;
            std::string worldName;  
            World world;
            InputContextId context = INVALID_INPUT_CONTEXT_ID;
            InternedString actions[ACT_COUNT];
            bool held[ACT_COUNT] = {};
            std::shared_ptr<Feedback> feedback;

             
            u32 mode = MODE_ORBIT;
            Entity camera = Entity::undefined();  
            bool rigReady = false;
            Rig rig;  
            Rig target;  
            Entity follow = Entity::undefined();
            bool edgePan = true;
             
             
            Vec2 lastMouse{};
            bool mouseSeen = false;
            bool pointerMoved = false;
            float flyYaw = 0.0f;
            float flyPitch = 0.0f;
            FlyBoost boost;
            bool captured = false;
             
            std::vector<float> roofs;
            u32 roofSize = 0;
            u32 roofVersion = 0xFFFFFFFFu;

            bool pressing = false;
            Vec2 pressNdc{};
            u64 ticket = 0;
            GamePicker* picker = nullptr;

             
            bool pointerInside = false;
            Vec2 pointerNdc{};
            std::optional<Vec3> pointerGround;
            u32 clicks = 0;  
            u32 picks = 0;  
            std::string pickNote;  

             
            Selection selection;
            u64 selectionRevision = 0;
            bool markersCreated = false;
            bool markersParked = true;
        };

        std::optional<Vec3> groundHit(const TransformComponent& camera, const CameraComponent& lens, const float aspect,
                                      const Vec2 ndc)
        {
            const float half = std::tan(lens.fov * 0.5f);
            const Vec3 direction = rotate(camera.rotation, Vec3(ndc.x * half * aspect, ndc.y * half, -1.0f));
            if (direction.y > -1.0e-4f)
            {
                return std::nullopt;
            }
            return camera.position + direction * (-camera.position.y / direction.y);
        }

        float lengthXZ(const Vec3& a, const Vec3& b) { return std::hypot(b.x - a.x, b.z - a.z); }

         

        Entity parentOf(const World world, const Entity entity)
        {
            if (!world.services().contains<EntityTransformManager>())
            {
                return Entity::undefined();
            }
            return world.services().read<EntityTransformManager>()->attachedParentOf(entity);
        }

        template <typename T, typename Fn>
        void eachRow(const World world, Fn&& fn)
        {
            for (const Chunk* chunk : world.getChunksContaining(Aspect(getComponentId<T>())))
            {
                const T* rows = chunk->readColumn<T>();
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    fn(rows[row], chunk->entities()[row]);
                }
            }
        }

         
        Selection roadNear(const RoadGraph& graph, const float x, const float z)
        {
            Selection found;
            const int nx = static_cast<int>(std::lround(x / CELL));
            const int nz = static_cast<int>(std::lround(z / CELL));
            if (nx >= 0 && nz >= 0)
            {
                const u32 node = graph.nodeAt(static_cast<u32>(nx), static_cast<u32>(nz));
                if (node != NO_ID && std::fabs(x - nx * CELL) <= graph.nodes[node].half[1] + 1.0f &&
                    std::fabs(z - nz * CELL) <= graph.nodes[node].half[0] + 1.0f)
                {
                    found.kind = KIND_JUNCTION;
                    found.x = static_cast<u32>(nx);
                    found.z = static_cast<u32>(nz);
                    return found;
                }
            }
            float best = 1.0e9f;
            for (u32 axis = 0; axis < 2; ++axis)
            {
                const float across = axis == 0 ? z : x;
                const float along = axis == 0 ? x : z;
                const int cell = static_cast<int>(std::floor(along / CELL));
                const int centre = static_cast<int>(std::lround(across / CELL));
                for (int line = centre - 2; line <= centre + 2; ++line)
                {
                    if (line < 0 || cell < 0)
                    {
                        continue;
                    }
                    const u32 edge = graph.edgeAt(axis, static_cast<u32>(line), static_cast<u32>(cell));
                    if (edge == NO_ID)
                    {
                        continue;
                    }
                    const float distance = std::fabs(across - static_cast<float>(line) * CELL);
                    const float corridor =
                        std::max(ROAD_CLASS_SPECS[graph.edges[edge].roadClass].corridor, graph.edges[edge].half());
                    if (distance <= corridor && distance < best)
                    {
                        best = distance;
                        found.kind = KIND_ROAD;
                        found.axis = axis;
                        found.x = axis == 0 ? static_cast<u32>(cell) : static_cast<u32>(line);
                        found.z = axis == 0 ? static_cast<u32>(line) : static_cast<u32>(cell);
                    }
                }
            }
            return found;
        }

         
        u32 selectedEdge(const RoadGraph& graph, const Selection& selection)
        {
            return selection.axis == 0 ? graph.edgeAt(0, selection.z, selection.x)
                                       : graph.edgeAt(1, selection.x, selection.z);
        }

         
         
        Selection resolve(const World world, const CityMap* map, Entity picked, const std::optional<Vec3>& at)
        {
            Selection selection;
            for (u32 depth = 0; depth < 16 && picked.isValid() && world.isEntityAlive(picked); ++depth)
            {
                selection.entity = picked;
                if (const auto* car = world.tryGetComponent<CityCarComponent>(picked))
                {
                    selection.kind = KIND_CAR;
                    selection.id = car->id;
                    return selection;
                }
                if (const auto* ped = world.tryGetComponent<CityPedestrianComponent>(picked))
                {
                    selection.kind = KIND_PERSON;
                    selection.id = ped->id;
                    return selection;
                }
                if (const auto* avatar = world.tryGetComponent<CityAvatarComponent>(picked))
                {
                     
                    if (avatar->pedestrian != NO_PEDESTRIAN)
                    {
                        selection.kind = KIND_PERSON;
                        selection.id = avatar->pedestrian;
                        if (world.services().contains<CityAvatarPool>())
                        {
                            const CityAvatarPool& pool = world.services().get<CityAvatarPool>();
                            if (avatar->index < pool.pedestrianEntity.size())
                            {
                                selection.entity = pool.pedestrianEntity[avatar->index];
                            }
                        }
                    }
                    return selection;
                }
                if (const auto* drone = world.tryGetComponent<CityDroneComponent>(picked))
                {
                    selection.kind = KIND_DRONE;
                    selection.id = drone->id;
                    return selection;
                }
                if (const auto* parcel = world.tryGetComponent<CityParcelComponent>(picked))
                {
                    selection.kind = KIND_PACKAGE;
                    selection.id = parcel->drone;
                    return selection;
                }
                if (const auto* building = world.tryGetComponent<CityBuildingComponent>(picked))
                {
                    selection.kind = KIND_BUILDING;
                    selection.id = building->lot;
                    return selection;
                }
                if (world.hasComponent<CityLampComponent>(picked))
                {
                    selection.kind = KIND_LAMP;
                    return selection;
                }
                if (const auto* piece = world.tryGetComponent<CityPieceComponent>(picked))
                {
                    if (piece->kind == PIECE_TREE || piece->kind == PIECE_TRAFFIC_LIGHT)
                    {
                        selection.kind = piece->kind == PIECE_TREE ? KIND_TREE : KIND_TRAFFIC_LIGHT;
                        return selection;
                    }
                    break;  
                }
                if (const auto* road = world.tryGetComponent<CityRoadComponent>(picked))
                {
                    selection.kind = KIND_ROAD;
                    selection.axis = road->z0 == road->z1 ? 0u : 1u;
                    selection.x = road->x0;
                    selection.z = road->z0;
                    return selection;
                }
                if (const auto* junction = world.tryGetComponent<CityJunctionComponent>(picked))
                {
                    selection.kind = KIND_JUNCTION;
                    selection.x = junction->x;
                    selection.z = junction->z;
                    return selection;
                }
                if (world.hasComponent<CityGroundComponent>(picked))
                {
                    break;
                }
                picked = parentOf(world, picked);
            }
            selection = {};
            if (!at || !map)
            {
                return selection;
            }
            selection.at = *at;
            const Selection road = roadNear(map->graph(), at->x, at->z);
            if (road.kind != KIND_NONE)
            {
                selection.kind = road.kind;
                selection.axis = road.axis;
                selection.x = road.x;
                selection.z = road.z;
                return selection;
            }
            const float cx = std::floor(at->x / CELL);
            const float cz = std::floor(at->z / CELL);
            if (cx < 0.0f || cz < 0.0f)
            {
                selection.kind = KIND_GROUND;
                return selection;
            }
            std::optional<u32> lotId;
            eachRow<CityLotComponent>(world,
                                      [&](const CityLotComponent& lot, Entity)
                                      {
                                          const u32 x = static_cast<u32>(cx);
                                          const u32 z = static_cast<u32>(cz);
                                          if (!lotId && x >= lot.x0 && x < lot.x1 && z >= lot.z0 && z < lot.z1)
                                          {
                                              lotId = lot.id;
                                          }
                                      });
            if (!lotId)
            {
                selection.kind = KIND_GROUND;
                return selection;
            }
            selection.kind = KIND_LOT;
            selection.id = *lotId;
            eachRow<CityBuildingComponent>(world,
                                           [&](const CityBuildingComponent& building, const Entity entity)
                                           {
                                               if (building.lot == *lotId)
                                               {
                                                   selection.kind = KIND_BUILDING;
                                                   selection.entity = entity;
                                               }
                                           });
            return selection;
        }

         

        using FieldValue = std::variant<u32, double, std::string>;

        struct InspectField
        {
            std::string key;
            std::string label;
            FieldValue value;
            std::string unit;
        };

        struct InspectGroup
        {
            std::string key;  
            std::string title;
            std::vector<InspectField> fields;
        };

        struct Inspection
        {
            u32 kind = KIND_NONE;
            Entity entity = Entity::undefined();
            std::string title;
            std::vector<InspectGroup> groups;
            std::vector<Vec3> route;  
            std::vector<std::array<u32, 3>> routeNodes;  
            Vec3 pin{0.0f};  
            bool pinned = false;

            InspectGroup& group(const std::string& key, const std::string& title)
            {
                for (InspectGroup& existing : groups)
                {
                    if (existing.key == key)
                    {
                        return existing;
                    }
                }
                return groups.emplace_back(InspectGroup{key, title, {}});
            }
            void add(const std::string& groupKey, const std::string& key, const std::string& label, FieldValue value,
                     const std::string& unit = {})
            {
                InspectGroup& into = group(groupKey, groupKey);
                into.fields.push_back({key, label, std::move(value), unit});
            }
        };

        constexpr const char* TRUCK_NAMES[] = {"van", "box truck", "semi"};

        const char* droneStatus(const u32 phase)
        {
            constexpr const char* NAMES[DRONE_PHASES] = {"charging",   "waiting for a parcel",
                                                         "taking off", "cruising",
                                                         "descending", "dropping",
                                                         "climbing",   "returning",
                                                         "landing"};
            return NAMES[std::min(phase, DRONE_PHASES - 1)];
        }

        std::string lotText(const CityMap& map, const u32 lot)
        {
            const LotRef* ref = map.lot(lot);
            if (!ref)
            {
                return std::format("lot {} (gone)", lot);
            }
            return ref->special != SPECIAL_NONE
                ? std::format("{} {}", SPECIAL_ZONE_NAMES[std::min(ref->special, SPECIAL_KINDS - 1)], lot)
                : std::format("{} {}", ZONE_NAMES[std::min(ref->zone, ZONE_COUNT - 1)], lot);
        }

         
        void addPackage(Inspection& inspection, const CityStateComponent& city, const CityMap& map,
                        const Package& package, const std::string& group)
        {
            inspection.add(group, "package", "Package", package.id);
            inspection.add(group, "packageKind", "Kind", std::string(PACKAGE_KIND_NAMES[package.kind]));
            inspection.add(group, "packageState", "State", std::string(PACKAGE_STATE_NAMES[package.state]));
            inspection.add(group, "origin", "From", lotText(map, package.origin));
            inspection.add(group, "destination", "To", lotText(map, package.destination));
            inspection.add(group, "ordered", "Ordered at", clockText(timeOfDay(city, package.orderedStep)));
            if (package.state == PACKAGE_DELIVERED)
            {
                inspection.add(group, "delivered", "Delivered at", clockText(timeOfDay(city, package.deliveredStep)));
            }
        }

        const char* purposeName(const u32 purpose) { return PED_PURPOSE_NAMES[std::min(purpose, PED_PURPOSES - 1)]; }

        const char* routeStateText(const u32 state)
        {
            constexpr const char* NAMES[] = {"none", "pending", "ready", "failed", "rerouting"};
            return NAMES[std::min(state, 4u)];
        }

        const char* levelName(const u32 level)
        {
            constexpr const char* NAMES[CONGESTION_LEVELS] = {"free", "busy", "heavy", "jammed"};
            return NAMES[std::min(level, CONGESTION_LEVELS - 1)];
        }

        std::string clockAtStep(const CityStateComponent& city, const u32 step)
        {
            return clockText(timeOfDay(city, step));
        }

         
        u32 minutesBetween(const CityStateComponent& city, const u32 from, const u32 to)
        {
            return to <= from ? 0u
                              : static_cast<u32>(static_cast<double>(to - from) * 1440.0 / std::max(city.daySteps, 1u));
        }

        constexpr float ETA_SPEED_SHARE = 0.6f;  

        std::string roadText(const RoadGraph& graph, const u32 edge)
        {
            const GraphEdge& e = graph.edges[edge];
            const GraphNode& a = graph.nodes[e.a];
            const GraphNode& b = graph.nodes[e.b];
            return std::format("{} ({},{} - {},{})", CLASS_NAMES[e.roadClass], a.x, a.z, b.x, b.z);
        }

        struct Population
        {
            u32 residents = 0;
            u32 workers = 0;
            u32 atHome = 0;
            u32 atWork = 0;
            u32 visitors = 0;
        };

         
         
         
        Population populationOf(const World world, const CityStateComponent& city, const CityMap& map, const u32 lot)
        {
            Population population;
            const LotRef* ref = map.lot(lot);
            const u32 lotEdge = ref ? ref->edge : NO_ID;
            const RoadGraph& graph = map.graph();
            eachRow<CityCarComponent>(world,
                                      [&](const CityCarComponent& car, Entity)
                                      {
                                          if (isTruck(city, car.id))
                                          {
                                              return;
                                          }
                                          population.residents += car.homeLot == lot ? 1 : 0;
                                          population.workers += car.workLot == lot ? 1 : 0;
                                          if (car.state != CAR_PARKED)
                                          {
                                              return;
                                          }
                                          const bool onLotRoad = lotEdge != NO_ID &&
                                              graph.edgeUnder(car.axis, car.line, car.pos, car.dir) == lotEdge;
                                          if (car.homeLot == lot && car.purpose == TRIP_WORK)
                                          {
                                              ++population.atHome;
                                          }
                                          else if (car.workLot == lot && car.purpose != TRIP_WORK && onLotRoad)
                                          {
                                              ++population.atWork;
                                          }
                                      });
            eachRow<CityPedestrianComponent>(world,
                                             [&](const CityPedestrianComponent& ped, Entity)
                                             {
                                                 population.residents += ped.homeLot == lot ? 1 : 0;
                                                 if (ped.mode != PED_DWELLING)
                                                 {
                                                     return;
                                                 }
                                                 population.atHome +=
                                                     ped.homeLot == lot && ped.purpose == TRIP_SHOP ? 1 : 0;
                                             });
            return population;
        }

         
         
        void addRoute(Inspection& inspection, const CityStateComponent& city, const RoadGraph& graph,
                      const RouteSlot* slot, const u32 state, const u32 index, const bool pedestrian, const Vec3& from,
                      const float speed, const float maxSpeed)
        {
            const bool following = state == ROUTE_READY || state == ROUTE_REROUTING;
            inspection.add("route", "state", "Route", std::string(routeStateText(state)));
            if (!slot || !following)
            {
                return;
            }
            const u32 side = city.size + 1;
            const sizet first = pedestrian ? static_cast<sizet>(index) + 1 : index;
            inspection.route.push_back(from);
            float length = 0.0f;
            float seconds = 0.0f;
            for (sizet i = first; i < slot->active.size(); ++i)
            {
                const u32 key = slot->active[i];
                Vec3 point;
                if (pedestrian)
                {
                    const u32 node = graph.nodeOfKey(key / 4);
                    if (node == NO_ID)
                    {
                        break;
                    }
                    point = cornerPoint(graph, node, key & 3);
                    inspection.routeNodes.push_back({key / 4 % side, key / 4 / side, key & 3});
                }
                else
                {
                    point = Vec3(static_cast<float>(key % side) * CELL, 0.0f, static_cast<float>(key / side) * CELL);
                    inspection.routeNodes.push_back({key % side, key / side, 0});
                }
                const float leg = lengthXZ(inspection.route.back(), point);
                length += leg;
                float limit = speed;
                if (!pedestrian && i > first)
                {
                    const u32 edge = graph.edgeBetween(slot->active[i - 1], key);
                    limit = std::min(maxSpeed, edge != NO_ID ? graph.edges[edge].limit() : maxSpeed);
                }
                seconds += leg / std::max(pedestrian ? speed : limit * ETA_SPEED_SHARE, 0.5f);
                inspection.route.push_back(point);
            }
            const u32 arrive = city.step + static_cast<u32>(seconds * 60.0f);
            inspection.add("route", "remaining", "Nodes left", static_cast<u32>(inspection.routeNodes.size()));
            inspection.add("route", "lengthM", "Distance", static_cast<double>(std::round(length)), "m");
            inspection.add("route", "etaMinutes", "Arrives in",
                           static_cast<u32>(minutesBetween(city, city.step, arrive)), "min");
            inspection.add("route", "etaClock", "Arrives at", clockAtStep(city, arrive));
        }

        Vec3 positionOf(const World world, const Entity entity)
        {
            const auto* transform = world.tryGetComponent<TransformComponent>(entity);
            return transform ? transform->position : Vec3(0.0f);
        }

         
        template <typename T>
        std::optional<std::pair<T, Entity>> rowById(const World world, const u32 id, const Entity hint)
        {
            if (hint.isValid() && world.isEntityAlive(hint))
            {
                if (const T* row = world.tryGetComponent<T>(hint); row && row->id == id)
                {
                    return std::make_pair(*row, hint);
                }
            }
            std::optional<std::pair<T, Entity>> found;
            eachRow<T>(world,
                       [&](const T& row, const Entity entity)
                       {
                           if (!found && row.id == id)
                           {
                               found = std::make_pair(row, entity);
                           }
                       });
            return found;
        }

         
        void addTour(Inspection& inspection, const CityStateComponent& city, const CityMap& map, const World world,
                     const CityCarComponent& car)
        {
            inspection.add("tour", "base", "Base", lotText(map, car.homeLot));
            inspection.add("tour", "capacity", "Holds", truckCapacity(truckIndex(city, car.id)), "orders");
            if (car.purpose != TRIP_IDLE)
            {
                inspection.add("tour", "targetLot", car.purpose == TRIP_DEPOT ? "Back to" : "Next stop",
                               lotText(map, car.targetLot));
            }
            const LogisticsService* logistics =
                world.services().contains<LogisticsService>() ? &world.services().get<LogisticsService>() : nullptr;
            const TruckSlot* slot = logistics ? logistics->findTruck(truckIndex(city, car.id)) : nullptr;
            if (!slot)
            {
                return;
            }
            inspection.add("tour", "stops", "Stops left",
                           static_cast<u32>(slot->stops.size() - std::min<sizet>(slot->next, slot->stops.size())));
            for (sizet i = slot->next; i < slot->stops.size() && i < slot->next + 6; ++i)
            {
                const TourStop& stop = slot->stops[i];
                inspection.add("tour", std::format("stop{}", i - slot->next + 1),
                               std::format("Stop {}", i - slot->next + 1),
                               std::format("{} at {}", STOP_NAMES[std::min(stop.action, 3u)], lotText(map, stop.lot)));
            }
            u32 units = 0;
            std::string ids;
            for (const u32 id : slot->load)
            {
                const Package* package = logistics->package(id);
                units += package ? package->units : 0;
                if (ids.size() < 48)
                {
                    ids += std::format("{}{}", ids.empty() ? "" : " ", id);
                }
            }
            inspection.add("load", "packages", "Packages", static_cast<u32>(slot->load.size()));
            inspection.add("load", "units", "Goods", units, "units");
            if (!ids.empty())
            {
                inspection.add("load", "ids", "Ids", ids);
            }
        }

         
        void addSite(Inspection& inspection, const World world, const u32 lot)
        {
            const LogisticsService* logistics =
                world.services().contains<LogisticsService>() ? &world.services().get<LogisticsService>() : nullptr;
            const Site* site = logistics ? logistics->site(lot) : nullptr;
            if (!site)
            {
                return;
            }
            inspection.add("logistics", "role", "Role", std::string(SPECIAL_ZONE_NAMES[site->special]));
            if (site->capacity > 0)
            {
                inspection.add("logistics", "stock", "Stock", std::format("{} / {}", site->stock, site->capacity));
                inspection.add("logistics", "queued", "Orders waiting", static_cast<u32>(site->queue.size()));
                inspection.add("logistics", "incoming", "Supply coming", site->incoming, "units");
            }
            if (site->special == SPECIAL_DRONE_HUB)
            {
                inspection.add("logistics", "drones", "Drones based", site->drones);
            }
            if (site->special == SPECIAL_TRUCK_DEPOT || site->special == SPECIAL_WAREHOUSE)
            {
                inspection.add("logistics", "trucks", "Trucks based", site->trucks);
            }
            inspection.add("logistics", "shipped", "Shipped", site->shipped, "units");
            if (site->special != SPECIAL_FACTORY)
            {
                inspection.add("logistics", "received", "Received", site->received, "units");
            }
        }

         
         
        Inspection inspect(const World world, const Selection& selection, const bool detail = true)
        {
            Inspection inspection;
            inspection.kind = selection.kind;
            inspection.entity = selection.entity;
            const CityStateComponent* found = findCity(world);
            if (!found || selection.kind == KIND_NONE)
            {
                inspection.title = "Nothing selected";
                return inspection;
            }
            const CityStateComponent city = *found;
            const std::shared_ptr<const CityMap> map = worldMap(world, city);
            const RoadGraph& graph = map->graph();
            const PathService* paths =
                world.services().contains<PathService>() ? &world.services().get<PathService>() : nullptr;
            switch (selection.kind)
            {
            case KIND_CAR:
                {
                    const auto row = rowById<CityCarComponent>(world, selection.id, selection.entity);
                    if (!row)
                    {
                        inspection.title = std::format("Car {} (gone)", selection.id);
                        break;
                    }
                    const CityCarComponent& car = row->first;
                    inspection.entity = row->second;
                    const bool truck = isTruck(city, car.id);
                    inspection.title = truck
                        ? std::format("Truck {} ({})", car.id, TRUCK_NAMES[truckModel(truckIndex(city, car.id))])
                        : std::format("Car {}", car.id);
                    const Vec3 at = positionOf(world, row->second);
                    inspection.pin = at + Vec3(0.0f, 4.0f, 0.0f);
                    inspection.pinned = true;
                    const u32 edge = graph.edgeUnder(car.axis, car.line, car.pos, car.dir);
                    std::string status;
                    if (car.state == CAR_DRIVING)
                    {
                        status = car.speed < 0.1f ? "queued" : "driving";
                    }
                    else if (car.state == CAR_AWAITING_ROUTE)
                    {
                        status = "waiting";
                    }
                    else if (truck)
                    {
                        status = car.purpose == TRIP_IDLE ? "at its base"
                            : car.parkUntil == 0xFFFFFFFFu
                            ? "arrived"
                            : (car.purpose == TRIP_DEPOT ? "unloading, then back" : "at a stop");
                    }
                    else
                    {
                        const LotRef* work = map->lot(car.workLot);
                        status = car.purpose == TRIP_WORK                              ? "at home"
                            : car.purpose == TRIP_SHOP || (work && work->edge == edge) ? "at work"
                                                                                       : "shopping";
                    }
                    inspection.add("", "id", "Id", car.id);
                    inspection.add("", "status", "Status", status);
                    inspection.add("", "speed", "Speed", std::round(car.speed * 36.0) / 10.0, "km/h");
                    inspection.add("", "road", "Road", edge != NO_ID ? roadText(graph, edge) : std::string("-"));
                    if (truck)
                    {
                        addTour(inspection, city, *map, world, car);
                        addRoute(inspection, city, graph, paths ? paths->findCar(car.id) : nullptr, car.routeState,
                                 car.routeIndex, false, at, car.speed, car.maxSpeed);
                        break;
                    }
                    inspection.add("schedule", "homeLot", "Home lot", car.homeLot);
                    inspection.add("schedule", "workLot", "Work lot", car.workLot);
                    inspection.add("schedule", "purpose", "Trip", std::string(purposeName(car.purpose)));
                    inspection.add("schedule", "targetLot", "To lot", car.targetLot);
                    if (car.state == CAR_PARKED)
                    {
                        inspection.add("schedule", "departs", "Leaves at", clockAtStep(city, car.parkUntil));
                        inspection.add("schedule", "departsInMinutes", "Leaves in",
                                       minutesBetween(city, city.step, car.parkUntil), "min");
                    }
                    inspection.add("schedule", "trips", "Trips done", car.trips);
                    addRoute(inspection, city, graph, paths ? paths->findCar(car.id) : nullptr, car.routeState,
                             car.routeIndex, false, at, car.speed, car.maxSpeed);
                    break;
                }
            case KIND_PERSON:
                {
                    const auto row = rowById<CityPedestrianComponent>(world, selection.id, selection.entity);
                    if (!row)
                    {
                        inspection.title = std::format("Person {} (gone)", selection.id);
                        break;
                    }
                    const CityPedestrianComponent& ped = row->first;
                    inspection.entity = row->second;
                    inspection.title = std::format("Person {}", ped.id);
                    const Vec3 at = positionOf(world, row->second);
                    inspection.pin = at + Vec3(0.0f, 3.0f, 0.0f);
                    inspection.pinned = true;
                    const bool indoors = ped.mode == PED_DWELLING || ped.mode == PED_AWAITING_ROUTE;
                    const char* status = ped.mode == PED_WALKING ? "walking"
                        : ped.mode == PED_CROSSING               ? "crossing"
                        : indoors                                ? "indoors"
                                                                 : "waiting";
                    const bool moving = ped.mode == PED_WALKING || ped.mode == PED_CROSSING;
                    inspection.add("", "id", "Id", ped.id);
                    inspection.add("", "status", "Status", std::string(status));
                    inspection.add("", "speed", "Speed", moving ? std::round(ped.speed * 36.0) / 10.0 : 0.0, "km/h");
                    inspection.add("", "junction", "At junction", std::format("{},{}", ped.x, ped.z));
                    inspection.add("schedule", "homeLot", "Home lot", ped.homeLot);
                    inspection.add("schedule", "purpose", "Walk", std::string(purposeName(ped.purpose)));
                    inspection.add("schedule", "targetLot", "To lot", ped.targetLot);
                    if (ped.mode == PED_DWELLING)
                    {
                        inspection.add("schedule", "departs", "Leaves at", clockAtStep(city, ped.dwellUntil));
                        inspection.add("schedule", "departsInMinutes", "Leaves in",
                                       minutesBetween(city, city.step, ped.dwellUntil), "min");
                    }
                    inspection.add("schedule", "walks", "Walks done", ped.legs);
                    addRoute(inspection, city, graph, paths ? paths->findPedestrian(ped.id) : nullptr, ped.routeState,
                             ped.routeIndex, true, at, ped.speed, ped.speed);
                    break;
                }
            case KIND_DRONE:
                {
                    const auto row = rowById<CityDroneComponent>(world, selection.id, selection.entity);
                    if (!row)
                    {
                        inspection.title = std::format("Drone {} (gone)", selection.id);
                        break;
                    }
                    const CityDroneComponent& drone = row->first;
                    inspection.entity = row->second;
                    inspection.title = std::format("Drone {}", drone.id);
                    inspection.pin = Vec3(drone.x, drone.y + 2.0f, drone.z);
                    inspection.pinned = true;
                    const bool out = drone.package != NO_PACKAGE;
                    inspection.add("", "id", "Id", drone.id);
                    inspection.add("", "status", "Status", std::string(droneStatus(drone.phase)));
                    inspection.add("", "altitude", "Height", std::round(drone.y * 10.0) / 10.0, "m");
                    inspection.add("", "hub", "Hub", lotText(*map, drone.hub));
                    inspection.add("", "pad", "Pad", drone.pad + 1);
                    inspection.add("", "deliveries", "Deliveries", drone.deliveries);
                    if (drone.phase == DRONE_CHARGING)
                    {
                        inspection.add("", "readyAt", "Charged at", clockAtStep(city, drone.phaseUntil));
                    }
                    const LogisticsService* logistics = world.services().contains<LogisticsService>()
                        ? &world.services().get<LogisticsService>()
                        : nullptr;
                    if (out)
                    {
                        const Package* package = logistics ? logistics->package(drone.package) : nullptr;
                        if (package)
                        {
                            addPackage(inspection, city, *map, *package, "parcel");
                        }
                        else
                        {
                            inspection.add("parcel", "package", "Package", drone.package);
                        }
                    }
                    if (drone.phase >= DRONE_TAKEOFF)
                    {
                         
                        const bool back = drone.phase >= DRONE_CLIMB;
                        const float lane = back ? drone.returnAltitude : drone.altitude;
                        const Vec3 to =
                            back ? Vec3(drone.padX, drone.padY, drone.padZ) : Vec3(drone.toX, drone.toY, drone.toZ);
                        inspection.add("flight", "lane", "Lane", static_cast<double>(lane), "m");
                        inspection.add("flight", "toward", "Toward",
                                       back ? std::string("its pad") : lotText(*map, drone.targetLot));
                        const float left = lengthXZ(Vec3(drone.x, 0.0f, drone.z), to);
                        inspection.add("flight", "distanceM", "Distance", static_cast<double>(std::round(left)), "m");
                        inspection.route.push_back(Vec3(drone.x, drone.y, drone.z));
                        if (drone.phase == DRONE_TAKEOFF || drone.phase == DRONE_CLIMB)
                        {
                            inspection.route.push_back(Vec3(drone.x, lane, drone.z));
                        }
                        if (left > 0.5f)
                        {
                            inspection.route.push_back(Vec3(to.x, lane, to.z));
                        }
                        inspection.route.push_back(to);
                    }
                    break;
                }
            case KIND_PACKAGE:
                {
                    const auto row = rowById<CityDroneComponent>(world, selection.id, Entity::undefined());
                    const LogisticsService* logistics = world.services().contains<LogisticsService>()
                        ? &world.services().get<LogisticsService>()
                        : nullptr;
                    const u32 id = !row                    ? NO_PACKAGE
                        : row->first.package != NO_PACKAGE ? row->first.package
                                                           : row->first.dropped;
                    const Package* package = logistics && id != NO_PACKAGE ? logistics->package(id) : nullptr;
                    inspection.title = package ? std::format("Package {}", package->id) : std::string("Package");
                    if (world.isEntityAlive(selection.entity))
                    {
                        inspection.pin = positionOf(world, selection.entity) + Vec3(0.0f, 1.5f, 0.0f);
                        inspection.pinned = true;
                    }
                    if (package)
                    {
                        addPackage(inspection, city, *map, *package, "");
                        inspection.add("", "drone", "Drone", selection.id);
                    }
                    break;
                }
            case KIND_BUILDING:
            case KIND_LOT:
                {
                    const LotRef* lot = map->lot(selection.id);
                    std::optional<CityBuildingComponent> building;
                    eachRow<CityBuildingComponent>(world,
                                                   [&](const CityBuildingComponent& row, const Entity entity)
                                                   {
                                                       if (row.lot == selection.id)
                                                       {
                                                           building = row;
                                                           inspection.entity = entity;
                                                       }
                                                   });
                    inspection.kind = building ? KIND_BUILDING : KIND_LOT;
                    inspection.title = building ? std::format("Building on lot {}", selection.id)
                                                : std::format("Lot {}", selection.id);
                    if (!lot)
                    {
                        inspection.title += " (gone)";
                        break;
                    }
                    inspection.add("", "lot", "Lot", lot->id);
                    inspection.add("", "zone", "Zone", std::string(ZONE_NAMES[std::min(lot->zone, ZONE_COUNT - 1)]));
                    inspection.add("", "road", "Road",
                                   lot->edge != NO_ID ? roadText(graph, lot->edge) : std::string("none (cut off)"));
                    if (building)
                    {
                        inspection.add("", "model", "Model",
                                       building->model.isValid() ? std::string(building->model.toView())
                                                                 : std::string("box"));
                        inspection.add("", "height", "Height", std::round(building->height * 10.0) / 10.0, "m");
                        inspection.add(
                            "", "footprint", "Footprint",
                            std::format("{:.0f} x {:.0f} m", building->halfX * 2.0f, building->halfZ * 2.0f));
                        inspection.pin = Vec3(building->x, building->height + 6.0f, building->z);
                        inspection.pinned = true;
                    }
                    addSite(inspection, world, lot->id);
                    if (!detail)
                    {
                        break;
                    }
                    const Population population = populationOf(world, city, *map, lot->id);
                    inspection.add("occupancy", "residents", "Residents", population.residents);
                    inspection.add("occupancy", "workers", "Workers", population.workers);
                    inspection.add("occupancy", "atHome", "Home now", population.atHome);
                    inspection.add("occupancy", "atWork", "At work now", population.atWork);
                    break;
                }
            case KIND_ROAD:
                {
                    const u32 edge = selectedEdge(graph, selection);
                    if (edge == NO_ID)
                    {
                        inspection.title = "Road (gone)";
                        break;
                    }
                    const GraphEdge& e = graph.edges[edge];
                    inspection.title = std::format("Road: {}", CLASS_NAMES[e.roadClass]);
                    const float along = (static_cast<float>(e.lo + e.hi) * 0.5f) * CELL;
                    const float across = static_cast<float>(e.line) * CELL;
                    inspection.pin = e.axis == 0 ? Vec3(along, 6.0f, across) : Vec3(across, 6.0f, along);
                    inspection.pinned = true;
                    if (!detail)
                    {
                        break;
                    }
                    RoadLoadCounter counter(graph);
                    eachRow<CityCarComponent>(world, [&](const CityCarComponent& car, Entity) { counter.add(car); });
                    const float capacity = edgeCapacity(e);
                    inspection.add("", "class", "Class", std::string(CLASS_NAMES[e.roadClass]));
                    inspection.add("", "lanes", "Lanes each way", e.lanes);
                    inspection.add("", "lengthM", "Length", static_cast<double>(e.length()), "m");
                    inspection.add("", "limit", "Limit", std::round(e.limit() * 36.0) / 10.0, "km/h");
                    inspection.add("", "from", "From", std::format("{},{}", graph.nodes[e.a].x, graph.nodes[e.a].z));
                    inspection.add("", "to", "To", std::format("{},{}", graph.nodes[e.b].x, graph.nodes[e.b].z));
                    for (const u32 reverse : {0u, 1u})
                    {
                        const char* group = reverse == 0 ? "forward" : "reverse";
                        const u32 cars = counter.carsOn(edge, reverse);
                        InspectGroup& into = inspection.group(group, reverse == 0 ? "toward +" : "toward -");
                        into.fields.push_back({"cars", "Cars", cars, {}});
                        into.fields.push_back({"capacity", "Holds", static_cast<double>(std::round(capacity)), {}});
                        into.fields.push_back(
                            {"level", "Traffic", std::string(levelName(congestionLevel(cars, capacity))), {}});
                    }
                    break;
                }
            case KIND_JUNCTION:
                {
                    const u32 node = graph.nodeAt(selection.x, selection.z);
                    if (node == NO_ID)
                    {
                        inspection.title = "Junction (gone)";
                        break;
                    }
                    const GraphNode& n = graph.nodes[node];
                    inspection.title = std::format("Junction {},{}", n.x, n.z);
                    inspection.add("", "roads", "Roads", n.degree);
                    inspection.add("", "signal", "Signal", std::string(n.signal ? "yes" : "no"));
                    if (n.signal)
                    {
                        inspection.add("", "green", "Green for",
                                       std::string(greenAxis(city, n.x, n.z, city.step) == 0 ? "x roads" : "z roads"));
                        inspection.add("", "changesInSeconds", "Changes in",
                                       static_cast<double>(phaseStepsLeft(city, n.x, n.z, city.step) / 60), "s");
                    }
                    inspection.pin = Vec3(static_cast<float>(n.x) * CELL, 6.0f, static_cast<float>(n.z) * CELL);
                    inspection.pinned = true;
                    break;
                }
            case KIND_LAMP:
                {
                    const auto* lamp = world.isEntityAlive(selection.entity)
                        ? world.tryGetComponent<CityLampComponent>(selection.entity)
                        : nullptr;
                    inspection.title = "Street lamp";
                    if (lamp)
                    {
                        inspection.add("", "on", "Light", std::string(lamp->on != 0 ? "on" : "off"));
                        inspection.add("", "onAt", "On at", clockText(lamp->onAt));
                        inspection.add("", "offAt", "Off at", clockText(lamp->offAt));
                    }
                    break;
                }
            case KIND_TREE:
                inspection.title = "Tree";
                break;
            case KIND_TRAFFIC_LIGHT:
                inspection.title = "Traffic light";
                break;
            default:
                inspection.title = std::format("Empty land ({:.0f}, {:.0f})", selection.at.x, selection.at.z);
                break;
            }
            return inspection;
        }

        void writeInspection(const Inspection& inspection, IFormatWriter& out)
        {
            out.writeString("kind", KIND_NAMES[inspection.kind]);
            out.writeEntity("entity", inspection.entity);
            out.writeString("title", inspection.title);
            const auto writeField = [&out](const InspectField& field)
            {
                if (const u32* number = std::get_if<u32>(&field.value))
                {
                    out.writeU32(field.key, *number);
                }
                else if (const double* real = std::get_if<double>(&field.value))
                {
                    out.writeF64(field.key, *real);
                }
                else
                {
                    out.writeString(field.key, std::get<std::string>(field.value));
                }
            };
            for (const InspectGroup& group : inspection.groups)
            {
                if (!group.key.empty())
                {
                    out.beginObject(group.key);
                }
                for (const InspectField& field : group.fields)
                {
                    writeField(field);
                }
                if (group.key == "route")
                {
                    out.beginArray("nodes", static_cast<u32>(inspection.routeNodes.size()));
                    for (const std::array<u32, 3>& node : inspection.routeNodes)
                    {
                        out.beginObject({});
                        out.writeU32("x", node[0]);
                        out.writeU32("z", node[1]);
                        if (inspection.kind == KIND_PERSON)
                        {
                            out.writeU32("corner", node[2]);
                        }
                        out.endObject();
                    }
                    out.endArray();
                }
                if (!group.key.empty())
                {
                    out.endObject();
                }
            }
        }

        std::string fieldText(const InspectField& field)
        {
            std::string value;
            if (const u32* number = std::get_if<u32>(&field.value))
            {
                value = std::to_string(*number);
            }
            else if (const double* real = std::get_if<double>(&field.value))
            {
                value = std::format("{:g}", *real);
            }
            else
            {
                value = std::get<std::string>(field.value);
            }
            return field.unit.empty() ? std::format("{}: {}", field.label, value)
                                      : std::format("{}: {} {}", field.label, value, field.unit);
        }

        std::vector<std::string> inspectionLines(const Inspection& inspection)
        {
            std::vector<std::string> lines;
            for (const InspectGroup& group : inspection.groups)
            {
                if (!group.key.empty())
                {
                    lines.push_back(std::format("- {} -", group.title));
                }
                for (const InspectField& field : group.fields)
                {
                    lines.push_back(fieldText(field));
                }
            }
            return lines;
        }

         

        void refreshRoofs(SystemContext& context, CityPlay& play, const CityStateComponent& city)
        {
            if (play.roofVersion == city.designVersion && play.roofSize == city.size)
            {
                return;
            }
            play.roofVersion = city.designVersion;
            play.roofSize = city.size;
            play.roofs.assign(static_cast<sizet>(city.size) * city.size, 0.0f);
            context.query<ecs::Read<CityBuildingComponent>>()->forEachChunk(
                [&](Chunk* chunk)
                {
                    const CityBuildingComponent* rows = chunk->readColumn<CityBuildingComponent>();
                    for (sizet row = 0; row < chunk->size(); ++row)
                    {
                        const CityBuildingComponent& b = rows[row];
                        const auto cell = [&](const float m)
                        { return static_cast<u32>(std::clamp(m / CELL, 0.0f, static_cast<float>(city.size - 1))); };
                        for (u32 z = cell(b.z - b.halfZ); z <= cell(b.z + b.halfZ); ++z)
                        {
                            for (u32 x = cell(b.x - b.halfX); x <= cell(b.x + b.halfX); ++x)
                            {
                                float& roof = play.roofs[static_cast<sizet>(z) * city.size + x];
                                roof = std::max(roof, b.height);
                            }
                        }
                    }
                });
        }

        float roofAt(const CityPlay& play, const float x, const float z)
        {
            if (play.roofSize == 0 || x < 0.0f || z < 0.0f)
            {
                return 0.0f;
            }
            const u32 cx = static_cast<u32>(x / CELL);
            const u32 cz = static_cast<u32>(z / CELL);
            return cx < play.roofSize && cz < play.roofSize ? play.roofs[static_cast<sizet>(cz) * play.roofSize + cx]
                                                            : 0.0f;
        }

        void clampTarget(Rig& rig, const float extent)
        {
            rig.distance = std::clamp(rig.distance, MIN_DISTANCE, MAX_DISTANCE);
            rig.tilt = std::clamp(rig.tilt, MIN_TILT, MAX_TILT);
            rig.focus.x = std::clamp(rig.focus.x, -FOCUS_MARGIN, extent + FOCUS_MARGIN);
            rig.focus.z = std::clamp(rig.focus.z, -FOCUS_MARGIN, extent + FOCUS_MARGIN);
        }

         
        void clearRoofs(const CityPlay& play, Rig& rig)
        {
            for (u32 i = 0; i < 8; ++i)
            {
                const Vec3 at = rigPosition(rig);
                const float floor = roofAt(play, at.x, at.z) + GROUND_CLEARANCE;
                if (at.y >= floor - 1.0e-3f || rig.tilt >= MAX_TILT)
                {
                    return;
                }
                rig.tilt = std::min(MAX_TILT,
                                    std::asin(std::clamp((floor - rig.focus.y) / rig.distance, -1.0f, 1.0f)) + 0.01f);
            }
        }

        float approach(const float from, const float to, const float share)
        {
            const float next = from + (to - from) * share;
            return std::fabs(to - next) < 1.0e-3f ? to : next;
        }

         
        Entity playCamera(SystemContext& context, const CityPlay& play, std::optional<GameViewPointer>& view)
        {
            if (play.input)
            {
                view = gameViewPointer(context.services(), *play.input);
            }
            if (view && context.isEntityValid(view->camera) && context.hasComponent<TransformComponent>(view->camera))
            {
                return view->camera;
            }
            Entity camera = Entity::undefined();
            context.query<ecs::Read<CityCameraComponent>>()->forEachChunk(
                [&](Chunk* chunk)
                {
                    if (!camera.isValid() && chunk->size() > 0)
                    {
                        camera = chunk->entities()[0];
                    }
                });
            return camera;
        }

        std::optional<CityStateComponent> readCity(SystemContext& context)
        {
            std::optional<CityStateComponent> city;
            context.query<ecs::Read<CityStateComponent>>()->forEachChunk(
                [&](Chunk* chunk)
                {
                    if (!city && chunk->size() > 0)
                    {
                        city = chunk->readColumn<CityStateComponent>()[0];
                    }
                });
            return city;
        }

         
         
        void moveCamera(SystemContext& context)
        {
            ServiceLocator& services = context.services();
            if (!services.contains<CityPlay>())
            {
                return;
            }
            WriteService<CityPlay> service = services.write<CityPlay>();
            CityPlay& play = *service;
            std::optional<GameViewPointer> view;
            const Entity camera = playCamera(context, play, view);
            if (!camera.isValid() || !context.isEntityValid(camera) ||
                !context.hasComponent<TransformComponent>(camera))
            {
                return;
            }
            if (view)
            {
                context.query<ecs::Read<CityCameraComponent>>()->forEachChunk(
                    [&](Chunk* chunk)
                    {
                        for (u32 row = 0; row < chunk->size(); ++row)
                            if (chunk->entities()[row] != camera)
                                context.getCommandBuffer().destroyEntity(chunk->entities()[row]);
                    });
            }
            if (context.hasComponent<CameraComponent>(camera))
            {
                CameraComponent& lens = context.getComponent<CameraComponent>(camera);
                if (lens.farPlane != 4000.0f)
                    lens.farPlane = 4000.0f;
            }
            const TransformComponent& current = context.getConstComponent<TransformComponent>(camera);
            const float seconds = std::min(context.deltaTime, MAX_SECONDS);
            if (play.camera != camera || !play.rigReady)
            {
                play.camera = camera;
                play.rig = play.target = rigOf(current);
                play.rigReady = true;
                const Vec3 forward = rotate(current.rotation, Vec3(0.0f, 0.0f, -1.0f));
                play.flyYaw = std::atan2(-forward.x, -forward.z);
                play.flyPitch = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
            }
            InputSystem* input = play.input;
            if (play.pressed(ACT_TOGGLE_FLY))
            {
                play.mode = play.mode == MODE_FLY ? MODE_ORBIT : MODE_FLY;
                const Vec3 forward = rotate(current.rotation, Vec3(0.0f, 0.0f, -1.0f));
                play.flyYaw = std::atan2(-forward.x, -forward.z);
                play.flyPitch = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
                if (play.mode == MODE_ORBIT)
                {
                    play.rig = play.target = rigOf(current);
                }
                play.follow = Entity::undefined();
            }
            const bool orbitHeld = play.down(ACT_ORBIT);
            if (input && orbitHeld != play.captured)
            {
                input->setCursorCaptured(orbitHeld);
                play.captured = orbitHeld;
            }
            const MouseState mouse = input ? input->getMouseState() : MouseState{};
            if (input && (!play.mouseSeen || mouse.x != play.lastMouse.x || mouse.y != play.lastMouse.y))
            {
                play.pointerMoved = play.mouseSeen;
                play.mouseSeen = true;
                play.lastMouse = Vec2{mouse.x, mouse.y};
            }
            const auto axis = [&](const Action positive, const Action negative)
            { return (play.down(positive) ? 1.0f : 0.0f) - (play.down(negative) ? 1.0f : 0.0f); };

            if (play.mode == MODE_FLY)
            {
                const Vec3 direction(axis(ACT_RIGHT, ACT_LEFT), axis(ACT_ROTATE_RIGHT, ACT_ROTATE_LEFT),
                                     axis(ACT_FORWARD, ACT_BACK));
                const bool looking = orbitHeld && (mouse.deltaX != 0.0f || mouse.deltaY != 0.0f);
                const float boost = play.boost.advance(play.down(ACT_BOOST), seconds, BOOST_RATE, BOOST_MAX);
                if (direction == Vec3(0.0f, 0.0f, 0.0f) && !looking)
                {
                    return;
                }
                TransformComponent& transform = context.getComponent<TransformComponent>(camera);
                if (looking)
                {
                    play.flyYaw -= mouse.deltaX * LOOK_PER_PIXEL;
                    play.flyPitch =
                        std::clamp(play.flyPitch - mouse.deltaY * LOOK_PER_PIXEL, -FLY_MAX_PITCH, FLY_MAX_PITCH);
                    transform.rotation = lookRotation(play.flyYaw, play.flyPitch);
                }
                const float speed = std::max(transform.position.y * FLY_SPEED_PER_ALTITUDE, FLY_MIN_SPEED);
                const Vec3 move =
                    rotate(transform.rotation, Vec3(direction.x, 0.0f, -direction.z)) + Vec3(0.0f, direction.y, 0.0f);
                transform.position = transform.position + move * (speed * seconds * boost);
                return;
            }

            const std::optional<CityStateComponent> city = readCity(context);
            const float extent = city ? static_cast<float>(city->size) * CELL : 4096.0f;
            if (city)
            {
                refreshRoofs(context, play, *city);
            }
            Rig& target = play.target;
            if (orbitHeld)
            {
                target.yaw -= mouse.deltaX * ORBIT_PER_PIXEL;
                target.tilt += mouse.deltaY * ORBIT_PER_PIXEL;
            }
            target.yaw += axis(ACT_ROTATE_RIGHT, ACT_ROTATE_LEFT) * ROTATE_RATE * seconds;
            float panX = axis(ACT_RIGHT, ACT_LEFT);
            float panZ = axis(ACT_FORWARD, ACT_BACK);
            const bool inside = view && view->inside;
            if (inside && play.edgePan && play.pointerMoved && !orbitHeld)
            {
                panX += view->ndc.x > EDGE_NDC ? 1.0f : view->ndc.x < -EDGE_NDC ? -1.0f : 0.0f;
                panZ += view->ndc.y > EDGE_NDC ? 1.0f : view->ndc.y < -EDGE_NDC ? -1.0f : 0.0f;
            }
            if (panX != 0.0f || panZ != 0.0f)
            {
                const float speed =
                    PAN_PER_DISTANCE * target.distance * (play.down(ACT_BOOST) ? PAN_BOOST : 1.0f) * seconds;
                const float s = std::sin(target.yaw);
                const float c = std::cos(target.yaw);
                 
                target.focus.x += (panX * c - panZ * s) * speed;
                target.focus.z += (-panX * s - panZ * c) * speed;
                play.follow = Entity::undefined();
            }
            if (inside && mouse.scrollDeltaY != 0.0f)
            {
                target.distance *= std::exp(-mouse.scrollDeltaY * ZOOM_PER_NOTCH);
            }
            if (play.follow.isValid())
            {
                if (context.isEntityValid(play.follow) && context.hasComponent<TransformComponent>(play.follow))
                {
                    const Vec3 at = context.getConstComponent<TransformComponent>(play.follow).position;
                    target.focus = Vec3(at.x, std::max(at.y - 1.0f, 0.0f), at.z);
                }
                else
                {
                    play.follow = Entity::undefined();
                }
            }
            clampTarget(target, extent);
            clearRoofs(play, target);
            const float share = 1.0f - std::exp(-DAMPING * seconds);
            Rig& rig = play.rig;
            rig.focus = Vec3(approach(rig.focus.x, target.focus.x, share), approach(rig.focus.y, target.focus.y, share),
                             approach(rig.focus.z, target.focus.z, share));
            rig.yaw = approach(rig.yaw, target.yaw, share);
            rig.tilt = approach(rig.tilt, target.tilt, share);
            rig.distance = approach(rig.distance, target.distance, share);
            clearRoofs(play, rig);
            const Vec3 position = rigPosition(rig);
            const Quat rotation = rigRotation(rig);
            const Vec3 moved = position - current.position;
            const float turned = std::fabs(rotation.x - current.rotation.x) +
                std::fabs(rotation.y - current.rotation.y) + std::fabs(rotation.z - current.rotation.z) +
                std::fabs(rotation.w - current.rotation.w);
            if (std::fabs(moved.x) + std::fabs(moved.y) + std::fabs(moved.z) < 1.0e-3f && turned < 1.0e-5f)
            {
                return;
            }
            TransformComponent& transform = context.getComponent<TransformComponent>(camera);
            transform.position = position;
            transform.rotation = rotation;
        }

         

        TransformComponent segmentBox(const Vec3& a, const Vec3& b, const float width, const float thickness,
                                      const float y)
        {
            const float length = std::max(lengthXZ(a, b), 0.1f);
            const float yaw = std::atan2(b.x - a.x, b.z - a.z);
            return TransformComponent{.position = {(a.x + b.x) * 0.5f, y, (a.z + b.z) * 0.5f},
                                      .rotation = yawRotation(yaw),
                                      .scale = {width * 0.5f, thickness * 0.5f, length * 0.5f + width * 0.5f}};
        }

        constexpr const char* MARKER_MESH = "cube2";
        constexpr const char* MARKER_MODEL = "city_marker";
        constexpr const char* MARKER_MATERIAL = "DefaultMaterial";

        void createMarkers(SystemContext& context)
        {
            CommandBuffer& buffer = context.getCommandBuffer();
            const AssetHandle mesh{InternedString(MARKER_MESH), AssetType::MESH};
             
            ServiceLocator& services = context.services();
            const bool baked = services.contains<ModelSpawner>() &&
                services.get<ModelSpawner>().hasModel(InternedString(MARKER_MODEL));
            const AssetHandle material{InternedString(baked ? catalog::MARKER_MATERIAL : MARKER_MATERIAL),
                                       AssetType::MATERIAL};
            for (u32 slot = 0; slot < MARKER_COUNT; ++slot)
            {
                const Entity entity = buffer.createEntity();
                buffer.addComponent<TransformComponent>(
                    entity, TransformComponent{.position = {0.0f, MARKER_PARK_Y, 0.0f}, .scale = Vec3(0.1f)});
                buffer.addComponent<TransformMatrixComponent>(entity);
                buffer.addComponent<MeshComponent>(entity, MeshComponent{.meshHandle = mesh});
                buffer.addComponent<MaterialComponent>(
                    entity,
                    MaterialComponent{.materialHandle = slot >= MARKER_CORNERS
                                          ? AssetHandle{InternedString(MARKER_MATERIAL), AssetType::MATERIAL}
                                          : material});
                buffer.addComponent<RenderTintComponent>(entity, RenderTintComponent{.color = Vec4(1.0f)});
                buffer.addComponent<CityMarkerComponent>(entity, CityMarkerComponent{.slot = slot});
            }
        }

         
        void placeMarkers(SystemContext& context, const std::vector<Marker>& wanted)
        {
            context
                .query<ecs::Read<CityMarkerComponent>, ecs::Write<TransformComponent>,
                       ecs::Write<RenderTintComponent>>()
                ->forEachChunk(
                    [&](Chunk* chunk)
                    {
                        const CityMarkerComponent* markers = chunk->readColumn<CityMarkerComponent>();
                        const TransformComponent* transforms = chunk->readColumn<TransformComponent>();
                        const RenderTintComponent* tints = chunk->readColumn<RenderTintComponent>();
                        for (sizet row = 0; row < chunk->size(); ++row)
                        {
                            const u32 slot = markers[row].slot;
                            const Marker& want = slot < wanted.size() ? wanted[slot] : Marker{};
                            const TransformComponent place = want.shown
                                ? want.transform
                                : TransformComponent{.position = {0.0f, MARKER_PARK_Y, 0.0f}, .scale = Vec3(0.1f)};
                            const TransformComponent& now = transforms[row];
                            if (!(now.position == place.position) || !(now.scale == place.scale) ||
                                now.rotation.x != place.rotation.x || now.rotation.y != place.rotation.y ||
                                now.rotation.z != place.rotation.z || now.rotation.w != place.rotation.w)
                            {
                                chunk->mutableColumn<TransformComponent>()[row] = place;
                                chunk->markModified<TransformComponent>(row);
                            }
                            if (want.shown && !(tints[row].color == want.color))
                            {
                                chunk->mutableColumn<RenderTintComponent>()[row].color = want.color;
                                chunk->markModified<RenderTintComponent>(row);
                            }
                        }
                    });
        }

         
        void runTools(SystemContext& context)
        {
            ServiceLocator& services = context.services();
            if (!services.contains<CityPlay>())
            {
                return;
            }
            WriteService<CityPlay> service = services.write<CityPlay>();
            CityPlay& play = *service;
            const std::optional<CityStateComponent> city = readCity(context);
            const std::shared_ptr<const CityMap> map =
                services.contains<PathService>() ? services.read<PathService>()->map() : nullptr;

             
            std::optional<GameViewPointer> view;
            std::optional<Vec3> ground;
            if (play.input)
            {
                view = gameViewPointer(services, *play.input);
            }
            if (view && view->inside && context.isEntityValid(view->camera) &&
                context.hasComponent<TransformComponent>(view->camera) &&
                context.hasComponent<CameraComponent>(view->camera))
            {
                ground = groundHit(context.getConstComponent<TransformComponent>(view->camera),
                                   context.getConstComponent<CameraComponent>(view->camera), view->aspect, view->ndc);
            }
            play.pointerInside = view && view->inside;
            play.pointerNdc = view ? view->ndc : Vec2{};
            play.pointerGround = ground;
            const bool click = play.down(ACT_CLICK);
            const bool clickStarted = play.pressed(ACT_CLICK);
            play.clicks += clickStarted ? 1 : 0;
            if (play.pressed(ACT_CANCEL) && play.selection.kind != KIND_NONE)
                play.issue(SELECT_COMMAND, {});
            if (city && !play.captured)
            {
                if (view)
                {
                    if (clickStarted && view && view->inside)
                    {
                        play.pressing = true;
                        play.pressNdc = view->ndc;
                    }
                    if (play.pressing && !click)
                    {
                        play.pressing = false;
                        const bool still = view && view->inside &&
                            std::fabs(view->ndc.x - play.pressNdc.x) < CLICK_NDC &&
                            std::fabs(view->ndc.y - play.pressNdc.y) < CLICK_NDC;
                        if (!still)
                        {
                            play.pickNote = "moved";
                        }
                        else if (!play.picker || play.ticket != 0)
                        {
                            play.pickNote = "no picker";
                        }
                        else
                        {
                            play.ticket = play.picker->request(services, view->ndc);
                            play.pickNote = play.ticket != 0 ? "pending" : "refused";
                        }
                    }
                }
            }
            if (play.ticket != 0 && play.picker)
            {
                if (const std::optional<GamePickResult> picked = play.picker->take(play.ticket))
                {
                    play.ticket = 0;
                    ++play.picks;
                    play.pickNote = picked->hit ? "hit" : "miss";
                    if (!picked->hit)
                    {
                        play.issue(SELECT_COMMAND, {});
                    }
                    else
                    {
                        play.issue(SELECT_COMMAND,
                                   std::format(R"("id":{},"x":{:.2f},"z":{:.2f})", picked->entity.id(),
                                               picked->worldPosition.x, picked->worldPosition.z));
                    }
                }
            }

             
            const bool selected = play.selection.kind != KIND_NONE;
            if (!selected && play.markersParked)
            {
                return;
            }
            if (!play.markersCreated)
            {
                bool any = false;
                context.query<ecs::Read<CityMarkerComponent>>()->forEachChunk([&](Chunk* chunk)
                                                                              { any = any || chunk->size() > 0; });
                if (!any)
                {
                    createMarkers(context);
                }
                play.markersCreated = true;
                return;  
            }
            std::vector<Marker> wanted(MARKER_COUNT);
            if (selected && city)
            {
                const Inspection inspection = inspect(play.world, play.selection, false);
                 
                const float grow = std::max(1.0f, play.rig.distance / MARKER_GROW_DISTANCE);
                if (play.cornerRevision != play.selectionRevision)
                {
                    play.cornerRevision = play.selectionRevision;
                    play.cornerAge = 0.0f;
                }
                play.cornerAge += context.deltaTime;
                Entity visual = play.selection.entity;
                if (play.selection.kind == KIND_PERSON && services.contains<CityAvatarPool>())
                {
                    const auto pool = services.read<CityAvatarPool>();
                    if (play.selection.id < pool->slotOf.size())
                    {
                        const u32 slot = pool->slotOf[play.selection.id];
                        if (slot < pool->avatars.size() && slot < pool->pedestrianOf.size() &&
                            pool->pedestrianOf[slot] == play.selection.id &&
                            play.world.isEntityAlive(pool->avatars[slot]))
                            visual = pool->avatars[slot];
                    }
                }
                std::vector<Entity> parts;
                if (play.world.isEntityAlive(visual))
                {
                    play.world.services().get<EntityTransformManager>().collectSubtree(visual, parts);
                    parts.push_back(visual);
                }
                Vec3 lo(1.0e20f), hi(-1.0e20f);
                bool bounded = false;
                for (const Entity entity : parts)
                {
                    if (const auto* bounds = play.world.tryGetComponent<WorldBoundingBoxComponent>(entity))
                    {
                        for (u32 axis = 0; axis < 3; ++axis)
                        {
                            lo[axis] = std::min(lo[axis], bounds->min[axis]);
                            hi[axis] = std::max(hi[axis], bounds->max[axis]);
                        }
                        bounded = true;
                    }
                }
                if (bounded)
                {
                    const float growth = 1.0f - std::exp(-play.cornerAge * 12.0f);
                    const float thickness = std::max(0.12f, play.rig.distance * 0.0015f);
                    lo = lo - Vec3(thickness * 2.0f);
                    hi = hi + Vec3(thickness * 2.0f);
                    for (u32 corner = 0; corner < 8; ++corner)
                    {
                        for (u32 axis = 0; axis < 3; ++axis)
                        {
                            Vec3 at((corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y, (corner & 4) ? hi.z : lo.z);
                            Vec3 half(thickness);
                            const float segment =
                                std::max(thickness * 2.0f, (hi[axis] - lo[axis]) * (0.04f + 0.18f * growth));
                            at[axis] += ((corner & (1u << axis)) ? -1.0f : 1.0f) * segment * 0.5f;
                            half[axis] = segment * 0.5f;
                            Marker& bracket = wanted[MARKER_CORNERS + corner * 3 + axis];
                            bracket.shown = true;
                            bracket.transform.position = at;
                            bracket.transform.scale = half;
                            bracket.color = Vec4(1.0f, 0.3f, 0.18f, 1.0f);
                        }
                    }
                }
                const bool person = inspection.kind == KIND_PERSON;
                const bool flight = inspection.kind == KIND_DRONE;
                const float y = person ? SIDEWALK_HEIGHT + 0.35f : 0.35f;
                for (sizet i = 0; i + 1 < inspection.route.size() && MARKER_ROUTE + i < MARKER_CORNERS; ++i)
                {
                    const Vec3& a = inspection.route[i];
                    const Vec3& b = inspection.route[i + 1];
                    const float width = (person ? 0.5f : flight ? 0.25f : 0.9f) * grow;
                     
                    const float below = flight ? FLIGHT_LINE_DROP * grow : 0.0f;
                    TransformComponent segment =
                        segmentBox(a, b, width, flight ? width : 0.15f, flight ? a.y - below : y);
                    if (flight && lengthXZ(a, b) < 0.5f)
                    {
                         
                        segment = TransformComponent{
                            .position = {a.x, (a.y + b.y) * 0.5f - below, a.z},
                            .scale = {width * 0.5f, std::fabs(b.y - a.y) * 0.5f + 0.1f, width * 0.5f}};
                    }
                    wanted[MARKER_ROUTE + i] = {.shown = true,
                                                .transform = segment,
                                                .color = flight ? Vec4(1.0f, 0.45f, 0.9f, 1.0f)
                                                                : Vec4(0.1f, 1.0f, 0.85f, 1.0f)};
                }
            }
            placeMarkers(context, wanted);
            play.markersParked = !selected;
        }

         

        constexpr u32 HUD_REFRESH_FRAMES = 10;
        constexpr u32 TRAFFIC_REFRESH_FRAMES = 30;  
        constexpr u32 INSPECT_LINES = 24;
        constexpr u32 SPEEDS[] = {0, 1, 2, 4, 8};

        constexpr auto HUD_COMPOSITION = "assets/ui/city/hud.composition.json";

        struct CityHud
        {
            struct View
            {
                std::unique_ptr<UiDocumentView> document;
                bool fillWidth;
            };

            CityHud(UiContext& context, UiViewContext* viewContext, CityPlay& play) :
                ui(&context), view(viewContext), scope(context)
            {
                auto composition = readUiComposition(resolveContentPath(HUD_COMPOSITION));
                if (!composition)
                {
                    SWARNING_LOG("CityShowcase: cannot read HUD composition: {}", composition.error().message.c_str());
                    return;
                }
                if (view)
                {
                    constexpr std::pair<std::string_view, std::string_view> fonts[] = {
                        {"JetBrainsMono", "JetBrainsMonoRegularFont.ttf"},
                        {"ChakraPetch", "ChakraPetch-Bold.ttf"},
                        {"Geist", "Geist-Regular.ttf"},
                        {"GeistMono", "GeistMono-Regular.ttf"},
                        {"GeistMonoMedium", "GeistMono-Medium.ttf"}};
                    for (const auto& [name, file] : fonts)
                    {
                        if (const auto font = view->registerFont(
                                InternedString(name), resolveContentPath(std::format("assets/fonts/{}", file)));
                            !font)
                        {
                            SWARNING_LOG("CityShowcase: HUD font failed: {}", font.error().message.c_str());
                        }
                    }
                    styles = std::make_unique<UiStyleSheet>();
                    auto& sheet = *styles;
                    if (const auto loaded = sheet.load(resolveContentPath(composition->baseStyle)); !loaded)
                    {
                        SWARNING_LOG("CityShowcase: the base stylesheet failed: {}", loaded.error().message.c_str());
                    }
                    if (!composition->theme.empty())
                    {
                        const auto theme = resolveContentPath(composition->theme);
                        if (const auto themed = sheet.setTheme("city", std::span(&theme, 1)); !themed)
                        {
                            SWARNING_LOG("CityShowcase: HUD theme failed: {}", themed.error().message.c_str());
                        }
                    }
                    if (!composition->style.empty())
                    {
                        if (const auto loaded = sheet.loadLayer("city", resolveContentPath(composition->style));
                            !loaded)
                        {
                            SWARNING_LOG("CityShowcase: the HUD stylesheet failed: {}", loaded.error().message.c_str());
                        }
                    }
                    sheet.watch(true);
                }

                screen =
                    scope.createPanel("city.screen", UiRectDesc{.size = composition->screen, .pivot = {0.0f, 0.0f}});
                if (view && styles)
                    view->setSubtreeStylesheet(screen.entity(), styles.get());
                screen.setInteractable(false);
                screen.setVisible(false);
                std::unordered_map<std::string, UiViewContract> contracts;
                auto& barContract = contracts["city.bar"];
                barContract.require("city.bar", UiWidgetKind::Panel,
                                    [](const Widget& widget) { widget.as<Panel>().setAccessibleName("City status"); });
                barContract.require("city.bar.clock", UiWidgetKind::Label,
                                    [this](const Widget& widget)
                                    {
                                        clock = widget.as<Label>();
                                        shownClock.clear();
                                    });
                barContract.require("city.bar.counts", UiWidgetKind::Label,
                                    [this](const Widget& widget)
                                    {
                                        counts = widget.as<Label>();
                                        shownCounts.clear();
                                    });
                barContract.require("city.bar.logistics", UiWidgetKind::Label,
                                    [this](const Widget& widget)
                                    {
                                        logistics = widget.as<Label>();
                                        shownLogistics.clear();
                                    });
                barContract.require("city.bar.traffic", UiWidgetKind::Label,
                                    [this](const Widget& widget)
                                    {
                                        traffic = widget.as<Label>();
                                        shownTraffic.clear();
                                    });
                auto& inspect = contracts["city.inspect"];
                inspect.require("city.inspect", UiWidgetKind::Panel,
                                [this](const Widget& widget)
                                {
                                    panel = widget.as<Panel>();
                                    panel.setAccessibleName("Inspect");
                                    panel.setVisible(false);
                                    shownSelection = 0xFFFFFFFFFFFFFFFFull;
                                });
                inspect.require("city.inspect.title", UiWidgetKind::Label,
                                [this](const Widget& widget)
                                {
                                    title = widget.as<Label>();
                                    shownTitle.clear();
                                });
                lines.resize(INSPECT_LINES);
                for (u32 i = 0; i < INSPECT_LINES; ++i)
                {
                    inspect.require(InternedString(std::format("city.inspect.line{}", i)), UiWidgetKind::Label,
                                    [this, i](const Widget& widget)
                                    {
                                        lines[i] = widget.as<Label>();
                                        shownLines[i].clear();
                                        shownSelection = 0xFFFFFFFFFFFFFFFFull;
                                    });
                }
                for (const auto& action : composition->actions)
                {
                    if (!bindAction(contracts[action.view], action, play))
                    {
                        SWARNING_LOG("CityShowcase: unsupported HUD binding '{}'", action.id.c_str());
                        return;
                    }
                }

                std::unordered_map<std::string, Panel> mounts;
                std::vector<Entity> parents;
                parents.reserve(composition->views.size());
                for (const auto& entry : composition->views)
                {
                    Entity parent = screen.entity();
                    if (!entry.parent.empty())
                    {
                        const std::string key = entry.parent + "/" + entry.slot;
                        auto [mount, added] = mounts.try_emplace(key);
                        if (added)
                        {
                            UiRectDesc desc;
                            desc.pivot = {0.0f, 0.0f};
                            desc.contentSize = UiContentSizeComponent{};
                            desc.layout =
                                UiLayoutComponent{.direction = UiLayoutDirection::Row, .align = UiLayoutAlign::Center};
                            mount->second = scope.createPanel("city.hud.mount", desc);
                            contracts[entry.parent].slot(InternedString(entry.slot),
                                                         [host = mount->second](const Widget& slot) mutable
                                                         { host.setParent(slot); });
                        }
                        parent = mount->second.entity();
                    }
                    parents.push_back(parent);
                }
                for (sizet index = 0; index < composition->views.size(); ++index)
                {
                    const auto& entry = composition->views[index];
                    UiViewContract& contract = contracts[entry.name];
                    const auto protectedIds = composition->protectedIds(entry.name);
                    for (const auto& element : contract.elements())
                    {
                        if (element.required && !std::ranges::contains(protectedIds, element.id.toView()))
                        {
                            SWARNING_LOG("CityShowcase: HUD composition does not protect required widget '{}'",
                                         element.id.toView());
                            return;
                        }
                    }
                    auto document =
                        std::make_unique<UiDocumentView>(context, entry.name, std::move(contract), parents[index]);
                    if (!document->load(resolveContentPath(entry.path)))
                    {
                        SWARNING_LOG("CityShowcase: failed to load HUD view '{}'", entry.name.c_str());
                        return;
                    }
                    documents.push_back({std::move(document), entry.fillWidth});
                }
                ready = clock.isValid() && counts.isValid() && logistics.isValid() && traffic.isValid() &&
                    panel.isValid() && title.isValid();
                screen.setVisible(ready);
                scope.commit();
            }

            bool bindAction(UiViewContract& contract, const UiCompositionAction& action, CityPlay& play)
            {
                if (action.event != "click")
                {
                    return false;
                }
                const InternedString id(action.id);
                const std::string args = action.args.substr(1, action.args.size() - 2);
                JsonFragmentReader reader(action.args);
                if (action.command == SPEED_COMMAND)
                {
                    u32 speed = 0;
                    if (reader.readU32("speed", speed) != FieldRead::Ok)
                    {
                        return false;
                    }
                    const auto at = std::ranges::find(SPEEDS, speed);
                    if (at == std::end(SPEEDS))
                    {
                        return false;
                    }
                    const sizet index = static_cast<sizet>(at - std::begin(SPEEDS));
                    contract.require(id, UiWidgetKind::Button,
                                     [this, &play, index, speed, args](const Widget& widget)
                                     {
                                         speedButtons[index] = widget.as<Button>();
                                         speedButtons[index].setAccessibleName(
                                             speed == 0 ? "Pause" : std::format("Speed {}x", speed));
                                         speedButtons[index].onClick([&play, args]
                                                                     { play.issue(SPEED_COMMAND, args); });
                                         shownSpeed = 0xFFFFFFFFu;
                                     });
                }
                else if (action.command == CAMERA_COMMAND && args.empty())
                {
                    contract.require(id, UiWidgetKind::Button,
                                     [this, &play](const Widget& widget)
                                     {
                                         followButton = widget.as<Button>();
                                         shownFollow.reset();
                                         followButton.onClick(
                                             [&play]
                                             {
                                                 const bool following =
                                                     play.follow.isValid() && play.follow == play.selection.entity;
                                                 play.issue(CAMERA_COMMAND,
                                                            std::format(R"("follow":{})",
                                                                        following ? 0 : play.selection.entity.id()));
                                             });
                                     });
                }
                else if (action.command == GENERATE_COMMAND && args.empty())
                {
                    contract.require(id, UiWidgetKind::Button,
                                     [&play](const Widget& widget)
                                     {
                                         widget.as<Button>().onClick(
                                             [&play]
                                             {
                                                 const CityStateComponent* city = findCity(play.world);
                                                 play.issue(GENERATE_COMMAND,
                                                            std::format(R"("seed":{})", city ? city->seed + 1 : 1),
                                                            "generated a new city");
                                             });
                                     });
                }
                else if (action.command == SAVE_COMMAND || action.command == LOAD_COMMAND ||
                         action.command == SELECT_COMMAND)
                {
                    std::string name, done;
                    if (reader.readString("name", name) == FieldRead::Ok)
                    {
                        done = std::format("{} '{}'", action.command == SAVE_COMMAND ? "saved as" : "loaded", name);
                    }
                    contract.require(id, UiWidgetKind::Button,
                                     [&play, command = action.command, args, done](const Widget& widget)
                                     {
                                         widget.as<Button>().onClick([&play, command, args, done]
                                                                     { play.issue(command, args, done); });
                                     });
                }
                else
                {
                    return false;
                }
                return true;
            }
            CityHud(const CityHud&) = delete;
            CityHud& operator=(const CityHud&) = delete;
            ~CityHud()
            {
                if (view && screen.isValid())
                    view->setSubtreeStylesheet(screen.entity(), nullptr);
            }

            static void show(Label& label, std::string& shown, std::string text)
            {
                if (text != shown)
                {
                    label.setText(text);
                    shown = std::move(text);
                }
            }

            static void mark(Button& button, const bool active)
            {
                if (!button.isValid())
                {
                    return;
                }
                button.setStyleClass(active ? "city.button.active" : "city.button");
                button.setAccessibleSelected(active);
            }

            UiContext* ui = nullptr;
            UiViewContext* view = nullptr;
            std::unique_ptr<UiStyleSheet> styles;
            UiScope scope;
            std::vector<View> documents;
            Panel screen;
            Panel panel;
            Label clock;
            Label counts;
            Label logistics;
            Label traffic;
            Label title;
            std::vector<Label> lines;
            std::vector<std::string> shownLines = std::vector<std::string>(INSPECT_LINES);
            Button speedButtons[std::size(SPEEDS)];
            Button followButton;
            std::string shownClock, shownCounts, shownLogistics, shownTraffic, shownTitle;
            Vec2 screenSize{0.0f};
            u32 shownSpeed = 0xFFFFFFFFu;
            u64 shownSelection = 0xFFFFFFFFFFFFFFFFull;
            std::optional<bool> shownFollow;
            u32 frames = 0;
            bool ready = false;
        };

        std::string grouped(const u32 value)
        {
            std::string digits = std::to_string(value);
            for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3)
            {
                digits.insert(static_cast<sizet>(at), ",");
            }
            return digits;
        }

         
         
         
        void updateHud(SystemContext& context)
        {
            ServiceLocator& services = context.services();
            if (!services.contains<CityHud>() || !services.contains<CityPlay>())
            {
                return;
            }
            WriteService<CityHud> hudService = services.write<CityHud>();
            CityHud& hud = *hudService;
            if (!hud.ready)
            {
                return;
            }
            WriteService<CityPlay> playService = services.write<CityPlay>();
            CityPlay& play = *playService;
            if (hud.view)
            {
                if (hud.styles)
                    hud.styles->pollWatch();
                const Vec2 size = hud.view->frame().targetSize / hud.ui->hierarchy().layoutScale();
                if (size != hud.screenSize && size.x > 0.0f && size.y > 0.0f)
                {
                    hud.screenSize = size;
                    hud.screen.setSize(size);
                }
            }
            for (auto& entry : hud.documents)
            {
                if (entry.fillWidth)
                {
                    auto& document = *entry.document;
                    const auto& hierarchy = hud.ui->hierarchy();
                    const Entity parent = hierarchy.parentOf(document.root());
                    const float width = parent == hud.screen.entity()
                        ? hud.screen.size().x
                        : hierarchy.absoluteRectOf(parent).size.x / hierarchy.layoutScale();
                    Widget root(&document.scope(), document.root(), document.scope().generation());
                    if (width > 0.0f && root.size().x != width)
                    {
                        root.setSize({width, root.size().y});
                    }
                }
            }
            const u32 frame = hud.frames++;
            const bool selectionChanged = hud.shownSelection != play.selectionRevision;
            if (frame % HUD_REFRESH_FRAMES != 0 && !selectionChanged)
            {
                return;
            }
            const std::optional<CityStateComponent> city = readCity(context);
            if (!city)
            {
                return;
            }
            const u32 day = city->step / std::max(city->daySteps, 1u) + 1;
            CityHud::show(hud.clock, hud.shownClock,
                          std::format("Day {}  {}", day, clockText(timeOfDay(*city, city->step))));
            if (city->speed != hud.shownSpeed)
            {
                hud.shownSpeed = city->speed;
                for (sizet i = 0; i < std::size(SPEEDS); ++i)
                {
                    CityHud::mark(hud.speedButtons[i], SPEEDS[i] == city->speed);
                }
            }
            LogisticsStats logistics;
            if (services.contains<LogisticsService>())
            {
                logistics = services.read<LogisticsService>()->stats();
            }
            u32 inTransit = 0;
            for (u32 kind = 0; kind < PACKAGE_KINDS; ++kind)
            {
                inTransit += logistics.counts[kind][PACKAGE_IN_TRANSIT];
            }
            CityHud::show(
                hud.counts, hud.shownCounts,
                std::format("People {}  Cars {}", grouped(city->pedestrians), grouped(city->cars - city->trucks)));
             
             
            CityHud::show(hud.logistics, hud.shownLogistics,
                          std::format("Trucks {}/{}  Drones {}/{}  En route {}  Delivered {}",
                                      grouped(logistics.trucksOut), grouped(city->trucks),
                                      grouped(logistics.dronesFlying), grouped(logistics.drones), grouped(inTransit),
                                      grouped(logistics.deliveredToday)));
            const std::shared_ptr<const CityMap> map =
                services.contains<PathService>() ? services.read<PathService>()->map() : nullptr;
            if (map && frame % TRAFFIC_REFRESH_FRAMES == 0)
            {
                RoadLoadCounter counter(map->graph());
                context.query<ecs::Read<CityCarComponent>>()->forEachChunk(
                    [&](Chunk* chunk)
                    {
                        const CityCarComponent* cars = chunk->readColumn<CityCarComponent>();
                        for (sizet row = 0; row < chunk->size(); ++row)
                        {
                            counter.add(cars[row]);
                        }
                    });
                const RoadLoad load = counter.load();
                const double jammed =
                    load.directions() > 0 ? 100.0 * load.levels[CONGESTION_LEVELS - 1] / load.directions() : 0.0;
                CityHud::show(hud.traffic, hud.shownTraffic,
                              std::format("Roads {:.1f}% full  jammed {:.0f}%", load.occupancy(), jammed));
            }
            const bool following = play.follow.isValid() && play.follow == play.selection.entity;
            if (following != hud.shownFollow)
            {
                hud.shownFollow = following;
                CityHud::mark(hud.followButton, following);
            }
            hud.shownSelection = play.selectionRevision;
            const bool selected = play.selection.kind != KIND_NONE;
            if (selected != hud.panel.isVisible())
            {
                hud.panel.setVisible(selected);
            }
            const bool moving = play.selection.kind == KIND_CAR || play.selection.kind == KIND_PERSON ||
                play.selection.kind == KIND_DRONE;
            if (!selected || (!selectionChanged && !moving && frame % (HUD_REFRESH_FRAMES * 6) != 0))
            {
                return;
            }
            const Inspection inspection = inspect(play.world, play.selection);
            CityHud::show(hud.title, hud.shownTitle, inspection.title);
            const std::vector<std::string> text = inspectionLines(inspection);
            for (u32 i = 0; i < INSPECT_LINES; ++i)
            {
                const bool used = i < text.size();
                CityHud::show(hud.lines[i], hud.shownLines[i], used ? text[i] : std::string());
                if (used != hud.lines[i].isVisible())
                {
                    hud.lines[i].setVisible(used);
                }
            }
            const bool followable = moving && inspection.entity.isValid();
            if (followable != hud.followButton.isVisible())
            {
                hud.followButton.setVisible(followable);
            }
        }

         

        std::optional<double> number(const ControlArgs& args, const char* key)
        {
            const std::optional<std::string> text = args.json(key);
            if (!text)
            {
                return std::nullopt;
            }
            try
            {
                std::string value = *text;
                std::erase(value, '"');
                return std::stod(value);
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

         
        std::string playOf(ControlServer& control, const ControlArgs& args, const World*& world)
        {
            world = statusWorld(control, args);
            if (!world)
            {
                return control.unknownWorld(args);
            }
            if (!findCity(*world))
            {
                return NO_CITY;
            }
            if (!world->services().contains<CityPlay>())
            {
                return "the world has no play camera or tools: they run in the play world";
            }
            return {};
        }

         
        void readyRig(const World world, CityPlay& play)
        {
            if (play.rigReady)
            {
                return;
            }
            eachRow<CityCameraComponent>(world,
                                         [&](const CityCameraComponent&, const Entity entity)
                                         {
                                             if (!play.rigReady && world.hasComponent<TransformComponent>(entity))
                                             {
                                                 play.camera = entity;
                                                 play.rig = play.target =
                                                     rigOf(world.getComponent<TransformComponent>(entity));
                                                 play.rigReady = true;
                                             }
                                         });
        }

        void writeVec3(IFormatWriter& out, const char* name, const Vec3& v)
        {
            out.beginObject(name);
            out.writeF32("x", v.x);
            out.writeF32("y", v.y);
            out.writeF32("z", v.z);
            out.endObject();
        }

        void writeCamera(const World world, const CityPlay& play, IFormatWriter& out)
        {
            out.writeString("mode", play.mode == MODE_FLY ? "fly" : play.follow.isValid() ? "follow" : "orbit");
            out.writeEntity("camera", play.camera);
            if (play.camera.isValid() && world.isEntityAlive(play.camera))
                if (const auto* lens = world.tryGetComponent<CameraComponent>(play.camera))
                    out.writeF32("farPlane", lens->farPlane);
            writeVec3(out, "focus", play.rig.focus);
            out.writeF32("yaw", play.rig.yaw);
            out.writeF32("tilt", play.rig.tilt);
            out.writeF32("distance", play.rig.distance);
            writeVec3(out, "position",
                      play.camera.isValid() && world.isEntityAlive(play.camera) &&
                              world.hasComponent<TransformComponent>(play.camera)
                          ? world.getComponent<TransformComponent>(play.camera).position
                          : rigPosition(play.rig));
            out.beginObject("target");
            writeVec3(out, "focus", play.target.focus);
            out.writeF32("yaw", play.target.yaw);
            out.writeF32("tilt", play.target.tilt);
            out.writeF32("distance", play.target.distance);
            out.endObject();
            out.writeEntity("follow", play.follow);
            out.writeBool("edgePan", play.edgePan);
            out.beginObject("pointer");
            out.writeBool("inside", play.pointerInside);
            out.writeF32("ndcX", play.pointerNdc.x);
            out.writeF32("ndcY", play.pointerNdc.y);
            if (play.pointerGround)
            {
                writeVec3(out, "ground", *play.pointerGround);
            }
            out.writeBool("picking", play.ticket != 0);
            out.writeU32("clicks", play.clicks);
            out.writeU32("picks", play.picks);
            out.writeString("lastPick", play.pickNote);
            out.endObject();
        }

        std::string speedCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
        {
            const World* world = statusWorld(control, args);
            if (!world)
            {
                return control.unknownWorld(args);
            }
            const std::optional<u64> speed = args.integer("speed");
            if (!speed || std::ranges::find(SPEEDS, static_cast<u32>(*speed)) == std::end(SPEEDS))
            {
                return "speed must be 0, 1, 2, 4 or 8";
            }
            for (Chunk* chunk : world->getChunksContaining(Aspect(getComponentId<CityStateComponent>())))
            {
                if (!chunk->empty())
                {
                    const u32 before = chunk->readColumn<CityStateComponent>()[0].speed;
                    world->getComponent<CityStateComponent>(chunk->entities()[0]).speed = static_cast<u32>(*speed);
                    out.writeU32("speed", static_cast<u32>(*speed));
                    out.writeU32("previous", before);
                    return {};
                }
            }
            return NO_CITY;
        }

        std::string cameraQuery(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
        {
            const World* world = nullptr;
            if (std::string error = playOf(control, args, world); !error.empty())
            {
                return error;
            }
            WriteService<CityPlay> play = world->services().write<CityPlay>();
            readyRig(*world, *play);
            writeCamera(*world, *play, out);
            return {};
        }

        std::string cameraCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
        {
            const World* world = nullptr;
            if (std::string error = playOf(control, args, world); !error.empty())
            {
                return error;
            }
            WriteService<CityPlay> service = world->services().write<CityPlay>();
            CityPlay& play = *service;
            readyRig(*world, play);
            const std::optional<double> x = number(args, "x");
            const std::optional<double> z = number(args, "z");
            const std::optional<double> distance = number(args, "distance");
            const std::optional<double> yaw = number(args, "yaw");
            const std::optional<double> tilt = number(args, "tilt");
            if (x.has_value() != z.has_value())
            {
                return "x and z go together";
            }
            if (distance && (*distance < MIN_DISTANCE || *distance > MAX_DISTANCE))
            {
                return std::format("distance must be {}..{} m", MIN_DISTANCE, MAX_DISTANCE);
            }
            if (tilt && (*tilt < MIN_TILT || *tilt > MAX_TILT))
            {
                return std::format("tilt must be {}..{} rad", MIN_TILT, MAX_TILT);
            }
            if (const std::optional<std::string> mode = args.string("mode"))
            {
                if (*mode != "orbit" && *mode != "fly")
                {
                    return "mode must be orbit or fly";
                }
                play.mode = *mode == "fly" ? MODE_FLY : MODE_ORBIT;
            }
            if (args.json("follow"))
            {
                const std::optional<u64> follow = args.integer("follow");
                const Entity entity(follow.value_or(0));
                if (entity.isValid() && !world->isEntityAlive(entity))
                {
                    return std::format("follow: entity {} is not alive", entity.id());
                }
                play.follow = entity.isValid() ? entity : Entity::undefined();
            }
            if (x)
            {
                play.target.focus = Vec3(static_cast<float>(*x), 0.0f, static_cast<float>(*z));
                play.follow = Entity::undefined();
            }
            if (distance)
            {
                play.target.distance = static_cast<float>(*distance);
            }
            if (yaw)
            {
                play.target.yaw = static_cast<float>(*yaw);
            }
            if (tilt)
            {
                play.target.tilt = static_cast<float>(*tilt);
            }
            if (const std::optional<bool> edgePan = args.boolean("edgePan"))
            {
                play.edgePan = *edgePan;
            }
            if (args.boolean("instant").value_or(false))
            {
                play.rig = play.target;
            }
            writeCamera(*world, play, out);
            return {};
        }

         
        std::string selectionOf(const World world, const ControlArgs& args, Selection& selection)
        {
            const std::optional<double> x = number(args, "x");
            const std::optional<double> z = number(args, "z");
            if (x.has_value() != z.has_value())
            {
                return "x and z go together";
            }
            const Entity entity(args.json("id") ? args.integer("id").value_or(0) : 0);
            if (!entity.isValid() && !x)
            {
                selection = {};
                return {};
            }
            if (entity.isValid() && !world.isEntityAlive(entity))
            {
                return std::format("entity {} is not alive", entity.id());
            }
            const CityStateComponent* city = findCity(world);
            if (!city)
            {
                return NO_CITY;
            }
            const std::shared_ptr<const CityMap> map = worldMap(world, *city);
            const std::optional<Vec3> at =
                x ? std::optional<Vec3>(Vec3(static_cast<float>(*x), 0.0f, static_cast<float>(*z))) : std::nullopt;
            selection = resolve(world, map.get(), entity, at);
            return {};
        }

        std::string selectCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
        {
            const World* world = nullptr;
            if (std::string error = playOf(control, args, world); !error.empty())
            {
                return error;
            }
            Selection selection;
            if (std::string error = selectionOf(*world, args, selection); !error.empty())
            {
                return error;
            }
            {
                WriteService<CityPlay> play = world->services().write<CityPlay>();
                play->selection = selection;
                ++play->selectionRevision;
            }
            writeInspection(inspect(*world, selection), out);
            return {};
        }

        std::string inspectQuery(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
        {
            const World* world = statusWorld(control, args);
            if (!world)
            {
                return control.unknownWorld(args);
            }
            if (!findCity(*world))
            {
                return NO_CITY;
            }
            Selection selection;
            if (args.json("id") || args.json("x"))
            {
                if (std::string error = selectionOf(*world, args, selection); !error.empty())
                {
                    return error;
                }
            }
            else if (world->services().contains<CityPlay>())
            {
                selection = world->services().read<CityPlay>()->selection;
            }
            writeInspection(inspect(*world, selection), out);
            return {};
        }
    }  

    Entity activeCityCamera(SystemContext& context)
    {
        std::optional<GameViewPointer> view;
        const auto play = context.services().read<CityPlay>();
        return playCamera(context, *play, view);
    }

    void registerPlay(const ModuleContext& context)
    {
        ControlServer& control = context.control;
        const auto bind = [&control](std::string (*fn)(ControlServer&, const ControlArgs&, IFormatWriter&))
        { return [&control, fn](const ControlArgs& args, IFormatWriter& out) { return fn(control, args, out); }; };
        control.registerQuery("cityUiActions", context.owner.toView(), "Declared game UI actions", {},
                              [](const ControlArgs&, IFormatWriter& out)
                              {
                                  const auto composition = readUiComposition(resolveContentPath(HUD_COMPOSITION));
                                  if (!composition)
                                  {
                                      return std::string(composition.error().message);
                                  }
                                  out.beginArray("actions", static_cast<u32>(composition->actions.size()));
                                  for (const auto& action : composition->actions)
                                  {
                                      out.beginObject({});
                                      out.writeString("view", action.view);
                                      out.writeString("id", action.id);
                                      out.writeString("event", action.event);
                                      out.writeString("command", action.command);
                                      out.beginObject("args");
                                      JsonFragmentReader reader(action.args);
                                      for (const auto field : {"tool", "class", "zone", "name"})
                                      {
                                          std::string value;
                                          if (reader.readString(field, value) == FieldRead::Ok)
                                          {
                                              out.writeString(field, value);
                                          }
                                      }
                                      u32 speed = 0;
                                      if (reader.readU32("speed", speed) == FieldRead::Ok)
                                      {
                                          out.writeU32("speed", speed);
                                      }
                                      out.endObject();
                                      out.endObject();
                                  }
                                  out.endArray();
                                  return std::string{};
                              });
        control.registerCommand(
            SPEED_COMMAND, context.owner.toView(),
            "sets the city's simulation speed: city steps per fixed step (0 pauses the city and its people's "
            "animations; the camera, tools and HUD keep running); replies the speed and the previous one. The HUD's "
            "speed buttons run it",
            {requiredArg("speed", ArgKind::Integer, "0, 1, 2, 4 or 8"), WORLD_ARG}, bind(&speedCommand));
        control.registerQuery(
            CAMERA_QUERY, context.owner.toView(),
            "the play camera: mode (orbit, follow or fly), the camera entity, the orbit rig (focus on the ground, yaw "
            "and tilt in rad, distance in m) and the target it is damped toward, the camera position, the entity it "
            "follows, whether the screen edges pan, and the pointer as the tools saw it (inside the view, ndc, the "
            "ground point under it, left clicks seen, picks answered and lastPick: hit, miss, moved, refused or no "
            "picker)",
            {WORLD_ARG}, bind(&cameraQuery));
        control.registerCommand(
            CAMERA_COMMAND, context.owner.toView(),
            "moves the play camera's orbit rig toward a focus (x, z m), distance (15..2000 m), yaw and tilt (rad below "
            "the horizon, 0.15..1.5), damped unless instant; follow=<entity> keeps an agent in focus (0 stops); "
            "mode=fly switches to the fly camera (F in play); edgePan turns the screen-edge pan on or off; replies "
            "what cityCamera does",
            {optionalArg("mode", ArgKind::String, "orbit or fly", "unchanged"),
             optionalArg("x", ArgKind::Number, "focus x, m", "unchanged"),
             optionalArg("z", ArgKind::Number, "focus z, m", "unchanged"),
             optionalArg("distance", ArgKind::Number, "m from the focus (15..2000)", "unchanged"),
             optionalArg("yaw", ArgKind::Number, "rad about +Y; 0 looks toward -z", "unchanged"),
             optionalArg("tilt", ArgKind::Number, "rad below the horizon (0.15..1.5)", "unchanged"),
             optionalArg("follow", ArgKind::Entity, "an entity to keep in focus; 0 stops following", "unchanged"),
             optionalArg("edgePan", ArgKind::Boolean, "the screen edges pan", "unchanged"),
             optionalArg("instant", ArgKind::Boolean, "jump instead of gliding", "false"), WORLD_ARG},
            bind(&cameraCommand));
        control.registerCommand(
            SELECT_COMMAND, context.owner.toView(),
            "selects a city object: id (a picked entity: a model's submesh walks up to its root; a car, person, "
            "drone, building, lamp, tree or traffic light) and or x, z m on the ground (the road, junction, lot or "
            "building there). The HUD's inspect panel shows it, a pin marks it and its route is drawn; no args "
            "deselects. A click of the inspect tool runs it with the picked entity and point. Replies what "
            "cityInspect does",
            {optionalArg("id", ArgKind::Entity, "the picked entity", "none"),
             optionalArg("x", ArgKind::Number, "a ground point x, m", "none"),
             optionalArg("z", ArgKind::Number, "a ground point z, m", "none"), WORLD_ARG},
            bind(&selectCommand));
        control.registerQuery(
            INSPECT_QUERY, context.owner.toView(),
            "what the inspect panel shows for an entity or ground point (citySelect's args), or the current "
            "selection: kind (car, person, drone, building, lot, road, junction, lamp, tree, trafficLight, ground, "
            "none), entity, title; a car or person: id, status (driving, queued, waiting, walking, crossing, at home, "
            "at work, shopping), speed km/h, road or junction, schedule (homeLot, workLot, purpose, targetLot, "
            "departs, departsInMinutes, trips or walks) and route (state, remaining, lengthM, etaMinutes, etaClock, "
            "nodes still to go); a building or lot: lot, zone, road, model, height, footprint and occupancy "
            "(residents, workers, atHome, atWork); a road: class, lanes, length, limit, ends and cars, capacity and "
            "traffic level each way; a junction: roads, signal, green axis",
            {optionalArg("id", ArgKind::Entity, "an entity", "the selection"),
             optionalArg("x", ArgKind::Number, "a ground point x, m", "the selection"),
             optionalArg("z", ArgKind::Number, "a ground point z, m", "the selection"), WORLD_ARG},
            bind(&inspectQuery));
    }

    void attachPlay(const ModuleContext& context, const World world)
    {
        ServiceLocator& services = world.services();
        const World* play = context.control.world("play");
        auto state = std::make_shared<CityPlay>(
            services.contains<InputSystem>() ? &services.get<InputSystem>() : nullptr, context.control, context.owner,
            play && *play == world ? std::string("play") : std::string(), world);
        CityPlay& playState = *state;
         
        ServiceLocator& scene = context.scene.services();
        playState.picker = services.contains<GamePicker>() ? &services.get<GamePicker>()
            : scene.contains<GamePicker>()                 ? &scene.get<GamePicker>()
                                                           : nullptr;
        services.registerService(std::move(state), context.owner);
        world.registerSystem(
            {.moduleName = context.owner.toView().data(),
             .name = CAMERA_SYSTEM_NAME,
             .fn = &moveCamera,
             .update = SystemPhaseDescriptor{
                 .reads = {getComponentId<CityCameraComponent>(), getComponentId<CityStateComponent>(),
                           getComponentId<CityBuildingComponent>()},
                 .writes = {getComponentId<TransformComponent>(), getComponentId<CameraComponent>()}}});
        const std::vector<ComponentID> inspected = {
            getComponentId<CityStateComponent>(),       getComponentId<CityCarComponent>(),
            getComponentId<CityPedestrianComponent>(),  getComponentId<CityDroneComponent>(),
            getComponentId<CityBuildingComponent>(),    getComponentId<CityLotComponent>(),
            getComponentId<CityLampComponent>(),        getComponentId<CameraComponent>(),
            getComponentId<CityParcelComponent>(),      getComponentId<CityAvatarComponent>(),
            getComponentId<WorldBoundingBoxComponent>()};
        world.registerSystem({.moduleName = context.owner.toView().data(),
                              .name = TOOLS_SYSTEM_NAME,
                              .fn = &runTools,
                              .update = SystemPhaseDescriptor{.reads = inspected,
                                                              .writes = {getComponentId<CityMarkerComponent>(),
                                                                         getComponentId<TransformComponent>(),
                                                                         getComponentId<RenderTintComponent>()}}});
        if (services.contains<UiContext>())
        {
            services.registerService(
                std::make_shared<CityHud>(services.get<UiContext>(),
                                          services.contains<UiViewContext>() ? &services.get<UiViewContext>() : nullptr,
                                          playState),
                context.owner);
            world.registerSystem(
                {.moduleName = context.owner.toView().data(),
                 .name = HUD_SYSTEM_NAME,
                 .fn = &updateHud,
                 .update = SystemPhaseDescriptor{.reads = inspected, .writes = {getComponentId<UiLabelComponent>()}}});
        }
    }
}  
