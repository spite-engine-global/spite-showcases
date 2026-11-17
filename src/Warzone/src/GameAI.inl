 
 
 
 
 
 

constexpr u32 AI_LOS_BUDGET = 160;    
constexpr u32 AI_PATH_BUDGET = 6;     
constexpr f32 AI_RESPAWN = 12.0f;     
f32 g_aiRespawn = AI_RESPAWN;         
 
 
u32 g_aiAllow[4] = {~0u, ~0u, ~0u, ~0u};
constexpr u32 AI_SHOT_RESERVE = 96;   

struct AiUnit
{
    u32 slot = 0;
    Entity root = Entity::undefined();
    warzone::AiState s;
    f32 think = 0.0f, dead = 0.0f, repath = 0.0f, stuck = 0.0f;
    bool targetVisible = false, needPath = false;
    u32 stride = 1;  
    f32 pending = 0.0f;  
    bool crashed = false;
    Entity proxy = Entity::undefined();  
    int full = -1;                       
    int home = -1;                       
    int route = -1;                      
    bool parked = false;                 
    f32 wash = 0.0f;                     
};

struct PlayAi
{
    std::vector<AiUnit> units;
    warzone::NavGrid nav;
    bool navReady = false;
    std::vector<warzone::V3> gridPos;
    std::vector<u8> gridLive;
    warzone::Rng rng{2026};
    u32 frame = 0;
    u32 losThisFrame = 0, pathsThisFrame = 0;
     
    u32 spawned = 0, respawns = 0, losTotal = 0, pathsTotal = 0, pathsFailed = 0, shots[4] = {}, skippedFire = 0;
    u32 strideCounts[3] = {};
    u64 losFrames = 0;
    u32 maxLosFrame = 0;
};
PlayAi g_ai;
Vec3 g_battleHot{};
f32 g_battleHotSpread = 0.0f;
bool g_battleHotValid = false;

const char* const AI_MODELS[] = {"wz_tank_proxy", "wz_amp_proxy", "wz_helicopter_proxy", "wz_aircraft_proxy"};
const char* const AI_NAMES[] = {"AI Tank", "AI AMP", "AI Heli", "AI Jet"};
const char* const AI_WEAPONS[] = {"125 mm HE", "30 mm autocannon", "AT missile", "guided bomb"};
const f32 AI_HP[] = {1000.0f, 700.0f, 700.0f, 600.0f};
void createAiFullPool(const ModelSpawner& spawner, const World& world);  
bool aiFullPoolEmpty();

void aiBox(warzone::VehicleType t, Vec3& centre, Vec3& half)
{
    switch (t)
    {
    case warzone::VehicleType::Tank:
        centre = TANK_DEF.hullBoxCentre, half = TANK_DEF.hullBoxHalf;
        break;
    case warzone::VehicleType::Amp:
        centre = AMP_DEF.hullBoxCentre, half = AMP_DEF.hullBoxHalf;
        break;
    case warzone::VehicleType::Heli:
        centre = HELI_BOX_CENTRE, half = HELI_BOX_HALF;
        break;
    default:
        centre = PLANE_BOX_CENTRE, half = PLANE_BOX_HALF;
        break;
    }
}

 
bool clearSight(const Vec3& a, const Vec3& b)
{
    const Vec3 d = b - a;
    const f32 L = length(d);
    if (L < 1.0f)
        return true;
    if (g_terrain.valid())
    {
        const Vec3 n = d * (1.0f / L);
        const f32 o[3] = {a.x, a.y, a.z}, dir[3] = {n.x, n.y, n.z};
        f32 dist;
        if (g_terrain.raycast(o, dir, L - 3.0f, dist))
            return false;
    }
    if (g_destruction.ready)
    {
        u32 piece;
        f32 t;
        warzone::V3 nrm;
        if (g_destruction.structure.raycast(toV3(a), toV3(b), piece, t, &nrm) && t * L < L - 4.0f)
            return false;
    }
    return true;
}

 
void buildNavGrid()
{
    auto& nav = g_ai.nav;
    const f32 half = g_terrain.valid() ? g_terrain.half : 1024.0f;
    nav.init(half, 8.0f);
    for (const auto& b : g_town.buildings)
        nav.blockCentres(b.bounds.lo.x, b.bounds.lo.z, b.bounds.hi.x, b.bounds.hi.z, 1.0f);
    if (g_terrain.valid())
        for (int j = 0; j < nav.n; ++j)
            for (int i = 0; i < nav.n; ++i)
            {
                const warzone::V3 c = nav.centre(i, j);
                f32 gx, gz;
                g_terrain.gradient(c.x, c.z, gx, gz);
                if (g_terrain.height(c.x, c.z) < 13.4f || std::sqrt(gx * gx + gz * gz) > 0.55f)
                    nav.blocked[static_cast<size_t>(j) * nav.n + i] = 1;
            }
    g_ai.navReady = true;
}

f32 groundAt(f32 x, f32 z) { return g_terrain.valid() ? g_terrain.height(x, z) : 0.0f; }

 
warzone::V3 aiGroundPoint(f32 limit, int tries, int except)
{
    warzone::V3 best{0, 0, 0};
    f32 bestD = -1.0f;
    for (int k = 0; k < tries * 4 && tries > 0; ++k)
    {
        const f32 x = g_ai.rng.range(-limit, limit), z = g_ai.rng.range(-limit, limit);
        if (g_ai.nav.isBlocked(x, z))
            continue;
        --tries;
        f32 dmin = 1e9f;
        const auto& vs = g_weapons.vehicles;
        for (u32 i = 0; i < vs.size() && i < 4000; i += std::max<u32>(1, static_cast<u32>(vs.size() / 200)))
            if (static_cast<int>(i) != except && !vs[i].wrecked && !vs[i].benched)
                dmin = std::min(dmin, std::hypot(vs[i].box.centre.x - x, vs[i].box.centre.z - z));
        if (dmin > bestD)
        {
            bestD = dmin;
            best = {x, groundAt(x, z), z};
        }
    }
    return best;
}

f32 mapLimit() { return (g_terrain.valid() ? g_terrain.half : 1024.0f) - 120.0f; }

 
warzone::V3 cityPoint(int c, f32 margin)
{
    const auto& city = g_cities[c];
    for (int k = 0; k < 40; ++k)
    {
        const f32 x = city.cx + g_ai.rng.range(-city.hx - margin, city.hx + margin), z = city.cz + g_ai.rng.range(-city.hz - margin, city.hz + margin);
        if (!g_ai.nav.isBlocked(x, z))
            return {x, groundAt(x, z), z};
    }
    return {city.cx, groundAt(city.cx, city.cz), city.cz + city.hz + 20.0f};
}

int pickCity()
{
    f32 total = 0.0f;
    for (const auto& c : g_cities)
        total += c.hx * c.hz;
    f32 r = g_ai.rng.range(0.0f, total);
    for (int i = 0; i < static_cast<int>(g_cities.size()); ++i)
        if ((r -= g_cities[i].hx * g_cities[i].hz) <= 0.0f)
            return i;
    return static_cast<int>(g_cities.size()) - 1;
}

 
void columnPath(AiUnit& u, int route, int i, bool forward)
{
    const auto& pts = g_routes[route].pts;
    u.s.path.clear();
    for (int k = i; forward ? k < static_cast<int>(pts.size()) : k >= 0; k += forward ? 1 : -1)
        u.s.path.push_back({pts[k].x, 0.0f, pts[k].z});
    u.s.pathIdx = 0;
    u.route = route;
    if (!u.s.path.empty())
    {
        u.s.goal = u.s.path.back();
        u.s.hasGoal = true;
    }
}

