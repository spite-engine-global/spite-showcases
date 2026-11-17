 
 
 
 
 
 

 
 
struct GroundDef
{
    const char* label;       
    const char* modelChild;  
    const char* turretNode;
    const char* gunNode;
    const char* weapon;      
    Vec3 turretPivot, gunPivot, muzzle, tiltPivot, hullBoxCentre, hullBoxHalf;
    Vec3 colliderHalf, colliderOffset;
    f32 mass;
    f32 cornerX, cornerZ;  
    f32 restPitch;         
    f32 bow;               
    f32 ramDamage, ramPerSpeed;
    f32 camDist, camPivot, camUp, zoomDist, zoomFov;
    f32 engineRef;
    bool autocannon;
};

 
const GroundDef TANK_DEF{"T-80U", "T-80 Model", "Object_48", "Object_48.001", "125 mm HE",
                         {-0.02f, 1.92f, -0.13f}, {0.01f, 2.02f, 1.19f}, {-0.01f, 1.785f, 6.77f}, {0.0f, 1.0f, 0.0f},
                         {0.0f, 1.45f, 0.0f}, {1.95f, 1.45f, 3.95f}, {1.9f, 0.95f, 3.85f}, {0.0f, 1.5f, 0.0f}, 46000.0f,
                         1.6f, 3.3f, 0.0f, 4.3f, 320.0f, 90.0f, 11.0f, 3.2f, 2.4f, 4.5f, 20.0f, 20.0f, false};
 
 
 
const GroundDef AMP_DEF{"AMP  |  30 mm", "AMP Model", "tower", "gun", "30 mm autocannon",
                        {0.18f, 2.28f, -0.25f}, {0.21f, 2.52f, 0.56f}, {0.20f, 2.74f, 3.51f}, {0.0f, 0.9f, 0.0f},
                        {0.0f, 1.85f, 0.0f}, {1.95f, 1.75f, 3.7f}, {1.85f, 0.95f, 3.55f}, {0.0f, 1.45f, 0.0f}, 24000.0f,
                        1.45f, 3.0f, 0.076f, 4.0f, 180.0f, 60.0f, 11.0f, 3.7f, 2.6f, 3.6f, 24.0f, 16.0f, true};

struct WheelDef
{
    const char* node;
    Vec3 centre;
};
const std::array<WheelDef, 6> ROAD_WHEELS = {{{"Object_18", {0.01f, 0.41f, 2.32f}},
                                              {"Object_20", {0.01f, 0.41f, 1.435f}},
                                              {"Object_22", {0.01f, 0.44f, 0.465f}},
                                              {"Object_24", {0.01f, 0.41f, -0.36f}},
                                              {"Object_26", {0.01f, 0.415f, -1.345f}},
                                              {"Object_28", {0.01f, 0.42f, -2.295f}}}};
const WheelDef SPROCKET_LEFT{"Object_10", {1.61f, 0.825f, 3.2f}};   
const WheelDef SPROCKET_RIGHT{"Object_12", {-1.6f, 0.825f, 3.2f}};
const WheelDef IDLER{"Object_16", {0.01f, 0.795f, -2.995f}};
constexpr u32 TANK_AMMO = 40;

 
struct Xf
{
    Quat q = Quat::identity();
    Vec3 t{};
    f32 s = 1.0f;
    Vec3 apply(const Vec3& p) const { return t + rotate(q, p * s); }
    Vec3 inverse(const Vec3& p) const { return rotate(conjugate(q), p - t) * (1.0f / s); }
};
Xf compose(const Xf& a, const Xf& b) { return {normalize(multiply(a.q, b.q)), a.apply(b.t), a.s * b.s}; }

 
Entity parentOf(const World& world, Entity e)
{
    return world.services().contains<EntityTransformManager>() ? world.services().read<EntityTransformManager>()->attachedParentOf(e)
                                                               : Entity::undefined();
}

void subtree(const World& world, Entity root, std::vector<Entity>& out)
{
    out.clear();
    if (world.services().contains<EntityTransformManager>())
        world.services().read<EntityTransformManager>()->collectSubtree(root, out);
}

Entity findInSubtree(const World& world, Entity root, std::string_view name)
{
    std::vector<Entity> all;
    subtree(world, root, all);
    for (Entity e : all)
        if (const auto* n = world.tryGetComponent<EntityNameComponent>(e); n && n->name.toView() == name)
            return e;
    return Entity::undefined();
}

void meshesInSubtree(const World& world, Entity root, std::vector<Entity>& out)
{
    std::vector<Entity> all;
    subtree(world, root, all);
    for (Entity e : all)
        if (world.hasComponent<MeshComponent>(e))
            out.push_back(e);
}

 
Quat inFrame(const Quat& f, const Quat& q) { return normalize(multiply(multiply(f, q), conjugate(f))); }

 
struct PartRig
{
    Entity e = Entity::undefined();
    Vec3 restPos{}, axis{1, 0, 0}, pivot{};
    Quat restRot = Quat::identity();
    Vec3 restScale{1, 1, 1};
    Xf frame;  

    bool setup(const World& world, Entity part, Entity root, const Vec3& axisHull, const Vec3& pivotHull)
    {
        if (!part.isValid())
            return false;
        e = part;
        const auto& t = world.getComponent<TransformComponent>(part);
        restPos = t.position;
        restRot = t.rotation;
        restScale = t.scale;
        std::vector<Entity> chain;
        for (Entity p = parentOf(world, part); p.isValid() && p != root; p = parentOf(world, p))
            chain.push_back(p);
        frame = {};
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const auto& pt = world.getComponent<TransformComponent>(*it);
            frame = compose(frame, Xf{pt.rotation, pt.position, pt.scale.x});
        }
        axis = normalize(rotate(conjugate(frame.q), axisHull));
        pivot = frame.inverse(pivotHull);
        return true;
    }
     
    void pose(TransformComponent& t, f32 angle, const Vec3& slideHull = {}) const
    {
        const Quat r = fromAxisAngle(axis, angle);
        t.rotation = normalize(multiply(r, restRot));
        t.position = pivot + rotate(r, restPos - pivot) + rotate(conjugate(frame.q), slideHull) * (1.0f / frame.s);
    }
     
    void pose2(TransformComponent& t, f32 angle, const Quat& tiltHull) const
    {
        const Quat r = normalize(multiply(inFrame(conjugate(frame.q), tiltHull), fromAxisAngle(axis, angle)));
        t.rotation = normalize(multiply(r, restRot));
        t.position = pivot + rotate(r, restPos - pivot);
    }
};

struct TankRig
{
    const GroundDef* def = &TANK_DEF;
    Entity root = Entity::undefined(), model = Entity::undefined(), engine = Entity::undefined(), reloadVoice = Entity::undefined();
    Vec3 modelRestPos{};
    Quat modelRestRot = Quat::identity();
    PartRig turret, gun, sprocketLeft, sprocketRight, idler;
    std::array<PartRig, 6> wheels;
    u32 vehicle = 0;
    bool player = false;
    warzone::TankParams params;
    warzone::TankState state;
    warzone::AutocannonParams gunParams;  
    warzone::AutocannonState gun30;
    f32 terrainPitch = 0.0f, terrainRoll = 0.0f, blocked = 0.0f, ram = 0.0f, stall = 0.0f, settled = 0.0f;
    bool engineStarted = false;
    bool wreckPosed = false;
    u32 ammo = TANK_AMMO;
    Vec3 aim{}, gunImpact{};
    bool gunImpactValid = false;
    f32 range = 0.0f, spread = 0.0f;
    bool onTarget = false;
    u32 rams = 0;
    RigidbodyComponent body;
    bool resetPending = false;
    bool bodyOff = false;
    bool isAmp() const { return def->autocannon; }
};

 
 
enum class Active : u8
{
    Tank,
    Amp,
    Heli,
    Plane,
    Giga
};
Active g_active = Active::Tank;
bool g_snapCamera = false;  

struct TankControl
{
    f32 camYaw = 3.14159f, camPitch = -0.06f;
    bool zoom = false;
    int mouseMode = 1;  
    f32 mouseSensitivity = 0.0022f;
     
    bool scripted = false;
    std::vector<Vec3> path;
    f32 speedLimit = 1.0f;
    bool hasAim = false;
    Vec3 aim{};
    u32 fireQueue = 0;
    f32 driveThrottle = 0.0f, driveSteer = 0.0f, driveSeconds = 0.0f;
    bool cameraFollowsAim = true;
    bool scriptZoom = false;  
     
    f32 lastMouseDx = 0.0f, lastMouseDy = 0.0f, lastThrottle = 0.0f, lastSteer = 0.0f;
    bool cursorCaptured = false, keysActive = false;
     
     
    bool gameFocus = false, captureRequested = false, released = false;
};

struct CameraPose
{
    Vec3 pos{}, dir{0, 0, -1};
    f32 fov = 1.0f;
    bool valid = false;
};

std::vector<TankRig> g_tanks;
TankControl g_tankCtl;
std::mutex g_tankMutex;  
CameraPose g_cam;

