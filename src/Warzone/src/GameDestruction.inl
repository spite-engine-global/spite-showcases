 
 
 

namespace wa = warzone::audio;
std::mutex g_audioMutex;
wa::Settings g_audioSettings;
wa::Settings audioSettings()
{
    std::scoped_lock lock(g_audioMutex);
    return g_audioSettings;
}
void setAudioSettings(const wa::Settings& s)
{
    std::scoped_lock lock(g_audioMutex);
    g_audioSettings = wa::sanitize(s);
}
f32 mixAudio(wa::Category category, f32 distance, f32 reference, f32 source = 1, f32 radius = 0)
{
    auto settings = audioSettings();
    settings.master = 100;
    settings.mute = 0;
    return wa::mix(settings, category, distance, reference, source, radius);
}

constexpr f32 PARK_Y = -600.0f;  

enum FxKind : u8
{
    FX_DUST,
    FX_SMOKE,
    FX_FIRE,
    FX_FLASH,  
    FX_SPARK,  
    FX_PUFF,   
    FX_SHOCK,  
    FX_DEBRIS,
    FX_FLAME,
    FX_WASH,
    FX_KINDS
};
static_assert(FX_KINDS == warzone::vfx::CARD_KINDS && static_cast<int>(FX_WASH) == static_cast<int>(warzone::vfx::WASH), "FxKind and vfx::Card are the same list");
const char* const FX_MODELS[FX_KINDS] = {"wz_fx2_dust", "wz_fx2_smoke", "wz_fx2_fire", "wz_fx2_flash", "wz_fx2_spark", "wz_fx2_puff", "wz_fx2_shock", "wz_fx2_debris", "wz_fx2_flame", "wz_fx2_wash"};
 
const char* const FX_MODELS_OLD[FX_KINDS] = {"wz_fx_dust", "wz_fx_smoke", "wz_fx_fire", "wz_fx_flash", "wz_fx_spark", "wz_fx_smoke", "wz_fx_flash", "wz_fx_dust", "wz_fx_fire", "wz_fx_dust"};
constexpr const uint32_t* FX_POOL = warzone::vfx::POOL;

struct FxSlot
{
    Entity e = Entity::undefined();
    FxKind kind = FX_DUST;
    f32 age = -1.0f, life = 1.0f, s0 = 1.0f, s1 = 2.0f;
    Vec3 pos{}, vel{};
    bool loop = false;  
    bool player = false;
    f32 rise = 0.0f, windK = 0.0f, drag = 0.6f;  
    bool grounded = false;                       
    warzone::vfx::Tint tint{};                   
    warzone::vfx::Tint tintSent{-1.0f, 0, 0, 0};
};
 
struct PendingSound
{
    Entity e;
    Vec3 pos;
    f32 due, distance, reference, source, radius;
    wa::Category category;
};
struct LightSlot
{
    Entity e = Entity::undefined();
    f32 age = -1.0f, life = 0.5f, peak = 0.0f, dist = 0.0f;  
};
struct PendingVelocity
{
    Entity e;
    Vec3 linear, angular;
    u32 due, tries;
};
 
 
struct PendingBody
{
    Entity e;
    RigidbodyComponent body;
    u32 due;
};
struct FadeInfo
{
    Vec3 pos, scale;
    bool started = false;
};
struct ScheduledBlast
{
    u32 frame;
    warzone::Blast blast;
    bool effects = true;  
    bool nuke = false;
    u32 piece = ~0u;
    warzone::V3 velocity{};
};

constexpr u32 NUKE_DEBRIS_CAP = 16384;

struct PlayDestruction
{
    bool ready = false;
    warzone::Structure structure;
    std::vector<Entity> pieceEntity;
    std::vector<u16> pieceType;
    std::vector<Vec3> pieceScale;
    std::vector<warzone::Material> pieceMat;
    warzone::DebrisSet debris;
    std::unordered_map<u64, Entity> debrisEntity;
    std::unordered_map<u64, FadeInfo> fading;
    std::unordered_map<u64, Vec3> fallStart;  
    u32 hangKicks = 0;
    std::vector<PendingVelocity> velocities;
    std::vector<PendingBody> bodies;
    std::vector<ScheduledBlast> scheduled;  
    struct NukeWork { warzone::Blast blast; std::vector<u32> pieces; size_t next = 0; f32 age = 0; };
    std::vector<NukeWork> nukeWork;
    struct NukeBuilding { warzone::Blast blast{}; f32 until = 0; u32 stride = 1; bool dustStarted = false; };
    std::vector<NukeBuilding> nukeBuildings;
    struct NukeDebris { Entity e; Vec3 pos, velocity, scale, half, offset, axis; Quat rotation; f32 age = 0, spin = 1.5f, drag = 0.035f; bool settled = false; };
    std::vector<NukeDebris> nukeDebris;
    std::vector<FxSlot> fx;
    std::vector<LightSlot> lights;
    warzone::vfx::Wind wind;
    warzone::vfx::Governor governor;
    warzone::vfx::DelayQueue<160> delayed;  
    int forceTier = -1;                     
    u32 vfxTier[warzone::vfx::TIERS]{};     
    u32 vfxDemoted = 0, vfxStolen = 0, vfxCards = 0;
    u32 vfxByKind[FX_KINDS]{};  
    std::vector<Entity> blastVoices, rumbleVoices;
    u32 nextBlastVoice = 0, nextRumbleVoice = 0;
    std::vector<PendingSound> sounds;
    f32 time = 0.0f;  
    u32 frame = 0;
    warzone::Rng rng{20261115};
    std::vector<warzone::Box> buildingBox;
    std::vector<u32> buildingLastFall, buildingNextDust;
    std::vector<bool> buildingRumbled;
     
     
    std::vector<Entity> buildingBoxEntity;
    std::vector<u32> buildingIntactCount;
    std::vector<u8> buildingPartial;  
    std::vector<u8> buildingNear, buildingDirty, buildingShown, buildingTier;  
    std::vector<u8> authoredBuildings;
    std::vector<u32> buildingNearSince, buildingBlastUntil;
    std::vector<Entity> pendingKill;  
    warzone::BuildingGrid buildingGrid;
    std::vector<f32> lodDist;
    std::vector<u8> residentWant;
    std::vector<u32> lodOrder, detailScratch;
    std::vector<u8> detailWant, tierWant;
    std::vector<std::pair<Entity, MeshComponent>> meshAdds;  
    u32 detailBuildings = 0, detailPieces = 0, lowBuildings = 0, lowPieces = 0, detailSwaps = 0, detailPending = 0;
    u32 residentBuildings = 0, residentPieces = 0, streamedIn = 0, streamedOut = 0, deferredBuildings = 0;
    bool streamedOnce = false;
    std::vector<TransformComponent> pieceXf;  
     
    u32 blasts = 0, destroyed = 0, fallen = 0, fragments = 0, maxFallsInFrame = 0, maxLiveDebris = 0, velocitiesDropped = 0;
    u32 farRemoved = 0, blastsDeferred = 0;  
};
PlayDestruction g_destruction;
warzone::StreamParams g_stream;
bool g_gameTiers = true;
bool g_captureBuildings = false;
 
warzone::TierParams g_tier;
warzone::StreamParams g_detail{.radiusIn = 140.0f, .radiusOut = 180.0f, .pieceBudget = 2500, .budgetSlack = 1.2f};
constexpr u32 STREAM_BODY_CAP = 48000;  
constexpr u32 DETAIL_SWAPS_PER_FRAME = 500;  
 
 
 
u32 g_streamInPerFrame = 150, g_streamOutPerFrame = 600;
 
 
f32 g_effectReach = 1.0f;
u32 g_lightLimit = 8;
std::mutex g_destructionMutex;
Vec3 g_listener{0, 0, 0};  
bool g_listenerSet = false;  
f32 g_shake = 0.0f;        

Vec3 toVec(const warzone::V3& v) { return {v.x, v.y, v.z}; }
warzone::V3 toV3(const Vec3& v) { return {v.x, v.y, v.z}; }

 
warzone::Box boxOf(u16 type, const TransformComponent& t)
{
    const auto& pt = warzone::PIECES[type];
    Vec3 size{pt.sx * t.scale.x, pt.sy * t.scale.y, pt.sz * t.scale.z};
    const Vec3 xAxis = rotate(t.rotation, {1, 0, 0});
    if (std::abs(xAxis.x) < 0.5f)
        std::swap(size.x, size.z);
    return {{t.position.x - size.x * 0.5f, t.position.y, t.position.z - size.z * 0.5f},
            {t.position.x + size.x * 0.5f, t.position.y + size.y, t.position.z + size.z * 0.5f}};
}

RigidbodyComponent staticBody()
{
    RigidbodyComponent body;
    body.bodyType = RigidbodyComponent::BodyType::Static;
    body.friction = 0.8f;
    body.restitution = 0.05f;
    body.objectLayer = static_cast<u16>(ObjectLayer::NonMoving);
    return body;
}

void makeDynamic(RigidbodyComponent& body, f32 mass)
{
    body.bodyType = RigidbodyComponent::BodyType::Dynamic;
    body.mass = mass;
    body.friction = 0.75f;
    body.restitution = 0.08f;
    body.linearDamping = 0.05f;
    body.angularDamping = 0.15f;
    body.objectLayer = static_cast<u16>(ObjectLayer::Moving);
}

