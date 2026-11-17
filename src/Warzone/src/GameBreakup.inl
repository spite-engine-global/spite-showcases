 
 
 
 
 
 

constexpr u32 BREAK_MAX_PARTS = 140;    
constexpr u32 BREAK_MAX_MESHES = 900;   

struct PieceMesh
{
    Entity e = Entity::undefined();
    Vec3 offset{};  
    Quat rot = Quat::identity();
    Vec3 scale{1, 1, 1};
};

struct FlyingPart
{
    std::vector<PieceMesh> meshes;
    warzone::ThrownState s;
    Quat rot = Quat::identity();
    Vec3 spinAxis{1, 0, 0};
    f32 spinRate = 0.0f;
    bool main = false;
    FxSlot* fire = nullptr;
    f32 puff = 0.0f, burn = 0.0f;
    f32 ptAcc[3]{};                   
    warzone::pt::TrailState ptTrail;  
};

struct PlayBreakup
{
    std::vector<FlyingPart> parts;
    std::vector<u32> queue;               
    std::vector<std::string> killRequests;  
    warzone::Rng rng{6262};
    u32 deaths = 0, broken = 0, mainOnly = 0, none = 0, thrown = 0, evicted = 0, landed = 0;
};
PlayBreakup g_break;
std::mutex g_breakMutex;

void queueBreakup(u32 vi) { g_break.queue.push_back(vi); }

Entity parentIn(const SystemContext& ctx, Entity e)
{
    return ctx.services().contains<EntityTransformManager>() ? ctx.services().read<EntityTransformManager>()->attachedParentOf(e) : Entity::undefined();
}

void subtreeIn(const SystemContext& ctx, Entity root, std::vector<Entity>& out)
{
    out.clear();
    if (ctx.services().contains<EntityTransformManager>())
        ctx.services().read<EntityTransformManager>()->collectSubtree(root, out);
}

 
bool worldXf(const SystemContext& ctx, Entity e, Xf& out)
{
    std::vector<Entity> chain;
    for (Entity p = e; p.isValid(); p = parentIn(ctx, p))
    {
        chain.push_back(p);
        if (chain.size() > 32)
            return false;
    }
    out = {};
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        const auto* t = ctx.tryGetConstComponent<TransformComponent>(*it);
        if (!t)
            return false;
        out = compose(out, Xf{t->rotation, t->position, t->scale.x});
    }
    return true;
}

Entity findNamed(const SystemContext& ctx, const std::vector<Entity>& all, std::string_view name)
{
    for (Entity e : all)
        if (const auto* n = ctx.tryGetConstComponent<EntityNameComponent>(e); n && n->name.toView() == name)
            return e;
    return Entity::undefined();
}

 
void throwPart(SystemContext& ctx, u32 vi, Entity node, const Vec3* pivotOverride, f32 radius, int partIndex)
{
    auto& v = g_weapons.vehicles[vi];
    auto& cb = ctx.getCommandBuffer();
    std::vector<Entity> sub;
    subtreeIn(ctx, node, sub);
    Xf nodeXf;
    if (!worldXf(ctx, node, nodeXf))
        return;
    FlyingPart part;
    const Vec3 pivot = pivotOverride ? *pivotOverride : nodeXf.t;
    for (Entity m : sub)
    {
        const auto* mesh = ctx.tryGetConstComponent<MeshComponent>(m);
        const auto* mat = ctx.tryGetConstComponent<MaterialComponent>(m);
        Xf mx;
        if (!mesh || !mat || !worldXf(ctx, m, mx))
            continue;
        if (mx.s < 1e-3f)  
            continue;
        const Entity c = cb.createEntity();
        TransformComponent t;
        t.position = mx.t;
        t.rotation = mx.q;
        t.scale = {mx.s, mx.s, mx.s};
        cb.addComponent<TransformComponent>(c, std::move(t));
        cb.addComponent<MeshComponent>(c, MeshComponent(*mesh));
        cb.addComponent<MaterialComponent>(c, MaterialComponent(*mat));
        RenderTintComponent tint;
        tint.color = Vec4{0.2f, 0.18f, 0.16f, 1.0f};  
        cb.addComponent<RenderTintComponent>(c, std::move(tint));
        part.meshes.push_back({c, mx.t - pivot, mx.q, {mx.s, mx.s, mx.s}});
    }
    if (part.meshes.empty())
        return;
    if (auto* t = ctx.tryGetComponent<TransformComponent>(node))
    {
        v.hidden.push_back({node, t->scale});
        t->scale = {1e-4f, 1e-4f, 1e-4f};
    }
    const warzone::Launch l = warzone::launchFor(v.type, partIndex, toV3(v.velocity), g_break.rng);
    part.s.pos = toV3(pivot);
    part.s.vel = l.vel;
    part.s.radius = radius;
    part.spinRate = l.spinRate;
    part.spinAxis = normalize(Vec3{g_break.rng.range(-1.0f, 1.0f), g_break.rng.range(-0.4f, 0.4f), g_break.rng.range(-1.0f, 1.0f)} + Vec3{0.01f, 0, 0});
    if (v.type == warzone::VehicleType::Heli)
        part.spinAxis = normalize(Vec3{g_break.rng.range(-0.3f, 0.3f), 1.0f, g_break.rng.range(-0.3f, 0.3f)});  
    part.main = partIndex == 0;
    part.burn = part.main ? 25.0f : 6.0f;
    if (part.main && length(pivot - g_cam.pos) < 1500.0f && !particlesOn())
        part.fire = spawnFx(ctx, FX_FLAME, pivot, 4.0f, 4.0f, part.burn, {}, true);
    g_break.parts.push_back(std::move(part));
    ++g_break.thrown;
}