struct ColumnState
{
    int route = -1, at = 0, left = 0;
    bool forward = true;
};

 
 
warzone::V3 frontSite(AiUnit& u, ColumnState& col, f32& yaw)
{
    u.home = u.route = -1;
    const bool air = warzone::domainOf(u.s.type) == warzone::Domain::Air;
    const f32 lim = mapLimit() - 100.0f;
    yaw = g_ai.rng.range(-3.14f, 3.14f);
    if (g_cities.empty())
        return air ? warzone::V3{g_ai.rng.range(-lim, lim), 0.0f, g_ai.rng.range(-lim, lim)} : aiGroundPoint(lim, 6, -1);
    if (u.s.type == warzone::VehicleType::Plane)
        return {g_ai.rng.range(-lim, lim), 0.0f, g_ai.rng.range(-lim, lim)};
    if (u.s.type == warzone::VehicleType::Heli)
    {
        u.home = pickCity();
        return cityPoint(u.home, 500.0f);
    }
    if (col.left <= 0 && !g_routes.empty() && g_ai.rng.chance(0.45f))
    {
        col.route = g_ai.rng.irange(0, static_cast<int>(g_routes.size()) - 1);
        const int n = static_cast<int>(g_routes[col.route].pts.size());
        col.left = g_ai.rng.irange(6, 12);
        col.forward = g_ai.rng.chance(0.5f);
        col.at = std::clamp(g_ai.rng.irange(0, n - 1), 0, n - 1);
    }
    if (col.left > 0 && col.route >= 0)
    {
        const auto& pts = g_routes[col.route].pts;
        const int n = static_cast<int>(pts.size());
        const int i = std::clamp(col.at, 0, n - 1);
        col.at += col.forward ? -1 : 1;  
        --col.left;
        if (col.at < 0 || col.at >= n)
            col.left = 0;
        columnPath(u, col.route, i, col.forward);
        const int nxt = std::clamp(i + (col.forward ? 1 : -1), 0, n - 1);
        yaw = std::atan2(pts[nxt].x - pts[i].x, pts[nxt].z - pts[i].z);
        return {pts[i].x, groundAt(pts[i].x, pts[i].z), pts[i].z};
    }
    u.home = pickCity();
    return cityPoint(u.home, 40.0f);
}

void placeAiUnit(AiUnit& u, const warzone::V3& at, f32 yaw)
{
    auto& s = u.s;
    s.pos = at;
    s.yaw = yaw;
    s.vel = {};
    s.vy = 0.0f;
    s.speed = s.type == warzone::VehicleType::Plane ? warzone::aiParams(s.type).maxSpeed : 0.0f;
    if (s.type == warzone::VehicleType::Heli)
        s.pos.y = groundAt(at.x, at.z) + warzone::aiParams(s.type).cruiseAgl;
    if (s.type == warzone::VehicleType::Plane)
        s.pos.y = groundAt(at.x, at.z) + warzone::aiParams(s.type).cruiseAgl;
    if (u.route < 0)
    {
        s.path.clear();
        s.pathIdx = 0;
        s.hasGoal = false;
    }
    s.target = -1;
    s.phase = warzone::PlanePhase::Cruise;
    s.fireTimer = g_ai.rng.range(0.5f, 3.0f);
    u.think = g_ai.rng.range(0.0f, 0.5f);
    u.dead = 0.0f;
    u.crashed = false;
    u.needPath = false;
}

 
std::string spawnAiUnits(const ModelSpawner* spawnerPtr, const World& world, u32 total, const std::array<f32, 4>& mix, u32 seed, u32& added)
{
    added = 0;
    if (!g_weapons.ready)
        return "Play is not ready (weapons pool missing)";
    if (!spawnerPtr)
        return "no ModelSpawner service";
    const auto& spawner = *spawnerPtr;
    for (const char* m : AI_MODELS)
        if (!spawner.hasModel(InternedString(m)))
            return std::format("proxy model {} is not baked (tools/vehicles/import_proxies.py)", m);
    if (!g_ai.navReady)
        buildNavGrid();
    if (seed)
        g_ai.rng = warzone::Rng(seed);
    const u32 have = static_cast<u32>(g_ai.units.size());
    if (total <= have)
        return {};
    const u32 count = total - have;
    f32 sum = mix[0] + mix[1] + mix[2] + mix[3];
    if (sum <= 0.0f)
        return "mix weights are all zero";
     
    std::vector<int> types;
    std::array<f32, 4> acc{};
    for (u32 k = 0; k < count; ++k)
    {
        int pick = 0;
        f32 bestNeed = -1e9f;
        for (int t = 0; t < 4; ++t)
        {
            acc[t] += mix[t] / sum;
            if (acc[t] > bestNeed)
            {
                bestNeed = acc[t];
                pick = t;
            }
        }
        acc[pick] -= 1.0f;
        types.push_back(pick);
    }
    std::array<std::vector<u32>, 4> byType;  
    std::array<std::vector<Entity>, 4> roots;
    std::array<u32, 4> numbering{};
    ColumnState column;
    for (const auto& u : g_ai.units)
        ++numbering[static_cast<int>(u.s.type)];
    for (u32 k = 0; k < count; ++k)
    {
        const int t = types[k];
        AiUnit u;
        u.s.type = static_cast<warzone::VehicleType>(t);
        u.s.seed = warzone::hash32(have + k + 1);
        u.s.orbit = g_ai.rng.range(-3.1f, 3.1f);
        f32 yaw = 0.0f;
        const warzone::V3 at = frontSite(u, column, yaw);
        placeAiUnit(u, at, yaw);
        const Entity e = world.createEntity();
        TransformComponent tr;
        tr.position = toVec(u.s.pos);
        tr.rotation = fromAxisAngle({0, 1, 0}, u.s.yaw);
        world.addComponent<TransformComponent>(e, tr);
        setEntityName(world, e, std::format("{} {}", AI_NAMES[t], ++numbering[t]).c_str());
        u.root = e;
        byType[t].push_back(static_cast<u32>(g_ai.units.size()));
        roots[t].push_back(e);
        g_ai.units.push_back(std::move(u));
    }
    std::scoped_lock lock(g_weaponsMutex);
    for (int t = 0; t < 4; ++t)
    {
        if (roots[t].empty())
            continue;
         
         
        const std::string matName = std::string(AI_MODELS[t]) + "_m0";
        for (size_t k = 0; k < byType[t].size(); ++k)
        {
            AiUnit& u = g_ai.units[byType[t][k]];
            VehicleSlot v;
            v.root = u.root;
            if (const auto* n = world.tryGetComponent<EntityNameComponent>(u.root))
                v.name = std::string(n->name.toView());  
            MeshComponent mesh;
            mesh.meshHandle = AssetHandle{InternedString(AI_MODELS[t]), AssetType::MESH, 0};
            MaterialComponent mat;
            mat.materialHandle = AssetHandle{InternedString(matName.c_str()), AssetType::MATERIAL, 0};
            world.addComponent<MeshComponent>(u.root, mesh);
            world.addComponent<TransformMatrixComponent>(u.root, TransformMatrixComponent{});
            world.addComponent<RenderLodPolicyComponent>(u.root, RenderLodPolicyComponent{});
            world.addComponent<MaterialComponent>(u.root, mat);
            world.addComponent<RenderTintComponent>(u.root, RenderTintComponent{});
            v.weapon = AI_WEAPONS[t];
            v.player = false;
            v.ai = true;
            v.type = u.s.type;
            v.homePos = toVec(u.s.pos);
            v.homeYaw = u.s.yaw;
            v.health = {AI_HP[t], AI_HP[t]};
            if (t >= 2)
                v.armor = {0.9f, 1.2f, 1.4f, 1.3f};
            v.rotation = fromAxisAngle({0, 1, 0}, u.s.yaw);
            Vec3 c, h;
            aiBox(u.s.type, c, h);
            v.box.centre = toV3(toVec(u.s.pos) + rotate(v.rotation, c));
            v.box.half = toV3(h);
            u.proxy = u.root;
            v.meshes = {u.root};
            u.slot = registerVehicle(std::move(v));
            ++added;
        }
    }
    g_ai.spawned += added;
    if (aiFullPoolEmpty())
        createAiFullPool(spawner, world);
    return {};
}

 
 
 
 
 
constexpr u32 AI_FULL_MAX[4] = {12, 12, 8, 4};
constexpr u32 AI_FULL_ACTIVE0[4] = {10, 10, 6, 3};
constexpr f32 AI_FULL_RANGE0[4] = {1100.0f, 1100.0f, 1500.0f, 2200.0f};
u32 g_aiFullActive[4] = {AI_FULL_ACTIVE0[0], AI_FULL_ACTIVE0[1], AI_FULL_ACTIVE0[2], AI_FULL_ACTIVE0[3]};
f32 g_aiFullRange[4] = {AI_FULL_RANGE0[0], AI_FULL_RANGE0[1], AI_FULL_RANGE0[2], AI_FULL_RANGE0[3]};
const Vec3 AI_FULL_PARK{30000.0f, PARK_Y, 30000.0f};  
const char* const AI_FULL_MODELS[] = {"wz_tank", "wz_amp", "wz_helicopter", "wz_aircraft"};
const f32 AI_MOUNT_YAW[] = {0.0f, 180.0f, -90.0f, -90.0f};
const Vec3 AI_MOUNT_POS[] = {{0, 0, 0}, {0, 2.58f, 0}, {-8.86f, 2.83f, -2.18f}, {0, 0, -3.29f}};

