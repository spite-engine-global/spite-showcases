 
 
 

constexpr auto PARTICLES_VS = "./shaders/bin/wz_particles.vert.spv";
constexpr auto PARTICLES_FS = "./shaders/bin/wz_particles.frag.spv";
constexpr u32 PT_VIEWS = 32;
constexpr u32 PT_CONST_BYTES = 128;  
constexpr u32 PT_MAX = 40000;        
constexpr u32 PT_SLOTS = 4;

struct PtWorldLife  
{
    ~PtWorldLife();
};

struct ParticleState
{
    warzone::pt::System sys;
    struct JetExhaust { warzone::V3 left, right, forward; f32 throttle, time, dt; warzone::vfx::Tier tier; bool player; };
    std::vector<JetExhaust> jets;
    std::atomic<bool> on{true}, alive{false};
    std::atomic<int> quality{3};
     
    std::mutex mutex;
    std::vector<warzone::pt::GpuParticle> back, front;
    u32 frontCount = 0;
    u64 publish = 0;
    std::atomic<bool> metadataReady{false}, reloadWanted{false};
    bool texturesOwned = false;
    std::array<std::atomic<u32>, warzone::pt::ATLASES> tex{};  
    std::array<warzone::pt::AtlasInfo, warzone::pt::ATLASES> atlasInfo = warzone::pt::ATLAS_DEFAULTS;
    std::atomic<u32> atlasesReady{0};
    std::string loadLog;
    std::string gpuNote = "waiting for the atlases";  
     
    std::array<BufferHandle, PT_SLOTS> buffers{};
    std::array<u8*, PT_SLOTS> mapped{};
    std::array<u64, PT_SLOTS> slotPublish{};
    std::array<u32, PT_SLOTS> slotCount{};
    std::array<bool, PT_VIEWS> viewHasDepth{};
         
    f32 sunStrength = 1.0f, ambient = 0.6f, backscatter = 0.9f, softScale = 1.0f;
     
    std::atomic<u32> draws{0}, drawn{0}, uploadBytes{0};
    f32 spawnRate = 0.0f;
    f32 lastBuildMs = 0.0f;
};
ParticleState g_pt;

PtWorldLife::~PtWorldLife() { g_pt.alive = false; }

 
 
bool particlesOn()
{
    return g_pt.on.load(std::memory_order_relaxed) && g_pt.atlasesReady.load(std::memory_order_relaxed) == warzone::pt::ATLASES;
}

 
void particlesSetQuality(int level) { g_pt.quality = std::clamp(level, 0, 3); }
int particlesQuality() { return g_pt.quality.load(); }

const warzone::V3 PT_DUST{0.70f, 0.65f, 0.57f};   
const warzone::V3 PT_WALL{0.74f, 0.73f, 0.70f};   
const warzone::V3 PT_ASH{0.45f, 0.44f, 0.43f};

 
void ptReadAtlasMetadata()
{
    auto infos = warzone::pt::ATLAS_DEFAULTS;
    std::string log;
    if (std::ifstream in{resolveContentPath("assets/vfx/particles/atlases.txt")})
        for (std::string line; std::getline(in, line);)
            if (!line.empty() && line[0] != '#' && !warzone::pt::parseAtlasLine(line, infos))
                log += std::format("bad atlas metadata: {}; ", line);
    std::scoped_lock lock(g_pt.mutex);
    g_pt.atlasInfo = infos;
    g_pt.loadLog = log;
    g_pt.metadataReady = true;
}

AssetHandle ptAtlasHandle(int atlas, bool normal)
{
    return textureAssetHandle(InternedString(std::format("wz_pt_{}_{}", warzone::pt::ATLAS_NAMES[atlas], normal ? "normal" : "color")),
                              normal ? TextureColorSpace::LINEAR : TextureColorSpace::SRGB);
}

 
void ptAttach(const ModuleContext& context, World world)
{
    world.services().registerService(std::make_shared<PtWorldLife>(), context.owner);
    g_pt.sys.clear();
    g_pt.jets.clear();
    g_pt.sys.stats = {};
    g_pt.alive = true;
}

