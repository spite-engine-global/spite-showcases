constexpr f32 NUKE_CAMERA_HOLD = 0.75f, NUKE_CAMERA_RETURN = 0.25f;

 
struct NukeBattleTime : std::enable_shared_from_this<NukeBattleTime>
{
    ControlServer& control;
    bool active = false, wanted = false;
    f32 previous = 1, value = 1, frameScale = 1, requested = 1;
    explicit NukeBattleTime(ControlServer& c) : control(c) {}
    ~NukeBattleTime() { restore(); }

    bool set(f32 scale)
    {
        const auto reply = control.handle(std::format(R"({{"command":"setConfig","args":{{"section":"application.timing","path":"timeScale","value":{}}}}})", scale));
        JsonFragmentReader r(reply);
        bool ok = false; std::string status;
        if (r.readBool("ok", ok) == FieldRead::Ok && ok && r.beginObject("result") == FieldRead::Ok &&
            r.readString("status", status) == FieldRead::Ok && (status == "applied" || status == "deferred")) {
             
            value = scale; return true;
        }
        SWARNING_LOG("GIGA cinematic timing change failed: {}", reply);
        return false;
    }
    void restore()
    {
        wanted = false;
        if (active && set(previous)) active = false;
    }
    void start()
    {
        wanted = true;
        control.runAtFrameStart(MODULE, R"({"query":"config","args":{"section":"application.timing"}})",
            [weak = weak_from_this()](const std::string& reply) {
                const auto self = weak.lock();
                if (!self || !self->wanted) return;
                JsonFragmentReader r(reply);
                u32 sections = 0;
                if (r.beginObject("result") == FieldRead::Ok && r.beginArray("sections", sections) == FieldRead::Ok)
                    for (u32 i = 0; i < sections; ++i) {
                        u32 fields = 0;
                        if (r.beginObject({}) != FieldRead::Ok) break;
                        if (r.beginArray("fields", fields) == FieldRead::Ok) {
                            for (u32 j = 0; j < fields; ++j) {
                                if (r.beginObject({}) != FieldRead::Ok) break;
                                std::string path; f32 scale = 1;
                                if (r.readString("path", path) == FieldRead::Ok && path == "timeScale" &&
                                    r.readF32("running", scale) == FieldRead::Ok && scale > 0) {
                                    self->previous = self->frameScale = self->value = scale;
                                    self->requested = scale * 0.18f;
                                    self->active = self->set(self->requested);
                                    return;
                                }
                                r.endObject();
                            }
                            r.endArray();
                        }
                        r.endObject();
                    }
                SWARNING_LOG("GIGA cinematic could not read running time scale: {}", reply);
            });
    }
    void update(f32 factor)
    {
        if (!active || !wanted || std::abs(previous * factor - requested) < 0.005f) return;
        requested = previous * factor;
        g_frameStart([weak = weak_from_this(), factor] {
            if (const auto self = weak.lock(); self && self->wanted && self->active) self->set(self->previous * factor);
        });
    }
    void stop()
    {
        if (!active && !wanted) return;
        wanted = false;
        g_frameStart([weak = weak_from_this()] {
            if (const auto self = weak.lock()) self->restore();
        });
    }
    f32 unscaledDelta(f32 dt)
    {
         
        const f32 unscaled = dt / frameScale;
        frameScale = value;
        return unscaled;
    }
};
std::weak_ptr<NukeBattleTime> g_nukeBattleTime;
void restoreNukeBattleTime() { if (const auto time = g_nukeBattleTime.lock()) time->restore(); }