void breakUp(SystemContext& ctx, u32 vi)
{
    auto& v = g_weapons.vehicles[vi];
    ++g_break.deaths;
    const Entity root = v.ai ? v.breakRoot : v.root;
    const int detail = warzone::breakupDetail(length(toVec(v.box.centre) - g_cam.pos));
    if (!root.isValid() || detail == 0)
    {
        ++g_break.none;
        return;
    }
    std::vector<Entity> all;
    subtreeIn(ctx, root, all);
    Vec3 c, h;
    aiBox(v.type, c, h);
    const Vec3 rootPos = toVec(v.box.centre) - rotate(v.rotation, c);
    switch (v.type)
    {
    case warzone::VehicleType::Tank:
    case warzone::VehicleType::Amp:
    {
        const GroundDef& d = v.type == warzone::VehicleType::Tank ? TANK_DEF : AMP_DEF;
        const Vec3 ring = rootPos + rotate(v.rotation, d.turretPivot + Vec3{0, 0.6f, 0});
        const Entity turret = findNamed(ctx, all, d.turretNode), gun = findNamed(ctx, all, d.gunNode);
        if (turret.isValid())
            throwPart(ctx, vi, turret, &ring, v.type == warzone::VehicleType::Tank ? 1.1f : 0.9f, 0);
         
        bool gunUnderTurret = false;
        for (Entity p = gun; p.isValid(); p = parentIn(ctx, p))
            gunUnderTurret = gunUnderTurret || p == turret;
        if (gun.isValid() && !gunUnderTurret)
            throwPart(ctx, vi, gun, nullptr, 0.4f, 1);
        if (detail >= 2 && v.type == warzone::VehicleType::Tank)
            for (const char* n : {SPROCKET_LEFT.node, SPROCKET_RIGHT.node, IDLER.node})
                if (const Entity e = findNamed(ctx, all, n); e.isValid())
                    throwPart(ctx, vi, e, nullptr, 0.45f, 2);
        break;
    }
    case warzone::VehicleType::Heli:
        if (const Entity e = findNamed(ctx, all, "blades"); e.isValid())
            throwPart(ctx, vi, e, nullptr, 0.25f, 0);
        if (detail >= 2)
            if (const Entity e = findNamed(ctx, all, "rear_blades"); e.isValid())
                throwPart(ctx, vi, e, nullptr, 0.3f, 1);
        break;
    default:
    {
         
        int k = 0;
        for (Entity e : all)
        {
            if (!ctx.tryGetConstComponent<MeshComponent>(e))
                continue;
            if (detail < 2 && k > 0)
                break;
            throwPart(ctx, vi, e, nullptr, 0.8f, k++);
        }
        break;
    }
    }
    (detail >= 2 ? g_break.broken : g_break.mainOnly)++;
}

void restoreHidden(SystemContext& ctx, VehicleSlot& v)
{
    for (auto& [e, s] : v.hidden)
        if (auto* t = ctx.tryGetComponent<TransformComponent>(e))
            t->scale = s;
    v.hidden.clear();
}