struct AiFull
{
    warzone::VehicleType type = warzone::VehicleType::Tank;
    Entity root = Entity::undefined();
    PartRig turret, gun, rotor, tail;
    std::vector<Entity> meshes;
    int unit = -1;
    bool charred = false;
    f32 turretYaw = 0.0f, elevation = 0.0f, rotorAngle = 0.0f;
    u32 assignedFrame = 0;
};
std::vector<AiFull> g_aiFull;
bool aiFullPoolEmpty() { return g_aiFull.empty(); }

void createAiFullPool(const ModelSpawner& spawner, const World& world)
{
    for (int t = 0; t < 4; ++t)
    {
        if (!spawner.hasModel(InternedString(AI_FULL_MODELS[t])))
            continue;
        std::vector<Entity> mounts;
        std::vector<Entity> roots;
        for (u32 k = 0; k < AI_FULL_MAX[t]; ++k)
        {
            const Entity r = world.createEntity();
            TransformComponent tr;
            tr.position = AI_FULL_PARK;
            world.addComponent<TransformComponent>(r, tr);
            setEntityName(world, r, std::format("AI Near {} {}", AI_NAMES[t], k + 1).c_str());
            const Entity m = world.createEntity();
            TransformComponent mt;
            mt.position = AI_MOUNT_POS[t];
            mt.rotation = fromAxisAngle({0, 1, 0}, toRadians(AI_MOUNT_YAW[t]));
            world.addComponent<TransformComponent>(m, mt);
            std::vector<std::pair<Entity, Entity>> edge{{m, r}};
            attachAll(world, edge);
            roots.push_back(r);
            mounts.push_back(m);
        }
        auto made = spawner.spawn(world, InternedString(AI_FULL_MODELS[t]), mounts);
        if (!made)
        {
            SWARNING_LOG("Warzone: near pool {} failed: {}", AI_FULL_MODELS[t], made.error().message);
            continue;
        }
        for (u32 k = 0; k < AI_FULL_MAX[t]; ++k)
        {
            AiFull f;
            f.type = static_cast<warzone::VehicleType>(t);
            f.root = roots[k];
            const Entity e = roots[k];
            auto origin = [&](Entity part)
            {
                PartRig tmp;
                tmp.setup(world, part, e, {0, 1, 0}, {});
                return tmp.frame.apply(tmp.restPos);
            };
            if (t == 0 || t == 1)
            {
                const GroundDef& d = t == 0 ? TANK_DEF : AMP_DEF;
                f.turret.setup(world, findInSubtree(world, e, d.turretNode), e, {0, 1, 0}, d.turretPivot);
                f.gun.setup(world, findInSubtree(world, e, d.gunNode), e, {1, 0, 0}, d.gunPivot);
            }
            else if (t == 2)
            {
                const Entity blades = findInSubtree(world, e, "blades"), tailBlades = findInSubtree(world, e, "rear_blades");
                if (blades.isValid())
                    f.rotor.setup(world, blades, e, {0, 1, 0}, origin(blades));
                if (tailBlades.isValid())
                    f.tail.setup(world, tailBlades, e, {1, 0, 0}, origin(tailBlades));
            }
            meshesInSubtree(world, e, f.meshes);
            for (Entity m : f.meshes)
                if (!world.hasComponent<RenderTintComponent>(m))
                    world.addComponent<RenderTintComponent>(m, RenderTintComponent{});
            g_aiFull.push_back(std::move(f));
        }
    }
    SINFO_LOG("Warzone: AI near pool {} full models", g_aiFull.size());
}

void showProxy(SystemContext& ctx, AiUnit& u, bool show)
{
    if (auto* t = ctx.tryGetComponent<TransformComponent>(u.proxy))
        t->scale = show ? Vec3{1.0f, 1.0f, 1.0f} : Vec3{1e-4f, 1e-4f, 1e-4f};  
}

void setTint(SystemContext& ctx, const std::vector<Entity>& meshes, bool charred)
{
    for (Entity m : meshes)
        if (auto* tint = ctx.tryGetComponent<RenderTintComponent>(m))
            tint->color = charred ? Vec4{0.16f, 0.145f, 0.13f, 1.0f} : Vec4{1.0f, 1.0f, 1.0f, 1.0f};
}

