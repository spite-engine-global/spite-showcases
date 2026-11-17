 
 
 
 
 
 

 
 
const Vec3 PLANE_BOX_CENTRE{0.0f, 0.2f, 0.4f};
const Vec3 PLANE_BOX_HALF{3.2f, 1.7f, 8.4f};
const Vec3 PLANE_NOZZLE{0.0f, 0.15f, -8.9f};
const Vec3 PLANE_EXHAUSTS[] = {{-0.78f, -0.35f, -7.0f}, {0.78f, -0.35f, -7.0f}};
const Vec3 PLANE_HATCH{0.0f, -2.4f, 1.0f};  
constexpr std::array<const char*, 8> PLANE_BOMBS = {"bomb", "bomb.004", "bomb.003", "bomb.005", "bomb.001", "bomb.006", "bomb.002", "bomb.007"};
constexpr u32 PLANE_BOMB_COUNT = 8;
constexpr f32 BOMB_PICK_RADIUS = 45.0f;   
constexpr f32 SIGHT_BASE_PITCH = -1.30f;  

struct PlaneRig
{
    Entity root = Entity::undefined(), model = Entity::undefined(), engine = Entity::undefined(), burner = Entity::undefined();
    struct Bomb
    {
        PartRig part;
        Vec3 hull{};
        bool loaded = true;
    };
    std::array<Bomb, PLANE_BOMB_COUNT> bombs;
    u32 vehicle = 0;
    bool player = false, crashed = false, engineStarted = false;
    warzone::PlaneParams params;
    warzone::PlaneState state;
    u32 bombsLeft = PLANE_BOMB_COUNT, nextBomb = 0, dropped = 0, guidedDrops = 0;
    f32 dropCooldown = 0.0f, rearm = 0.0f, smoke = 0.0f, wreckSpin = 0.0f;
    Vec3 lastWingTips[2]{};
    warzone::pt::TrailState wingTrails[2]{};
    bool wingTrailActive = false;
     
    bool sight = false;
    f32 savedYaw = 0.0f, savedPitch = 0.0f, courseYaw = 0.0f, courseAlt = 0.0f, sightYaw = 0.0f, sightPitch = 0.0f;
     
    Vec3 aim{};
    bool hasTarget = false;
    int64_t targetId = -1;  
    warzone::TargetClass targetClass = warzone::TargetClass::Unit;
    std::string targetName;
    Vec3 targetPos{};
    warzone::BombPrediction prediction;
    f32 targetDistance = 0.0f;
    u32 crashes = 0;
    bool outside = false;  
};

 
struct PlaneScript
{
    bool flying = false;        
    std::vector<Vec3> path;     
    bool hold = false;          
    f32 holdAlt = 0.0f;
    bool sight = false;         
    bool hasThrottle = false;
    f32 throttle = 0.65f;
    u32 drop = 0;               
    bool dropAnyway = false;    
    bool teleport = false;
    Vec3 teleportTo{};
    f32 teleportYaw = 0.0f, teleportSpeed = 120.0f;
};

std::vector<PlaneRig> g_planes;
PlaneScript g_planeScript;  

PlaneRig* playerPlane()
{
    for (auto& p : g_planes)
        if (p.player)
            return &p;
    return nullptr;
}

bool planeHudReady()
{
    const PlaneRig* p = playerPlane();
    return p && p->vehicle < g_weapons.vehicles.size();
}

