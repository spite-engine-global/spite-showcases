 
 

 
std::vector<warzone::Block> g_blocks;
warzone::Town g_town;
bool g_townReady = false;

 
struct WorldCity
{
    std::string name;
    f32 cx, cz, hx, hz;
    u32 zone, towers, density;
};
struct WorldRoute
{
    std::string name, kind;
    std::vector<Vec3> pts;  
};
std::vector<WorldCity> g_cities;
std::vector<WorldRoute> g_routes;
std::vector<warzone::CityInfo> g_cityInfo;

bool loadWorldMeta()
{
    std::ifstream in(resolveContentPath("assets_baked/warzone/world_meta.txt"));
    if (!in)
        return false;
    g_blocks.clear();
    g_cities.clear();
    g_routes.clear();
    g_cityInfo.clear();
    std::string line;
    while (std::getline(in, line))
    {
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "city")
        {
            WorldCity c;
            c.density = 0;
            ls >> c.name >> c.cx >> c.cz >> c.hx >> c.hz >> c.zone >> c.towers >> c.density;
            g_cities.push_back(c);
            g_cityInfo.push_back({c.name, c.cx, c.cz, 0.81f * std::hypot(c.hx, c.hz), static_cast<int>(c.towers), static_cast<int>(c.density)});
        }
        else if (tag == "block")
        {
            warzone::Block b;
            u32 zone = 0, city = 0;
            ls >> b.x0 >> b.z0 >> b.x1 >> b.z1 >> zone >> city;
            b.zone = static_cast<warzone::Zone>(zone);
            b.city = static_cast<u8>(city);
            g_blocks.push_back(b);
        }
        else if (tag == "route")
        {
            WorldRoute r;
            u32 n = 0;
            ls >> r.name >> r.kind >> n;
            for (u32 i = 0; i < n; ++i)
            {
                f32 x, z;
                ls >> x >> z;
                r.pts.push_back({x, 0.0f, z});
            }
            g_routes.push_back(std::move(r));
        }
    }
    SINFO_LOG("Warzone: world v2: {} cities, {} blocks, {} routes", g_cities.size(), g_blocks.size(), g_routes.size());
    return !g_blocks.empty();
}

void loadBlocks()
{
    if (loadWorldMeta())
        return;
    g_blocks.clear();
    std::ifstream in(resolveContentPath("assets_baked/warzone/terrain.json"));
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t at = text.find("\"blocks\"");
    if (at == std::string::npos)
        return;
    const size_t end = text.find(']', at);
    while (true)
    {
        at = text.find('{', at);
        if (at == std::string::npos || at > end)
            break;
        warzone::Block b{};
        auto field = [&](const char* key, float& out)
        {
            const size_t k = text.find(key, at);
            if (k != std::string::npos)
                out = std::stof(text.substr(text.find(':', k) + 1, 24));
        };
        field("\"x0\"", b.x0);
        field("\"x1\"", b.x1);
        field("\"z0\"", b.z0);
        field("\"z1\"", b.z1);
        g_blocks.push_back(b);
        at = text.find('}', at);
    }
}

const warzone::Town& townPlan(u32 seed)
{
    if (!g_townReady || g_town.seed != seed)
    {
        if (g_blocks.empty())
            loadBlocks();
        g_town = warzone::generateTown(seed, g_blocks, [](float x, float z) { return g_terrain.valid() ? g_terrain.height(x, z) : 0.0f; }, {},
                                       std::span<const warzone::CityInfo>(g_cityInfo));
        g_townReady = true;
    }
    return g_town;
}

MeshComponent meshOf(const warzone::PieceType& type)
{
    MeshComponent m;
    m.meshHandle = AssetHandle{InternedString(type.model), AssetType::MESH, 0};
    return m;
}
MaterialComponent materialOf(const warzone::PieceType& type)
{
    MaterialComponent m;
    m.materialHandle = AssetHandle{InternedString(type.material), AssetType::MATERIAL, 0};
    return m;
}

Quat yawRotation(u8 yaw) { return fromAxisAngle({0, 1, 0}, yaw * PI * 0.5f); }

 
Entity findTownRoot(const World& world, u32* seed = nullptr)
{
    auto query = world.createQueryBuilder().buildQuery<Read<WarzoneTown>>();
    for (auto [e, town] : query->view<Entity, Read<WarzoneTown>>())
    {
        if (seed)
            *seed = town.seed;
        return e;
    }
    return Entity::undefined();
}