BoxColliderComponent colliderFor(u16 type, const Vec3& scale)
{
    const auto& pt = warzone::PIECES[type];
    BoxColliderComponent box;
     
    box.halfExtents = {std::max(0.05f, pt.sx * scale.x * 0.5f), std::max(0.05f, pt.sy * scale.y * 0.5f),
                       std::max(0.05f, pt.sz * scale.z * 0.5f)};
    box.offset = {0.0f, pt.sy * scale.y * 0.5f, 0.0f};
    return box;
}

 
void attachDestruction(const ModuleContext& context, World world)
{
    auto& d = g_destruction;
    {
        std::scoped_lock lock(g_destructionMutex);
        d = PlayDestruction{};
    }
    d.debris.params.cap = 1200;
    d.debris.params.staticSeconds = 45.0f;
    d.structure.params.waveFrames = 14;  
    d.structure.params.waveJitter = 10;

     
     
     
    std::vector<warzone::Box> boxes;
    std::vector<f32> hp, ground;
    std::vector<u16> building;
    std::vector<TransformComponent> placement(g_town.buildings.size());
    d.authoredBuildings.assign(g_town.buildings.size(), 0);
    auto authored = world.createQueryBuilder().buildQuery<Read<WarzoneBuilding>, Read<TransformComponent>>();
    for (auto [b, t] : authored->view<Read<WarzoneBuilding>, Read<TransformComponent>>())
        if (b.id < placement.size()) { placement[b.id] = t; d.authoredBuildings[b.id] = 1; }
    for (u32 i = 0; i < g_town.pieces.size(); ++i)
    {
        const auto& p = g_town.pieces[i];
        if (p.type >= warzone::PIECE_COUNT)
            continue;
        TransformComponent t;
        t.position = toVec(p.pos);
        t.rotation = yawRotation(p.yaw);
        t.scale = toVec(p.scale);
        const auto& root = placement[p.building];
        t.position = root.position + rotate(root.rotation, {t.position.x * root.scale.x, t.position.y * root.scale.y, t.position.z * root.scale.z});
        t.rotation = multiply(root.rotation, t.rotation);
        t.scale = {t.scale.x * root.scale.x, t.scale.y * root.scale.y, t.scale.z * root.scale.z};
        const warzone::Box b = boxOf(p.type, t);
        const auto mat = warzone::PIECES[p.type].mat;
        boxes.push_back(b);
        hp.push_back(warzone::hitPoints(b, mat));
        building.push_back(p.building);
        const warzone::V3 c = b.centre();
        ground.push_back(g_terrain.valid() ? g_terrain.height(c.x, c.z) : b.lo.y);
        d.pieceEntity.push_back(Entity::undefined());
        d.pieceType.push_back(p.type);
        d.pieceScale.push_back(t.scale);
        d.pieceMat.push_back(mat);
        d.pieceXf.push_back(t);
    }
    d.structure.build(boxes, hp, building, ground);

    u32 buildings = 0;
    for (u16 b : building)
        buildings = std::max<u32>(buildings, b + 1u);
    d.buildingBox.assign(buildings, {{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}});
    for (size_t i = 0; i < boxes.size(); ++i)
    {
        auto& bb = d.buildingBox[building[i]];
        bb.lo = {std::min(bb.lo.x, boxes[i].lo.x), std::min(bb.lo.y, boxes[i].lo.y), std::min(bb.lo.z, boxes[i].lo.z)};
        bb.hi = {std::max(bb.hi.x, boxes[i].hi.x), std::max(bb.hi.y, boxes[i].hi.y), std::max(bb.hi.z, boxes[i].hi.z)};
    }
    d.buildingLastFall.assign(buildings, 0);
    d.buildingNextDust.assign(buildings, 0);
    d.buildingRumbled.assign(buildings, false);
    g_listenerSet = false;
    d.buildingIntactCount.assign(buildings, 0);
    for (u16 b : building)
        ++d.buildingIntactCount[b];
    d.buildingNear.assign(buildings, 0);  
    d.buildingDirty.assign(buildings, 1);
    d.buildingShown.assign(buildings, 0);
    d.buildingPartial.assign(buildings, 0);
    d.buildingTier.assign(buildings, 0);
    d.detailWant.assign(buildings, 0);
    d.tierWant.assign(buildings, 0);
    d.buildingNearSince.assign(buildings, 0);
    d.nukeBuildings.assign(buildings, {});
    d.buildingBlastUntil.assign(buildings, 0);
    d.buildingBoxEntity.assign(buildings, Entity::undefined());
    {
        const Entity lodRoot = world.createEntity();
        world.addComponent<TransformComponent>(lodRoot);
        setEntityName(world, lodRoot, "Building Stand-ins");
        std::vector<std::pair<Entity, Entity>> lodEdges;
        for (const auto& bs : g_town.buildings)
        {
            if (bs.id >= buildings || bs.count == 0)
                continue;
            const Entity e = world.createEntity();
            TransformComponent t;
            t.position = {0.0f, PARK_Y, 0.0f};
            t.scale = {1e-3f, 1e-3f, 1e-3f};
            world.addComponent<TransformComponent>(e, t);
            MeshComponent mesh;
            const std::string standInName = warzone::standInModel(bs.kind, bs.floors, bs.bounds, bs.id);
            mesh.meshHandle = AssetHandle{InternedString(standInName), AssetType::MESH, 0};
            MaterialComponent mat;
            mat.materialHandle = AssetHandle{InternedString(standInName + "_mat"), AssetType::MATERIAL, 0};
            world.addComponent<MeshComponent>(e, mesh);
            world.addComponent<MaterialComponent>(e, mat);
             
            const u32 h = warzone::hash32(bs.id * 2654435761u + 17u);
            const f32 v = 0.82f + 0.36f * static_cast<f32>(h & 255u) / 255.0f, w = 0.05f * (static_cast<f32>((h >> 8) & 255u) / 255.0f - 0.5f);
            RenderTintComponent tint;
            tint.color = Vec4{v * (1.0f + w), v, v * (1.0f - w), 1.0f};
            world.addComponent<RenderTintComponent>(e, tint);
            setEntityName(world, e, bs.name + " (far)");
            lodEdges.emplace_back(e, lodRoot);
            d.buildingBoxEntity[bs.id] = e;
        }
        attachAll(world, lodEdges);
    }

     
    const Entity pool = world.createEntity();
    world.addComponent<TransformComponent>(pool);
    setEntityName(world, pool, "Effects Pool");
    std::vector<std::pair<Entity, Entity>> poolEdges;
    auto& root = context.services;
    if (root.contains<ModelSpawner>())
    {
        const auto& spawner = root.get<ModelSpawner>();
        for (int k = 0; k < FX_KINDS; ++k)
        {
            InternedString model(FX_MODELS[k]);
            if (!spawner.hasModel(model))
                model = InternedString(FX_MODELS_OLD[k]);
            if (!spawner.hasModel(model))
            {
                SWARNING_LOG("Warzone: effect card {} is not baked", FX_MODELS[k]);
                continue;
            }
            std::vector<Entity> parents(FX_POOL[k], Entity::undefined());
            auto made = spawner.spawn(world, model, parents);
            if (!made)
            {
                SWARNING_LOG("Warzone: spawning {} failed: {}", FX_MODELS[k], made.error().message);
                continue;
            }
            for (Entity e : *made)
            {
                auto& t = world.getComponent<TransformComponent>(e);
                t.position = {0.0f, PARK_Y, 0.0f};
                t.scale = {0.01f, 0.01f, 0.01f};
                FlipbookComponent fb;
                fb.columns = 8;
                fb.rows = 8;
                fb.frameCount = 64;
                fb.framesPerSecond = 24.0f;
                fb.loop = false;
                fb.playing = false;
                fb.cameraFacing = true;
                if (world.hasComponent<FlipbookComponent>(e))
                    world.getComponent<FlipbookComponent>(e) = fb;
                else
                    world.addComponent<FlipbookComponent>(e, fb);
                if (!world.hasComponent<RenderTintComponent>(e))  
                    world.addComponent<RenderTintComponent>(e);
                d.fx.push_back({e, static_cast<FxKind>(k)});
                poolEdges.emplace_back(e, pool);
            }
        }
    }
    for (int i = 0; i < 8; ++i)
    {
        const Entity e = world.createEntity();
        TransformComponent t;
        t.position = {0.0f, PARK_Y, 0.0f};
        world.addComponent<TransformComponent>(e, t);
        PointLightComponent light;
        light.color = {1.0f, 0.62f, 0.3f};
        light.intensity = 0.0f;
        light.radius = 40.0f;
        world.addComponent<PointLightComponent>(e, light);
        setEntityName(world, e, "Blast Light");
        d.lights.push_back({e});
        poolEdges.emplace_back(e, pool);
    }
    auto voice = [&](const char* clip, const char* name)
    {
        const Entity e = world.createEntity();
        TransformComponent t;
        t.position = {0.0f, PARK_Y, 0.0f};
        world.addComponent<TransformComponent>(e, t);
        AudioEmitterComponent a;
        a.clip = InternedString(clip);
        a.playing = false;
        a.spatial = true;
        a.volume = 1.0f;
        world.addComponent<AudioEmitterComponent>(e, a);
        setEntityName(world, e, name);
        poolEdges.emplace_back(e, pool);
        return e;
    };
    for (int i = 0; i < 4; ++i)
        d.blastVoices.push_back(voice("wz_blast.wav", "Blast Voice"));
    for (int i = 0; i < 3; ++i)
        d.rumbleVoices.push_back(voice("wz_rumble.wav", "Rumble Voice"));
    attachAll(world, poolEdges);
    d.ready = true;
    SINFO_LOG("Warzone: destruction ready, {} pieces ({} unsupported at build), {} buildings, {} effect cards", d.pieceEntity.size(),
              d.structure.unsupportedAtBuild(), buildings, d.fx.size());
}

 
bool tracked(const SystemContext& ctx, Entity e) { return e.isValid() && ctx.isEntityValid(e) && ctx.isEntityRegistered(e); }

 
 
void applyTint(SystemContext& ctx, FxSlot& f, const warzone::vfx::Tint& t)
{
    const auto& o = f.tintSent;
    if (std::abs(t.r - o.r) < 0.004f && std::abs(t.g - o.g) < 0.004f && std::abs(t.b - o.b) < 0.004f && std::abs(t.a - o.a) < 0.004f)
        return;
    if (auto* rt = ctx.tryGetComponent<RenderTintComponent>(f.e))
    {
        rt->color = Vec4{t.r, t.g, t.b, t.a};
        f.tintSent = t;
    }
}

FxSlot* spawnFx(SystemContext& ctx, FxKind kind, const Vec3& pos, f32 s0, f32 s1, f32 life, const Vec3& vel, bool loop = false)
{
    auto& d = g_destruction;
    if (particlesOn())  
    {
        if (!loop)
            warzone::pt::card(g_pt.sys, static_cast<warzone::vfx::Card>(kind), toV3(pos), s0, s1, life, toV3(vel),
                              kind == FX_SMOKE || kind == FX_PUFF ? PT_ASH : PT_DUST, 1.0f);
        ++d.vfxByKind[kind];
        return nullptr;
    }
    FxSlot* slot = nullptr;
    for (auto& f : d.fx)
        if (f.kind == kind && f.age < 0.0f)
        {
            slot = &f;
            break;
        }
    if (!slot)  
        for (auto& f : d.fx)
            if (f.kind == kind && (!f.player || g_pt.sys.playerEffects) && (!slot || f.age / f.life > slot->age / slot->life))
                slot = &f;
    if (!slot)
        return nullptr;
    slot->player = g_pt.sys.playerEffects;
    if (slot->age >= 0.0f)
        ++d.vfxStolen;
    slot->age = 0.0f;
    slot->life = life;
    slot->loop = loop;
    slot->s0 = s0;
    slot->s1 = s1;
    slot->pos = pos;
    slot->vel = vel;
     
    static constexpr f32 WIND_K[FX_KINDS] = {0.4f, 0.5f, 0.0f, 0.0f, 0.0f, 0.6f, 0.0f, 0.0f, 0.0f, 0.2f};
    slot->windK = WIND_K[kind];
    slot->drag = 0.6f;
    slot->grounded = kind == FX_DUST || kind == FX_SMOKE || kind == FX_PUFF || kind == FX_WASH;
    slot->rise = slot->grounded ? std::max(0.0f, vel.y) : 0.0f;  
    slot->tint = {};
    ++d.vfxCards;
    ++d.vfxByKind[kind];
    if (auto* t = ctx.tryGetComponent<TransformComponent>(slot->e))
    {
        t->position = pos;
        t->scale = {s0, s0, s0};
    }
    if (auto* fb = ctx.tryGetComponent<FlipbookComponent>(slot->e))
    {
        fb->elapsedSeconds = 0.0f;
        fb->framesPerSecond = loop ? 20.0f : std::clamp(static_cast<f32>(fb->frameCount) / life, 0.5f, 240.0f);
        fb->playing = true;
        fb->loop = loop;
    }
    applyTint(ctx, *slot, slot->tint);
    return slot;
}