Quat planeRotation(const warzone::PlaneState& s)
{
    return normalize(multiply(multiply(fromAxisAngle({0, 1, 0}, warzone::planeYaw(s.dir)), fromAxisAngle({1, 0, 0}, -warzone::planePitch(s.dir))),
                              fromAxisAngle({0, 0, 1}, -s.roll)));
}

 
void resetPlaneFlight(PlaneRig& p, const Vec3& pos, f32 yaw, f32 speed)
{
    p.state = {};
    p.state.pos = toV3(pos);
    p.state.dir = warzone::dirFromAngles(yaw, 0.0f);
    p.state.speed = speed;
    p.state.throttle = 0.7f;
    p.crashed = false;
    p.sight = false;
    p.courseYaw = yaw;
    p.courseAlt = pos.y;
    p.wreckSpin = 0.0f;
    p.wingTrailActive = false;
}

 
void attachPlanes(const ModuleContext& context, World world)
{
    g_planes.clear();
    {
        std::scoped_lock lock(g_tankMutex);
        g_planeScript = PlaneScript{};
    }
    std::vector<std::pair<Entity, WarzonePlane>> found;
    for (auto [e, plane] : world.createQueryBuilder().buildQuery<Read<WarzonePlane>>()->view<Entity, Read<WarzonePlane>>())
        found.emplace_back(e, plane);
    std::vector<std::pair<Entity, Entity>> edges;
    const Entity pool = world.createEntity();
    world.addComponent<TransformComponent>(pool);
    setEntityName(world, pool, "Bomb Pool");
    for (auto& [e, plane] : found)
    {
        PlaneRig rig;
        rig.root = e;
        rig.player = plane.role == 0;
        rig.model = findInSubtree(world, e, "Plane Model");
        if (!rig.model.isValid())
        {
            SWARNING_LOG("Warzone: airplane {} has no 'Plane Model' child", e.id());
            continue;
        }
        auto origin = [&](Entity part)
        {
            PartRig tmp;
            tmp.setup(world, part, e, {0, 1, 0}, {});
            return tmp.frame.apply(tmp.restPos);
        };
        for (u32 i = 0; i < PLANE_BOMB_COUNT; ++i)
        {
            const Entity b = findInSubtree(world, e, PLANE_BOMBS[i]);
            if (!b.isValid())
                continue;
            auto& bomb = rig.bombs[i];
            bomb.part.setup(world, b, e, {0, 1, 0}, origin(b));
            bomb.hull = origin(b);
             
            const auto* mesh = world.tryGetComponent<MeshComponent>(b);
            const auto* mat = world.tryGetComponent<MaterialComponent>(b);
            if (mesh && mat)
            {
                const Entity proxy = world.createEntity();
                TransformComponent t;
                t.position = {0.0f, PARK_Y, 0.0f};
                t.scale = {0.01f, 0.01f, 0.01f};
                world.addComponent<TransformComponent>(proxy, t);
                world.addComponent<TransformMatrixComponent>(proxy, TransformMatrixComponent{});
                world.addComponent<RenderLodPolicyComponent>(proxy, RenderLodPolicyComponent{});
                world.addComponent<MeshComponent>(proxy, MeshComponent(*mesh));
                world.addComponent<MaterialComponent>(proxy, MaterialComponent(*mat));
                setEntityName(world, proxy, "Bomb Proxy");
                edges.emplace_back(proxy, pool);
                const f32 s = bomb.part.frame.s * bomb.part.restScale.x;
                g_weapons.bombVisuals.push_back({proxy, false, normalize(multiply(bomb.part.frame.q, bomb.part.restRot)), s, rig.player});
            }
        }
        const TransformComponent rt = world.getComponent<TransformComponent>(e);  
        resetPlaneFlight(rig, rt.position, yawOf(rt.rotation), 120.0f);

        VehicleSlot v;
        v.root = e;
        if (const auto* n = world.tryGetComponent<EntityNameComponent>(e))
            v.name = std::string(n->name.toView());
        v.weapon = "guided bomb";
        v.player = rig.player;
        v.type = warzone::VehicleType::Plane;
        v.homePos = rt.position;
        v.homeYaw = yawOf(rt.rotation);
        v.health = {plane.hp, plane.hp};
        v.armor = {1.0f, 1.2f, 1.3f, 1.3f};
        v.rotation = rt.rotation;
        meshesInSubtree(world, e, v.meshes);
        for (Entity m : v.meshes)
            if (!world.hasComponent<RenderTintComponent>(m))
                world.addComponent<RenderTintComponent>(m, RenderTintComponent{});
        rig.vehicle = registerVehicle(std::move(v));
        rig.engine = makeVoice(world, "wz_engine.wav", "Jet Voice", edges, e, true);
        world.getComponent<TransformComponent>(rig.engine).position = PLANE_NOZZLE;
         
        if (context.services.contains<ModelSpawner>())
        {
            const auto& spawner = context.services.get<ModelSpawner>();
            std::vector<Entity> parents(1, Entity::undefined());
            if (spawner.hasModel(InternedString("wz_fx_fire")))
                if (auto made = spawner.spawn(world, InternedString("wz_fx_fire"), parents); made && !made->empty())
                {
                    rig.burner = made->front();
                    auto& t = world.getComponent<TransformComponent>(rig.burner);
                    t.position = {0.0f, PARK_Y, 0.0f};
                    t.scale = {0.01f, 0.01f, 0.01f};
                    FlipbookComponent fb;
                    fb.columns = 8;
                    fb.rows = 8;
                    fb.frameCount = 64;
                    fb.framesPerSecond = 30.0f;
                    fb.loop = true;
                    fb.playing = true;
                    fb.cameraFacing = true;
                    if (world.hasComponent<FlipbookComponent>(rig.burner))
                        world.getComponent<FlipbookComponent>(rig.burner) = fb;
                    else
                        world.addComponent<FlipbookComponent>(rig.burner, fb);
                    edges.emplace_back(rig.burner, pool);
                }
        }
        g_planes.push_back(std::move(rig));
    }
    attachAll(world, edges);
    SINFO_LOG("Warzone: {} airplanes ready, {} bomb proxies", g_planes.size(), g_weapons.bombVisuals.size());
}

 
 
 
void updateBombTarget(PlaneRig& p, const Vec3& aim)
{
    std::vector<warzone::BombCandidate> cands;
    for (u32 i = 0; i < g_weapons.vehicles.size(); ++i)
    {
        const auto& v = g_weapons.vehicles[i];
        if (i == p.vehicle || v.wrecked || v.benched || (p.player && v.player))  
            continue;
        cands.push_back({static_cast<int64_t>(i), v.box.centre, warzone::TargetClass::Unit});
    }
    if (g_destruction.ready)
    {
        std::vector<u32> pieces;
        g_destruction.structure.overlapSphere(toV3(aim), 14.0f, pieces);
        std::vector<u32> seen;
        for (u32 piece : pieces)
        {
            const auto& node = g_destruction.structure.node(piece);
            if (std::find(seen.begin(), seen.end(), node.building) != seen.end())
                continue;
            seen.push_back(node.building);
            const bool prop = node.building < g_town.buildings.size() && warzone::isBombProp(g_town.buildings[node.building].kind);
             
            warzone::V3 at = node.box.centre();
            at.y = node.box.hi.y;
            if (warzone::distance(node.box, toV3(aim)) < 1.0f)
                at = toV3(aim);
            cands.push_back({warzone::BUILDING_LOCK + node.building, at, prop ? warzone::TargetClass::Prop : warzone::TargetClass::Building});
        }
    }
    const int pick = warzone::pickBombTarget(toV3(aim), cands, BOMB_PICK_RADIUS);
    p.hasTarget = pick >= 0;
    if (!p.hasTarget)
    {
        p.targetId = -1;
        p.targetName.clear();
        return;
    }
    const auto& c = cands[pick];
    p.targetId = c.id;
    p.targetClass = c.cls;
    p.targetPos = toVec(c.pos);
    if (c.id < warzone::BUILDING_LOCK)
        p.targetName = g_weapons.vehicles[static_cast<size_t>(c.id)].name;
    else
    {
        const u32 b = static_cast<u32>(c.id - warzone::BUILDING_LOCK);
        p.targetName = b < g_town.buildings.size() ? g_town.buildings[b].name : std::string("building");
    }
}

 
bool nextBombRelease(const PlaneRig& p, const Quat& q, Vec3& from, Vec3& vel, u32& slot)
{
    slot = p.nextBomb;
    for (u32 k = 0; k < PLANE_BOMB_COUNT && !p.bombs[slot].loaded; ++k)
        slot = (slot + 1) % PLANE_BOMB_COUNT;
    if (!p.bombs[slot].loaded)
        return false;
    from = toVec(p.state.pos) + rotate(q, p.bombs[slot].hull);
    vel = toVec(p.state.dir) * p.state.speed + rotate(q, Vec3{0, -3.0f, 0});
    return true;
}

 
void planeSystem(SystemContext& ctx)
{
    if (g_planes.empty() || !g_weapons.ready)
        return;
    const f32 dt = std::min(ctx.deltaTime, 0.05f);
    for (auto& p : g_planes)
    {
        auto* rt = ctx.tryGetComponent<TransformComponent>(p.root);
        if (!rt || p.vehicle >= g_weapons.vehicles.size())
            continue;
        auto& v = g_weapons.vehicles[p.vehicle];
        warzone::pt::PlayerEffects playerFx(g_pt.sys, p.player);
        if (p.player)
        {
             
            std::scoped_lock lock(g_tankMutex);
            auto& sc = g_planeScript;
            if (sc.teleport && g_active == Active::Plane)
            {
                sc.teleport = false;
                v.respawnPending = true;
                v.respawnPos = sc.teleportTo;
                v.respawnYaw = sc.teleportYaw;
                v.respawnSpeed = sc.teleportSpeed;
                g_tankCtl.camYaw = sc.teleportYaw;
                g_tankCtl.camPitch = 0.0f;
            }
        }
        if (v.respawnPending)
        {
            v.respawnPending = false;
            reviveVehicle(ctx, p.vehicle);
            resetPlaneFlight(p, v.respawnPos, v.respawnYaw, std::max(v.respawnSpeed, 90.0f));
            for (auto& b : p.bombs)
                b.loaded = true;
            p.bombsLeft = PLANE_BOMB_COUNT;
            p.rearm = 0.0f;
        }
        if (v.benched)
        {
            p.wingTrailActive = false;
            rt->position = {rt->position.x, PARK_Y, rt->position.z};
            v.box.centre = toV3(rt->position);
            if (auto* a = ctx.tryGetComponent<AudioEmitterComponent>(p.engine))
                a->volume = 0.0f;
            if (auto* b = p.burner.isValid() ? ctx.tryGetComponent<TransformComponent>(p.burner) : nullptr)
                b->position = {0.0f, PARK_Y, 0.0f};
            continue;
        }
        const bool active = p.player && g_active == Active::Plane;
        Vec3 aimDir = toVec(p.state.dir);
        aimDir.y = 0.0f;
        aimDir = length(aimDir) > 1e-3f ? normalize(aimDir) : Vec3{0, 0, 1};
        f32 throttleIn = 0.0f;
        bool fire = false, scriptedDrop = false, dropAnyway = false;
        bool sightNow = false;
        if (active && !v.wrecked)
        {
            std::scoped_lock lock(g_tankMutex);
            auto& c = g_tankCtl;
            auto& sc = g_planeScript;
            if (g_snapCamera)
            {
                g_snapCamera = false;
                c.camYaw = warzone::planeYaw(p.state.dir);
                c.camPitch = warzone::planePitch(p.state.dir) - 0.05f;
            }
            const PlayerInput pin = pollPlayerInput(ctx, dt);
            throttleIn = pin.throttle;
            fire = pin.fire;
            if (pin.keys || pin.fire)
            {
                sc.flying = false;
                sc.hold = false;
                sc.hasThrottle = false;
            }
            c.scripted = sc.flying || sc.hold;
            sightNow = c.zoom || sc.sight;
            const f32 yawNow = warzone::planeYaw(p.state.dir);
            if (sightNow && !p.sight)
            {
                 
                p.sight = true;
                p.savedYaw = c.camYaw;
                p.savedPitch = c.camPitch;
                p.courseYaw = yawNow;
                p.courseAlt = p.state.pos.y;
            }
            else if (!sightNow && p.sight)
            {
                 
                p.sight = false;
                c.camYaw = yawNow;
                c.camPitch = std::clamp(warzone::planePitch(p.state.dir) - 0.05f, -1.2f, 0.9f);
            }
            if (p.sight)
            {
                p.courseYaw = warzone::wrapPi(p.courseYaw + pin.steer * 0.55f * dt);
                p.courseAlt += pin.climb * 25.0f * dt;
                if (c.hasAim)
                {
                     
                    const Vec3 eye = toVec(p.state.pos) + rotate(fromAxisAngle({0, 1, 0}, yawNow), PLANE_HATCH);
                    const Vec3 to = c.aim - eye;
                    const f32 flat = std::sqrt(to.x * to.x + to.z * to.z);
                    const f32 want = warzone::wrapPi(std::atan2(to.x, to.z) - yawNow);
                    const f32 k = 1.0f - std::exp(-4.0f * dt);
                    p.sightYaw += (std::clamp(want, -1.1f, 1.1f) - p.sightYaw) * k;
                    p.sightPitch += (std::clamp(std::atan2(to.y, flat) - SIGHT_BASE_PITCH, -0.27f, 1.1f) - p.sightPitch) * k;
                }
                else
                {
                    p.sightYaw = std::clamp(warzone::wrapPi(c.camYaw - p.savedYaw), -1.1f, 1.1f);
                    p.sightPitch = std::clamp(c.camPitch - p.savedPitch, -0.27f, 1.1f);
                    c.camYaw = warzone::wrapPi(p.savedYaw + p.sightYaw);
                    c.camPitch = p.savedPitch + p.sightPitch;
                }
                const f32 climb = std::clamp((p.courseAlt - p.state.pos.y) * 0.012f, -0.3f, 0.3f);
                aimDir = toVec(warzone::dirFromAngles(p.courseYaw, climb));
            }
            else
            {
                 
                c.camYaw = warzone::wrapPi(c.camYaw + pin.steer * 0.55f * dt);
                c.camPitch = std::clamp(c.camPitch + pin.climb * 0.35f * dt, -1.2f, 0.9f);
                aimDir = Vec3{std::cos(c.camPitch) * std::sin(c.camYaw), std::sin(c.camPitch), std::cos(c.camPitch) * std::cos(c.camYaw)};
            }
            if (sc.flying && !sc.path.empty() && !p.sight)
            {
                const Vec3 to = sc.path.front() - toVec(p.state.pos);
                const f32 flat = std::sqrt(to.x * to.x + to.z * to.z);
                if (flat < 70.0f)
                {
                    sc.path.erase(sc.path.begin());
                    if (sc.path.empty())
                    {
                        sc.flying = false;
                        sc.hold = true;
                        sc.holdAlt = p.state.pos.y;
                    }
                }
                else
                    aimDir = normalize(Vec3{to.x, std::clamp(to.y, -0.5f * flat, 0.5f * flat), to.z});
            }
            else if (sc.hold && !p.sight)
            {
                const f32 climb = std::clamp((sc.holdAlt - p.state.pos.y) * 0.012f, -0.3f, 0.3f);
                aimDir = toVec(warzone::dirFromAngles(warzone::planeYaw(p.state.dir), climb));
            }
            if (sc.flying && sc.path.empty())
                sc.flying = false;
            c.scripted = sc.flying || sc.hold;
            if (sc.hasThrottle)
            {
                p.state.throttle = sc.throttle;
                throttleIn = 0.0f;
            }
            c.lastThrottle = p.state.throttle;
            c.lastSteer = pin.steer;
            if (sc.drop > 0)
            {
                fire = true;
                scriptedDrop = true;
                dropAnyway = sc.dropAnyway;
            }
        }
        else if (!active)
            p.sight = false;

         
        p.outside = std::max(std::abs(p.state.pos.x), std::abs(p.state.pos.z)) > (g_terrain.valid() ? g_terrain.half - 300.0f : 1300.0f);
        if (p.outside && !v.wrecked)
        {
            const Vec3 home = normalize(Vec3{200.0f - p.state.pos.x, 0.0f, -150.0f - p.state.pos.z});
            aimDir = normalize(Vec3{home.x, std::clamp(aimDir.y, -0.2f, 0.3f), home.z});
            if (active && !p.sight)
            {
                 
                std::scoped_lock lock(g_tankMutex);
                const f32 homeYaw = std::atan2(home.x, home.z);
                g_tankCtl.camYaw = warzone::wrapPi(g_tankCtl.camYaw + std::clamp(warzone::wrapPi(homeYaw - g_tankCtl.camYaw), -dt, dt));
            }
        }
         
        const f32 ground = g_terrain.valid() ? g_terrain.height(p.state.pos.x, p.state.pos.z) : 0.0f;
        if (!v.wrecked)
            warzone::stepPlane(p.state, p.params, toV3(aimDir), throttleIn, dt);
        else if (!p.crashed)
        {
             
            Vec3 vel = toVec(p.state.dir) * p.state.speed;
            vel.y -= warzone::GRAVITY * 1.4f * dt;
            p.state.speed = length(vel);
            p.state.dir = toV3(normalize(vel));
            p.state.pos = p.state.pos + toV3(vel * dt);
            p.wreckSpin = std::min(p.wreckSpin + 2.0f * dt, 3.0f);
            p.state.roll = warzone::wrapPi(p.state.roll + p.wreckSpin * dt);
        }
         
        if (!p.crashed)
        {
            bool hit = p.state.pos.y - g_terrain.height(p.state.pos.x, p.state.pos.z) < 1.6f;
            if (!hit && g_destruction.ready)
            {
                const Quat q = planeRotation(p.state);
                std::vector<u32> touching;
                for (const Vec3& probe : {Vec3{0, 0, 6.5f}, Vec3{0, 0.4f, 0}, Vec3{5.0f, 0, -1.0f}, Vec3{-5.0f, 0, -1.0f}, Vec3{0, 1.2f, -6.5f}})
                {
                    g_destruction.structure.overlapSphere(toV3(toVec(p.state.pos) + rotate(q, probe)), probe.z == 0.4f ? 2.4f : 1.5f, touching);
                    if (!touching.empty())
                    {
                        hit = true;
                        break;
                    }
                }
            }
            if (hit)
            {
                const Vec3 at = toVec(p.state.pos);
                p.crashed = true;
                ++p.crashes;
                if (!v.wrecked)
                {
                    v.hitFrom = at - toVec(p.state.dir) * 10.0f;
                    damageVehicle(ctx, p.vehicle, v.health.hp + 1.0f, at - toVec(p.state.dir) * 10.0f, p.vehicle);
                }
                warzone::ProjectileSpec crash = warzone::specOf(warzone::ProjectileKind::Shell);
                crash.blastRadius = 9.0f;
                crash.blastDamage = 900.0f;
                crash.push = 16.0f;
                explode(ctx, at, crash, p.vehicle, static_cast<int>(p.vehicle));
                const f32 gh = g_terrain.height(at.x, at.z);
                p.state.pos.y = std::max(gh, std::min(p.state.pos.y, gh + 30.0f));
                if (p.state.pos.y - gh < 3.0f)
                    p.state.pos.y = gh + 0.8f;
                p.state.speed = 0.0f;
                p.state.dir = toV3(normalize(Vec3{p.state.dir.x, 0.0f, p.state.dir.z}));
                p.state.roll = 0.35f;
            }
        }

         
        const Quat q = planeRotation(p.state);
        rt->position = toVec(p.state.pos);
        rt->rotation = q;
        const Vec3 pos = rt->position;

         
        p.dropCooldown = std::max(0.0f, p.dropCooldown - dt);
        if (active && !v.wrecked && g_cam.valid)
        {
            {
                std::scoped_lock lock(g_tankMutex);
                if (g_tankCtl.hasAim)
                    p.aim = g_tankCtl.aim;  
                else
                {
                    const Vec3 from = g_cam.pos + g_cam.dir * 3.0f, to = g_cam.pos + g_cam.dir * 3000.0f;
                    const Hit h = traceSegment(from, to, p.vehicle);
                    p.aim = h.kind != Hit::None ? h.pos : to;
                }
            }
            updateBombTarget(p, p.aim);
            Vec3 from, vel;
            u32 slot;
            const bool loaded = nextBombRelease(p, q, from, vel, slot);
            const Vec3 target = p.hasTarget ? p.targetPos : p.aim;
            p.targetDistance = length(target - pos);
            if (loaded)
                p.prediction = warzone::predictBomb(toV3(from), toV3(vel), toV3(target));
             
             
            const Vec3 toT = target - from;
            const bool steep = -toT.y >= std::tan(0.61f) * std::sqrt(toT.x * toT.x + toT.z * toT.z);
            if (fire && loaded && p.dropCooldown <= 0.0f && (!scriptedDrop || dropAnyway || (p.hasTarget && p.prediction.inRange && steep)))
            {
                const u32 targetVehicle = p.hasTarget && p.targetId < warzone::BUILDING_LOCK ? static_cast<u32>(p.targetId) : ~0u;
                if (launch(warzone::ProjectileKind::Bomb, from, vel, p.vehicle, targetVehicle, target))
                {
                    if (!particlesOn()) spawnFx(ctx, FX_FLASH, from, 2.5f, 4, 0.15f, {});
                    p.bombs[slot].loaded = false;
                    --p.bombsLeft;
                    ++p.dropped;
                    if (p.hasTarget)
                        ++p.guidedDrops;
                    p.nextBomb = (slot + 1) % PLANE_BOMB_COUNT;
                    p.dropCooldown = 0.4f;
                    p.rearm = 0.0f;
                    playVoice(g_weapons.impactVoices, g_weapons.nextImpact, from, 25.0f, 0.5f, wa::Category::Impacts);
                    std::scoped_lock lock(g_tankMutex);
                    if (g_planeScript.drop > 0)
                        --g_planeScript.drop;
                }
            }
        }
        else if (!active)
            p.hasTarget = false;
         
        p.rearm += dt;
        if (p.bombsLeft < PLANE_BOMB_COUNT && p.rearm > 12.0f && !v.wrecked)
        {
            for (auto& b : p.bombs)
                if (!b.loaded)
                {
                    b.loaded = true;
                    ++p.bombsLeft;
                    break;
                }
            p.rearm = 6.0f;
        }
        for (auto& b : p.bombs)
            if (auto* t = b.part.e.isValid() ? ctx.tryGetComponent<TransformComponent>(b.part.e) : nullptr)
                t->scale = b.loaded ? b.part.restScale : Vec3{0.001f, 0.001f, 0.001f};

         
        const bool jetParticles = particlesOn();
        if (jetParticles && !v.wrecked && !p.crashed) {
            const auto tier = tierAt(pos, 4);
            queueJetExhaust(toV3(pos + rotate(q, PLANE_EXHAUSTS[0])),
                toV3(pos + rotate(q, PLANE_EXHAUSTS[1])), toV3(rotate(q, {0, 0, 1})),
                p.state.throttle, g_destruction.time, dt, tier, p.player);
            const f32 turnLoad = std::hypot(p.state.yawRate, p.state.pitchRate) * p.state.speed / 9.81f;
            const bool vapour = tier == warzone::vfx::NEAR && g_pt.sys.quality > 0 && p.state.speed > 90 && turnLoad > 1.8f;
            for (int side = 0; side < 2; ++side) {
                const Vec3 tip = pos + rotate(q, {side ? 5.3f : -5.3f, 0.1f, -1.8f});
                if (vapour && p.wingTrailActive && length(tip - p.lastWingTips[side]) < 30) {
                    warzone::pt::Spawn puff;
                    puff.style = warzone::pt::S_WING_VAPOUR; puff.life = 0.9f;
                    puff.s0 = 0.18f; puff.s1 = 1.15f; puff.drag = 0.4f; puff.windK = 0.15f; puff.prio = 0;
                    warzone::pt::trail(g_pt.sys, p.wingTrails[side], toV3(p.lastWingTips[side]), toV3(tip), 1.2f, puff);
                } else p.wingTrails[side] = {};
                p.lastWingTips[side] = tip;
            }
            p.wingTrailActive = vapour;
        } else p.wingTrailActive = false;
        if (auto* b = p.burner.isValid() ? ctx.tryGetComponent<TransformComponent>(p.burner) : nullptr)
        {
            const f32 heat = v.wrecked || jetParticles ? 0.0f : std::clamp((p.state.throttle - 0.45f) / 0.55f, 0.0f, 1.0f);
            if (heat > 0.02f)
            {
                const f32 flicker = 0.9f + 0.1f * std::sin(g_destruction.time * 53.0f);
                const f32 s = 3 * (0.9f + 1.9f * heat) * flicker;
                b->position = pos + rotate(q, PLANE_NOZZLE + Vec3{0, -0.5f, 1.9f - 0.4f * s});
                b->scale = {s * 3, s * 3, s};
                if (auto* fb = ctx.tryGetComponent<FlipbookComponent>(p.burner); fb && !fb->playing)
                {
                    fb->playing = true;
                    fb->loop = true;
                    fb->framesPerSecond = 30.0f;
                }
            }
            else
                b->position = {0.0f, PARK_Y, 0.0f};
        }
        p.smoke -= dt;
        if (!p.crashed && p.smoke <= 0.0f && (v.wrecked || v.health.hp < 0.45f * v.health.maxHp))
        {
            p.smoke = v.wrecked ? 0.03f : 0.07f;
            spawnFx(ctx, v.wrecked ? FX_FIRE : FX_PUFF, pos + rotate(q, PLANE_NOZZLE), v.wrecked ? 2.5f : 1.4f, v.wrecked ? 5.0f : 6.0f,
                    v.wrecked ? 0.6f : 3.0f, {0, 0.5f, 0});
        }

         
        v.rotation = q;
        v.velocity = toVec(p.state.dir) * p.state.speed;
        v.box.centre = toV3(pos + rotate(q, PLANE_BOX_CENTRE));
        v.box.axis[0] = toV3(rotate(q, {1, 0, 0}));
        v.box.axis[1] = toV3(rotate(q, {0, 1, 0}));
        v.box.axis[2] = toV3(rotate(q, {0, 0, 1}));
        v.box.half = toV3(PLANE_BOX_HALF);

         
        if (auto* a = ctx.tryGetComponent<AudioEmitterComponent>(p.engine))
        {
            a->volume = p.crashed ? 0.0f : mixAudio(wa::Category::Vehicles, length(pos - g_listener), 60.0f, 0.35f + 0.65f * p.state.throttle);
            if (!p.engineStarted && ctx.services().contains<AudioWorld>())
            {
                p.engineStarted = true;
                ctx.services().write<AudioWorld>()->play(p.engine);
            }
        }
        (void)ground;
    }
}

 
 