void setCameraPose(const Vec3& pos, const Vec3& dir, f32 fov)
{
    g_cam = {pos, dir, fov, true};
    g_listener = pos;
    g_listenerSet = true;
    g_pt.sys.viewer = toV3(pos);
}

 
void addShake(TransformComponent& cam, f32 dt)
{
    g_shakeTime += dt;
    if (g_shake <= 0.001f)
        return;
    const f32 a = g_shake * g_shake * 0.035f, t = g_shakeTime;
    const Vec3 axis = normalize(Vec3{std::sin(t * 37.0f) + 0.3f * std::sin(t * 91.0f), std::sin(t * 43.0f + 1.3f), 0.4f * std::sin(t * 29.0f + 2.1f)});
    cam.rotation = normalize(multiply(cam.rotation, fromAxisAngle(axis, a * std::sin(t * 61.0f))));
}

struct Hud
{
    std::unique_ptr<UiScope> scope;
    Canvas canvas;
    bool visible = true;
};
std::weak_ptr<Hud> g_hud;

TankRig* playerRig(bool amp)
{
    for (auto& t : g_tanks)
        if (t.player && t.isAmp() == amp)
            return &t;
    return nullptr;
}
TankRig* playerTank() { return playerRig(false); }
TankRig* playerAmp() { return playerRig(true); }

bool rigActive(const TankRig& t) { return t.player && (t.isAmp() ? g_active == Active::Amp : g_active == Active::Tank); }

 
 
void setActiveLocked(Active a)
{
    if (g_active == a)
        return;
    g_active = a;
    auto& c = g_tankCtl;
    c.scripted = false;
    c.path.clear();
    c.hasAim = false;
    c.fireQueue = 0;
    c.driveSeconds = 0.0f;
    c.scriptZoom = false;
    g_snapCamera = true;
}

f32 yawOf(const Quat& q)
{
    const Vec3 f = rotate(q, {0, 0, 1});
    return std::atan2(f.x, f.z);
}

Quat tiltOf(const TankRig& t)
{
    const f32 pitch = t.terrainPitch + t.state.pitch, roll = t.terrainRoll + t.state.roll;
    return normalize(multiply(fromAxisAngle({1, 0, 0}, -pitch), fromAxisAngle({0, 0, 1}, roll)));
}

Vec3 restBarrel(const GroundDef& d) { return rotate(fromAxisAngle({1, 0, 0}, -d.restPitch), {0, 0, 1}); }

f32 recoilDistance(const TankRig& t) { return t.isAmp() ? t.gunParams.recoilBack * t.gun30.recoil : t.params.recoilBack * t.state.recoil; }

 
void muzzleHull(const TankRig& t, Vec3& pos, Vec3& dir, const Quat* tiltOverride = nullptr)
{
    const GroundDef& d = *t.def;
    const Quat qt = fromAxisAngle({0, 1, 0}, t.state.turretYaw), qe = fromAxisAngle({1, 0, 0}, -t.state.elevation);
    const Quat tilt = tiltOverride ? *tiltOverride : tiltOf(t);
    const Vec3 barrel = restBarrel(d);
    Vec3 p = d.gunPivot + rotate(qe, d.muzzle - d.gunPivot) + rotate(qe, barrel * -recoilDistance(t));
    p = d.turretPivot + rotate(qt, p - d.turretPivot);
    pos = d.tiltPivot + rotate(tilt, p - d.tiltPivot);
    dir = rotate(tilt, rotate(qt, rotate(qe, barrel)));
}

 
void attachTanks(const ModuleContext& context, World world)
{
    (void)context;
    g_tanks.clear();
    {
        std::scoped_lock lock(g_tankMutex);
        g_tankCtl = TankControl{};
        g_active = Active::Tank;
        g_snapCamera = false;
    }
    struct Found
    {
        Entity e;
        u32 role;
        f32 hp;
        const GroundDef* def;
    };
    std::vector<Found> found;
    for (auto [e, tank] : world.createQueryBuilder().buildQuery<Read<WarzoneTank>>()->view<Entity, Read<WarzoneTank>>())
        found.push_back({e, tank.role, tank.hp, &TANK_DEF});
    for (auto [e, amp] : world.createQueryBuilder().buildQuery<Read<WarzoneAmp>>()->view<Entity, Read<WarzoneAmp>>())
        found.push_back({e, amp.role, amp.hp, &AMP_DEF});
    std::vector<std::pair<Entity, Entity>> edges;
    for (const auto& f : found)
    {
        const Entity e = f.e;
        const GroundDef& d = *f.def;
        TankRig rig;
        rig.def = f.def;
        rig.root = e;
        rig.player = f.role == 0;
        rig.model = findInSubtree(world, e, d.modelChild);
        if (!rig.model.isValid())
        {
            SWARNING_LOG("Warzone: vehicle {} has no '{}' child", e.id(), d.modelChild);
            continue;
        }
        const auto& mt = world.getComponent<TransformComponent>(rig.model);
        rig.modelRestPos = mt.position;
        rig.modelRestRot = mt.rotation;
        rig.turret.setup(world, findInSubtree(world, e, d.turretNode), e, {0, 1, 0}, d.turretPivot);
        rig.gun.setup(world, findInSubtree(world, e, d.gunNode), e, {1, 0, 0}, d.gunPivot);
        if (d.autocannon)
        {
            rig.params = warzone::ampDriveParams();
            rig.gun30.ammo = rig.gunParams.ammo;
            rig.ammo = rig.gun30.ammo;
        }
        else
        {
            for (size_t i = 0; i < ROAD_WHEELS.size(); ++i)
                rig.wheels[i].setup(world, findInSubtree(world, e, ROAD_WHEELS[i].node), e, {1, 0, 0}, ROAD_WHEELS[i].centre);
            rig.sprocketLeft.setup(world, findInSubtree(world, e, SPROCKET_LEFT.node), e, {1, 0, 0}, SPROCKET_LEFT.centre);
            rig.sprocketRight.setup(world, findInSubtree(world, e, SPROCKET_RIGHT.node), e, {1, 0, 0}, SPROCKET_RIGHT.centre);
            rig.idler.setup(world, findInSubtree(world, e, IDLER.node), e, {1, 0, 0}, IDLER.centre);
        }
        const TransformComponent rt = world.getComponent<TransformComponent>(e);  
        rig.state.turretYaw = 0.0f;
        if (rig.player && !d.autocannon)
            g_tankCtl.camYaw = yawOf(rt.rotation);

         
        RigidbodyComponent body;
        body.bodyType = RigidbodyComponent::BodyType::Dynamic;
        body.mass = d.mass;
        body.friction = 0.2f;
        body.restitution = 0.0f;
        body.linearDamping = 0.0f;
        body.angularDamping = 0.0f;
        body.useGravity = false;
        body.allowSleeping = false;
        body.lockAngularX = true;
        body.lockAngularZ = true;
        body.objectLayer = static_cast<u16>(ObjectLayer::Moving);
        world.addComponent<RigidbodyComponent>(e, body);
        rig.body = body;
        BoxColliderComponent box;
        box.halfExtents = d.colliderHalf;
        box.offset = d.colliderOffset;
        world.addComponent<BoxColliderComponent>(e, box);

        VehicleSlot v;
        v.root = e;
        if (const auto* n = world.tryGetComponent<EntityNameComponent>(e))
            v.name = std::string(n->name.toView());
        v.weapon = d.weapon;
        v.player = rig.player;
        v.type = d.autocannon ? warzone::VehicleType::Amp : warzone::VehicleType::Tank;
        v.homePos = rt.position;
        v.homeYaw = yawOf(rt.rotation);
        v.health = {f.hp, f.hp};
        v.rotation = rt.rotation;
        meshesInSubtree(world, e, v.meshes);
         
         
        for (Entity m : v.meshes)
            if (!world.hasComponent<RenderTintComponent>(m))
                world.addComponent<RenderTintComponent>(m, RenderTintComponent{});
        rig.vehicle = registerVehicle(std::move(v));
        rig.engine = makeVoice(world, "wz_engine.wav", "Engine Voice", edges, e, true);
        world.getComponent<TransformComponent>(rig.engine).position = {0.0f, 1.5f, -2.5f};
        if (rig.player && !d.autocannon)
        {
            rig.reloadVoice = makeVoice(world, "wz_reload.wav", "Reload Voice", edges, e);
            world.getComponent<TransformComponent>(rig.reloadVoice).position = d.turretPivot;
        }
        g_tanks.push_back(std::move(rig));
    }
    attachAll(world, edges);
    SINFO_LOG("Warzone: {} ground vehicles ready", g_tanks.size());
}

 
Vec3 aimForCamera(u32 ignore, f32& range)
{
    const Vec3 from = g_cam.pos + g_cam.dir * 2.0f, to = g_cam.pos + g_cam.dir * 2500.0f;
    const Hit hit = traceSegment(from, to, ignore);
    const Vec3 p = hit.kind != Hit::None ? hit.pos : to;
    range = length(p - g_cam.pos);
    return p;
}

 
bool predictImpact(const Vec3& muzzle, const Vec3& velocity, u32 ignore, Vec3& out, warzone::ProjectileKind kind = warzone::ProjectileKind::Shell)
{
    warzone::Projectile p;
    p.kind = kind;
    p.pos = toV3(muzzle);
    p.vel = toV3(velocity);
    for (int i = 0; i < 120; ++i)
    {
        const Vec3 a = toVec(p.pos);
        warzone::stepProjectile(p, 0.05f);
        const Hit hit = traceSegment(a, toVec(p.pos), ignore);
        if (hit.kind != Hit::None)
        {
            out = hit.pos;
            return true;
        }
    }
    return false;
}