void ptUpdate(SystemContext& ctx, f32 dt, const Vec3& eye, const warzone::vfx::Wind& wind)
{
    auto& s = g_pt.sys;
    if (s.quality != g_pt.quality.load())
        s.setQuality(g_pt.quality.load());
    if (!particlesOn())
    {
        if (!s.parts.empty())
            s.clear();
        std::scoped_lock lock(g_pt.mutex);
        g_pt.frontCount = 0;
        ++g_pt.publish;
        return;
    }
    s.wind = wind;
    if (g_pt.metadataReady)
    {
        std::scoped_lock lock(g_pt.mutex);
        s.atlas = g_pt.atlasInfo;
    }
    const auto ground = [](f32 x, f32 z) { return g_terrain.valid() ? g_terrain.height(x, z) : -1.0e9f; };
    s.viewer = {eye.x, eye.y, eye.z};
    s.begin(dt);
    ctx.parallelFor(static_cast<u32>(s.parts.size()), 4096, [&](u32 b, u32 e) { s.simulate(b, e, dt, ground); });
    s.finish(dt);
}

void queueJetExhaust(const warzone::V3& left, const warzone::V3& right, const warzone::V3& forward,
                     f32 throttle, f32 time, f32 dt, warzone::vfx::Tier tier, bool player = false)
{
    g_pt.jets.push_back({left, right, forward, throttle, time, dt, tier, player});
}

void ptPublishSystem(SystemContext& ctx)
{
    auto& s = g_pt.sys;
    if (!particlesOn()) { g_pt.jets.clear(); return; }
     
    std::erase_if(s.parts, [](const auto& p) {
        return p.style == warzone::pt::S_JET_CORE || p.style == warzone::pt::S_JET_PLUME || p.style == warzone::pt::S_JET_DIAMOND;
    });
    for (const auto& jet : g_pt.jets) {
        warzone::pt::PlayerEffects playerFx(s, jet.player);
        warzone::pt::jetExhaust(s, jet.left, jet.right, jet.forward, jet.throttle, jet.time, jet.dt,
            jet.player ? warzone::vfx::NEAR : jet.tier);
    }
    g_pt.jets.clear();
    const f32 dt = std::min(ctx.deltaTime, 0.1f);
    std::array<u32, warzone::pt::ATLASES> tex{};
    for (int i = 0; i < warzone::pt::ATLASES; ++i)
        tex[i] = static_cast<u32>(i) | (((g_pt.tex[i].load() >> 16) != 0xFFFFu ? static_cast<u32>(i) : 0xFFFFu) << 16);
    {
        std::scoped_lock lock(g_lookMutex);
        const auto sun = normalize(g_look.sunDir);
        s.sunDirection = {sun.x, sun.y, sun.z};
    }
    s.prepare();
    const warzone::V3 e = s.viewer;
    ctx.parallelFor(static_cast<u32>(s.parts.size()), 4096, [&](u32 b, u32 en) { s.pack(b, en, e, tex); });
    g_pt.back.resize(PT_MAX);
    const u32 n = s.gather(g_pt.back.data(), std::min(PT_MAX, s.cap));
    if (dt > 0.0f)
        g_pt.spawnRate += (static_cast<f32>(s.stats.spawned) / dt - g_pt.spawnRate) * std::min(1.0f, dt * 2.0f);
    std::scoped_lock lock(g_pt.mutex);
    std::swap(g_pt.back, g_pt.front);
    g_pt.frontCount = n;
    ++g_pt.publish;
}

void registerParticlePublishSystem(World world)
{
    SystemDescriptor sys;
    sys.moduleName = MODULE; sys.name = "WarzoneParticlePublish"; sys.fn = ptPublishSystem;
    sys.update = SystemPhaseDescriptor{.stage = CoreExecutionStages::POST_UPDATE, .runsAfter = {{MODULE, "WarzoneCamera"}}};
    world.registerSystem(sys);
}

 
struct ParticlePush
{
    u32 depthIndex, flags, softScaleBits, backscatterBits;
    f32 sunX, sunY, sunZ, sunStrength;
    f32 sunR, sunG, sunB, ambient;
    f32 skyR, skyG, skyB, pad0;
    f32 fogR, fogG, fogB, sunGlow;
    f32 p22, p32, p11, pad1;
    f32 maxScreen, invWidth, invHeight, pad2;
};
static_assert(sizeof(ParticlePush) == 112);
constexpr u32 PT_CTL_HAS_DEPTH = 1, PT_CTL_SRGB = 2, PT_CTL_SOFT = 4, PT_CTL_CHEAP = 8;

