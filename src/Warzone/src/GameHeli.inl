 
 
 
 
 
 

 
 
const Vec3 HELI_TILT_PIVOT{0.0f, 2.6f, 0.0f};
const Vec3 HELI_BOX_CENTRE{0.0f, 2.1f, -2.6f};
const Vec3 HELI_BOX_HALF{1.9f, 2.0f, 8.4f};
constexpr std::array<const char*, 8> HELI_ROCKETS = {"rocket", "rocket.004", "rocket.001", "rocket.005", "rocket.002", "rocket.006", "rocket.003", "rocket.007"};
constexpr u32 HELI_MISSILES = 8;
constexpr f32 LOCK_CONE = 0.04f;    
constexpr f32 LOCK_RANGE = 1500.0f;

struct HeliRig
{
    Entity root = Entity::undefined(), model = Entity::undefined(), engine = Entity::undefined();
    Vec3 modelRestPos{};
    Quat modelRestRot = Quat::identity();
    PartRig rotor, tail;
    struct Rocket
    {
        PartRig part;
        Vec3 hull{};  
        bool loaded = true;
    };
    std::array<Rocket, HELI_MISSILES> rockets;
    u32 vehicle = 0;
    bool player = false, crashed = false;
    warzone::HeliParams params;
    warzone::HeliState state;
    warzone::LockParams lockParams;
    warzone::LockState lock;
    Vec3 lockPoint{};         
    std::string lockName;     
    u32 missiles = HELI_MISSILES, nextRocket = 0, launched = 0, lockedLaunches = 0;
    f32 launchCooldown = 0.0f, rearm = 0.0f, downwash = 0.0f, ptWash = 0.0f;  
    Vec3 aim{};
    f32 range = 0.0f;
    f32 cyclicF = 0.0f, cyclicS = 0.0f;  
    u32 bumps = 0;
    f32 bumpDamage = 0.0f;
    bool engineStarted = false;
};

 
struct HeliScript
{
    bool flying = false;           
    std::vector<Vec3> path;        
    bool hover = false;
    f32 speedLimit = 1.0f;
    u32 fire = 0;                  
    bool fireUnlocked = false;
    f32 climb = 0.0f, forward = 0.0f, strafe = 0.0f, seconds = 0.0f;
    bool teleport = false, resetLock = false;  
    Vec3 teleportTo{};
    f32 teleportYaw = 0.0f;
};

std::vector<HeliRig> g_helis;
HeliScript g_heliScript;  
bool g_heliSightWas = false;  

HeliRig* playerHeli()
{
    for (auto& h : g_helis)
        if (h.player)
            return &h;
    return nullptr;
}

bool heliHudReady()
{
    const HeliRig* h = playerHeli();
    return h && h->vehicle < g_weapons.vehicles.size();
}

 
void attachHelis(const ModuleContext& context, World world)
{
    (void)context;
    g_helis.clear();
    {
        std::scoped_lock lock(g_tankMutex);
        g_heliScript = HeliScript{};
    }
    std::vector<std::pair<Entity, WarzoneHeli>> found;
    for (auto [e, heli] : world.createQueryBuilder().buildQuery<Read<WarzoneHeli>>()->view<Entity, Read<WarzoneHeli>>())
        found.emplace_back(e, heli);
    std::vector<std::pair<Entity, Entity>> edges;
    for (auto& [e, heli] : found)
    {
        HeliRig rig;
        rig.root = e;
        rig.player = heli.role == 0;
        rig.model = findInSubtree(world, e, "Heli Model");
        if (!rig.model.isValid())
        {
            SWARNING_LOG("Warzone: helicopter {} has no 'Heli Model' child", e.id());
            continue;
        }
        const auto& mt = world.getComponent<TransformComponent>(rig.model);
        rig.modelRestPos = mt.position;
        rig.modelRestRot = mt.rotation;
         
        auto origin = [&](Entity part)
        {
            PartRig tmp;
            tmp.setup(world, part, e, {0, 1, 0}, {});
            return tmp.frame.apply(tmp.restPos);
        };
        const Entity blades = findInSubtree(world, e, "blades"), tailBlades = findInSubtree(world, e, "rear_blades");
        if (blades.isValid())
            rig.rotor.setup(world, blades, e, {0, 1, 0}, origin(blades));
        if (tailBlades.isValid())
            rig.tail.setup(world, tailBlades, e, {1, 0, 0}, origin(tailBlades));
        for (u32 i = 0; i < HELI_MISSILES; ++i)
        {
            const Entity r = findInSubtree(world, e, HELI_ROCKETS[i]);
            if (r.isValid())
            {
                rig.rockets[i].part.setup(world, r, e, {0, 1, 0}, origin(r));
                rig.rockets[i].hull = origin(r);
            }
        }
        const TransformComponent rt = world.getComponent<TransformComponent>(e);  
        rig.state.pos = toV3(rt.position);
        rig.state.yaw = yawOf(rt.rotation);
        if (g_terrain.valid())
            rig.state.grounded = rt.position.y <= g_terrain.height(rt.position.x, rt.position.z) + 0.3f;
        rig.state.rotorSpeed = rig.state.grounded ? rig.params.rotorIdle : rig.params.rotorFlight;

        VehicleSlot v;
        v.root = e;
        if (const auto* n = world.tryGetComponent<EntityNameComponent>(e))
            v.name = std::string(n->name.toView());
        v.weapon = "AT missile";
        v.player = rig.player;
        v.type = warzone::VehicleType::Heli;
        v.homePos = rt.position;
        v.homeYaw = yawOf(rt.rotation);
        v.health = {heli.hp, heli.hp};
        v.armor = {0.9f, 1.2f, 1.4f, 1.3f};  
        v.rotation = rt.rotation;
        meshesInSubtree(world, e, v.meshes);
        for (Entity m : v.meshes)
            if (!world.hasComponent<RenderTintComponent>(m))
                world.addComponent<RenderTintComponent>(m, RenderTintComponent{});
        rig.vehicle = registerVehicle(std::move(v));
        rig.engine = makeVoice(world, "wz_engine.wav", "Rotor Voice", edges, e, true);
        world.getComponent<TransformComponent>(rig.engine).position = {0.0f, 4.0f, 0.0f};
        g_helis.push_back(std::move(rig));
    }
    attachAll(world, edges);
    SINFO_LOG("Warzone: {} helicopters ready", g_helis.size());
}

 
 
