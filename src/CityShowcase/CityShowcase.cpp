#include "CityShowcase.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "CityCatalog.hpp"
#include "CityDesign.hpp"
#include "CityLogistics.hpp"
#include "CityModel.hpp"
#include "CityPaths.hpp"
#include "CityPlay.hpp"
#include "CityRoutes.hpp"
#include "CityTrips.hpp"
#include "CityTransitions.hpp"
#include "animation/AnimationComponents.hpp"
#include "assetruntime/AssetComponents.hpp"
#include "assetruntime/ModelSpawner.hpp"
#include "base/ContentRoot.hpp"
#include "base/Logging.hpp"
#include "base/MathFunctions.hpp"
#include "base/ServiceLocator.hpp"
#include "control/ControlServer.hpp"
#include "ecs/CommandBuffer.hpp"
#include "ecs/Query.hpp"
#include "ecs/SystemContext.hpp"
#include "ecs/World.hpp"
#include "engine/Module.hpp"
#include "renderruntime/EnvironmentComponent.hpp"
#include "renderruntime/LightComponents.hpp"
#include "renderruntime/RenderingComponents.hpp"
#include "serialization/FormatWalk.hpp"
#include "serialization/JsonFormat.hpp"
#include "world/EntityTransformManager.hpp"
#include "world/TransformComponents.hpp"

 
 
 
 
 
 
 
 
 
 
 
namespace
{
    using namespace spite;
    using namespace spite::city;
    using namespace spite::city::model;

    constexpr const char* BOX_MESH = "cube2";  
    constexpr const char* MATERIAL = "cube_mat_1";

    constexpr u32 DEFAULT_SIZE = 256;
    constexpr u32 DEFAULT_CARS = 50000;
    constexpr u32 DEFAULT_PEDESTRIANS = 30000;
    constexpr u32 DEFAULT_PEOPLE = 1250;  
    constexpr float AVATAR_PARK_Y = -100000.0f;  
    constexpr u32 DEFAULT_DRONES = 1000;  
    constexpr u32 AUTO_TRUCKS = 0xFFFFFFFFu;  
    constexpr u32 DEFAULT_LAMP_LIGHTS = 384;
    constexpr u32 DEFAULT_HOUR = 17;
    constexpr u32 DEFAULT_DAY_SECONDS = 720;  
    constexpr u32 STEPS_PER_SECOND = 60;  
    constexpr u32 SAVE_VERSION = 1;
    constexpr float MIN_CAR_SPEED = 17.0f;  
    constexpr float MAX_CAR_SPEED = 26.0f;
    constexpr float CAR_SPACING = CAR_LENGTH + MIN_GAP + 1.0f;  
    constexpr float COMMUTE_SPAN = 0.5f / 24.0f;  
    constexpr u64 PARK_SALT = 0x9A2Cull;
    constexpr u64 SUBLANE_SALT = 0x5B1Eull;
    constexpr u64 LAMP_SALT = 0x1A3Bull;
    constexpr u64 AGENT_SALT = 0xA6E7ull;
    constexpr u64 PERSON_SALT = 0x9E25ull;
    constexpr u64 TREE_SALT = 0x72EEull;
    constexpr u64 TRUCK_SALT = 0x7C0Cull;
    constexpr u64 DRONE_SALT = 0xD20Eull;
     
    constexpr float LAMP_LIGHT_RADIUS = 30.0f;
    constexpr float LAMP_LIGHT_PARK_Y = -100000.0f;
    constexpr u32 LAMP_LIGHT_FRAMES = 10;
    constexpr float LAMP_LIGHT_MOVE = 20.0f;  

    template <typename T>
    u32 countOf(const World world)
    {
        u32 count = 0;
        for (const Chunk* chunk : world.getChunksContaining(Aspect(getComponentId<T>())))
        {
            count += static_cast<u32>(chunk->size());
        }
        return count;
    }

     
    template <typename T, typename Fn>
    void forEachRow(const World world, Fn&& fn)
    {
        for (const Chunk* chunk : world.getChunksContaining(Aspect(getComponentId<T>())))
        {
            const T* rows = chunk->readColumn<T>();
            for (sizet row = 0; row < chunk->size(); ++row)
            {
                fn(rows[row]);
            }
        }
    }

     
     
    class Spawner
    {
    public:
        explicit Spawner(const World world) :
            m_world(world), m_loading(sceneLoading(world)), m_buffer(world.createCommandBuffer()),
            m_mesh{InternedString(BOX_MESH), AssetType::MESH}, m_material{InternedString(MATERIAL), AssetType::MATERIAL}
        {
        }

        CommandBuffer& buffer() { return m_buffer; }

        Entity box(const Vec3& position, const Vec3& halfExtents, const Vec4& color)
        {
            const Entity entity = m_buffer.createEntity();
            m_buffer.addComponent<TransformComponent>(entity,
                                                      TransformComponent{.position = position, .scale = halfExtents});
            m_buffer.addComponent<TransformMatrixComponent>(entity);
            m_buffer.addComponent<MeshComponent>(entity, MeshComponent{.meshHandle = m_mesh});
            m_buffer.addComponent<MaterialComponent>(entity, MaterialComponent{.materialHandle = m_material});
            m_buffer.addComponent<RenderTintComponent>(entity, RenderTintComponent{.color = color});
            batch();
            return entity;
        }

        template <typename T>
        Entity box(const TransformComponent& transform, const Vec4& color, T component)
        {
            const Entity entity = box(transform.position, transform.scale, color);
            m_buffer.addComponent<T>(entity, std::move(component));
            if constexpr (std::is_same_v<T, CityCarComponent> || std::is_same_v<T, CityPedestrianComponent>)
                m_buffer.addComponent<RenderFadeComponent>(entity);
            return entity;
        }

         
        template <typename T>
        Entity mesh(const TransformComponent& transform, const char* model, const char* material, const Vec4& color,
                    T component)
        {
            const Entity entity = m_buffer.createEntity();
            m_buffer.addComponent<TransformComponent>(entity, TransformComponent(transform));
            m_buffer.addComponent<TransformMatrixComponent>(entity);
            m_buffer.addComponent<MeshComponent>(
                entity, MeshComponent{.meshHandle = AssetHandle{InternedString(model), AssetType::MESH, 0}});
            m_buffer.addComponent<MaterialComponent>(
                entity,
                MaterialComponent{.materialHandle = AssetHandle{InternedString(material), AssetType::MATERIAL, 0}});
            m_buffer.addComponent<RenderTintComponent>(entity, RenderTintComponent{.color = color});
            m_buffer.addComponent<T>(entity, std::move(component));
            batch();
            return entity;
        }

         
        template <typename T>
        Entity data(T component)
        {
            const Entity entity = m_buffer.createEntity();
            m_buffer.addComponent<T>(entity, std::move(component));
            batch();
            return entity;
        }

        void commit() { m_buffer.commit(); }

    private:
        World m_world;
        bool m_loading;
        u32 m_prepared = 0;
        void batch()
        {
            if (m_loading && ++m_prepared % 1024 == 0)
            {
                m_buffer.commit();
                sceneLoadCheckpoint(m_world, "showcase.loadGeometry");
            }
        }
        CommandBuffer m_buffer;
        AssetHandle m_mesh;
        AssetHandle m_material;
    };

     
    struct LayoutParams
    {
        u32 cars = DEFAULT_CARS;
        u32 pedestrians = DEFAULT_PEDESTRIANS;
        u32 people = DEFAULT_PEOPLE;
        u32 drones = DEFAULT_DRONES;
        u32 trucks = AUTO_TRUCKS;
        u32 lampLights = DEFAULT_LAMP_LIGHTS;
        u32 daySteps = DEFAULT_DAY_SECONDS * STEPS_PER_SECOND;
        float dayStart = static_cast<float>(DEFAULT_HOUR) / 24.0f;
        u32 speed = 1;  
    };

    LayoutParams paramsOf(const CityStateComponent& city)
    {
        return {.cars = city.cars,
                .pedestrians = city.pedestrians,
                .people = city.people,
                .drones = city.drones,
                .trucks = city.trucks,
                .lampLights = city.lampLights,
                .daySteps = city.daySteps,
                .dayStart = city.dayStart,
                .speed = city.speed};
    }

     
    u32 maxCars(const RoadGraph& graph)
    {
        double length = 0.0;
        for (const GraphEdge& edge : graph.edges)
        {
            length += 2.0 * edge.lanes * edge.length();
        }
        return static_cast<u32>(std::min(length / CAR_SPACING, 4.0e9));
    }

     
     
    class LaneSlots
    {
    public:
        struct Slot
        {
            u32 sub = 0;
            float pos = 0.0f;
        };

         
        std::vector<u64> park;

        explicit LaneSlots(const RoadGraph& graph) :
            park(graph.nodes.size() * 8, 0), m_graph(graph),
            m_perLane(std::max(static_cast<u32>(static_cast<float>(graph.size) * CELL / CAR_SPACING), 1u)),
            m_taken(static_cast<sizet>(laneCount(graph.size)) * MAX_LANES * m_perLane, 0)
        {
        }

         
         
        std::optional<Slot> claim(const u32 axis, const u32 line, const float dir, const u32 first, const float pos)
        {
            const u32 lane = laneOf(m_graph.size, axis, line, dir);
            const int base = std::clamp(static_cast<int>(pos / CAR_SPACING), 0, static_cast<int>(m_perLane) - 1);
            for (int i = 0; i <= MAX_PROBE; ++i)
            {
                for (const int slot : {base + i, base - i})
                {
                    if (slot < 0 || slot >= static_cast<int>(m_perLane))
                    {
                        continue;
                    }
                    const float at = (static_cast<float>(slot) + 0.5f) * CAR_SPACING;
                    const u32 edge = m_graph.edgeUnder(axis, line, at, dir);
                    if (edge == NO_ID || !outsideBoxes(m_graph.edges[edge], dir, at))
                    {
                        continue;
                    }
                    const u32 sublanes = m_graph.edges[edge].lanes;
                    for (u32 j = 0; j < sublanes; ++j)
                    {
                        const u32 sub = (first + j) % sublanes;
                        u8& taken = m_taken[(static_cast<sizet>(lane) * MAX_LANES + sub) * m_perLane + slot];
                        if (!taken)
                        {
                            taken = 1;
                            return Slot{sub, at};
                        }
                    }
                }
            }
            return std::nullopt;
        }

    private:
        static constexpr int MAX_PROBE = 6;

         
        bool outsideBoxes(const GraphEdge& edge, const float dir, const float pos) const
        {
            for (const u32 node : {edge.a, edge.b})
            {
                const GraphNode& n = m_graph.nodes[node];
                const float u = dir * (pos - static_cast<float>(m_graph.along(node, edge.axis)) * CELL);
                if (u > -n.stop[1 - edge.axis] - 0.05f && u < n.half[1 - edge.axis] + CAR_HALF_LENGTH)
                {
                    return false;
                }
            }
            return true;
        }

        const RoadGraph& m_graph;
        u32 m_perLane;
        std::vector<u8> m_taken;
    };

     
     
    CityCarComponent residentCar(const CityStateComponent& city, const CityMap& map, const u32 id, LaneSlots& slots,
                                 const float maxSpeed)
    {
        const RoadGraph& graph = map.graph();
        const u64 seed = city.seed;
        const u32 home = map.pick(CATEGORY_HOME, mix(seed ^ HOME_SALT ^ id));
        const u32 work = nearLot(map, CATEGORY_WORK, home, COMMUTE_CELLS, mix(seed ^ HOME_SALT ^ 1 ^ id), true);
        CityCarComponent car{.id = id, .maxSpeed = maxSpeed, .homeLot = home, .workLot = work};
        if (graph.edges.empty())
        {
            car.state = CAR_PARKED;
            car.parkUntil = 0xFFFFFFFFu;
            return car;
        }
         
        const auto park = [&](const u32 purpose, const u32 lotId)
        {
            const LotRef* lot = map.reachable(lotId);
            const u32 edge = lot ? lot->edge : id % static_cast<u32>(graph.edges.size());
            const u32 node = lot ? tripNode(map, lotId, id, 0xFFFFFFFFu, NO_ID) : graph.edges[edge].b;
            const ParkChoice place = chooseParking(
                graph, node, edge, false, lot ? lot->edge : NO_ID, parkSpotOf(city, map, id), slots.park.data(),
                parksOnStreet(seed, id, lotId, lot ? lot->zone : ZONE_COMMERCIAL_LOW, false));
            car.axis = graph.edges[place.edge].axis;
            car.line = graph.edges[place.edge].line;
            car.dir = place.dir;
            car.pos = place.pos;
            car.state = CAR_PARKED;
            car.speed = 0.0f;
            car.sublane = place.garage ? PARK_GARAGE : 0;
            car.purpose = purpose;
            car.targetLot = lotId;
            car.destX = graph.nodes[node].x;
            car.destZ = graph.nodes[node].z;
            assignTrip(car, nextTrip(car, city, map, 0));
            return car;
        };
         
         
        const auto drive = [&](const u32 purpose, const u32 target, const u32 leftPurpose, const u32 left)
        {
            const u64 hash = mix(seed ^ PARK_SALT ^ id);
            const GraphEdge& edge = graph.edges[hash % graph.edges.size()];
            const float dir = (hash >> 20 & 1) != 0 ? -1.0f : 1.0f;
            const float along = static_cast<float>(edge.lo) +
                static_cast<float>(edge.hi - edge.lo) * static_cast<float>((hash >> 32) % 1000) / 1000.0f;
            const std::optional<LaneSlots::Slot> slot = slots.claim(
                edge.axis, edge.line, dir, static_cast<u32>(mix(seed ^ SUBLANE_SALT ^ id) % MAX_LANES), along * CELL);
            const u32 node = tripNode(map, target, id, 0, NO_ID);
            if (!slot || node == NO_ID)
            {
                return park(leftPurpose, left);
            }
            const GraphEdge& on = graph.edges[graph.edgeUnder(edge.axis, edge.line, slot->pos, dir)];
            car.axis = on.axis;
            car.line = on.line;
            car.dir = dir;
            car.pos = slot->pos;
            car.sublane = slot->sub;
            car.state = CAR_DRIVING;
            car.purpose = purpose;
            car.targetLot = target;
            car.destX = graph.nodes[node].x;
            car.destZ = graph.nodes[node].z;
            car.parkUntil = 0;
             
             
            const u32 ahead = dir > 0.0f ? on.b : on.a;
            const float line =
                static_cast<float>(graph.along(ahead, on.axis)) * CELL - dir * graph.nodes[ahead].stop[1 - on.axis];
            const float toLine = std::max(dir * (line - slot->pos), 0.0f);
            car.speed = std::min({car.maxSpeed * 0.5f, on.limit(), std::sqrt(2.0f * BRAKING * toLine)});
            return car;
        };
        const float t = timeOfDay(city, 0);
        const float leaveHome = morningTime(seed, id, 0);
        const float leaveWork = eveningTime(seed, id, 0);
        if (t >= leaveHome && t < leaveHome + COMMUTE_SPAN)
        {
            return drive(TRIP_WORK, work, TRIP_HOME, home);
        }
        if (t >= leaveHome + COMMUTE_SPAN && t < leaveWork)
        {
            return park(TRIP_WORK, work);
        }
        if (t >= leaveWork && t < leaveWork + COMMUTE_SPAN)
        {
            return drive(TRIP_HOME, home, TRIP_WORK, work);
        }
        return park(TRIP_HOME, home);
    }

     

    class Models
    {
    public:
        explicit Models(const World world) : m_world(world)
        {
            if (world.services().contains<ModelSpawner>())
            {
                m_spawner = &world.services().get<ModelSpawner>();
            }
        }

        bool has(const char* model) const { return m_spawner && m_spawner->hasModel(InternedString(model)); }

        template <typename Names>
        bool hasAll(const Names& names) const
        {
            return std::ranges::all_of(names, [this](const auto& name) { return has(name); });
        }

         
        std::vector<Entity> spawn(const char* model, const std::span<const TransformComponent> places) const
        {
            if (places.empty() || !has(model))
            {
                return {};
            }
            std::vector<Entity> result;
            result.reserve(places.size());
            const sizet batchSize = sceneLoading(m_world) ? 128 : places.size();
            for (sizet base = 0; base < places.size(); base += batchSize)
            {
                const sizet count = std::min(batchSize, places.size() - base);
                const std::vector<Entity> parents(count, Entity::undefined());
                auto roots = m_spawner->spawn(m_world, InternedString(model), parents);
                if (!roots)
                {
                    SWARNING_LOG("CityShowcase: model '{}' did not spawn: {}", model, roots.error().message.c_str());
                    return {};
                }
                for (sizet i = 0; i < count; ++i)
                    m_world.getComponent<TransformComponent>((*roots)[i]) = places[base + i];
                result.insert(result.end(), roots->begin(), roots->end());
                sceneLoadCheckpoint(m_world, "showcase.loadModels", base + count, places.size());
            }
            return result;
        }

         
        template <typename T>
        void attach(const std::span<const Entity> roots, const std::span<const T> rows) const
        {
            const ComponentID id = getComponentId<T>();
            m_world.addComponents(roots, std::span(&id, 1),
                                  [&]
                                  {
                                      for (sizet i = 0; i < roots.size(); ++i)
                                      {
                                          m_world.getComponent<T>(roots[i]) = rows[i];
                                      }
                                  });
            if constexpr (std::is_same_v<T, CityCarComponent> || std::is_same_v<T, CityAvatarComponent>)
                prepareCityFades(m_world, roots);
        }

    private:
        World m_world;
        const ModelSpawner* m_spawner = nullptr;
    };

     
    template <typename Row>
    struct ModelBatch
    {
        std::vector<TransformComponent> places;
        std::vector<Row> rows;

        void add(const TransformComponent& place, const Row& row)
        {
            places.push_back(place);
            rows.push_back(row);
        }
    };

     
     
    template <typename Row, typename Fallback>
    void spawnBatches(const Models& models, const std::map<std::string, ModelBatch<Row>>& batches, Fallback&& fallback)
    {
        for (const auto& [model, batch] : batches)
        {
            const std::vector<Entity> roots = models.spawn(model.c_str(), batch.places);
            if (roots.empty())
            {
                for (sizet i = 0; i < batch.rows.size(); ++i)
                {
                    fallback(batch.places[i], batch.rows[i]);
                }
                continue;
            }
            models.attach<Row>(roots, batch.rows);
        }
    }

     