PipelineHandle particlePipeline(RenderPassExecutionContext& context, Format format)
{
    GraphicsPipelineSpec spec;
    spec.depthTestEnable = false;
    spec.depthWriteEnable = false;
    spec.cullMode = CullMode::NONE;
    spec.topology = PrimitiveTopology::TRIANGLE_STRIP;
    spec.colorAttachmentFormats = {format};
    BlendStateDesc blend;  
    blend.blendEnable = true;
    blend.srcColorBlendFactor = BlendFactor::ONE;
    blend.dstColorBlendFactor = BlendFactor::ONE_MINUS_SRC_ALPHA;
    blend.srcAlphaBlendFactor = BlendFactor::ZERO;
    blend.dstAlphaBlendFactor = BlendFactor::ONE;
    spec.blendStates = {blend};
    spec.vertexBindings = {{.binding = 0, .stride = sizeof(warzone::pt::GpuParticle), .inputRate = VertexInputRate::INSTANCE},
                           {.binding = 1, .stride = 0, .inputRate = VertexInputRate::INSTANCE}};  
    spec.vertexAttributes = {{.location = 0, .binding = 0, .format = Format::R32G32B32A32_SFLOAT, .offset = 0},
                             {.location = 1, .binding = 0, .format = Format::R32G32B32A32_UINT, .offset = 16},
                             {.location = 2, .binding = 0, .format = Format::R32G32B32A32_SFLOAT, .offset = 32}};
    for (u32 k = 0; k < 8; ++k)
        spec.vertexAttributes.push_back({.location = 3 + k, .binding = 1, .format = Format::R32G32B32A32_SFLOAT, .offset = k * 16});
    spec.shaders = {
        {.path = InternedString(PARTICLES_VS), .stage = ShaderStage::VERTEX},
        {.path = InternedString(PARTICLES_FS), .stage = ShaderStage::FRAGMENT},
    };
    return context.services().write<ShaderManager>()->getOrCreatePipeline(spec);
}

void ptReleaseTextures(ServiceLocator& services, IBindlessResourceManager*)
{
    if (g_pt.texturesOwned && services.contains<AssetRuntime>())
    {
        auto assets = services.write<AssetRuntime>();
        for (int i = 0; i < warzone::pt::ATLASES; ++i)
            for (bool normal : {false, true})
                if (!normal || assets->manifest().getTextureEntry(ptAtlasHandle(i, true).name)) assets->release(ptAtlasHandle(i, normal));
    }
    g_pt.texturesOwned = false;
    g_pt.atlasesReady = 0;
    for (auto& index : g_pt.tex) index = 0xFFFFFFFFu;
}

void ptPumpTextures(RenderPassExecutionContext& context)
{
    auto& services = context.services();
    if (!services.contains<AssetRuntime>()) return;
    if (g_pt.reloadWanted.exchange(false))
    {
        ptReleaseTextures(services, nullptr);
        ptReadAtlasMetadata();
    }
    auto assets = services.write<AssetRuntime>();
    if (!g_pt.texturesOwned)
    {
        for (int i = 0; i < warzone::pt::ATLASES; ++i)
            for (bool normal : {false, true})
                if (!normal || assets->manifest().getTextureEntry(ptAtlasHandle(i, true).name)) assets->load(ptAtlasHandle(i, normal));
        g_pt.texturesOwned = true;
    }
    u32 ready = 0;
    for (int i = 0; i < warzone::pt::ATLASES; ++i)
    {
        const auto colour = assets->textureIndex(ptAtlasHandle(i, false).name, TextureColorSpace::SRGB);
        const auto normal = assets->textureIndex(ptAtlasHandle(i, true).name, TextureColorSpace::LINEAR);
        const bool available = colour && *colour < 0xFFFF;
        const u32 index = available ? *colour | ((normal && *normal < 0xFFFF ? *normal : 0xFFFFu) << 16) : 0xFFFFFFFFu;
        if (g_pt.tex[i].exchange(index) != index) g_pt.slotPublish.fill(~0ull);
        ready += available;
    }
    g_pt.atlasesReady = ready;
    std::scoped_lock lock(g_pt.mutex);
    g_pt.gpuNote = std::format("{} of {} borrowed baked atlases ready", ready, static_cast<int>(warzone::pt::ATLASES));
}