struct GigaState
{
    Entity root = Entity::undefined(), skin = Entity::undefined(), nuke = Entity::undefined();
    u32 slot = ~0u;
    bool detonate = false; Vec3 blastAt{};
    bool enabled = false, fire = false, pickup = false, toss = false;
    Vec3 pos{230, 0, 330}, aim{0, 0, 1}, nukePos{}, nukeVel{};
    f32 yaw = 0, cooldown = 0, action = 0, idle = 0, scriptMove = 0, nukeAge = -1;
    int carry = -1, thrown = -1, swapFrames = 0;
    bool launchedBody = false, grounded = true, jump = false;
    f32 verticalSpeed = 0;
    Vec3 throwVel{}, lastThrown{};
    f32 thrownAge = 0;
    u32 nukes = 0, throws = 0;
    struct NukeCamera {
        f32 age = -1, returnAt = -1, fromFov = 1;
        Vec3 from{};
        Quat rotation = Quat::identity();
    } camera;
};
GigaState g_giga;
std::mutex g_gigaMutex;
bool gigaWanted() { return g_battle.giga >= 0.5f; }
bool gigaCinematicActive() { return g_giga.enabled && g_giga.camera.age >= 0; }

void captureNukeCamera()
{
    auto& shot = g_giga.camera;
    shot.from = g_cam.pos; shot.rotation = lookRotation(g_cam.dir); shot.fromFov = g_cam.fov;
}

void returnNukeCamera()
{
    auto& shot = g_giga.camera;
    if (shot.age < 0 || shot.returnAt >= 0) return;
    shot.returnAt = shot.age;
    captureNukeCamera();
}

f32 nukeFlightScale()
{
    const auto& shot = g_giga.camera;
    if (shot.age < 0) return 1;
    const f32 t = shot.returnAt < 0 ? 0 : std::clamp((shot.age - shot.returnAt) / NUKE_CAMERA_RETURN, 0.0f, 1.0f);
    return 0.18f + 0.82f * t * t * (3 - 2 * t);
}

void gigaAnimation(SystemContext& ctx, const char* clip, f32 speed = 1)
{
    if (auto* p = ctx.tryGetComponent<AnimationPlayerComponent>(g_giga.skin))
    {
        const InternedString name(std::string("wz_giga_chad.") + clip);
        if (p->clip != name) { p->clip = name; p->time = 0; }
        p->playing = true; p->loop = true; p->speed = speed;
    }
}

void attachGiga(const ModuleContext& context, World world)
{
    g_giga = {};
    const auto time = std::make_shared<NukeBattleTime>(context.control);
    world.services().registerService(time, context.owner);
    g_nukeBattleTime = time;
    if (context.services.contains<InputSystem>()) {
        auto input = context.services.write<InputSystem>();
        input->mapActionToKey(InternedString("giga_jump"), KeyCode::SPACE, InputActionType::Press);
    }
    if (!context.services.contains<ModelSpawner>()) { SWARNING_LOG("GIGA: no ModelSpawner"); return; }
    const auto& spawner = context.services.get<ModelSpawner>();
    const std::array<Entity, 1> parents{Entity::undefined()};
    auto& g = g_giga;
    auto names = world.createQueryBuilder().buildQuery<Read<EntityNameComponent>>();
    for (auto [entity, name] : names->view<Entity, Read<EntityNameComponent>>()) {
        if (name.name.toView() == "GIGA Chad") g.root = entity;
        if (name.name.toView() == "GIGA Truth Nuke") g.nuke = entity;
    }
    if (!g.root.isValid()) {
        auto made = spawner.spawn(world, InternedString("wz_giga_chad"), parents);
        if (!made) { SWARNING_LOG("GIGA: {}", made.error().message); return; }
        g.root = made.value()[0];
    }
    setEntityName(world, g.root, "GIGA Chad");
    std::vector<Entity> ents; subtree(world, g.root, ents);
    for (Entity e : ents) if (world.hasComponent<SkinComponent>(e)) { g.skin = e; break; }
    if (!g.skin.isValid()) { SWARNING_LOG("GIGA: imported model has no skin"); return; }
    AnimationPlayerComponent anim; anim.clip = InternedString("wz_giga_chad.laugh_01.001");
    if (!world.hasComponent<AnimationPlayerComponent>(g.skin)) world.addComponent<AnimationPlayerComponent>(g.skin, anim);
    auto& tr = world.getComponent<TransformComponent>(g.root); tr.position = {230, PARK_Y, 330};
    VehicleSlot v; v.root = g.root; v.name = "GIGA Chad"; v.weapon = "Truth nuke";
    v.player = true; v.immortal = true; v.benched = true; v.health.hp = v.health.maxHp = 1000;
    v.box.half = {0.5f, 1.05f, 0.5f}; v.box.centre = toV3(tr.position);
    g.slot = registerVehicle(std::move(v));
    if (!g.nuke.isValid()) {
        auto bomb = spawner.spawn(world, InternedString("wz_truth_nuke"), parents);
        if (!bomb) SWARNING_LOG("GIGA nuke: {}", bomb.error().message);
        else { g.nuke = bomb.value()[0]; setEntityName(world, g.nuke, "GIGA Truth Nuke"); }
    }
    if (g.nuke.isValid()) {
        auto& bomb = world.getComponent<TransformComponent>(g.nuke);
        bomb.position = {0, PARK_Y, 0};
        bomb.scale = {6, 6, 6};
    }

}