void aiFullUpdate(SystemContext& ctx, f32 dt)
{
    auto& vs = g_weapons.vehicles;
     
    if (g_ai.frame % 8 == 0)
    {
        const f32 zoom = std::clamp(std::tan(g_cam.fov * 0.5f) / std::tan(toRadians(60.0f) * 0.5f), 0.25f, 2.0f);
        for (int t = 0; t < 4; ++t)
        {
            std::vector<std::pair<f32, int>> near;
            for (int k = 0; k < static_cast<int>(g_ai.units.size()); ++k)
            {
                const auto& u = g_ai.units[k];
                if (static_cast<int>(u.s.type) != t || u.parked || vs[u.slot].benched)
                    continue;
                const Vec3 offset = toVec(u.s.pos) - g_cam.pos;
                const f32 d = length(offset);
                const f32 facing = g_cam.valid && dot(offset, g_cam.dir) < -0.2f * d ? 2.0f : 1.0f;
                const f32 score = d * zoom * facing * (u.full >= 0 ? 0.8f : 1.0f);
                if (score < g_aiFullRange[t])
                    near.push_back({score, k});
            }
            std::sort(near.begin(), near.end());
            std::vector<int> want;
            for (size_t i = 0; i < near.size() && i < g_aiFullActive[t]; ++i)
                want.push_back(near[i].second);
             
            for (auto& f : g_aiFull)
                if (static_cast<int>(f.type) == t && f.unit >= 0 && std::find(want.begin(), want.end(), f.unit) == want.end())
                {
                    showProxy(ctx, g_ai.units[f.unit], true);
                    auto& rv = vs[g_ai.units[f.unit].slot];
                    rv.breakRoot = Entity::undefined();
                    for (auto& [e, sc] : rv.hidden)  
                        if (auto* t = ctx.tryGetComponent<TransformComponent>(e))
                            t->scale = sc;
                    rv.hidden.clear();
                    g_ai.units[f.unit].full = -1;
                    f.unit = -1;
                    if (auto* tr = ctx.tryGetComponent<TransformComponent>(f.root))
                        tr->position = AI_FULL_PARK;
                }
            for (int k : want)
            {
                if (g_ai.units[k].full >= 0)
                    continue;
                for (int fi = 0; fi < static_cast<int>(g_aiFull.size()); ++fi)
                {
                    auto& f = g_aiFull[fi];
                    if (static_cast<int>(f.type) != t || f.unit >= 0)
                        continue;
                    f.unit = k;
                    f.assignedFrame = g_ai.frame;
                    g_ai.units[k].full = fi;
                    vs[g_ai.units[k].slot].breakRoot = f.root;
                    f.charred = !vs[g_ai.units[k].slot].wrecked;
                    showProxy(ctx, g_ai.units[k], true);
                    break;
                }
            }
        }
    }
    for (auto& f : g_aiFull)
    {
        if (f.unit < 0)
            continue;
        auto& u = g_ai.units[f.unit];
        const auto& v = vs[u.slot];
        if (g_ai.frame >= f.assignedFrame + 2) showProxy(ctx, u, false);
        if (f.charred != v.wrecked)
        {
            f.charred = v.wrecked;
            setTint(ctx, f.meshes, f.charred);
        }
        if (auto* tr = ctx.tryGetComponent<TransformComponent>(f.root))
        {
            tr->position = toVec(u.s.pos);
            tr->rotation = v.rotation;
        }
        if (v.wrecked)
            continue;
        if (f.type == warzone::VehicleType::Tank || f.type == warzone::VehicleType::Amp)
        {
             
            f32 wantYaw = 0.0f, wantElev = 0.0f;
            if (u.s.target >= 0 && u.s.target < static_cast<int>(vs.size()))
            {
                const Vec3 to = rotate(conjugate(v.rotation), toVec(vs[u.s.target].box.centre) - toVec(u.s.pos));
                wantYaw = std::atan2(to.x, to.z);
                f32 e = 0.0f;
                if (warzone::solveElevation(f.type == warzone::VehicleType::Tank ? 420.0f : 960.0f, warzone::GRAVITY, std::hypot(to.x, to.z), to.y - 2.0f, e))
                    wantElev = std::clamp(e, -0.12f, 0.9f);
            }
            const f32 rate = f.type == warzone::VehicleType::Tank ? 0.6f : 1.75f;
            f.turretYaw = warzone::turnToward(f.turretYaw, wantYaw, rate, dt);
            f.elevation += std::clamp(wantElev - f.elevation, -0.2f * dt, 0.2f * dt);
            if (auto* p = ctx.tryGetComponent<TransformComponent>(f.turret.e))
                f.turret.pose(*p, f.turretYaw);
            if (auto* p = ctx.tryGetComponent<TransformComponent>(f.gun.e))
                f.gun.pose(*p, -f.elevation);
        }
        else if (f.type == warzone::VehicleType::Heli)
        {
            f.rotorAngle = std::fmod(f.rotorAngle + 27.0f * dt, 6.2831853f);
            if (auto* p = ctx.tryGetComponent<TransformComponent>(f.rotor.e))
                f.rotor.pose(*p, f.rotorAngle);
            if (auto* p = ctx.tryGetComponent<TransformComponent>(f.tail.e))
                f.tail.pose(*p, f.rotorAngle * 1.7f);
        }
        else if (f.type == warzone::VehicleType::Plane && particlesOn() && !v.gigaControlled && !v.benched) {
            const Vec3 pos = toVec(u.s.pos);
            const f32 throttle = std::clamp((u.s.speed - 70) / 100, 0.4f, 1.0f);
            queueJetExhaust(toV3(pos + rotate(v.rotation, PLANE_EXHAUSTS[0])),
                toV3(pos + rotate(v.rotation, PLANE_EXHAUSTS[1])), toV3(rotate(v.rotation, {0, 0, 1})),
                throttle, g_destruction.time + u.slot, dt, tierAt(pos, 4));
        }
    }
}