bool ptEnsureBuffer(RenderPassExecutionContext& context, u32 slot)
{
    if (slot >= PT_SLOTS)
        return false;
    if (g_pt.mapped[slot])
        return true;
    auto& services = context.services();
    if (!services.contains<IRenderResourceManager>())
        return false;
    auto rm = services.write<IRenderResourceManager>();
    BufferDesc bd;
    bd.size = PT_VIEWS * PT_CONST_BYTES + static_cast<sizet>(PT_MAX) * sizeof(warzone::pt::GpuParticle);
    bd.usage = BufferUsage::VERTEX_BUFFER;
    bd.memoryUsage = MemoryUsage::CPU_TO_GPU;
    g_pt.buffers[slot] = rm->createBuffer(bd, "WarzoneParticles");
    g_pt.mapped[slot] = static_cast<u8*>(rm->mapBuffer(g_pt.buffers[slot]));
    g_pt.slotPublish[slot] = ~0ull;
    return g_pt.mapped[slot] != nullptr;
}

void ptReleaseBuffers(ServiceLocator& services)
{
    for (u32 s = 0; s < PT_SLOTS; ++s)
    {
        if (g_pt.buffers[s].isValid() && services.contains<IRenderResourceManager>())
            services.write<IRenderResourceManager>()->destroyBuffer(g_pt.buffers[s]);
        g_pt.buffers[s] = {};
        g_pt.mapped[s] = nullptr;
    }
}