void tintFx(SystemContext& ctx, FxSlot* f, const warzone::vfx::Tint& t)
{
    if (!f)
        return;
    f->tint = t;
    applyTint(ctx, *f, t);
}

 
FxSlot* spawnSpec(SystemContext& ctx, const warzone::vfx::Spec& s, const Vec3& origin)
{
    FxSlot* f = spawnFx(ctx, static_cast<FxKind>(s.card), origin + toVec(s.pos), s.s0, s.s1, s.life, toVec(s.vel), s.loop);
    if (!f)
        return nullptr;
    f->rise = s.rise > 0.0f ? s.rise : f->rise;
    f->windK = s.windK > 0.0f || s.card == warzone::vfx::DEBRIS ? s.windK : f->windK;
    f->drag = s.drag;
    f->grounded = s.grounded;
    f->tint = s.tint;
    applyTint(ctx, *f, f->tint);
    return f;
}

 
void flash(SystemContext& ctx, const Vec3& pos, f32 peak, f32 life, f32 radius = 40)
{
    auto& d = g_destruction;
    const f32 dist = length(pos - g_listener);
    const int pick = warzone::vfx::pickLight(std::span(d.lights).first(std::min<size_t>(g_lightLimit, d.lights.size())), peak, dist);
    if (pick < 0)
        return;
    LightSlot* slot = &d.lights[pick];
    slot->age = 0.0f;
    slot->life = life;
    slot->peak = peak;
    slot->dist = dist;
    if (auto* light = ctx.tryGetComponent<PointLightComponent>(slot->e)) light->radius = radius;
    if (auto* t = ctx.tryGetComponent<TransformComponent>(slot->e))
        t->position = pos;
}

 
warzone::vfx::Tier effectTier(const Vec3& at, f32 radius)
{
    if (g_pt.sys.playerEffects) return warzone::vfx::NEAR;
    auto& d = g_destruction;
    if (d.forceTier >= 0)
        return static_cast<warzone::vfx::Tier>(d.forceTier);
    const warzone::vfx::Tier want = warzone::vfx::tierFor(length(at - g_listener) / g_effectReach, radius);
    const warzone::vfx::Tier got = d.governor.admit(want);
    d.vfxDemoted += got != want;
    ++d.vfxTier[got];
    return got;
}
 
warzone::vfx::Tier tierAt(const Vec3& at, f32 radius)
{
    if (g_pt.sys.playerEffects) return warzone::vfx::NEAR;
    return g_destruction.forceTier >= 0 ? static_cast<warzone::vfx::Tier>(g_destruction.forceTier) : warzone::vfx::tierFor(length(at - g_listener) / g_effectReach, radius);
}

 
void spawnPlan(SystemContext& ctx, const warzone::vfx::Plan& p, const Vec3& at)
{
    auto& d = g_destruction;
    for (int i = 0; i < p.count; ++i)
    {
        const auto& s = p.cards[i];
        if (s.delay <= 0.0f)
            spawnSpec(ctx, s, at);
        else
            d.delayed.push(s, toV3(at), d.time + s.delay);
    }
}

 
void explosionEffects(SystemContext& ctx, const Vec3& at, f32 r)
{
    auto& d = g_destruction;
    const f32 dist = length(at - g_listener);
    const f32 ground = g_terrain.valid() ? g_terrain.height(at.x, at.z) : at.y;
    const warzone::vfx::Tier tier = effectTier(at, r);
    const warzone::vfx::Plan p = warzone::vfx::explosion(r, dist, at.y - ground, tier, d.rng);
    if (particlesOn())
        warzone::pt::explosion(g_pt.sys, toV3(at), r, at.y - ground, tier, PT_DUST);
    else
        spawnPlan(ctx, p, at);
    if (p.lightPeak > 0.0f)
        flash(ctx, at + Vec3{0, 1.5f, 0}, p.lightPeak, p.lightLife);
}

 
void playVoice(std::vector<Entity>& voices, u32& next, const Vec3& pos, f32 reference = 60.0f, f32 volume = 1.0f, wa::Category category = wa::Category::Weapons, f32 radius = 0)
{
    if (voices.empty())
        return;
    const f32 d = length(pos - g_listener);
    const Entity e = voices[next++ % voices.size()];
    g_destruction.sounds.push_back({e, pos, g_destruction.time + wa::delay(audioSettings(), d), d, reference, volume, radius, category});
}

 
struct SoundStats
{
    u32 started = 0, reloaded = 0, noAudio = 0;
} g_soundStats;

void replayVoice(SystemContext& ctx, Entity e, wa::Category category = wa::Category::Weapons, f32 distance = 0, f32 reference = 1, f32 source = 1, f32 radius = 0)
{
    if (!ctx.services().contains<AudioWorld>())
    {
        ++g_soundStats.noAudio;
        return;
    }
    if (auto* a = ctx.tryGetComponent<AudioEmitterComponent>(e))
        a->volume = mixAudio(category, distance, reference, source, radius);
    auto audio = ctx.services().write<AudioWorld>();
    audio->stop(e);
    audio->play(e);
    ++g_soundStats.started;
}

void updateSounds(SystemContext& ctx)
{
    auto& d = g_destruction;
    size_t keep = 0;
    for (auto& snd : d.sounds)
    {
        if (snd.due > d.time && audioSettings().delay >= 0.5f)
        {
            d.sounds[keep++] = snd;
            continue;
        }
        if (auto* t = ctx.tryGetComponent<TransformComponent>(snd.e))
            t->position = snd.pos;
        replayVoice(ctx, snd.e, snd.category, snd.distance, snd.reference, snd.source, snd.radius);
    }
    d.sounds.resize(keep);
}

void blastEffects(SystemContext& ctx, const warzone::Blast& b)
{
    const Vec3 c = toVec(b.centre);
    const f32 r = b.radius;
    explosionEffects(ctx, c, r);
    g_shake = std::min(1.0f, g_shake + warzone::shakeAt(std::clamp(r / 4.0f, 0.3f, 1.5f), r, length(c - g_listener)));
    playVoice(g_destruction.blastVoices, g_destruction.nextBlastVoice, c, 80.0f, 1, wa::Category::Explosions, r);
}

void updateFx(SystemContext& ctx, f32 dt)
{
    auto& d = g_destruction;
    d.time += dt;
    g_shake = std::max(0.0f, g_shake - dt * 2.2f);
    updateSounds(ctx);
    d.governor.newFrame();
    d.delayed.pop(d.time, 24, [&](const warzone::vfx::Pending& p) { spawnSpec(ctx, p.spec, toVec(p.origin)); });
    for (auto& f : d.fx)
    {
        if (f.age < 0.0f)
            continue;
        f.age += dt;
        auto* t = ctx.tryGetComponent<TransformComponent>(f.e);
        if (!t)
            continue;
        if (f.age >= f.life)
        {
            f.age = -1.0f;
            t->position = {0.0f, PARK_Y, 0.0f};
            t->scale = {0.01f, 0.01f, 0.01f};
            if (auto* fb = ctx.tryGetComponent<FlipbookComponent>(f.e))
                fb->playing = false;
            continue;
        }
        const f32 u = f.loop ? std::min(1.0f, f.age / 1.5f) : f.age / f.life;
        const f32 ground = g_terrain.valid() ? g_terrain.height(f.pos.x, f.pos.z) : 0.0f;
        warzone::vfx::Motion m{toV3(f.pos), toV3(f.vel), f.age - dt, f.life, f.rise, f.windK, f.drag};
        warzone::vfx::step(m, dt, d.wind, d.time, ground);  
        f.pos = toVec(m.pos);
        f.vel = toVec(m.vel);
        const f32 s = f.s0 + (f.s1 - f.s0) * (1.0f - (1.0f - u) * (1.0f - u));
        t->position = f.pos;
         
        if (f.grounded && g_terrain.valid())
            t->position.y = std::max(f.pos.y, g_terrain.height(f.pos.x, f.pos.z) + s * (f.kind == FX_WASH ? 0.18f : 0.4f));
        t->scale = {s, s, s};
        if (f.loop)  
        {
            warzone::vfx::Tint tint = f.tint;
            tint.a *= warzone::vfx::fadeAlpha(f.age / f.life, 0.02f, 0.85f);
            applyTint(ctx, f, tint);
        }
        else
            applyTint(ctx, f, f.tint);
    }
    for (auto& l : d.lights)
    {
        if (l.age < 0.0f)
            continue;
        l.age += dt;
        auto* light = ctx.tryGetComponent<PointLightComponent>(l.e);
        if (!light)
            continue;
        if (l.age >= l.life)
        {
            l.age = -1.0f;
            light->intensity = 0.0f;
            if (auto* t = ctx.tryGetComponent<TransformComponent>(l.e))
                t->position = {0.0f, PARK_Y, 0.0f};
            continue;
        }
        const f32 k = 1.0f - l.age / l.life;
        light->intensity = l.peak * k * k;
    }
    ptUpdate(ctx, dt, g_listener, d.wind);
}

 
 
 
bool g_directBodySwap = false;

void swapBody(SystemContext& ctx, CommandBuffer& cb, Entity e, const RigidbodyComponent& body, u32 frame)
{
    if (g_directBodySwap && ctx.hasComponent<RigidbodyComponent>(e))
    {
        std::erase_if(g_destruction.bodies, [&](const PendingBody& b) { return b.e == e; });
        ctx.getComponent<RigidbodyComponent>(e) = body;
        return;
    }
    auto& pending = g_destruction.bodies;
    const bool waiting = std::erase_if(pending, [&](const PendingBody& b) { return b.e == e; }) > 0;
    if (!waiting && ctx.hasComponent<RigidbodyComponent>(e))
        cb.removeComponent<RigidbodyComponent>(e);
    pending.push_back({e, body, frame + 1});
}

void addDebris(u64 id, Entity e, std::vector<warzone::DebrisSet::Event>& events)
{
    g_destruction.debrisEntity[id] = e;
    g_destruction.debris.add(id, events);
}

 
 