void aiFire(SystemContext& ctx, AiUnit& u, const warzone::AiFire& f)
{
    auto& w = g_weapons;
    auto& s = u.s;
    const int t = static_cast<int>(s.type);
    if (w.shots.size() + AI_SHOT_RESERVE >= MAX_SHOTS)
    {
        ++g_ai.skippedFire;
        return;
    }
    const Vec3 pos = toVec(s.pos);
    Vec3 aim = toVec(f.aim);
    if (s.target >= 0 && s.target < static_cast<int>(w.vehicles.size()))
        aim = aim + w.vehicles[s.target].velocity * (length(aim - pos) / (s.type == warzone::VehicleType::Tank ? 420.0f : 900.0f));  
    const bool near = length(pos - g_cam.pos) < 450.0f;
    auto& rng = w.rng;
    switch (s.type)
    {
    case warzone::VehicleType::Tank:
    case warzone::VehicleType::Amp:
    {
        const bool tank = s.type == warzone::VehicleType::Tank;
        const f32 speed = tank ? 420.0f : 960.0f;
        const Vec3 flat = normalize(Vec3{aim.x - pos.x, 0.0f, aim.z - pos.z});
        const Vec3 muzzle = pos + Vec3{0, tank ? 2.0f : 2.7f, 0} + flat * (tank ? 5.5f : 3.6f);
        const f32 dx = std::hypot(aim.x - muzzle.x, aim.z - muzzle.z), dy = aim.y - muzzle.y;
        f32 elev = 0.0f;
        if (!warzone::solveElevation(speed, warzone::GRAVITY, dx, dy, elev))
            return;
        const f32 spread = warzone::aiParams(s.type).spread;
        elev += rng.range(-spread, spread);
        const f32 yaw = std::atan2(flat.x, flat.z) + rng.range(-spread, spread);
        const Vec3 dir{std::sin(yaw) * std::cos(elev), std::sin(elev), std::cos(yaw) * std::cos(elev)};
        if (launch(tank ? warzone::ProjectileKind::Shell : warzone::ProjectileKind::Autocannon, muzzle, dir * speed, u.slot))
        {
            ++g_ai.shots[t];
            if (near)
            {
                if (tank)
                    muzzleEffects(ctx, muzzle, dir, true);
                else
                    autocannonEffects(ctx, muzzle, dir, g_ai.shots[t]);
            }
        }
        break;
    }
    case warzone::VehicleType::Heli:
    {
        const Vec3 fwd = toVec(warzone::forwardOf(s.yaw));
        const Vec3 side = Vec3{fwd.z, 0.0f, -fwd.x} * ((g_ai.shots[t] & 1) ? 2.1f : -2.1f);
        const Vec3 from = pos + Vec3{0, 0.8f, 0} + side + fwd * 1.5f;
        if (launch(warzone::ProjectileKind::Missile, from, normalize(aim - from) * 60.0f + toVec(s.vel), u.slot,
                   s.target >= 0 ? static_cast<u32>(s.target) : ~0u, aim))
            ++g_ai.shots[t];
        break;
    }
    case warzone::VehicleType::Plane:
        if (launch(warzone::ProjectileKind::Bomb, pos - Vec3{0, 2.0f, 0}, toVec(s.vel), u.slot, s.target >= 0 ? static_cast<u32>(s.target) : ~0u, aim))
            ++g_ai.shots[t];
        break;
    default:
        break;
    }
}

 
void aiThink(AiUnit& u)
{
    auto& s = u.s;
    const auto& vs = g_weapons.vehicles;
    const f32 range = warzone::engageRange(s.type);
    if (s.type == warzone::VehicleType::Plane)
    {
         
        if (s.target >= 0 && s.target < static_cast<int>(vs.size()) && !vs[s.target].wrecked && !vs[s.target].benched &&
            warzone::flatDist(vs[s.target].box.centre, s.pos) < range * 1.25f)
            return;
         
        const f32 release = warzone::bombReleaseDistance(warzone::aiParams(s.type).maxSpeed, warzone::aiParams(s.type).cruiseAgl);
        int best = -1, nearest = -1;
        f32 bd = 1e9f, nd = 1e9f;
        g_vehicleGrid.forEachIn(s.pos.x - range, s.pos.z - range, s.pos.x + range, s.pos.z + range,
                                [&](int i)
                                {
                                    if (i == static_cast<int>(u.slot) || i >= static_cast<int>(vs.size()) || vs[i].wrecked || vs[i].benched ||
                                        !warzone::canEngage(s.type, vs[i].type))
                                        return;
                                    const f32 d = warzone::flatDist(vs[i].box.centre, s.pos);
                                    if (d > range)
                                        return;
                                    if (d < nd)
                                        nd = d, nearest = i;
                                    const f32 off = std::abs(warzone::wrapAngle(warzone::yawTo(s.pos, vs[i].box.centre) - s.yaw));
                                    if (d > release + 150.0f && off < 0.9f && d < bd)
                                        bd = d, best = i;
                                });
        s.target = best >= 0 ? best : nearest;
        u.targetVisible = s.target >= 0;
        if (s.target >= 0)
            s.targetPos = vs[s.target].box.centre;
        else if (!s.hasGoal || warzone::flatDist(s.pos, s.goal) < 150.0f)
        {
             
            if (!g_cities.empty())
            {
                const auto& c = g_cities[pickCity()];
                s.goal = {c.cx, 0.0f, c.cz};
            }
            else
                s.goal = {g_ai.rng.range(-700.0f, 700.0f), 0.0f, g_ai.rng.range(-700.0f, 700.0f)};
            s.hasGoal = true;
        }
        return;
    }
    warzone::NearestThree near;
    g_vehicleGrid.forEachIn(s.pos.x - range, s.pos.z - range, s.pos.x + range, s.pos.z + range,
                            [&](int i)
                            {
                                if (i == static_cast<int>(u.slot) || i >= static_cast<int>(vs.size()))
                                    return;
                                const auto& v = vs[i];
                                if (v.wrecked || v.benched || !warzone::canEngage(s.type, v.type))
                                    return;
                                const f32 d = warzone::len(v.box.centre - s.pos);
                                if (d < range)
                                    near.add(i, d);
                            });
    std::vector<warzone::Candidate> cands;
    cands.reserve(3);
    const Vec3 eye = toVec(s.pos) + Vec3{0, s.type == warzone::VehicleType::Plane ? -2.0f : 2.5f, 0};
    for (size_t k = 0; k < near.count; ++k)
    {
        bool visible;
        if (s.type == warzone::VehicleType::Plane)
            visible = true;  
        else if (g_ai.losThisFrame < AI_LOS_BUDGET)
        {
            ++g_ai.losThisFrame;
            visible = clearSight(eye, toVec(vs[near.entries[k].i].box.centre));
        }
        else
            visible = near.entries[k].i == s.target && u.targetVisible;  
        cands.push_back({near.entries[k].i, vs[near.entries[k].i].box.centre, vs[near.entries[k].i].type, visible});
    }
    s.target = warzone::pickTarget(s.type, s.pos, cands);
    u.targetVisible = s.target >= 0;
    if (s.target >= 0)
        return;
     
    if (u.route >= 0 && s.pathIdx < s.path.size())
        return;
    if (u.route >= 0)
    {
        u.route = -1;  
        f32 best = 1e9f;
        for (int c = 0; c < static_cast<int>(g_cities.size()); ++c)
            if (const f32 d = std::hypot(g_cities[c].cx - s.pos.x, g_cities[c].cz - s.pos.z); d < best)
                best = d, u.home = c;
    }
     
    const f32 hunt = u.home >= 0 && warzone::domainOf(s.type) == warzone::Domain::Ground ? 700.0f : warzone::HUNT_RANGE;
    const int prey = g_vehicleGrid.nearest(s.pos, hunt,
                                           [&](int i)
                                           {
                                               return i != static_cast<int>(u.slot) && i < static_cast<int>(vs.size()) && !vs[i].wrecked &&
                                                      !vs[i].benched && warzone::canEngage(s.type, vs[i].type);
                                           });
    warzone::V3 goal;
    if (prey >= 0)
        goal = vs[prey].box.centre;
    else if (!s.hasGoal || warzone::flatDist(s.pos, s.goal) < 40.0f)
        goal = u.home >= 0 ? cityPoint(u.home, 0.0f)
                           : warzone::V3{g_ai.rng.range(-mapLimit(), mapLimit()) * 0.8f, 0.0f, g_ai.rng.range(-mapLimit(), mapLimit()) * 0.8f};
    else
        goal = s.goal;
    const bool moved = !s.hasGoal || warzone::flatDist(goal, s.goal) > 60.0f;
    s.goal = goal;
    s.hasGoal = true;
    if (warzone::domainOf(s.type) == warzone::Domain::Ground && (moved || s.pathIdx >= s.path.size()))
        u.needPath = true;
}

Quat aiRotation(const AiUnit& u, f32 yawRate)
{
    const auto& s = u.s;
    const Quat yawQ = fromAxisAngle({0, 1, 0}, s.yaw);
    switch (s.type)
    {
    case warzone::VehicleType::Tank:
    case warzone::VehicleType::Amp:
    {
        f32 gx = 0, gz = 0;
        if (g_terrain.valid())
            g_terrain.gradient(s.pos.x, s.pos.z, gx, gz);
        const Vec3 n = normalize(Vec3{-gx, 1.0f, -gz});
        const Vec3 f0 = toVec(warzone::forwardOf(s.yaw));
        const Vec3 f = normalize(f0 - n * dot(f0, n));
        return quatFromBasis(cross(n, f), n, f);
    }
    case warzone::VehicleType::Heli:
    {
        const Vec3 local = rotate(conjugate(yawQ), toVec(s.vel));
        const f32 pitch = std::clamp(-local.z * 0.006f, -0.3f, 0.2f), roll = std::clamp(local.x * 0.006f, -0.3f, 0.3f);
        return normalize(multiply(yawQ, multiply(fromAxisAngle({1, 0, 0}, -pitch), fromAxisAngle({0, 0, 1}, roll))));
    }
    default:
    {
        const f32 pitch = std::atan2(s.vy, std::max(s.speed, 1.0f));
        const f32 bank = std::clamp(std::atan(yawRate * s.speed / 9.81f), -1.2f, 1.2f);
        return normalize(multiply(yawQ, multiply(fromAxisAngle({1, 0, 0}, -pitch), fromAxisAngle({0, 0, 1}, -bank))));
    }
    }
}