Vec3 aimPointForName(std::string_view name, bool& ok)
{
    ok = true;
    if (const int v = vehicleByName(name); v >= 0)
        return toVec(g_weapons.vehicles[v].box.centre);
    for (const auto& b : g_town.buildings)
        if (b.name == name)
        {
            const warzone::V3 c = b.bounds.centre();
            return {c.x, b.bounds.lo.y + std::min(5.0f, (b.bounds.hi.y - b.bounds.lo.y) * 0.35f), c.z};
        }
    ok = false;
    return {};
}

 
struct PlayerInput
{
    f32 throttle = 0.0f, steer = 0.0f, climb = 0.0f;
    bool brake = false, fire = false, keys = false;
};

bool menuBlocksGame();  

 
 
PlayerInput pollPlayerInput(SystemContext& ctx, f32 dt)
{
    PlayerInput in;
    auto& c = g_tankCtl;
    if (menuBlocksGame())
    {
         
        c.gameFocus = c.keysActive = c.zoom = false;
        if (c.captureRequested && ctx.services().contains<InputSystem>())
        {
            c.captureRequested = false;
            auto input = ctx.services().write<InputSystem>();
            input->setCursorCaptured(false);
            input->setCursorVisible(true);
        }
        return in;
    }
    if (ctx.services().contains<InputSystem>())
    {
        const auto input = ctx.services().read<InputSystem>();
        auto val = [&](const char* a) { return input->getActionValue(InternedString(a)); };
        in.throttle = val("tank_forward") - val("tank_back");
        in.steer = val("tank_left") - val("tank_right");
        in.climb = val("tank_brake") - val("veh_down");
        in.keys = std::abs(in.throttle) > 0.01f || std::abs(in.steer) > 0.01f || std::abs(in.climb) > 0.01f;
        in.brake = input->isActionDown(InternedString("tank_brake"));
        in.fire = input->isActionDown(InternedString("tank_fire")) || input->isActionDown(InternedString("tank_fire_key"));
        c.zoom = input->isActionDown(InternedString("tank_zoom"));
        c.camYaw += (val("tank_look_left") - val("tank_look_right")) * 1.5f * dt;
        c.camPitch += (val("tank_look_up") - val("tank_look_down")) * 0.7f * dt;
        const MouseState mouse = input->getMouseState();
        const bool hostFocus = input->hasGameInputFocus();
        if (!hostFocus || in.keys || in.fire || in.brake || c.zoom)
            c.released = false;
        if (input->getKeyState(KeyCode::ESCAPE).isPressed || input->getKeyState(KeyCode::F6).isPressed)
            c.released = true;
        c.gameFocus = hostFocus && !c.released;
        c.cursorCaptured = input->isCursorCaptured();
        c.lastMouseDx = mouse.deltaX;
        c.lastMouseDy = mouse.deltaY;
        if (c.mouseMode == 2 || (c.mouseMode == 1 && (c.cursorCaptured || c.gameFocus)))
        {
            const f32 k = c.mouseSensitivity * (c.zoom ? 0.35f : 1.0f);
            c.camYaw -= mouse.deltaX * k;
            c.camPitch -= mouse.deltaY * k;
            if (std::abs(mouse.deltaX) + std::abs(mouse.deltaY) > 0.0f)
                c.hasAim = false;  
        }
        if (in.keys || in.fire)
            c.scripted = false;  
    }
    c.keysActive = in.keys;
    c.camYaw = warzone::wrapPi(c.camYaw);
    if (ctx.services().contains<InputSystem>() && c.mouseMode != 0 && c.gameFocus != c.captureRequested)
    {
         
        c.captureRequested = c.gameFocus;
        auto input = ctx.services().write<InputSystem>();
        input->setCursorCaptured(c.gameFocus);
        input->setCursorVisible(!c.gameFocus);
    }
    return in;
}

 
void tankSystem(SystemContext& ctx)
{
    if (g_tanks.empty() || !g_weapons.ready)
        return;
    const f32 dt = std::min(ctx.deltaTime, 0.05f);
    for (auto& t : g_tanks)
    {
        auto* rt = ctx.tryGetComponent<TransformComponent>(t.root);
        if (!rt || t.vehicle >= g_weapons.vehicles.size())
            continue;
        const GroundDef& d = *t.def;
        auto& v = g_weapons.vehicles[t.vehicle];
        warzone::pt::PlayerEffects playerFx(g_pt.sys, t.player);
         
        if (v.respawnPending)
        {
            v.respawnPending = false;
            reviveVehicle(ctx, t.vehicle);
            t.state = warzone::TankState{};
            t.gun30 = warzone::AutocannonState{};
            t.gun30.ammo = t.gunParams.ammo;
            t.ammo = d.autocannon ? t.gun30.ammo : TANK_AMMO;
            t.wreckPosed = false;
            t.terrainPitch = t.terrainRoll = 0.0f;
            t.resetPending = true;
            if (rigActive(t))
            {
                std::scoped_lock lock(g_tankMutex);
                g_tankCtl.camYaw = v.respawnYaw;
                g_tankCtl.camPitch = -0.06f;
            }
        }
        if (t.resetPending || v.benched)
        {
            if (v.benched && !t.bodyOff && ctx.hasComponent<RigidbodyComponent>(t.root))
            {
                ctx.getCommandBuffer().removeComponent<RigidbodyComponent>(t.root);
                t.bodyOff = true;
            }
            const Vec3 at = v.benched ? Vec3{v.homePos.x, PARK_Y, v.homePos.z} : Vec3{v.respawnPos.x, g_terrain.height(v.respawnPos.x, v.respawnPos.z) + 0.05f, v.respawnPos.z};
            rt->position = at;
            rt->rotation = fromAxisAngle({0, 1, 0}, v.benched ? v.homeYaw : v.respawnYaw);
            v.box.centre = toV3(at + rotate(rt->rotation, d.hullBoxCentre));
            if (auto* a = ctx.tryGetComponent<AudioEmitterComponent>(t.engine))
                a->volume = 0.0f;
            if (!v.benched)
            {
                if (t.bodyOff)
                {
                    ctx.getCommandBuffer().addComponent<RigidbodyComponent>(t.root, RigidbodyComponent(t.body));
                    t.bodyOff = false;
                }
                else if (ctx.services().contains<PhysicsWorld>())
                    t.resetPending = !ctx.services().write<PhysicsWorld>()->teleport(t.root, rt->position, rt->rotation);
            }
            continue;
        }
        const Quat yawQ = fromAxisAngle({0, 1, 0}, yawOf(rt->rotation));
        const Vec3 fwd = rotate(yawQ, {0, 0, 1});
        const Vec3 pos = rt->position;
        const auto* bodyState = ctx.tryGetConstComponent<RigidbodyStateComponent>(t.root);
        const Vec3 measured = bodyState ? bodyState->linearVelocity : Vec3{};

         
        warzone::TankInput in;
        bool scriptFire = false;
        const bool active = rigActive(t);
        if (active && !v.wrecked)
        {
            std::scoped_lock lock(g_tankMutex);
            auto& c = g_tankCtl;
            if (g_snapCamera)
            {
                g_snapCamera = false;
                c.camYaw = yawOf(rt->rotation);
                c.camPitch = -0.06f;
            }
            const PlayerInput pin = pollPlayerInput(ctx, dt);
            in.throttle = pin.throttle;
            in.steer = pin.steer;
            in.brake = pin.brake;
            in.fire = pin.fire;
            c.camPitch = std::clamp(c.camPitch, -0.6f, d.autocannon ? 0.8f : 0.45f);
            if (c.scripted)
            {
                if (c.driveSeconds > 0.0f)
                {
                    c.driveSeconds -= dt;
                    in.throttle = c.driveThrottle;
                    in.steer = c.driveSteer;
                }
                else if (!c.path.empty())
                {
                    const Vec3 to = c.path.front() - pos;
                    const f32 dist = std::sqrt(to.x * to.x + to.z * to.z);
                    if (dist < 4.0f)
                        c.path.erase(c.path.begin());
                    else
                    {
                        const f32 err = warzone::wrapPi(std::atan2(to.x, to.z) - yawOf(rt->rotation));
                        in.steer = std::clamp(err * 2.2f, -1.0f, 1.0f);
                        const f32 slow = c.path.size() == 1 ? std::clamp(dist / 25.0f, 0.25f, 1.0f) : 1.0f;
                        in.throttle = std::abs(err) > 1.0f ? 0.0f : c.speedLimit * slow * (1.0f - std::min(0.6f, std::abs(err)));
                    }
                }
                else
                    in.brake = true;
            }
            in.throttle = std::clamp(in.throttle, -1.0f, 1.0f);
            c.lastThrottle = in.throttle;
            c.lastSteer = in.steer;
             
            if (c.hasAim)
            {
                t.aim = c.aim;
                t.range = length(c.aim - pos);
            }
            else if (g_cam.valid)
                t.aim = aimForCamera(t.vehicle, t.range);
            in.hasAim = c.hasAim || g_cam.valid;
            if (c.scriptZoom)
                c.zoom = true;
            scriptFire = c.fireQueue > 0;
        }
        else if (!v.wrecked)
            in.brake = true;  
        if (in.hasAim)
        {
             
            const Vec3 hull = rotate(conjugate(rt->rotation), t.aim - pos);
            const Vec3 untilted = d.tiltPivot + rotate(conjugate(tiltOf(t)), hull - d.tiltPivot);
            warzone::aimAngles(toV3(untilted - Vec3{d.turretPivot.x, d.gunPivot.y, d.turretPivot.z}), t.params.muzzleSpeed, in.aimYaw,
                               in.aimElevation);
            in.aimElevation -= d.restPitch;
        }
        const f32 yawErr = std::abs(warzone::wrapPi(in.aimYaw - t.state.turretYaw));
        const f32 elevErr = std::abs(std::clamp(in.aimElevation, t.params.minElevation, t.params.maxElevation) - t.state.elevation);
         
         
        t.settled = std::abs(t.state.pitchVel) < 0.04f && std::abs(t.state.rollVel) < 0.04f ? t.settled + dt : 0.0f;
        bool ampTrigger = in.fire;
        if (d.autocannon)
            ampTrigger = in.fire || (scriptFire && in.hasAim && yawErr < 0.02f && elevErr < 0.02f);
        else if (scriptFire && in.hasAim && t.state.reload <= 0.0f && t.settled > 0.3f && yawErr < 0.006f && elevErr < 0.004f)
        {
            in.fire = true;
            std::scoped_lock lock(g_tankMutex);
            if (g_tankCtl.fireQueue > 0)
                --g_tankCtl.fireQueue;
        }
        if (d.autocannon || t.ammo == 0)
            in.fire = false;  

         
        const bool wasReloading = t.state.reload > 1.0f;
        const Quat aimTilt = tiltOf(t);  
        if (!v.wrecked)
        {
            warzone::stepDrive(t.state, t.params, in, dt);
            warzone::stepGun(t.state, t.params, in, dt);
        }
        else
        {
            t.state.speed = warzone::approach(t.state.speed, 0.0f, 8.0f * dt);
            t.state.yawRate = 0.0f;
            if (!t.wreckPosed)
            {
                 
                t.wreckPosed = true;
                t.state.turretYaw = warzone::wrapPi(t.state.turretYaw + 0.65f);
                t.state.elevation = t.params.minElevation - 0.06f;
                t.state.pitchVel += 1.4f;
                t.state.rollVel -= 1.0f;
            }
        }
        warzone::stepSuspension(t.state, t.params, dt);
        t.onTarget = in.hasAim && yawErr < (d.autocannon ? 0.02f : 0.006f) && elevErr < (d.autocannon ? 0.02f : 0.004f);
        if (t.player && !d.autocannon && wasReloading && t.state.reload <= 1.0f && t.state.reload > 0.0f && t.ammo > 0)
            replayVoice(ctx, t.reloadVoice);

         
        f32 ground = pos.y;
        if (g_terrain.valid())
        {
            auto h = [&](f32 x, f32 z)
            {
                const Vec3 p = pos + rotate(yawQ, Vec3{x, 0.0f, z});
                return g_terrain.height(p.x, p.z);
            };
            const f32 cx = d.cornerX, cz = d.cornerZ;
            const f32 fl = h(cx, cz), fr = h(-cx, cz), bl = h(cx, -cz), br = h(-cx, -cz);
            ground = 0.25f * (fl + fr + bl + br);
            const f32 k = 1.0f - std::exp(-10.0f * dt);
            t.terrainPitch += (std::atan2(0.5f * (fl + fr) - 0.5f * (bl + br), 2.0f * cz) - t.terrainPitch) * k;
            t.terrainRoll += (std::atan2(0.5f * (fl + bl) - 0.5f * (fr + br), 2.0f * cx) - t.terrainRoll) * k;
        }
        f32 vy = std::clamp((ground - pos.y) * 9.0f, -14.0f, 6.0f);
        if (pos.y - ground > 0.8f)
            vy = measured.y - warzone::GRAVITY * dt;  
         
        const f32 along = dot(measured, fwd);
        if (std::abs(t.state.speed) > 1.0f && std::abs(along) < std::abs(t.state.speed) * 0.5f && bodyState)
        {
            t.blocked += dt;
            if (t.blocked > 0.25f)
                t.state.speed = along;
        }
        else
            t.blocked = 0.0f;
         
        t.ram = std::max(0.0f, t.ram - dt);
         
        t.stall = std::abs(in.throttle) > 0.5f && std::abs(along) < 1.0f ? t.stall + dt : 0.0f;
        const bool pushing = t.stall > 0.3f;
        if ((std::abs(t.state.speed) > 2.0f || pushing) && t.ram <= 0.0f && g_destruction.ready)
        {
            const f32 sgn = (pushing ? in.throttle : t.state.speed) > 0.0f ? 1.0f : -1.0f;
            const Vec3 bumper = pos + fwd * (d.bow * sgn) + Vec3{0, 1.6f, 0};
            const Vec3 side = rotate(yawQ, {d.cornerX - 0.2f, 0.0f, 0.0f});
            std::vector<u32> touching;
            for (f32 k : {-1.0f, 0.0f, 1.0f})  
                if (touching.empty())
                    g_destruction.structure.overlapSphere(toV3(bumper + side * k), 1.1f, touching);
            if (!touching.empty())
            {
                warzone::Blast b;
                b.centre = toV3(bumper);
                b.radius = 2.4f;
                const f32 sp = std::max(std::abs(t.state.speed), 3.0f);
                b.damage = d.ramDamage + d.ramPerSpeed * sp;
                b.push = sp * 0.9f;
                scheduleBlast(b, 0, false);
                spawnFx(ctx, FX_DUST, bumper, 2.5f, 8.0f, 3.5f, fwd * (2.0f * sgn) + Vec3{0, 0.8f, 0});
                t.state.speed *= 0.72f;
                t.ram = pushing ? 0.45f : 0.3f;
                ++t.rams;
                g_shake = std::min(1.0f, g_shake + (active ? 0.25f : 0.0f));
            }
        }
        if (ctx.services().contains<PhysicsWorld>())
        {
            auto physics = ctx.services().write<PhysicsWorld>();
            if (physics->hasBody(t.root))
            {
                const Vec3 vel = fwd * t.state.speed + Vec3{0, vy, 0};
                physics->setLinearVelocity(t.root, vel);
                physics->setAngularVelocity(t.root, {0, t.state.yawRate, 0});
            }
        }

         
        if (d.autocannon)
        {
            const int rounds = v.wrecked ? 0 : warzone::stepAutocannon(t.gun30, t.gunParams, ampTrigger, dt);
            t.spread = warzone::spreadOf(t.gun30, t.gunParams, std::abs(t.state.speed) / t.params.maxForward);
            if (rounds > 0)
            {
                Vec3 mHull, dHull;
                muzzleHull(t, mHull, dHull, &aimTilt);
                const Vec3 muzzle = pos + rotate(rt->rotation, mHull), dir = normalize(rotate(rt->rotation, dHull));
                auto& rng = g_weapons.rng;
                for (int i = 0; i < rounds; ++i)
                {
                    const Vec3 shot = toVec(warzone::scatter(toV3(dir), t.spread, rng.f01(), rng.f01()));
                    launch(warzone::ProjectileKind::Autocannon, muzzle + dir * (0.3f * i), shot * t.params.muzzleSpeed + measured, t.vehicle);
                    autocannonEffects(ctx, muzzle, dir, t.gun30.fired - i);
                }
                 
                t.state.pitchVel += 0.06f * rounds * std::cos(t.state.turretYaw);
                t.state.rollVel += 0.05f * rounds * std::sin(t.state.turretYaw);
                std::scoped_lock lock(g_tankMutex);
                if (active && g_tankCtl.fireQueue > 0)
                    g_tankCtl.fireQueue -= std::min<u32>(g_tankCtl.fireQueue, static_cast<u32>(rounds));
            }
            t.ammo = t.gun30.ammo;
        }
        Vec3 mHull, dHull;
        muzzleHull(t, mHull, dHull, t.state.firedThisStep ? &aimTilt : nullptr);
        const Vec3 muzzle = pos + rotate(rt->rotation, mHull), dir = normalize(rotate(rt->rotation, dHull));
        if (!d.autocannon && t.state.firedThisStep && t.ammo > 0)
        {
            --t.ammo;
            launch(warzone::ProjectileKind::Shell, muzzle, dir * t.params.muzzleSpeed + measured, t.vehicle);
            muzzleEffects(ctx, muzzle, dir, true);
        }
        if (active)
            t.gunImpactValid = predictImpact(muzzle, dir * t.params.muzzleSpeed + measured, t.vehicle, t.gunImpact,
                                             d.autocannon ? warzone::ProjectileKind::Autocannon : warzone::ProjectileKind::Shell);

         
        if (auto* m = ctx.tryGetComponent<TransformComponent>(t.model))
        {
            const Quat tilt = tiltOf(t);
            m->rotation = normalize(multiply(tilt, t.modelRestRot));
            m->position = d.tiltPivot + rotate(tilt, t.modelRestPos - d.tiltPivot);
        }
        if (auto* p = t.turret.e.isValid() ? ctx.tryGetComponent<TransformComponent>(t.turret.e) : nullptr)
            t.turret.pose(*p, t.state.turretYaw);
        if (auto* p = t.gun.e.isValid() ? ctx.tryGetComponent<TransformComponent>(t.gun.e) : nullptr)
            t.gun.pose(*p, -t.state.elevation, rotate(fromAxisAngle({1, 0, 0}, -t.state.elevation), restBarrel(d) * -recoilDistance(t)));
        for (auto& w : t.wheels)
            if (auto* p = w.e.isValid() ? ctx.tryGetComponent<TransformComponent>(w.e) : nullptr)
                w.pose(*p, t.state.wheelAngle);
        if (auto* p = t.sprocketLeft.e.isValid() ? ctx.tryGetComponent<TransformComponent>(t.sprocketLeft.e) : nullptr)
            t.sprocketLeft.pose(*p, t.state.sprocketLeft);
        if (auto* p = t.sprocketRight.e.isValid() ? ctx.tryGetComponent<TransformComponent>(t.sprocketRight.e) : nullptr)
            t.sprocketRight.pose(*p, t.state.sprocketRight);
        if (auto* p = t.idler.e.isValid() ? ctx.tryGetComponent<TransformComponent>(t.idler.e) : nullptr)
            t.idler.pose(*p, t.state.wheelAngle * (t.params.wheelRadius / 0.365f));

         
        v.rotation = rt->rotation;
        v.velocity = measured;
        v.box.centre = toV3(pos + rotate(rt->rotation, d.hullBoxCentre));
        v.box.axis[0] = toV3(rotate(rt->rotation, {1, 0, 0}));
        v.box.axis[1] = toV3(rotate(rt->rotation, {0, 1, 0}));
        v.box.axis[2] = toV3(rotate(rt->rotation, {0, 0, 1}));
        v.box.half = toV3(d.hullBoxHalf);

         
        if (auto* a = ctx.tryGetComponent<AudioEmitterComponent>(t.engine))
        {
            const f32 work = std::min(1.0f, std::abs(t.state.speed) / t.params.maxForward + std::abs(t.state.yawRate) * 0.5f);
            a->volume = v.wrecked ? 0.0f : mixAudio(wa::Category::Vehicles, length(pos - g_listener), d.engineRef, 0.3f + 0.7f * work);
            if (!t.engineStarted && ctx.services().contains<AudioWorld>())
            {
                t.engineStarted = true;
                ctx.services().write<AudioWorld>()->play(t.engine);
            }
        }
    }
}

 
 