Entity findBuildingRoot(const World& world, u32 id)
{
    auto query = world.createQueryBuilder().buildQuery<Read<WarzoneBuilding>>();
    for (auto [e, b] : query->view<Entity, Read<WarzoneBuilding>>())
        if (b.id == id)
            return e;
    return Entity::undefined();
}

void attachAll(const World& world, std::span<const std::pair<Entity, Entity>> edges)
{
    if (world.services().contains<EntityTransformManager>())
        world.services().write<EntityTransformManager>()->attachBatch(edges);
}

 
std::string raiseBuilding(const World& world, u32 seed, u32 buildingId, int part, u32& created)
{
    created = 0;
    const auto& town = townPlan(seed);
    if (buildingId >= town.buildings.size())
        return std::format("no building {} (the plan for seed {} has {})", buildingId, seed, town.buildings.size());
    u32 rootSeed = seed;
    Entity townRoot = findTownRoot(world, &rootSeed);
    if (townRoot.isValid() && rootSeed != seed)
        return std::format("the scene's Town was raised from seed {}, not {}: raze it first", rootSeed, seed);
    if (!townRoot.isValid())
    {
        townRoot = world.createEntity();
        world.addComponent<TransformComponent>(townRoot);
        world.addComponent<WarzoneTown>(townRoot, WarzoneTown{.seed = seed, .generator = warzone::GENERATOR_VERSION});
        setEntityName(world, townRoot, "Town");
    }
    const auto& b = town.buildings[buildingId];
    Entity root = findBuildingRoot(world, buildingId);
    std::vector<std::pair<Entity, Entity>> edges;
    if (!root.isValid())
    {
        root = world.createEntity();
        world.addComponent<TransformComponent>(root);
        world.addComponent<WarzoneBuilding>(root, WarzoneBuilding{.id = buildingId, .kind = static_cast<u32>(b.kind), .floors = b.floors});
        setEntityName(world, root, b.name);
        edges.emplace_back(root, townRoot);
    }
    std::vector<bool> present(b.parts, false);
    {
        auto query = world.createQueryBuilder().buildQuery<Read<WarzonePiece>>();
        for (const auto& p : query->view<Read<WarzonePiece>>())
            if (p.building == buildingId && p.part < present.size())
                present[p.part] = true;
    }
    for (u32 i = b.first; i < b.first + b.count; ++i)
    {
        const auto& p = town.pieces[i];
        if ((part >= 0 && p.part != static_cast<u32>(part)) || present[p.part])
            continue;
        const auto& type = warzone::PIECES[p.type];
        const Entity e = world.createEntity();
        TransformComponent t;
        t.position = {p.pos.x, p.pos.y, p.pos.z};
        t.rotation = yawRotation(p.yaw);
        t.scale = {p.scale.x, p.scale.y, p.scale.z};
        world.addComponent<TransformComponent>(e, t);
        world.addComponent<MeshComponent>(e, meshOf(type));
        world.addComponent<MaterialComponent>(e, materialOf(type));
        world.addComponent<WarzonePiece>(e, WarzonePiece{.building = buildingId, .type = p.type, .part = p.part, .floor = p.floor, .index = i});
        setEntityName(world, e, std::format("{} F{}", type.slug, p.floor));
        edges.emplace_back(e, root);
        ++created;
    }
    attachAll(world, edges);
    return {};
}

 
u32 razeBuilding(const World& world, u32 buildingId, int part)
{
    std::vector<Entity> doomed;
    u32 remaining = 0;
    {
        auto query = world.createQueryBuilder().buildQuery<Read<WarzonePiece>>();
        for (auto [e, p] : query->view<Entity, Read<WarzonePiece>>())
        {
            if (p.building != buildingId)
                continue;
            if (part < 0 || p.part == static_cast<u32>(part))
                doomed.push_back(e);
            else
                ++remaining;
        }
    }
    const u32 removed = static_cast<u32>(doomed.size());
    if (remaining == 0)
    {
        const Entity root = findBuildingRoot(world, buildingId);
        if (root.isValid())
            doomed.push_back(root);
    }
    world.destroyEntities(doomed);
    return removed;
}

