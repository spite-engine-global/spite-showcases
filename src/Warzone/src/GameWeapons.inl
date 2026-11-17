 
 
 
 
 

constexpr u32 TRACER_POOL = 48;
constexpr u32 SCORCH_POOL = 40, MARK_POOL = 64, PATCH_POOL = 24, CRATER_POOL = 10, SCRAP_POOL = 36;
constexpr f32 SCORCH_LIFE = 240.0f, MARK_LIFE = 90.0f, SOOT_LIFE = 150.0f, CRATER_LIFE = 420.0f, PATCH_LIFE = 310.0f, SCRAP_REST = 45.0f;
constexpr u32 MAX_SHOTS = 640;

 
 
struct VehicleSlot
{
    Entity root = Entity::undefined();
    std::string name;
    std::string weapon = "125 mm HE";
    bool player = false;
    bool immortal = false, gigaControlled = false;
    bool ai = false;  
    Entity breakRoot = Entity::undefined();  
    std::vector<std::pair<Entity, Vec3>> hidden;  
    warzone::Health health;
    warzone::Armor armor;
    warzone::Obb box;
    Vec3 velocity{};
    Quat rotation = Quat::identity();
    std::vector<Entity> meshes;  
    bool wrecked = false;
    f32 burn = 0.0f, nextPuff = 0.0f;
    FxSlot* fire = nullptr;
    f32 ptAcc[3]{};  
    f32 wreckAge = 0.0f;  
    u32 charSeed = 0;
    int patchSlot = -1;
    f32 patchBorn = -1.0f;  
    f32 nextSmoulder = 0.0f;
    u32 kills = 0, deaths = 0, suicides = 0;
    f32 hitTimer = 0.0f;  
    Vec3 hitFrom{};
     
    bool benched = false, respawnPending = false;
    warzone::VehicleType type = warzone::VehicleType::Tank;
    Vec3 homePos{};       
    f32 homeYaw = 0.0f;
    Vec3 respawnPos{};
    f32 respawnYaw = 0.0f, respawnSpeed = 0.0f;
};

struct ShotSlot
{
    warzone::Projectile p;
    int tracer = -1, visual = -1;  
    f32 trail = 0.0f;
    warzone::pt::TrailState ptTrail;  
    u32 targetVehicle = ~0u;
};

struct TracerSlot
{
    Entity e = Entity::undefined();
    bool live = false;
    f32 length = 8.0f, width = 0.35f;
};

struct BombVisual
{
    Entity e = Entity::undefined();
    bool live = false;
    Quat rest = Quat::identity();  
    f32 scale = 1.0f;
    bool playerOnly = false;
};

struct ScrapSlot
{
    Entity e = Entity::undefined();
    warzone::decal::Scrap s;
    bool live = false;
};

struct FeedLine
{
    std::string text;
    f32 age = 0.0f;
};

 
struct FireRequest
{
    warzone::ProjectileKind kind;
    Vec3 from, to;
    std::string target;
    u32 count = 1;
    f32 spread = 0.0f, interval = 0.08f, speed = 0.0f;
};

struct PlayWeapons
{
    bool ready = false;
    std::vector<ShotSlot> shots;
    std::vector<TracerSlot> tracers;
     
    warzone::decal::Pool scorchPool, markPool, patchPool, craterPool;
    std::vector<Entity> scorchE, markE, patchE, emberE, craterE;
    bool decalAtlas = false;  
    u32 decalSeq = 0;
    std::vector<ScrapSlot> scrap;
    u32 nextScrap = 0;
    std::vector<BombVisual> bombVisuals;  
    std::vector<VehicleSlot> vehicles;
    std::vector<FeedLine> feed;
    std::vector<Entity> cannonVoices, mgVoices, impactVoices;
    u32 nextCannon = 0, nextMg = 0, nextImpact = 0;
    std::vector<FireRequest> requests;  
    std::vector<std::pair<f32, FireRequest>> bursts;  
    warzone::Rng rng{777};
     
    u32 fired[5] = {}, hitPiece = 0, hitVehicle = 0, hitGround = 0, airburst = 0, explosions = 0, kills = 0, decalsPlaced = 0;
    f32 lastImpactDistance = 0.0f;
    Vec3 lastImpact{};
    struct ImpactLog
    {
        u8 kind, hit;
        Vec3 pos, from;
    };
    std::vector<ImpactLog> impacts;  
};
PlayWeapons g_weapons;
std::mutex g_weaponsMutex;

 
warzone::UnitGrid g_vehicleGrid;
bool g_vehicleGridValid = false;