bool groundCamera(SystemContext& ctx, TankRig* t, TransformComponent& cam, CameraComponent* lens, f32 dt)
{
    if (!t)
        return false;
    const auto* rt = ctx.tryGetConstComponent<TransformComponent>(t->root);
    if (!rt)
        return false;
    const GroundDef& d = *t->def;
    TankControl c;
    {
        std::scoped_lock lock(g_tankMutex);
        if (g_tankCtl.hasAim && g_tankCtl.cameraFollowsAim)
        {
             
            const Vec3 base = rt->position + Vec3{0, 3.0f, 0};
            const Vec3 to = g_tankCtl.aim - base;
            const f32 wantYaw = std::atan2(to.x, to.z);
            const f32 wantPitch = std::clamp(std::atan2(to.y, std::sqrt(to.x * to.x + to.z * to.z)) - 0.04f, -0.6f, 0.8f);
            const f32 k = 1.0f - std::exp(-(d.autocannon ? 4.0f : 2.5f) * dt);
            g_tankCtl.camYaw = warzone::wrapPi(g_tankCtl.camYaw + warzone::wrapPi(wantYaw - g_tankCtl.camYaw) * k);
            g_tankCtl.camPitch += (wantPitch - g_tankCtl.camPitch) * k;
        }
        else if (g_tankCtl.scripted)
        {
             
            const f32 k = 1.0f - std::exp(-1.8f * dt);
            g_tankCtl.camYaw = warzone::wrapPi(g_tankCtl.camYaw + warzone::wrapPi(yawOf(rt->rotation) - g_tankCtl.camYaw) * k);
            g_tankCtl.camPitch += (-0.07f - g_tankCtl.camPitch) * k;
        }
        c = g_tankCtl;
    }
    const Vec3 dir{std::cos(c.camPitch) * std::sin(c.camYaw), std::sin(c.camPitch), std::cos(c.camPitch) * std::cos(c.camYaw)};
    const Vec3 base = rt->position + Vec3{0, c.zoom ? d.camPivot - 0.2f : d.camPivot, 0};
    f32 dist = c.zoom ? d.zoomDist : d.camDist;
     
    if (g_destruction.ready)
    {
        u32 piece;
        f32 th;
        const Vec3 back = base - dir * dist + Vec3{0, 1.4f, 0};
        if (g_destruction.structure.raycast(toV3(base), toV3(back), piece, th))
            dist = std::max(3.0f, dist * th - 0.6f);
    }
    Vec3 eye = base - dir * dist + Vec3{0, c.zoom ? 0.4f : d.camUp, 0};
    eye = keepAboveGround(eye, 1.2f);
    const f32 k = 1.0f - std::exp(-14.0f * dt);
    cam.position = length(cam.position - eye) > 60.0f ? eye : cam.position + (eye - cam.position) * k;
    cam.rotation = lookRotation(dir);
    if (lens)
        lens->fov = toRadians(c.zoom ? d.zoomFov : 58.0f);
    return true;
}

 
u32 rgba(u8 r, u8 g, u8 b, f32 a) { return uiCanvasColor(r, g, b, static_cast<u8>(std::clamp(a, 0.0f, 1.0f) * 255.0f)); }