void gigaNukeBlast(SystemContext& ctx, const Vec3& at)
{
    warzone::pt::PlayerEffects playerFx(g_pt.sys, true);
    warzone::ProjectileSpec spec = warzone::specOf(warzone::ProjectileKind::Bomb);
    spec.blastRadius = warzone::giga::NUKE_RADIUS; spec.blastDamage = warzone::giga::NUKE_DAMAGE; spec.push = 220;
    explode(ctx, at, spec, g_giga.slot, -1, false, false);
    scheduleNukeDestruction({toV3(at), spec.blastRadius, spec.blastDamage, spec.push});
    flash(ctx, at + Vec3{0, 24, 0}, 24000000, 0.65f, 1300);
    playVoice(g_destruction.blastVoices, g_destruction.nextBlastVoice, at, 1500, 1, wa::Category::Explosions, warzone::giga::NUKE_RADIUS);
    g_shake = 1;
    if (particlesOn()) g_pt.sys.nuke(toV3(at));
    auto& w = g_weapons;
    const Vec3 ground{at.x, groundAt(at.x, at.z), at.z};
    const Vec3 up = groundNormalAt(at.x, at.z);
    layMark(ctx, w.scorchPool, w.scorchE, warzone::decal::SCORCH, ground, up, 700, SCORCH_LIFE, 0.65f);
}
void gigaCombat(SystemContext& ctx, f32 dt)
{
    auto& g=g_giga;
    if (ctx.services().contains<InputSystem>() && !menuBlocksGame() && !gigaCinematicActive()) {
        const auto in=ctx.services().read<InputSystem>();
        if(in->hasGameInputFocus()) {
            g.fire |= in->getKeyState(KeyCode::F).isPressed;
            g.pickup |= in->getKeyState(KeyCode::G).isPressed;
            g.toss |= in->getKeyState(KeyCode::T).isPressed;
        }
    }
    if (g.fire && warzone::giga::ready(g.cooldown) && g.nukeAge < 0 && g.nuke.isValid()) {
        g.cooldown=warzone::giga::COOLDOWN; g.action=0.9f;
        g.nukePos=g.pos+Vec3{0,1.8f,0}+g.aim*2;
        g.nukeVel=toVec(warzone::giga::throwVelocity(toV3(g.aim),65)); g.nukeAge=0; ++g.nukes;
        g.camera = {}; g.camera.age = 0;
        captureNukeCamera();
        if (const auto time = g_nukeBattleTime.lock()) time->start();
    }
    g.fire=false;
    if(g.nukeAge>=0) {
        const auto time = g_nukeBattleTime.lock();
        const f32 flightDt = dt * (time && time->active ? 1 : nukeFlightScale());
        const Vec3 prev=g.nukePos; g.nukeAge+=flightDt; g.nukeVel.y-=9.81f*flightDt; g.nukePos+=g.nukeVel*flightDt;
        const Hit hit=traceSegment(prev,g.nukePos,g.slot);
        if(hit.kind!=Hit::None || g.nukeAge>15) {
            if (hit.kind != Hit::None) g.nukePos = hit.pos;
            gigaNukeBlast(ctx,g.nukePos); g.nukeAge=-1;
            returnNukeCamera();
        }
        if(auto* tr=ctx.tryGetComponent<TransformComponent>(g.nuke)) {
            tr->position=g.nukeAge<0 ? Vec3{0,PARK_Y,0} : g.nukePos;
            tr->rotation=fromAxisAngle({1,0,0},g.nukeAge*4);
        }
    }
    if(g.pickup && g.carry<0 && g.thrown<0) {
        std::vector<warzone::giga::Target> candidates;
        for(const auto& v:g_weapons.vehicles) candidates.push_back({v.box.centre,!v.player && v.ai,!v.wrecked,!v.benched && !v.gigaControlled});
        g.carry=warzone::giga::pick(toV3(g.pos),candidates);
        if(g.carry>=0) {
            auto& v=g_weapons.vehicles[g.carry]; v.gigaControlled=true;
            if(ctx.hasComponent<RigidbodyComponent>(v.root)) ctx.getCommandBuffer().removeComponent<RigidbodyComponent>(v.root);
            g.swapFrames=3;
        }
    }
    g.pickup=false;
    if(g.carry>=0) {
        auto& v=g_weapons.vehicles[g.carry];
        const Vec3 at=g.pos+toVec(warzone::giga::carryOffset(v.box.half.y));
        if(auto* tr=ctx.tryGetComponent<TransformComponent>(v.root)) { tr->position=at; tr->rotation=Quat::identity(); }
        v.box.centre=toV3(at); v.rotation=Quat::identity(); v.velocity={};
        if(g.swapFrames>0) --g.swapFrames;
        if(g.toss && g.swapFrames==0) {
            BoxColliderComponent collider; collider.halfExtents=toVec(v.box.half);
            if(!ctx.hasComponent<BoxColliderComponent>(v.root)) ctx.getCommandBuffer().addComponent<BoxColliderComponent>(v.root,std::move(collider));
            RigidbodyComponent body; body.mass=18000; body.bodyType=RigidbodyComponent::BodyType::Dynamic;
            body.objectLayer=static_cast<u16>(ObjectLayer::Vehicle); body.restitution=0.12f; body.linearDamping=0.01f;
            ctx.getCommandBuffer().addComponent<RigidbodyComponent>(v.root,std::move(body));
            g.throwVel=toVec(warzone::giga::throwVelocity(toV3(g.aim))); g.thrown=g.carry; g.carry=-1;
            g.lastThrown=at; g.launchedBody=false; g.thrownAge=0; ++g.throws; g.action=0.9f;
        }
    }
    g.toss=false;
    if(g.thrown>=0 && ctx.services().contains<PhysicsWorld>()) {
        auto& v=g_weapons.vehicles[g.thrown]; auto physics=ctx.services().write<PhysicsWorld>();
        if(!g.launchedBody) {
            if(physics->hasBody(v.root)) {
                g.launchedBody=physics->setLinearVelocity(v.root,g.throwVel);
                physics->setAngularVelocity(v.root,{1,2,0.5f});
            }
        } else if(const auto* tr=ctx.tryGetConstComponent<TransformComponent>(v.root)) {
            const Vec3 at=tr->position; g.thrownAge+=dt;
            v.box.centre=toV3(at); v.rotation=tr->rotation;
            v.velocity=(at-g.lastThrown)/std::max(dt,0.001f);
            Hit hit=traceSegment(g.lastThrown,at,static_cast<u32>(g.thrown));
            const auto* state=ctx.tryGetConstComponent<RigidbodyStateComponent>(v.root);
            const float speed=state ? length(state->linearVelocity) : 110;
            const bool hard=g.thrownAge>0.08f && (hit.kind!=Hit::None || at.y<=groundAt(at.x,at.z)+v.box.half.y+0.4f || speed<25);
            if(hard || g.thrownAge>12) {

                if(hit.kind==Hit::Vehicle) damageVehicle(ctx,hit.index,10000,at,g.slot);
                auto spec=warzone::specOf(warzone::ProjectileKind::Bomb); spec.blastRadius=22; spec.blastDamage=14000; spec.push=45;
                explode(ctx,at,spec,g.slot,-1); wreckVehicle(ctx,static_cast<u32>(g.thrown),g.slot);
                ctx.getCommandBuffer().removeComponent<RigidbodyComponent>(v.root);
                ctx.getCommandBuffer().removeComponent<BoxColliderComponent>(v.root);
                for(auto& u:g_ai.units) if(u.slot==static_cast<u32>(g.thrown)) { u.s.pos=toV3(at); u.s.vel={}; u.crashed=true; u.dead=0; }
                v.gigaControlled=false; g.thrown=-1;
            }
            g.lastThrown=at;
        }
    }
}
void gigaSystem(SystemContext& ctx)
{
    std::scoped_lock lock(g_gigaMutex);
    auto& g = g_giga;
    if (g.detonate) { gigaNukeBlast(ctx, g.blastAt); g.detonate = false; ++g.nukes; }
    const auto battleTime = g_nukeBattleTime.lock();
    if (g.slot >= g_weapons.vehicles.size()) {
        g.camera = {};
        if (battleTime) battleTime->stop();
        return;
    }
    auto* tr = ctx.tryGetComponent<TransformComponent>(g.root);
    if (!tr) {
        g.camera = {};
        if (battleTime) battleTime->stop();
        return;
    }
    const f32 dt = std::min(ctx.deltaTime, 0.05f);
    const f32 cameraDt = battleTime ? battleTime->unscaledDelta(ctx.deltaTime) : ctx.deltaTime;
    if (g.camera.age >= 0) {
        g.camera.age += std::max(0.0f, cameraDt);
        if (g.camera.age >= NUKE_CAMERA_HOLD && g.camera.returnAt < 0) {
            returnNukeCamera();
            g.camera.returnAt = NUKE_CAMERA_HOLD;
        }
        if (g.camera.returnAt >= 0 && g.camera.age - g.camera.returnAt >= NUKE_CAMERA_RETURN) {
            g.camera = {};
            if (battleTime) battleTime->stop();
        } else if (battleTime) battleTime->update(nukeFlightScale());
    }
    const bool wanted = gigaWanted();
    if (wanted != g.enabled)
    {
        g.enabled = wanted;
        if (wanted) { g.pos.y = groundAt(g.pos.x, g.pos.z); g.grounded = true; g.verticalSpeed = 0; g.jump = false; g_active = Active::Giga; g_match.spawnScreen = false;
            if (g_match.active) g_match.playerVehicle = static_cast<int>(g.slot);
            for (auto& v : g_weapons.vehicles) if (v.player && !v.immortal) v.benched = true;
        } else {
            g.camera = {};
            if (battleTime) battleTime->stop();
            for (int vi : {g.carry, g.thrown}) if (vi >= 0) {
                auto& held = g_weapons.vehicles[vi]; held.gigaControlled = false;
                if (ctx.hasComponent<RigidbodyComponent>(held.root)) ctx.getCommandBuffer().removeComponent<RigidbodyComponent>(held.root);
                if (ctx.hasComponent<BoxColliderComponent>(held.root)) ctx.getCommandBuffer().removeComponent<BoxColliderComponent>(held.root);
                for (auto& u : g_ai.units) if (u.slot == static_cast<u32>(vi)) { u.s.pos = held.box.centre; u.s.vel = {}; }
            }
            g.carry = g.thrown = -1; g.nukeAge = -1; g.fire = g.pickup = g.toss = false;
            if (auto* bomb = ctx.tryGetComponent<TransformComponent>(g.nuke)) bomb->position.y = PARK_Y;
            g_active = Active::Tank; if (playerTank()) { auto& tank = g_weapons.vehicles[playerTank()->vehicle]; tank.benched = false; tank.respawnPending = true; tank.respawnPos = g.pos; }
        }
    }
    auto& v = g_weapons.vehicles[g.slot]; v.benched = !g.enabled; v.respawnPending = false;
    if (!g.enabled) { tr->position.y = PARK_Y; return; }
    g_active = Active::Giga;
    if (g_match.active && !g_match.clock.over) { g_match.spawnScreen = false; g_match.playerVehicle = static_cast<int>(g.slot); }
    if (ctx.services().contains<InputSystem>()) {
        auto input = ctx.services().write<InputSystem>();
        const bool capture = input->hasGameInputFocus() && !menuBlocksGame();
        input->setCursorCaptured(capture); input->setCursorVisible(!capture);
    }
    float forward = gigaCinematicActive() ? 0 : g.scriptMove, side = 0; bool run = true;
    if (ctx.services().contains<InputSystem>() && !menuBlocksGame() && !gigaCinematicActive())
    {
        auto input = ctx.services().read<InputSystem>();
        if (input->hasGameInputFocus())
        {
            auto val = [&](const char* n) { return input->getActionValue(InternedString(n)); };
            forward += val("tank_forward") - val("tank_back"); side = val("tank_right") - val("tank_left");
            run = std::abs(g.scriptMove) > 0.01f || input->isActionDown(InternedString("veh_down"));
            g.jump |= input->wasActionPressedThisFrame(InternedString("giga_jump"));
            const auto mouse = input->getMouseState();
            g_tankCtl.camYaw -= mouse.deltaX * g_tankCtl.mouseSensitivity;
            g_tankCtl.camPitch = std::clamp(g_tankCtl.camPitch - mouse.deltaY * g_tankCtl.mouseSensitivity, -0.65f, 0.7f);
        }
    }
    const Vec3 previous = g.pos;
    if (g.jump && g.grounded) { g.verticalSpeed = warzone::giga::JUMP_SPEED; g.grounded = false; }
    g.jump = false;
    const float yaw = g_tankCtl.camYaw;
    const Vec3 f{std::sin(yaw), 0, std::cos(yaw)}, r{-std::cos(yaw), 0, std::sin(yaw)};
    g.aim = normalize(f + Vec3{0, g_tankCtl.camPitch, 0});
    Vec3 move = f * forward + r * side;
    const bool moving = length(move) > 0.01f;
    if (moving) { move = normalize(move); g.pos += move * (run ? warzone::giga::RUN_SPEED : warzone::giga::WALK_SPEED) * dt; g.yaw = std::atan2(move.x, move.z); g.idle = 0; }
    else g.idle += dt;
    const float limit = mapLimit() - 10; g.pos.x = std::clamp(g.pos.x, -limit, limit); g.pos.z = std::clamp(g.pos.z, -limit, limit);
    const float ground = groundAt(g.pos.x, g.pos.z);
    if (!g.grounded) {
        g.pos.y += g.verticalSpeed * dt - 0.5f * warzone::giga::GRAVITY * dt * dt;
        g.verticalSpeed -= warzone::giga::GRAVITY * dt;
        if (g.pos.y <= ground && g.verticalSpeed <= 0) { g.pos.y = ground; g.verticalSpeed = 0; g.grounded = true; }
    } else g.pos.y = ground;
    if (moving && run && g_destruction.ready) {
        const warzone::V3 a = toV3(previous + Vec3{0, 1.05f, 0});
        const warzone::V3 b = toV3(g.pos + Vec3{0, 1.05f, 0});
        const warzone::V3 half{0.5f, 1.05f, 0.5f};
        std::vector<u32> candidates;
        g_destruction.structure.overlapSphere((a + b) * 0.5f, warzone::len(b - a) * 0.5f + 1.3f, candidates);
        for (u32 piece : candidates) {
            const auto& box = g_destruction.structure.node(piece).box;
            const warzone::Box expanded{box.lo - half, box.hi + half};
            float t;
            if (!warzone::segmentBox(expanded, a, b, t)) continue;
            scheduleGigaImpact(piece, toV3(move * warzone::giga::RUN_SPEED + Vec3{0, 8, 0}));
        }
    }
    tr->position = g.pos; tr->rotation = fromAxisAngle({0, 1, 0}, g.yaw);
    v.box.centre = toV3(g.pos + Vec3{0, 1.05f, 0}); v.rotation = tr->rotation;
    g.cooldown = warzone::giga::tick(g.cooldown, dt); g.action = std::max(0.0f, g.action-dt);
    gigaCombat(ctx, dt);
    gigaAnimation(ctx, g.action > 0 ? "chop.001" : moving ? (run ? "run.001" : "walk.001") : (g.idle > 9 ? "dance_03.001" : "laugh_01.001"), moving && run ? 1.8f : 1.0f);
}