void respawnAiUnit(SystemContext& ctx, AiUnit& u)
{
    ColumnState none;
    none.left = -1;  
    f32 yaw = 0.0f;
    if (warzone::domainOf(u.s.type) == warzone::Domain::Ground && !g_routes.empty() && g_ai.rng.chance(0.3f))
        none.left = 0;
    const warzone::V3 at = frontSite(u, none, yaw);
    placeAiUnit(u, at, yaw);
    reviveVehicle(ctx, u.slot);
    ++g_ai.respawns;
}

 
void parkAiUnit(SystemContext& ctx, AiUnit& u)
{
    auto& v = g_weapons.vehicles[u.slot];
    reviveVehicle(ctx, u.slot);
    u.parked = true;
    v.benched = true;
    u.s.pos = {30000.0f, PARK_Y, 30000.0f};
    u.s.vel = {};
    u.s.speed = 0.0f;
    u.s.target = -1;
    u.targetVisible = false;
    if (auto* t = tracked(ctx, u.root) ? ctx.tryGetComponent<TransformComponent>(u.root) : nullptr)
        t->position = toVec(u.s.pos);
    v.box.centre = toV3(toVec(u.s.pos));
}

void aiSystem(SystemContext& ctx)
{
    if (!g_weapons.ready)
        return;
    auto& ai = g_ai;
    auto& vs = g_weapons.vehicles;
    ++ai.frame;
     
    {
        ai.gridPos.resize(vs.size());
        ai.gridLive.resize(vs.size());
        for (u32 i = 0; i < vs.size(); ++i)
        {
            ai.gridPos[i] = vs[i].box.centre;
            ai.gridLive[i] = vs[i].benched ? 0 : 1;
        }
        const f32 half = g_terrain.valid() ? g_terrain.half : 1024.0f;
        g_vehicleGrid.build(half, 64.0f, ai.gridPos, ai.gridLive);
        g_vehicleGridValid = true;
    }
    if (ai.units.empty())
        return;
    const f32 dt = std::min(ctx.deltaTime, 0.05f);
    ai.losThisFrame = ai.pathsThisFrame = 0;
    ai.strideCounts[0] = ai.strideCounts[1] = ai.strideCounts[2] = 0;
    Vec3 sum{};
    u32 live = 0;
    u32 ordinal[4] = {};
    for (u32 k = 0; k < ai.units.size(); ++k)
    {
        AiUnit& u = ai.units[k];
        if (u.slot >= vs.size())
            continue;
        auto& v = vs[u.slot];
        auto& s = u.s;
        if (v.gigaControlled)
        {
            ++ordinal[static_cast<int>(s.type)];
            s.pos = v.box.centre;
            s.vel = toV3(v.velocity);
            continue;
        }
        if (ordinal[static_cast<int>(s.type)]++ >= g_aiAllow[static_cast<int>(s.type)])
        {
            if (!u.parked || !v.benched)
                parkAiUnit(ctx, u);
            continue;
        }
        if (u.parked)
        {
            u.parked = false;
            v.benched = false;
            respawnAiUnit(ctx, u);  
        }
         
        const f32 camD = length(toVec(s.pos) - g_cam.pos);
        u.stride = camD < 700.0f ? 1u : camD < 1400.0f ? 2u : 3u;
        ++ai.strideCounts[u.stride - 1];
        u.pending += dt;
        if ((ai.frame + k) % u.stride != 0 && !v.respawnPending)
            continue;
        const f32 step = std::min(u.pending, 0.15f);
        u.pending = 0.0f;
        if (v.respawnPending)  
        {
            v.respawnPending = false;
            placeAiUnit(u, toV3(v.respawnPos), v.respawnYaw);
            reviveVehicle(ctx, u.slot);
        }
        if (v.wrecked)
        {
            u.dead += step;
            if (warzone::domainOf(s.type) == warzone::Domain::Air && !u.crashed)
            {
                 
                s.vy -= 9.81f * step;
                s.pos = s.pos + warzone::V3{s.vel.x * 0.98f, s.vy, s.vel.z * 0.98f} * step;
                s.vel = s.vel * (1.0f - 0.3f * step);
                const f32 g = groundAt(s.pos.x, s.pos.z);
                if (s.pos.y <= g + 1.0f)
                {
                    s.pos.y = g + 0.5f;
                    u.crashed = true;
                    warzone::ProjectileSpec crash = warzone::specOf(warzone::ProjectileKind::Shell);
                    crash.blastRadius = 6.0f;
                    explode(ctx, toVec(s.pos), crash, u.slot, -1);
                }
            }
            if (u.dead > g_aiRespawn && !v.benched)
                respawnAiUnit(ctx, u);
        }
        else
        {
             
            if (s.target >= 0 && (s.target >= static_cast<int>(vs.size()) || vs[s.target].wrecked || vs[s.target].benched))
                s.target = -1;
            if (s.target >= 0)
                s.targetPos = vs[s.target].box.centre;
            u.think -= step;
            if (u.think <= 0.0f)
            {
                u.think = warzone::aiParams(s.type).thinkInterval * static_cast<f32>(u.stride) * (0.85f + 0.3f * ai.rng.f01());
                aiThink(u);
            }
            u.repath -= step;
            if (u.needPath && u.repath <= 0.0f && ai.pathsThisFrame < AI_PATH_BUDGET && ai.navReady)
            {
                u.repath = 3.0f;  
                ++ai.pathsThisFrame;
                ++ai.pathsTotal;
                u.needPath = false;
                if (!ai.nav.findPath(s.pos, s.goal, s.path, 6000))
                    ++ai.pathsFailed;
                s.pathIdx = 0;
            }
            const f32 yaw0 = s.yaw;
            warzone::AiFire f;
            const f32 g = groundAt(s.pos.x, s.pos.z);
            switch (s.type)
            {
            case warzone::VehicleType::Tank:
            case warzone::VehicleType::Amp:
                f = warzone::stepGround(s, step, u.targetVisible);
                if (ai.nav.isBlocked(s.pos.x, s.pos.z) && ai.navReady)
                {
                     
                    s.pos = s.pos - s.vel * step;
                    s.speed = 0.0f;
                    u.needPath = true;
                }
                s.pos.y = groundAt(s.pos.x, s.pos.z);
                u.stuck = (s.speed < 0.5f && !u.targetVisible && s.pathIdx < s.path.size()) ? u.stuck + step : 0.0f;
                if (u.stuck > 6.0f)
                {
                    u.stuck = 0.0f;
                    s.goal = u.home >= 0 ? cityPoint(u.home, 0.0f) : warzone::V3{ai.rng.range(-800.0f, 800.0f), 0.0f, ai.rng.range(-800.0f, 800.0f)};
                    u.route = -1;
                    u.needPath = true;
                }
                break;
            case warzone::VehicleType::Heli:
                f = warzone::stepHeli(s, step, u.targetVisible, g + (ai.nav.isBlocked(s.pos.x, s.pos.z) ? 30.0f : 0.0f));  
                if (particlesOn() && s.pos.y - g < 30.0f)
                {
                     
                    const auto tier = tierAt(toVec(s.pos), 8.0f);
                    if (tier <= warzone::vfx::MID)
                        warzone::pt::rotorWash(g_pt.sys, warzone::V3{s.pos.x, g, s.pos.z}, 7.5f, std::clamp(1.0f - (s.pos.y - g) / 30.0f, 0.0f, 1.0f), tier,
                                               step, u.wash, PT_DUST);
                }
                break;
            default:
                f = warzone::stepPlane(s, step, s.target >= 0, g);
                break;
            }
             
            const f32 edge = mapLimit();
            if (std::abs(s.pos.x) > edge || std::abs(s.pos.z) > edge)
            {
                s.goal = {0.0f, 0.0f, 0.0f};
                s.hasGoal = true;
                if (s.type == warzone::VehicleType::Plane && (std::abs(s.pos.x) > edge + 150.0f || std::abs(s.pos.z) > edge + 150.0f))
                {
                    s.phase = warzone::PlanePhase::Cruise;  
                    s.target = -1;
                }
                if (warzone::domainOf(s.type) == warzone::Domain::Ground)
                {
                    s.pos.x = std::clamp(s.pos.x, -edge, edge), s.pos.z = std::clamp(s.pos.z, -edge, edge);
                    u.needPath = true;
                }
            }
            if (f.fire)
                aiFire(ctx, u, f);
            const f32 yawRate = step > 0.0f ? warzone::wrapAngle(s.yaw - yaw0) / step : 0.0f;
            v.rotation = aiRotation(u, yawRate);
            v.velocity = toVec(s.vel);
            if (warzone::domainOf(s.type) == warzone::Domain::Ground)
            {
                sum = sum + toVec(s.pos);
                ++live;
            }
        }
         
        if (auto* t = ctx.tryGetComponent<TransformComponent>(u.root))
        {
            t->position = toVec(s.pos);
            t->rotation = v.rotation;
        }
        Vec3 c, h;
        aiBox(s.type, c, h);
        v.box.centre = toV3(toVec(s.pos) + rotate(v.rotation, c));
        v.box.axis[0] = toV3(rotate(v.rotation, {1, 0, 0}));
        v.box.axis[1] = toV3(rotate(v.rotation, {0, 1, 0}));
        v.box.axis[2] = toV3(rotate(v.rotation, {0, 0, 1}));
        v.box.half = toV3(h);
    }
    aiFullUpdate(ctx, dt);
    ai.losTotal += ai.losThisFrame;
    ai.maxLosFrame = std::max(ai.maxLosFrame, ai.losThisFrame);
    ++ai.losFrames;
     
     
    if (ai.frame % 30 == 0)
    {
        int bestK = -1, bestN = 0;
        for (int k = 0; k < static_cast<int>(ai.units.size()); ++k)
        {
            const auto& u = ai.units[k];
            if (u.s.target < 0 || !u.targetVisible || u.slot >= vs.size() || vs[u.slot].wrecked)
                continue;
            int n = 0;
            g_vehicleGrid.forEachIn(u.s.pos.x - 250.0f, u.s.pos.z - 250.0f, u.s.pos.x + 250.0f, u.s.pos.z + 250.0f,
                                    [&](int i) { n += (!vs[i].wrecked && warzone::flatDist(vs[i].box.centre, u.s.pos) < 250.0f) ? 1 : 0; });
            if (n > bestN)
                bestN = n, bestK = k;
        }
        if (bestK >= 0 && bestN >= 2)
        {
            const warzone::V3 p = ai.units[bestK].s.pos;
            Vec3 m{};
            int n = 0;
            std::vector<f32> dist;
            g_vehicleGrid.forEachIn(p.x - 250.0f, p.z - 250.0f, p.x + 250.0f, p.z + 250.0f,
                                    [&](int i)
                                    {
                                        if (!vs[i].wrecked && warzone::flatDist(vs[i].box.centre, p) < 250.0f)
                                        {
                                            m = m + toVec(vs[i].box.centre);
                                            ++n;
                                            dist.push_back(warzone::flatDist(vs[i].box.centre, p));
                                        }
                                    });
            m = m * (1.0f / static_cast<f32>(n));
            std::sort(dist.begin(), dist.end());
            g_battleHot = {m.x, groundAt(m.x, m.z), m.z};
            g_battleHotSpread = dist[dist.size() / 2];
            g_battleHotValid = true;
        }
        else
            g_battleHotValid = false;
    }
    if (g_battleHotValid)
    {
        const f32 k = 1.0f - std::exp(-0.35f * dt);
        const bool jump = g_battleSpread <= 0.0f || length(g_battleHot - g_battleCentre) > 900.0f;
        g_battleCentre = jump ? g_battleHot : g_battleCentre + (g_battleHot - g_battleCentre) * k;
        const f32 want = std::max(g_battleHotSpread * 2.0f + 120.0f, 260.0f) / 0.9f;  
        g_battleSpread = jump ? want : g_battleSpread + (want - g_battleSpread) * k;
    }
    else if (live)
    {
        const Vec3 c = sum * (1.0f / static_cast<f32>(live));
        std::vector<f32> d;
        for (const auto& u : ai.units)
            if (u.slot < vs.size() && !u.parked && !vs[u.slot].wrecked && warzone::domainOf(u.s.type) == warzone::Domain::Ground)
                d.push_back(std::hypot(u.s.pos.x - c.x, u.s.pos.z - c.z));
        std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
        const f32 spread = d[d.size() / 2];  
        const f32 k = 1.0f - std::exp(-0.5f * dt);
        g_battleCentre = g_battleCentre + (Vec3{c.x, groundAt(c.x, c.z), c.z} - g_battleCentre) * (g_battleSpread <= 0.0f ? 1.0f : k);
        g_battleSpread = g_battleSpread <= 0.0f ? spread : g_battleSpread + (spread - g_battleSpread) * k;
    }
}