bool project(const Vec3& world, f32 w, f32 h, Vec2& out)
{
    if (!g_cam.valid)
        return false;
    const Vec3 f = g_cam.dir;
    const Vec3 right = normalize(cross(f, Vec3{0, 1, 0}));
    const Vec3 up = cross(right, f);
    const Vec3 d = world - g_cam.pos;
    const f32 z = dot(d, f);
    if (z < 0.5f)
        return false;
    const f32 th = std::tan(g_cam.fov * 0.5f);
    const f32 x = dot(d, right) / (z * th * (w / h)), y = dot(d, up) / (z * th);
    out = {w * 0.5f * (1.0f + x), h * 0.5f * (1.0f - y)};
    return true;
}

void ring(UiCanvasPainter& p, Vec2 c, f32 r, f32 width, u32 color, f32 a0 = 0.0f, f32 a1 = 6.2832f, int segments = 24)
{
    std::array<Vec2, 49> pts;
    segments = std::min(segments, 48);
    for (int i = 0; i <= segments; ++i)
    {
        const f32 a = a0 + (a1 - a0) * i / segments;
        pts[i] = {c.x + std::sin(a) * r, c.y - std::cos(a) * r};
    }
    p.polyline(pts.data(), static_cast<u32>(segments + 1), width, color);
}

struct HudColors
{
    u32 white = rgba(236, 234, 222, 0.9f), dim = rgba(236, 234, 222, 0.55f), amber = rgba(255, 176, 64, 0.95f);
    u32 green = rgba(120, 230, 120, 0.95f), red = rgba(240, 70, 50, 0.95f), panel = rgba(12, 14, 12, 0.55f);
};

bool matchActive();  
int playerScore();
std::string matchClockText();
bool drawMatchOverlay(UiCanvasPainter& p, f32 w, f32 h);
bool matchOverlayOnly();

 
 