bool planeCamera(SystemContext& ctx, TransformComponent& cam, CameraComponent* lens, f32 dt)
{
    PlaneRig* p = playerPlane();
    if (!p)
        return false;
    const auto* rt = ctx.tryGetConstComponent<TransformComponent>(p->root);
    if (!rt)
        return false;
    const f32 yawNow = warzone::planeYaw(p->state.dir);
    if (p->sight)
    {
        cam.position = rt->position + rotate(fromAxisAngle({0, 1, 0}, yawNow), PLANE_HATCH);
        const Vec3 dir = toVec(warzone::dirFromAngles(yawNow + p->sightYaw, SIGHT_BASE_PITCH + p->sightPitch));
        cam.rotation = lookRotation(dir);
        if (lens)
            lens->fov = toRadians(40.0f);
        return true;
    }
    TankControl c;
    {
        std::scoped_lock lock(g_tankMutex);
        if (g_planeScript.flying || g_planeScript.hold || g_tankCtl.hasAim || g_weapons.vehicles[p->vehicle].wrecked)
        {
             
            const f32 k = 1.0f - std::exp(-2.2f * dt);
            g_tankCtl.camYaw = warzone::wrapPi(g_tankCtl.camYaw + warzone::wrapPi(yawNow - g_tankCtl.camYaw) * k);
            g_tankCtl.camPitch += (warzone::planePitch(p->state.dir) - g_tankCtl.camPitch) * k;
        }
        c = g_tankCtl;
    }
    const Vec3 dir{std::cos(c.camPitch) * std::sin(c.camYaw), std::sin(c.camPitch), std::cos(c.camPitch) * std::cos(c.camYaw)};
    Vec3 eye = rt->position - dir * 27.0f + Vec3{0, 5.5f, 0};
    eye = keepAboveGround(eye, 2.0f);
    cam.position = eye;
    cam.rotation = lookRotation(normalize(rt->position + dir * 60.0f - eye));
    if (lens)
        lens->fov = toRadians(64.0f);
    return true;
}

 
void drawPlaneHud(SystemContext& ctx, UiCanvasPainter& p, f32 w, f32 h, const TankControl& c)
{
    PlaneRig* plane = playerPlane();
    const auto* rt = ctx.tryGetConstComponent<TransformComponent>(plane->root);
    if (!rt)
        return;
    const HudColors col;
    const Vec2 mid{w * 0.5f, h * 0.5f};
    const auto& s = plane->state;
    const auto& v = g_weapons.vehicles[plane->vehicle];
    const f32 ground = g_terrain.valid() ? g_terrain.height(s.pos.x, s.pos.z) : 0.0f;
    const f32 kmh = s.speed * 3.6f;
    if (plane->sight)
    {
         
        const u32 g = rgba(150, 240, 150, 0.9f);
        p.line({mid.x - 140, mid.y}, {mid.x - 18, mid.y}, 1.5f, g);
        p.line({mid.x + 18, mid.y}, {mid.x + 140, mid.y}, 1.5f, g);
        p.line({mid.x, mid.y - 140}, {mid.x, mid.y - 18}, 1.5f, g);
        p.line({mid.x, mid.y + 18}, {mid.x, mid.y + 140}, 1.5f, g);
        ring(p, mid, 10.0f, 1.5f, g);
        const f32 r = std::tan(std::atan(BOMB_PICK_RADIUS / std::max(20.0f, length(plane->aim - g_cam.pos)))) / std::tan(g_cam.fov * 0.5f) * h * 0.5f;
        ring(p, mid, std::clamp(r, 20.0f, h * 0.45f), 1.0f, rgba(150, 240, 150, 0.4f), 0.0f, 6.2832f, 40);
        for (int sx : {-1, 1})
            for (int sy : {-1, 1})
            {
                const Vec2 k{mid.x + sx * (h * 0.42f), mid.y + sy * (h * 0.42f)};
                p.line(k, {k.x - sx * 40.0f, k.y}, 2.0f, g);
                p.line(k, {k.x, k.y - sy * 40.0f}, 2.0f, g);
            }
        p.text({mid.x - 200, mid.y - h * 0.42f - 30}, {400, 20}, std::format("BOMB SIGHT   {:.0f} m   ALT {:.0f} m", length(plane->aim - g_cam.pos), s.pos.y - ground), g,
               UiCanvasAlign::Center, 15.0f);
    }
    else
    {
         
        ring(p, mid, 14.0f, 1.5f, col.white);
        p.rect({mid.x - 1.5f, mid.y - 1.5f}, {3, 3}, col.white, 1.5f);
        Vec2 fp;
        if (project(rt->position + toVec(s.dir) * 600.0f, w, h, fp))
        {
            ring(p, fp, 9.0f, 2.0f, col.green);
            p.line({fp.x - 22, fp.y}, {fp.x - 9, fp.y}, 2.0f, col.green);
            p.line({fp.x + 9, fp.y}, {fp.x + 22, fp.y}, 2.0f, col.green);
            p.line({fp.x, fp.y - 9}, {fp.x, fp.y - 17}, 2.0f, col.green);
        }
    }
     
    Vec2 tp;
    if (plane->hasTarget && project(plane->targetPos, w, h, tp))
    {
        const bool ok = plane->prediction.inRange && plane->bombsLeft > 0;
        const u32 lc = ok ? col.green : col.amber;
        const f32 dist = std::max(1.0f, length(plane->targetPos - g_cam.pos));
        const f32 sz = plane->targetClass == warzone::TargetClass::Unit ? 6.0f : 10.0f;
        const f32 half = std::clamp(sz / dist / std::tan(g_cam.fov * 0.5f) * h * 0.5f, 16.0f, 120.0f);
        const f32 arm = std::max(6.0f, half * 0.4f);
        for (int sx : {-1, 1})
            for (int sy : {-1, 1})
            {
                const Vec2 k{tp.x + sx * half, tp.y + sy * half};
                p.line(k, {k.x - sx * arm, k.y}, 2.5f, lc);
                p.line(k, {k.x, k.y - sy * arm}, 2.5f, lc);
            }
        p.rect({tp.x + half + 10, tp.y - half}, {230, 66}, col.panel, 4.0f);
        p.text({tp.x + half + 16, tp.y - half + 4}, {220, 18}, std::format("{}  {}", warzone::targetClassName(plane->targetClass), plane->targetName), lc,
               UiCanvasAlign::Left, 14.0f);
        p.text({tp.x + half + 16, tp.y - half + 24}, {220, 18},
               std::format("{:.0f} m   TTI {:.1f} s", plane->targetDistance, plane->prediction.timeToImpact), col.white, UiCanvasAlign::Left, 14.0f);
        p.text({tp.x + half + 16, tp.y - half + 44}, {220, 16},
               plane->bombsLeft == 0 ? std::string("NO BOMBS") : ok ? std::string("IN RANGE - RELEASE") : std::format("OUT OF RANGE ({:.0f} m)", plane->prediction.miss),
               lc, UiCanvasAlign::Left, 12.0f);
    }
     
    p.rect({mid.x - 260, mid.y - 34}, {110, 68}, col.panel, 4.0f);
    p.text({mid.x - 256, mid.y - 30}, {100, 16}, "SPEED", col.dim, UiCanvasAlign::Left, 12.0f);
    p.text({mid.x - 256, mid.y - 12}, {100, 26}, std::format("{:.0f}", kmh), s.stalled ? col.red : col.white, UiCanvasAlign::Left, 22.0f);
    p.text({mid.x - 256, mid.y + 14}, {100, 16}, std::format("km/h  THR {:.0f}%", s.throttle * 100.0f), col.dim, UiCanvasAlign::Left, 11.0f);
    p.rect({mid.x + 150, mid.y - 34}, {110, 68}, col.panel, 4.0f);
    p.text({mid.x + 154, mid.y - 30}, {100, 16}, "ALT", col.dim, UiCanvasAlign::Left, 12.0f);
    p.text({mid.x + 154, mid.y - 12}, {100, 26}, std::format("{:.0f} m", s.pos.y - ground), col.white, UiCanvasAlign::Left, 22.0f);
    p.text({mid.x + 154, mid.y + 14}, {100, 16}, std::format("VS {:+.0f}  ASL {:.0f}", s.dir.y * s.speed, s.pos.y), col.dim, UiCanvasAlign::Left, 11.0f);
    if (plane->outside && !v.wrecked)
        p.text({mid.x - 200, mid.y + 104}, {400, 24}, "LEAVING THE BATTLE - TURNING BACK", col.amber, UiCanvasAlign::Center, 18.0f);
    if (s.stalled && !v.wrecked && std::fmod(g_destruction.time, 0.6f) < 0.4f)
        p.text({mid.x - 150, mid.y + 70}, {300, 30}, "STALL", col.red, UiCanvasAlign::Center, 26.0f);
    else if (!v.wrecked && s.pos.y - ground < 60.0f && s.dir.y < -0.15f)
        p.text({mid.x - 150, mid.y + 70}, {300, 30}, "PULL UP", col.red, UiCanvasAlign::Center, 26.0f);
     
    const Vec2 o = drawCommonHud(p, w, h, plane->vehicle, rt->position, warzone::planeYaw(s.dir), plane->sight ? warzone::planeYaw(s.dir) : c.camYaw,
                                 "MIG  |  GUIDED BOMBS");
    const Vec2 ic{o.x + 62, o.y + 76};
    ring(p, ic, 44.0f, 2.0f, col.dim, 0.0f, 6.2832f, 32);
    const f32 off = std::clamp(warzone::planePitch(s.dir) * 90.0f, -36.0f, 36.0f);
    const Vec2 dirH{std::cos(s.roll), std::sin(s.roll)};
    const Vec2 cH{ic.x, ic.y + off};
    p.line({cH.x - dirH.x * 38, cH.y - dirH.y * 38}, {cH.x + dirH.x * 38, cH.y + dirH.y * 38}, 2.0f, col.green);
    p.line({ic.x - 14, ic.y}, {ic.x - 5, ic.y}, 2.5f, col.amber);
    p.line({ic.x + 5, ic.y}, {ic.x + 14, ic.y}, 2.5f, col.amber);
    p.text({o.x + 130, o.y + 50}, {160, 22}, std::format("{:.0f} km/h", kmh), col.white, UiCanvasAlign::Left, 20.0f);
    p.text({o.x + 130, o.y + 80}, {160, 18}, std::format("GUIDED BOMB  x{}/{}", plane->bombsLeft, PLANE_BOMB_COUNT), col.white, UiCanvasAlign::Left, 14.0f);
    for (u32 i = 0; i < PLANE_BOMB_COUNT; ++i)
        p.rect({o.x + 130 + i * 18.0f, o.y + 102}, {12, 6}, plane->bombs[i].loaded ? col.green : rgba(60, 60, 56, 0.8f), 1.5f);
    p.text({o.x + 130, o.y + 120}, {170, 18}, c.scripted ? std::string("AUTOPILOT") : plane->sight ? std::string("SIGHT - COURSE HELD") : std::string("RMB: BOMB SIGHT"),
           c.scripted ? col.amber : col.dim, UiCanvasAlign::Left, 12.0f);
}

 
void registerPlaneSystems(World world)
{
    SystemDescriptor sys;
    sys.moduleName = MODULE;
    sys.name = "WarzonePlane";
    sys.fn = planeSystem;
    sys.update = SystemPhaseDescriptor{.reads = {getComponentId<WarzonePlane>()},
                                       .writes = {getComponentId<TransformComponent>(), getComponentId<AudioEmitterComponent>(),
                                                  getComponentId<FlipbookComponent>(), getComponentId<PointLightComponent>(),
                                                  getComponentId<RenderTintComponent>()}};
    world.registerSystem(sys);
}