void registerAiSystem(World world)
{
    SystemDescriptor sys;
    sys.moduleName = MODULE;
    sys.name = "WarzoneAi";
    sys.fn = aiSystem;
    sys.update = SystemPhaseDescriptor{.writes = {getComponentId<TransformComponent>(), getComponentId<RenderTintComponent>(),
                                                  getComponentId<FlipbookComponent>(), getComponentId<PointLightComponent>(),
                                                  getComponentId<AudioEmitterComponent>()}};
    world.registerSystem(sys);
}

void attachAi()
{
    g_ai = PlayAi{};
    g_aiFull.clear();
    g_vehicleGridValid = false;
    g_battleSpread = 0.0f;
}

void registerAiCommands(const ModuleContext& context)
{
    auto& control = context.control;
    auto& services = context.services;
    const std::string_view owner = MODULE;
    control.registerCommand(
        "warzoneAiPool", owner,
        "near full-model pool of the AI units: preset=phase6 (4 per type within 240 m) or phase7 (the defaults), or count / range "
        "for every type (count is capped at the pool size); answers per type the active count, range and models in use",
        {optionalArg("preset", ArgKind::String, "phase6 | phase7"), optionalArg("count", ArgKind::Integer, "full models per type"),
         optionalArg("range", ArgKind::Number, "metres")},
        [](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            const auto preset = args.string("preset").value_or("");
            for (u32 t = 0; t < 4; ++t)
            {
                if (preset == "phase6")
                {
                    g_aiFullActive[t] = 4;
                    g_aiFullRange[t] = 240.0f;
                }
                else if (preset == "phase7")
                {
                    g_aiFullActive[t] = AI_FULL_ACTIVE0[t];
                    g_aiFullRange[t] = AI_FULL_RANGE0[t];
                }
                if (auto v = args.integer("count"))
                    g_aiFullActive[t] = std::min<u32>(static_cast<u32>(*v), AI_FULL_MAX[t]);
                if (auto v = number(args, "range"))
                    g_aiFullRange[t] = *v;
            }
            u32 used[4] = {};
            for (const auto& f : g_aiFull)
                used[static_cast<int>(f.type)] += f.unit >= 0 ? 1 : 0;
            result.writeFloats("active", std::array<f32, 4>{f32(g_aiFullActive[0]), f32(g_aiFullActive[1]), f32(g_aiFullActive[2]), f32(g_aiFullActive[3])});
            result.writeFloats("range", std::array<f32, 4>{g_aiFullRange[0], g_aiFullRange[1], g_aiFullRange[2], g_aiFullRange[3]});
            result.writeFloats("inUse", std::array<f32, 4>{f32(used[0]), f32(used[1]), f32(used[2]), f32(used[3])});
            return {};
        });
    control.registerCommand(
        "warzoneBattle", owner,
        "AI battle in Play: units (total AI units wanted; adds the missing ones, spread over the map), mix [tank,amp,heli,plane] "
        "weights (default [0.4,0.3,0.18,0.12]), seed",
        {optionalArg("units", ArgKind::Number, "total AI units wanted", "10"), optionalArg("mix", ArgKind::Json, "[tank,amp,heli,plane] weights"),
         optionalArg("seed", ArgKind::Number, "seed for positions and types (0 keeps the running sequence)", "0")},
        [&control, &services](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            const World* play = control.world("play");
            if (!play)
                return "warzoneBattle needs Play running";
            const u32 units = static_cast<u32>(std::clamp(number(args, "units").value_or(10.0f), 0.0f, 20000.0f));
            std::array<f32, 4> mix = {0.4f, 0.3f, 0.18f, 0.12f};
            if (auto text = args.json("mix"))
                if (std::sscanf(text->c_str(), "[%f,%f,%f,%f]", &mix[0], &mix[1], &mix[2], &mix[3]) != 4)
                    return "mix must be [tank,amp,heli,plane]";
            u32 added = 0;
            const std::string err = spawnAiUnits(services.contains<ModelSpawner>() ? &services.get<ModelSpawner>() : nullptr, *play, units, mix, static_cast<u32>(number(args, "seed").value_or(0.0f)), added);
            if (!err.empty())
                return err;
            result.writeU32("added", added);
            result.writeU32("units", static_cast<u32>(g_ai.units.size()));
            result.writeU32("vehicles", static_cast<u32>(g_weapons.vehicles.size()));
            return {};
        });
    control.registerQuery(
        "warzoneBattleState", owner, "the AI battle: units alive/dead by type, kills, shots, sight tests and paths per frame, the battle centre",
        {}, [](const ControlArgs&, IFormatWriter& result) -> std::string
        {
            const auto& ai = g_ai;
            const auto& vs = g_weapons.vehicles;
            u32 alive[4] = {}, dead[4] = {}, kills = 0, engaged = 0, parked = 0;
            for (const auto& u : ai.units)
            {
                if (u.slot >= vs.size())
                    continue;
                if (u.parked)
                {
                    ++parked;
                    continue;
                }
                const int t = static_cast<int>(u.s.type);
                (vs[u.slot].wrecked ? dead : alive)[t]++;
                kills += vs[u.slot].kills;
                engaged += u.s.target >= 0 ? 1 : 0;
            }
            result.writeU32("units", static_cast<u32>(ai.units.size()));
            result.writeU32("parked", parked);
            result.writeFloats("alive", std::array<f32, 4>{f32(alive[0]), f32(alive[1]), f32(alive[2]), f32(alive[3])});
            result.writeFloats("dead", std::array<f32, 4>{f32(dead[0]), f32(dead[1]), f32(dead[2]), f32(dead[3])});
            result.writeU32("engaged", engaged);
            result.writeU32("aiKills", kills);
            result.writeU32("respawns", ai.respawns);
            result.writeFloats("shots", std::array<f32, 4>{f32(ai.shots[0]), f32(ai.shots[1]), f32(ai.shots[2]), f32(ai.shots[3])});
            result.writeU32("skippedFire", ai.skippedFire);
            result.writeU32("shotsInFlight", static_cast<u32>(g_weapons.shots.size()));
            result.writeF32("losPerFrame", ai.losFrames ? static_cast<f32>(ai.losTotal) / ai.losFrames : 0.0f);
            result.writeU32("losMaxFrame", ai.maxLosFrame);
            result.writeU32("paths", ai.pathsTotal);
            result.writeU32("pathsFailed", ai.pathsFailed);
            result.writeFloats("strides", std::array<f32, 3>{f32(ai.strideCounts[0]), f32(ai.strideCounts[1]), f32(ai.strideCounts[2])});
            result.writeFloats("centre", std::array<f32, 3>{g_battleCentre.x, g_battleCentre.y, g_battleCentre.z});
            result.writeF32("spread", g_battleSpread);
            result.writeU32("kills", g_weapons.kills);
            result.writeU32("explosions", g_weapons.explosions);
            {
                 
                std::vector<std::pair<f32, u32>> hot;
                for (const auto& u : ai.units)
                    if (u.slot < vs.size() && !vs[u.slot].wrecked && u.s.target >= 0 && u.targetVisible)
                        hot.push_back({std::hypot(u.s.pos.x - g_battleCentre.x, u.s.pos.z - g_battleCentre.z), u.slot});
                std::sort(hot.begin(), hot.end());
                const u32 n = static_cast<u32>(std::min<size_t>(hot.size(), 4));
                result.beginArray("hotUnits", n);
                for (u32 i = 0; i < n; ++i)
                    result.writeString({}, vs[hot[i].second].name);
                result.endArray();
            }
            result.beginArray("sample", static_cast<u32>(std::min<size_t>(ai.units.size(), 12)));
            for (size_t k = 0; k < ai.units.size() && k < 12; ++k)
            {
                const auto& u = ai.units[k];
                result.beginObject({});
                result.writeString("name", u.slot < vs.size() ? vs[u.slot].name : std::string("?"));
                result.writeFloats("pos", std::array<f32, 3>{u.s.pos.x, u.s.pos.y, u.s.pos.z});
                result.writeF32("speed", u.s.speed);
                result.writeU32("target", u.s.target >= 0 ? static_cast<u32>(u.s.target) : 0xFFFFFFFFu);
                result.writeBool("visible", u.targetVisible);
                result.writeBool("wrecked", u.slot < vs.size() && vs[u.slot].wrecked);
                result.writeU32("phase", static_cast<u32>(u.s.phase));
                result.writeF32("dTarget", u.s.target >= 0 ? warzone::flatDist(u.s.pos, u.s.targetPos) : -1.0f);
                result.writeBool("runIn", u.s.runIn);
                result.writeF32("fireTimer", u.s.fireTimer);
                result.writeF32("yawErr", u.s.target >= 0 ? warzone::wrapAngle(warzone::yawTo(u.s.pos, u.s.targetPos) - u.s.yaw) : 0.0f);
                result.writeU32("path", static_cast<u32>(u.s.path.size() - std::min(u.s.path.size(), u.s.pathIdx)));
                result.endObject();
            }
            result.endArray();
            return {};
        });
}