void updateLock(HeliRig& h, f32 dt, const Hit& reticleHit)
{
    std::vector<warzone::LockCandidate> cands;
    const Vec3 eye = g_cam.pos;
    for (u32 i = 0; i < g_weapons.vehicles.size(); ++i)
    {
        const auto& v = g_weapons.vehicles[i];
        if (i == h.vehicle || v.wrecked || v.benched)
            continue;
        const Vec3 c = toVec(v.box.centre);
        const Vec3 d = c - eye;
        const f32 dist = length(d);
        if (dist > LOCK_RANGE || dot(d, g_cam.dir) < dist * std::cos(LOCK_CONE * 1.5f))
            continue;  
        const Hit los = traceSegment(eye + g_cam.dir * 2.0f, c, h.vehicle);
        if (los.kind == Hit::Vehicle && los.index == i)
            cands.push_back({static_cast<int64_t>(i), toV3(c), true});
        else if (los.kind == Hit::None)
            cands.push_back({static_cast<int64_t>(i), toV3(c), true});
    }
    if (reticleHit.kind == Hit::Piece && g_destruction.ready)
    {
        const u32 b = g_destruction.structure.node(reticleHit.index).building;
        cands.push_back({warzone::BUILDING_LOCK + b, toV3(reticleHit.pos), false});
    }
    const int pick = warzone::pickLockCandidate(toV3(eye), toV3(g_cam.dir), cands, LOCK_CONE, LOCK_RANGE);
    const int64_t id = pick >= 0 ? cands[pick].id : -1;
    warzone::stepLock(h.lock, h.lockParams, id, dt);
    if (h.lock.id < 0)
    {
        h.lockName.clear();
        return;
    }
    if (h.lock.id < warzone::BUILDING_LOCK)
    {
        const auto& v = g_weapons.vehicles[static_cast<size_t>(h.lock.id)];
        if (v.wrecked)
        {
            h.lock = {};
            h.lockName.clear();
            return;
        }
        h.lockPoint = toVec(v.box.centre);
        h.lockName = v.name;
    }
    else
    {
        if (pick >= 0 && cands[pick].id == h.lock.id)
            h.lockPoint = toVec(cands[pick].pos);  
        const u32 b = static_cast<u32>(h.lock.id - warzone::BUILDING_LOCK);
        h.lockName = b < g_town.buildings.size() ? g_town.buildings[b].name : std::string("building");
    }
}

 
void heliSystem(SystemContext& ctx)
{
    if (g_helis.empty() || !g_weapons.ready)
        return;
    const f32 dt = std::min(ctx.deltaTime, 0.05f);
    for (auto& h : g_helis)
    {
        auto* rt = ctx.tryGetComponent<TransformComponent>(h.root);
        if (!rt || h.vehicle >= g_weapons.vehicles.size())
            continue;
        auto& v = g_weapons.vehicles[h.vehicle];
        warzone::pt::PlayerEffects playerFx(g_pt.sys, h.player);
        if (v.respawnPending)
        {
             
            v.respawnPending = false;
            reviveVehicle(ctx, h.vehicle);
            h.state = warzone::HeliState{};
            h.state.pos = toV3(v.respawnPos);
            h.state.yaw = v.respawnYaw;
            h.state.grounded = v.respawnPos.y <= g_terrain.height(v.respawnPos.x, v.respawnPos.z) + 0.5f;
            h.state.rotorSpeed = h.params.rotorFlight;
            h.crashed = false;
            h.lock = {};
            for (auto& r : h.rockets)
                r.loaded = true;
            h.missiles = HELI_MISSILES;
            if (h.player && g_active == Active::Heli)
            {
                std::scoped_lock lock(g_tankMutex);
                g_tankCtl.camYaw = v.respawnYaw;
                g_tankCtl.camPitch = -0.12f;
            }
        }
        if (v.benched)
        {
            rt->position = {rt->position.x, PARK_Y, rt->position.z};
            v.box.centre = toV3(rt->position);
            if (auto* a = ctx.tryGetComponent<AudioEmitterComponent>(h.engine))
                a->volume = 0.0f;
            continue;
        }
        const bool active = h.player && g_active == Active::Heli;
        warzone::HeliInput in;
        bool fire = false, fireUnlocked = false, scriptedFire = false;
        if (active && !v.wrecked)
        {
            std::scoped_lock lock(g_tankMutex);
            auto& c = g_tankCtl;
            auto& sc = g_heliScript;
            if (g_snapCamera)
            {
                g_snapCamera = false;
                c.camYaw = h.state.yaw;
                c.camPitch = -0.12f;
            }
            if (sc.teleport)
            {
                sc.teleport = false;
                h.state.pos = toV3(sc.teleportTo);
                h.state.vel = {};
                h.state.yaw = sc.teleportYaw;
                h.state.yawRate = 0.0f;
                h.state.grounded = false;
                h.state.rotorSpeed = h.params.rotorFlight;
                c.camYaw = sc.teleportYaw;
            }
            if (sc.resetLock)
            {
                sc.resetLock = false;
                h.lock = {};
            }
            const PlayerInput pin = pollPlayerInput(ctx, dt);
            in.forward = pin.throttle;
            in.strafe = pin.steer;
            in.climb = pin.climb;
            fire = pin.fire;
            if (pin.keys || pin.fire)
            {
                sc.flying = false;
                sc.hover = false;
                sc.seconds = 0.0f;
            }
            c.scripted = sc.flying || sc.seconds > 0.0f || sc.hover;
            c.camPitch = std::clamp(c.camPitch, -1.2f, 0.6f);
            const Vec3 pos = toVec(h.state.pos);
            if (sc.seconds > 0.0f)
            {
                sc.seconds -= dt;
                in.forward = sc.forward;
                in.strafe = sc.strafe;
                in.climb = sc.climb;
            }
            else if (sc.flying && !sc.path.empty())
            {
                const Vec3 to = sc.path.front() - pos;
                const f32 flat = std::sqrt(to.x * to.x + to.z * to.z);
                if (flat < 8.0f && std::abs(to.y) < 4.0f)
                {
                    sc.path.erase(sc.path.begin());
                    if (sc.path.empty())
                        sc.hover = true;
                }
                else
                {
                     
                    const f32 want = std::min(h.params.maxForward * sc.speedLimit, sc.path.size() == 1 ? flat * 0.45f : 1e9f);
                    const Vec3 wv = flat > 0.1f ? Vec3{to.x / flat, 0.0f, to.z / flat} * want : Vec3{};
                    const Vec3 f = toVec(warzone::heliForward(h.state.yaw)), l = toVec(warzone::heliLeft(h.state.yaw));
                    in.forward = std::clamp(dot(wv, f) / h.params.maxForward, -1.0f, 1.0f);
                    in.strafe = std::clamp(dot(wv, l) / h.params.maxSide, -1.0f, 1.0f);
                    in.climb = std::clamp(to.y * 0.18f, -1.0f, 1.0f);
                    if (!c.hasAim && flat > 25.0f)
                    {
                        in.hasYawTarget = true;
                        in.yawTarget = std::atan2(to.x, to.z);
                    }
                }
            }
            if (!in.hasYawTarget && !(sc.flying && !c.hasAim))
            {
                 
                in.hasYawTarget = true;
                in.yawTarget = c.hasAim ? std::atan2(c.aim.x - pos.x, c.aim.z - pos.z) : c.camYaw;
            }
            in.forward = std::clamp(in.forward, -1.0f, 1.0f);
            c.lastThrottle = in.forward;
            c.lastSteer = in.strafe;
            if (c.scriptZoom)
                c.zoom = true;
            if (sc.fire > 0)
            {
                fire = true;
                scriptedFire = true;
                fireUnlocked = sc.fireUnlocked;
            }
        }
         
        const warzone::V3 before = h.state.pos;
        const bool wasGrounded = h.state.grounded;
        const f32 vyBefore = h.state.vel.y;
        const f32 ground = g_terrain.valid() ? g_terrain.height(h.state.pos.x, h.state.pos.z) : 0.0f;
        if (!v.wrecked)
            warzone::stepHeli(h.state, h.params, in, ground, dt);
        else if (!h.crashed)
        {
             
            h.state.vel.y -= warzone::GRAVITY * dt;
            h.state.vel.x *= 1.0f - 0.3f * dt;
            h.state.vel.z *= 1.0f - 0.3f * dt;
            h.state.yawRate = warzone::approach(h.state.yawRate, 2.6f, 2.0f * dt);
            h.state.yaw = warzone::wrapPi(h.state.yaw + h.state.yawRate * dt);
            h.state.roll = warzone::approach(h.state.roll, 0.45f, 0.5f * dt);
            h.state.pitch = warzone::approach(h.state.pitch, 0.25f, 0.4f * dt);
            h.state.pos = h.state.pos + h.state.vel * dt;
            h.state.rotorSpeed = warzone::approach(h.state.rotorSpeed, 6.0f, 4.0f * dt);
            h.state.rotor = warzone::wrapPi(h.state.rotor + h.state.rotorSpeed * dt);
            if (h.state.pos.y <= ground)
            {
                h.state.pos.y = ground;
                h.state.vel = {};
                h.state.yawRate = 0.0f;
                h.crashed = true;
                warzone::ProjectileSpec crash = warzone::specOf(warzone::ProjectileKind::Shell);
                crash.blastRadius = 8.0f;
                crash.blastDamage = 700.0f;
                explode(ctx, toVec(h.state.pos) + Vec3{0, 1.0f, 0}, crash, h.vehicle, static_cast<int>(h.vehicle));
            }
        }
         
        if (!v.wrecked && g_destruction.ready)
        {
            const Quat yq = fromAxisAngle({0, 1, 0}, h.state.yaw);
            const Vec3 p = toVec(h.state.pos);
            std::array<Vec3, 7> probes = {Vec3{0, 2.0f, 0}, Vec3{0, 2.0f, 4.5f}, Vec3{0, 2.6f, -8.5f}, Vec3{7.2f, 4.4f, 0}, Vec3{-7.2f, 4.4f, 0},
                                          Vec3{0, 4.4f, 7.2f}, Vec3{0, 4.4f, -7.2f}};
            std::vector<u32> touching;
            Vec3 push{};
            for (size_t i = 0; i < probes.size(); ++i)
            {
                const Vec3 at = p + rotate(yq, probes[i]);
                g_destruction.structure.overlapSphere(toV3(at), i < 3 ? 1.9f : 0.9f, touching);
                for (u32 piece : touching)
                {
                    const warzone::Box& b = g_destruction.structure.node(piece).box;
                    const Vec3 c = toVec(b.centre());
                    Vec3 away = at - c;
                    if (at.y > b.hi.y - 0.5f)
                        away = {0, 1, 0};  
                    push = push + normalize(away);
                }
                if (!touching.empty())
                    break;
            }
            if (length(push) > 0.0f)
            {
                const Vec3 n = normalize(push);
                Vec3 vel = toVec(h.state.vel);
                const f32 into = dot(vel, n);
                const f32 speed = length(vel);
                h.state.pos = before + toV3(n * 0.15f);
                if (into < 0.0f)
                    vel = vel - n * (into * 1.4f);  
                h.state.vel = toV3(vel * 0.6f);
                ++h.bumps;
                if (speed > 6.0f)
                {
                    const f32 dmg = (speed - 6.0f) * 9.0f;
                    h.bumpDamage += dmg;
                    damageVehicle(ctx, h.vehicle, dmg, p - n * 5.0f, h.vehicle);
                    warzone::Blast b;
                    b.centre = toV3(p - n * 2.0f + Vec3{0, 2.0f, 0});
                    b.radius = 2.0f;
                    b.damage = speed * 12.0f;
                    b.push = speed * 0.3f;
                    scheduleBlast(b, 0, false);
                    spawnFx(ctx, FX_SPARK, p - n * 2.0f + Vec3{0, 3.0f, 0}, 2.0f, 3.0f, 0.4f, n * 2.0f);
                    if (active)
                        g_shake = std::min(1.0f, g_shake + 0.3f);
                }
            }
        }
         
        if (!v.wrecked && h.state.grounded && !wasGrounded && vyBefore < -7.0f)
            damageVehicle(ctx, h.vehicle, (-vyBefore - 7.0f) * 45.0f, toVec(h.state.pos) - Vec3{0, 5, 0}, h.vehicle);

         
        rt->position = toVec(h.state.pos);
        rt->rotation = fromAxisAngle({0, 1, 0}, h.state.yaw);
        const Quat tilt = normalize(multiply(fromAxisAngle({1, 0, 0}, h.state.pitch), fromAxisAngle({0, 0, 1}, -h.state.roll)));
        if (auto* m = ctx.tryGetComponent<TransformComponent>(h.model))
        {
            m->rotation = normalize(multiply(tilt, h.modelRestRot));
            m->position = HELI_TILT_PIVOT + rotate(tilt, h.modelRestPos - HELI_TILT_PIVOT);
        }
         
        const f32 kc = 1.0f - std::exp(-6.0f * dt);
        h.cyclicF += (std::clamp(in.forward, -1.0f, 1.0f) - h.cyclicF) * kc;
        h.cyclicS += (std::clamp(in.strafe, -1.0f, 1.0f) - h.cyclicS) * kc;
        if (auto* p = h.rotor.e.isValid() ? ctx.tryGetComponent<TransformComponent>(h.rotor.e) : nullptr)
            h.rotor.pose2(*p, h.state.rotor,
                          normalize(multiply(fromAxisAngle({1, 0, 0}, 0.075f * h.cyclicF), fromAxisAngle({0, 0, 1}, -0.075f * h.cyclicS))));
        if (auto* p = h.tail.e.isValid() ? ctx.tryGetComponent<TransformComponent>(h.tail.e) : nullptr)
            h.tail.pose(*p, h.state.tail);

         
        const Quat bodyQ = normalize(multiply(rt->rotation, tilt));
        auto hullToWorld = [&](const Vec3& hp) { return rt->position + rotate(rt->rotation, HELI_TILT_PIVOT + rotate(tilt, hp - HELI_TILT_PIVOT)); };
        (void)bodyQ;
        h.launchCooldown = std::max(0.0f, h.launchCooldown - dt);
        if (active && !v.wrecked && g_cam.valid)
        {
            Hit reticle;
            {
                const Vec3 from = g_cam.pos + g_cam.dir * 2.0f, to = g_cam.pos + g_cam.dir * 2500.0f;
                reticle = traceSegment(from, to, h.vehicle);
                h.aim = reticle.kind != Hit::None ? reticle.pos : to;
                h.range = length(h.aim - toVec(h.state.pos));
            }
            {
                std::scoped_lock lock(g_tankMutex);
                if (g_tankCtl.hasAim)
                {
                     
                    h.range = length(g_tankCtl.aim - toVec(h.state.pos));
                }
            }
            updateLock(h, dt, reticle);
            const bool locked = h.lock.locked;
            if (fire && h.missiles > 0 && h.launchCooldown <= 0.0f && (locked || fireUnlocked || !scriptedFire))
            {
                 
                u32 slot = h.nextRocket;
                for (u32 k = 0; k < HELI_MISSILES && !h.rockets[slot].loaded; ++k)
                    slot = (slot + 1) % HELI_MISSILES;
                if (h.rockets[slot].loaded)
                {
                    auto& r = h.rockets[slot];
                    const Vec3 from = hullToWorld(r.hull + Vec3{0, 0, 1.2f});
                    const Vec3 target = locked ? h.lockPoint : h.aim;
                    const Vec3 dir = normalize(normalize(target - from) * 0.6f + toVec(warzone::heliForward(h.state.yaw)) * 0.4f);
                    const u32 targetVehicle = locked && h.lock.id < warzone::BUILDING_LOCK ? static_cast<u32>(h.lock.id) : ~0u;
                    if (launch(warzone::ProjectileKind::Missile, from, dir * warzone::specOf(warzone::ProjectileKind::Missile).speed + toVec(h.state.vel),
                               h.vehicle, targetVehicle, target))
                    {
                        r.loaded = false;
                        --h.missiles;
                        ++h.launched;
                        if (locked)
                            ++h.lockedLaunches;
                        h.nextRocket = (slot + 1) % HELI_MISSILES;
                        h.launchCooldown = 0.45f;
                        h.rearm = 0.0f;
                        spawnFx(ctx, FX_FLASH, from + dir * 0.6f, 1.2f, 2.0f, 0.12f, dir * 3.0f);
                        spawnFx(ctx, FX_PUFF, from - dir * 1.0f, 1.0f, 4.0f, 2.2f, -dir * 3.0f + Vec3{0, 0.6f, 0});
                        flash(ctx, from, 2500.0f, 0.15f);
                        playVoice(g_weapons.cannonVoices, g_weapons.nextCannon, from, 80.0f, 0.55f);
                        std::scoped_lock lock(g_tankMutex);
                        if (g_heliScript.fire > 0)
                            --g_heliScript.fire;
                    }
                }
            }
        }
        else if (!active)
            h.lock = {};
         
        h.rearm += dt;
        if (h.missiles < HELI_MISSILES && h.rearm > 7.0f && !v.wrecked)
        {
            for (auto& r : h.rockets)
                if (!r.loaded)
                {
                    r.loaded = true;
                    ++h.missiles;
                    break;
                }
            h.rearm = 3.0f;
        }
        for (auto& r : h.rockets)
            if (auto* p = r.part.e.isValid() ? ctx.tryGetComponent<TransformComponent>(r.part.e) : nullptr)
                p->scale = r.loaded ? r.part.restScale : Vec3{0.001f, 0.001f, 0.001f};

         
        h.downwash -= dt;
        const f32 agl = h.state.pos.y - ground;
        const warzone::vfx::Wash wash = warzone::vfx::rotorWash(agl, h.state.rotorSpeed / h.params.rotorFlight, tierAt(rt->position, 8.0f));
        if (particlesOn())
        {
             
            const f32 rotor = std::clamp((h.state.rotorSpeed - h.params.rotorIdle) / std::max(h.params.rotorFlight - h.params.rotorIdle, 1.0f), 0.0f, 1.0f);
            const f32 strength = v.wrecked ? 0.0f : rotor * std::clamp(1.0f - agl / 30.0f, 0.0f, 1.0f);
            warzone::pt::rotorWash(g_pt.sys, warzone::V3{h.state.pos.x, ground, h.state.pos.z}, 7.5f, strength, tierAt(rt->position, 8.0f), dt,
                                   h.ptWash, PT_DUST);
        }
        else if (!v.wrecked && h.state.rotorSpeed > h.params.rotorIdle + 4.0f && wash.active && h.downwash <= 0.0f)
        {
            h.downwash = wash.interval;
            auto& rng = g_weapons.rng;
            for (int i = 0; i < wash.cards; ++i)
            {
                const f32 a = rng.range(0.0f, 6.283f);
                const Vec3 side{std::cos(a), 0.0f, std::sin(a)};
                const Vec3 at = Vec3{h.state.pos.x, ground + 0.5f, h.state.pos.z} + side * rng.range(wash.radiusMin, wash.radiusMax);
                spawnFx(ctx, FX_WASH, at, wash.s0, wash.s1, wash.life, side * wash.speed + Vec3{0, 0.3f, 0});
            }
        }

         
        v.rotation = rt->rotation;
        v.velocity = toVec(h.state.vel);
        v.box.centre = toV3(rt->position + rotate(rt->rotation, HELI_BOX_CENTRE));
        v.box.axis[0] = toV3(rotate(rt->rotation, {1, 0, 0}));
        v.box.axis[1] = toV3(rotate(rt->rotation, {0, 1, 0}));
        v.box.axis[2] = toV3(rotate(rt->rotation, {0, 0, 1}));
        v.box.half = toV3(HELI_BOX_HALF);

         
        if (auto* a = ctx.tryGetComponent<AudioEmitterComponent>(h.engine))
        {
            const f32 work = std::clamp(h.state.rotorSpeed / h.params.rotorFlight, 0.0f, 1.2f);
            a->volume = h.crashed ? 0.0f : mixAudio(wa::Category::Vehicles, length(rt->position - g_listener), 30.0f, 0.25f + 0.6f * work);
            if (!h.engineStarted && ctx.services().contains<AudioWorld>())
            {
                h.engineStarted = true;
                ctx.services().write<AudioWorld>()->play(h.engine);
            }
        }
    }
}

 
bool heliCamera(SystemContext& ctx, TransformComponent& cam, CameraComponent* lens, f32 dt)
{
    HeliRig* h = playerHeli();
    if (!h)
        return false;
    const auto* rt = ctx.tryGetConstComponent<TransformComponent>(h->root);
    if (!rt)
        return false;
    TankControl c;
    {
        std::scoped_lock lock(g_tankMutex);
        if (g_tankCtl.hasAim && g_tankCtl.cameraFollowsAim)
        {
            const Vec3 base = rt->position + Vec3{0, 3.5f, 0};
            const Vec3 to = g_tankCtl.aim - base;
            const f32 wantYaw = std::atan2(to.x, to.z);
            const f32 wantPitch = std::clamp(std::atan2(to.y, std::sqrt(to.x * to.x + to.z * to.z)), -1.2f, 0.6f);
            const f32 k = 1.0f - std::exp(-3.0f * dt);
            g_tankCtl.camYaw = warzone::wrapPi(g_tankCtl.camYaw + warzone::wrapPi(wantYaw - g_tankCtl.camYaw) * k);
            g_tankCtl.camPitch += (wantPitch - g_tankCtl.camPitch) * k;
        }
        else if (g_heliScript.flying)
        {
            const f32 k = 1.0f - std::exp(-1.5f * dt);
            g_tankCtl.camYaw = warzone::wrapPi(g_tankCtl.camYaw + warzone::wrapPi(h->state.yaw - g_tankCtl.camYaw) * k);
            g_tankCtl.camPitch += (-0.16f - g_tankCtl.camPitch) * k;
        }
        c = g_tankCtl;
    }
    const Vec3 dir{std::cos(c.camPitch) * std::sin(c.camYaw), std::sin(c.camPitch), std::cos(c.camPitch) * std::cos(c.camYaw)};
    const Vec3 base = rt->position + Vec3{0, 3.6f, 0};
    f32 dist = c.zoom ? 7.0f : 20.0f;
    if (g_destruction.ready)
    {
        u32 piece;
        f32 th;
        const Vec3 back = base - dir * dist + Vec3{0, 4.0f, 0};
        if (g_destruction.structure.raycast(toV3(base), toV3(back), piece, th))
            dist = std::max(4.0f, dist * th - 0.8f);
    }
    Vec3 eye = base - dir * dist + Vec3{0, 4.0f, 0};
    eye = keepAboveGround(eye, 1.5f);
    const f32 k = 1.0f - std::exp(-10.0f * dt);
    if (c.zoom)
    {
         
        cam.position = rt->position + rotate(rt->rotation, Vec3{0.0f, 1.3f, 7.4f}) + dir * 0.5f;
        g_heliSightWas = true;
    }
    else
    {
        cam.position = length(cam.position - eye) > 80.0f || g_heliSightWas ? eye : cam.position + (eye - cam.position) * k;
        g_heliSightWas = false;
    }
    cam.rotation = lookRotation(dir);
    if (lens)
        lens->fov = toRadians(c.zoom ? 22.0f : 62.0f);
    return true;
}