MeshComponent pieceMesh(u16 type, u8 tier)
{
    if (!g_gameTiers)
        tier = 1;
    if (tier == 0)
        return meshOf(warzone::PIECES[type]);
    static std::array<std::array<MeshComponent, warzone::PIECE_COUNT>, 3> kits;
    auto& m = kits[tier][type];
    if (!m.meshHandle.isValid())
        m.meshHandle = AssetHandle{InternedString(std::string(tier == 1 ? "wz_pf_" : "wz_pl_") + std::string(warzone::PIECES[type].model.substr(5))), AssetType::MESH, 0};
    return m;
}

 
 
 
 
 
bool streamPieces(SystemContext& ctx, CommandBuffer& cb, u32 b, bool in, u32& budget)
{
    auto& d = g_destruction;
    const auto& spec = g_town.buildings[b];
    for (u32 i = spec.first; i < spec.first + spec.count; ++i)
    {
        if (d.structure.node(i).state != warzone::PieceState::Intact)
            continue;  
        if (!in)
        {
            if (d.pieceEntity[i].isValid())
            {
                if (budget == 0)
                    return false;
                --budget;
                if (tracked(ctx, d.pieceEntity[i]))
                    cb.destroyEntity(d.pieceEntity[i]);
                else
                    d.pendingKill.push_back(d.pieceEntity[i]);
                d.pieceEntity[i] = Entity::undefined();
                ++d.streamedOut;
            }
        }
        else if (!d.pieceEntity[i].isValid())
        {
            if (budget == 0)
                return false;
            --budget;
            const Entity e = cb.createEntity();
            cb.addComponent<TransformComponent>(e, TransformComponent(d.pieceXf[i]));
             
             
            cb.addComponent<TransformMatrixComponent>(e, TransformMatrixComponent{});
            const auto& pt = warzone::PIECES[d.pieceType[i]];
            cb.addComponent<MeshComponent>(e, pieceMesh(d.pieceType[i], d.buildingTier[b]));
            cb.addComponent<MaterialComponent>(e, materialOf(pt));
            cb.addComponent<RigidbodyComponent>(e, staticBody());
            cb.addComponent<BoxColliderComponent>(e, colliderFor(d.pieceType[i], d.pieceScale[i]));
            d.pieceEntity[i] = e;
            ++d.streamedIn;
        }
    }
    return true;
}

 
bool swapDetail(SystemContext& ctx, CommandBuffer& cb, u32 b, u8 tier)
{
    auto& d = g_destruction;
    const auto& spec = g_town.buildings[b];
    for (u32 i = spec.first; i < spec.first + spec.count; ++i)
    {
        if (d.structure.node(i).state != warzone::PieceState::Intact || !d.pieceEntity[i].isValid())
            continue;
        if (!tracked(ctx, d.pieceEntity[i]))
            return false;
    }
    for (u32 i = spec.first; i < spec.first + spec.count; ++i)
    {
        if (d.structure.node(i).state != warzone::PieceState::Intact || !d.pieceEntity[i].isValid())
            continue;
        cb.removeComponent<MeshComponent>(d.pieceEntity[i]);
        d.meshAdds.emplace_back(d.pieceEntity[i], pieceMesh(d.pieceType[i], tier));
    }
    return true;
}

void updateBuildingLod(SystemContext& ctx, CommandBuffer& cb)
{
    auto& d = g_destruction;
    {
        size_t keep = 0;
        for (const Entity e : d.pendingKill)
        {
            if (tracked(ctx, e))
                cb.destroyEntity(e);
            else
                d.pendingKill[keep++] = e;
        }
        d.pendingKill.resize(keep);
    }
    for (auto& [e, mesh] : d.meshAdds)  
        if (ctx.isEntityValid(e) && ctx.isEntityRegistered(e) && !ctx.hasComponent<MeshComponent>(e))
            cb.addComponent<MeshComponent>(e, MeshComponent(mesh));
    d.meshAdds.clear();
    const u32 n = static_cast<u32>(d.buildingNear.size());
    if (d.buildingGrid.cells.empty())
        d.buildingGrid.build(d.buildingBox);
    d.lodDist.resize(n, 1e9f);
    d.residentWant = d.buildingNear;
    auto& want = d.residentWant;
    if (g_captureBuildings)
    {
        d.lodOrder.clear(); d.residentPieces = 0;
        for (u32 b = 0; b < n; ++b) {
            want[b] = d.authoredBuildings[b];
            d.detailWant[b] = want[b]; d.tierWant[b] = 1; d.lodDist[b] = 0;
            if (want[b]) d.residentPieces += d.buildingIntactCount[b];
            if (want[b] || d.buildingNear[b] || d.buildingPartial[b]) d.lodOrder.push_back(b);
        }
    }
    else if (g_listenerSet)
    {
        d.buildingGrid.candidates(g_listener.x, g_listener.z, std::max(g_stream.radiusIn, g_stream.radiusOut), d.lodOrder);
         
        for (u32 b = 0; b < n; ++b)
            if (d.buildingNear[b] || d.buildingPartial[b] || d.buildingBlastUntil[b] > d.frame)
                d.lodOrder.push_back(b);
        std::sort(d.lodOrder.begin(), d.lodOrder.end());
        d.lodOrder.erase(std::unique(d.lodOrder.begin(), d.lodOrder.end()), d.lodOrder.end());
        for (u32 b : d.lodOrder)
            d.lodDist[b] = g_town.buildings[b].count ? warzone::groundDistance(d.buildingBox[b], g_listener.x, g_listener.z) : 1e9f;
        std::sort(d.lodOrder.begin(), d.lodOrder.end(), [&](u32 a, u32 b) { return d.lodDist[a] != d.lodDist[b] ? d.lodDist[a] < d.lodDist[b] : a < b; });
        d.residentPieces = warzone::chooseByOrder(d.lodOrder, d.lodDist, d.buildingIntactCount, want, g_stream);
        for (u32 b : d.lodOrder)
            if (d.buildingBlastUntil[b] > d.frame) want[b] = 1;
        for (u32 b = 0; b < n; ++b)
            if (!want[b]) d.detailWant[b] = 0;
    }
    else {
        d.lodOrder.clear();
        for (u32 b = 0; b < n; ++b)
            if (d.buildingBlastUntil[b] > d.frame) { want[b] = 1; d.lodOrder.push_back(b); }
    }
    u32 budgetIn = d.streamedOnce ? g_streamInPerFrame : 1000000u, budgetOut = g_streamOutPerFrame;  
    d.streamedOnce = d.streamedOnce || g_listenerSet;
    d.residentBuildings = 0;
    d.deferredBuildings = 0;
    d.detailPieces = d.detailBuildings = d.lowPieces = d.lowBuildings = d.detailPending = 0;
    u32 swapBudget = DETAIL_SWAPS_PER_FRAME;
    if (g_listenerSet && g_gameTiers)
    {
        warzone::chooseDetail(d.lodOrder, d.lodDist, d.buildingIntactCount, want, d.detailWant, d.detailScratch, g_detail);
        for (u32 b : d.lodOrder)
            d.tierWant[b] = warzone::chooseTier(d.detailWant[b] != 0, d.lodDist[b], d.buildingTier[b], g_tier);
    }
    for (const u32 b : d.lodOrder)
    {
        if (want[b] != d.buildingNear[b] || d.buildingPartial[b])
        {
            u32& budget = want[b] ? budgetIn : budgetOut;
            if (budget == 0)
                ++d.deferredBuildings;
            else if (want[b])
            {
                if (!d.buildingNear[b] && !d.buildingPartial[b])
                    d.buildingTier[b] = g_gameTiers ? d.tierWant[b] : 1;  
                if (streamPieces(ctx, cb, b, true, budgetIn))
                {
                    d.buildingNear[b] = 1;
                    d.buildingPartial[b] = 0;
                    d.buildingNearSince[b] = d.frame;
                }
                else
                    d.buildingPartial[b] = 1;
            }
            else if (streamPieces(ctx, cb, b, false, budgetOut))
            {
                d.buildingNear[b] = 0;
                d.buildingPartial[b] = 0;
                d.buildingTier[b] = 0;
                d.buildingDirty[b] = 1;
            }
            else
                d.buildingPartial[b] = 1;
        }
        d.residentBuildings += d.buildingNear[b];
        if (g_gameTiers && d.buildingNear[b] && d.tierWant[b] != d.buildingTier[b])
        {
            const u32 n = g_town.buildings[b].count;
            if (swapBudget >= std::min(n, DETAIL_SWAPS_PER_FRAME) && d.frame > d.buildingNearSince[b] + 2 &&
                swapDetail(ctx, cb, b, d.tierWant[b]))
            {
                d.buildingTier[b] = d.tierWant[b];
                swapBudget -= std::min(swapBudget, n);
                ++d.detailSwaps;
            }
            else
                ++d.detailPending;
        }
        if (d.buildingNear[b] && d.buildingTier[b])
        {
            (d.buildingTier[b] == 1 ? d.detailBuildings : d.lowBuildings) += 1;
            (d.buildingTier[b] == 1 ? d.detailPieces : d.lowPieces) += d.buildingIntactCount[b];
        }
    }
     
     
    for (u32 b = 0; b < n; ++b)
    {
        const Entity box = d.buildingBoxEntity[b];
        const bool show = (!d.buildingNear[b] || d.frame < d.buildingNearSince[b] + 3) && d.nukeBuildings[b].until <= d.time &&
            (!g_captureBuildings || d.authoredBuildings[b]);
        if (show != static_cast<bool>(d.buildingShown[b]) || (show && d.buildingDirty[b]))
        {
            if (!box.isValid() || !tracked(ctx, box))
                continue;
            d.buildingShown[b] = show ? 1 : 0;
            d.buildingDirty[b] = 0;
            if (auto* t = ctx.tryGetComponent<TransformComponent>(box))
            {
                const auto& spec = g_town.buildings[b];
                f32 top = -1e9f;
                for (u32 i = spec.first; i < spec.first + spec.count; ++i)
                    if (d.structure.node(i).state == warzone::PieceState::Intact)
                        top = std::max(top, d.structure.node(i).box.hi.y);
                const auto s = warzone::standIn(spec.kind, spec.floors, d.buildingBox[b], top, d.buildingIntactCount[b]);
                if (show && s.shown)
                {
                    t->position = toVec(s.base);
                    t->scale = toVec(s.size);
                }
                else
                {
                    t->position = {0.0f, PARK_Y, 0.0f};
                    t->scale = {1e-3f, 1e-3f, 1e-3f};
                }
            }
        }
    }
}