bool gigaCamera(SystemContext& ctx, TransformComponent& cam, CameraComponent* lens, f32 dt)
{
    if (!g_giga.enabled) return false;
    const Vec3 target = g_giga.pos + Vec3{0, 1.5f, 0};
    cam.position = target - g_giga.aim * 3.5f + Vec3{0, 1.5f, 0};
    cam.position.y = std::max(cam.position.y, groundAt(cam.position.x, cam.position.z)+0.8f);
    cam.rotation = lookRotation(target + g_giga.aim * 15 - cam.position);
    if (gigaCinematicActive()) {
        const auto& shot = g_giga.camera;
        const auto* rig = ctx.tryGetConstComponent<WarzoneCameraRig>(g_cameraRigEntity);
        const f32 baseFov = g_authoredFov > 0 ? g_authoredFov : toRadians(60.0f);
        const f32 normalFov = 2 * std::atan(std::tan(baseFov * 0.5f) / (rig ? rig->smoothZoom : 1));
        const auto ease = [](f32 t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); };
        if (shot.returnAt >= 0) {
            const f32 t = ease((shot.age - shot.returnAt) / NUKE_CAMERA_RETURN);
            cam.position = shot.from + (cam.position - shot.from) * t;
            cam.rotation = slerp(shot.rotation, cam.rotation, t);
            if (lens) lens->fov = shot.fromFov + (normalFov - shot.fromFov) * t;
        } else {
            const Vec3 focus = g_giga.nukePos + Vec3{0, 0.8f, 0};
            const f32 t = ease(shot.age / 0.18f);
            cam.position = shot.from;
            cam.rotation = slerp(shot.rotation, lookRotation(focus - cam.position), t);
            if (lens) lens->fov = shot.fromFov + (std::min(shot.fromFov, toRadians(28.0f)) - shot.fromFov) * t;
        }
        cam.position = keepAboveGround(cam.position, 1.2f);
    } else addShake(cam, dt);
    return true;
}