void registerTownCommands(const ModuleContext& context)
{
    auto& control = context.control;
    const std::string_view owner = MODULE;
    control.registerQuery(
        "warzoneTownPlan", owner,
        "the seeded town plan: buildings (id, name, kind, floors, parts, pieces, bounds) and totals; built=true adds how "
        "many pieces of each building the scene holds",
        {optionalArg("seed", ArgKind::Integer, "town seed", "the scene Town's seed, else 7"),
         optionalArg("built", ArgKind::Boolean, "count the scene's pieces per building", "false")},
        [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            const World* scene = control.world("scene");
            u32 seed = 7;
            if (scene)
                findTownRoot(*scene, &seed);
            if (auto v = args.integer("seed"))
                seed = static_cast<u32>(*v);
            const auto& town = townPlan(seed);
            std::vector<u32> built(town.buildings.size(), 0);
            if (args.boolean("built").value_or(false) && scene)
            {
                auto query = scene->createQueryBuilder().buildQuery<Read<WarzonePiece>>();
                for (const auto& p : query->view<Read<WarzonePiece>>())
                    if (p.building < built.size())
                        ++built[p.building];
            }
            result.writeU32("seed", seed);
            result.writeU32("generator", warzone::GENERATOR_VERSION);
            result.writeU32("blocks", static_cast<u32>(g_blocks.size()));
            result.writeU32("buildingCount", static_cast<u32>(town.buildings.size()));
            result.writeU32("pieces", static_cast<u32>(town.pieces.size()));
            result.beginArray("buildings", static_cast<u32>(town.buildings.size()));
            for (const auto& b : town.buildings)
            {
                result.beginObject({});
                result.writeU32("id", b.id);
                result.writeString("name", b.name);
                result.writeString("kind", warzone::kindName(b.kind));
                result.writeU32("floors", b.floors);
                result.writeU32("parts", b.parts);
                result.writeU32("pieces", b.count);
                result.writeU32("built", built[b.id]);
                const f32 lo[3] = {b.bounds.lo.x, b.bounds.lo.y, b.bounds.lo.z};
                const f32 hi[3] = {b.bounds.hi.x, b.bounds.hi.y, b.bounds.hi.z};
                result.writeFloats("lo", lo);
                result.writeFloats("hi", hi);
                result.endObject();
            }
            result.endArray();
            return {};
        });

    control.registerUndoableCommand(
        "warzoneRaise", owner,
        "raises one building of the seeded town plan in the scene world, one part (ground slab, then each storey's "
        "columns, walls, slab above, then the roof) or all parts; undo razes it",
        {requiredArg("building", ArgKind::Integer, "building id from warzoneTownPlan"),
         optionalArg("part", ArgKind::Integer, "part index; absent = every part"),
         optionalArg("seed", ArgKind::Integer, "town seed", "7")},
        [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            const World* world = control.world(args);
            if (!world)
                return control.unknownWorld(args);
            const u32 building = static_cast<u32>(args.integer("building").value_or(0));
            const int part = args.integer("part") ? static_cast<int>(*args.integer("part")) : -1;
            const u32 seed = static_cast<u32>(args.integer("seed").value_or(7));
            u32 created = 0;
            if (auto error = raiseBuilding(*world, seed, building, part, created); !error.empty())
                return error;
            result.writeU32("created", created);
            if (created > 0)
                control.recordInverse("warzoneRaze", part >= 0 ? std::format(R"({{"building":{},"part":{}}})", building, part)
                                                               : std::format(R"({{"building":{}}})", building));
            return {};
        });

    control.registerUndoableCommand(
        "warzoneRaze", owner, "deletes one building part (or the whole building) of the town from the scene world; undo raises it",
        {requiredArg("building", ArgKind::Integer, "building id"), optionalArg("part", ArgKind::Integer, "part index; absent = all")},
        [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            const World* world = control.world(args);
            if (!world)
                return control.unknownWorld(args);
            const u32 building = static_cast<u32>(args.integer("building").value_or(0));
            const int part = args.integer("part") ? static_cast<int>(*args.integer("part")) : -1;
            u32 seed = 7;
            findTownRoot(*world, &seed);
            const u32 removed = razeBuilding(*world, building, part);
            result.writeU32("removed", removed);
            if (removed > 0)
                control.recordInverse("warzoneRaise", part >= 0 ? std::format(R"({{"building":{},"part":{},"seed":{}}})", building, part, seed)
                                                                : std::format(R"({{"building":{},"seed":{}}})", building, seed));
            return {};
        });
}