void destructionSystem(SystemContext& ctx)
{
    if (ctx.services().contains<AudioWorld>())
    {
        const auto settings = audioSettings();
        const f32 gain = settings.mute >= 0.5f ? 0 : settings.master * 0.01f;
        auto audio = ctx.services().write<AudioWorld>();
        if (audio->masterGain() != gain) audio->setMasterGain(gain);
    }
    auto& d = g_destruction;
    if (!d.ready)
        return;
    const f32 dt = std::min(ctx.deltaTime, 0.1f);
    const u32 frame = ++d.frame;
    auto& cb = ctx.getCommandBuffer();
    updateBuildingLod(ctx, cb);
    std::vector<warzone::Removal> removals;
    std::vector<warzone::DebrisSet::Event> events;

     
    std::vector<ScheduledBlast> due;
    {
        std::scoped_lock lock(g_destructionMutex);
        size_t keep = 0;
        constexpr size_t BLAST_BUDGET = 60;  
        for (auto& s : d.scheduled)
        {
            if (s.frame <= frame && due.size() < BLAST_BUDGET)
                due.push_back(s);
            else
            {
                if (s.frame <= frame)
                    ++d.blastsDeferred;
                d.scheduled[keep++] = s;
            }
        }
        d.scheduled.resize(keep);
    }
    for (const auto& sb : due)
    {
        if (sb.piece != ~0u) {
            const auto& node = d.structure.node(sb.piece);
            if (node.state == warzone::PieceState::Removed) continue;
            d.buildingBlastUntil[node.building] = frame + 120;
            if (!tracked(ctx, d.pieceEntity[sb.piece])) {
                auto retry = sb;
                retry.frame = frame + 1;
                std::scoped_lock lock(g_destructionMutex);
                d.scheduled.push_back(retry);
                continue;
            }
            d.structure.remove(sb.piece, frame);
            removals.push_back({sb.piece, false, sb.velocity});
            continue;
        }
        const warzone::Blast& b = sb.blast;
        if (sb.nuke)
        {
            PlayDestruction::NukeWork work{b};
            d.structure.overlapSphere(b.centre, b.radius, work.pieces);
            if (g_captureBuildings)
                std::erase_if(work.pieces, [&](u32 piece) { return !d.authoredBuildings[d.structure.node(piece).building]; });
            const u32 stride = std::max(1u, (static_cast<u32>(work.pieces.size()) + 8191) / 8192);
            for (u32 piece : work.pieces) {
                const u32 building = d.structure.node(piece).building;
                d.nukeBuildings[building] = {b, d.time + 8, stride};
                d.buildingBlastUntil[building] = frame + 480;
            }
            std::sort(work.pieces.begin(), work.pieces.end(), [&](u32 a, u32 c) {
                const auto da = warzone::distance(d.structure.node(a).box, b.centre);
                const auto dc = warzone::distance(d.structure.node(c).box, b.centre);
                return da != dc ? da < dc : a < c;
            });
            d.nukeWork.push_back(std::move(work));
        }
        else
            d.structure.applyBlast(b, frame, removals);
        if (sb.effects)
            blastEffects(ctx, b);
        ++d.blasts;
         
        if (ctx.services().contains<PhysicsWorld>() && length(toVec(b.centre) - g_listener) < 700.0f)
        {
            auto physics = ctx.services().write<PhysicsWorld>();
            for (const auto& item : d.debris.items())
            {
                if (item.state != warzone::DebrisState::Dynamic)
                    continue;
                const Entity e = d.debrisEntity[item.id];
                if (!tracked(ctx, e) || !physics->hasBody(e))
                    continue;
                const auto* t = ctx.tryGetConstComponent<TransformComponent>(e);
                if (!t)
                    continue;
                const warzone::V3 off = toV3(t->position) - b.centre;
                const f32 dist = warzone::len(off);
                if (dist < b.radius * 2.0f)
                    physics->addImpulse(e, toVec(warzone::norm(off + warzone::V3{0, 0.5f, 0}) * (b.push * 200.0f * (1.0f - dist / (b.radius * 2.0f)))));
            }
        }
    }

     
    u32 nukeBudget = 1024;
    for (auto& work : d.nukeWork) {
        work.age += dt;
        while (nukeBudget && work.next < work.pieces.size())
        {
            const u32 piece = work.pieces[work.next];
            if (warzone::distance(d.structure.node(piece).box, work.blast.centre) > work.age * 1200) break;
            const u32 building = d.structure.node(piece).building;
            if (d.structure.node(piece).state != warzone::PieceState::Removed) {
                d.buildingBlastUntil[building] = frame + 480;
                d.nukeBuildings[building].until = d.time + 8;
            }
            d.structure.nukePiece(work.pieces[work.next++], work.blast, frame, removals);
            --nukeBudget;
        }
    }
    std::erase_if(d.nukeWork, [](const auto& work) { return work.next == work.pieces.size(); });

     
    d.structure.tick(frame, removals);
    d.maxFallsInFrame = std::max<u32>(d.maxFallsInFrame, static_cast<u32>(removals.size()));

     
    u32 fragmentBudget = 80;
    u32 dustBudget = 3;
    u32 nukeVisualBudget = 1280, nukeChunkBudget = 128, nukeDustBudget = 6;
    for (const auto& r : removals)
    {
        const u32 i = r.piece;
        const Entity e = d.pieceEntity[i];
        const auto& node = d.structure.node(i);
        const warzone::Box& box = node.box;
        const f32 volume = box.volume();
        const u16 b = node.building;
        if (d.buildingIntactCount[b] > 0)
            --d.buildingIntactCount[b];
        d.buildingDirty[b] = 1;
        auto& nuclear = d.nukeBuildings[b];
        if (nuclear.until > d.time) {
            if (!nuclear.dustStarted && nukeDustBudget) {
                nuclear.dustStarted = true; --nukeDustBudget;
                const auto& bb = d.buildingBox[b];
                const Vec3 out = normalize(toVec(bb.centre() - nuclear.blast.centre) + Vec3{0, 0.1f, 0});
                if (particlesOn()) warzone::pt::collapseDust(g_pt.sys, {bb.centre().x, bb.lo.y, bb.centre().z},
                    toV3(out), std::clamp(std::max(bb.size().x, bb.size().z) * 0.25f, 3.0f, 9.0f), tierAt(toVec(bb.centre()), 8), PT_WALL);
            }
             
            const bool launch = nukeVisualBudget && d.nukeDebris.size() < NUKE_DEBRIS_CAP &&
                (i == g_town.buildings[b].first || warzone::hash32(i) % nuclear.stride == 0);
            if (launch) {
                --nukeVisualBudget;
                Entity flying = e;
                const auto t = d.pieceXf[i];
                if (!tracked(ctx, flying)) {
                    if (flying.isValid()) d.pendingKill.push_back(flying);
                    flying = cb.createEntity();
                    cb.addComponent<TransformComponent>(flying, TransformComponent(t));
                    cb.addComponent<TransformMatrixComponent>(flying, TransformMatrixComponent{});
                    cb.addComponent<MeshComponent>(flying, pieceMesh(d.pieceType[i], 0));
                    cb.addComponent<MaterialComponent>(flying, materialOf(warzone::PIECES[d.pieceType[i]]));
                } else {
                    if (ctx.hasComponent<RigidbodyComponent>(flying)) cb.removeComponent<RigidbodyComponent>(flying);
                    if (ctx.hasComponent<BoxColliderComponent>(flying)) cb.removeComponent<BoxColliderComponent>(flying);
                    std::erase_if(d.bodies, [&](const auto& pending) { return pending.e == flying; });
                }
                const f32 distance = warzone::distance(box, nuclear.blast.centre);
                const f32 strength = std::clamp(1.0f - distance / nuclear.blast.radius, 0.0f, 1.0f);
                const bool structural = d.pieceType[i] == warzone::P_COLUMN || d.pieceType[i] == warzone::P_BEAM || d.pieceType[i] == warzone::P_SLAB;
                const bool panel = !structural && volume > 1 && (d.pieceMat[i] == warzone::Material::Masonry || d.pieceMat[i] == warzone::Material::Concrete);
                const f32 response = std::clamp(std::pow(2.0f / std::max(volume, 0.1f), 0.18f), 0.35f, 1.3f) *
                    (structural ? 0.42f : d.pieceMat[i] == warzone::Material::Steel ? 0.7f : 1.0f);
                const bool supportFall = warzone::len(r.velocity) < 0.1f;
                Vec3 velocity = toVec(warzone::nukeVelocity(box, nuclear.blast)) * response * (supportFall ? 0.2f : d.rng.range(0.75f, 1.15f));
                if (structural) velocity.y = std::min(velocity.y, 6 + 10 * strength);
                velocity += Vec3{d.rng.range(-3, 3), d.rng.range(-3, 2), d.rng.range(-3, 3)};
                const Vec3 offset{0, warzone::PIECES[d.pieceType[i]].sy * t.scale.y * 0.5f, 0};
                const auto& type = warzone::PIECES[d.pieceType[i]];
                d.nukeDebris.push_back({flying, t.position + rotate(t.rotation, offset), velocity, t.scale,
                    {type.sx * t.scale.x * 0.5f, type.sy * t.scale.y * 0.5f, type.sz * t.scale.z * 0.5f},
                    offset, normalize(Vec3{d.rng.range(-1, 1), d.rng.range(-1, 1), 0.5f}), t.rotation});
                auto& main = d.nukeDebris.back();
                main.spin = d.rng.range(0.4f, 2.4f) * (structural ? 0.45f : 1.0f);
                if (panel) {
                    main.scale *= 0.65f; main.half *= 0.65f; main.offset *= 0.65f;
                    const auto base = main;
                    for (u32 c = 0; c < 2 && nukeChunkBudget && d.nukeDebris.size() < NUKE_DEBRIS_CAP; ++c) {
                        --nukeChunkBudget;
                        auto chunk = base;
                        chunk.e = cb.createEntity();
                        chunk.pos += Vec3{d.rng.range(-1, 1), d.rng.range(-1, 1), d.rng.range(-1, 1)};
                        chunk.scale *= 0.7f; chunk.half *= 0.7f; chunk.offset *= 0.7f;
                        chunk.velocity = velocity * d.rng.range(0.85f, 1.3f) + Vec3{d.rng.range(-12, 12), d.rng.range(2, 14), d.rng.range(-12, 12)};
                        chunk.spin = d.rng.range(3, 7); chunk.drag = 0.08f;
                        TransformComponent tr;
                        tr.position = chunk.pos - rotate(chunk.rotation, chunk.offset); tr.rotation = chunk.rotation; tr.scale = chunk.scale;
                        cb.addComponent<TransformComponent>(chunk.e, std::move(tr));
                        cb.addComponent<TransformMatrixComponent>(chunk.e, TransformMatrixComponent{});
                        cb.addComponent<MeshComponent>(chunk.e, pieceMesh(d.pieceType[i], 0));
                        cb.addComponent<MaterialComponent>(chunk.e, materialOf(type));
                        d.nukeDebris.push_back(chunk);
                    }
                }
            } else if (tracked(ctx, e)) cb.destroyEntity(e);
            else if (e.isValid()) d.pendingKill.push_back(e);
            d.pieceEntity[i] = Entity::undefined();
            ++d.fallen;
            continue;
        }
         
         
        if (!tracked(ctx, e) || (d.buildingBlastUntil[b] <= frame && length(toVec(box.centre()) - g_listener) > 600.0f))
        {
            if (tracked(ctx, e))
                cb.destroyEntity(e);
            else if (e.isValid())
                d.pendingKill.push_back(e);
            d.pieceEntity[i] = Entity::undefined();
            ++(r.destroyed ? d.destroyed : d.fallen);
            ++d.farRemoved;
            continue;
        }
        if (!r.destroyed)
            d.buildingLastFall[b] = frame;
        if (!r.destroyed && !d.buildingRumbled[b])
        {
            d.buildingRumbled[b] = true;
            const auto& bb = d.buildingBox[b];
            playVoice(d.rumbleVoices, d.nextRumbleVoice, toVec(bb.centre()) + Vec3{0, -bb.size().y * 0.3f, 0}, 60, 1, wa::Category::Explosions);
        }
        const bool shatter = r.destroyed && volume > 0.6f && d.pieceMat[i] != warzone::Material::Steel;
        if (shatter && fragmentBudget > 0)
        {
            ++d.destroyed;
            const u32 n = std::min<u32>(fragmentBudget, std::clamp<u32>(static_cast<u32>(volume / 1.2f + 0.5f), 1u, 5u));
            fragmentBudget -= n;
            const auto* src = ctx.tryGetConstComponent<TransformComponent>(e);
            const MeshComponent mesh = pieceMesh(d.pieceType[i], d.buildingTier[g_town.pieces[i].building]);
            const MaterialComponent mat = materialOf(warzone::PIECES[d.pieceType[i]]);
            for (u32 k = 0; k < n; ++k)
            {
                const f32 s = d.rng.range(0.24f, 0.4f);
                TransformComponent t;
                t.position = {d.rng.range(box.lo.x, box.hi.x), d.rng.range(box.lo.y, box.hi.y - 0.2f), d.rng.range(box.lo.z, box.hi.z)};
                t.rotation = normalize(multiply(src ? src->rotation : Quat::identity(),
                                                fromAxisAngle(normalize(Vec3{d.rng.range(-1, 1), d.rng.range(-1, 1), d.rng.range(-1, 1)}),
                                                              d.rng.range(0.0f, 1.2f))));
                t.scale = d.pieceScale[i] * s;
                const Entity f = cb.createEntity();
                cb.addComponent<TransformComponent>(f, std::move(t));
                cb.addComponent<TransformMatrixComponent>(f, TransformMatrixComponent{});
                cb.addComponent<MeshComponent>(f, MeshComponent(mesh));
                cb.addComponent<MaterialComponent>(f, MaterialComponent(mat));
                RigidbodyComponent body;
                const warzone::V3 fs = box.size() * s;
                makeDynamic(body, warzone::massOf({{0, 0, 0}, fs}, d.pieceMat[i]));
                cb.addComponent<RigidbodyComponent>(f, std::move(body));
                cb.addComponent<BoxColliderComponent>(f, colliderFor(d.pieceType[i], d.pieceScale[i] * s));
                EntityNameComponent label;
                label.name = InternedString("Debris");
                cb.addComponent<EntityNameComponent>(f, std::move(label));
                const Vec3 spin{d.rng.range(-4, 4), d.rng.range(-4, 4), d.rng.range(-4, 4)};
                const Vec3 jitter{d.rng.range(-1.5f, 1.5f), d.rng.range(0.0f, 2.0f), d.rng.range(-1.5f, 1.5f)};
                d.velocities.push_back({f, toVec(r.velocity) * d.rng.range(0.7f, 1.3f) + jitter, spin, frame + 3, 0});
                addDebris(f.id(), f, events);
                ++d.fragments;
            }
            cb.destroyEntity(e);
            if (dustBudget > 0)
            {
                --dustBudget;
                spawnFx(ctx, FX_DUST, toVec(box.centre()), 2.5f, 7.0f, 4.0f, {0, 0.8f, 0});
            }
        }
        else
        {
            ++d.fallen;
            RigidbodyComponent body = staticBody();
            makeDynamic(body, warzone::massOf(box, d.pieceMat[i]));
            swapBody(ctx, cb, e, body, frame);
            const Vec3 spin = r.destroyed || warzone::len(r.velocity) > 0.1f
                                  ? Vec3{d.rng.range(-2, 2), d.rng.range(-2, 2), d.rng.range(-2, 2)}
                                  : Vec3{d.rng.range(-1.0f, 1.0f), d.rng.range(-0.3f, 0.3f), d.rng.range(-1.0f, 1.0f)};
             
             
            const Vec3 fall = toVec(r.velocity) + Vec3{d.rng.range(-1.2f, 1.2f), -0.8f, d.rng.range(-1.2f, 1.2f)};
            d.velocities.push_back({e, fall, spin, frame + 4, 0});
            if (const auto* t0 = ctx.tryGetConstComponent<TransformComponent>(e))
                d.fallStart[e.id()] = t0->position;
            addDebris(e.id(), e, events);
        }
    }

     
    {
        size_t keep = 0;
        for (auto& b : d.bodies)
        {
            if (b.due > frame)
            {
                d.bodies[keep++] = b;
                continue;
            }
            if (tracked(ctx, b.e) && !ctx.hasComponent<RigidbodyComponent>(b.e))
                cb.addComponent<RigidbodyComponent>(b.e, RigidbodyComponent(b.body));
        }
        d.bodies.resize(keep);
    }

     
    for (u32 b = 0; b < d.buildingBox.size(); ++b)
    {
        if (d.nukeBuildings[b].until > d.time) continue;
        if (d.buildingLastFall[b] == 0 || frame - d.buildingLastFall[b] > 40 || frame < d.buildingNextDust[b])
            continue;
        d.buildingNextDust[b] = frame + 7;
        const auto& bb = d.buildingBox[b];
        const f32 edge = d.rng.f01();
        const bool alongX = d.rng.chance(0.5f);
        const Vec3 at = alongX ? Vec3{bb.lo.x + (bb.hi.x - bb.lo.x) * edge, bb.lo.y + 1.0f, d.rng.chance(0.5f) ? bb.lo.z : bb.hi.z}
                               : Vec3{d.rng.chance(0.5f) ? bb.lo.x : bb.hi.x, bb.lo.y + 1.0f, bb.lo.z + (bb.hi.z - bb.lo.z) * edge};
        const Vec3 out = normalize(at - toVec(bb.centre()) + Vec3{0, 0.1f, 0});
        if (particlesOn())  
            warzone::pt::collapseDust(g_pt.sys, toV3(at), warzone::V3{out.x, 0.0f, out.z}, 7.0f, tierAt(at, 8.0f), PT_WALL);
        else
            spawnFx(ctx, FX_DUST, at, 6.0f, 16.0f, 6.0f, Vec3{out.x * 3.0f, 1.2f, out.z * 3.0f});
    }

     
    if (ctx.services().contains<PhysicsWorld>() && !d.velocities.empty())
    {
        auto physics = ctx.services().write<PhysicsWorld>();
        size_t keep = 0;
        for (auto& v : d.velocities)
        {
            if (v.due > frame)
            {
                d.velocities[keep++] = v;
                continue;
            }
            if (tracked(ctx, v.e) && physics->hasBody(v.e))
            {
                physics->setLinearVelocity(v.e, v.linear);
                physics->setAngularVelocity(v.e, v.angular);
            }
            else if (++v.tries < 240)
                d.velocities[keep++] = v;
            else
                ++d.velocitiesDropped;
        }
        d.velocities.resize(keep);
    }

     
    {
         
         
        auto physics = ctx.services().contains<PhysicsWorld>() ? std::optional(ctx.services().write<PhysicsWorld>()) : std::nullopt;
        d.debris.update(
            dt,
            [&](u64 id)
            {
                const Entity e = d.debrisEntity[id];
                if (!tracked(ctx, e))
                    return true;
                const auto* state = ctx.tryGetConstComponent<RigidbodyStateComponent>(e);
                if (auto it = d.fallStart.find(id); it != d.fallStart.end() && physics)
                    if (const auto* t = ctx.tryGetConstComponent<TransformComponent>(e))
                    {
                        const f32 ground = g_terrain.valid() ? g_terrain.height(t->position.x, t->position.z) : 0.0f;
                        if (length(t->position - it->second) < 0.5f && t->position.y - ground > 1.0f)
                        {
                            if ((*physics)->hasBody(e) && (!state || !state->awake))
                            {
                                const f32 a = static_cast<f32>(warzone::hash32(static_cast<u32>(id) + d.hangKicks) % 628) * 0.01f;
                                (*physics)->setLinearVelocity(e, {std::cos(a) * 2.0f, -1.0f, std::sin(a) * 2.0f});
                                (*physics)->setAngularVelocity(e, {std::sin(a) * 1.5f, 0.5f, std::cos(a) * 1.5f});
                                ++d.hangKicks;
                            }
                            return true;
                        }
                    }
                return !state || state->awake;
            },
            events);
    }
    u32 settleDust = 2;
    for (const auto& ev : events)
    {
        const Entity e = d.debrisEntity[ev.id];
        if (!tracked(ctx, e))
        {
            if (ev.to == warzone::DebrisState::Gone)
                d.debrisEntity.erase(ev.id), d.fading.erase(ev.id);
            continue;  
        }
        switch (ev.to)
        {
        case warzone::DebrisState::Static:
            swapBody(ctx, cb, e, staticBody(), frame);
            if (settleDust > 0 && d.rng.chance(0.12f))
                if (const auto* t = ctx.tryGetConstComponent<TransformComponent>(e))
                {
                    --settleDust;
                    spawnFx(ctx, FX_DUST, t->position + Vec3{0, 0.5f, 0}, 2.0f, 5.0f, 3.0f, {0, 0.4f, 0});
                }
            break;
        case warzone::DebrisState::Fading:
            if (const auto* t = ctx.tryGetConstComponent<TransformComponent>(e))
                d.fading[ev.id] = {t->position, t->scale, true};
            if (ctx.hasComponent<RigidbodyComponent>(e))
                cb.removeComponent<RigidbodyComponent>(e);
            std::erase_if(d.bodies, [&](const PendingBody& b) { return b.e == e; });  
            break;
        case warzone::DebrisState::Gone:
            d.fallStart.erase(ev.id);
            cb.destroyEntity(e);
            d.fading.erase(ev.id);
            d.debrisEntity.erase(ev.id);
            break;
        default:
            break;
        }
    }
    for (auto& [id, info] : d.fading)
    {
        const f32 u = d.debris.fade(id);
        const Entity e = d.debrisEntity[id];
        if (!tracked(ctx, e))
            continue;
        if (auto* t = ctx.tryGetComponent<TransformComponent>(e))
        {
            t->scale = info.scale * (1.0f - 0.8f * u);
            t->position = info.pos - Vec3{0.0f, 1.5f * u, 0.0f};
        }
    }
    d.maxLiveDebris = std::max(d.maxLiveDebris, d.debris.live());

    size_t flightsKept = 0;
    for (auto flight : d.nukeDebris) {
        flight.age += dt;
        if (flight.age >= 30) {
            if (tracked(ctx, flight.e)) cb.destroyEntity(flight.e);
            else d.pendingKill.push_back(flight.e);
            continue;
        }
        if (!flight.settled) {
            flight.pos += flight.velocity * dt + Vec3{0, -4.905f * dt * dt, 0};
            flight.velocity.y -= 9.81f * dt;
            flight.velocity *= std::exp(-flight.drag * dt);
            flight.rotation = normalize(multiply(fromAxisAngle(flight.axis, flight.spin * dt), flight.rotation));
            const f32 extent = std::abs(rotate(flight.rotation, {flight.half.x, 0, 0}).y) +
                std::abs(rotate(flight.rotation, {0, flight.half.y, 0}).y) + std::abs(rotate(flight.rotation, {0, 0, flight.half.z}).y);
            const f32 ground = g_terrain.valid() ? g_terrain.height(flight.pos.x, flight.pos.z) : 0;
            if (flight.pos.y < ground + extent && flight.velocity.y < 0) {
                flight.pos.y = ground + extent;
                flight.velocity.y *= -0.18f;
                flight.velocity.x *= 0.6f; flight.velocity.z *= 0.6f;
                if (length(flight.velocity) < 3) { flight.velocity = {}; flight.settled = true; }
            }
        }
        if (auto* t = ctx.tryGetComponent<TransformComponent>(flight.e)) {
            t->position = flight.pos - rotate(flight.rotation, flight.offset);
            t->rotation = flight.rotation;
            t->scale = flight.scale * std::clamp((30 - flight.age) / 3, 0.0f, 1.0f);
        }
        d.nukeDebris[flightsKept++] = flight;
    }
    d.nukeDebris.resize(flightsKept);

     
    updateFx(ctx, dt);
}