    Vec4 roadColor(const u32 roadClass)
    {
        return roadClass == ROAD_HIGHWAY ? Vec4(0.09f, 0.09f, 0.1f, 1.0f) : Vec4(0.12f, 0.12f, 0.13f, 1.0f);
    }

    CityBuildingComponent buildingRow(const DesignBuilding& building, const float t)
    {
        CityBuildingComponent row{.lot = building.lot,
                                  .zone = building.zone,
                                  .model = InternedString(building.model),
                                  .x = building.x,
                                  .z = building.z,
                                  .halfX = building.halfX,
                                  .halfZ = building.halfZ,
                                  .rotation = building.rotation,
                                  .height = building.height,
                                  .dayColor = building.color,
                                  .dusk = building.dusk,
                                  .dawn = building.dawn,
                                  .windows = building.windows};
        row.light = buildingLight(row, t);
        return row;
    }

    TransformComponent placed(const Vec3& position, const float yaw, const Vec3& scale = Vec3(1.0f))
    {
        return TransformComponent{.position = position, .rotation = yawRotation(yaw), .scale = scale};
    }

     
     
     
    void addRoadTiles(const RoadGraph& graph, const GraphEdge& edge, std::vector<TransformComponent>& bays,
                      std::vector<TransformComponent>& ends)
    {
        float low = 0.0f;
        float high = 0.0f;
        edgeSpan(graph, edge, low, high);
        const float length = high - low;
        if (length < 0.5f)
        {
            return;
        }
        const u32 count = std::max(static_cast<u32>(std::lround(length / catalog::ROAD_TILE_LENGTH)), 1u);
        const float tile = length / static_cast<float>(count);
        const float corridor = ROAD_CLASS_SPECS[edge.roadClass].corridor;
        const Vec3 scale(std::max(corridor, edge.half() + 1.0f) / corridor, 1.0f, tile / catalog::ROAD_TILE_LENGTH);
        const float across = static_cast<float>(edge.line) * CELL;
        for (u32 i = 0; i < count; ++i)
        {
            const float along = low + (static_cast<float>(i) + 0.5f) * tile;
            const bool bay = hasParkingLane(edge) && i >= BAY_SKIP && i + BAY_SKIP < count;
            (bay ? bays : ends)
                .push_back(edge.axis == 0 ? placed({along, 0.0f, across}, 1.5707963f, scale)
                                          : placed({across, 0.0f, along}, 0.0f, scale));
        }
    }

     
    u32 junctionClass(const RoadGraph& graph, const GraphNode& node)
    {
        u32 widest = ROAD_STREET;
        for (const u32 edge : node.edges)
        {
            widest = edge != NO_ID ? std::max(widest, graph.edges[edge].roadClass) : widest;
        }
        return widest;
    }

    void addTrafficLights(const GraphNode& node, ModelBatch<CitySignalComponent>& lights)
    {
        for (u32 approach = 0; approach < 4; ++approach)
            if (node.edges[approach] != NO_ID)
                lights.add(trafficLightPlace(node, approach),
                           CitySignalComponent{.x = node.x, .z = node.z, .axis = approach & 1});
    }

     
     
     
     
    void spawnDesign(const World world, const CityDesign& design, const RoadGraph& graph, const float t)
    {
        const Models models(world);
        const bool roadModels = models.hasAll(catalog::ROAD_SEGMENTS) && models.hasAll(catalog::ROAD_ENDS) &&
            models.hasAll(catalog::ROAD_JUNCTIONS);
        const bool props = roadModels && models.has(catalog::TRAFFIC_LIGHT);
        std::map<std::string, ModelBatch<CityPieceComponent>> pieces;
        ModelBatch<CitySignalComponent> signals;
        Spawner spawn(world);
        const float half = static_cast<float>(design.size) * CELL * 0.5f;
         
        spawn.box(TransformComponent{.position = {half, -0.56f, half}, .scale = {half + 400.0f, 0.5f, half + 400.0f}},
                  {0.3f, 0.29f, 0.27f, 1.0f}, CityGroundComponent{.size = design.size});
        for (const GraphEdge& edge : graph.edges)
        {
            const GraphNode& a = graph.nodes[edge.a];
            const GraphNode& b = graph.nodes[edge.b];
            const CityRoadComponent road{
                .x0 = a.x, .z0 = a.z, .x1 = b.x, .z1 = b.z, .roadClass = edge.roadClass, .lanes = edge.lanes};
            if (roadModels)
            {
                spawn.data(road);
                ModelBatch<CityPieceComponent>& tiles = pieces[catalog::ROAD_SEGMENTS[edge.roadClass]];
                ModelBatch<CityPieceComponent>& ends = pieces[catalog::ROAD_ENDS[edge.roadClass]];
                addRoadTiles(graph, edge, tiles.places, ends.places);
                tiles.rows.resize(tiles.places.size(), CityPieceComponent{.kind = PIECE_ROAD});
                ends.rows.resize(ends.places.size(), CityPieceComponent{.kind = PIECE_ROAD});
            }
            else
            {
                TransformComponent slab;
                placeSlab(graph, edge, slab);
                spawn.box(slab, roadColor(edge.roadClass), road);
            }
            for (const u32 reverse : {0u, 1u})
            {
                const CityLaneComponent lane{.axis = edge.axis, .line = edge.line, .from = edge.lo, .reverse = reverse};
                if (roadModels)
                {
                    const Entity entity = spawn.data(lane);
                    spawn.buffer().addComponent<RenderTintComponent>(entity,
                                                                     RenderTintComponent{.color = congestionTint(0)});
                    continue;
                }
                TransformComponent strip;
                placeLane(graph, edge, reverse, strip);
                spawn.box(strip, congestionTint(0), lane);
            }
        }
        for (u32 node = 0; node < graph.nodes.size(); ++node)
        {
            const GraphNode& n = graph.nodes[node];
            const CityJunctionComponent junction{.x = n.x, .z = n.z};
            TransformComponent square;
            placeJunction(graph, node, square);
            if (!roadModels)
            {
                spawn.box(square, {0.13f, 0.13f, 0.14f, 1.0f}, junction);
                continue;
            }
            spawn.data(junction);
            const u32 roadClass = junctionClass(graph, n);
            const float corridor = ROAD_CLASS_SPECS[roadClass].corridor;
            pieces[catalog::ROAD_JUNCTIONS[roadClass]].add(
                placed({square.position.x, 0.0f, square.position.z}, 0.0f,
                       {square.scale.x / corridor, 1.0f, square.scale.z / corridor}),
                CityPieceComponent{.kind = PIECE_ROAD});
            if (props && n.signal)
            {
                addTrafficLights(n, signals);
            }
        }
        spawn.commit();

         
        ModelBatch<CityLampComponent> lamps;
        u64 lampRng = mix(design.seed ^ LAMP_SALT);
        for (const LampPlace& place : lampPlaces(graph))
        {
            CityLampComponent lamp{.x = place.x,
                                   .z = place.z,
                                   .yaw = place.yaw,
                                   .onAt = uniform(lampRng, 0.75f, 0.77f),
                                   .offAt = uniform(lampRng, 0.23f, 0.25f)};
            lamp.on = inWindow(t, lamp.onAt, lamp.offAt) ? 1u : 0u;
            lamps.add(placed({place.x, 0.0f, place.z}, place.yaw), lamp);
        }
        const auto boxLamp = [&spawn](const TransformComponent& place, const CityLampComponent& lamp)
        {
            spawn.box(TransformComponent{.position = {place.position.x, 3.0f, place.position.z},
                                         .scale = {0.12f, 3.0f, 0.12f}},
                      lampTint(lamp.on), lamp);
        };
        const std::vector<Entity> lampRoots = models.spawn(catalog::STREET_LAMP, lamps.places);
        if (lampRoots.empty())
        {
            for (sizet i = 0; i < lamps.rows.size(); ++i)
            {
                boxLamp(lamps.places[i], lamps.rows[i]);
            }
        }
        else
        {
            models.attach<CityLampComponent>(lampRoots, lamps.rows);
            std::vector<RenderTintComponent> tints;
            tints.reserve(lamps.rows.size());
            for (const CityLampComponent& lamp : lamps.rows)
            {
                tints.push_back({.color = lampTint(lamp.on)});
            }
            models.attach<RenderTintComponent>(lampRoots, tints);  
        }

        for (const DesignLot& lot : design.lots)
        {
            spawn.data(CityLotComponent{.id = lot.id,
                                        .x0 = lot.x0,
                                        .z0 = lot.z0,
                                        .x1 = lot.x1,
                                        .z1 = lot.z1,
                                        .zone = lot.zone,
                                        .special = lot.special,
                                        .axis = lot.axis,
                                        .line = lot.line});
        }
        for (const ZoneOverride& zone : design.overrides)
        {
            spawn.data(CityZoneOverrideComponent{.x0 = zone.x0,
                                                 .z0 = zone.z0,
                                                 .x1 = zone.x1,
                                                 .z1 = zone.z1,
                                                 .zone = zone.zone,
                                                 .special = zone.special});
        }
        spawn.commit();

         
        const auto boxBuilding = [&spawn](const TransformComponent&, const CityBuildingComponent& row)
        {
            spawn.box(TransformComponent{.position = {row.x, row.height * 0.5f, row.z},
                                         .scale = {row.halfX, row.height * 0.5f, row.halfZ}},
                      row.dayColor, row);
        };
        std::map<std::string, ModelBatch<CityBuildingComponent>> buildings;
        for (const DesignBuilding& building : design.buildings)
        {
            for (CityLampComponent source : buildingNightLights(building))
            {
                source.on = inWindow(t, source.onAt, source.offAt) ? 1u : 0u;
                const Entity light = spawn.data(source);
                spawn.buffer().addComponent<RenderTintComponent>(light,
                                                                 RenderTintComponent{.color = lampTint(source.on)});
            }
            const CityBuildingComponent row = buildingRow(building, t);
            if (building.model.empty() || !models.has(building.model.c_str()))
            {
                boxBuilding({}, row);
                continue;
            }
            const float yaw = (1.0f - static_cast<float>(building.rotation & 3)) * 1.5707963f;
            buildings[building.model].add(placed({building.x, 0.0f, building.z}, yaw), row);
        }
        spawnBatches(models, buildings, boxBuilding);
        spawn.commit();
        spawnBatches(models, pieces, [](const TransformComponent&, const CityPieceComponent&) {});
        const auto posts = models.spawn(catalog::TRAFFIC_LIGHT, signals.places);
        if (!posts.empty())
        {
            models.attach<CitySignalComponent>(posts, signals.rows);
            const std::vector<CityPieceComponent> pieces(posts.size(), CityPieceComponent{.kind = PIECE_TRAFFIC_LIGHT});
            models.attach<CityPieceComponent>(posts, pieces);
            const CityStateComponent& city = *findCity(world);
            auto& transforms = world.services().get<EntityTransformManager>();
            std::vector<Entity> parts;
            auto buffer = world.createCommandBuffer();
            constexpr const char* materials[] = {"city_mat_traffic_vehicle_red", "city_mat_traffic_vehicle_amber",
                                                 "city_mat_traffic_vehicle_green", "city_mat_traffic_ped_red",
                                                 "city_mat_traffic_ped_green"};
            for (sizet i = 0; i < posts.size(); ++i)
            {
                parts.clear();
                transforms.collectSubtree(posts[i], parts);
                for (const Entity part : parts)
                {
                    const auto* material = world.tryGetComponent<MaterialComponent>(part);
                    if (!material)
                        continue;
                    for (u32 bulb = 0; bulb < std::size(materials); ++bulb)
                        if (material->materialHandle.name.toView() == materials[bulb])
                        {
                            CitySignalComponent lens = signals.rows[i];
                            lens.pedestrian = bulb >= 3 ? 1u : 0u;
                            lens.color = bulb < 3 ? bulb : bulb == 3 ? SIGNAL_RED : SIGNAL_GREEN;
                            lens.on = signalLensOn(city, lens, city.step) ? 1u : 0u;
                            buffer.addComponent<CitySignalComponent>(part, CitySignalComponent(lens));
                            buffer.addComponent<RenderFadeComponent>(
                                part, RenderFadeComponent{.opacity = static_cast<float>(lens.on)});
                            break;
                        }
                }
            }
            buffer.commit();
        }
    }

     
    u32 drawnPeople(const CityStateComponent& city)
    {
        return (city.models & MODEL_PEOPLE) != 0 ? std::min(city.people, city.pedestrians) : 0;
    }

     
    struct HubPads
    {
        u32 lot = 0;
        std::array<Vec3, 4> pads{};
    };

     
     
    std::vector<HubPads> hubPads(const CityDesign& design, const CityMap& map)
    {
        std::vector<HubPads> hubs;
        for (const u32 lot : map.specialLots(SPECIAL_DRONE_HUB))
        {
            const auto building = std::ranges::lower_bound(design.buildings, lot, {}, &DesignBuilding::lot);
            if (building == design.buildings.end() || building->lot != lot)
            {
                continue;
            }
            HubPads hub{.lot = lot};
            const bool hubModel = building->model == catalog::DRONE_HUB;
            for (u32 pad = 0; pad < 4; ++pad)
            {
                const float dx = std::min(std::fabs(catalog::HUB_PADS[pad][0]), std::max(building->halfX - 2.0f, 0.0f));
                const float dz = std::min(std::fabs(catalog::HUB_PADS[pad][1]), std::max(building->halfZ - 2.0f, 0.0f));
                hub.pads[pad] = {building->x + std::copysign(dx, catalog::HUB_PADS[pad][0]),
                                 hubModel ? catalog::HUB_PAD_HEIGHT : building->height + 0.05f,
                                 building->z + std::copysign(dz, catalog::HUB_PADS[pad][1])};
            }
            hubs.push_back(hub);
        }
        return hubs;
    }

     
    Vec4 truckColor(const u32 index)
    {
        constexpr Vec4 FLEETS[] = {{0.95f, 0.95f, 0.95f, 1.0f},
                                   {0.8f, 0.12f, 0.1f, 1.0f},
                                   {0.15f, 0.3f, 0.75f, 1.0f},
                                   {0.95f, 0.75f, 0.1f, 1.0f},
                                   {0.2f, 0.55f, 0.25f, 1.0f}};
        return FLEETS[mix(index ^ 0xF1EEull) % std::size(FLEETS)];
    }

     
     
    CityCarComponent baseTruck(const CityStateComponent& city, const CityMap& map, const u32 id, LaneSlots& slots,
                               const float maxSpeed)
    {
        const RoadGraph& graph = map.graph();
        const u32 base = truckBase(map, truckIndex(city, id));
        CityCarComponent car{.id = id, .maxSpeed = maxSpeed, .state = CAR_PARKED, .homeLot = base, .workLot = base};
        car.purpose = TRIP_IDLE;
        car.targetLot = base;
        car.parkUntil = static_cast<u32>(mix(city.seed ^ TRUCK_SALT ^ id) % 600);
        const LotRef* lot = map.reachable(base);
        if (!lot)
        {
            car.parkUntil = 0xFFFFFFFFu;
            return car;
        }
        const u32 node = map.accessNode(*lot, id % 2);
        const ParkChoice place = chooseParking(graph, node, lot->edge, false, lot->edge, parkSpotOf(city, map, id),
                                               slots.park.data(), parksOnStreet(city.seed, id, base, lot->zone, true));
        car.axis = graph.edges[place.edge].axis;
        car.line = graph.edges[place.edge].line;
        car.dir = place.dir;
        car.pos = place.pos;
        car.sublane = place.garage ? PARK_GARAGE : 0;
        car.destX = graph.nodes[node].x;
        car.destZ = graph.nodes[node].z;
        return car;
    }

     
     
     
    void populate(const World world, const CityStateComponent& city, const CityDesign& design, const CityMap& map)
    {
        const RoadGraph& graph = map.graph();
        const u64 seed = city.seed;
        const bool carModels = (city.models & MODEL_CARS) != 0;
        u64 rng = seed ^ AGENT_SALT;
        Spawner spawn(world);
        LaneSlots slots(graph);
        const bool truckModels = (city.models & MODEL_TRUCKS) != 0;
         
        std::vector<CityCarComponent> bases;
        for (u32 id = city.cars - std::min(city.trucks, city.cars); id < city.cars; ++id)
        {
            bases.push_back(baseTruck(city, map, id, slots, 0.0f));
        }
        for (u32 id = 0; id < city.cars; ++id)
        {
            const float maxSpeed = uniform(rng, MIN_CAR_SPEED, MAX_CAR_SPEED);
            const bool truck = isTruck(city, id);
            CityCarComponent car = truck ? bases[truckIndex(city, id)] : residentCar(city, map, id, slots, maxSpeed);
            car.maxSpeed = maxSpeed;
            const Vec4 color = truck ? truckColor(truckIndex(city, id))
                                     : hsv(draw(seed, id, 1), uniform(rng, 0.3f, 0.9f), uniform(rng, 0.6f, 1.0f));
            TransformComponent transform;
            placeVehicle(car, graph, transform, city);
            if (truck && truckModels)
            {
                spawn.mesh(transform, catalog::TRUCKS[truckModel(truckIndex(city, id))].name, catalog::VEHICLE_MATERIAL,
                           color, car);
            }
            else if (!truck && carModels)
            {
                spawn.mesh(transform, catalog::CARS[carModel(id)].name, catalog::VEHICLE_MATERIAL, color, car);
            }
            else
            {
                spawn.box(transform, color, car);
            }
        }
        spawn.commit();

         
         
        const float startTime = timeOfDay(city, 0);
        for (u32 id = 0; id < city.pedestrians; ++id)
        {
            const u32 home = map.pick(CATEGORY_HOME, mix(seed ^ HOME_SALT ^ PED_SALT ^ id));
            const u32 work = pedWorkLot(map, home, id, seed);
            const bool atWork =
                isWorker(seed, id) && map.reachable(work) && startTime >= 9.0f / 24.0f && startTime < 19.0f / 24.0f;
            CityPedestrianComponent ped{.id = id,
                                        .speed = uniform(rng, 1.1f, 1.7f),
                                        .mode = PED_DWELLING,
                                        .homeLot = home,
                                        .targetLot = atWork ? work : home,
                                        .purpose = atWork ? TRIP_WORK : TRIP_HOME,
                                        .workLot = work};
            if (atWork)
                ped.carLot = carLotNear(map, work, mix(seed ^ PED_SALT ^ id));
            relocatePedestrian(ped, map, atWork ? work : home);
            assignWalk(ped, nextWalk(ped, city, map, 0), map);
             
             
            if (startTime >= 6.0f / 24.0f && startTime < 23.0f / 24.0f && draw(seed ^ PED_SALT ^ 81, id, 0) < .6f)
                ped.dwellUntil =
                    std::min(ped.dwellUntil, 1u + static_cast<u32>(draw(seed ^ PED_SALT ^ 82, id, 0) * 1800));
             
            const Vec4 color = hsv(uniform(rng, 0.0f, 1.0f), uniform(rng, 0.2f, 0.5f), uniform(rng, 0.25f, 0.55f));
            TransformComponent transform;
            placePedestrian(ped, graph, transform);
            spawn.box(transform, color, ped);
        }
        spawn.commit();

         
        const Models models(world);
        std::map<std::string, ModelBatch<CityAvatarComponent>> avatars;
        for (u32 slot = 0; slot < drawnPeople(city); ++slot)
        {
            avatars[catalog::PEOPLE[slot % std::size(catalog::PEOPLE)]].add(
                TransformComponent{.position = {0.0f, AVATAR_PARK_Y, 0.0f}}, CityAvatarComponent{.index = slot});
        }
        for (const auto& [avatar, batch] : avatars)
        {
            const std::vector<Entity> roots = models.spawn(avatar.c_str(), batch.places);
            if (roots.empty())
            {
                continue;
            }
            models.attach<CityAvatarComponent>(roots, batch.rows);
            const std::vector<AnimationPlayerComponent> players(
                roots.size(), AnimationPlayerComponent{.clip = InternedString(std::format("{}.idle", avatar))});
            models.attach<AnimationPlayerComponent>(roots, players);
        }

         
         
        const std::vector<HubPads> hubs = hubPads(design, map);
        const bool drones = (city.models & MODEL_DRONES) != 0;
        const bool parcels = (city.models & MODEL_PARCELS) != 0;
        const u32 drawnDrones = std::min(city.drones, static_cast<u32>(hubs.size()) * 4);
        for (u32 id = 0; id < drawnDrones; ++id)
        {
            const HubPads& hub = hubs[id % hubs.size()];
            const u32 pad = static_cast<u32>(id / hubs.size()) % 4;
            const Vec3 at = hub.pads[pad];
            const CityDroneComponent drone{.id = id,
                                           .hub = hub.lot,
                                           .pad = pad,
                                           .phase = DRONE_CHARGING,
                                           .x = at.x,
                                           .y = at.y,
                                           .z = at.z,
                                           .yaw = static_cast<float>(pad) * 1.5707963f,
                                           .padX = at.x,
                                           .padY = at.y,
                                           .padZ = at.z,
                                           .phaseUntil = static_cast<u32>(mix(seed ^ DRONE_SALT ^ id) % 900)};
            const Vec4 color =
                nextRandom(rng) % 5 == 0 ? Vec4(0.9f, 0.15f, 0.1f, 1.0f) : Vec4(0.95f, 0.95f, 1.0f, 1.0f);
            TransformComponent transform;
            placeDrone(drone, transform, drones);
            if (drones)
            {
                spawn.mesh(transform, catalog::DRONE, catalog::VEHICLE_MATERIAL, color, drone);
            }
            else
            {
                spawn.box(transform, color, drone);
            }
            const TransformComponent hidden = parcelPlace(DroneView{}, 0, parcels);
            const Vec4 cardboard(0.72f, 0.55f, 0.36f, 1.0f);
            if (parcels)
            {
                spawn.mesh(hidden, catalog::PACKAGE, catalog::VEHICLE_MATERIAL, Vec4(1.0f),
                           CityParcelComponent{.drone = id});
            }
            else
            {
                spawn.box(hidden, cardboard, CityParcelComponent{.drone = id});
            }
        }
        spawn.commit();
    }

     
    Entity addSky(CommandBuffer& buffer, const Vec3& center, const u32 moon, const float t)
    {
        CitySunComponent sun{.moon = moon};
        DirectionalLightComponent light{.castShadows = moon == 0};
        TransformComponent transform{.position = {center.x, 10.0f, center.z}};
        turnSun(sun, light, transform, t);
        const Entity entity = buffer.createEntity();
        buffer.addComponent<TransformComponent>(entity, std::move(transform));
        buffer.addComponent<TransformMatrixComponent>(entity);
        buffer.addComponent<DirectionalLightComponent>(entity, std::move(light));
        buffer.addComponent<CitySunComponent>(entity, std::move(sun));
        return entity;
    }

     
     