bool planeCamera(SystemContext& ctx, TransformComponent& cam, CameraComponent* lens, f32 dt);  

bool vehicleCamera(SystemContext& ctx, u32 mode, TransformComponent& cam, CameraComponent* lens, f32 dt)
{
    if (mode == 8)
        return heliCamera(ctx, cam, lens, dt);
    if (mode == 9)
        return planeCamera(ctx, cam, lens, dt);
    return groundCamera(ctx, mode == 7 ? playerAmp() : playerTank(), cam, lens, dt);
}

 
void drawHeliHud(SystemContext& ctx, UiCanvasPainter& p, f32 w, f32 h, const TankControl& c)
{
    HeliRig* heli = playerHeli();
    const auto* rt = ctx.tryGetConstComponent<TransformComponent>(heli->root);
    if (!rt)
        return;
    const HudColors col;
    const Vec2 mid{w * 0.5f, h * 0.5f};
    const auto& s = heli->state;
     
    const f32 conePx = std::tan(LOCK_CONE) / std::tan(g_cam.fov * 0.5f) * h * 0.5f;
    ring(p, mid, conePx, 1.5f, col.dim, 0.0f, 6.2832f, 32);
    p.line({mid.x - 14, mid.y}, {mid.x - 5, mid.y}, 2.0f, col.white);
    p.line({mid.x + 5, mid.y}, {mid.x + 14, mid.y}, 2.0f, col.white);
    p.line({mid.x, mid.y + 5}, {mid.x, mid.y + 14}, 2.0f, col.white);
    p.text({mid.x - 60, mid.y + conePx + 6}, {120, 18}, std::format("{:.0f} m", heli->range), col.white, UiCanvasAlign::Center, 14.0f);
     
    Vec2 tp;
    if (heli->lock.id >= 0 && project(heli->lockPoint, w, h, tp))
    {
        const bool locked = heli->lock.locked;
        const u32 lc = locked ? col.red : col.amber;
        const f32 dist = std::max(1.0f, length(heli->lockPoint - g_cam.pos));
        const f32 size = std::clamp(7.0f / dist / std::tan(g_cam.fov * 0.5f) * h * 0.5f, 24.0f, 140.0f);
        const f32 half = size * (locked ? 1.0f : 1.0f + 1.6f * (1.0f - heli->lock.progress));
        const f32 arm = std::max(6.0f, half * 0.35f);
        for (int sx : {-1, 1})
            for (int sy : {-1, 1})
            {
                const Vec2 corner{tp.x + sx * half, tp.y + sy * half};
                p.line(corner, {corner.x - sx * arm, corner.y}, 2.5f, lc);
                p.line(corner, {corner.x, corner.y - sy * arm}, 2.5f, lc);
            }
        if (!locked)
            ring(p, tp, half + 8.0f, 3.0f, lc, 0.0f, 6.2832f * heli->lock.progress, 32);
        p.text({tp.x - 150, tp.y - half - 26}, {300, 18},
               locked ? std::format("LOCKED  {}  {:.0f} m", heli->lockName, dist) : std::format("LOCKING {:.0f}%  {}", heli->lock.progress * 100.0f, heli->lockName),
               lc, UiCanvasAlign::Center, 14.0f);
    }
     
    const f32 ground = g_terrain.valid() ? g_terrain.height(s.pos.x, s.pos.z) : 0.0f;
    const Vec3 vel = toVec(s.vel);
    const f32 kmh = std::sqrt(vel.x * vel.x + vel.z * vel.z) * 3.6f;
    p.rect({mid.x - 250, mid.y - 34}, {104, 68}, col.panel, 4.0f);
    p.text({mid.x - 246, mid.y - 30}, {96, 16}, "SPEED", col.dim, UiCanvasAlign::Left, 12.0f);
    p.text({mid.x - 246, mid.y - 12}, {96, 26}, std::format("{:.0f}", kmh), col.white, UiCanvasAlign::Left, 22.0f);
    p.text({mid.x - 246, mid.y + 14}, {96, 16}, "km/h", col.dim, UiCanvasAlign::Left, 12.0f);
    p.rect({mid.x + 146, mid.y - 34}, {104, 68}, col.panel, 4.0f);
    p.text({mid.x + 150, mid.y - 30}, {96, 16}, "ALT", col.dim, UiCanvasAlign::Left, 12.0f);
    p.text({mid.x + 150, mid.y - 12}, {96, 26}, std::format("{:.0f} m", s.pos.y - ground), col.white, UiCanvasAlign::Left, 22.0f);
    p.text({mid.x + 150, mid.y + 14}, {96, 16}, std::format("VS {:+.1f}  ASL {:.0f}", vel.y, s.pos.y), col.dim, UiCanvasAlign::Left, 11.0f);
     
    const Vec2 o = drawCommonHud(p, w, h, heli->vehicle, rt->position, s.yaw, c.camYaw, "HIND  |  AT MISSILES");
     
    const Vec2 ic{o.x + 62, o.y + 76};
    ring(p, ic, 44.0f, 2.0f, col.dim, 0.0f, 6.2832f, 32);
    const f32 off = std::clamp(s.pitch * 90.0f, -36.0f, 36.0f);
    const Vec2 dirH{std::cos(s.roll), std::sin(s.roll)};
    const Vec2 cH{ic.x, ic.y - off};
    p.line({cH.x - dirH.x * 38, cH.y - dirH.y * 38}, {cH.x + dirH.x * 38, cH.y + dirH.y * 38}, 2.0f, col.green);
    p.line({ic.x - 14, ic.y}, {ic.x - 5, ic.y}, 2.5f, col.amber);
    p.line({ic.x + 5, ic.y}, {ic.x + 14, ic.y}, 2.5f, col.amber);
    p.text({o.x + 130, o.y + 50}, {160, 22}, std::format("{:.0f} km/h", kmh), col.white, UiCanvasAlign::Left, 20.0f);
    p.text({o.x + 130, o.y + 80}, {160, 18}, std::format("AT MISSILE  x{}/{}", heli->missiles, HELI_MISSILES), col.white, UiCanvasAlign::Left, 14.0f);
    for (u32 i = 0; i < HELI_MISSILES; ++i)
        p.rect({o.x + 130 + i * 18.0f, o.y + 102}, {12, 6}, heli->rockets[i].loaded ? col.green : rgba(60, 60, 56, 0.8f), 1.5f);
    const bool hover = kmh < 7.0f && std::abs(vel.y) < 0.5f && !s.grounded;
    if (c.scripted)
        p.text({o.x + 130, o.y + 120}, {160, 18}, "AUTOPILOT", col.amber, UiCanvasAlign::Left, 12.0f);
    else
        p.text({o.x + 130, o.y + 120}, {160, 18}, s.grounded ? "LANDED" : hover ? "HOVER HOLD" : "PILOT", hover ? col.green : col.dim, UiCanvasAlign::Left, 12.0f);
}

 
void registerHeliSystems(World world)
{
    SystemDescriptor sys;
    sys.moduleName = MODULE;
    sys.name = "WarzoneHeli";
    sys.fn = heliSystem;
    sys.update = SystemPhaseDescriptor{.reads = {getComponentId<WarzoneHeli>()},
                                       .writes = {getComponentId<TransformComponent>(), getComponentId<AudioEmitterComponent>(),
                                                  getComponentId<FlipbookComponent>(), getComponentId<PointLightComponent>(),
                                                  getComponentId<RenderTintComponent>()}};
    world.registerSystem(sys);
}