void removePart(SystemContext& ctx, FlyingPart& p)
{
    auto& cb = ctx.getCommandBuffer();
    for (auto& m : p.meshes)
        if (tracked(ctx, m.e))
            cb.destroyEntity(m.e);
    if (p.fire && p.fire->loop)
        p.fire->life = std::max(0.0f, p.fire->age);
    p.meshes.clear();
}

void breakupSystem(SystemContext& ctx)
{
    if (!g_weapons.ready)
        return;
    const f32 dt = std::min(ctx.deltaTime, 0.05f);
    auto& b = g_break;
    {
        std::vector<std::string> kills;
        {
            std::scoped_lock lock(g_breakMutex);
            kills.swap(b.killRequests);
        }
        for (const auto& name : kills)
            if (const int vi = vehicleByName(name); vi >= 0 && !g_weapons.vehicles[vi].wrecked)
            {
                auto& v = g_weapons.vehicles[vi];
                damageVehicle(ctx, static_cast<u32>(vi), v.health.hp + 1.0f, toVec(v.box.centre) + Vec3{0, 30, 0}, static_cast<u32>(vi));
            }
    }
    for (u32 vi : b.queue)
        if (vi < g_weapons.vehicles.size())
            breakUp(ctx, vi);
    b.queue.clear();
    auto ground = [](f32 x, f32 z) { return g_terrain.valid() ? g_terrain.height(x, z) : 0.0f; };
    u32 meshes = 0;
    for (auto& p : b.parts)
    {
        const bool hit = warzone::stepPiece(p.s, dt, ground);
        if (hit && p.s.phase == warzone::ThrownPhase::Resting)
            ++b.landed;
        if (hit && p.main && p.s.bounces == 1)
            spawnFx(ctx, FX_DUST, toVec(p.s.pos), 2.0f, 6.0f, 3.0f, {0, 1.0f, 0});
        if (p.s.phase == warzone::ThrownPhase::Flying)
            p.rot = normalize(multiply(fromAxisAngle(p.spinAxis, p.spinRate * p.s.spin * dt), p.rot));
        const Vec3 pivot = toVec(p.s.pos);
         
        p.burn -= dt;
        if (p.fire)
        {
            if (p.fire->age < 0.0f || !p.fire->loop || p.burn <= 0.0f)
                p.fire = nullptr;
            else
                p.fire->pos = pivot + Vec3{0, 1.3f, 0}, p.fire->vel = {};
        }
        if (particlesOn() && p.burn > 0.0f)
        {
             
            const warzone::vfx::Tier tier = tierAt(pivot, 3.0f);
            if (p.main)
                warzone::pt::wreck(g_pt.sys, toV3(pivot), 0.55f, p.burn, 25.0f, tier, dt, p.ptAcc);
            if (p.s.phase == warzone::ThrownPhase::Flying)
            {
                warzone::pt::Spawn puff = warzone::pt::trailPuff(true, tier);
                puff.style = warzone::pt::S_SMOKE, puff.tint = warzone::pt::grey(0.5f, 0.9f), puff.life = 3.5f;
                const Vec3 back = pivot - toVec(p.s.vel) * dt;
                warzone::pt::trail(g_pt.sys, p.ptTrail, toV3(back), toV3(pivot), warzone::pt::trailSpacing(true, tier) * (p.main ? 1.0f : 1.6f), puff);
            }
        }
        else if (p.burn > 0.0f && (p.puff -= dt) <= 0.0f)
        {
            const warzone::vfx::Tier tier = tierAt(pivot, 3.0f);
            p.puff = warzone::vfx::partPuffInterval(p.s.phase == warzone::ThrownPhase::Flying, tier);  
            const f32 boost = warzone::vfx::distanceBoost(length(pivot - g_listener));
            tintFx(ctx, spawnFx(ctx, FX_PUFF, pivot + Vec3{0, 0.5f, 0}, (p.main ? 2.0f : 1.1f) * boost, (p.main ? 5.5f : 3.0f) * boost, 3.0f, {0, 1.6f, 0}),
                   {0.4f, 0.4f, 0.4f, 0.9f});
        }
        for (auto& m : p.meshes)
            if (auto* t = ctx.tryGetComponent<TransformComponent>(m.e))
            {
                t->position = pivot + rotate(p.rot, m.offset);
                t->rotation = normalize(multiply(p.rot, m.rot));
            }
        meshes += static_cast<u32>(p.meshes.size());
    }
     
    for (auto& p : b.parts)
        if (p.s.phase == warzone::ThrownPhase::Gone)
            removePart(ctx, p);
    std::vector<f32> ages;
    for (const auto& p : b.parts)
        ages.push_back(p.meshes.empty() ? 1e9f : p.s.age);
    size_t cap = BREAK_MAX_PARTS;
    if (meshes > BREAK_MAX_MESHES && !b.parts.empty())
        cap = std::min<size_t>(cap, b.parts.size() * BREAK_MAX_MESHES / meshes);
    for (size_t i : warzone::overCap(ages, cap))
        if (!b.parts[i].meshes.empty())
        {
            removePart(ctx, b.parts[i]);
            ++b.evicted;
        }
    std::erase_if(b.parts, [](const FlyingPart& p) { return p.meshes.empty(); });
}