    Entity authorCity(const World world, const CityDesign& design, const LayoutParams& params, const u32 version)
    {
        const std::shared_ptr<const RoadGraph> graph = buildGraph(design.size, design.edges);
        const CityMap map(design.seed, design.size, graph, design.lots);
        const Models models(world);
        const bool cars = std::ranges::all_of(catalog::CARS, [&](const auto& car) { return models.has(car.name); });
        const bool trucks =
            std::ranges::all_of(catalog::TRUCKS, [&](const auto& truck) { return models.has(truck.name); });
        const u32 drawn = (cars ? MODEL_CARS : 0u) | (models.hasAll(catalog::PEOPLE) ? MODEL_PEOPLE : 0u) |
            (models.has(catalog::DRONE) ? MODEL_DRONES : 0u) | (trucks ? MODEL_TRUCKS : 0u) |
            (models.has(catalog::PACKAGE) ? MODEL_PARCELS : 0u);
         
        const bool bases = !map.specialLots(SPECIAL_TRUCK_DEPOT).empty() || !map.specialLots(SPECIAL_WAREHOUSE).empty();
        const u32 truckCount =
            bases ? std::min(params.trucks == AUTO_TRUCKS ? params.cars / 20 : params.trucks, params.cars) : 0u;
        const CityStateComponent city{.seed = design.seed,
                                      .size = design.size,
                                      .cars = params.cars,
                                      .drones = params.drones,
                                      .pedestrians = params.pedestrians,
                                      .people = params.people,
                                      .lampLights = params.lampLights,
                                      .models = drawn,
                                      .daySteps = params.daySteps,
                                      .dayStart = params.dayStart,
                                      .designVersion = version,
                                      .speed = params.speed,
                                      .trucks = truckCount};
        const float t0 = timeOfDay(city, 0);
        const float extent = static_cast<float>(design.size) * CELL;
        const float center = extent * 0.5f;
        Spawner spawn(world);
        CommandBuffer& buffer = spawn.buffer();
        const Entity state = buffer.createEntity();
        buffer.addComponent<CityStateComponent>(state, CityStateComponent(city));

         
        const Entity sun = addSky(buffer, {center, 0.0f, center}, 0, t0);
        buffer.addComponent<EnvironmentComponent>(sun, EnvironmentComponent{.mode = EnvironmentMode::Procedural});
        addSky(buffer, {center, 0.0f, center}, 1, t0);
        for (u32 i = 0; i < params.lampLights; ++i)
        {
            const Entity light = buffer.createEntity();
            buffer.addComponent<TransformComponent>(light,
                                                    TransformComponent{.position = {center, LAMP_LIGHT_PARK_Y, center},
                                                                       .rotation = lightRotation({0.0f, -1.0f, 0.0f})});
            buffer.addComponent<TransformMatrixComponent>(light);
            buffer.addComponent<SpotLightComponent>(light,
                                                    SpotLightComponent{.color = {1.0f, 0.8f, 0.55f},
                                                                       .intensity = 0.0f,  
                                                                       .radius = LAMP_LIGHT_RADIUS,
                                                                       .innerConeAngle = toRadians(35.0f),
                                                                       .outerConeAngle = toRadians(62.0f)});
            buffer.addComponent<CityLampLightComponent>(light);
        }

        Entity camera = Entity::undefined();
        for (const Chunk* chunk : world.getChunksContaining(Aspect(getComponentId<CameraComponent>())))
            if (!chunk->empty())
            {
                camera = chunk->entities()[0];
                break;
            }
        const bool createCamera = !camera.isValid();
        if (createCamera)
            camera = buffer.createEntity();
        const float height = 80.0f + extent * 0.3f;
        const float back = extent * 0.15f + 100.0f;
        if (createCamera)
        {
            buffer.addComponent<TransformComponent>(
                camera,
                TransformComponent{.position = {center, height, extent + back},
                                   .rotation = fromAxisAngle({1.0f, 0.0f, 0.0f}, -std::atan2(height, center + back))});
            buffer.addComponent<TransformMatrixComponent>(camera);
            buffer.addComponent<CameraComponent>(camera,
                                                 CameraComponent{.fov = toRadians(60.0f),
                                                                 .nearPlane = 0.5f,
                                                                 .farPlane = 4000.0f,
                                                                 .exposure = cameraEv(t0),
                                                                 .autoExposure = true});
            buffer.addComponent<CityCameraComponent>(camera, CityCameraComponent{.focus = {center, 0.0f, center}});
        }
        else
        {
            if (!world.hasComponent<CityCameraComponent>(camera))
                buffer.addComponent<CityCameraComponent>(camera, CityCameraComponent{.focus = {center, 0.0f, center}});
            auto& lens = world.getComponent<CameraComponent>(camera);
            lens.farPlane = 4000.0f;
            lens.autoExposure = true;
            lens.exposure = cameraEv(t0);
        }
        spawn.commit();

        spawnDesign(world, design, *graph, t0);
        populate(world, city, design, map);
        return camera;
    }

    std::vector<Entity> entitiesWith(const World world, const std::initializer_list<ComponentID> components)
    {
        std::vector<Entity> entities;
        for (const ComponentID component : components)
        {
            for (const Chunk* chunk : world.getChunksContaining(Aspect(component)))
            {
                entities.insert(entities.end(), chunk->entities().begin(), chunk->entities().end());
            }
        }
        return entities;
    }

    std::vector<Entity> designEntities(const World world)
    {
        return entitiesWith(world,
                            {getComponentId<CityGroundComponent>(), getComponentId<CityRoadComponent>(),
                             getComponentId<CityJunctionComponent>(), getComponentId<CityLaneComponent>(),
                             getComponentId<CityLampComponent>(), getComponentId<CityLotComponent>(),
                             getComponentId<CityZoneOverrideComponent>(), getComponentId<CityBuildingComponent>(),
                             getComponentId<CityPieceComponent>()});
    }

     
    void destroyCityEntities(const World world, const std::vector<Entity>& entities)
    {
        if (!world.services().contains<EntityTransformManager>())
        {
            world.destroyEntities(entities);
            return;
        }
        const EntityTransformManager& transforms = world.services().read<EntityTransformManager>().get();
        std::vector<Entity> all;
        all.reserve(entities.size());
        std::vector<Entity> subtree;
        for (const Entity entity : entities)
        {
            if (transforms.collectSubtree(entity, subtree) && !subtree.empty())
            {
                all.insert(all.end(), subtree.begin(), subtree.end());
            }
            else
            {
                all.push_back(entity);
            }
        }
        world.destroyEntities(all);
    }

    std::vector<Entity> cityEntities(const World world)
    {
        std::vector<Entity> entities = designEntities(world);
        const std::vector<Entity> rest =
            entitiesWith(world,
                         {getComponentId<CityStateComponent>(), getComponentId<CitySunComponent>(),
                          getComponentId<CityCameraComponent>(), getComponentId<CityLampLightComponent>(),
                          getComponentId<CityCarComponent>(), getComponentId<CityPedestrianComponent>(),
                          getComponentId<CityDroneComponent>(), getComponentId<CityParcelComponent>()});
        entities.insert(entities.end(), rest.begin(), rest.end());
        return entities;
    }

}  

namespace spite::city
{
    const CityStateComponent* findCity(const World world)
    {
        for (const Chunk* chunk : world.getChunksContaining(Aspect(getComponentId<CityStateComponent>())))
        {
            if (!chunk->empty())
            {
                return &chunk->readColumn<CityStateComponent>()[0];
            }
        }
        return nullptr;
    }
}  

namespace
{

    Entity cityStateEntity(const World world)
    {
        for (const Chunk* chunk : world.getChunksContaining(Aspect(getComponentId<CityStateComponent>())))
        {
            if (!chunk->empty())
            {
                return chunk->entities()[0];
            }
        }
        return Entity::undefined();
    }

     
    CityDesign readDesign(const World world, const CityStateComponent& city)
    {
        CityDesign design;
        design.seed = city.seed;
        design.size = city.size;
        forEachRow<CityRoadComponent>(
            world, [&](const CityRoadComponent& road)
            { design.edges.push_back({road.x0, road.z0, road.x1, road.z1, road.roadClass, road.lanes}); });
        design.edges = edgesOf(cellsOf(design.size, design.edges));
        forEachRow<CityLotComponent>(world,
                                     [&](const CityLotComponent& lot)
                                     {
                                         design.lots.push_back({lot.id, lot.x0, lot.z0, lot.x1, lot.z1, lot.zone,
                                                                lot.special, lot.axis, lot.line});
                                     });
        std::ranges::sort(design.lots, {}, &DesignLot::id);
        forEachRow<CityBuildingComponent>(world,
                                          [&](const CityBuildingComponent& b)
                                          {
                                              design.buildings.push_back({.lot = b.lot,
                                                                          .zone = b.zone,
                                                                          .model = std::string(b.model.toView()),
                                                                          .x = b.x,
                                                                          .z = b.z,
                                                                          .halfX = b.halfX,
                                                                          .halfZ = b.halfZ,
                                                                          .rotation = b.rotation,
                                                                          .height = b.height,
                                                                          .color = b.dayColor,
                                                                          .dusk = b.dusk,
                                                                          .dawn = b.dawn,
                                                                          .windows = b.windows});
                                          });
        std::ranges::sort(design.buildings, {}, &DesignBuilding::lot);
        forEachRow<CityZoneOverrideComponent>(
            world, [&](const CityZoneOverrideComponent& zone)
            { design.overrides.push_back({zone.x0, zone.z0, zone.x1, zone.z1, zone.zone, zone.special}); });
        return design;
    }

     
     
    void replaceDesign(const World world, const CityDesign& design)
    {
        const Entity stateEntity = cityStateEntity(world);
        CityStateComponent& state = world.getComponent<CityStateComponent>(stateEntity);
        ++state.designVersion;
        const float t = timeOfDay(state, state.step);
        destroyCityEntities(world, designEntities(world));
        spawnDesign(world, design, *buildGraph(design.size, design.edges), t);
    }

     