Vec2 drawCommonHud(UiCanvasPainter& p, f32 w, f32 h, u32 vehicle, const Vec3& at, f32 hullYaw, f32 camYaw, std::string_view title)
{
    const HudColors col;
    const auto& v = g_weapons.vehicles[vehicle];
    const Vec2 mid{w * 0.5f, h * 0.5f};
    if (v.hitTimer > 0.0f)
    {
        const Vec3 dv = v.hitFrom - at;
        const f32 rel = camYaw - std::atan2(dv.x, dv.z);  
        ring(p, mid, 90.0f, 6.0f, rgba(240, 50, 40, std::min(1.0f, v.hitTimer)), rel - 0.35f, rel + 0.35f, 12);
    }
    const Vec2 o{24.0f, h - 176.0f};
    p.rect(o, {300, 152}, col.panel, 6.0f);
    const f32 hpFrac = v.health.hp / v.health.maxHp;
    const u32 hpCol = hpFrac > 0.6f ? col.green : hpFrac > 0.3f ? col.amber : col.red;
    p.text({o.x + 130, o.y + 12}, {160, 18}, std::format("HULL {:.0f}/{:.0f}", v.health.hp, v.health.maxHp), col.white, UiCanvasAlign::Left, 14.0f);
    p.rect({o.x + 130, o.y + 34}, {150, 7}, rgba(40, 40, 36, 0.8f), 2.0f);
    p.rect({o.x + 130, o.y + 34}, {150 * hpFrac, 7}, hpCol, 2.0f);
    if (matchActive())
    {
        p.text({24, 18}, {300, 22}, std::format("SCORE {}   KILLS {}", playerScore(), v.kills), col.white, UiCanvasAlign::Left, 18.0f);
        p.text({24, 42}, {360, 18}, std::format("{}  |  DEATHMATCH   [TAB] SCOREBOARD", title), col.dim, UiCanvasAlign::Left, 12.0f);
        p.text({mid.x - 60, 38}, {120, 22}, matchClockText(), col.amber, UiCanvasAlign::Center, 20.0f);
    }
    else
    {
        p.text({24, 18}, {300, 22}, std::format("SCORE {}   KILLS {}", v.kills * 100, v.kills), col.white, UiCanvasAlign::Left, 18.0f);
        p.text({24, 42}, {420, 18}, std::format("{}  |  WARZONE   [1] TANK  [2] AMP  [3] HELI  [4] PLANE   [M] DEATHMATCH", title), col.dim, UiCanvasAlign::Left, 12.0f);
    }
    const f32 hdg = std::fmod(360.0f + 180.0f - camYaw * 57.2958f, 360.0f);  
    p.text({mid.x - 60, 16}, {120, 20}, std::format("{:03.0f}", hdg), col.white, UiCanvasAlign::Center, 16.0f);
     
    const f32 mapSize = 220.0f, scale = mapSize / 440.0f;
    const Vec2 mo{w - mapSize - 24.0f, 18.0f};
    p.rect(mo, {mapSize, mapSize}, rgba(16, 20, 16, 0.62f), 4.0f);
    auto toMap = [&](f32 x, f32 z) { return Vec2{mo.x + mapSize * 0.5f + (x - at.x) * scale, mo.y + mapSize * 0.5f + (z - at.z) * scale}; };
    for (const auto& b : g_town.buildings)
    {
        Vec2 a = toMap(b.bounds.lo.x, b.bounds.lo.z), z = toMap(b.bounds.hi.x, b.bounds.hi.z);
        a = {std::max(a.x, mo.x), std::max(a.y, mo.y)};
        z = {std::min(z.x, mo.x + mapSize), std::min(z.y, mo.y + mapSize)};
        if (z.x > a.x && z.y > a.y)
            p.rect(a, {z.x - a.x, z.y - a.y}, rgba(150, 140, 120, 0.7f));
    }
    for (u32 i = 0; i < g_weapons.vehicles.size(); ++i)
    {
        if (i == vehicle || g_weapons.vehicles[i].benched)
            continue;
        const auto& o2 = g_weapons.vehicles[i];
        const Vec2 q = toMap(o2.box.centre.x, o2.box.centre.z);
        if (q.x > mo.x && q.x < mo.x + mapSize && q.y > mo.y && q.y < mo.y + mapSize)
            p.rect({q.x - 4, q.y - 4}, {8, 8}, o2.wrecked ? rgba(90, 90, 90, 0.9f) : o2.player ? rgba(110, 170, 240, 0.9f) : col.red, 2.0f);
    }
    const Vec2 tc = toMap(at.x, at.z);
    auto mapDir = [&](f32 yaw, f32 r) { return Vec2{tc.x + std::sin(yaw) * r, tc.y + std::cos(yaw) * r}; };
    p.triangle(mapDir(hullYaw, 9), mapDir(hullYaw + 2.5f, 6), mapDir(hullYaw - 2.5f, 6), col.green);
    p.line(tc, mapDir(camYaw, 26), 1.5f, rgba(236, 234, 222, 0.6f));
    p.text({mo.x, mo.y + mapSize + 2}, {mapSize, 16}, "N", col.dim, UiCanvasAlign::Center, 12.0f);
    f32 fy = mo.y + mapSize + 22;
    for (const auto& line : g_weapons.feed)
    {
        p.text({w - 424, fy}, {400, 18}, line.text, rgba(255, 220, 160, std::min(1.0f, (20.0f - line.age) / 3.0f)), UiCanvasAlign::Right, 14.0f);
        fy += 20;
    }
    if (v.wrecked)
        p.text({mid.x - 200, mid.y - 90}, {400, 40}, "DESTROYED", col.red, UiCanvasAlign::Center, 34.0f);
    return o;
}

void drawGroundHud(UiCanvasPainter& p, f32 w, f32 h, const TankRig* t, const TransformComponent* rt, const TankControl& c)
{
    const HudColors col;
    const GroundDef& d = *t->def;
    const Vec2 mid{w * 0.5f, h * 0.5f};
    if (d.autocannon)
    {
         
        const f32 px = std::max(6.0f, std::tan(t->spread) / std::tan(g_cam.fov * 0.5f) * h * 0.5f);
        const u32 cc = t->gun30.overheated ? col.red : col.white;
        ring(p, mid, px, 1.5f, cc);
        for (int i = 0; i < 4; ++i)
        {
            const f32 a = i * 1.5707963f;
            const Vec2 u{std::sin(a), -std::cos(a)};
            p.line({mid.x + u.x * (px + 4), mid.y + u.y * (px + 4)}, {mid.x + u.x * (px + 14), mid.y + u.y * (px + 14)}, 2.0f, cc);
        }
        p.rect({mid.x - 1.0f, mid.y - 1.0f}, {2, 2}, cc, 1.0f);
    }
    else
    {
        const f32 gap = 9.0f, len = 16.0f;
        p.line({mid.x - gap - len, mid.y}, {mid.x - gap, mid.y}, 2.0f, col.white);
        p.line({mid.x + gap, mid.y}, {mid.x + gap + len, mid.y}, 2.0f, col.white);
        p.line({mid.x, mid.y + gap}, {mid.x, mid.y + gap + len}, 2.0f, col.white);
        p.rect({mid.x - 1.5f, mid.y - 1.5f}, {3, 3}, col.white, 1.5f);
    }
    p.text({mid.x - 60, mid.y + 30}, {120, 20}, std::format("{:.0f} m", t->range), col.white, UiCanvasAlign::Center, 15.0f);
     
    Vec2 g;
    if (t->gunImpactValid && project(t->gunImpact, w, h, g))
    {
        const bool busy = d.autocannon ? t->gun30.overheated : t->state.reload > 0.0f;
        const u32 mc = busy ? col.amber : (t->onTarget ? col.green : col.white);
        ring(p, g, d.autocannon ? 6.0f : 11.0f, 2.0f, mc);
        p.line({g.x - 4, g.y}, {g.x + 4, g.y}, 1.5f, mc);
    }
    if (d.autocannon)
    {
         
        const f32 heat = t->gun30.heat;
        const u32 hc = t->gun30.overheated ? col.red : heat > 0.7f ? col.amber : col.green;
        p.rect({mid.x - 70, mid.y + 54}, {140, 6}, col.panel, 2.0f);
        p.rect({mid.x - 70, mid.y + 54}, {140 * heat, 6}, hc, 2.0f);
        p.text({mid.x - 90, mid.y + 62}, {180, 18},
               t->gun30.ammo == 0 ? std::string("NO AMMO") : t->gun30.overheated ? std::string("OVERHEAT") : std::format("HEAT {:.0f}%", heat * 100.0f), hc,
               UiCanvasAlign::Center, 13.0f);
    }
    else
    {
        const f32 frac = 1.0f - t->state.reload / t->params.reload;
        p.rect({mid.x - 70, mid.y + 54}, {140, 6}, col.panel, 2.0f);
        p.rect({mid.x - 70, mid.y + 54}, {140 * frac, 6}, t->state.reload > 0 ? col.amber : col.green, 2.0f);
        p.text({mid.x - 90, mid.y + 62}, {180, 18},
               t->ammo == 0 ? std::string("NO AMMO") : t->state.reload > 0 ? std::format("RELOADING {:.1f} s", t->state.reload) : std::string("LOADED"),
               t->state.reload > 0 ? col.amber : col.green, UiCanvasAlign::Center, 13.0f);
    }
    const f32 hy0 = yawOf(rt->rotation);
    const Vec2 o = drawCommonHud(p, w, h, t->vehicle, rt->position, hy0, c.camYaw, d.label);
    const auto& v = g_weapons.vehicles[t->vehicle];
     
    const Vec2 ic{o.x + 62, o.y + 76};
    auto onScreen = [&](f32 yaw, f32 x, f32 y)  
    {
        const f32 a = c.camYaw - yaw, b = a + 1.5707963f;
        return Vec2{ic.x + std::sin(a) * y - std::sin(b) * x, ic.y - std::cos(a) * y + std::cos(b) * x};
    };
    std::array<Vec2, 5> hull = {onScreen(hy0, 16, 30), onScreen(hy0, -16, 30), onScreen(hy0, -16, -30), onScreen(hy0, 16, -30), onScreen(hy0, 16, 30)};
    p.polyline(hull.data(), 5, 2.0f, v.wrecked ? col.red : col.white);
    const f32 turretYawWorld = hy0 + t->state.turretYaw;
    p.line(onScreen(turretYawWorld, 0, 0), onScreen(turretYawWorld, 0, d.autocannon ? 34 : 42), d.autocannon ? 2.0f : 3.0f, col.amber);
    ring(p, ic, d.autocannon ? 7.0f : 9.0f, 2.0f, col.amber);
    p.line({ic.x, ic.y - 56}, {ic.x, ic.y - 46}, 2.0f, col.dim);  
    p.text({o.x + 130, o.y + 50}, {160, 22}, std::format("{:.0f} km/h", std::abs(t->state.speed) * 3.6f), col.white, UiCanvasAlign::Left, 20.0f);
    p.text({o.x + 130, o.y + 80}, {160, 18}, d.autocannon ? std::format("30 mm HE-T  x{}", t->gun30.ammo) : std::format("125 mm HE  x{}", t->ammo),
           col.white, UiCanvasAlign::Left, 14.0f);
    p.text({o.x + 130, o.y + 100}, {160, 18}, std::format("TURRET {:+.0f}  GUN {:+.1f}", -t->state.turretYaw * 57.2958f, (t->state.elevation + d.restPitch) * 57.2958f),
           col.dim, UiCanvasAlign::Left, 12.0f);
    p.text({o.x + 130, o.y + 120}, {160, 18}, c.scripted ? std::string("AUTOPILOT") : std::string("DRIVER"), c.scripted ? col.amber : col.dim, UiCanvasAlign::Left, 12.0f);
}