const char* const KIND_NAMES[] = {"shell", "bullet", "missile", "bomb", "autocannon"};
constexpr int KIND_COUNT = static_cast<int>(warzone::ProjectileKind::Count);

 
Quat quatFromBasis(const Vec3& x, const Vec3& y, const Vec3& z)
{
    const f32 m00 = x.x, m11 = y.y, m22 = z.z;
    const f32 trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0.0f)
    {
        const f32 s = std::sqrt(trace + 1.0f) * 2.0f;
        q = Quat((y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s);
    }
    else if (m00 > m11 && m00 > m22)
    {
        const f32 s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = Quat(0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s);
    }
    else if (m11 > m22)
    {
        const f32 s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = Quat((y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s);
    }
    else
    {
        const f32 s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = Quat((z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s);
    }
    return normalize(q);
}

Entity makeVoice(World world, const char* clip, const char* name, std::vector<std::pair<Entity, Entity>>& edges, Entity parent, bool loop = false)
{
    const Entity e = world.createEntity();
    TransformComponent t;
    t.position = {0.0f, PARK_Y, 0.0f};
    world.addComponent<TransformComponent>(e, t);
    AudioEmitterComponent a;
    a.clip = InternedString(clip);
    a.playing = false;
    a.loop = loop;
    a.spatial = true;
    a.volume = 1.0f;
    world.addComponent<AudioEmitterComponent>(e, a);
    setEntityName(world, e, name);
    edges.emplace_back(e, parent);
    return e;
}

 
void attachWeapons(const ModuleContext& context, World world)
{
    auto& w = g_weapons;
    {
        std::scoped_lock lock(g_weaponsMutex);
        w = PlayWeapons{};
    }
    const Entity pool = world.createEntity();
    world.addComponent<TransformComponent>(pool);
    setEntityName(world, pool, "Weapons Pool");
    std::vector<std::pair<Entity, Entity>> edges;
    auto& root = context.services;
    if (root.contains<ModelSpawner>())
    {
        const auto& spawner = root.get<ModelSpawner>();
        auto spawnMany = [&](const char* model, u32 count, std::vector<Entity>& out)
        {
            if (!spawner.hasModel(InternedString(model)))
            {
                SWARNING_LOG("Warzone: weapon card {} is not baked", model);
                return;
            }
            std::vector<Entity> parents(count, Entity::undefined());
            auto made = spawner.spawn(world, InternedString(model), parents);
            if (!made)
            {
                SWARNING_LOG("Warzone: spawning {} failed: {}", model, made.error().message);
                return;
            }
            for (Entity e : *made)
            {
                auto& t = world.getComponent<TransformComponent>(e);
                t.position = {0.0f, PARK_Y, 0.0f};
                t.scale = {0.01f, 0.01f, 0.01f};
                edges.emplace_back(e, pool);
                out.push_back(e);
            }
        };
        std::vector<Entity> tracers;
        spawnMany(spawner.hasModel(InternedString("wz_fx2_tracer")) ? "wz_fx2_tracer" : "wz_fx_tracer", TRACER_POOL, tracers);
        for (Entity e : tracers)
            w.tracers.push_back({e});
         
        w.decalAtlas = spawner.hasModel(InternedString("wz_fx3_decal"));
        const char* card = w.decalAtlas ? "wz_fx3_decal" : "wz_fx_scorch";
        spawnMany(card, SCORCH_POOL, w.scorchE);
        if (w.decalAtlas)
        {
            spawnMany(card, MARK_POOL, w.markE);
            spawnMany(card, PATCH_POOL, w.patchE);
            spawnMany(card, PATCH_POOL, w.emberE);
            for (auto* list : {&w.scorchE, &w.markE, &w.patchE, &w.emberE})
                for (Entity e : *list)
                {
                    FlipbookComponent fb;
                    fb.columns = fb.rows = warzone::decal::ATLAS_N;
                    fb.frameCount = warzone::decal::ATLAS_N * warzone::decal::ATLAS_N;
                    fb.framesPerSecond = 1.0f;
                    fb.loop = false;
                    fb.playing = false;
                    fb.cameraFacing = false;
                    if (world.hasComponent<FlipbookComponent>(e))
                        world.getComponent<FlipbookComponent>(e) = fb;
                    else
                        world.addComponent<FlipbookComponent>(e, fb);
                    if (!world.hasComponent<RenderTintComponent>(e))
                        world.addComponent<RenderTintComponent>(e);
                }
            if (spawner.hasModel(InternedString("wz_fx3_crater")))
                spawnMany("wz_fx3_crater", CRATER_POOL, w.craterE);
        }
        w.scorchPool.init(static_cast<u32>(w.scorchE.size()));
        w.markPool.init(static_cast<u32>(w.markE.size()));
        w.patchPool.init(static_cast<u32>(w.patchE.size()));
        w.craterPool.init(static_cast<u32>(w.craterE.size()));
         
        static constexpr const char* SCRAP_MODELS[] = {"wz_p_beam", "wz_p_metal_crate", "wz_p_vent", "wz_p_pipe_rack", "wz_p_drum", "wz_p_slab"};
        for (u32 i = 0; i < SCRAP_POOL; ++i)
        {
            std::vector<Entity> one;
            spawnMany(SCRAP_MODELS[i % std::size(SCRAP_MODELS)], 1, one);
            for (Entity e : one)
            {
                RenderTintComponent tint;
                tint.color = Vec4{0.11f, 0.10f, 0.09f, 1.0f};
                world.addComponent<RenderTintComponent>(e, tint);
                w.scrap.push_back({e});
            }
        }
    }
    for (int i = 0; i < 2; ++i)
        w.cannonVoices.push_back(makeVoice(world, "wz_cannon.wav", "Cannon Voice", edges, pool));
    for (int i = 0; i < 6; ++i)
        w.mgVoices.push_back(makeVoice(world, "wz_mg.wav", "Gun Voice", edges, pool));
    for (int i = 0; i < 3; ++i)
        w.impactVoices.push_back(makeVoice(world, "wz_impact.wav", "Impact Voice", edges, pool));
    attachAll(world, edges);
    w.ready = true;
    SINFO_LOG("Warzone: weapons ready, {} tracers, {} scorch / {} mark / {} patch / {} crater cards, {} scrap", w.tracers.size(), w.scorchE.size(), w.markE.size(), w.patchE.size(),
             w.craterE.size(), w.scrap.size());
}

u32 registerVehicle(VehicleSlot v)
{
    g_weapons.vehicles.push_back(std::move(v));
    return static_cast<u32>(g_weapons.vehicles.size() - 1);
}

int vehicleByName(std::string_view name)
{
    for (u32 i = 0; i < g_weapons.vehicles.size(); ++i)
        if (g_weapons.vehicles[i].name == name)
            return static_cast<int>(i);
    return -1;
}

 
struct Hit
{
    enum Kind : u8
    {
        None,
        Ground,
        Piece,
        Vehicle
    } kind = None;
    f32 t = 2.0f;  
    Vec3 pos{}, normal{0, 1, 0};
    u32 index = 0;
};

 
Hit traceSegment(const Vec3& p0, const Vec3& p1, u32 ignore = ~0u)
{
    Hit best;
    const Vec3 d = p1 - p0;
    const f32 L = length(d);
    if (L < 1e-4f)
        return best;
    if (g_terrain.valid())
    {
        const f32 o[3] = {p0.x, p0.y, p0.z};
        const Vec3 n = d * (1.0f / L);
        const f32 dir[3] = {n.x, n.y, n.z};
        f32 dist;
        if (g_terrain.raycast(o, dir, L, dist))
        {
            best.kind = Hit::Ground;
            best.t = dist / L;
            const Vec3 at = p0 + n * dist;
            f32 gx, gz;
            g_terrain.gradient(at.x, at.z, gx, gz);
            best.normal = normalize(Vec3{-gx, 1.0f, -gz});
        }
    }
    if (g_destruction.ready)
    {
        u32 piece;
        f32 t;
        warzone::V3 n;
        if (g_destruction.structure.raycast(toV3(p0), toV3(p1), piece, t, &n) && t < best.t)
        {
            best.kind = Hit::Piece;
            best.t = t;
            best.index = piece;
            best.normal = toVec(n);
        }
    }
    auto testVehicle = [&](u32 i)
    {
        if (i == ignore || g_weapons.vehicles[i].benched)
            return;
        const auto& v = g_weapons.vehicles[i];
        f32 t;
        warzone::V3 n;
        if (warzone::segmentObb(v.box, toV3(p0), toV3(p1), t, &n) && t < best.t)
        {
            best.kind = Hit::Vehicle;
            best.t = t;
            best.index = i;
            best.normal = toVec(n);
        }
    };
     
    if (g_vehicleGridValid && g_vehicleGrid.pos.size() == g_weapons.vehicles.size())
        g_vehicleGrid.forEachNearSegment(toV3(p0), toV3(p1), 14.0f, [&](int i) { testVehicle(static_cast<u32>(i)); });
    else
        for (u32 i = 0; i < g_weapons.vehicles.size(); ++i)
            testVehicle(i);
    if (best.kind != Hit::None)
        best.pos = p0 + d * best.t;
    return best;
}

 
 
Vec3 groundNormalAt(f32 x, f32 z)
{
    f32 gx, gz;
    g_terrain.gradient(x, z, gx, gz);
    return normalize(Vec3{-gx, 1.0f, -gz});
}

f32 slopeOf(const Vec3& up) { return std::sqrt(std::max(0.0f, 1.0f - up.y * up.y)) / std::max(up.y, 0.2f); }

void setCardAlpha(SystemContext& ctx, Entity e, f32 a)
{
    if (auto* t = ctx.tryGetComponent<RenderTintComponent>(e))
        t->color = Vec4{1.0f, 1.0f, 1.0f, a};
}

void parkCard(SystemContext& ctx, Entity e)
{
    if (auto* t = ctx.tryGetComponent<TransformComponent>(e))
    {
        t->position = {0.0f, PARK_Y, 0.0f};
        t->scale = {0.01f, 0.01f, 0.01f};
    }
}

 
 
int layMark(SystemContext& ctx, warzone::decal::Pool& pool, const std::vector<Entity>& ents, warzone::decal::Kind kind, const Vec3& at, const Vec3& normal, f32 size,
            f32 life, f32 lift, bool mesh = false)
{
    auto& w = g_weapons;
    if (ents.empty())
        return -1;
    const u32 seq = w.decalSeq++;
    const int frame = w.decalAtlas ? warzone::decal::frameFor(kind, seq * 2654435761u + 17u) : 0;
    const int slot = pool.acquire(g_destruction.time, life, size, static_cast<u8>(frame));
    if (slot < 0)
        return -1;
    const Entity e = ents[static_cast<size_t>(slot)];
    auto* t = ctx.tryGetComponent<TransformComponent>(e);
    if (!t)
        return -1;
    const warzone::decal::Basis b = warzone::decal::surfaceBasis(toV3(normal), w.rng.range(0.0f, 6.2832f));
    t->rotation = quatFromBasis(toVec(b.x), toVec(b.y), toVec(b.z));
    float surfaceLift = 0;
    if (normal.y > 0.8f)
        for (const auto& block : g_blocks)
            if (warzone::decal::overlapsSidewalk(at.x, at.z, size * 0.5f, block.x0, block.z0, block.x1, block.z1))
            {
                surfaceLift = 0.20f;
                break;
            }
    t->position = at + Vec3{0, surfaceLift, 0} + toVec(b.z) * lift;
    t->scale = mesh ? Vec3{size, size, size} : Vec3{size, size, 1.0f};
    if (!mesh && w.decalAtlas)
        if (auto* fb = ctx.tryGetComponent<FlipbookComponent>(e))
        {
            fb->elapsedSeconds = static_cast<f32>(frame) + 0.5f;  
            fb->playing = false;
        }
    if (!mesh)
        setCardAlpha(ctx, e, 1.0f);
    ++w.decalsPlaced;
    return slot;
}

 
void placeBlastMarks(SystemContext& ctx, const Vec3& at, f32 blastRadius)
{
    auto& w = g_weapons;
    if (w.scorchE.empty() || !g_terrain.valid())
        return;
    const f32 ground = g_terrain.height(at.x, at.z);
    if (at.y - ground > 1.5f)
        return;  
    const Vec3 up = groundNormalAt(at.x, at.z);
    const Vec3 p{at.x, ground, at.z};
    const f32 slope = slopeOf(up);
    const f32 size = warzone::decal::scorchSize(blastRadius);
    layMark(ctx, w.scorchPool, w.scorchE, warzone::decal::SCORCH, p, up, size, SCORCH_LIFE, warzone::decal::groundLift(size, slope, w.decalSeq));
    const f32 crater = warzone::decal::craterSize(blastRadius);
    if (crater > 0.0f)
        layMark(ctx, w.craterPool, w.craterE, warzone::decal::SCORCH, p, up, crater, CRATER_LIFE, warzone::decal::groundLift(crater, slope, w.decalSeq + 4), true);
}

 
void placeBulletMark(SystemContext& ctx, const Vec3& at, const Vec3& normal)
{
    auto& w = g_weapons;
    if (w.markE.empty() || !warzone::decal::wantsBulletMark(length(at - g_listener), w.rng.f01()))
        return;
    const f32 size = warzone::decal::bulletMarkSize(w.rng.f01());
    layMark(ctx, w.markPool, w.markE, warzone::decal::BULLET, at, normal, size, MARK_LIFE, warzone::decal::groundLift(size, 0.0f, w.decalSeq));
}

 
void placeSoot(SystemContext& ctx, const Vec3& at, const Vec3& normal, f32 blastRadius)
{
    auto& w = g_weapons;
    if (w.markE.empty())
        return;
    if (blastRadius < 0.5f && !warzone::decal::wantsBulletMark(length(at - g_listener), w.rng.f01()))
        return;
    const f32 size = warzone::decal::sootSize(blastRadius, w.rng.f01());
    layMark(ctx, w.markPool, w.markE, warzone::decal::SOOT, at, normal, size, SOOT_LIFE, 0.03f + 0.004f * static_cast<f32>(w.decalSeq % 8u));
}

 
void placeWreckPatch(SystemContext& ctx, VehicleSlot& v)
{
    auto& w = g_weapons;
    if (w.patchE.empty() || !g_terrain.valid())
        return;
    const Vec3 c = toVec(v.box.centre);
    if (tierAt(c, 6.0f) > warzone::vfx::FAR)
        return;
    const f32 size = warzone::decal::wreckPatchSize(v.box.half.x, v.box.half.z);
    const f32 ground = g_terrain.height(c.x, c.z);
    const Vec3 up = groundNormalAt(c.x, c.z);
    const Vec3 p{c.x, ground, c.z};
    const f32 lift = warzone::decal::groundLift(size, slopeOf(up), w.decalSeq) + 0.03f;
    const int slot = layMark(ctx, w.patchPool, w.patchE, warzone::decal::PATCH, p, up, size, PATCH_LIFE, lift);
    if (slot < 0)
        return;
    const size_t si = static_cast<size_t>(slot);
    v.patchSlot = slot;
    v.patchBorn = w.patchPool.slots[si].born;
     
    if (auto* pt = ctx.tryGetComponent<TransformComponent>(w.patchE[si]))
        if (auto* et = ctx.tryGetComponent<TransformComponent>(w.emberE[si]))
        {
            *et = *pt;
            et->position = pt->position + up * 0.02f;
        }
    if (auto* fb = ctx.tryGetComponent<FlipbookComponent>(w.emberE[si]))
    {
        const int variant = (w.patchPool.slots[si].frame - warzone::decal::FIRST[warzone::decal::PATCH]) % warzone::decal::COUNT[warzone::decal::EMBER];
        fb->elapsedSeconds = static_cast<f32>(warzone::decal::FIRST[warzone::decal::EMBER] + variant) + 0.5f;
        fb->playing = false;
    }
    setCardAlpha(ctx, w.emberE[si], 0.0f);
}

 
void throwScrap(const VehicleSlot& v, u32 vi)
{
    auto& w = g_weapons;
    if (w.scrap.empty() || !g_terrain.valid())
        return;
    const Vec3 c = toVec(v.box.centre);
    const auto tier = tierAt(c, 5.0f);
    if (tier > warzone::vfx::MID)
        return;
    const f32 size = std::clamp(warzone::len(v.box.half) / 4.0f, 0.7f, 1.4f);
    for (auto s : warzone::decal::scrapPlan(vi * 31u + v.deaths, toV3(c), size, tier == warzone::vfx::NEAR ? 6 : 3))
    {
        ScrapSlot& slot = w.scrap[w.nextScrap++ % w.scrap.size()];
        s.scale *= 0.5f;
        slot.s = s;
        slot.live = true;
    }
}

void updateScrap(SystemContext& ctx, f32 dt)
{
    for (auto& slot : g_weapons.scrap)
    {
        if (!slot.live)
            continue;
        auto* t = ctx.tryGetComponent<TransformComponent>(slot.e);
        if (!t)
            continue;
        auto& s = slot.s;
        const f32 ground = g_terrain.valid() ? g_terrain.height(s.pos.x, s.pos.z) : 0.0f;
        warzone::decal::stepScrap(s, dt, ground);
        const f32 sink = s.landed ? std::clamp((s.age - SCRAP_REST) / 3.0f, 0.0f, 1.0f) : 0.0f;  
        if (sink >= 1.0f)
        {
            slot.live = false;
            parkCard(ctx, slot.e);
            continue;
        }
        t->position = toVec(s.pos);
        t->rotation = normalize(multiply(fromAxisAngle({0, 1, 0}, s.yaw), fromAxisAngle({1, 0, 0}, s.roll + 0.35f)));
        const f32 k = s.scale * (1.0f - sink);
        t->scale = {k, k, k};
    }
}

 
void updateDecals(SystemContext& ctx)
{
    auto& w = g_weapons;
    const f32 now = g_destruction.time;
    auto run = [&](warzone::decal::Pool& pool, const std::vector<Entity>& ents, bool tinted)
    {
        for (size_t i = 0; i < pool.slots.size() && i < ents.size(); ++i)
        {
            if (!pool.slots[i].used)
                continue;
            const f32 a = pool.alpha(static_cast<int>(i), now);
            if (a <= 0.0f)
            {
                pool.slots[i].used = false;
                parkCard(ctx, ents[i]);
                if (&pool == &w.patchPool && i < w.emberE.size())
                    parkCard(ctx, w.emberE[i]);
                continue;
            }
            if (a < 1.0f && tinted)
                setCardAlpha(ctx, ents[i], a);
        }
    };
    run(w.scorchPool, w.scorchE, w.decalAtlas);
    run(w.markPool, w.markE, true);
    run(w.patchPool, w.patchE, true);
    run(w.craterPool, w.craterE, false);  
}

void sparks(SystemContext& ctx, const Vec3& at, const Vec3& normal, f32 size)
{
    if (particlesOn())
    {
        warzone::pt::impact(g_pt.sys, toV3(at), toV3(normal), warzone::pt::S_ARMOUR, size, tierAt(at, 1.0f), PT_DUST);
        return;
    }
    spawnFx(ctx, FX_SPARK, at + normal * 0.3f, size, size * 1.3f, 0.45f, normal * 1.5f);
}

void explode(SystemContext& ctx, const Vec3& at, const warzone::ProjectileSpec& spec, u32 owner, int directVehicle, bool damagePieces = true, bool effects = true);
void onVehicleKilled(u32 vi, u32 attacker);  
void queueBreakup(u32 vi);                    

void wreckVehicle(SystemContext& ctx, u32 vi, u32 attacker)
{
    auto& w = g_weapons;
    auto& v = w.vehicles[vi];
    if (v.immortal || v.wrecked) return;
    v.wrecked = true;
    v.burn = 70.0f;
    ++v.deaths;
    const Vec3 c = toVec(v.box.centre);
    if (attacker < w.vehicles.size() && attacker != vi)
        ++w.vehicles[attacker].kills;
    else
        ++v.suicides;  
    onVehicleKilled(vi, attacker);
    queueBreakup(vi);
    ++w.kills;
     
    warzone::ProjectileSpec cookoff = warzone::specOf(warzone::ProjectileKind::Shell);
    cookoff.blastRadius = 7.0f;
    cookoff.blastDamage = 600.0f;
    cookoff.push = 16.0f;
    explode(ctx, c + Vec3{0, 1.0f, 0}, cookoff, vi, -1);
    if (tierAt(c, 7.0f) <= warzone::vfx::MID)  
        spawnFx(ctx, FX_FIRE, c + Vec3{0, 3.0f, 0}, 9.0f, 16.0f, 1.8f, {0, 3.0f, 0});
    v.fire = particlesOn() ? nullptr : spawnFx(ctx, FX_FLAME, c + Vec3{0, 1.8f, 0}, 6.0f, 6.0f, v.burn, {0, 0, 0}, true);
    v.ptAcc[0] = v.ptAcc[1] = v.ptAcc[2] = 0.0f;
     
     
    v.wreckAge = 0.0f;
    v.charSeed = vi * 977u + v.deaths * 131u;
    v.nextSmoulder = 0.0f;
    auto& cb = ctx.getCommandBuffer();
    u32 meshIndex = 0;
    for (Entity m : v.meshes)
    {
        const u32 mi = meshIndex++;
        if (!tracked(ctx, m))
            continue;
        if (auto* existing = ctx.tryGetComponent<RenderTintComponent>(m))
            existing->color = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
        else
        {
            const warzone::decal::Rgb ch = warzone::decal::charTint(v.charSeed, mi);
            RenderTintComponent tint;
            tint.color = Vec4{ch.r, ch.g, ch.b, 1.0f};
            cb.addComponent<RenderTintComponent>(m, std::move(tint));
        }
    }
    placeWreckPatch(ctx, v);
    throwScrap(v, vi);
    const std::string by = attacker < w.vehicles.size() && attacker != vi ? w.vehicles[attacker].name : std::string("the town");
    const std::string weapon = attacker < w.vehicles.size() ? w.vehicles[attacker].weapon : std::string("blast");
    if (attacker == vi)
        w.feed.push_back({std::format("{}  [crashed]", v.name)});  
    else
        w.feed.push_back({std::format("{}  [{}]  {}", by, weapon, v.name)});
    if (w.feed.size() > 6)
        w.feed.erase(w.feed.begin());
    SINFO_LOG("Warzone: {} destroyed by {}", v.name, by);
}

 
void reviveVehicle(SystemContext& ctx, u32 vi)
{
    auto& v = g_weapons.vehicles[vi];
    v.health.hp = v.health.maxHp;
    v.health.dead = false;
    v.wrecked = false;
    v.burn = 0.0f;
    v.hitTimer = 0.0f;
    if (v.patchSlot >= 0 && static_cast<size_t>(v.patchSlot) < g_weapons.emberE.size())
        setCardAlpha(ctx, g_weapons.emberE[static_cast<size_t>(v.patchSlot)], 0.0f);
    v.patchSlot = -1;
    if (v.fire && v.fire->loop)
        v.fire->life = std::max(0.0f, v.fire->age);  
    v.fire = nullptr;
    for (auto& [e, s] : v.hidden)  
        if (auto* t = ctx.tryGetComponent<TransformComponent>(e))
            t->scale = s;
    v.hidden.clear();
    for (Entity m : v.meshes)
        if (auto* tint = ctx.tryGetComponent<RenderTintComponent>(m))
            tint->color = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
}

void damageVehicle(SystemContext& ctx, u32 vi, f32 amount, const Vec3& from, u32 attacker)
{
    auto& v = g_weapons.vehicles[vi];
    if (v.wrecked || v.immortal)
        return;
    v.hitTimer = 2.5f;
    v.hitFrom = from;
    if (v.health.apply(warzone::giga::damage(v.immortal, amount)))
        wreckVehicle(ctx, vi, attacker);
}

void explode(SystemContext& ctx, const Vec3& at, const warzone::ProjectileSpec& spec, u32 owner, int directVehicle, bool damagePieces, bool effects)
{
    warzone::pt::PlayerEffects playerFx(g_pt.sys, owner < g_weapons.vehicles.size() && g_weapons.vehicles[owner].player);
    auto& w = g_weapons;
    ++w.explosions;
    const f32 r = spec.blastRadius;
    if (damagePieces && g_destruction.ready)
    {
        warzone::Blast b;
        b.centre = toV3(at);
        b.radius = r;
        b.damage = spec.blastDamage;
        b.push = spec.push;
        scheduleBlast(b, 0, false);
    }
     
    auto splashOn = [&](u32 i)
    {
        if (static_cast<int>(i) == directVehicle || w.vehicles[i].wrecked || w.vehicles[i].benched)
            return;
        const f32 d = warzone::distance(w.vehicles[i].box, toV3(at));
        const f32 dmg = warzone::splash(spec.blastDamage * 0.35f, r, d) * w.vehicles[i].armor.top;
        if (dmg > 0.0f)
            damageVehicle(ctx, i, dmg, at, owner);
    };
    if (g_vehicleGridValid && g_vehicleGrid.pos.size() == w.vehicles.size())
    {
        const f32 reach = r + 14.0f;
        std::vector<int> hit;
        g_vehicleGrid.forEachIn(at.x - reach, at.z - reach, at.x + reach, at.z + reach, [&](int i) { hit.push_back(i); });
        for (int i : hit)  
            splashOn(static_cast<u32>(i));
    }
    else
        for (u32 i = 0; i < w.vehicles.size(); ++i)
            splashOn(i);
    if (!effects) return;
    auto& rng = w.rng;
    if (r < 2.0f)
    {
         
        if (particlesOn())
            warzone::pt::smallBlast(g_pt.sys, toV3(at), r, tierAt(at, r), PT_DUST);
        else
            spawnPlan(ctx, warzone::vfx::smallBlast(r, length(at - g_listener), tierAt(at, r), rng), at);
        if (rng.chance(0.4f))
            flash(ctx, at, 1800.0f, 0.12f);
        playVoice(w.impactVoices, w.nextImpact, at, 45.0f, 0.8f, wa::Category::Impacts);
        g_shake = std::min(1.0f, g_shake + warzone::shakeAt(0.12f, r, length(at - g_listener)));
        return;
    }
     
    explosionEffects(ctx, at, r);
    playVoice(g_destruction.blastVoices, g_destruction.nextBlastVoice, at, 90.0f, 1, wa::Category::Explosions, spec.blastRadius);
    g_shake = std::min(1.0f, g_shake + warzone::shakeAt(std::clamp(r / 4.5f, 0.3f, 1.5f), r, length(at - g_listener)));
    placeBlastMarks(ctx, at, r);
}

 
 
bool launch(warzone::ProjectileKind kind, const Vec3& from, const Vec3& velocity, u32 owner, u32 targetVehicle = ~0u,
            const Vec3& targetPoint = {})
{
    auto& w = g_weapons;
    if (w.shots.size() >= MAX_SHOTS)
        return false;
    const bool player = owner < w.vehicles.size() && w.vehicles[owner].player;
    warzone::pt::PlayerEffects playerFx(g_pt.sys, player);
    ShotSlot s;
    s.p.kind = kind;
    s.p.pos = toV3(from);
    s.p.vel = toV3(velocity) * (kind == warzone::ProjectileKind::Bomb ? warzone::BOMB_SPEED_SCALE : 1.0f);
    s.p.owner = owner;
    s.targetVehicle = targetVehicle;
    s.p.guided = kind == warzone::ProjectileKind::Missile || kind == warzone::ProjectileKind::Bomb;
    s.p.target = toV3(targetVehicle < w.vehicles.size() ? toVec(w.vehicles[targetVehicle].box.centre) : targetPoint);
    if (kind == warzone::ProjectileKind::Bomb)
        for (u32 i = 0; i < w.bombVisuals.size(); ++i)
            if (!w.bombVisuals[i].live && (player || !w.bombVisuals[i].playerOnly))
            {
                w.bombVisuals[i].live = true;
                s.visual = static_cast<int>(i);
                break;
            }
    if (kind != warzone::ProjectileKind::Bomb)
        for (u32 i = 0; i < w.tracers.size(); ++i)
            if (!w.tracers[i].live)
            {
                w.tracers[i].live = true;
                const bool bullet = kind == warzone::ProjectileKind::Bullet, cannon30 = kind == warzone::ProjectileKind::Autocannon;
                w.tracers[i].length = bullet ? 18.0f : cannon30 ? 13.0f : kind == warzone::ProjectileKind::Shell ? 9.0f : 3.0f;
                w.tracers[i].width = bullet ? 0.45f : cannon30 ? 0.55f : 0.7f;
                s.tracer = static_cast<int>(i);
                break;
            }
    w.shots.push_back(s);
    if (kind == warzone::ProjectileKind::Bomb && particlesOn())
        warzone::pt::bombRelease(g_pt.sys, toV3(from), warzone::norm(s.p.vel));
    ++w.fired[static_cast<int>(kind)];
    return true;
}

 
void muzzleEffects(SystemContext& ctx, const Vec3& muzzle, const Vec3& dir, bool cannon)
{
    auto& rng = g_weapons.rng;
    const f32 dist = length(muzzle - g_listener);
    const warzone::vfx::Muzzle m = warzone::vfx::muzzle(cannon ? 2 : 0, dist, tierAt(muzzle, cannon ? 3.0f : 0.8f));
    if (particlesOn())
    {
        const f32 agl = g_terrain.valid() ? muzzle.y - g_terrain.height(muzzle.x, muzzle.z) : 10.0f;
        warzone::pt::muzzle(g_pt.sys, toV3(muzzle), toV3(dir), cannon ? 2 : 0, agl, tierAt(muzzle, cannon ? 3.0f : 0.8f), PT_DUST,
                            static_cast<uint32_t>(rng.irange(0, 5)));
        if (cannon && m.lightPeak > 0.0f)
            flash(ctx, muzzle + dir * 1.5f, m.lightPeak, m.lightLife);
        if (cannon)
        {
            playVoice(g_weapons.cannonVoices, g_weapons.nextCannon, muzzle, 120.0f);
            g_shake = std::min(1.0f, g_shake + warzone::shakeAt(0.45f, 3.0f, length(muzzle - g_listener)));
        }
        else
            playVoice(g_weapons.mgVoices, g_weapons.nextMg, muzzle, 50.0f, 0.8f);
        return;
    }
    if (cannon)
    {
        spawnFx(ctx, FX_FLASH, muzzle + dir * 1.2f, m.flashS0, m.flashS1, m.flashLife, dir * 4.0f);
        if (m.shock)
            spawnFx(ctx, FX_SHOCK, muzzle + dir * 1.6f, 1.2f, 6.5f, 0.3f, dir * 2.0f);
        if (m.lightPeak > 0.0f)
            flash(ctx, muzzle + dir * 1.5f, m.lightPeak, m.lightLife);
         
        for (int i = 0; i < m.smokePuffs; ++i)
            tintFx(ctx, spawnFx(ctx, FX_PUFF, muzzle + dir * (1.0f + i * 1.8f), 1.4f + i * 0.5f, 5.5f + i, 2.8f,
                                dir * (7.0f - i * 1.8f) + Vec3{rng.range(-0.5f, 0.5f), 0.5f, rng.range(-0.5f, 0.5f)}),
                   {0.95f, 0.93f, 0.9f, 0.9f});
         
        if (g_terrain.valid() && m.groundDust > 0)
        {
            const f32 ground = g_terrain.height(muzzle.x, muzzle.z);
            if (muzzle.y - ground < 4.0f)
                for (int i = 0; i < m.groundDust; ++i)
                {
                    const f32 a = rng.range(0.0f, 6.283f);
                    const Vec3 side{std::cos(a), 0.0f, std::sin(a)};
                    const Vec3 at = Vec3{muzzle.x, ground + 0.6f, muzzle.z} + dir * 2.0f + side * rng.range(0.5f, 3.0f);
                    spawnFx(ctx, FX_DUST, at, 2.5f, 9.0f, 3.5f, side * rng.range(2.0f, 5.0f) + dir * 3.0f + Vec3{0, 0.5f, 0});
                }
        }
        playVoice(g_weapons.cannonVoices, g_weapons.nextCannon, muzzle, 120.0f);
        g_shake = std::min(1.0f, g_shake + warzone::shakeAt(0.45f, 3.0f, length(muzzle - g_listener)));
    }
    else
    {
        spawnFx(ctx, FX_FLASH, muzzle + dir * 0.4f, m.flashS0, m.flashS1, m.flashLife, dir * 2.0f);
        playVoice(g_weapons.mgVoices, g_weapons.nextMg, muzzle, 50.0f, 0.8f);
    }
}

 
void autocannonEffects(SystemContext& ctx, const Vec3& muzzle, const Vec3& dir, u32 round)
{
    auto& rng = g_weapons.rng;
    const warzone::vfx::Muzzle m = warzone::vfx::muzzle(1, length(muzzle - g_listener), tierAt(muzzle, 1.2f));
    if (particlesOn())
    {
        const f32 agl = g_terrain.valid() ? muzzle.y - g_terrain.height(muzzle.x, muzzle.z) : 10.0f;
        warzone::pt::muzzle(g_pt.sys, toV3(muzzle), toV3(dir), 1, agl, tierAt(muzzle, 1.2f), PT_DUST, round);
        if (round % 3 == 0 && m.lightPeak > 0.0f)
            flash(ctx, muzzle + dir * 1.0f, m.lightPeak, m.lightLife);
    }
    else
        spawnFx(ctx, FX_FLASH, muzzle + dir * 0.7f, m.flashS0, m.flashS1, m.flashLife, dir * 3.0f);
    if (round % 3 == 0 && !particlesOn())
    {
        if (m.smokePuffs > 0)
            tintFx(ctx, spawnFx(ctx, FX_PUFF, muzzle + dir * 1.2f, 0.6f, 2.6f, 1.6f, dir * 3.5f + Vec3{rng.range(-0.3f, 0.3f), 0.5f, rng.range(-0.3f, 0.3f)}),
                   {0.95f, 0.93f, 0.9f, 0.85f});
        if (m.lightPeak > 0.0f)
            flash(ctx, muzzle + dir * 1.0f, m.lightPeak, m.lightLife);
    }
    playVoice(g_weapons.mgVoices, g_weapons.nextMg, muzzle, 70.0f, 1.0f);
    g_shake = std::min(1.0f, g_shake + warzone::shakeAt(0.05f, 1.0f, length(muzzle - g_listener)));
}

 
 
 
 
void wreckSmokeHook(SystemContext& ctx, const Vec3& pos, f32 s0, f32 s1, f32 life, f32 rise, f32 windK, const warzone::vfx::Tint& tint, bool smouldering)
{
    if (particlesOn())
    {
        warzone::pt::Spawn p;
        p.style = smouldering ? warzone::pt::S_WISP : warzone::pt::S_PLUME, p.pos = toV3(pos), p.posR = s0 * 0.3f;
        p.vel = {0.0f, rise, 0.0f}, p.velR = 0.3f, p.drag = 0.6f, p.rise = rise, p.windK = windK;
        p.life = life * 1.6f, p.lifeJ = 0.25f, p.s0 = s0, p.s1 = s1 * 1.6f, p.sizeJ = 0.25f, p.spin = 0.2f, p.prio = 0;
        p.tint = warzone::pt::tintOf({tint.r, tint.g, tint.b}, tint.a);
        g_pt.sys.burst(p, smouldering ? 2 : 1);
        return;
    }
    FxSlot* puff = spawnFx(ctx, FX_PUFF, pos, s0, s1, life, {g_weapons.rng.range(-0.4f, 0.4f), rise, g_weapons.rng.range(-0.4f, 0.4f)});
    if (puff)
    {
        puff->rise = rise;
        puff->windK = windK;
        tintFx(ctx, puff, tint);
    }
}

 
void impact(SystemContext& ctx, ShotSlot& s, const Hit& hit)
{
    auto& w = g_weapons;
    const auto& spec = warzone::specOf(s.p.kind);
    const Vec3 dir = normalize(toVec(s.p.vel));
    w.lastImpact = hit.pos;
    w.impacts.push_back({static_cast<u8>(s.p.kind), static_cast<u8>(hit.kind), hit.pos, toVec(s.p.pos)});
    if (w.impacts.size() > 16)
        w.impacts.erase(w.impacts.begin());
    w.lastImpactDistance = length(hit.pos - g_listener);
    int direct = -1;
    switch (hit.kind)
    {
    case Hit::Ground:
        ++w.hitGround;
        if (spec.blastRadius < warzone::decal::CRATER_MIN_RADIUS)  
            placeBulletMark(ctx, hit.pos + hit.normal * 0.02f, hit.normal);
        break;
    case Hit::Piece:
        ++w.hitPiece;
        placeSoot(ctx, hit.pos, hit.normal, spec.blastRadius);  
        break;
    case Hit::Vehicle:
    {
        ++w.hitVehicle;
        direct = static_cast<int>(hit.index);
        auto& v = w.vehicles[hit.index];
        const Vec3 local = rotate(conjugate(v.rotation), dir);
        const f32 dmg = spec.direct * warzone::armorFactor(v.armor, toV3(local));
        damageVehicle(ctx, hit.index, dmg, hit.pos - dir * 30.0f, s.p.owner);
        playVoice(w.impactVoices, w.nextImpact, hit.pos, 50.0f, 1, wa::Category::Impacts);
        sparks(ctx, hit.pos, hit.normal, s.p.kind == warzone::ProjectileKind::Bullet ? 1.4f : 3.5f);
        break;
    }
    default:
        break;
    }
    if (spec.blastRadius > 0.0f)
    {
        explode(ctx, hit.pos + hit.normal * 0.3f, spec, s.p.owner, direct);
        return;
    }
     
    if (hit.kind == Hit::Piece && g_destruction.ready)
    {
        warzone::Blast b;
        b.centre = toV3(hit.pos);
        b.radius = 0.4f;
        b.damage = spec.direct;
        b.push = 2.0f;
        scheduleBlast(b, 0, false);
    }
    if (hit.kind != Hit::Vehicle && particlesOn())
        warzone::pt::impact(g_pt.sys, toV3(hit.pos), toV3(hit.normal), hit.kind == Hit::Piece ? warzone::pt::S_WALL : warzone::pt::S_GROUND_DIRT, 1.0f,
                            tierAt(hit.pos, 1.0f), hit.kind == Hit::Piece ? PT_WALL : PT_DUST);
    else if (hit.kind != Hit::Vehicle)
    {
        const warzone::vfx::Impact im = warzone::vfx::impact(hit.kind == Hit::Piece ? warzone::vfx::PIECE : warzone::vfx::GROUND, 1.0f,
                                                            length(hit.pos - g_listener), tierAt(hit.pos, 1.0f), w.rng);
        if (im.dust)
            spawnFx(ctx, FX_DUST, hit.pos + hit.normal * 0.3f, im.dustS0, im.dustS1, im.dustLife, hit.normal * 1.2f);
        if (im.sparks)
            sparks(ctx, hit.pos, hit.normal, 0.9f * im.sparkSize);
        if (im.debris)  
            spawnFx(ctx, FX_DEBRIS, hit.pos + hit.normal * 0.4f, im.debrisSize, im.debrisSize * 3.5f, 0.9f, hit.normal * 1.0f);
    }
}

void updateTracer(SystemContext& ctx, TracerSlot& tr, const Vec3& head, const Vec3& dir)
{
    auto* t = ctx.tryGetComponent<TransformComponent>(tr.e);
    if (!t)
        return;
     
    Vec3 toCam = g_listener - head;
    Vec3 z = toCam - dir * dot(toCam, dir);
    z = length(z) > 1e-3f ? normalize(z) : Vec3{0, 1, 0};
    const Vec3 y = cross(z, dir);
    t->rotation = quatFromBasis(dir, y, z);
    f32 length_ = tr.length, width = tr.width;  
    warzone::vfx::tracerShape(length(g_listener - head), tr.length, tr.width, length_, width);
    t->position = head - dir * (length_ * 0.5f);
    t->scale = {length_, width, 1.0f};
}

void parkTracer(SystemContext& ctx, int index)
{
    if (index < 0)
        return;
    auto& tr = g_weapons.tracers[index];
    tr.live = false;
    if (auto* t = ctx.tryGetComponent<TransformComponent>(tr.e))
    {
        t->position = {0.0f, PARK_Y, 0.0f};
        t->scale = {0.01f, 0.01f, 0.01f};
    }
}

void weaponsSystem(SystemContext& ctx)
{
    auto& w = g_weapons;
    if (!w.ready)
        return;
    const f32 dt = std::min(ctx.deltaTime, 0.05f);

     
    {
        std::scoped_lock lock(g_weaponsMutex);
        for (auto& r : w.requests)
            for (u32 i = 0; i < r.count; ++i)
                w.bursts.push_back({g_destruction.time + i * r.interval, r});
        w.requests.clear();
    }
    for (size_t i = 0; i < w.bursts.size();)
    {
        if (w.bursts[i].first > g_destruction.time)
        {
            ++i;
            continue;
        }
        const FireRequest r = w.bursts[i].second;
        w.bursts.erase(w.bursts.begin() + static_cast<std::ptrdiff_t>(i));
        const auto& spec = warzone::specOf(r.kind);
        int target = r.target.empty() ? -1 : vehicleByName(r.target);
        Vec3 to = target >= 0 ? toVec(w.vehicles[target].box.centre) : r.to;
        to = to + Vec3{w.rng.range(-r.spread, r.spread), w.rng.range(-r.spread, r.spread), w.rng.range(-r.spread, r.spread)};
        Vec3 dir = normalize(to - r.from);
        f32 speed = r.speed > 0.0f ? r.speed : spec.speed;
        if (r.kind == warzone::ProjectileKind::Shell || r.kind == warzone::ProjectileKind::Bullet)
        {
            const Vec3 flat{to.x - r.from.x, 0.0f, to.z - r.from.z};
            f32 el;
            if (warzone::solveElevation(speed, warzone::GRAVITY, length(flat), to.y - r.from.y, el))
                dir = normalize(normalize(flat) * std::cos(el) + Vec3{0, std::sin(el), 0});
        }
        if (r.kind == warzone::ProjectileKind::Bomb && r.speed <= 0.0f)
            speed = 180.0f;
        if (launch(r.kind, r.from, dir * speed, ~0u, target >= 0 ? static_cast<u32>(target) : ~0u, to))
            muzzleEffects(ctx, r.from, dir, r.kind == warzone::ProjectileKind::Shell);
    }

     
    for (auto& s : w.shots)
    {
        warzone::pt::PlayerEffects playerFx(g_pt.sys, s.p.owner < w.vehicles.size() && w.vehicles[s.p.owner].player);
        if (s.targetVehicle < w.vehicles.size() && !w.vehicles[s.targetVehicle].wrecked)
            s.p.target = w.vehicles[s.targetVehicle].box.centre;
        const Vec3 before = toVec(s.p.pos);
        warzone::stepProjectile(s.p, dt);
        const Vec3 after = toVec(s.p.pos);
        const Hit hit = traceSegment(before, after, s.p.owner);
        if (hit.kind != Hit::None)
        {
            impact(ctx, s, hit);
            s.p.alive = false;
        }
        else if (s.p.age >= warzone::specOf(s.p.kind).life || after.y < -100.0f)
        {
            if (warzone::specOf(s.p.kind).blastRadius > 0.0f)
            {
                ++w.airburst;
                explode(ctx, after, warzone::specOf(s.p.kind), s.p.owner, -1);
            }
            s.p.alive = false;
        }
        if (!s.p.alive)
        {
            parkTracer(ctx, s.tracer);
            s.tracer = -1;
            if (s.visual >= 0)
            {
                auto& bv = w.bombVisuals[s.visual];
                bv.live = false;
                if (auto* t = ctx.tryGetComponent<TransformComponent>(bv.e))
                {
                    t->position = {0.0f, PARK_Y, 0.0f};
                    t->scale = {0.01f, 0.01f, 0.01f};
                }
                s.visual = -1;
            }
            continue;
        }
        if ((s.tracer >= 0 || g_pt.sys.playerEffects) && s.p.kind != warzone::ProjectileKind::Bomb && particlesOn())
        {
             
            const bool bullet = s.p.kind == warzone::ProjectileKind::Bullet;
            const bool cannon30 = s.p.kind == warzone::ProjectileKind::Autocannon;
            const f32 baseLength = s.tracer >= 0 ? w.tracers[s.tracer].length : bullet ? 18 : cannon30 ? 13 : 9;
            const f32 baseWidth = s.tracer >= 0 ? w.tracers[s.tracer].width : bullet ? 0.45f : cannon30 ? 0.55f : 0.7f;
            f32 length_ = baseLength, width = baseWidth;
            warzone::vfx::tracerShape(length(g_listener - after), baseLength, baseWidth, length_, width);
            warzone::pt::tracer(g_pt.sys, toV3(after), toV3(before), toV3(normalize(toVec(s.p.vel))), length_, width, 0.12f, dt, tierAt(after, 1.0f));
        }
        else if (s.tracer >= 0)
            updateTracer(ctx, w.tracers[s.tracer], after, normalize(toVec(s.p.vel)));
        if (s.visual >= 0)
            if (auto* t = ctx.tryGetComponent<TransformComponent>(w.bombVisuals[s.visual].e))
            {
                 
                const Vec3 z = normalize(toVec(s.p.vel));
                const Vec3 x0 = std::abs(z.y) < 0.98f ? cross(Vec3{0, 1, 0}, z) : Vec3{1, 0, 0};
                const Vec3 x = normalize(x0), y = cross(z, x);
                t->rotation = normalize(multiply(quatFromBasis(x, y, z), w.bombVisuals[s.visual].rest));
                t->position = after;
                const f32 sc = w.bombVisuals[s.visual].scale;
                t->scale = {sc, sc, sc};
            }
         
        s.trail -= dt;
        const bool missile = s.p.kind == warzone::ProjectileKind::Missile;
        const bool bomb = s.p.kind == warzone::ProjectileKind::Bomb;
        if (particlesOn())
        {
            if (bomb)
                warzone::pt::bombFlight(g_pt.sys, s.ptTrail, toV3(before), toV3(after), warzone::norm(s.p.vel), dt, tierAt(after, 2));
             
            if (missile || (s.p.kind == warzone::ProjectileKind::Shell && s.p.age < 0.6f))
            {
                const warzone::vfx::Tier tier = tierAt(after, 2.0f);
                warzone::pt::trail(g_pt.sys, s.ptTrail, toV3(before), toV3(after), warzone::pt::trailSpacing(missile, tier), warzone::pt::trailPuff(missile, tier));
                if (missile)
                {
                    warzone::pt::Spawn g;
                    g.style = warzone::pt::S_GLOW, g.pos = toV3(after - normalize(toVec(s.p.vel)) * 1.2f), g.flags = warzone::pt::P_STATIC;
                    g.life = dt * 1.5f, g.s0 = g.s1 = 1.6f * warzone::pt::tierSize(tier), g.heat = 1.2f, g.prio = 0;
                    g_pt.sys.spawn(g);
                }
            }
        }
        else if (s.trail <= 0.0f && (missile || bomb || (s.p.kind == warzone::ProjectileKind::Shell && s.p.age < 0.6f)))
        {
            if (bomb) spawnFx(ctx, FX_FIRE, after - normalize(toVec(s.p.vel)) * 1.5f, 1.2f, 2.5f, 0.12f, {});
            const f32 boost = warzone::vfx::distanceBoost(length(after - g_listener));
            s.trail = warzone::vfx::trailInterval(missile, tierAt(after, 2.0f));
            tintFx(ctx, spawnFx(ctx, FX_PUFF, after, (missile ? 1.1f : 0.5f) * boost, (missile ? 3.6f : 2.0f) * boost, missile ? 2.6f : 1.4f, {0, 0.4f, 0}),
                   {0.88f, 0.88f, 0.86f, 1.0f});
        }
    }
    std::erase_if(w.shots, [](const ShotSlot& s) { return !s.p.alive; });

     
     
    updateDecals(ctx);
    updateScrap(ctx, dt);
    for (u32 vi = 0; vi < w.vehicles.size(); ++vi)
    {
        auto& v = w.vehicles[vi];
        warzone::pt::PlayerEffects playerFx(g_pt.sys, v.player);
        v.hitTimer = std::max(0.0f, v.hitTimer - dt);
        if (!v.wrecked)
            continue;
        v.wreckAge += dt;
        if (v.wreckAge < 1.8f + dt)  
        {
            u32 mi = 0;
            for (Entity m : v.meshes)
            {
                const u32 idx = mi++;
                if (auto* tint = tracked(ctx, m) ? ctx.tryGetComponent<RenderTintComponent>(m) : nullptr)
                {
                    const auto c = warzone::decal::tintAt(warzone::decal::charTint(v.charSeed, idx), v.wreckAge);
                    tint->color = Vec4{c.r, c.g, c.b, 1.0f};
                }
            }
        }
        const Vec3 c = toVec(v.box.centre);
        const f32 size = std::clamp(warzone::len(v.box.half) / 4.0f, 0.7f, 2.2f);
        const warzone::decal::Smoulder sm = warzone::decal::smoulderAt(v.wreckAge);
         
        if (v.patchSlot >= 0 && static_cast<size_t>(v.patchSlot) < w.patchPool.slots.size() && w.patchPool.slots[static_cast<size_t>(v.patchSlot)].born == v.patchBorn)
        {
            const f32 pa = w.patchPool.alpha(v.patchSlot, g_destruction.time);
            const f32 glow = v.burn > 0.0f ? 0.85f * std::clamp(v.wreckAge / 15.0f, 0.0f, 1.0f) : sm.ember;
            setCardAlpha(ctx, w.emberE[static_cast<size_t>(v.patchSlot)], glow * pa);
        }
        if (v.burn <= 0.0f)
        {
             
            if (sm.interval > 0.0f)
            {
                v.nextSmoulder -= dt;
                if (v.nextSmoulder <= 0.0f)
                {
                    static constexpr f32 TIER_MUL[warzone::vfx::TIERS] = {1.0f, 2.0f, 4.0f, 8.0f};
                    v.nextSmoulder = sm.interval * TIER_MUL[tierAt(c, 3.0f)];
                    const f32 grow = tierAt(c, 3.0f) >= warzone::vfx::FAR ? 1.8f : 1.0f;
                    wreckSmokeHook(ctx, c + Vec3{w.rng.range(-0.8f, 0.8f) * size, 1.2f * size, w.rng.range(-0.8f, 0.8f) * size}, 1.2f * size * sm.size * grow,
                                   4.5f * size * grow, 6.5f, 1.4f, 0.8f, {sm.grey, sm.grey, sm.grey, sm.alpha}, true);
                }
            }
            continue;
        }
        v.burn -= dt;
        v.nextPuff -= dt;
         
        if (particlesOn())
        {
            warzone::pt::wreck(g_pt.sys, toV3(c), size, v.burn, 70.0f, tierAt(c, 4.0f), dt, v.ptAcc);
            continue;
        }
        const warzone::vfx::Plume plume = warzone::vfx::wreckPlume(tierAt(c, 4.0f), v.burn, 70.0f, size);
        if (v.nextPuff <= 0.0f)
        {
            v.nextPuff = plume.interval;
            wreckSmokeHook(ctx, c + Vec3{w.rng.range(-1.0f, 1.0f) * size, 1.6f * size, w.rng.range(-1.0f, 1.0f) * size}, plume.s0, plume.s1, plume.life, plume.rise,
                           plume.windK, plume.tint, false);
        }
        if (v.fire && v.fire->age >= 0.0f)
        {
            v.fire->s0 = v.fire->s1 = 6.0f * size * plume.flameScale;
            v.fire->pos = c + Vec3{0, 0.3f * v.fire->s1, 0};  
        }
    }
    for (auto& f : w.feed)
        f.age += dt;
    std::erase_if(w.feed, [](const FeedLine& f) { return f.age > 20.0f; });
}

void registerWeaponsSystem(World world)
{
    SystemDescriptor sys;
    sys.moduleName = MODULE;
    sys.name = "WarzoneWeapons";
    sys.fn = weaponsSystem;
    sys.update = SystemPhaseDescriptor{
        .reads = {getComponentId<MeshComponent>()},
        .writes = {getComponentId<TransformComponent>(), getComponentId<FlipbookComponent>(), getComponentId<PointLightComponent>(),
                   getComponentId<AudioEmitterComponent>(), getComponentId<RenderTintComponent>()}};
    world.registerSystem(sys);
}

warzone::ProjectileKind kindFromName(std::string_view name)
{
    for (int i = 0; i < KIND_COUNT; ++i)
        if (name == KIND_NAMES[i])
            return static_cast<warzone::ProjectileKind>(i);
    return warzone::ProjectileKind::Shell;
}

bool vec3Arg(const ControlArgs& args, std::string_view key, Vec3& out)
{
    const auto text = args.json(key);
    f32 x, y, z;
    if (!text || std::sscanf(text->c_str(), "[%f,%f,%f]", &x, &y, &z) != 3)
        return false;
    out = {x, y, z};
    return true;
}

void registerWeaponsCommands(const ModuleContext& context)
{
    auto& control = context.control;
    const std::string_view owner = MODULE;
    static const std::array<std::string_view, 5> kinds = {"shell", "bullet", "missile", "bomb", "autocannon"};
    control.registerCommand(
        "warzoneFire", owner,
        "fires test projectiles in the Play world from a point at a point or a vehicle: shell (ballistic HE, solved "
        "elevation), bullet (tracer, chips walls), missile (guided at target), bomb (guided, falls); count/interval make a "
        "burst, spread scatters it",
        {requiredArg("kind", ArgKind::String, "projectile kind", kinds), requiredArg("from", ArgKind::Json, "[x,y,z] muzzle"),
         optionalArg("to", ArgKind::Json, "[x,y,z] aim point"), optionalArg("target", ArgKind::String, "vehicle name (overrides to)"),
         optionalArg("count", ArgKind::Integer, "rounds", "1"), optionalArg("interval", ArgKind::Number, "s between rounds", "0.08"),
         optionalArg("spread", ArgKind::Number, "m of scatter at the aim point", "0"),
         optionalArg("speed", ArgKind::Number, "launch speed m/s (default the kind's)")},
        [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            if (!control.world("play") || !g_weapons.ready)
                return "warzoneFire needs Play running";
            FireRequest r;
            r.kind = kindFromName(args.string("kind").value_or("shell"));
            if (!vec3Arg(args, "from", r.from))
                return "from must be [x,y,z]";
            r.target = args.string("target").value_or("");
            if (r.target.empty() && !vec3Arg(args, "to", r.to))
                return "give to=[x,y,z] or target=<vehicle>";
            if (!r.target.empty() && vehicleByName(r.target) < 0)
                return std::format("no vehicle '{}'", r.target);
            r.count = static_cast<u32>(std::clamp<long long>(static_cast<long long>(args.integer("count").value_or(1)), 1, 200));
            r.interval = number(args, "interval").value_or(0.08f);
            r.spread = number(args, "spread").value_or(0.0f);
            r.speed = number(args, "speed").value_or(0.0f);
            std::scoped_lock lock(g_weaponsMutex);
            g_weapons.requests.push_back(r);
            result.writeString("kind", KIND_NAMES[static_cast<int>(r.kind)]);
            result.writeU32("count", r.count);
            return {};
        });

    control.registerQuery(
        "warzoneWeapons", owner,
        "the Play world's weapons: projectiles in flight, rounds fired by kind, hits on ground / pieces / vehicles, "
        "explosions, scorch decals, camera shake, every vehicle's health and wreck state, the kill feed",
        {}, [](const ControlArgs&, IFormatWriter& result) -> std::string
        {
            const auto& w = g_weapons;
            if (!w.ready)
                return "no weapons state (start Play)";
            result.writeU32("inFlight", static_cast<u32>(w.shots.size()));
            for (int i = 0; i < KIND_COUNT; ++i)
                result.writeU32(std::format("fired_{}", KIND_NAMES[i]), w.fired[i]);
            result.writeU32("hitGround", w.hitGround);
            result.writeU32("hitPiece", w.hitPiece);
            result.writeU32("hitVehicle", w.hitVehicle);
            result.writeU32("airburst", w.airburst);
            result.writeU32("explosions", w.explosions);
            result.writeU32("decals", w.decalsPlaced);
            result.writeU32("kills", w.kills);
            result.writeF32("shake", g_shake);
            const f32 li[3] = {w.lastImpact.x, w.lastImpact.y, w.lastImpact.z};
            result.writeFloats("lastImpact", li);
            result.writeF32("lastImpactDistance", w.lastImpactDistance);
            result.writeU32("pendingSounds", static_cast<u32>(g_destruction.sounds.size()));
            result.writeU32("soundsStarted", g_soundStats.started);
            result.writeU32("soundsReloaded", g_soundStats.reloaded);
            result.writeU32("soundsNoAudio", g_soundStats.noAudio);
            result.beginArray("vehicles", static_cast<u32>(w.vehicles.size()));
            for (const auto& v : w.vehicles)
            {
                result.beginObject({});
                result.writeString("name", v.name);
                result.writeF32("hp", v.health.hp);
                result.writeF32("maxHp", v.health.maxHp);
                result.writeBool("wrecked", v.wrecked);
                result.writeU32("kills", v.kills);
                const f32 c[3] = {v.box.centre.x, v.box.centre.y, v.box.centre.z};
                result.writeFloats("centre", c);
                result.endObject();
            }
            result.endArray();
            static const char* const HIT[] = {"none", "ground", "piece", "vehicle"};
            result.beginArray("impacts", static_cast<u32>(w.impacts.size()));
            for (const auto& im : w.impacts)
            {
                result.beginObject({});
                result.writeString("kind", KIND_NAMES[im.kind]);
                result.writeString("hit", HIT[im.hit]);
                const f32 p[3] = {im.pos.x, im.pos.y, im.pos.z};
                result.writeFloats("at", p);
                result.endObject();
            }
            result.endArray();
            result.beginArray("feed", static_cast<u32>(w.feed.size()));
            for (const auto& f : w.feed)
                result.writeString({}, f.text);
            result.endArray();
            return {};
        });
}