    std::string designJson(const CityDesign& design, const LayoutParams& params, const int indent = -1)
    {
        JsonFragmentWriter out;
        out.writeU32("version", SAVE_VERSION);
        out.writeU64("seed", design.seed);
        out.writeU32("size", design.size);
        out.writeU32("cars", params.cars);
        out.writeU32("pedestrians", params.pedestrians);
        out.writeU32("people", params.people);
        out.writeU32("drones", params.drones);
        out.writeU32("trucks", params.trucks);
        out.writeU32("lampLights", params.lampLights);
        out.writeU32("daySteps", params.daySteps);
        out.writeF32("dayStart", params.dayStart);
        out.beginArray("edges", static_cast<u32>(design.edges.size()));
        for (const DesignEdge& edge : design.edges)
        {
            out.beginObject({});
            out.writeU32("x0", edge.x0);
            out.writeU32("z0", edge.z0);
            out.writeU32("x1", edge.x1);
            out.writeU32("z1", edge.z1);
            out.writeString("class", ROAD_CLASS_SPECS[std::min(edge.roadClass, ROAD_CLASSES - 1)].name);
            out.writeU32("lanes", edge.lanes);
            out.endObject();
        }
        out.endArray();
        out.beginArray("lots", static_cast<u32>(design.lots.size()));
        for (const DesignLot& lot : design.lots)
        {
            out.beginObject({});
            out.writeU32("id", lot.id);
            out.writeU32("x0", lot.x0);
            out.writeU32("z0", lot.z0);
            out.writeU32("x1", lot.x1);
            out.writeU32("z1", lot.z1);
            out.writeString("zone", ZONE_NAMES[std::min(lot.zone, ZONE_COUNT - 1)]);
            out.writeU32("special", lot.special);
            out.writeU32("axis", lot.axis);
            out.writeU32("line", lot.line);
            out.endObject();
        }
        out.endArray();
        out.beginArray("buildings", static_cast<u32>(design.buildings.size()));
        for (const DesignBuilding& building : design.buildings)
        {
            out.beginObject({});
            out.writeU32("lot", building.lot);
            out.writeString("zone", ZONE_NAMES[std::min(building.zone, ZONE_COUNT - 1)]);
            out.writeString("model", building.model);
            const float footprint[4] = {building.x, building.z, building.halfX, building.halfZ};
            out.writeFloats("footprint", footprint);
            out.writeU32("rotation", building.rotation);
            out.writeF32("height", building.height);
            const float color[4] = {building.color.x, building.color.y, building.color.z, building.color.w};
            out.writeFloats("color", color);
            out.writeF32("dusk", building.dusk);
            out.writeF32("dawn", building.dawn);
            out.writeU32("windows", building.windows);
            out.endObject();
        }
        out.endArray();
        out.beginArray("zoneOverrides", static_cast<u32>(design.overrides.size()));
        for (const ZoneOverride& zone : design.overrides)
        {
            out.beginObject({});
            out.writeU32("x0", zone.x0);
            out.writeU32("z0", zone.z0);
            out.writeU32("x1", zone.x1);
            out.writeU32("z1", zone.z1);
            out.writeString("zone", ZONE_NAMES[std::min(zone.zone, ZONE_COUNT - 1)]);
            out.writeU32("special", zone.special);
            out.endObject();
        }
        out.endArray();
        std::string text;
        out.dump(text, indent);
        return text;
    }

     
    std::string parseDesign(const std::string_view text, CityDesign& design, LayoutParams& params)
    {
        JsonFragmentReader in(text);
        if (!in.valid())
        {
            return "the design is not a JSON object";
        }
        u32 version = 0;
        if (in.readU32("version", version) != FieldRead::Ok || version != SAVE_VERSION)
        {
            return std::format("the design's version must be {}", SAVE_VERSION);
        }
        if (in.readU64("seed", design.seed) != FieldRead::Ok || in.readU32("size", design.size) != FieldRead::Ok ||
            design.size < MIN_SIZE || design.size > MAX_SIZE)
        {
            return std::format("the design needs a seed and a size of {}..{} cells", MIN_SIZE, MAX_SIZE);
        }
        in.readU32("cars", params.cars);
        in.readU32("pedestrians", params.pedestrians);
        in.readU32("people", params.people);
        if (in.readU32("drones", params.drones) != FieldRead::Ok)
        {
            in.readU32("aircraft", params.drones);  
        }
        params.trucks = AUTO_TRUCKS;
        in.readU32("trucks", params.trucks);
        in.readU32("lampLights", params.lampLights);
        in.readU32("daySteps", params.daySteps);
        in.readF32("dayStart", params.dayStart);
        params.cars = std::min(params.cars, MAX_CARS);
        params.pedestrians = std::min(params.pedestrians, MAX_PEDESTRIANS);
        params.drones = std::min(params.drones, MAX_DRONES);
        params.people = std::min(params.people, MAX_PEDESTRIANS);
        params.lampLights = std::min(params.lampLights, MAX_LAMP_LIGHTS);
        params.daySteps = std::max(params.daySteps, 1u);
        params.dayStart = params.dayStart - std::floor(params.dayStart);
        const auto each = [&](const char* name, const auto& fn) -> std::string
        {
            u32 count = 0;
            if (in.beginArray(name, count) != FieldRead::Ok)
            {
                return {};
            }
            for (u32 i = 0; i < count; ++i)
            {
                if (in.beginObject({}) != FieldRead::Ok)
                {
                    return std::format("{}[{}] is not an object", name, i);
                }
                const std::string error = fn();
                in.endObject();
                if (!error.empty())
                {
                    return std::format("{}[{}]: {}", name, i, error);
                }
            }
            in.endArray();
            return {};
        };
        const auto zoneField = [&](u32& zone)
        {
            std::string name;
            in.readString("zone", name);
            zone = zoneOf(name);
            return zone < ZONE_COUNT ? std::string{} : std::format("unknown zone '{}'", name);
        };
        std::vector<DesignEdge> edges;
        std::string error = each("edges",
                                 [&]
                                 {
                                     DesignEdge edge;
                                     std::string cls;
                                     in.readU32("x0", edge.x0);
                                     in.readU32("z0", edge.z0);
                                     in.readU32("x1", edge.x1);
                                     in.readU32("z1", edge.z1);
                                     in.readString("class", cls);
                                     in.readU32("lanes", edge.lanes);
                                     edge.roadClass = roadClassOf(cls);
                                     if (edge.roadClass >= ROAD_CLASSES)
                                     {
                                         return std::format("unknown class '{}'", cls);
                                     }
                                     if ((edge.x0 == edge.x1) == (edge.z0 == edge.z1) || edge.x0 > edge.x1 ||
                                         edge.z0 > edge.z1 || edge.x1 > design.size || edge.z1 > design.size)
                                     {
                                         return std::string("not an axis-aligned edge on the lattice");
                                     }
                                     edge.lanes = clampLanes(edge.lanes);
                                     edges.push_back(edge);
                                     return std::string{};
                                 });
        design.edges = edgesOf(cellsOf(design.size, edges));
        if (error.empty())
        {
            error = each("lots",
                         [&]
                         {
                             DesignLot lot;
                             in.readU32("id", lot.id);
                             in.readU32("x0", lot.x0);
                             in.readU32("z0", lot.z0);
                             in.readU32("x1", lot.x1);
                             in.readU32("z1", lot.z1);
                             in.readU32("special", lot.special);
                             in.readU32("axis", lot.axis);
                             in.readU32("line", lot.line);
                             if (lot.x0 >= lot.x1 || lot.z0 >= lot.z1 || lot.x1 > design.size || lot.z1 > design.size ||
                                 lot.axis > 1)
                             {
                                 return std::string("not a lot on the lattice");
                             }
                             std::string zoneError = zoneField(lot.zone);
                             design.lots.push_back(lot);
                             return zoneError;
                         });
        }
        if (error.empty())
        {
            error = each("buildings",
                         [&]
                         {
                             DesignBuilding building;
                             float footprint[4] = {};
                             float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                             in.readU32("lot", building.lot);
                             in.readString("model", building.model);
                             in.readFloats("footprint", footprint);
                             in.readU32("rotation", building.rotation);
                             in.readF32("height", building.height);
                             in.readFloats("color", color);
                             in.readF32("dusk", building.dusk);
                             in.readF32("dawn", building.dawn);
                             in.readU32("windows", building.windows);
                             building.x = footprint[0];
                             building.z = footprint[1];
                             building.halfX = footprint[2];
                             building.halfZ = footprint[3];
                             building.rotation &= 3;
                             building.color = {color[0], color[1], color[2], color[3]};
                             std::string zoneError = zoneField(building.zone);
                             design.buildings.push_back(std::move(building));
                             return zoneError;
                         });
        }
        if (error.empty())
        {
            error = each("zoneOverrides",
                         [&]
                         {
                             ZoneOverride zone;
                             in.readU32("x0", zone.x0);
                             in.readU32("z0", zone.z0);
                             in.readU32("x1", zone.x1);
                             in.readU32("z1", zone.z1);
                             in.readU32("special", zone.special);
                             zone.special = zone.special < SPECIAL_KINDS ? zone.special : SPECIAL_NONE;
                             std::string zoneError = zoneField(zone.zone);
                             design.overrides.push_back(zone);
                             return zoneError;
                         });
        }
        std::ranges::sort(design.lots, {}, &DesignLot::id);
        std::ranges::sort(design.buildings, {}, &DesignBuilding::lot);
        return error;
    }

}  

namespace spite::city
{
    std::string clockText(const float t)
    {
        const u32 minutes = static_cast<u32>(t * 24.0f * 60.0f) % (24 * 60);
        return std::format("{:02}:{:02}", minutes / 60, minutes % 60);
    }

    const World* statusWorld(ControlServer& control, const ControlArgs& args)
    {
        const World* world = args.string("world") ? control.world(args) : control.world("play");
        return world ? world : control.world(args);
    }

    std::shared_ptr<const model::CityMap> worldMap(const World world, const CityStateComponent& city)
    {
        if (world.services().contains<PathService>())
        {
            const std::shared_ptr<const model::CityMap>& kept = world.services().read<PathService>()->map();
            if (kept && world.services().read<PathService>()->mapVersion() == city.designVersion &&
                kept->seed() == city.seed && kept->size() == city.size)
            {
                return kept;
            }
        }
        const model::CityDesign design = readDesign(world, city);
        return std::make_shared<const model::CityMap>(city.seed, city.size, model::buildGraph(city.size, design.edges),
                                                      design.lots);
    }
}  

namespace
{

    bool validName(const std::string_view name)
    {
        return !name.empty() && name.size() <= 64 &&
            std::ranges::all_of(name, [](const char c)
                                { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; });
    }

    std::filesystem::path savePath(const std::string_view name)
    {
        return savesDirectory() / "city" / (std::string(name) + ".json");
    }

    void writeDesignCounts(const World world, IFormatWriter& out)
    {
        out.writeU32("edges", countOf<CityRoadComponent>(world));
        out.writeU32("junctions", countOf<CityJunctionComponent>(world));
        out.writeU32("lots", countOf<CityLotComponent>(world));
        out.writeU32("buildings", countOf<CityBuildingComponent>(world));
    }

     
     
    std::string rebuildCity(ControlServer& control, const World world, const CityDesign& design, LayoutParams params,
                            IFormatWriter& out)
    {
        const CityStateComponent* city = findCity(world);
        u32 version = 0;
        if (city)
        {
            control.recordInverse(
                APPLY_DESIGN_COMMAND,
                std::format(R"({{"design":{}}})", designJson(readDesign(world, *city), paramsOf(*city))));
            version = city->designVersion + 1;
        }
        const std::shared_ptr<const RoadGraph> graph = buildGraph(design.size, design.edges);
        params.cars = std::min(params.cars, maxCars(*graph));
        destroyCityEntities(world, cityEntities(world));
        const Entity camera = authorCity(world, design, params, version);
        out.writeU64("seed", design.seed);
        out.writeU32("size", design.size);
        writeDesignCounts(world, out);
        out.writeU32("cars", countOf<CityCarComponent>(world));
        out.writeU32("pedestrians", countOf<CityPedestrianComponent>(world));
        out.writeU32("drones", countOf<CityDroneComponent>(world));
        out.writeU32("trucks", findCity(world) ? findCity(world)->trucks : 0u);
        out.writeEntity("camera", camera);
        return {};
    }

     
     
    template <typename Edit>
    std::string editDesign(ControlServer& control, const ControlArgs& args, IFormatWriter& out, Edit&& edit)
    {
        const World* world = control.world(args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const CityStateComponent* city = findCity(*world);
        if (!city)
        {
            return NO_CITY;
        }
        CityDesign design = readDesign(*world, *city);
        const std::string before = designJson(design, paramsOf(*city));
        if (std::string error = edit(design); !error.empty())
        {
            return error;
        }
        replaceDesign(*world, design);
        control.recordInverse(APPLY_DESIGN_COMMAND, std::format(R"({{"design":{}}})", before));
        writeDesignCounts(*world, out);
        return {};
    }

     
    bool readCoords(const ControlArgs& args, const std::initializer_list<const char*> names, const u32 limit,
                    u32* values)
    {
        u32 i = 0;
        for (const char* name : names)
        {
            const std::optional<u64> value = args.integer(name);
            if (!value || *value > limit)
            {
                return false;
            }
            values[i++] = static_cast<u32>(*value);
        }
        return true;
    }

     
     
    void enableCrowdLod(ControlServer& control, const std::optional<std::string>& world)
    {
        const std::string reply =
            control.handle(std::format(R"({{"id":0,"command":"setAnimationLod","args":{{"enabled":true{}}}}})",
                                       world ? std::format(R"(,"world":"{}")", *world) : std::string()));
        if (reply.find(R"("ok":true)") == std::string::npos)
        {
            SWARNING_LOG("CityShowcase: the animation LOD stays off: {}", reply);
        }
    }

     
    std::string layoutCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = control.world(args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const auto read = [&args](const char* name, const u64 fallback) -> std::optional<u64>
        { return args.json(name) ? args.integer(name) : std::optional<u64>(fallback); };
        const std::optional<u64> seed = read("seed", 1);
        const std::optional<u64> size = read("size", DEFAULT_SIZE);
        const std::optional<u64> cars = read("cars", DEFAULT_CARS);
        const std::optional<u64> pedestrians = read("pedestrians", DEFAULT_PEDESTRIANS);
        const std::optional<u64> people = read("people", DEFAULT_PEOPLE);
        const std::optional<u64> drones = read("drones", DEFAULT_DRONES);
        const std::optional<u64> trucks = read("trucks", AUTO_TRUCKS);
        const std::optional<u64> lampLights = read("lampLights", DEFAULT_LAMP_LIGHTS);
        const std::optional<u64> hour = read("hour", DEFAULT_HOUR);
        const std::optional<u64> daySeconds = read("daySeconds", DEFAULT_DAY_SECONDS);
        const std::optional<u64> replace = read("replace", 0);
        if (!seed)
        {
            return "seed must be an integer";
        }
        if (!size || *size < MIN_SIZE || *size > MAX_SIZE)
        {
            return std::format("size must be {}..{} cells", MIN_SIZE, MAX_SIZE);
        }
        if (!pedestrians || *pedestrians > MAX_PEDESTRIANS)
        {
            return std::format("pedestrians must be 0..{}", MAX_PEDESTRIANS);
        }
        if (!people || *people > MAX_PEDESTRIANS)
        {
            return std::format("people must be 0..{}", MAX_PEDESTRIANS);
        }
        if (!drones || *drones > MAX_DRONES)
        {
            return std::format("drones must be 0..{}", MAX_DRONES);
        }
        if (!lampLights || *lampLights > MAX_LAMP_LIGHTS)
        {
            return std::format("lampLights must be 0..{}", MAX_LAMP_LIGHTS);
        }
        if (!hour || *hour > 23)
        {
            return "hour must be 0..23";
        }
        if (!daySeconds || *daySeconds < 1 || *daySeconds > 86400)
        {
            return "daySeconds must be 1..86400";
        }
        if (!replace || *replace > 1)
        {
            return "replace must be 0 or 1";
        }
        const std::vector<Entity> existing = cityEntities(*world);
        if (!existing.empty() && *replace == 0)
        {
            return std::format("the world has a city already ({} entities): run cityLayout with replace=1",
                               existing.size());
        }
        const CityDesign design = generateDesign(*seed, static_cast<u32>(*size));
        const u32 carLimit = std::min(MAX_CARS, maxCars(*buildGraph(design.size, design.edges)));
        if (!cars || *cars > carLimit)
        {
            return std::format("cars must be 0..{} (the roads of this city hold at most {})", carLimit, carLimit);
        }
        if (!trucks || (*trucks != AUTO_TRUCKS && *trucks > *cars))
        {
            return "trucks must be 0..cars";
        }
        destroyCityEntities(*world, existing);
        const Entity camera = authorCity(*world, design,
                                         {.cars = static_cast<u32>(*cars),
                                          .pedestrians = static_cast<u32>(*pedestrians),
                                          .people = static_cast<u32>(*people),
                                          .drones = static_cast<u32>(*drones),
                                          .trucks = static_cast<u32>(*trucks),
                                          .lampLights = static_cast<u32>(*lampLights),
                                          .daySteps = static_cast<u32>(*daySeconds) * STEPS_PER_SECOND,
                                          .dayStart = static_cast<float>(*hour) / 24.0f},
                                         0);
        out.writeU64("seed", *seed);
        out.writeU32("size", static_cast<u32>(*size));
        writeDesignCounts(*world, out);
        out.writeU32("cars", countOf<CityCarComponent>(*world));
        out.writeU32("pedestrians", countOf<CityPedestrianComponent>(*world));
        out.writeU32("drones", countOf<CityDroneComponent>(*world));
        out.writeU32("trucks", findCity(*world) ? findCity(*world)->trucks : 0u);
        enableCrowdLod(control, args.string("world"));
        const std::vector<Entity> authored = cityEntities(*world);
        out.writeU32("entities", static_cast<u32>(authored.size()));
        out.writeEntity("camera", camera);
        control.auditLayout(*world, authored, out);
        return {};
    }

     
    std::string generateCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = control.world(args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const CityStateComponent* city = findCity(*world);
        if (!city)
        {
            return NO_CITY;
        }
        const std::optional<u64> seed = args.json("seed") ? args.integer("seed") : std::optional<u64>(city->seed);
        const std::optional<u64> size = args.json("size") ? args.integer("size") : std::optional<u64>(city->size);
        if (!seed)
        {
            return "seed must be an integer";
        }
        if (!size || *size < MIN_SIZE || *size > MAX_SIZE)
        {
            return std::format("size must be {}..{} cells", MIN_SIZE, MAX_SIZE);
        }
        return rebuildCity(control, *world, generateDesign(*seed, static_cast<u32>(*size)), paramsOf(*city), out);
    }

     
    std::string applyDesignCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = control.world(args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const std::optional<std::string> text = args.json("design");
        if (!text)
        {
            return "design must be a saved design object";
        }
        CityDesign design;
        const CityStateComponent* city = findCity(*world);
        LayoutParams params = city ? paramsOf(*city) : LayoutParams{};
        if (std::string error = parseDesign(*text, design, params); !error.empty())
        {
            return error;
        }
        if (city && city->seed == design.seed && city->size == design.size && city->cars == params.cars &&
            city->pedestrians == params.pedestrians && city->drones == params.drones &&
            (params.trucks == AUTO_TRUCKS || city->trucks == params.trucks))
        {
            control.recordInverse(
                APPLY_DESIGN_COMMAND,
                std::format(R"({{"design":{}}})", designJson(readDesign(*world, *city), paramsOf(*city))));
            replaceDesign(*world, design);
            writeDesignCounts(*world, out);
            return {};
        }
        return rebuildCity(control, *world, design, params, out);
    }

    std::string buildRoadCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        return editDesign(control, args, out,
                          [&](CityDesign& design)
                          {
                              u32 at[4] = {};
                              if (!readCoords(args, {"x0", "z0", "x1", "z1"}, design.size, at))
                              {
                                  return std::format("x0 z0 x1 z1 must be lattice points 0..{}", design.size);
                              }
                              const u32 roadClass = roadClassOf(args.string("class").value_or(""));
                              return buildRoad(design, at[0], at[1], at[2], at[3], roadClass);
                          });
    }