bool vec3ListArg(const ControlArgs& args, std::string_view key, std::vector<Vec3>& out)
{
    const auto text = args.json(key);
    if (!text)
        return false;
    out.clear();
    const char* s = text->c_str();
    f32 x, y, z;
    int n;
    while (*s)
    {
        if (std::sscanf(s, "[%f,%f,%f]%n", &x, &y, &z, &n) == 3)
        {
            out.push_back({x, y, z});
            s += n;
        }
        else
            ++s;
    }
    return !out.empty();
}

void registerHeliCommands(const ModuleContext& context)
{
    auto& control = context.control;
    const std::string_view owner = MODULE;
    control.registerCommand(
        "warzoneHeli", owner,
        "flies the player helicopter by script (filming, tests) and makes it the active vehicle: teleport [x,y,z] heading (deg, compass) "
        "puts it in the air; goto [x,y,z] / path [[x,y,z],...] at speed 0-1 then hovers; hover; climb/forward/strafe for seconds; aim "
        "[x,y,z] or aimAt <vehicle or building> points the camera (and so the reticle and the lock); fire n missiles, each once the lock "
        "is complete (unlocked=true fires at once); release gives control back; camera=true switches the game camera to the heli",
        {optionalArg("teleport", ArgKind::Json, "[x,y,z] place in the air"), optionalArg("heading", ArgKind::Number, "compass deg for teleport", "0"),
         optionalArg("goto", ArgKind::Json, "[x,y,z] fly there and hover"), optionalArg("path", ArgKind::Json, "[[x,y,z],...] waypoints"),
         optionalArg("speed", ArgKind::Number, "fraction of top speed", "1"), optionalArg("hover", ArgKind::Boolean, "stop and hold position"),
         optionalArg("climb", ArgKind::Number, "-1..1 for `seconds`"), optionalArg("forward", ArgKind::Number, "-1..1 for `seconds`"),
         optionalArg("strafe", ArgKind::Number, "-1..1 (+ left) for `seconds`"), optionalArg("seconds", ArgKind::Number, "how long the stick holds"),
         optionalArg("aim", ArgKind::Json, "[x,y,z] look / aim point"), optionalArg("aimAt", ArgKind::String, "vehicle or building name"),
         optionalArg("fire", ArgKind::Integer, "missiles to fire"), optionalArg("unlocked", ArgKind::Boolean, "fire without waiting for the lock"),
         optionalArg("zoom", ArgKind::Boolean, "narrow view"), optionalArg("release", ArgKind::Boolean, "hand control back to the player"),
         optionalArg("camera", ArgKind::Boolean, "game camera follows the heli")},
        [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            if (!control.world("play") || !playerHeli())
                return "warzoneHeli needs Play running with a player helicopter (WarzoneHeli role 0)";
            std::scoped_lock lock(g_tankMutex);
            setActiveLocked(Active::Heli);
            auto& c = g_tankCtl;
            auto& sc = g_heliScript;
            if (args.boolean("release").value_or(false))
            {
                sc = HeliScript{};
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
                sc.flying = false;
                sc.path.clear();
            }
            std::vector<Vec3> path;
            if (vec3ListArg(args, "path", path) || (vec3Arg(args, "goto", p) && (path = {p}, true)))
            {
                sc.path = path;
                sc.flying = true;
                sc.hover = false;
                sc.seconds = 0.0f;
            }
            c.scripted = sc.flying || sc.seconds > 0.0f || sc.hover;
            if (auto v = number(args, "speed"))
                sc.speedLimit = std::clamp(*v, 0.05f, 1.0f);
            if (args.boolean("hover").value_or(false))
            {
                sc.flying = false;
                sc.path.clear();
                sc.hover = true;
                sc.seconds = 0.0f;
            }
            if (auto s = number(args, "seconds"))
            {
                sc.seconds = *s;
                sc.climb = number(args, "climb").value_or(0.0f);
                sc.forward = number(args, "forward").value_or(0.0f);
                sc.strafe = number(args, "strafe").value_or(0.0f);
                sc.flying = false;
            }
            if (vec3Arg(args, "aim", p))
            {
                c.aim = p;
                c.hasAim = true;
                sc.resetLock = true;
            }
            if (auto target = args.string("aimAt"))
            {
                bool ok;
                const Vec3 at = aimPointForName(*target, ok);
                if (!ok)
                    return std::format("no building or vehicle '{}'", *target);
                c.aim = at;
                c.hasAim = true;
                sc.resetLock = true;
            }
            if (auto n = args.integer("fire"))
                sc.fire = static_cast<u32>(std::max<long long>(0, static_cast<long long>(*n)));
            if (auto u = args.boolean("unlocked"))
                sc.fireUnlocked = *u;
            if (auto z = args.boolean("zoom"))
                c.scriptZoom = *z;
            if (args.boolean("camera").value_or(false))
            {
                g_cameraCommand.pending = true;
                g_cameraCommand.mode = 8;
                g_cameraCommand.spot = -1;
                g_cameraCommand.target.clear();
            }
            result.writeBool("flying", sc.flying);
            result.writeU32("waypoints", static_cast<u32>(sc.path.size()));
            result.writeBool("hasAim", c.hasAim);
            result.writeU32("fireQueue", sc.fire);
            return {};
        });

    control.registerQuery(
        "warzoneHeliState", owner,
        "the player helicopter in Play: position, heading, speed, altitude above ground, vertical speed, attitude, rotor, missiles, "
        "lock (target, progress, locked), aim, collisions, health, autopilot, input focus",
        {}, [&control](const ControlArgs&, IFormatWriter& result) -> std::string
        {
            const World* play = control.world("play");
            HeliRig* h = playerHeli();
            if (!play || !h)
                return "no player helicopter (start Play)";
            const auto& v = g_weapons.vehicles[h->vehicle];
            const auto& s = h->state;
            const f32 p[3] = {s.pos.x, s.pos.y, s.pos.z};
            const f32 ground = g_terrain.valid() ? g_terrain.height(s.pos.x, s.pos.z) : 0.0f;
            result.writeString("name", v.name);
            result.writeBool("active", g_active == Active::Heli);
            result.writeFloats("position", p);
            result.writeF32("headingDeg", std::fmod(360.0f + 180.0f - s.yaw * 57.2958f, 360.0f));
            result.writeF32("kmh", std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z) * 3.6f);
            result.writeF32("forwardSpeed", dot(s.vel, warzone::heliForward(s.yaw)));
            result.writeF32("verticalSpeed", s.vel.y);
            result.writeF32("altitude", s.pos.y - ground);
            result.writeBool("grounded", s.grounded);
            result.writeF32("pitchDeg", s.pitch * 57.2958f);
            result.writeF32("rollDeg", s.roll * 57.2958f);
            result.writeF32("yawRate", s.yawRate);
            result.writeF32("rotorSpeed", s.rotorSpeed);
            result.writeF32("rotorAngle", s.rotor);
            result.writeU32("missiles", h->missiles);
            result.writeU32("launched", h->launched);
            result.writeU32("lockedLaunches", h->lockedLaunches);
            result.writeBool("locked", h->lock.locked);
            result.writeF32("lockProgress", h->lock.progress);
            result.writeString("lockTarget", h->lockName);
            const f32 lp[3] = {h->lockPoint.x, h->lockPoint.y, h->lockPoint.z};
            result.writeFloats("lockPoint", lp);
            const f32 a[3] = {h->aim.x, h->aim.y, h->aim.z};
            result.writeFloats("aim", a);
            result.writeF32("range", h->range);
            result.writeU32("bumps", h->bumps);
            result.writeF32("bumpDamage", h->bumpDamage);
            result.writeF32("hp", v.health.hp);
            result.writeBool("wrecked", v.wrecked);
            result.writeBool("crashed", h->crashed);
            std::scoped_lock lock(g_tankMutex);
            result.writeBool("autopilot", g_heliScript.flying);
            result.writeU32("waypoints", static_cast<u32>(g_heliScript.path.size()));
            result.writeU32("fireQueue", g_heliScript.fire);
            result.writeF32("camYawDeg", g_tankCtl.camYaw * 57.2958f);
            result.writeF32("camPitchDeg", g_tankCtl.camPitch * 57.2958f);
            result.writeBool("gameFocus", g_tankCtl.gameFocus);
            result.writeF32("stickForward", g_tankCtl.lastThrottle);
            result.writeF32("stickStrafe", g_tankCtl.lastSteer);
            return {};
        });
}