void registerBreakupSystem(World world)
{
    SystemDescriptor sys;
    sys.moduleName = MODULE;
    sys.name = "WarzoneBreakup";
    sys.fn = breakupSystem;
    sys.update = SystemPhaseDescriptor{
        .reads = {getComponentId<MeshComponent>(), getComponentId<MaterialComponent>(), getComponentId<EntityNameComponent>()},
        .writes = {getComponentId<TransformComponent>(), getComponentId<RenderTintComponent>(), getComponentId<FlipbookComponent>(),
                   getComponentId<PointLightComponent>(), getComponentId<AudioEmitterComponent>()}};
    world.registerSystem(sys);
}

void attachBreakup()
{
    std::scoped_lock lock(g_breakMutex);
    g_break = PlayBreakup{};
}

void registerBreakupCommands(const ModuleContext& context)
{
    auto& control = context.control;
    const std::string_view owner = MODULE;
    control.registerCommand("warzoneKill", owner, "destroys a vehicle by name in Play (it blows apart near the camera)",
                            {requiredArg("vehicle", ArgKind::String, "vehicle name, e.g. \"Target Tank 1\" or \"AI Heli 3\"")},
                            [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
                            {
                                if (!control.world("play"))
                                    return "warzoneKill needs Play running";
                                const std::string name(args.string("vehicle").value_or(""));
                                if (vehicleByName(name) < 0)
                                    return std::format("no vehicle named '{}'", name);
                                std::scoped_lock lock(g_breakMutex);
                                g_break.killRequests.push_back(name);
                                result.writeString("queued", name);
                                return {};
                            });
    control.registerQuery("warzoneBreakupState", owner, "vehicle break-up: deaths, full / main-part / none, parts thrown, live, landed, evicted",
                          {}, [](const ControlArgs&, IFormatWriter& result) -> std::string
                          {
                              const auto& b = g_break;
                              u32 flying = 0, resting = 0, meshes = 0, burning = 0;
                              for (const auto& p : b.parts)
                              {
                                  flying += p.s.phase == warzone::ThrownPhase::Flying;
                                  resting += p.s.phase == warzone::ThrownPhase::Resting;
                                  burning += p.burn > 0.0f;
                                  meshes += static_cast<u32>(p.meshes.size());
                              }
                              result.writeU32("deaths", b.deaths);
                              result.writeU32("fullBreakups", b.broken);
                              result.writeU32("mainPartOnly", b.mainOnly);
                              result.writeU32("noBreakup", b.none);
                              result.writeU32("partsThrown", b.thrown);
                              result.writeU32("live", static_cast<u32>(b.parts.size()));
                              result.writeU32("flying", flying);
                              result.writeU32("resting", resting);
                              result.writeU32("burning", burning);
                              result.writeU32("meshes", meshes);
                              result.writeU32("landed", b.landed);
                              result.writeU32("evicted", b.evicted);
                              result.beginArray("recent", static_cast<u32>(std::min<size_t>(b.parts.size(), 6)));
                              for (size_t i = b.parts.size() - std::min<size_t>(b.parts.size(), 6); i < b.parts.size(); ++i)
                              {
                                  const auto& p = b.parts[i];
                                  result.beginObject({});
                                  result.writeFloats("pos", std::array<f32, 3>{p.s.pos.x, p.s.pos.y, p.s.pos.z});
                                  result.writeU32("phase", static_cast<u32>(p.s.phase));
                                  result.writeU32("bounces", static_cast<u32>(p.s.bounces));
                                  result.writeU32("meshes", static_cast<u32>(p.meshes.size()));
                                  result.writeF32("age", p.s.age);
                                  result.endObject();
                              }
                              result.endArray();
                              return {};
                          });
}