void scheduleBlast(const warzone::Blast& b, u32 delayFrames, bool effects = true)
{
    std::scoped_lock lock(g_destructionMutex);
    g_destruction.scheduled.push_back({g_destruction.frame + 1 + delayFrames, b, effects});
}

void scheduleNukeDestruction(const warzone::Blast& b)
{
    std::scoped_lock lock(g_destructionMutex);
    g_destruction.scheduled.push_back({g_destruction.frame + 1, b, false, true});
}

void scheduleGigaImpact(u32 piece, const warzone::V3& velocity)
{
    std::scoped_lock lock(g_destructionMutex);
    auto& d = g_destruction;
    if (std::any_of(d.scheduled.begin(), d.scheduled.end(), [&](const auto& s) { return s.piece == piece; })) return;
    ScheduledBlast impact{d.frame + 1, {}, false};
    impact.piece = piece;
    impact.velocity = velocity;
    d.scheduled.push_back(impact);
}

void registerDestructionSystem(World world)
{
    SystemDescriptor sys;
    sys.moduleName = MODULE;
    sys.name = "WarzoneDestruction";
    sys.fn = destructionSystem;
    sys.update = SystemPhaseDescriptor{
        .reads = {getComponentId<RigidbodyStateComponent>(), getComponentId<WarzonePiece>(), getComponentId<MeshComponent>(),
                  getComponentId<MaterialComponent>()},
        .writes = {getComponentId<RigidbodyComponent>(), getComponentId<TransformComponent>(), getComponentId<TransformMatrixComponent>(),
                   getComponentId<FlipbookComponent>(), getComponentId<RenderTintComponent>(),
                   getComponentId<EntityNameComponent>(),
                   getComponentId<PointLightComponent>(), getComponentId<BoxColliderComponent>(), getComponentId<AudioEmitterComponent>()}};
    world.registerSystem(sys);
}