void drawHeliHud(SystemContext& ctx, UiCanvasPainter& p, f32 w, f32 h, const TankControl& c);  
bool heliHudReady();
void drawPlaneHud(SystemContext& ctx, UiCanvasPainter& p, f32 w, f32 h, const TankControl& c);  
bool planeHudReady();

void hudSystem(SystemContext& ctx)
{
    bool toggleHud = false;
     
    if (ctx.services().contains<InputSystem>())
    {
        const auto input = ctx.services().read<InputSystem>();
        toggleHud = input->hasGameInputFocus() && input->wasActionPressedThisFrame(InternedString("hud_toggle"));
        int pick = -1;
        if (input->wasActionPressedThisFrame(InternedString("veh_tank")))
            pick = 0;
        else if (input->wasActionPressedThisFrame(InternedString("veh_amp")))
            pick = 1;
        else if (input->wasActionPressedThisFrame(InternedString("veh_heli")))
            pick = 2;
        else if (input->wasActionPressedThisFrame(InternedString("veh_plane")))
            pick = 3;
        if (pick >= 0 && !matchActive())  
        {
            {
                std::scoped_lock lock(g_tankMutex);
                setActiveLocked(static_cast<Active>(pick));
            }
            g_cameraCommand.pending = true;
            g_cameraCommand.mode = 6u + static_cast<u32>(pick);
            g_cameraCommand.spot = -1;
            g_cameraCommand.target.clear();
        }
    }
    auto hud = g_hud.lock();
    if (!hud || !hud->canvas.isValid())
        return;
    if (toggleHud) hud->visible = !hud->visible;
    if (!hud->visible) {
        hud->canvas.setVisible(false);
        return;
    }
    const auto* cr = g_cameraRigEntity.isValid() ? ctx.tryGetConstComponent<WarzoneCameraRig>(g_cameraRigEntity) : nullptr;
    const u32 mode = cr ? cr->mode : 0u;
    TankRig* t = mode == 6 ? playerTank() : mode == 7 ? playerAmp() : nullptr;
    const auto* rt = t ? ctx.tryGetConstComponent<TransformComponent>(t->root) : nullptr;
    const bool overlayOnly = matchOverlayOnly();
    const bool show = g_active == Active::Giga || overlayOnly || (t && rt && t->vehicle < g_weapons.vehicles.size()) || (mode == 8 && heliHudReady()) || (mode == 9 && planeHudReady());
    hud->canvas.setVisible(show);
    if (!show)
        return;
    const Extent2D e = g_viewExtent[1];
    const f32 w = e.width ? static_cast<f32>(e.width) : 1280.0f, h = e.height ? static_cast<f32>(e.height) : 720.0f;
    hud->canvas.setSize({w, h});
    TankControl c;
    {
        std::scoped_lock lock(g_tankMutex);
        c = g_tankCtl;
    }
    hud->canvas.update(
        [&](UiCanvasPainter& p)
        {
            if (gigaHud(p, w, h)) { drawMatchOverlay(p, w, h); return; }
            if (overlayOnly)
            {
                drawMatchOverlay(p, w, h);
                return;
            }
            if (mode == 8)
                drawHeliHud(ctx, p, w, h, c);
            else if (mode == 9)
                drawPlaneHud(ctx, p, w, h, c);
            else if (t && rt)
                drawGroundHud(p, w, h, t, rt, c);
            drawMatchOverlay(p, w, h);  
        });
}

void createHud(const ModuleContext& context, World world)
{
    auto& services = world.services();
    if (!services.contains<UiContext>() || !services.contains<UiViewContext>())
        return;
    auto& ui = services.get<UiContext>();
    auto& view = services.get<UiViewContext>();
    const auto font = view.registerFont(InternedString("warzonehud"), resolveContentPath("assets/fonts/ChakraPetch-Bold.ttf"));
    if (!font)
    {
        SWARNING_LOG("Warzone: HUD font: {}", font.error().message);
        return;
    }
    const auto style = view.stylesheet().loadLayerText(InternedString("warzonehud"),
                                                       R"({"defaults":{"text":{"color":[1,1,1,1],"size":15,"font":"warzonehud"}}})");
    if (!style)
    {
        SWARNING_LOG("Warzone: HUD style: {}", style.error().message);
        return;
    }
    auto hud = std::make_shared<Hud>();
    hud->scope = std::make_unique<UiScope>(ui);
    UiCanvasDesc desc;
    desc.rect.anchor = {0, 0};
    desc.rect.pivot = {0, 0};
    desc.rect.offset = {0, 0};
    desc.rect.size = {1280, 720};
    desc.maxPrimitives = 4000;
    hud->canvas = hud->scope->createCanvas(InternedString("warzone_hud"), desc);
    hud->canvas.setInteractable(false);
    hud->scope->commit();
    services.registerService(hud, context.owner);
    g_hud = hud;
}

void registerTankSystems(World world)
{
    SystemDescriptor sys;
    sys.moduleName = MODULE;
    sys.name = "WarzoneTank";
    sys.fn = tankSystem;
    sys.update = SystemPhaseDescriptor{
        .reads = {getComponentId<RigidbodyStateComponent>(), getComponentId<WarzoneTank>(), getComponentId<WarzoneAmp>()},
        .writes = {getComponentId<TransformComponent>(), getComponentId<AudioEmitterComponent>(), getComponentId<FlipbookComponent>(),
                   getComponentId<RenderTintComponent>(), getComponentId<PointLightComponent>()}};
    world.registerSystem(sys);
    SystemDescriptor hud;
    hud.moduleName = MODULE;
    hud.name = "WarzoneHud";
    hud.fn = hudSystem;
    hud.update = SystemPhaseDescriptor{.reads = {getComponentId<TransformComponent>(), getComponentId<WarzoneCameraRig>()},
        .stage = CoreExecutionStages::POST_UPDATE, .runsAfter = {{MODULE, "WarzoneCamera"}}};
    world.registerSystem(hud);
}

 
bool parsePathArg(const ControlArgs& args, std::string_view key, std::vector<Vec3>& out)
{
    const auto text = args.json(key);
    if (!text)
        return false;
    out.clear();
    const char* s = text->c_str();
    f32 x, z;
    int n;
    while (*s)
    {
        if (std::sscanf(s, "[%f,%f]%n", &x, &z, &n) == 2)
        {
            out.push_back({x, 0.0f, z});
            s += n;
        }
        else
            ++s;
    }
    return !out.empty();
}