    std::string removeRoadCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        DesignEdge removed;
        std::string error = editDesign(control, args, out,
                                       [&](CityDesign& design)
                                       {
                                           u32 at[2] = {};
                                           if (!readCoords(args, {"x", "z"}, design.size, at))
                                           {
                                               return std::format("x z must be a lattice point 0..{}", design.size);
                                           }
                                           return removeRoad(design, at[0], at[1], &removed);
                                       });
        if (error.empty())
        {
            out.beginObject("removed");
            out.writeU32("x0", removed.x0);
            out.writeU32("z0", removed.z0);
            out.writeU32("x1", removed.x1);
            out.writeU32("z1", removed.z1);
            out.writeString("class", ROAD_CLASS_SPECS[removed.roadClass].name);
            out.endObject();
        }
        return error;
    }

    std::string roadLanesCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        u32 previous = 0;
        std::string error =
            editDesign(control, args, out,
                       [&](CityDesign& design)
                       {
                           u32 at[2] = {};
                           const std::optional<u64> lanes = args.integer("lanes");
                           if (!readCoords(args, {"x", "z"}, design.size, at))
                           {
                               return std::format("x z must be a lattice point 0..{}", design.size);
                           }
                           if (!lanes || *lanes < 1 || *lanes > MAX_LANES)
                           {
                               return std::format("lanes must be 1..{}", MAX_LANES);
                           }
                           return setRoadLanes(design, at[0], at[1], static_cast<u32>(*lanes), previous);
                       });
        if (error.empty())
        {
            out.writeU32("previous", previous);
        }
        return error;
    }

    std::string zoneCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        u32 changed = 0;
        std::string error = editDesign(
            control, args, out,
            [&](CityDesign& design)
            {
                u32 at[4] = {};
                if (!readCoords(args, {"x0", "z0", "x1", "z1"}, design.size, at) || at[0] >= at[2] || at[1] >= at[3])
                {
                    return std::format("x0 < x1 and z0 < z1 must be cells 0..{}", design.size);
                }
                 
                const std::string name = args.string("zone").value_or("");
                u32 zone = zoneOf(name);
                u32 special = SPECIAL_NONE;
                for (u32 kind = SPECIAL_DRONE_HUB; kind < SPECIAL_KINDS; ++kind)
                {
                    if (name == SPECIAL_ZONE_NAMES[kind])
                    {
                        special = kind;
                        zone =
                            kind == SPECIAL_DRONE_HUB || kind == SPECIAL_TRUCK_DEPOT ? ZONE_SPECIAL : ZONE_INDUSTRIAL;
                    }
                }
                if (zone >= ZONE_COUNT)
                {
                    return std::string("unknown zone");
                }
                changed = zoneRect(design, at[0], at[1], at[2], at[3], zone, special);
                return std::string{};
            });
        if (error.empty())
        {
            out.writeU32("rezoned", changed);
        }
        return error;
    }

    std::string bulldozeCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        u32 removed = 0;
        std::string error = editDesign(
            control, args, out,
            [&](CityDesign& design)
            {
                u32 at[4] = {};
                if (!readCoords(args, {"x0", "z0", "x1", "z1"}, design.size, at) || at[0] >= at[2] || at[1] >= at[3])
                {
                    return std::format("x0 < x1 and z0 < z1 must be cells 0..{}", design.size);
                }
                removed = bulldoze(design, at[0], at[1], at[2], at[3]);
                return std::string{};
            });
        if (error.empty())
        {
            out.writeU32("bulldozed", removed);
        }
        return error;
    }

    std::string saveCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = statusWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const CityStateComponent* city = findCity(*world);
        if (!city)
        {
            return NO_CITY;
        }
        const std::string name = args.string("name").value_or("");
        if (!validName(name))
        {
            return "name must be 1..64 letters, digits, '_' or '-'";
        }
        const std::filesystem::path path = savePath(name);
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        const std::string text = designJson(readDesign(*world, *city), paramsOf(*city), 1);
        file << text;
        if (!file)
        {
            return std::format("could not write {}", path.string());
        }
        out.writeString("path", path.generic_string());
        out.writeU32("bytes", static_cast<u32>(text.size()));
        writeDesignCounts(*world, out);
        return {};
    }

    std::string loadCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = control.world(args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const std::string name = args.string("name").value_or("");
        if (!validName(name))
        {
            return "name must be 1..64 letters, digits, '_' or '-'";
        }
        std::ifstream file(savePath(name), std::ios::binary);
        if (!file)
        {
            return std::format("no saved city '{}' ({})", name, savePath(name).generic_string());
        }
        std::stringstream text;
        text << file.rdbuf();
        CityDesign design;
        LayoutParams params;
        if (std::string error = parseDesign(text.str(), design, params); !error.empty())
        {
            return std::format("{}: {}", name, error);
        }
        return rebuildCity(control, *world, design, params, out);
    }

     
    std::string roadsQuery(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = statusWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const CityStateComponent* city = findCity(*world);
        if (!city)
        {
            return NO_CITY;
        }
        const std::shared_ptr<const CityMap> map = worldMap(*world, *city);
        const RoadGraph& graph = map->graph();
        u32 perClass[ROAD_CLASSES] = {};
        double km[ROAD_CLASSES] = {};
        u32 signals = 0;
        u32 deadEnds = 0;
        for (const GraphEdge& edge : graph.edges)
        {
            ++perClass[edge.roadClass];
            km[edge.roadClass] += edge.length() / 1000.0;
        }
        for (const GraphNode& node : graph.nodes)
        {
            signals += node.signal ? 1 : 0;
            deadEnds += node.degree == 1 ? 1 : 0;
        }
        out.writeU32("size", graph.size);
        out.writeU32("nodes", static_cast<u32>(graph.nodes.size()));
        out.writeU32("edges", static_cast<u32>(graph.edges.size()));
        out.writeU32("signals", signals);
        out.writeU32("deadEnds", deadEnds);
        out.beginObject("perClass");
        for (u32 cls = 0; cls < ROAD_CLASSES; ++cls)
        {
            out.writeU32(ROAD_CLASS_SPECS[cls].name, perClass[cls]);
        }
        out.endObject();
        out.beginObject("km");
        for (u32 cls = 0; cls < ROAD_CLASSES; ++cls)
        {
            out.writeF64(ROAD_CLASS_SPECS[cls].name, km[cls]);
        }
        out.endObject();
        if (args.json("x") || args.json("z"))
        {
            u32 at[2] = {};
            if (!readCoords(args, {"x", "z"}, graph.size, at))
            {
                return std::format("x z must be a lattice point 0..{}", graph.size);
            }
            const RoadAt road = roadAt(graph, at[0], at[1]);
            out.beginObject("at");
            if (road.node != NO_ID)
            {
                const GraphNode& n = graph.nodes[road.node];
                out.writeString("kind", "junction");
                out.writeU32("x", n.x);
                out.writeU32("z", n.z);
                out.writeU32("degree", n.degree);
                out.writeBool("signal", n.signal);
            }
            else if (road.edge != NO_ID)
            {
                const GraphEdge& edge = graph.edges[road.edge];
                out.writeString("kind", "edge");
                out.writeU32("x0", graph.nodes[edge.a].x);
                out.writeU32("z0", graph.nodes[edge.a].z);
                out.writeU32("x1", graph.nodes[edge.b].x);
                out.writeU32("z1", graph.nodes[edge.b].z);
                out.writeString("class", ROAD_CLASS_SPECS[edge.roadClass].name);
                out.writeU32("lanes", edge.lanes);
                out.writeF32("length", edge.length());
            }
            else
            {
                out.writeString("kind", "none");
            }
            out.endObject();
        }
        return {};
    }

     
    std::string lotsQuery(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = statusWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const CityStateComponent* city = findCity(*world);
        if (!city)
        {
            return NO_CITY;
        }
        const std::shared_ptr<const CityMap> map = worldMap(*world, *city);
        u32 lots[ZONE_COUNT] = {};
        u32 buildings[ZONE_COUNT] = {};
        u32 special[5] = {};
        u32 cutOff = 0;
        for (const LotRef& lot : map->lots())
        {
            ++lots[std::min(lot.zone, ZONE_COUNT - 1)];
            cutOff += lot.edge == NO_ID ? 1 : 0;
        }
        forEachRow<CityLotComponent>(*world,
                                     [&](const CityLotComponent& lot) { ++special[std::min(lot.special, 4u)]; });
        forEachRow<CityBuildingComponent>(*world, [&](const CityBuildingComponent& b)
                                          { ++buildings[std::min(b.zone, ZONE_COUNT - 1)]; });
        out.writeU32("lots", static_cast<u32>(map->lots().size()));
        out.writeU32("buildings", countOf<CityBuildingComponent>(*world));
        out.writeU32("cutOff", cutOff);
        const auto perZone = [&out](const char* name, const u32(&counts)[ZONE_COUNT])
        {
            out.beginObject(name);
            for (u32 zone = 0; zone < ZONE_COUNT; ++zone)
            {
                out.writeU32(ZONE_NAMES[zone], counts[zone]);
            }
            out.endObject();
        };
        perZone("lotsPerZone", lots);
        perZone("buildingsPerZone", buildings);
        out.beginObject("special");
        out.writeU32("droneHubs", special[SPECIAL_DRONE_HUB]);
        out.writeU32("truckDepots", special[SPECIAL_TRUCK_DEPOT]);
        out.writeU32("warehouses", special[SPECIAL_WAREHOUSE]);
        out.writeU32("factories", special[SPECIAL_FACTORY]);
        out.endObject();
        if (args.json("x") || args.json("z"))
        {
            u32 at[2] = {};
            if (!readCoords(args, {"x", "z"}, city->size - 1, at))
            {
                return std::format("x z must be a cell 0..{}", city->size - 1);
            }
            out.beginObject("at");
            bool found = false;
            forEachRow<CityLotComponent>(
                *world,
                [&](const CityLotComponent& lot)
                {
                    if (found || at[0] < lot.x0 || at[0] >= lot.x1 || at[1] < lot.z0 || at[1] >= lot.z1)
                    {
                        return;
                    }
                    found = true;
                    out.writeU32("id", lot.id);
                    out.writeU32("x0", lot.x0);
                    out.writeU32("z0", lot.z0);
                    out.writeU32("x1", lot.x1);
                    out.writeU32("z1", lot.z1);
                    out.writeString("zone", ZONE_NAMES[std::min(lot.zone, ZONE_COUNT - 1)]);
                    out.writeU32("special", lot.special);
                    out.writeBool("reachable", map->reachable(lot.id) != nullptr);
                    bool built = false;
                    forEachRow<CityBuildingComponent>(*world,
                                                      [&](const CityBuildingComponent& b)
                                                      {
                                                          if (!built && b.lot == lot.id)
                                                          {
                                                              built = true;
                                                              out.beginObject("building");
                                                              out.writeString("model", b.model.toView());
                                                              out.writeF32("height", b.height);
                                                              out.writeU32("rotation", b.rotation);
                                                              out.endObject();
                                                          }
                                                      });
                });
            if (!found)
            {
                out.writeString("kind", "none");
            }
            out.endObject();
        }
        return {};
    }

    std::string statusQuery(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = statusWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const CityStateComponent* city = findCity(*world);
        if (!city)
        {
            return NO_CITY;
        }
        const u32 cars = countOf<CityCarComponent>(*world);
        const u32 pedestrians = countOf<CityPedestrianComponent>(*world);
        const u32 drones = countOf<CityDroneComponent>(*world);
        out.writeU64("seed", city->seed);
        out.writeU32("size", city->size);
        out.writeU32("designVersion", city->designVersion);
        writeDesignCounts(*world, out);
        out.writeU32("cars", cars);
        out.writeU32("pedestrians", pedestrians);
        out.writeU32("trucks", city->trucks);
        out.writeU32("drones", drones);
        out.writeU32("parcels", countOf<CityParcelComponent>(*world));
        out.writeU32("laneStrips", countOf<CityLaneComponent>(*world));
        u32 lampCount = 0;
        forEachRow<CityLampComponent>(*world,
                                      [&](const CityLampComponent& lamp) { lampCount += lamp.glow == 0 ? 1 : 0; });
        out.writeU32("lamps", lampCount);
        out.writeU32("lampLights", countOf<CityLampLightComponent>(*world));
        out.writeU32("people", drawnPeople(*city));
        out.writeU32("pieces", countOf<CityPieceComponent>(*world));
        out.writeU32("models", city->models);
        out.writeU32("moving", cars + pedestrians + drones);
        out.writeU32("entities", static_cast<u32>(cityEntities(*world).size()));
        out.writeU32("step", city->step);
        out.writeU32("speed", city->speed);
        out.writeU32("signalSteps", city->signalSteps);
        out.writeString("clock", clockText(timeOfDay(*city, city->step)));
        return {};
    }

     
    std::string trafficQuery(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = statusWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const CityStateComponent* city = findCity(*world);
        if (!city)
        {
            return NO_CITY;
        }
        u32 driving = 0, moving = 0, parked = 0, awaiting = 0, offStreet = 0, trips = 0, turns = 0, redRuns = 0;
        u32 bays = 0, baysTaken = 0;
        double speeds = 0.0;
        const std::shared_ptr<const CityMap> map = worldMap(*world, *city);
        const RoadGraph& graph = map->graph();
        RoadLoadCounter counter(graph);
         
         
        const bool box = args.json("x") && args.json("z") && args.json("cells");
        const long long boxX = static_cast<long long>(args.integer("x").value_or(0));
        const long long boxZ = static_cast<long long>(args.integer("z").value_or(0));
        const long long boxCells = static_cast<long long>(args.integer("cells").value_or(0));
        const auto inBox = [&](const u32 axis, const u32 line, const float pos)
        {
            const long long cx = static_cast<long long>(axis == 0 ? pos / CELL : static_cast<float>(line));
            const long long cz = static_cast<long long>(axis == 0 ? static_cast<float>(line) : pos / CELL);
            return !box || (std::llabs(cx - boxX) <= boxCells && std::llabs(cz - boxZ) <= boxCells);
        };
        for (const GraphEdge& edge : graph.edges)
        {
            const BayRow row = bayRow(graph, edge);
            bays += row.count > 0 && inBox(edge.axis, edge.line, row.low) ? 2 * row.count : 0;
        }
        forEachRow<CityCarComponent>(*world,
                                     [&](const CityCarComponent& car)
                                     {
                                         counter.add(car);
                                         u32 key = 0;
                                         u64 bit = 0;
                                         baysTaken += car.axis <= 1 && car.state != CAR_DRIVING && !garaged(car) &&
                                                 inBox(car.axis, car.line, car.pos) &&
                                                 curbBits(graph, car.axis, car.line, car.pos, car.dir, key, bit)
                                             ? 1
                                             : 0;
                                         trips += car.trips;
                                         turns += car.turns;
                                         redRuns += car.redRuns;
                                         if (car.state != CAR_DRIVING)
                                         {
                                             ++(car.state == CAR_PARKED ? parked : awaiting);
                                             offStreet += garaged(car) ? 1 : 0;
                                             return;
                                         }
                                         ++driving;
                                         moving += car.speed > 0.0f ? 1 : 0;
                                         speeds += car.speed;
                                     });
        u32 levels[CONGESTION_LEVELS] = {};
        u32 busiest = 0;
        forEachRow<CityLaneComponent>(*world,
                                      [&](const CityLaneComponent& lane)
                                      {
                                          ++levels[std::min(lane.level, CONGESTION_LEVELS - 1)];
                                          busiest = std::max(busiest, lane.cars);
                                      });
        out.writeU32("driving", driving);
        out.writeU32("moving", moving);
        out.writeU32("queued", driving - moving);
        out.writeU32("parked", parked);
        out.writeU32("offStreet", offStreet);
        out.writeU32("bays", bays);
        out.writeU32("baysTaken", baysTaken);
        out.writeU32("awaitingRoute", awaiting);
        out.writeU32("trips", trips);
        out.writeU32("intersections", turns);
        out.writeU32("redRuns", redRuns);
        out.writeU32("boxConflicts", city->boxConflicts);
        out.writeU32("boxHeld", city->boxHeld);
        out.writeU32("laneChanges", city->laneChanges);
        out.writeF32("meanSpeed", driving > 0 ? static_cast<float>(speeds / driving) : 0.0f);
        out.writeU32("segmentsFree", levels[0]);
        out.writeU32("segmentsBusy", levels[1]);
        out.writeU32("segmentsHeavy", levels[2]);
        out.writeU32("segmentsJammed", levels[3]);
        out.writeU32("busiestSegmentCars", busiest);
        const RoadLoad load = counter.load();
        out.writeU32("carsOnRoads", load.cars);
        out.writeF64("roadCapacity", load.capacity);
        out.writeF64("occupancy", load.occupancy());
        out.writeU32("directionsFree", load.levels[0]);
        out.writeU32("directionsBusy", load.levels[1]);
        out.writeU32("directionsHeavy", load.levels[2]);
        out.writeU32("directionsJammed", load.levels[3]);
        out.writeU32("busiestDirectionCars", load.busiest);
        return {};
    }

     
    std::string pedestriansQuery(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = statusWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const CityStateComponent* city = findCity(*world);
        if (!city)
        {
            return NO_CITY;
        }
        const std::shared_ptr<const CityMap> map = worldMap(*world, *city);
        const RoadGraph& graph = map->graph();
        u32 modes[6] = {}, crossings = 0, unsafe = 0, outdoors[PED_PURPOSES] = {}, near = 0;
        const bool box = args.json("x") && args.json("z") && args.json("cells");
        const long long boxX = static_cast<long long>(args.integer("x").value_or(0));
        const long long boxZ = static_cast<long long>(args.integer("z").value_or(0));
        const long long boxCells = static_cast<long long>(args.integer("cells").value_or(0));
        forEachRow<CityPedestrianComponent>(
            *world,
            [&](const CityPedestrianComponent& ped)
            {
                ++modes[std::min(ped.mode, 5u)];
                outdoors[std::min(ped.purpose, PED_PURPOSES - 1)] += pedestrianIndoors(ped) ? 0 : 1;
                near += box && !pedestrianIndoors(ped) &&
                        std::llabs(static_cast<long long>(ped.x) - boxX) <= boxCells &&
                        std::llabs(static_cast<long long>(ped.z) - boxZ) <= boxCells
                    ? 1
                    : 0;
                crossings += ped.crossings;
                if (ped.mode == PED_CROSSING)
                {
                    const u32 node = graph.nodeAt(ped.x, ped.z);
                    u32 crossed = NO_ID;
                    if (walkKind(graph, node, ped.corner & 3, graph.nodeAt(ped.toX, ped.toZ), ped.toCorner & 3,
                                 &crossed) == WALK_CROSS &&
                        graph.nodes[node].signal)
                    {
                        unsafe += greenAxis(*city, ped.x, ped.z, city->step) == graph.edges[crossed].axis ? 1 : 0;
                    }
                }
            });
        out.writeU32("walking", modes[PED_WALKING]);
        out.writeU32("waiting", modes[PED_WAITING]);
        out.writeU32("median", modes[PED_MEDIAN]);
        out.writeU32("crossing", modes[PED_CROSSING]);
        out.writeU32("dwelling", modes[PED_DWELLING]);
        out.writeU32("awaitingRoute", modes[PED_AWAITING_ROUTE]);
        out.writeU32("crossings", crossings);
        out.writeU32("unsafe", unsafe);
        u32 outdoorsTotal = 0;
        out.beginObject("outdoorsByWalk");
        for (u32 purpose = 0; purpose < PED_PURPOSES; ++purpose)
        {
            out.writeU32(PED_PURPOSE_NAMES[purpose], outdoors[purpose]);
            outdoorsTotal += outdoors[purpose];
        }
        out.endObject();
        out.writeU32("outdoors", outdoorsTotal);
        if (box)
        {
            out.writeU32("outdoorsNear", near);
        }
        const World avatarWorld = *world;
        if (avatarWorld.services().contains<CityAvatarPool>())
        {
            const CityAvatarPool& pool = avatarWorld.services().get<CityAvatarPool>();
            out.writeU32("avatars", static_cast<u32>(pool.avatars.size()));
            out.writeU32("avatarsInUse", pool.assigned);
            out.writeU32("avatarsAcquired", pool.acquired);
            out.writeU32("avatarsReleased", pool.released);
            out.writeU32("nearWithoutAvatar", pool.nearBoxes);
        }
        return {};
    }

     
    std::string dayQuery(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = statusWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const CityStateComponent* city = findCity(*world);
        if (!city)
        {
            return NO_CITY;
        }
        const float t = timeOfDay(*city, city->step);
        u32 lights[3] = {}, lampsOn = 0, stale = 0;
        u32 nightKinds[5] = {};
        forEachRow<CityBuildingComponent>(*world,
                                          [&](const CityBuildingComponent& building)
                                          {
                                              ++lights[std::min(building.light, 2u)];
                                              stale += building.light != buildingLight(building, t) ? 1 : 0;
                                          });
        forEachRow<CityLampComponent>(*world,
                                      [&](const CityLampComponent& lamp)
                                      {
                                          if (lamp.glow != 0)
                                          {
                                              return;  
                                          }
                                          lampsOn += lamp.on;
                                          nightKinds[std::min(lamp.kind, NIGHT_FACADE)] += lamp.on;
                                          stale += lamp.on != (inWindow(t, lamp.onAt, lamp.offAt) ? 1u : 0u);
                                      });
        u32 sun = 0;
        forEachRow<CitySunComponent>(*world,
                                     [&](const CitySunComponent& row) { sun = row.moon == 0 ? row.level : sun; });
        const char* period = t < 0.22f ? "night" : t < 0.3f ? "dawn" : t < 0.72f ? "day" : t < 0.82f ? "dusk" : "night";
        out.writeF32("timeOfDay", t);
        out.writeString("clock", clockText(t));
        out.writeString("period", period);
        out.writeU32("daySteps", city->daySteps);
        out.writeU32("buildingsDay", lights[LIGHT_DAY]);
        out.writeU32("buildingsDark", lights[LIGHT_DARK]);
        out.writeU32("buildingsLit", lights[LIGHT_LIT]);
        out.writeU32("lampsOn", lampsOn);
        out.writeU32("streetLightsOn", nightKinds[NIGHT_STREET]);
        out.writeU32("entranceLightsOn", nightKinds[NIGHT_ENTRANCE]);
        out.writeU32("shopLightsOn", nightKinds[NIGHT_SHOP]);
        out.writeU32("yardLightsOn", nightKinds[NIGHT_YARD]);
        out.writeU32("facadeLightsOn", nightKinds[NIGHT_FACADE]);
        out.writeU32("sunLevel", sun);
        out.writeU32("stale", stale);
        return {};
    }

     
    std::string zonesQuery(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = statusWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const CityStateComponent* city = findCity(*world);
        if (!city)
        {
            return NO_CITY;
        }
        const std::shared_ptr<const CityMap> map = worldMap(*world, *city);
        const auto zoneOfLot = [&map](const u32 id)
        {
            const LotRef* lot = map->lot(id);
            return lot ? std::min(lot->zone, ZONE_COUNT - 1) : ZONE_COUNT;
        };
        u32 lots[ZONE_COUNT + 1] = {}, buildings[ZONE_COUNT + 1] = {}, residents[ZONE_COUNT + 1] = {};
        u32 workers[ZONE_COUNT + 1] = {};
        u32 pedestrianWorkers[ZONE_COUNT + 1] = {};
        u32 carPurposes[TRIP_PURPOSES] = {}, driving[TRIP_PURPOSES] = {}, parked[TRIP_PURPOSES] = {};
        u32 pedestrianPurposes[PED_PURPOSES] = {};
        for (const LotRef& lot : map->lots())
        {
            ++lots[std::min(lot.zone, ZONE_COUNT - 1)];
        }
        forEachRow<CityBuildingComponent>(*world, [&](const CityBuildingComponent& building)
                                          { ++buildings[std::min(building.zone, ZONE_COUNT - 1)]; });
        forEachRow<CityCarComponent>(*world,
                                     [&](const CityCarComponent& car)
                                     {
                                         if (isTruck(*city, car.id))
                                         {
                                             return;  
                                         }
                                         const u32 purpose = std::min(car.purpose, TRIP_PURPOSES - 1);
                                         ++residents[zoneOfLot(car.homeLot)];
                                         ++workers[zoneOfLot(car.workLot)];
                                         ++carPurposes[purpose];
                                         ++(car.state == CAR_DRIVING ? driving : parked)[purpose];
                                     });
        forEachRow<CityPedestrianComponent>(*world,
                                            [&](const CityPedestrianComponent& ped)
                                            {
                                                ++residents[zoneOfLot(ped.homeLot)];
                                                pedestrianWorkers[zoneOfLot(ped.workLot)] +=
                                                    isWorker(city->seed, ped.id) ? 1 : 0;
                                                ++pedestrianPurposes[std::min(ped.purpose, PED_PURPOSES - 1)];
                                            });
        const auto perZone = [&out](const char* name, const u32(&counts)[ZONE_COUNT + 1])
        {
            out.beginObject(name);
            for (u32 zone = 0; zone < ZONE_COUNT; ++zone)
            {
                out.writeU32(ZONE_NAMES[zone], counts[zone]);
            }
            out.writeU32("none", counts[ZONE_COUNT]);
            out.endObject();
        };
        const auto perPurpose = [&out](const char* name, const u32(&counts)[TRIP_PURPOSES])
        {
            out.beginObject(name);
            out.writeU32("home", counts[TRIP_HOME]);
            out.writeU32("work", counts[TRIP_WORK]);
            out.writeU32("shop", counts[TRIP_SHOP]);
            out.endObject();
        };
        perZone("lots", lots);
        perZone("buildings", buildings);
        perZone("residents", residents);
        perZone("workers", workers);
        perZone("pedestrianWorkers", pedestrianWorkers);
        perPurpose("carPurposes", carPurposes);
        perPurpose("carsDriving", driving);
        perPurpose("carsParked", parked);
        out.beginObject("pedestrianPurposes");
        for (u32 purpose = 0; purpose < PED_PURPOSES; ++purpose)
        {
            out.writeU32(PED_PURPOSE_NAMES[purpose], pedestrianPurposes[purpose]);
        }
        out.endObject();
        return {};
    }

     
     
    void writePackage(const CityStateComponent& city, const Package& package, IFormatWriter& out)
    {
        const auto at = [&](const char* name, const u32 step, const bool happened)
        {
            if (happened)
            {
                out.beginObject(name);
                out.writeU32("step", step);
                out.writeString("clock", clockText(timeOfDay(city, step)));
                out.endObject();
            }
        };
        out.writeU32("id", package.id);
        out.writeString("kind", PACKAGE_KIND_NAMES[package.kind]);
        out.writeString("state", PACKAGE_STATE_NAMES[package.state]);
        out.writeU32("origin", package.origin);
        out.writeU32("destination", package.destination);
        out.writeU32("units", package.units);
        out.writeString("vehicle",
                        package.vehicleKind == VEHICLE_TRUCK       ? "truck"
                            : package.vehicleKind == VEHICLE_DRONE ? "drone"
                                                                   : "none");
        if (package.vehicleKind != VEHICLE_NONE)
        {
            out.writeU32("vehicleId", package.vehicle);
        }
        at("ordered", package.orderedStep, true);
        at("loaded", package.loadedStep, package.state != PACKAGE_WAITING && package.state != PACKAGE_CANCELLED);
        at("sent", package.transitStep, package.state == PACKAGE_IN_TRANSIT || package.state == PACKAGE_DELIVERED);
        at("delivered", package.deliveredStep, package.state == PACKAGE_DELIVERED);
    }

    std::string ordersQuery(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = statusWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const CityStateComponent* city = findCity(*world);
        if (!city)
        {
            return NO_CITY;
        }
        if (!world->services().contains<LogisticsService>())
        {
            return "the world has no logistics service";
        }
        const u64 recent = std::min<u64>(args.integer("recent").value_or(10), 200);
        const ReadService<LogisticsService> logistics = world->services().read<LogisticsService>();
        const LogisticsStats& stats = logistics->stats();
        out.writeU32("trucks", city->trucks);
        out.writeU32("drones", stats.drones);
        out.writeU64("ordered", stats.ordered);
        out.writeU64("declined", stats.declined);
        out.writeU32("deliveredToday", stats.deliveredToday);
        out.writeU32("trucksOut", stats.trucksOut);
        out.writeU32("dronesFlying", stats.dronesFlying);
        out.beginObject("states");
        for (u32 state = 0; state < PACKAGE_STATES; ++state)
        {
            u32 total = 0;
            for (u32 kind = 0; kind < PACKAGE_KINDS; ++kind)
            {
                total += stats.counts[kind][state];
            }
            out.writeU32(PACKAGE_STATE_NAMES[state], total);
        }
        out.endObject();
        for (u32 kind = 0; kind < PACKAGE_KINDS; ++kind)
        {
            out.beginObject(PACKAGE_KIND_NAMES[kind]);
            for (u32 state = 0; state < PACKAGE_STATES; ++state)
            {
                out.writeU32(PACKAGE_STATE_NAMES[state], stats.counts[kind][state]);
            }
            out.endObject();
        }
        u32 stock[SPECIAL_KINDS] = {}, queued[SPECIAL_KINDS] = {}, incoming[SPECIAL_KINDS] = {};
        for (const Site& site : logistics->sites())
        {
            stock[site.special] += site.stock;
            queued[site.special] += static_cast<u32>(site.queue.size());
            incoming[site.special] += site.incoming;
        }
        out.beginObject("sites");
        for (u32 kind = SPECIAL_DRONE_HUB; kind < SPECIAL_KINDS; ++kind)
        {
            out.beginObject(SPECIAL_ZONE_NAMES[kind]);
            out.writeU32("count", stats.sites[kind]);
            if (kind == SPECIAL_DRONE_HUB || kind == SPECIAL_WAREHOUSE)
            {
                out.writeU32("stock", stock[kind]);
                out.writeU32("queued", queued[kind]);
                out.writeU32("incoming", incoming[kind]);
            }
            out.endObject();
        }
        out.endObject();
        const std::vector<const Package*> latest = logistics->recent(static_cast<u32>(recent));
        out.beginArray("recent", static_cast<u32>(latest.size()));
        for (const Package* package : latest)
        {
            out.beginObject({});
            writePackage(*city, *package, out);
            out.endObject();
        }
        out.endArray();
        return {};
    }

    std::string packageQuery(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = statusWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const CityStateComponent* city = findCity(*world);
        if (!city)
        {
            return NO_CITY;
        }
        const std::optional<u64> id = args.integer("id");
        if (!id || !world->services().contains<LogisticsService>())
        {
            return "id must be a package id";
        }
        const ReadService<LogisticsService> logistics = world->services().read<LogisticsService>();
        const Package* package = logistics->package(static_cast<u32>(*id));
        if (!package)
        {
            return std::format("no package {} (kept: the last {} finished and every open one)", *id, 4096);
        }
        writePackage(*city, *package, out);
        return {};
    }

    std::string pathsQuery(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
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
        if (!world->services().contains<PathService>())
        {
            return "the world has no path service: it runs in the play world";
        }
        const PathStats stats = world->services().read<PathService>()->stats();
        u32 carsAwaiting = 0, carsGreedy = 0, carsFollowing = 0, carsRerouting = 0;
        u32 pedsAwaiting = 0, pedsFollowing = 0, pedsDwelling = 0;
        forEachRow<CityCarComponent>(*world,
                                     [&](const CityCarComponent& car)
                                     {
                                         if (car.state == CAR_AWAITING_ROUTE)
                                         {
                                             ++carsAwaiting;
                                         }
                                         else if (car.state == CAR_DRIVING)
                                         {
                                             const bool following =
                                                 car.routeState == ROUTE_READY || car.routeState == ROUTE_REROUTING;
                                             ++(following ? carsFollowing : carsGreedy);
                                             carsRerouting += car.routeState == ROUTE_REROUTING ? 1 : 0;
                                         }
                                     });
        forEachRow<CityPedestrianComponent>(*world,
                                            [&](const CityPedestrianComponent& ped)
                                            {
                                                pedsAwaiting += ped.mode == PED_AWAITING_ROUTE ? 1 : 0;
                                                pedsDwelling += ped.mode == PED_DWELLING ? 1 : 0;
                                                pedsFollowing += ped.routeState == ROUTE_READY ? 1 : 0;
                                            });
        out.writeU32("workers", stats.workers);
        out.writeU64("issued", stats.issued);
        out.writeU64("completed", stats.completed);
        out.writeU64("failed", stats.failed);
        out.writeU64("stale", stats.stale);
        out.writeU64("unused", stats.unused);
        out.writeU64("reroutes", stats.reroutes);
        out.writeU64("deviations", stats.deviations);
        out.beginObject("perSecond");
        out.writeF64("issued", stats.issuedPerSecond);
        out.writeF64("completed", stats.completedPerSecond);
        out.writeF64("failed", stats.failedPerSecond);
        out.writeF64("stale", stats.stalePerSecond);
        out.endObject();
        out.writeU32("pending", stats.queued + stats.inFlight);
        out.writeU32("queued", stats.queued);
        out.writeU32("inFlight", stats.inFlight);
        out.writeU32("delaySteps", ROUTE_DELAY_STEPS);
        out.writeU64("stalledSteps", stats.stalledSteps);
        out.writeF64("stallMs", stats.stallMs);
        out.beginObject("latency");
        out.writeU32("samples", stats.latencySamples);
        out.writeF64("meanMs", stats.latencyMeanMs);
        out.writeF64("p95Ms", stats.latencyP95Ms);
        out.endObject();
        out.beginObject("search");
        out.writeF64("meanMicros", stats.meanSearchMicros);
        out.writeF64("meanSettled", stats.meanSettled);
        out.endObject();
        out.writeF64("meanCarRouteEdges", stats.meanCarEdges);
        out.writeF64("meanPedestrianRouteEdges", stats.meanPedestrianEdges);
        out.writeU32("carsAwaitingRoute", carsAwaiting);
        out.writeU32("carsGreedy", carsGreedy);
        out.writeU32("carsFollowing", carsFollowing);
        out.writeU32("carsRerouting", carsRerouting);
        out.writeU32("pedestriansAwaitingRoute", pedsAwaiting);
        out.writeU32("pedestriansDwelling", pedsDwelling);
        out.writeU32("pedestriansFollowing", pedsFollowing);
        return {};
    }

    const char* routeStateName(const u32 state)
    {
        constexpr const char* NAMES[] = {"none", "pending", "ready", "failed", "rerouting"};
        return NAMES[std::min(state, 4u)];
    }

     
    std::string routeQuery(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = statusWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        const CityStateComponent* city = findCity(*world);
        if (!city)
        {
            return NO_CITY;
        }
        if (!world->services().contains<PathService>())
        {
            return "the world has no path service: it runs in the play world";
        }
        const std::optional<std::string> kind = args.string("kind");
        const std::optional<u64> id = args.integer("id");
        if (!kind || (*kind != "car" && *kind != "ped") || !id)
        {
            return "kind must be car or ped, id an integer";
        }
        const bool car = *kind == "car";
        const ReadService<PathService> paths = world->services().read<PathService>();
        const auto find = [&]<typename T>(T& found, Entity& entity)
        {
            for (const Chunk* chunk : world->getChunksContaining(Aspect(getComponentId<T>())))
            {
                const T* rows = chunk->readColumn<T>();
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    if (rows[row].id == *id)
                    {
                        found = rows[row];
                        entity = chunk->entities()[row];
                        return true;
                    }
                }
            }
            return false;
        };
        Entity entity = Entity::undefined();
        const u32 side = city->size + 1;
        if (car)
        {
            CityCarComponent row;
            if (!find(row, entity))
            {
                return std::format("no car with id {}", *id);
            }
            const RouteSlot* found = paths->findCar(row.id);
            if (!found)
            {
                return std::format("car {} has no route slot yet: the play world has not stepped since its path "
                                   "service started",
                                   row.id);
            }
            const RouteSlot& slot = *found;
            out.writeEntity("entity", entity);
            out.writeString("kind", "car");
            out.writeU32("id", row.id);
            out.writeString("carState",
                            row.state == CAR_DRIVING      ? "driving"
                                : garaged(row)            ? "garaged"
                                : row.state == CAR_PARKED ? "parked"
                                                          : "awaitingRoute");
            out.writeString("state", routeStateName(row.routeState));
            out.writeU32("version", row.routeVersion);
            out.writeU32("index", row.routeIndex);
            out.writeU32("length", row.routeLength);
            out.writeU32("purpose", row.purpose);
            out.writeU32("targetLot", row.targetLot);
            out.beginObject("destination");
            out.writeU32("x", row.destX);
            out.writeU32("z", row.destZ);
            out.endObject();
            out.writeU32("incomingVersion", slot.incomingVersion);
            const bool shown = row.routeState == ROUTE_READY || row.routeState == ROUTE_REROUTING;
            out.beginArray("nodes", shown ? static_cast<u32>(slot.active.size()) : 0);
            for (sizet i = 0; shown && i < slot.active.size(); ++i)
            {
                out.beginObject({});
                out.writeU32("x", slot.active[i] % side);
                out.writeU32("z", slot.active[i] / side);
                out.endObject();
            }
            out.endArray();
            return {};
        }
        CityPedestrianComponent row;
        if (!find(row, entity))
        {
            return std::format("no pedestrian with id {}", *id);
        }
        const RouteSlot* found = paths->findPedestrian(row.id);
        if (!found)
        {
            return std::format("pedestrian {} has no route slot yet: the play world has not stepped since its path "
                               "service started",
                               row.id);
        }
        const RouteSlot& slot = *found;
        out.writeEntity("entity", entity);
        out.writeString("kind", "ped");
        out.writeU32("id", row.id);
        out.writeU32("mode", row.mode);
        out.writeString("state", routeStateName(row.routeState));
        out.writeU32("version", row.routeVersion);
        out.writeU32("index", row.routeIndex);
        out.writeU32("length", row.routeLength);
        out.writeU32("purpose", row.purpose);
        out.writeU32("targetLot", row.targetLot);
        out.writeU32("incomingVersion", slot.incomingVersion);
        const bool shown = row.routeState == ROUTE_READY;
        out.beginArray("nodes", shown ? static_cast<u32>(slot.active.size()) : 0);
        for (sizet i = 0; shown && i < slot.active.size(); ++i)
        {
            out.beginObject({});
            out.writeU32("x", slot.active[i] / 4 % side);
            out.writeU32("z", slot.active[i] / 4 / side);
            out.writeU32("corner", slot.active[i] & 3);
            out.endObject();
        }
        out.endArray();
        return {};
    }

     
    std::string pathWorkersCommand(ControlServer& control, const ControlArgs& args, IFormatWriter& out)
    {
        const World* world = statusWorld(control, args);
        if (!world)
        {
            return control.unknownWorld(args);
        }
        if (!world->services().contains<PathService>())
        {
            return "the world has no path service: it runs in the play world";
        }
        const std::optional<u64> workers = args.json("workers") ? args.integer("workers") : std::optional<u64>(0);
        if (!workers || *workers > 256)
        {
            return "workers must be 0..256";
        }
        WriteService<PathService> paths = world->services().write<PathService>();
        paths->setWorkers(static_cast<u32>(*workers));
        out.writeU32("workers", paths->workers());
        return {};
    }


    bool load(const ModuleContext& context)
    {
        registerComponent<CityStateComponent>(context.owner.id());
        registerComponent<CityBuildingComponent>(context.owner.id());
        registerComponent<CityRoadComponent>(context.owner.id());
        registerComponent<CityJunctionComponent>(context.owner.id());
        registerComponent<CityLotComponent>(context.owner.id());
        registerComponent<CityZoneOverrideComponent>(context.owner.id());
        registerComponent<CityGroundComponent>(context.owner.id());
        registerComponent<CityLaneComponent>(context.owner.id());
        registerComponent<CityLampComponent>(context.owner.id());
        registerComponent<CitySunComponent>(context.owner.id());
        registerComponent<CityLampLightComponent>(context.owner.id());
        registerComponent<CityPieceComponent>(context.owner.id());
        registerComponent<CitySignalComponent>(context.owner.id());
        registerComponent<CityCameraComponent>(context.owner.id());
        registerComponent<CityCarComponent>(context.owner.id());
        registerComponent<CityPedestrianComponent>(context.owner.id());
        registerComponent<CityAvatarComponent>(context.owner.id());
        registerComponent<CityDroneComponent>(context.owner.id());
        registerComponent<CityParcelComponent>(context.owner.id());
        registerComponent<CityMarkerComponent>(context.owner.id());

        ControlServer& control = context.control;
        const auto bind = [&control](std::string (*fn)(ControlServer&, const ControlArgs&, IFormatWriter&))
        { return [&control, fn](const ControlArgs& args, IFormatWriter& out) { return fn(control, args, out); }; };
        static constexpr std::string_view CLASSES[] = {"street", "avenue", "boulevard", "highway"};
        static constexpr std::string_view ZONES[] = {
            "residentialLow", "residentialHigh", "commercialLow", "commercialHigh", "industrial",
            "special",        "droneHub",        "truckDepot",    "warehouse",      "factory"};
        control.registerCommand(
            LAYOUT_COMMAND, context.owner.toView(),
            "authors a seeded city: a size x size lattice of 8 m cells with a highway ring, an avenue grid with "
            "boulevards and streets, lots in six zones (residentialLow/High, commercialLow/High, industrial, special) "
            "with catalog buildings, street lamps every 40 m, traffic lights, cars that drive "
            "home-work-shop trips over the road graph past signals, pedestrians on the sidewalks (boxes; the ones "
            "nearest the camera borrow one of `people` animated people), drones above, a turning shadow-casting sun, a "
            "moon, a procedural sky, "
            "lampLights spot lights on the lit lamps nearest the camera and an overview camera with auto exposure "
            "(the reply's camera: bindCamera view=0 id=<it>). Models the asset runtime has not baked draw as boxes "
            "(roads as slabs with lane strips tinted by congestion). The reply carries audit (entities, unrooted, "
            "unnamed) and, when some are, warnings (hierarchyAudit lists them)",
            {optionalArg("seed", ArgKind::Integer, "the layout seed", "1"),
             optionalArg("size", ArgKind::Integer, "lattice cells per side (32..1024; 8 m each)", "256"),
             optionalArg("cars", ArgKind::Integer, "cars on the roads (0..400000, at most one per 6.6 m of lane)",
                         "50000"),
             optionalArg("pedestrians", ArgKind::Integer, "pedestrians on the sidewalks (0..400000)", "30000"),
             optionalArg("people", ArgKind::Integer,
                         "the pool of animated people (where the people models are baked; 0..400000) the pedestrians "
                         "within about 220 m of the camera borrow, nearest first; the others are boxes",
                         "1250"),
             optionalArg("drones", ArgKind::Integer,
                         "delivery drones (0..50000; at most four per drone hub, one per pad)", "1000"),
             optionalArg("trucks", ArgKind::Integer,
                         "how many of the cars are delivery trucks based at the truck depots and warehouses "
                         "(0..cars; none without a base)",
                         "cars / 20"),
             optionalArg("lampLights", ArgKind::Integer,
                         "spot lights on the lit street lamps nearest the camera (0..1024, the renderer's spot light "
                         "limit)",
                         "384"),
             optionalArg("hour", ArgKind::Integer, "the hour of day at step 0 (0..23)", "17"),
             optionalArg("daySeconds", ArgKind::Integer, "seconds per day at the 60 Hz fixed step (1..86400)", "720"),
             optionalArg("replace", ArgKind::Integer, "1: delete the world's existing city first", "0"), WORLD_ARG},
            bind(&layoutCommand));
        control.registerUndoableCommand(
            GENERATE_COMMAND, context.owner.toView(),
            "regenerates the world's city design from a seed and size and repopulates its agents (counts, hour and "
            "day length kept; cars capped at what the new roads hold); undo restores the city before",
            {optionalArg("seed", ArgKind::Integer, "the design seed", "the city's"),
             optionalArg("size", ArgKind::Integer, "lattice cells per side (32..1024)", "the city's")},
            bind(&generateCommand));
        control.registerCommand(
            SAVE_COMMAND, context.owner.toView(),
            "writes the city design (version, seed, size, agent counts, day timing, edges, lots, buildings, zone "
            "overrides) as JSON to <saves directory>/city/<name>.json",
            {requiredArg("name", ArgKind::String, "the save's name: letters, digits, '_' or '-'"), WORLD_ARG},
            bind(&saveCommand));
        control.registerUndoableCommand(
            LOAD_COMMAND, context.owner.toView(),
            "replaces the world's city by a saved design and regenerates its agents from the saved seed and counts",
            {requiredArg("name", ArgKind::String, "the save's name")}, bind(&loadCommand));
        control.registerUndoableCommand(
            APPLY_DESIGN_COMMAND, context.owner.toView(),
            "replaces the world's city design by a save's JSON object (the inverse the city edits record); a different "
            "seed, size or agent count regenerates the agents too",
            {requiredArg("design", ArgKind::Json, "a saved design object (citySave's file)")},
            bind(&applyDesignCommand));
        control.registerQuery(ROADS_QUERY, context.owner.toView(),
                              "the road graph: lattice size, nodes, edges, signalled junctions, dead ends, edges and "
                              "km per class; with x "
                              "and z the junction or edge at that lattice point",
                              {optionalArg("x", ArgKind::Integer, "lattice point x"),
                               optionalArg("z", ArgKind::Integer, "lattice point z"), WORLD_ARG},
                              bind(&roadsQuery));
        control.registerQuery(
            LOTS_QUERY, context.owner.toView(),
            "lots and buildings per zone, special lots (drone hubs, truck depots, warehouses, factories) and lots cut "
            "off from the roads; with x and z the lot over that cell and its building",
            {optionalArg("x", ArgKind::Integer, "cell x"), optionalArg("z", ArgKind::Integer, "cell z"), WORLD_ARG},
            bind(&lotsQuery));
        control.registerQuery(STATUS_QUERY, context.owner.toView(),
                              "the city's seed, size, design version, entity counts per kind (people: the pool of "
                              "animated people the pedestrians near the camera borrow; pieces: road tiles, traffic "
                              "lights drawn from "
                              "models; models: the CityState.models flags, 1 cars, 2 people, 4 drones), the fixed "
                              "step and the clock",
                              {WORLD_ARG}, bind(&statusQuery));
        control.registerQuery(
            TRAFFIC_QUERY, context.owner.toView(),
            "the cars driving, moving and queued (stopped in a lane), parked, offStreet (of the parked and awaiting: "
            "standing in a garage, no curb slot was free, drawn nowhere), awaiting a route at the curb, trips "
            "finished, junctions crossed, stop lines passed on red (0 unless the stop rule broke), boxConflicts (car "
            "pairs on their paths through one junction box whose bodies overlap, cumulative; 0 unless the grants "
            "broke), boxHeld (cars at a stop line refused their movement at the last step), laneChanges (cumulative), "
            "the mean speed, lane strips "
            "per congestion level (segments*: free, busy, heavy, jammed; strips exist only where the road models are "
            "not baked), and from the car rows: carsOnRoads (driving cars on an edge), roadCapacity (cars every lane "
            "holds bumper to bumper), occupancy (percent of it, what the HUD shows) and edge directions per "
            "congestion level (directions*); bays (the parking bays of the streets and avenues, both sides) and "
            "baysTaken (parked cars standing in one), in the box of x, z and cells when given",
            {optionalArg("x", ArgKind::Integer, "lattice point x of a box to count the parking bays in"),
             optionalArg("z", ArgKind::Integer, "lattice point z of the box's centre"),
             optionalArg("cells", ArgKind::Integer, "half the box's side in cells"), WORLD_ARG},
            bind(&trafficQuery));
        control.registerQuery(PEDESTRIANS_QUERY, context.owner.toView(),
                              "the pedestrians walking, waiting at a crosswalk, on the median of a long one, crossing, "
                              "dwelling, awaiting a route, crossings finished and unsafe (crossing a road whose cars "
                              "have green; 0 unless an edit broke the walk rule); with x, z and cells (lattice points) "
                              "also outdoorsNear: the outdoor ones within cells of that point",
                              {optionalArg("x", ArgKind::Integer, "lattice point x of the box centre"),
                               optionalArg("z", ArgKind::Integer, "lattice point z of the box centre"),
                               optionalArg("cells", ArgKind::Integer, "half the box's side in cells"), WORLD_ARG},
                              bind(&pedestriansQuery));
        control.registerQuery(DAY_QUERY, context.owner.toView(),
                              "the time of day, its period, buildings in day, dark or lit state, lamps on, the sun "
                              "level and stale (rows whose light disagrees with the clock)",
                              {WORLD_ARG}, bind(&dayQuery));
        control.registerQuery(ZONES_QUERY, context.owner.toView(),
                              "lots, buildings, residents (cars and pedestrians by home lot) and workers (cars by work "
                              "lot) per zone (pedestrianWorkers: the pedestrians that work, by work lot), and the "
                              "cars' and pedestrians' trip purposes (home, work, shop; cars "
                              "also driving and parked)",
                              {WORLD_ARG}, bind(&zonesQuery));
        control.registerQuery(
            PATHS_QUERY, context.owner.toView(),
            "the path service of the play world: route requests issued, completed, failed and stale (totals and per "
            "second), the requests queued and in flight, delaySteps (a route asked for in step N arrives at step N + "
            "delaySteps), stalledSteps and stallMs (steps that waited for a search due then, and the time waited), "
            "latency (mean and p95 ms from a request to its search's end), workers, the mean route length in edges, "
            "and the cars and pedestrians waiting for or following a route",
            {WORLD_ARG}, bind(&pathsQuery));
        control.registerQuery(
            ROUTE_QUERY, context.owner.toView(),
            "one car's or pedestrian's route: its state, version, target lot, the nodes (junction x, z for a car; "
            "junction x, z and sidewalk corner for a pedestrian) and the index reached",
            {requiredArg("kind", ArgKind::String, "car or ped"),
             requiredArg("id", ArgKind::Integer, "the car or pedestrian id"), WORLD_ARG},
            bind(&routeQuery));
        control.registerCommand(
            PATH_WORKERS_COMMAND, context.owner.toView(),
            "restarts the play world's pathfinding pool with `workers` threads (0: a quarter of the hardware threads, "
            "at least one); queued requests are kept, and the city steps the same whatever the count",
            {optionalArg("workers", ArgKind::Integer, "worker threads (0..256)", "0"), WORLD_ARG},
            bind(&pathWorkersCommand));
        control.registerQuery(
            ORDERS_QUERY, context.owner.toView(),
            "the play world's deliveries: orders placed and declined (the store's queue was full), delivered today, "
            "trucks on a tour, drones flying, packages per state (waiting, loaded, inTransit, delivered, cancelled) "
            "overall and per kind (parcel by drone, freight by truck, supply from a factory to a store), the logistics "
            "sites (drone hubs, truck depots, warehouses, factories) with the stores' stock, queued orders and supply "
            "on the way, and the most recent orders",
            {optionalArg("recent", ArgKind::Integer, "recent orders listed (0..200)", "10"), WORLD_ARG},
            bind(&ordersQuery));
        control.registerQuery(PACKAGE_QUERY, context.owner.toView(),
                              "one package: its kind, state, origin and destination lots, the truck or drone carrying "
                              "it and the clock and step it was ordered, loaded, sent and delivered",
                              {requiredArg("id", ArgKind::Integer, "the package id"), WORLD_ARG}, bind(&packageQuery));
        registerPlay(context);
        return true;
    }

     
     
    void fitBuildings(SystemContext& context)
    {
        auto changed = context.query<ecs::Read<CityBuildingComponent>, ecs::Read<TransformComponent>,
                                     Modified<CityBuildingComponent>>();
        for (auto [entity, building, transform] :
             changed->view<Entity, ecs::Read<CityBuildingComponent>, ecs::Read<TransformComponent>>())
        {
            if (!context.hasComponent<MeshComponent>(entity))
            {
                continue;  
            }
            const float half = std::max(building.height, 0.0f) * 0.5f;
            if (transform.scale.y != half || transform.position.y != half)
            {
                TransformComponent& fitted = context.getComponent<TransformComponent>(entity);
                fitted.scale.y = half;
                fitted.position.y = half;
            }
        }
    }

     
     
     
     
     
    struct LampHead
    {
        float d2 = 0.0f;  
        u32 key = 0;
        Vec3 head{0.0f};
        u32 kind = NIGHT_STREET;
        float yaw = 0.0f;
    };

    struct CityLampPool
    {
        u32 frames = 0;
        Vec3 at{0.0f, -1.0e9f, 0.0f};  
        u32 lit = 0xFFFFFFFFu;  
        u32 lamps = 0xFFFFFFFFu;
        std::vector<LampHead> heads;  
        std::vector<u32> chosen;  
        std::vector<u32> held;  
    };

     
    u32 lampKey(const CityLampComponent& lamp)
    {
        return static_cast<u32>(
            mix(((static_cast<u64>(std::bit_cast<u32>(lamp.x)) << 32) | std::bit_cast<u32>(lamp.z)) ^ mix(lamp.kind)));
    }

    constexpr u32 NO_LAMP = 0xFFFFFFFFu;
    constexpr float LAMP_FADE_SECONDS = 0.6f;

     
     
     
     
    void lightLamps(SystemContext& context)
    {
        ServiceLocator& services = context.services();
        if (!services.contains<CityLampPool>())
        {
            return;
        }
        WriteService<CityLampPool> pool = services.write<CityLampPool>();
        const Entity cameraEntity = activeCityCamera(context);
        if (!cameraEntity.isValid())
            return;
        const TransformComponent& cameraTransform = context.getConstComponent<TransformComponent>(cameraEntity);
        const Vec3 camera = cameraTransform.position;
        const Vec3 forward = rotate(cameraTransform.rotation, Vec3(0.0f, 0.0f, -1.0f));
        const float reach = forward.y < -0.05f ? std::min(-camera.y / forward.y, 3000.0f) : 400.0f;
        Vec3 aim = camera + forward * reach;
        aim.y = 0.0f;
        const auto lights = context.query<ecs::Write<CityLampLightComponent>, ecs::Write<TransformComponent>,
                                          ecs::Write<SpotLightComponent>>();
        if (pool->frames++ % LAMP_LIGHT_FRAMES == 0)
        {
            std::vector<LampHead>& heads = pool->heads;
            heads.clear();
            u32 lamps = 0;
            context.query<ecs::Read<CityLampComponent>>()->forEachChunk(
                [&](Chunk* chunk)
                {
                    const CityLampComponent* rows = chunk->readColumn<CityLampComponent>();
                    for (sizet row = 0; row < chunk->size(); ++row)
                    {
                        if (rows[row].glow != 0)
                        {
                            continue;
                        }
                        ++lamps;
                        if (rows[row].on != 0)
                        {
                            const Vec3 head = lampHead(rows[row]);
                            heads.push_back({(head.x - aim.x) * (head.x - aim.x) + (head.z - aim.z) * (head.z - aim.z),
                                             lampKey(rows[row]), head, rows[row].kind, rows[row].yaw});
                        }
                    }
                });
            const Vec3 moved = aim - pool->at;
            const bool settled = std::sqrt(moved.x * moved.x + moved.z * moved.z) < LAMP_LIGHT_MOVE &&
                heads.size() == pool->lit && lamps == pool->lamps;
            if (!settled)
            {
                pool->at = aim;
                pool->lit = static_cast<u32>(heads.size());
                pool->lamps = lamps;
                sizet count = 0;
                lights->forEachChunk([&](Chunk* chunk) { count += chunk->size(); });
                const sizet nearest = std::min(count, heads.size());
                std::nth_element(heads.begin(), heads.begin() + static_cast<std::ptrdiff_t>(nearest), heads.end(),
                                 [](const LampHead& a, const LampHead& b) { return a.d2 < b.d2; });
                std::vector<u32>& chosen = pool->chosen;
                chosen.clear();
                for (sizet i = 0; i < nearest; ++i)
                {
                    chosen.push_back(heads[i].key);
                }
                std::ranges::sort(chosen);
                 
                std::vector<u32>& held = pool->held;
                held.clear();
                lights->forEachChunk(
                    [&](Chunk* chunk)
                    {
                        CityLampLightComponent* rows = chunk->mutableColumn<CityLampLightComponent>();
                        for (sizet row = 0; row < chunk->size(); ++row)
                        {
                            if (rows[row].target != NO_LAMP && std::ranges::binary_search(chosen, rows[row].target))
                            {
                                held.push_back(rows[row].target);
                            }
                            else if (rows[row].target != NO_LAMP)
                            {
                                rows[row].target = NO_LAMP;
                                chunk->markModified<CityLampLightComponent>(row);
                            }
                        }
                    });
                std::ranges::sort(held);
                sizet next = 0;
                const auto nextFresh = [&]() -> const LampHead*
                {
                    while (next < nearest && std::ranges::binary_search(held, heads[next].key))
                    {
                        ++next;
                    }
                    return next < nearest ? &heads[next++] : nullptr;
                };
                lights->forEachChunk(
                    [&](Chunk* chunk)
                    {
                        CityLampLightComponent* rows = chunk->mutableColumn<CityLampLightComponent>();
                        for (sizet row = 0; row < chunk->size(); ++row)
                        {
                            if (rows[row].target == NO_LAMP)
                            {
                                if (const LampHead* fresh = nextFresh())
                                {
                                    rows[row].target = fresh->key;
                                    rows[row].targetAt = fresh->head;
                                    rows[row].targetKind = fresh->kind;
                                    rows[row].targetYaw = fresh->yaw;
                                    chunk->markModified<CityLampLightComponent>(row);
                                }
                            }
                        }
                    });
                 
                context.query<ecs::Read<CityLampComponent>, ecs::Write<RenderTintComponent>>()->forEachChunk(
                    [&](Chunk* chunk)
                    {
                        const CityLampComponent* rows = chunk->readColumn<CityLampComponent>();
                        RenderTintComponent* tints = chunk->mutableColumn<RenderTintComponent>();
                        for (sizet row = 0; row < chunk->size(); ++row)
                        {
                            if (rows[row].glow == 0)
                            {
                                continue;
                            }
                            const Vec4 tint{1.0f, 1.0f, 1.0f, 0.0f};
                            if (std::fabs(tints[row].color.w - tint.w) > 0.02f)
                            {
                                tints[row].color = tint;
                                chunk->markModified<RenderTintComponent>(row);
                            }
                        }
                    });
            }
        }
         
        const float step = context.deltaTime / LAMP_FADE_SECONDS;
        lights->forEachChunk(
            [&](Chunk* chunk)
            {
                CityLampLightComponent* rows = chunk->mutableColumn<CityLampLightComponent>();
                TransformComponent* transforms = chunk->mutableColumn<TransformComponent>();
                SpotLightComponent* spots = chunk->mutableColumn<SpotLightComponent>();
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    CityLampLightComponent& light = rows[row];
                    float level = light.level;
                    if (light.target != light.lamp)
                    {
                        level = std::max(level - step, 0.0f);
                        if (level == 0.0f)
                        {
                            light.lamp = light.target;
                            light.kind = light.targetKind;
                            transforms[row].position = light.target != NO_LAMP
                                ? light.targetAt
                                : Vec3(pool->at.x, LAMP_LIGHT_PARK_Y, pool->at.z);
                            transforms[row].rotation = lightRotation(nightLightDirection(light.kind, light.targetYaw));
                            spots[row] = nightLightStyle(light.kind, light.lamp);
                            spots[row].intensity = 0.0f;
                            chunk->markModified<SpotLightComponent>(row);
                            chunk->markModified<TransformComponent>(row);
                            chunk->markModified<CityLampLightComponent>(row);
                        }
                    }
                    else if (light.lamp != NO_LAMP)
                    {
                        level = std::min(level + step, 1.0f);
                    }
                    if (level != light.level)
                    {
                        light.level = level;
                        spots[row].intensity = nightLightStyle(light.kind, light.lamp).intensity * level;
                        chunk->markModified<CityLampLightComponent>(row);
                        chunk->markModified<SpotLightComponent>(row);
                    }
                }
            });
    }

     
     
     
     
    constexpr u32 AVATAR_SCAN_FRAMES = 5;
    constexpr float AVATAR_RADIUS = 320.0f;  
    constexpr float AVATAR_KEEP_RADIUS = 400.0f;

    void parkAvatar(SystemContext& context, CityAvatarPool& pool, const u32 slot)
    {
        const u32 id = pool.pedestrianOf[slot];
        if (id == NO_PEDESTRIAN)
        {
            return;
        }
        if (context.isEntityValid(pool.pedestrianEntity[slot]))
        {
            context.getComponent<TransformComponent>(pool.pedestrianEntity[slot]).scale =
                pedestrianBoxScale(context.getConstComponent<CityPedestrianComponent>(pool.pedestrianEntity[slot]));
        }
        TransformComponent& avatar = context.getComponent<TransformComponent>(pool.avatars[slot]);
        const TransformComponent before = avatar;
        avatar.position = {0.0f, AVATAR_PARK_Y, 0.0f};
        queueCityFade(context, pool.avatars[slot], false, before, avatar);
        pool.retiring[slot] = CITY_FADE_SECONDS;
        context.getComponent<CityAvatarComponent>(pool.avatars[slot]).pedestrian = NO_PEDESTRIAN;
        pool.slotOf[id] = NO_PEDESTRIAN;
        pool.pedestrianOf[slot] = NO_PEDESTRIAN;
        --pool.assigned;
        ++pool.released;
    }

     
    bool resetAvatars(SystemContext& context, CityAvatarPool& pool, const u32 pedestrians)
    {
        pool = CityAvatarPool{};
        u32 count = 0;
        context.query<ecs::Read<CityAvatarComponent>>()->forEachChunk([&](Chunk* chunk)
                                                                      { count += static_cast<u32>(chunk->size()); });
        if (count == 0)
        {
            return false;
        }
        pool.avatars.assign(count, Entity::undefined());
        context.query<ecs::Read<CityAvatarComponent>>()->forEachChunk(
            [&](Chunk* chunk)
            {
                const CityAvatarComponent* rows = chunk->readColumn<CityAvatarComponent>();
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    if (rows[row].index < count)
                    {
                        pool.avatars[rows[row].index] = chunk->entities()[row];
                    }
                }
            });
        if (!std::ranges::all_of(pool.avatars, [](const Entity entity) { return entity.isValid(); }))
        {
            pool.avatars.clear();
            return false;
        }
        pool.first = pool.avatars[0];
        pool.pedestrianEntity.assign(count, Entity::undefined());
        pool.pedestrianOf.assign(count, NO_PEDESTRIAN);
        pool.slotD2.assign(count, 0.0f);
        pool.retiring.assign(count, 0.0f);
        pool.appearing.assign(count, false);
        pool.slotOf.assign(pedestrians, NO_PEDESTRIAN);
        pool.seen.assign(pedestrians, 0);
        return true;
    }

     
    void assignAvatars(SystemContext& context, CityAvatarPool& pool, const u32 pedestrians)
    {
        const Entity cameraEntity = activeCityCamera(context);
        if (!cameraEntity.isValid())
        {
            return;
        }
        const auto& pose = context.getConstComponent<TransformComponent>(cameraEntity);
        Vec3 camera = pose.position;
        const Vec3 forward = rotate(pose.rotation, {0, 0, -1});
        if (forward.y < -.1f && pose.position.y > 0)
            camera = pose.position + forward * (pose.position.y / -forward.y);
        const u32 slots = static_cast<u32>(pool.avatars.size());
        ++pool.scan;
        constexpr float KEEP2 = AVATAR_KEEP_RADIUS * AVATAR_KEEP_RADIUS;
        constexpr float NEAR2 = AVATAR_RADIUS * AVATAR_RADIUS;
        std::vector<AvatarCandidate>& candidates = pool.candidates;
        candidates.clear();
        context.query<ecs::Read<CityPedestrianComponent>, ecs::Read<TransformComponent>>()->forEachChunk(
            [&](Chunk* chunk)
            {
                const CityPedestrianComponent* rows = chunk->readColumn<CityPedestrianComponent>();
                const TransformComponent* transforms = chunk->readColumn<TransformComponent>();
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    const Vec3 d = transforms[row].position - camera;
                    const float d2 = d.x * d.x + d.z * d.z;
                    if (d2 < KEEP2 && rows[row].id < pedestrians && !pedestrianIndoors(rows[row]))
                    {
                        candidates.push_back({d2, rows[row].id, chunk->entities()[row]});
                    }
                }
            });
        for (const AvatarCandidate& c : candidates)
        {
            pool.seen[c.id] = pool.scan;
            if (pool.slotOf[c.id] != NO_PEDESTRIAN)
            {
                pool.slotD2[pool.slotOf[c.id]] = c.d2;
            }
        }
         
        for (u32 slot = 0; slot < slots; ++slot)
        {
            if (pool.pedestrianOf[slot] != NO_PEDESTRIAN && pool.seen[pool.pedestrianOf[slot]] != pool.scan)
            {
                parkAvatar(context, pool, slot);
            }
        }
         
        std::erase_if(candidates,
                      [&](const AvatarCandidate& c) { return c.d2 >= NEAR2 || pool.slotOf[c.id] != NO_PEDESTRIAN; });
        std::ranges::sort(candidates, {}, &AvatarCandidate::d2);
        u32 cursor = 0;
        pool.nearBoxes = 0;
        for (const AvatarCandidate& c : candidates)
        {
            while (cursor < slots && (pool.pedestrianOf[cursor] != NO_PEDESTRIAN || pool.retiring[cursor] > 0.0f))
            {
                ++cursor;
            }
            u32 slot = cursor < slots ? cursor++ : NO_PEDESTRIAN;
            if (slot == NO_PEDESTRIAN)
            {
                u32 worst = NO_PEDESTRIAN;
                for (u32 s = 0; s < slots; ++s)
                {
                    if (pool.pedestrianOf[s] != NO_PEDESTRIAN && pool.retiring[s] == 0.0f && pool.slotD2[s] >= NEAR2 &&
                        (worst == NO_PEDESTRIAN || pool.slotD2[s] > pool.slotD2[worst]))
                    {
                        worst = s;
                    }
                }
                if (worst == NO_PEDESTRIAN)
                {
                    ++pool.nearBoxes;
                    continue;
                }
                parkAvatar(context, pool, worst);
                ++pool.nearBoxes;
                continue;
            }
            pool.pedestrianOf[slot] = c.id;
            pool.appearing[slot] = true;
            pool.slotOf[c.id] = slot;
            pool.pedestrianEntity[slot] = c.entity;
            pool.slotD2[slot] = c.d2;
            context.getComponent<CityAvatarComponent>(pool.avatars[slot]).pedestrian = c.id;
            const u64 hash = mix(PERSON_SALT ^ c.id);
             
            context.getComponent<TransformComponent>(pool.avatars[slot]).rotation =
                yawRotation(static_cast<float>(hash >> 32 & 0xFFFF) * 1.0e-4f);
            context.getComponent<AnimationPlayerComponent>(pool.avatars[slot]).time =
                static_cast<float>(hash % 1000) * 0.01f;
            ++pool.assigned;
            ++pool.acquired;
        }
    }

    void updateAvatars(SystemContext& context)
    {
        ServiceLocator& services = context.services();
        if (!services.contains<CityAvatarPool>() || !services.contains<PathService>())
        {
            return;
        }
        const std::shared_ptr<const CityMap> map = services.read<PathService>()->map();
        WriteService<CityAvatarPool> lock = services.write<CityAvatarPool>();
        CityAvatarPool& pool = *lock;
        u32 pedestrians = 0;
        context.query<ecs::Read<CityStateComponent>>()->forEachChunk(
            [&](Chunk* chunk)
            {
                if (pedestrians == 0 && !chunk->empty())
                {
                    pedestrians = chunk->readColumn<CityStateComponent>()[0].pedestrians;
                }
            });
        if (!map || pedestrians == 0)
        {
            return;
        }
        if ((pool.avatars.empty() || !context.isEntityValid(pool.first) || pool.slotOf.size() != pedestrians) &&
            !resetAvatars(context, pool, pedestrians))
        {
            return;
        }
        for (float& remaining : pool.retiring)
            remaining = std::max(0.0f, remaining - context.deltaTime);
        if (pool.frames++ % AVATAR_SCAN_FRAMES == 0)
        {
            assignAvatars(context, pool, pedestrians);
        }
         
        const RoadGraph& graph = map->graph();
        for (u32 slot = 0; slot < pool.avatars.size(); ++slot)
        {
            if (pool.pedestrianOf[slot] == NO_PEDESTRIAN)
            {
                continue;
            }
            if (!context.isEntityValid(pool.pedestrianEntity[slot]) ||
                !context.hasComponent<CityPedestrianComponent>(pool.pedestrianEntity[slot]))
            {
                parkAvatar(context, pool, slot);
                continue;
            }
            const CityPedestrianComponent& ped =
                context.getConstComponent<CityPedestrianComponent>(pool.pedestrianEntity[slot]);
            TransformComponent& box = context.getComponent<TransformComponent>(pool.pedestrianEntity[slot]);
            if (pedestrianIndoors(ped))
            {
                parkAvatar(context, pool, slot);  
                continue;
            }
            if (box.scale.x != HIDDEN_PEDESTRIAN)
            {
                box.scale = Vec3(HIDDEN_PEDESTRIAN);
            }
            const TransformComponent& now = context.getConstComponent<TransformComponent>(pool.avatars[slot]);
            TransformComponent placed = now;
            placePedestrian(ped, graph, placed, true);
            if (pool.appearing[slot])
            {
                queueCityFade(context, pool.avatars[slot], true, now, placed);
                pool.appearing[slot] = false;
            }
            else
            {
                const float share = 1.0f - std::exp(-context.deltaTime * 15.0f);
                placed.position = now.position + (placed.position - now.position) * share;
                placed.rotation = slerp(now.rotation, placed.rotation, share);
            }
            if (!(placed.position == now.position) || !(placed.scale == now.scale) ||
                placed.rotation.x != now.rotation.x || placed.rotation.y != now.rotation.y ||
                placed.rotation.z != now.rotation.z || placed.rotation.w != now.rotation.w)
            {
                context.getComponent<TransformComponent>(pool.avatars[slot]) = placed;
            }
            const InternedString clip =
                personClip(ped, context.getConstComponent<SkinComponent>(pool.avatars[slot]).skeleton);
            if (clip.isValid() && clip != context.getConstComponent<AnimationPlayerComponent>(pool.avatars[slot]).clip)
            {
                context.getComponent<AnimationPlayerComponent>(pool.avatars[slot]).clip = clip;
            }
        }
    }

    void updateSignals(SystemContext& context)
    {
        std::optional<CityStateComponent> city;
        context.query<ecs::Read<CityStateComponent>>()->forEachChunk(
            [&](Chunk* chunk)
            {
                if (!city && !chunk->empty())
                    city = chunk->readColumn<CityStateComponent>()[0];
            });
        if (!city)
            return;
        context.query<ecs::Write<CitySignalComponent>>()->forEachChunk(
            [&](Chunk* chunk)
            {
                auto* lenses = chunk->mutableColumn<CitySignalComponent>();
                auto* fades =
                    chunk->hasComponent<RenderFadeComponent>() ? chunk->mutableColumn<RenderFadeComponent>() : nullptr;
                for (sizet row = 0; row < chunk->size(); ++row)
                {
                    if (lenses[row].color == SIGNAL_POST)
                        continue;
                    const u32 on = signalLensOn(*city, lenses[row], city->step) ? 1u : 0u;
                    if (on != lenses[row].on)
                    {
                        lenses[row].on = on;
                        chunk->markModified<CitySignalComponent>(row);
                    }
                    if (!fades)
                        context.getCommandBuffer().addComponent<RenderFadeComponent>(
                            chunk->entity(row), RenderFadeComponent{.opacity = static_cast<float>(on)});
                    else if (fades[row].opacity != static_cast<float>(on))
                    {
                        fades[row].opacity = static_cast<float>(on);
                        chunk->markModified<RenderFadeComponent>(row);
                    }
                }
            });
    }

    void attachWorld(const ModuleContext& context, const World world)
    {
        ServiceLocator& services = world.services();
        services.registerService(std::make_shared<PathService>(0), context.owner);
        services.registerService(std::make_shared<LogisticsService>(), context.owner);
         
        if (const World* play = context.control.world("play"); play && *play == world)
        {
            enableCrowdLod(context.control, std::string("play"));
        }
        attachPlay(context, world);
         
        world.registerSystem({.moduleName = context.owner.toView().data(),
                              .name = "CitySignals",
                              .fn = &updateSignals,
                              .update = SystemPhaseDescriptor{.reads = {getComponentId<CityStateComponent>()},
                                                              .writes = {getComponentId<CitySignalComponent>(),
                                                                         getComponentId<RenderFadeComponent>()},
                                                              .stage = CoreExecutionStages::BEGIN_FRAME + 1}});
        world.registerSystem({.moduleName = context.owner.toView().data(),
                              .name = FIT_SYSTEM_NAME,
                              .fn = &fitBuildings,
                              .update = SystemPhaseDescriptor{
                                  .reads = {getComponentId<CityBuildingComponent>(), getComponentId<MeshComponent>()},
                                  .writes = {getComponentId<TransformComponent>()}}});
        services.registerService(std::make_shared<CityLampPool>(), context.owner);
        services.registerService(std::make_shared<CityAvatarPool>(), context.owner);
        world.registerSystem(
            {.moduleName = context.owner.toView().data(),
             .name = AVATAR_SYSTEM_NAME,
             .fn = &updateAvatars,
             .update = SystemPhaseDescriptor{
                 .reads = {getComponentId<CityStateComponent>(), getComponentId<CityCameraComponent>(),
                           getComponentId<CityPedestrianComponent>(), getComponentId<SkinComponent>()},
                 .writes = {getComponentId<CityAvatarComponent>(), getComponentId<TransformComponent>(),
                            getComponentId<AnimationPlayerComponent>()}}});
        world.registerSystem(
            {.moduleName = context.owner.toView().data(),
             .name = LAMP_LIGHT_SYSTEM_NAME,
             .fn = &lightLamps,
             .update = SystemPhaseDescriptor{
                 .reads = {getComponentId<CityCameraComponent>(), getComponentId<CityLampComponent>()},
                 .writes = {getComponentId<CityLampLightComponent>(), getComponentId<TransformComponent>(),
                            getComponentId<SpotLightComponent>(), getComponentId<RenderTintComponent>()}}});
        world.registerSystem(
            {.moduleName = context.owner.toView().data(),
             .name = MOVE_SYSTEM_NAME,
             .fn = &moveCity,
             .fixedUpdate = SystemPhaseDescriptor{
                 .reads = {getComponentId<CityRoadComponent>(), getComponentId<CityLotComponent>(),
                           getComponentId<SkinComponent>(), getComponentId<CityCameraComponent>(),
                           getComponentId<CityParcelComponent>(), getComponentId<CityAvatarComponent>()},
                 .writes = {getComponentId<CityStateComponent>(), getComponentId<CityCarComponent>(),
                            getComponentId<CityPedestrianComponent>(), getComponentId<CityDroneComponent>(),
                            getComponentId<CityLaneComponent>(), getComponentId<CityBuildingComponent>(),
                            getComponentId<CityLampComponent>(), getComponentId<CitySunComponent>(),
                            getComponentId<TransformComponent>(), getComponentId<RenderTintComponent>(),
                            getComponentId<DirectionalLightComponent>(), getComponentId<AnimationPlayerComponent>(),
                            getComponentId<CameraComponent>(), getComponentId<EnvironmentComponent>()}}});
        attachTransitions(context, world);
    }
}  

#ifdef SPITE_SHOWCASE_BUNDLE
extern "C" const spite::ModuleDescriptor* spiteCityShowcase()
#else
SPITE_MODULE_EXPORT const spite::ModuleDescriptor* SPITE_MODULE_ENTRY()
#endif
{
    static const spite::ModuleDescriptor descriptor{.name = MODULE_NAME, .load = &load, .attachWorld = &attachWorld};
    return &descriptor;
}