void registerDestructionCommands(const ModuleContext& context)
{
    auto& control = context.control;
    const std::string_view owner = MODULE;
    control.registerCommand(
        "warzoneBodySwap", owner,
        "how a falling piece changes body: direct=true edits its RigidbodyComponent in place (SDK 0.2.5), false removes the "
        "body one frame and adds the new one the next; answers the mode in force",
        {optionalArg("direct", ArgKind::Boolean, "edit in place")},
        [](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            if (auto v = args.boolean("direct"))
                g_directBodySwap = *v;
            result.writeBool("direct", g_directBodySwap);
            return {};
        });
    control.registerCommand(
        "warzoneExplode", owner,
        "detonates a charge in the Play world: damages the building pieces it overlaps (falloff (1-d/r)^2, overflow spreads "
        "through the support graph), frees and pushes them, effects and sound",
        {requiredArg("position", ArgKind::Json, "[x,y,z] world metres"), optionalArg("radius", ArgKind::Number, "metres", "4"),
         optionalArg("damage", ArgKind::Number, "HP at the centre", "900"), optionalArg("push", ArgKind::Number, "m/s at the centre", "14"),
         optionalArg("delay", ArgKind::Integer, "frames before it goes off", "0")},
        [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            if (!control.world("play") || !g_destruction.ready)
                return "warzoneExplode needs Play running (destruction lives in the play world)";
            f32 x = 0, y = 0, z = 0;
            const auto pos = args.json("position").value_or("");
            if (std::sscanf(pos.c_str(), "[%f,%f,%f]", &x, &y, &z) != 3)
                return "position must be [x,y,z]";
            warzone::Blast b;
            b.centre = {x, y, z};
            b.radius = number(args, "radius").value_or(4.0f);
            b.damage = number(args, "damage").value_or(900.0f);
            b.push = number(args, "push").value_or(14.0f);
            scheduleBlast(b, static_cast<u32>(args.integer("delay").value_or(0)));
            std::vector<u32> hits;
            g_destruction.structure.overlapSphere(b.centre, b.radius, hits);
            result.writeU32("piecesInRadius", static_cast<u32>(hits.size()));
            return {};
        });

    control.registerCommand(
        "warzoneVfx", owner,
        "tunes and reads the effects (Phase 7 track 3): windDir (degrees, the way it blows toward, 0 = +X, 90 = +Z), windSpeed (m/s "
        "near the ground), gust (0..1), tier (pins every effect's distance tier 0 near .. 3 distant, 4 auto), budget (cards per "
        "frame before the governor demotes new effects); round 8 GPU particles: particles (on / off: the cards are the fallback), "
        "quality 0..3, ptSun / ptAmbient / ptScatter / ptSoft (look), ptCap / ptBudget, reloadAtlases; answers the values in "
        "force, live cards by kind and the counters, and the particle counters (ptLive, ptDrawn, ptSpawnedPerSecond, ptDraws, "
        "ptUploadBytes, ptCappedTotal, ptBudgetDroppedTotal, ptAtlasesReady, ptGpu, ptAtlasLog)",
        {optionalArg("windDir", ArgKind::Number, "degrees"), optionalArg("windSpeed", ArgKind::Number, "m/s"),
         optionalArg("gust", ArgKind::Number, "0..1"), optionalArg("tier", ArgKind::Integer, "0..3 pinned, 4 auto"),
         optionalArg("budget", ArgKind::Integer, "cards per frame"),
         optionalArg("particles", ArgKind::Boolean, "GPU particles on (the cards are the fallback)"),
         optionalArg("quality", ArgKind::Integer, "particles 0..3 (count, size, soft)"),
         optionalArg("ptSun", ArgKind::Number, "particles: sun light strength"), optionalArg("ptAmbient", ArgKind::Number, "particles: sky light"),
         optionalArg("ptScatter", ArgKind::Number, "particles: back-scatter toward the sun"),
         optionalArg("ptSoft", ArgKind::Number, "particles: soft depth fade scale"), optionalArg("ptCap", ArgKind::Integer, "live particles at most"),
         optionalArg("ptBudget", ArgKind::Integer, "particle spawns a frame"),
         optionalArg("reloadAtlases", ArgKind::Boolean, "reload baked particle atlases and atlas metadata")},
        [](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            auto& d = g_destruction;
            if (!d.ready)
                return "warzoneVfx needs Play running";
            if (auto v = number(args, "windDir"))
            {
                d.wind.dirX = std::cos(*v * 0.0174533f);
                d.wind.dirZ = std::sin(*v * 0.0174533f);
            }
            if (auto v = number(args, "windSpeed"))
                d.wind.speed = std::max(0.0f, *v);
            if (auto v = number(args, "gust"))
                d.wind.gust = std::clamp(*v, 0.0f, 1.0f);
            if (auto v = args.integer("tier"))
                d.forceTier = *v >= 4 ? -1 : static_cast<int>(std::clamp<long long>(static_cast<long long>(*v), 0, 3));
            if (auto v = args.integer("budget"))
                d.governor.budget = static_cast<int>(std::clamp<long long>(static_cast<long long>(*v), 1, 400));
            ptApplyArgs(args);
            ptWriteStats(result);
            std::array<u32, FX_KINDS> live{};
            for (const auto& f : d.fx)
                live[f.kind] += f.age >= 0.0f;
            static constexpr const char* NAMES[FX_KINDS] = {"dust", "smoke", "fire", "flash", "spark", "puff", "shock", "debris", "flame", "wash"};
            for (int k = 0; k < FX_KINDS; ++k)
                result.writeU32(std::format("live_{}", NAMES[k]), live[k]);
            result.writeF32("windDir", std::atan2(d.wind.dirZ, d.wind.dirX) * 57.2958f);
            result.writeF32("windSpeed", d.wind.speed);
            result.writeF32("gust", d.wind.gust);
            result.writeI32("tier", d.forceTier);
            result.writeI32("budget", d.governor.budget);
            result.writeU32("tierNear", d.vfxTier[0]);
            result.writeU32("tierMid", d.vfxTier[1]);
            result.writeU32("tierFar", d.vfxTier[2]);
            result.writeU32("tierDistant", d.vfxTier[3]);
            result.writeU32("demoted", d.vfxDemoted);
            result.writeU32("cardsSpawned", d.vfxCards);
            for (int k = 0; k < FX_KINDS; ++k)
                result.writeU32(std::format("spawned_{}", NAMES[k]), d.vfxByKind[k]);
            result.writeU32("cardsStolen", d.vfxStolen);
            result.writeU32("delayedWaiting", static_cast<u32>(d.delayed.n));
            result.writeU32("delayedDropped", d.delayed.dropped);
            return {};
        });

    control.registerCommand(
        "warzoneStream", owner,
        "tunes the building streamer live (the next frame uses it): radiusIn/radiusOut/budget for resident pieces, detailIn/"
        "detailOut/detailBudget for the full-detail kit; gameTiers=false uses full meshes and skips tier work (change only "
        "with Play stopped); streaming and far stand-ins remain. Answers the values now in force",
        {optionalArg("radiusIn", ArgKind::Number, "metres"), optionalArg("radiusOut", ArgKind::Number, "metres"),
         optionalArg("budget", ArgKind::Integer, "resident pieces at most"), optionalArg("detailIn", ArgKind::Number, "metres"),
         optionalArg("detailOut", ArgKind::Number, "metres"), optionalArg("detailBudget", ArgKind::Integer, "full-detail pieces at most"),
         optionalArg("lowIn", ArgKind::Number, "metres: coarse kit beyond"), optionalArg("lowOut", ArgKind::Number, "metres"),
         optionalArg("budgetIn", ArgKind::Integer, "pieces made a frame"), optionalArg("budgetOut", ArgKind::Integer, "pieces destroyed a frame"),
         optionalArg("gameTiers", ArgKind::Boolean, "full/simplified/coarse game kits; change with Play stopped"),
         optionalArg("authoredOnly", ArgKind::Boolean, "temporary capture mode: pin authored buildings in full detail independent of cameras; change with Play stopped")},
        [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            if (args.boolean("authoredOnly").value_or(g_captureBuildings) && args.boolean("gameTiers").value_or(false))
                return "authoredOnly capture requires gameTiers=false";
            if (auto capture = args.boolean("authoredOnly")) {
                if (*capture != g_captureBuildings && control.world("play")) return "stop Play before changing authoredOnly";
                if (*capture) {
                    u32 pieces = 0;
                    if (const auto* scene = control.world("scene")) {
                        u32 seed = 7; findTownRoot(*scene, &seed); townPlan(seed);
                        std::vector<u8> seen(g_town.buildings.size(), 0);
                        auto buildings = scene->createQueryBuilder().buildQuery<Read<WarzoneBuilding>>();
                        for (const auto& b : buildings->view<Read<WarzoneBuilding>>())
                            if (b.id < seen.size() && !seen[b.id]) { seen[b.id] = 1; pieces += g_town.buildings[b.id].count; }
                    }
                    if (pieces > STREAM_BODY_CAP) return "authored capture exceeds the 48000-piece physics budget";
                    g_gameTiers = false;
                }
                g_captureBuildings = *capture;
            }
            if (auto v = args.boolean("gameTiers"))
            {
                if (*v != g_gameTiers && control.world("play"))
                    return "stop Play before changing gameTiers; the next Play creates every piece and fragment with the chosen kit";
                g_gameTiers = *v;
            }
            if (auto v = number(args, "radiusIn"))
                g_stream.radiusIn = *v;
            if (auto v = number(args, "radiusOut"))
                g_stream.radiusOut = *v;
            if (auto v = args.integer("budget"))
                g_stream.pieceBudget = std::min<u32>(static_cast<u32>(*v), STREAM_BODY_CAP);
            if (auto v = number(args, "detailIn"))
                g_detail.radiusIn = *v;
            if (auto v = number(args, "detailOut"))
                g_detail.radiusOut = *v;
            if (auto v = args.integer("detailBudget"))
                g_detail.pieceBudget = static_cast<u32>(*v);
            if (auto v = number(args, "lowIn"))
                g_tier.lowIn = *v;
            if (auto v = number(args, "lowOut"))
                g_tier.lowOut = *v;
            if (auto v = args.integer("budgetIn"))
                g_streamInPerFrame = std::max<u32>(1, static_cast<u32>(*v));
            if (auto v = args.integer("budgetOut"))
                g_streamOutPerFrame = std::max<u32>(1, static_cast<u32>(*v));
            result.writeU32("budgetIn", g_streamInPerFrame);
            result.writeBool("gameTiers", g_gameTiers);
            result.writeBool("authoredOnly", g_captureBuildings);
            result.writeU32("budgetOut", g_streamOutPerFrame);
            result.writeF32("lowIn", g_tier.lowIn);
            result.writeF32("lowOut", g_tier.lowOut);
            result.writeF32("radiusIn", g_stream.radiusIn);
            result.writeF32("radiusOut", g_stream.radiusOut);
            result.writeU32("budget", g_stream.pieceBudget);
            result.writeF32("detailIn", g_detail.radiusIn);
            result.writeF32("detailOut", g_detail.radiusOut);
            result.writeU32("detailBudget", g_detail.pieceBudget);
            return {};
        });

    control.registerCommand(
        "warzoneCharge", owner,
        "scripted demolition in the Play world: charges on every column and wall of one storey of a building, set off one "
        "after another; whatever loses its support comes down in a wave",
        {requiredArg("building", ArgKind::String, "building name (warzoneTownPlan) or id"),
         optionalArg("floor", ArgKind::Integer, "storey", "0"), optionalArg("interval", ArgKind::Integer, "frames between charges", "3"),
         optionalArg("delay", ArgKind::Integer, "frames before the first", "0")},
        [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            if (!control.world("play") || !g_destruction.ready)
                return "warzoneCharge needs Play running";
            const std::string name = args.string("building").value_or("");
            const auto& town = g_town;
            int id = -1;
            for (const auto& b : town.buildings)
                if (b.name == name || std::to_string(b.id) == name)
                    id = b.id;
            if (id < 0)
                return std::format("no building '{}' in the town plan", name);
            const u32 floor = static_cast<u32>(args.integer("floor").value_or(0));
            const u32 interval = static_cast<u32>(args.integer("interval").value_or(3));
            u32 delay = static_cast<u32>(args.integer("delay").value_or(0));
            std::vector<std::pair<u32, warzone::Box>> targets;
            const auto& spec = town.buildings[id];
            for (u32 i = spec.first; i < spec.first + spec.count; ++i)
            {
                const auto& n = g_destruction.structure.node(i);
                if (town.pieces[i].floor != floor || town.pieces[i].type == warzone::P_SLAB || n.state != warzone::PieceState::Intact)
                    continue;
                targets.emplace_back(i, n.box);
            }
            std::sort(targets.begin(), targets.end(), [](auto& a, auto& b) { return a.first < b.first; });
            warzone::Rng rng(static_cast<u64>(id) * 31 + floor);
            for (size_t i = targets.size(); i > 1; --i)  
                std::swap(targets[i - 1], targets[rng.next() % i]);
            for (const auto& [i, box] : targets)
            {
                warzone::Blast b;
                b.centre = box.centre();
                b.radius = 1.6f;
                b.damage = g_destruction.structure.node(i).maxHp * 1.4f;
                b.push = 5.0f;
                scheduleBlast(b, delay);
                delay += interval;
            }
            result.writeU32("charges", static_cast<u32>(targets.size()));
            result.writeU32("lastFrame", delay);
            result.writeString("building", town.buildings[id].name);
            return {};
        });

    control.registerQuery(
        "warzoneDestruction", owner,
        "the Play world's destruction state: pieces, collapse queue, debris by state, effects, bodies; building=<name> adds "
        "that building's intact/removed counts and where its fallen pieces lie now (highest top above its ground)",
        {optionalArg("building", ArgKind::String, "building name or id")},
        [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            const auto& d = g_destruction;
            if (!d.ready)
                return "no destruction state (start Play)";
            if (auto name = args.string("building"))
            {
                int id = -1;
                for (const auto& b : g_town.buildings)
                    if (b.name == *name || std::to_string(b.id) == *name)
                        id = b.id;
                if (id < 0)
                    return std::format("no building '{}'", *name);
                const World* play = control.world("play");
                u32 intact = 0, removed = 0, present = 0;
                f32 intactTop = -1e9f, debrisTop = -1e9f, ground = 1e9f;
                for (u32 i = 0; i < d.structure.size(); ++i)
                {
                    const auto& n = d.structure.node(i);
                    if (n.building != static_cast<u32>(id))
                        continue;
                    ground = std::min(ground, n.box.lo.y);
                    if (n.state == warzone::PieceState::Intact)
                    {
                        ++intact;
                        intactTop = std::max(intactTop, n.box.hi.y);
                        continue;
                    }
                    ++removed;
                    const Entity e = d.pieceEntity[i];
                    if (play && play->isEntityValid(e))
                        if (const auto* t = play->tryGetComponent<TransformComponent>(e))
                        {
                            ++present;
                            debrisTop = std::max(debrisTop, t->position.y);
                        }
                }
                result.writeString("building", g_town.buildings[id].name);
                result.writeU32("buildingIntact", intact);
                result.writeU32("buildingRemoved", removed);
                result.writeU32("fallenPresent", present);
                result.writeF32("intactTopAboveGround", intact ? intactTop - ground : 0.0f);
                result.writeF32("fallenHighestAboveGround", present ? debrisTop - ground : 0.0f);
                result.writeF32("originalHeight", d.buildingBox[id].hi.y - d.buildingBox[id].lo.y);
                 
                std::vector<std::pair<f32, u32>> tops;
                for (u32 i = 0; i < d.structure.size(); ++i)
                {
                    const auto& n = d.structure.node(i);
                    if (n.building != static_cast<u32>(id) || n.state == warzone::PieceState::Intact || !play || !play->isEntityValid(d.pieceEntity[i]))
                        continue;
                    if (const auto* t = play->tryGetComponent<TransformComponent>(d.pieceEntity[i]))
                        tops.emplace_back(t->position.y - ground, i);
                }
                std::sort(tops.begin(), tops.end(), [](auto& a, auto& b) { return a.first > b.first; });
                tops.resize(std::min<size_t>(tops.size(), 6));
                result.beginArray("highestFallen", static_cast<u32>(tops.size()));
                for (auto [h, i] : tops)
                {
                    const Entity e = d.pieceEntity[i];
                    result.beginObject({});
                    result.writeU64("entity", e.id());
                    result.writeF32("aboveGround", h);
                    const auto* rb = play->tryGetComponent<RigidbodyComponent>(e);
                    result.writeString("body", rb ? (rb->bodyType == RigidbodyComponent::BodyType::Dynamic ? "dynamic" : rb->bodyType == RigidbodyComponent::BodyType::Static ? "static" : "kinematic") : "none");
                    const auto* st = play->tryGetComponent<RigidbodyStateComponent>(e);
                    result.writeBool("awake", st && st->awake);
                    result.writeBool("debris", d.debrisEntity.count(e.id()) > 0);
                    result.endObject();
                }
                result.endArray();
            }
            u32 intact = 0, scheduled = 0;
            for (u32 i = 0; i < d.structure.size(); ++i)
            {
                intact += d.structure.node(i).state == warzone::PieceState::Intact ? 1 : 0;
                scheduled += d.structure.node(i).state == warzone::PieceState::Scheduled ? 1 : 0;
            }
            u32 fxActive = 0;
            for (const auto& f : d.fx)
                fxActive += f.age >= 0.0f ? 1 : 0;
            result.writeU32("frame", d.frame);
            result.writeU32("pieces", static_cast<u32>(d.structure.size()));
            result.writeU32("intact", intact);
            result.writeU32("scheduled", scheduled);
            result.writeU32("blasts", d.blasts);
            result.writeU32("destroyed", d.destroyed);
            result.writeU32("fallen", d.fallen);
            result.writeU32("farRemoved", d.farRemoved);
            result.writeU32("buildings", static_cast<u32>(d.buildingNear.size()));
            result.writeU32("residentBuildings", d.residentBuildings);
            result.writeU32("buildingsAsBoxes", static_cast<u32>(d.buildingNear.size()) - d.residentBuildings);
            result.writeU32("residentPieces", d.residentPieces);
            result.writeU32("streamedIn", d.streamedIn);
            result.writeU32("streamedOut", d.streamedOut);
            result.writeU32("deferredBuildings", d.deferredBuildings);
            result.writeU32("detailBuildings", d.detailBuildings);
            result.writeU32("detailPieces", d.detailPieces);
            result.writeU32("lowBuildings", d.lowBuildings);
            result.writeU32("lowPieces", d.lowPieces);
            result.writeU32("detailSwaps", d.detailSwaps);
            result.writeBool("gameTiers", g_gameTiers);
            result.writeU32("detailPending", d.detailPending);
            result.writeU32("pendingKill", static_cast<u32>(d.pendingKill.size()));
            result.writeU32("blastsDeferred", d.blastsDeferred);
            result.writeU32("fragments", d.fragments);
            result.writeU32("debrisDynamic", d.debris.count(warzone::DebrisState::Dynamic));
            result.writeU32("debrisStatic", d.debris.count(warzone::DebrisState::Static));
            result.writeU32("debrisFading", d.debris.count(warzone::DebrisState::Fading));
            result.writeU32("debrisCap", d.debris.params.cap);
            result.writeU32("nukeDebris", static_cast<u32>(d.nukeDebris.size()));
            result.writeU32("nukeDebrisCap", NUKE_DEBRIS_CAP);
            u32 nukePending = 0;
            for (const auto& work : d.nukeWork) nukePending += static_cast<u32>(work.pieces.size() - work.next);
            result.writeU32("nukePiecesPending", nukePending);
            result.writeU32("maxLiveDebris", d.maxLiveDebris);
            result.writeU32("maxFallsInFrame", d.maxFallsInFrame);
            result.writeU32("velocitiesDropped", d.velocitiesDropped);
            result.writeU32("hangKicks", d.hangKicks);
            result.writeU32("velocitiesPending", static_cast<u32>(d.velocities.size()));
            result.writeU32("effectsActive", fxActive);
            result.writeU32("effectCards", static_cast<u32>(d.fx.size()));
            if (const World* play = control.world("play"); play && play->services().contains<PhysicsWorld>())
            {
                const auto physics = play->services().read<PhysicsWorld>();
                result.writeU32("bodies", physics->bodyCount());
                result.writeU32("activeBodies", physics->activeBodyCount());
            }
            return {};
        });
}