void writeGroundState(const World& play, const TankRig* t, IFormatWriter& result)
{
    const auto* rt = play.tryGetComponent<TransformComponent>(t->root);
    const auto& v = g_weapons.vehicles[t->vehicle];
    const f32 p[3] = {rt->position.x, rt->position.y, rt->position.z};
    result.writeString("name", v.name);
    result.writeBool("active", rigActive(*t));
    result.writeFloats("position", p);
    result.writeF32("headingDeg", std::fmod(360.0f + 180.0f - yawOf(rt->rotation) * 57.2958f, 360.0f));
    result.writeF32("speed", t->state.speed);
    result.writeF32("kmh", std::abs(t->state.speed) * 3.6f);
    result.writeF32("yawRate", t->state.yawRate);
    result.writeF32("turretDeg", t->state.turretYaw * 57.2958f);
    result.writeF32("gunDeg", (t->state.elevation + t->def->restPitch) * 57.2958f);
    result.writeF32("reload", t->state.reload);
    result.writeF32("recoil", t->isAmp() ? t->gun30.recoil : t->state.recoil);
    result.writeF32("hullPitchDeg", (t->terrainPitch + t->state.pitch) * 57.2958f);
    result.writeF32("hullRollDeg", (t->terrainRoll + t->state.roll) * 57.2958f);
    result.writeF32("wheelAngle", t->state.wheelAngle);
    result.writeU32("shots", t->isAmp() ? t->gun30.fired : t->state.shots);
    result.writeU32("ammo", t->ammo);
    if (t->isAmp())
    {
        result.writeF32("heat", t->gun30.heat);
        result.writeBool("overheated", t->gun30.overheated);
        result.writeF32("spreadMrad", t->spread * 1000.0f);
    }
    result.writeU32("rams", t->rams);
    result.writeF32("hp", v.health.hp);
    result.writeBool("wrecked", v.wrecked);
    const f32 a[3] = {t->aim.x, t->aim.y, t->aim.z};
    result.writeFloats("aim", a);
    result.writeF32("range", t->range);
    result.writeBool("onTarget", t->onTarget);
    const f32 g[3] = {t->gunImpact.x, t->gunImpact.y, t->gunImpact.z};
    result.writeFloats("gunImpact", g);
    result.writeBool("gunImpactValid", t->gunImpactValid);
    std::scoped_lock lock(g_tankMutex);
    const auto& c = g_tankCtl;
    result.writeBool("scripted", c.scripted);
    result.writeU32("waypoints", static_cast<u32>(c.path.size()));
    result.writeU32("fireQueue", c.fireQueue);
    result.writeF32("camYawDeg", c.camYaw * 57.2958f);
    result.writeF32("camPitchDeg", c.camPitch * 57.2958f);
    result.writeBool("cursorCaptured", c.cursorCaptured);
    result.writeBool("gameFocus", c.gameFocus);
    result.writeF32("mouseDx", c.lastMouseDx);
    result.writeF32("mouseDy", c.lastMouseDy);
    result.writeF32("throttle", c.lastThrottle);
    result.writeF32("steer", c.lastSteer);
    result.writeBool("keysActive", c.keysActive);
    const f32 cp[3] = {g_cam.pos.x, g_cam.pos.y, g_cam.pos.z};
    result.writeFloats("camera", cp);
    result.writeU32("viewWidth", g_viewExtent[1].width);
    result.writeU32("viewHeight", g_viewExtent[1].height);
}

 
void registerGroundCommands(const ModuleContext& context, bool amp)
{
    auto& control = context.control;
    const std::string_view owner = MODULE;
    static const std::array<std::string_view, 3> mouseModes = {"off", "captured", "always"};
    const char* name = amp ? "warzoneAmp" : "warzoneTank";
    control.registerCommand(
        name, owner,
        amp ? "drives the player AMP by script (filming, tests) and makes it the active vehicle: path [[x,z],...] or goto [x,z] at speed 0-1; "
              "aim [x,y,z] or aimAt <building or vehicle>; fire n autocannon rounds when on target; drive throttle/steer for seconds; stop; "
              "release gives control back to the keyboard; camera=true switches the game camera to the AMP; mouse sets mouse look"
            : "drives the player tank by script (filming, tests) and makes it the active vehicle: path [[x,z],...] or goto [x,z] at speed 0-1; "
              "aim [x,y,z] or aimAt <building or vehicle>; fire n rounds when on target; drive throttle/steer for seconds; stop; release gives "
              "control back to the keyboard; camera=true switches the game camera to the tank; mouse sets mouse look",
        {optionalArg("path", ArgKind::Json, "[[x,z],...] waypoints"), optionalArg("goto", ArgKind::Json, "[x,z] one waypoint"),
         optionalArg("speed", ArgKind::Number, "fraction of top speed", "1"), optionalArg("aim", ArgKind::Json, "[x,y,z] aim point"),
         optionalArg("aimAt", ArgKind::String, "building or vehicle name"), optionalArg("fire", ArgKind::Integer, "rounds to fire when on target"),
         optionalArg("throttle", ArgKind::Number, "-1..1 for `seconds`"), optionalArg("steer", ArgKind::Number, "-1..1 (+ left) for `seconds`"),
         optionalArg("seconds", ArgKind::Number, "how long throttle/steer last"), optionalArg("stop", ArgKind::Boolean, "clear the script and brake"),
         optionalArg("release", ArgKind::Boolean, "hand control back to the player (keyboard + mouse)"),
         optionalArg("camera", ArgKind::Boolean, "game camera follows this vehicle (third person)"),
         optionalArg("followAim", ArgKind::Boolean, "the camera swings to a scripted aim", "true"),
         optionalArg("zoom", ArgKind::Boolean, "hold the gunner's sight (narrow view)"),
         optionalArg("mouse", ArgKind::String, "mouse look: off, captured (when the game holds the mouse), always", "captured", mouseModes),
         optionalArg("sensitivity", ArgKind::Number, "radians per mouse pixel")},
        [&control, amp](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            if (!control.world("play") || !playerRig(amp))
                return amp ? "warzoneAmp needs Play running with a player AMP (WarzoneAmp role 0)"
                           : "warzoneTank needs Play running with a player tank (WarzoneTank role 0)";
            std::scoped_lock lock(g_tankMutex);
            setActiveLocked(amp ? Active::Amp : Active::Tank);
            auto& c = g_tankCtl;
            if (args.boolean("stop").value_or(false))
            {
                c.scripted = true;
                c.path.clear();
                c.driveSeconds = 0.0f;
                c.fireQueue = 0;
            }
            if (args.boolean("release").value_or(false))
            {
                c.scripted = false;
                c.hasAim = false;
                c.path.clear();
                c.fireQueue = 0;
                c.released = true;  
                c.scriptZoom = false;
            }
            std::vector<Vec3> path;
            if (parsePathArg(args, "path", path) || parsePathArg(args, "goto", path))
            {
                c.path = path;
                c.scripted = true;
                c.driveSeconds = 0.0f;
            }
            if (auto v = number(args, "speed"))
                c.speedLimit = std::clamp(*v, 0.05f, 1.0f);
            if (auto s = number(args, "seconds"))
            {
                c.driveSeconds = *s;
                c.driveThrottle = number(args, "throttle").value_or(0.0f);
                c.driveSteer = number(args, "steer").value_or(0.0f);
                c.scripted = true;
            }
            Vec3 aim;
            if (vec3Arg(args, "aim", aim))
            {
                c.aim = aim;
                c.hasAim = true;
            }
            if (auto target = args.string("aimAt"))
            {
                bool ok;
                const Vec3 p = aimPointForName(*target, ok);
                if (!ok)
                    return std::format("no building or vehicle '{}'", *target);
                c.aim = p;
                c.hasAim = true;
            }
            if (auto n = args.integer("fire"))
                c.fireQueue = static_cast<u32>(std::max<long long>(0, static_cast<long long>(*n)));
            if (auto z = args.boolean("zoom"))
                c.scriptZoom = *z;
            if (auto f = args.boolean("followAim"))
                c.cameraFollowsAim = *f;
            if (auto m = args.string("mouse"))
                c.mouseMode = *m == "off" ? 0 : *m == "always" ? 2 : 1;
            if (auto s = number(args, "sensitivity"))
                c.mouseSensitivity = *s;
            if (args.boolean("camera").value_or(false))
            {
                g_cameraCommand.pending = true;
                g_cameraCommand.mode = amp ? 7u : 6u;
                g_cameraCommand.spot = -1;
                g_cameraCommand.target.clear();
            }
            result.writeBool("scripted", c.scripted);
            result.writeU32("waypoints", static_cast<u32>(c.path.size()));
            result.writeBool("hasAim", c.hasAim);
            const f32 a[3] = {c.aim.x, c.aim.y, c.aim.z};
            result.writeFloats("aim", a);
            result.writeU32("fireQueue", c.fireQueue);
            return {};
        });

    control.registerQuery(amp ? "warzoneAmpState" : "warzoneTankState", owner,
                          amp ? "the player AMP in Play: position, heading, speed, turret and gun angles, rounds fired, ammo, heat and overheat, "
                                "spread, health, aim point and range, predicted impact, autopilot, input focus"
                              : "the player tank in Play: position, heading, speed, turret and gun angles, reload, ammo, health, aim point and "
                                "range, predicted shell impact, autopilot, input focus (cursor captured, mouse delta)",
                          {},
                          [&control, amp](const ControlArgs&, IFormatWriter& result) -> std::string
                          {
                              const World* play = control.world("play");
                              TankRig* t = playerRig(amp);
                              if (!play || !t)
                                  return amp ? "no player AMP (start Play)" : "no player tank (start Play)";
                              if (!play->tryGetComponent<TransformComponent>(t->root))
                                  return "the vehicle has no transform";
                              writeGroundState(*play, t, result);
                              return {};
                          });
}

void registerTankCommands(const ModuleContext& context)
{
    registerGroundCommands(context, false);
    registerGroundCommands(context, true);
}

void mapTankInput(const ModuleContext& context)
{
    if (!context.services.contains<InputSystem>())
        return;
    auto input = context.services.write<InputSystem>();
    auto axis = [&](const char* a, KeyCode k) { input->mapActionToKey(InternedString(a), k, InputActionType::Axis); };
    axis("tank_forward", KeyCode::W);
    axis("tank_back", KeyCode::S);
    axis("tank_left", KeyCode::A);
    axis("tank_right", KeyCode::D);
    axis("tank_brake", KeyCode::SPACE);  
    axis("veh_down", KeyCode::LSHIFT);   
    axis("tank_fire_key", KeyCode::F);
    axis("tank_look_left", KeyCode::Q);
    axis("tank_look_right", KeyCode::E);
    axis("tank_look_up", KeyCode::UP);
    axis("tank_look_down", KeyCode::DOWN);
    input->mapActionToMouse(InternedString("tank_fire"), MouseButton::Left, InputActionType::Axis);
    input->mapActionToMouse(InternedString("tank_zoom"), MouseButton::Right, InputActionType::Axis);
    input->mapActionToKey(InternedString("veh_tank"), KeyCode::NUM_1, InputActionType::Press);
    input->mapActionToKey(InternedString("veh_amp"), KeyCode::NUM_2, InputActionType::Press);
    input->mapActionToKey(InternedString("veh_heli"), KeyCode::NUM_3, InputActionType::Press);
    input->mapActionToKey(InternedString("veh_plane"), KeyCode::NUM_4, InputActionType::Press);
    input->mapActionToKey(InternedString("match_start"), KeyCode::M, InputActionType::Press);
    input->mapActionToKey(InternedString("match_board"), KeyCode::TAB, InputActionType::Axis);
    input->mapActionToKey(InternedString("hud_toggle"), KeyCode::H, InputActionType::Press);
}

void unmapTankInput(const ModuleContext& context)
{
    if (!context.services.contains<InputSystem>())
        return;
    auto input = context.services.write<InputSystem>();
    for (auto name : {"tank_forward", "tank_back", "tank_left", "tank_right", "tank_brake", "veh_down", "tank_fire_key", "tank_look_left",
                      "tank_look_right", "tank_look_up", "tank_look_down", "tank_fire", "tank_zoom", "veh_tank", "veh_amp", "veh_heli", "veh_plane", "match_start", "match_board", "hud_toggle"})
        input->unmapAction(InternedString(name));
}