void ptDraw(RenderPassExecutionContext& context)
{
    ptPumpTextures(context);
    const u32 view = context.view();
    if (!g_pt.alive || !particlesOn())
        return;
    const u32 slot = context.frameIndex();
    if (!ptEnsureBuffer(context, slot))
        return;
    u8* base = g_pt.mapped[slot];
    {
        std::scoped_lock lock(g_pt.mutex);
        if (g_pt.slotPublish[slot] != g_pt.publish)
        {
            const u32 n = std::min(g_pt.frontCount, PT_MAX);
            std::memcpy(base + PT_VIEWS * PT_CONST_BYTES, g_pt.front.data(), n * sizeof(warzone::pt::GpuParticle));
            auto* instances = reinterpret_cast<warzone::pt::GpuParticle*>(base + PT_VIEWS * PT_CONST_BYTES);
            for (u32 i = 0; i < n; ++i)
                instances[i].tex = g_pt.tex[instances[i].tex & 0xFFFFu].load();
            g_pt.slotCount[slot] = n;
            g_pt.slotPublish[slot] = g_pt.publish;
            g_pt.uploadBytes = n * static_cast<u32>(sizeof(warzone::pt::GpuParticle));
        }
    }
    const u32 count = g_pt.slotCount[slot];
    if (count == 0 || view >= PT_VIEWS)
        return;
    const auto camera = passViewCamera(context);
    if (!camera) return;
    const auto& m = *camera;
    Look look;
    {
        std::scoped_lock lock(g_lookMutex);
        look = g_look;
    }
     
    f32* k = reinterpret_cast<f32*>(base + view * PT_CONST_BYTES);
    std::memcpy(k, m.viewProjection.elements, 64);
    k[16] = m.pos.x, k[17] = m.pos.y, k[18] = m.pos.z, k[19] = 0.0f;
    k[20] = m.view.elements[0], k[21] = m.view.elements[4], k[22] = m.view.elements[8], k[23] = 0.0f;  
    k[24] = m.view.elements[1], k[25] = m.view.elements[5], k[26] = m.view.elements[9], k[27] = 0.0f;  
    k[28] = look.hazeOn ? look.hazeDensity : 0.0f, k[29] = look.hazeFalloff, k[30] = look.hazeBase, k[31] = look.hazeMax;

    const Format format = Format::R16G16B16A16_SFLOAT;
    const auto& q = warzone::pt::QUALITY[std::clamp(g_pt.quality.load(), 0, 3)];
    ParticlePush p{};
    const bool depth = view < g_pt.viewHasDepth.size() && g_pt.viewHasDepth[view];
    p.flags = (depth ? PT_CTL_HAS_DEPTH : 0u) | (format == Format::B8G8R8A8_SRGB || format == Format::R8G8B8A8_SRGB ? PT_CTL_SRGB : 0u) |
              (q.soft ? PT_CTL_SOFT : 0u) | (g_pt.quality.load() == 0 ? PT_CTL_CHEAP : 0u);
    if (depth)
        p.depthIndex = context.textureIndex(DEPTH_NAME);
    p.softScaleBits = std::bit_cast<u32>(g_pt.softScale);
    p.backscatterBits = std::bit_cast<u32>(g_pt.backscatter);
    const Vec3 sun = normalize(look.sunDir);
    const f32 day = smoothstep(-0.06f, 0.18f, sun.y);
    p.sunX = sun.x, p.sunY = sun.y, p.sunZ = sun.z, p.sunStrength = g_pt.sunStrength * day;
    p.sunR = look.sunColor.x, p.sunG = look.sunColor.y, p.sunB = look.sunColor.z;
    p.ambient = g_pt.ambient * (0.2f + 0.8f * smoothstep(-0.12f, 0.3f, sun.y));
    const Vec3 sky = mix(look.hazeColor, Vec3{0.55f, 0.65f, 0.85f}, 0.35f);
    p.skyR = sky.x, p.skyG = sky.y, p.skyB = sky.z;
    p.fogR = look.hazeColor.x, p.fogG = look.hazeColor.y, p.fogB = look.hazeColor.z, p.sunGlow = look.sunGlow;
    p.p22 = m.projection.elements[10], p.p32 = m.projection.elements[14], p.p11 = m.projection.elements[5];
    const Extent2D e = context.viewportExtent();
    p.maxScreen = q.maxScreen;
    p.invWidth = e.width ? 1.0f / static_cast<f32>(e.width) : 0.0f;
    p.invHeight = e.height ? 1.0f / static_cast<f32>(e.height) : 0.0f;
    context.pushConstants(p);
    context.bindVertexBuffer(g_pt.buffers[slot], 0, PT_VIEWS * PT_CONST_BYTES);
    context.bindVertexBuffer(g_pt.buffers[slot], 1, view * PT_CONST_BYTES);
    context.draw(4, count);
    ++g_pt.draws;
    g_pt.drawn = count;
}

 
 
void addParticlePass(RenderModuleBuilder& module)
{
    module.addGraphicsPass(
        InternedString("WarzoneParticles"),
        [](RGBuilder& builder)
        {
            builder.asGraphicsPassMatchingAttachment();
            const bool depth = builder.hasResource(DEPTH_NAME);
            if (depth)
                builder.read(DEPTH_NAME, RGResourceUsage{.access = AccessFlags::SHADER_READ,
                                                         .stage = PipelineStage::VERTEX_SHADER | PipelineStage::FRAGMENT_SHADER,
                                                         .layout = ImageLayout::SHADER_READ_ONLY_OPTIMAL});
            if (builder.view() < g_pt.viewHasDepth.size())
                g_pt.viewHasDepth[builder.view()] = depth;
            builder.writeColorAttachment(InternedString("SceneColor"), 0, RGUsage::ColorAttachmentWrite, {.loadOp = AttachmentLoadOp::LOAD});
        },
        [pipeline = PipelineHandle{}, format = Format::UNDEFINED](RenderPassExecutionContext& context) mutable
        {
            const Format current = Format::R16G16B16A16_SFLOAT;
            if (current != format)
            {
                pipeline = particlePipeline(context, current);
                format = current;
            }
            return pipeline;
        },
        ptDraw);
}

void ptCheckShaders()
{
    for (auto path : {PARTICLES_VS, PARTICLES_FS})
        if (!std::filesystem::is_regular_file(resolveContentPath(path)))
            throw std::runtime_error(std::format("WarzoneGame requires shader {}", path));
}