void registerGigaSystem(World world)
{
    SystemDescriptor s; s.moduleName = MODULE; s.name = "WarzoneGiga"; s.fn = gigaSystem;
    s.update = SystemPhaseDescriptor{.reads = {getComponentId<RigidbodyStateComponent>(), getComponentId<MeshComponent>()},
        .writes = {getComponentId<TransformComponent>(), getComponentId<AnimationPlayerComponent>(), getComponentId<RenderTintComponent>(), getComponentId<PointLightComponent>(), getComponentId<AudioEmitterComponent>(), getComponentId<FlipbookComponent>()}};
    world.registerSystem(s);
}
void registerGigaCommands(const ModuleContext& context)
{
    auto& c = context.control;
    c.registerCommand("warzoneGiga", MODULE, "GIGA testing controls in Play",
        {optionalArg("nuke", ArgKind::Json, "detonate truth nuke at [x,y,z] in Play"), optionalArg("move", ArgKind::Number, "forward motion -1..1"), optionalArg("jump", ArgKind::Boolean, "jump if grounded"), optionalArg("fire", ArgKind::Boolean, "throw nuke"), optionalArg("pickup", ArgKind::Boolean, "pick nearest enemy"), optionalArg("throw", ArgKind::Boolean, "throw carried vehicle"), optionalArg("position", ArgKind::Json, "[x,y,z]"), optionalArg("aim", ArgKind::Json, "[x,y,z] direction")},
        [](const ControlArgs& a, IFormatWriter&) -> std::string {
            std::scoped_lock lock(g_gigaMutex);
            if (auto n = number(a, "move")) g_giga.scriptMove = std::clamp(*n, -1.0f, 1.0f);
            g_giga.jump |= a.boolean("jump").value_or(false); g_giga.fire |= a.boolean("fire").value_or(false); g_giga.pickup |= a.boolean("pickup").value_or(false); g_giga.toss |= a.boolean("throw").value_or(false);
            Vec3 v; if (vec3Arg(a,"nuke",v)) { g_giga.blastAt = v; g_giga.detonate = true; }
            if (vec3Arg(a,"position",v)) { g_giga.pos = v; g_giga.verticalSpeed = 0; g_giga.grounded = v.y <= groundAt(v.x, v.z); }
            if (vec3Arg(a,"aim",v) && length(v)>0.01f) { g_tankCtl.camYaw=std::atan2(v.x,v.z); g_tankCtl.camPitch=v.y/std::max(0.01f,std::sqrt(v.x*v.x+v.z*v.z)); }
            return {};
        });
    c.registerQuery("warzoneGigaState", MODULE, "GIGA runtime evidence", {}, [&c](const ControlArgs&, IFormatWriter& r) -> std::string {
        std::scoped_lock lock(g_gigaMutex); const auto& g=g_giga;
        r.writeBool("enabled",g.enabled); r.writeF32("cooldown",g.cooldown); r.writeU32("nukes",g.nukes); r.writeU32("throws",g.throws); r.writeI32("carry",g.carry); r.writeI32("thrown",g.thrown);
        r.writeBool("bodyLaunched",g.launchedBody); r.writeBool("grounded",g.grounded); r.writeF32("verticalSpeed",g.verticalSpeed);
        if(g.thrown>=0) { const auto p=g_weapons.vehicles[g.thrown].box.centre; const f32 xyz[]={p.x,p.y,p.z}; r.writeFloats("thrownPosition",xyz); }
        r.writeU32("slot",g.slot); r.writeBool("nukeAirborne",g.nukeAge>=0);
        r.writeBool("nukeCinematic", gigaCinematicActive()); r.writeF32("nukeCinematicAge", g.camera.age);
        r.writeF32("nukeFlightScale", nukeFlightScale());
        if (const auto time = g_nukeBattleTime.lock()) r.writeBool("battleSlowMotion", time->active);
        if(g.slot<g_weapons.vehicles.size()) r.writeF32("hp",g_weapons.vehicles[g.slot].health.hp);
        if(const auto* world=c.world("play")) {
            if(const auto* anim=world->tryGetComponent<AnimationPlayerComponent>(g.skin)) { r.writeString("clip",anim->clip.toView()); r.writeF32("animationTime",anim->time); }
            if(const auto* state=world->tryGetComponent<AnimationStateComponent>(g.skin)) r.writeBool("animationBound",state->binding==AnimationStateComponent::Binding::Bound);
        }
        const f32 p[]={g.pos.x,g.pos.y,g.pos.z}; r.writeFloats("position",p); return {};
    });
}

bool gigaHud(UiCanvasPainter& p, f32 w, f32 h)
{
    if (!g_giga.enabled) return false;
    const HudColors col;
    p.text({18,90},{400,22},"GIGA CHAD",col.amber,UiCanvasAlign::Left,16);
    p.text({18,114},{460,18},"WASD move | Shift run | Space jump | F truth nuke",col.white,UiCanvasAlign::Left,12);
    p.text({18,132},{460,18},"G pickup | T throw | O/F10 settings",col.white,UiCanvasAlign::Left,12);
    p.text({18,152},{460,18},std::format("Truth nuke: {} | Vehicle: {}", g_giga.cooldown>0 ? std::format("{:.1f}s",g_giga.cooldown) : "READY", g_giga.carry>=0 ? "CARRIED" : g_giga.thrown>=0 ? "IN FLIGHT" : "G to grab within 18m"),col.white,UiCanvasAlign::Left,12);
    p.line({w/2-8,h/2},{w/2+8,h/2},2,col.dim);
    p.line({w/2,h/2-8},{w/2,h/2+8},2,col.dim);
    return true;
}