void registerPlaneCommands(const ModuleContext& context)
{
    auto& control = context.control;
    const std::string_view owner = MODULE;
    control.registerCommand(
        "warzonePlane", owner,
        "flies the player airplane by script (filming, tests) and makes it the active vehicle: teleport [x,y,z] heading (compass deg) "
        "speed (m/s) puts it in the air; path [[x,y,z],...] / goto [x,y,z] flies there then holds level; hold [altitude] flies level; "
        "throttle 0-1; sight=true holds the bomb-hatch sight (right mouse), aim [x,y,z] or aimAt <vehicle or building> points the sight "
        "(and the target pick); drop n bombs, each once the target is in range (anyway=true drops at once); release gives control back; "
        "camera=true switches the game camera to the plane",
        {optionalArg("teleport", ArgKind::Json, "[x,y,z] place in the air"), optionalArg("heading", ArgKind::Number, "compass deg for teleport", "0"),
         optionalArg("speed", ArgKind::Number, "m/s for teleport", "120"), optionalArg("goto", ArgKind::Json, "[x,y,z] fly there"),
         optionalArg("path", ArgKind::Json, "[[x,y,z],...] waypoints"), optionalArg("hold", ArgKind::Number, "fly level at this altitude (m above sea level)"),
         optionalArg("throttle", ArgKind::Number, "0..1 throttle setting"), optionalArg("sight", ArgKind::Boolean, "hold the bomb sight"),
         optionalArg("aim", ArgKind::Json, "[x,y,z] sight / target point"), optionalArg("aimAt", ArgKind::String, "vehicle or building name"),
         optionalArg("drop", ArgKind::Integer, "bombs to drop"), optionalArg("anyway", ArgKind::Boolean, "drop without waiting for range"),
         optionalArg("release", ArgKind::Boolean, "hand control back to the player"), optionalArg("camera", ArgKind::Boolean, "game camera follows the plane")},
        [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            if (!control.world("play") || !playerPlane())
                return "warzonePlane needs Play running with a player airplane (WarzonePlane role 0)";
            std::scoped_lock lock(g_tankMutex);
            setActiveLocked(Active::Plane);
            auto& c = g_tankCtl;
            auto& sc = g_planeScript;
            if (args.boolean("release").value_or(false))
            {
                sc = PlaneScript{};
                c.hasAim = false;
                c.scripted = false;
                c.scriptZoom = false;
                c.released = true;
            }
            Vec3 p;
            if (vec3Arg(args, "teleport", p))
            {
                sc.teleport = true;
                sc.teleportTo = p;
                sc.teleportYaw = toRadians(180.0f - number(args, "heading").value_or(0.0f));
                sc.teleportSpeed = number(args, "speed").value_or(120.0f);
                sc.flying = false;
                sc.path.clear();
                sc.hold = true;
                sc.holdAlt = p.y;
            }
            std::vector<Vec3> path;
            if (vec3ListArg(args, "path", path) || (vec3Arg(args, "goto", p) && (path = {p}, true)))
            {
                sc.path = path;
                sc.flying = true;
                sc.hold = false;
            }
            if (auto a = number(args, "hold"))
            {
                sc.hold = true;
                sc.flying = false;
                sc.path.clear();
                sc.holdAlt = *a;
            }
            if (auto t = number(args, "throttle"))
            {
                sc.hasThrottle = true;
                sc.throttle = std::clamp(*t, 0.0f, 1.0f);
            }
            if (auto s = args.boolean("sight"))
                sc.sight = *s;
            if (vec3Arg(args, "aim", p))
            {
                c.aim = p;
                c.hasAim = true;
            }
            if (auto target = args.string("aimAt"))
            {
                bool ok;
                const Vec3 at = aimPointForName(*target, ok);
                if (!ok)
                    return std::format("no building or vehicle '{}'", *target);
                c.aim = at;
                c.hasAim = true;
            }
            if (auto n = args.integer("drop"))
                sc.drop = static_cast<u32>(std::max<long long>(0, static_cast<long long>(*n)));
            if (auto a = args.boolean("anyway"))
                sc.dropAnyway = *a;
            if (args.boolean("camera").value_or(false))
            {
                g_cameraCommand.pending = true;
                g_cameraCommand.mode = 9;
                g_cameraCommand.spot = -1;
                g_cameraCommand.target.clear();
            }
            c.scripted = sc.flying || sc.hold;
            result.writeBool("flying", sc.flying);
            result.writeU32("waypoints", static_cast<u32>(sc.path.size()));
            result.writeBool("sight", sc.sight);
            result.writeBool("hasAim", c.hasAim);
            result.writeU32("dropQueue", sc.drop);
            return {};
        });

    control.registerQuery(
        "warzonePlaneState", owner,
        "the player airplane in Play: position, heading, speed, throttle, stall, altitude above ground, climb, attitude, bombs, the bomb "
        "sight, the target near the aim (class, name, distance, time to impact, in range), health, autopilot, input focus",
        {}, [&control](const ControlArgs&, IFormatWriter& result) -> std::string
        {
            const World* play = control.world("play");
            PlaneRig* pl = playerPlane();
            if (!play || !pl)
                return "no player airplane (start Play)";
            const auto& v = g_weapons.vehicles[pl->vehicle];
            const auto& s = pl->state;
            const f32 pos[3] = {s.pos.x, s.pos.y, s.pos.z};
            const f32 ground = g_terrain.valid() ? g_terrain.height(s.pos.x, s.pos.z) : 0.0f;
            result.writeString("name", v.name);
            result.writeBool("active", g_active == Active::Plane);
            result.writeFloats("position", pos);
            result.writeF32("headingDeg", std::fmod(360.0f + 180.0f - warzone::planeYaw(s.dir) * 57.2958f, 360.0f));
            result.writeF32("kmh", s.speed * 3.6f);
            result.writeF32("speed", s.speed);
            result.writeF32("throttle", s.throttle);
            result.writeBool("stalled", s.stalled);
            result.writeF32("liftFrac", s.liftFrac);
            result.writeF32("altitude", s.pos.y - ground);
            result.writeF32("climb", s.dir.y * s.speed);
            result.writeF32("pitchDeg", warzone::planePitch(s.dir) * 57.2958f);
            result.writeF32("rollDeg", s.roll * 57.2958f);
            result.writeF32("yawRate", s.yawRate);
            result.writeU32("bombs", pl->bombsLeft);
            result.writeU32("dropped", pl->dropped);
            result.writeU32("guidedDrops", pl->guidedDrops);
            result.writeBool("sight", pl->sight);
            result.writeF32("sightYawDeg", pl->sightYaw * 57.2958f);
            result.writeF32("sightPitchDeg", (SIGHT_BASE_PITCH + pl->sightPitch) * 57.2958f);
            const f32 a[3] = {pl->aim.x, pl->aim.y, pl->aim.z};
            result.writeFloats("aim", a);
            result.writeBool("hasTarget", pl->hasTarget);
            result.writeString("targetClass", pl->hasTarget ? warzone::targetClassName(pl->targetClass) : "");
            result.writeString("targetName", pl->targetName);
            const f32 tp[3] = {pl->targetPos.x, pl->targetPos.y, pl->targetPos.z};
            result.writeFloats("targetPos", tp);
            result.writeF32("targetDistance", pl->targetDistance);
            result.writeF32("timeToImpact", pl->prediction.timeToImpact);
            result.writeF32("predictedMiss", pl->prediction.miss);
            result.writeBool("inRange", pl->prediction.inRange);
            result.writeF32("hp", v.health.hp);
            result.writeBool("wrecked", v.wrecked);
            result.writeBool("crashed", pl->crashed);
            result.writeU32("crashes", pl->crashes);
            std::scoped_lock lock(g_tankMutex);
            result.writeBool("autopilot", g_planeScript.flying || g_planeScript.hold);
            result.writeU32("waypoints", static_cast<u32>(g_planeScript.path.size()));
            result.writeU32("dropQueue", g_planeScript.drop);
            result.writeF32("camYawDeg", g_tankCtl.camYaw * 57.2958f);
            result.writeF32("camPitchDeg", g_tankCtl.camPitch * 57.2958f);
            result.writeBool("gameFocus", g_tankCtl.gameFocus);
            return {};
        });
}