void ptUnload(const ModuleContext& context)
{
    auto& services = context.scene.services();
    ptReleaseTextures(services, nullptr);
    ptReleaseBuffers(services);

}

 
std::string ptApplyArgs(const ControlArgs& args)
{
    if (auto v = args.boolean("particles"))
        g_pt.on = *v;
    if (auto v = args.integer("quality"))
        particlesSetQuality(static_cast<int>(*v));
    if (auto v = number(args, "ptSun"))
        g_pt.sunStrength = std::clamp(*v, 0.0f, 4.0f);
    if (auto v = number(args, "ptAmbient"))
        g_pt.ambient = std::clamp(*v, 0.0f, 4.0f);
    if (auto v = number(args, "ptScatter"))
        g_pt.backscatter = std::clamp(*v, 0.0f, 4.0f);
    if (auto v = number(args, "ptSoft"))
        g_pt.softScale = std::clamp(*v, 0.0f, 8.0f);
    if (auto v = args.integer("ptCap"))
        g_pt.sys.cap = static_cast<u32>(std::clamp<long long>(static_cast<long long>(*v), 0, PT_MAX));
    if (auto v = args.integer("ptBudget"))
        g_pt.sys.budget = static_cast<u32>(std::clamp<long long>(static_cast<long long>(*v), 0, 20000));
    if (auto v = args.boolean("reloadAtlases"); v && *v)
        g_pt.reloadWanted = true;
    return {};
}

void ptWriteStats(IFormatWriter& result)
{
    const auto& s = g_pt.sys;
    result.writeBool("particles", g_pt.on.load());
    result.writeBool("particlesActive", particlesOn());
    result.writeI32("quality", g_pt.quality.load());
    result.writeU32("ptLive", s.stats.live);
    result.writeU32("ptDrawn", g_pt.drawn.load());
    result.writeF32("ptSpawnedPerSecond", g_pt.spawnRate);
    result.writeU32("ptSpawnedFrame", s.stats.spawned);
    result.writeU32("ptSpawnedTotal", static_cast<u32>(s.stats.spawnedTotal));
    result.writeU32("ptCappedTotal", static_cast<u32>(s.stats.cappedTotal));
    result.writeU32("ptBudgetDroppedTotal", static_cast<u32>(s.stats.droppedTotal));
    result.writeU32("ptEmitters", s.stats.emitters);
    result.writeU32("ptNukeLive", s.nukeLive);
    result.writeU32("ptNukeEmitters", static_cast<u32>(std::count_if(s.nukes.begin(), s.nukes.end(), [](const auto& e) { return e.alive; })));
    u32 hazeLive = 0, hazeRegions = 0;
    for (const auto& p : s.parts) hazeLive += p.style == warzone::pt::S_BATTLE_HAZE;
    for (const auto& c : s.battleHaze) hazeRegions += c.mass >= 0.5f;
    result.writeU32("ptHazeLive", hazeLive);
    result.writeU32("ptHazeRegions", hazeRegions);
    result.writeU32("ptCap", s.cap);
    result.writeU32("ptBudget", s.budget);
    result.writeU32("ptPlayerLive", static_cast<u32>(std::count_if(s.parts.begin(), s.parts.end(), [](const auto& p) { return p.prio == 3; })));
    result.writeU32("ptPlayerReserve", std::min(1024u, s.cap / 4));
    result.writeU32("ptDraws", g_pt.draws.load());
    result.writeU32("ptUploadBytes", g_pt.uploadBytes.load());
    result.writeU32("ptAtlasesReady", g_pt.atlasesReady.load());
    result.writeF32("ptSun", g_pt.sunStrength);
    result.writeF32("ptAmbient", g_pt.ambient);
    result.writeF32("ptScatter", g_pt.backscatter);
    result.writeF32("ptSoft", g_pt.softScale);
    std::scoped_lock lock(g_pt.mutex);
    result.writeString("ptGpu", g_pt.gpuNote);
    result.writeString("ptAtlasLog", g_pt.metadataReady ? (g_pt.loadLog.empty() ? std::string("ok") : g_pt.loadLog) : std::string("loading"));
}
