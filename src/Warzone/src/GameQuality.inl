 
 
 
 
namespace wq = warzone::quality;

 
 
struct QualityBase
{
    warzone::StreamParams stream, detail;
    warzone::TierParams tier;
    u32 inPerFrame = 0, outPerFrame = 0;
    i32 cardBudget = 40;
};

struct QualityGfx
{
    std::string value, status;  
};

struct QualityRuntime
{
    wq::Settings chosen;   
    wq::Settings running;  
    wq::HardwareInfo hw;
    wq::Governor gov;
    wq::FrameProbe probe{3.0f};
    QualityBase base;
    bool probing = false;   
    bool autoPick = false;  
    bool fileExists = false, filled = false, haveCam = false;
    std::filesystem::path file;
    f32 vramFrac = 0.0f, vramTimer = 0.0f;
    Vec3 lastCam{};
    std::map<std::string, QualityGfx> gfx;  
    u32 applies = 0, particleCalls = 0;
};
QualityRuntime g_q;
std::mutex g_qualityMutex;  
ControlServer* g_qualityControl = nullptr;
std::function<void(std::function<void()>)> g_frameStart;

constexpr f32 QUALITY_WARMUP_SEC = 4.0f;        
constexpr f32 QUALITY_APPLY_SETTLE_SEC = 3.0f;  
constexpr f32 QUALITY_JUMP_METRES = 150.0f;     
constexpr f32 QUALITY_JUMP_IGNORE_SEC = 2.5f;
constexpr f32 QUALITY_BLOOM_INTENSITY = 0.035f;  

u32 qualityMb(u64 bytes) { return static_cast<u32>(bytes >> 20); }

void qualityCaptureBase()
{
    auto& b = g_q.base;
    b.stream = g_stream;
    b.detail = g_detail;
    b.tier = g_tier;
    b.inPerFrame = g_streamInPerFrame;
    b.outPerFrame = g_streamOutPerFrame;
    b.cardBudget = g_destruction.governor.budget;
}

void qualityDetectHardware()
{
    auto& hw = g_q.hw;
    sizet vramMb = 0;
    hw.gpuName = queryGpuName(vramMb);
    hw.vramBytes = static_cast<u64>(vramMb) << 20;
    const warzone::GpuMemory m = warzone::queryGpuMemory();
    if (m.valid)
    {
        hw.vramBytes = hw.vramBytes ? hw.vramBytes : m.totalBytes;
        hw.vramUsedBytes = m.usedBytes;
        hw.vramBudgetBytes = m.budgetBytes;
    }
    hw.cpuName = queryCpuBrand();
    hw.ramMb = queryTotalRamMb();
    hw.cpuCores = std::thread::hardware_concurrency();
    hw.suggested = wq::suggestPreset({hw.vramBytes >> 20, hw.cpuCores, hw.probeMs});
}

 
 
void qualitySendConfig(const char* path, const std::string& valueJson)
{
    QualityGfx& slot = g_q.gfx[path];
    if (slot.value == valueJson)
        return;
    slot = {valueJson, "sent"};
    if (!g_qualityControl)
        return;
    g_qualityControl->runAtFrameStart(
        MODULE, std::format(R"({{"command":"setConfig","args":{{"section":"application.graphics","path":"{}","value":{}}}}})", path, valueJson),
        [key = std::string(path)](const std::string& reply)
        {
            std::string status;
            if (const size_t at = reply.find("\"status\":\""); at != std::string::npos)
                status = reply.substr(at + 10, reply.find('"', at + 10) - (at + 10));
            else
                status = "refused: " + reply.substr(0, 200);
            std::scoped_lock lock(g_qualityMutex);
            g_q.gfx[key].status = status;
        });
}

void qualityApplyGraphics(const wq::Knobs::Gfx& g)
{
    qualitySendConfig("aa", g.taa ? "\"taa\"" : "\"off\"");
    qualitySendConfig("ao", g.ao ? "\"gtao\"" : "\"off\"");
    qualitySendConfig("bloom.intensity", g.bloom ? std::format("{}", QUALITY_BLOOM_INTENSITY) : "0");
    qualitySendConfig("shadows.resolution", std::to_string(g.shadowResolution));
    qualitySendConfig("shadows.maxDistance", std::format("{}", g.shadowDistance));
    qualitySendConfig("shadows.pcfRadius", std::to_string(g.pcfRadius));
    qualitySendConfig("lodBias", std::format("{}", g.lodBias));
    qualitySendConfig("lod", g.lod ? "true" : "false");
}

 
 
void qualityApplyKnobs(const wq::Settings& r, bool graphics)
{
    const wq::Knobs k = wq::knobsFor(r);
    const auto& b = g_q.base;
    g_stream.radiusIn = b.stream.radiusIn * k.radius;
    g_stream.radiusOut = b.stream.radiusOut * k.radius;
    g_stream.pieceBudget = std::min<u32>(STREAM_BODY_CAP, static_cast<u32>(b.stream.pieceBudget * k.pieceBudget));
    g_detail.radiusIn = b.detail.radiusIn * k.detailRange;
    g_detail.radiusOut = b.detail.radiusOut * k.detailRange;
    g_detail.pieceBudget = std::min(g_stream.pieceBudget, static_cast<u32>(b.detail.pieceBudget * k.detailBudget));
    g_tier.lowIn = b.tier.lowIn * k.lowRange;
    g_tier.lowOut = b.tier.lowOut * k.lowRange;
    g_streamInPerFrame = std::max<u32>(1, static_cast<u32>(b.inPerFrame * k.streamRate));
    g_streamOutPerFrame = std::max<u32>(1, static_cast<u32>(b.outPerFrame * k.streamRate));
    for (int t = 0; t < 4; ++t)
    {
        g_aiFullActive[t] = std::clamp<u32>(static_cast<u32>(std::lround(AI_FULL_ACTIVE0[t] * k.aiCount)), 1, AI_FULL_MAX[t]);
        g_aiFullRange[t] = AI_FULL_RANGE0[t] * k.aiRange * std::max(1.0f, r.gameLodScale / 4.0f);
    }
    g_farScale = k.farScale;
    g_effectReach = k.effectReach;
    g_lightLimit = k.lights;
    g_destruction.governor.budget = std::max(8, static_cast<int>(b.cardBudget * k.cardBudget));
#ifdef WZ_HAS_PARTICLES_SET_QUALITY
    particlesSetQuality(k.particleQuality);  
    ++g_q.particleCalls;
#endif
    if (graphics)
        qualityApplyGraphics(k.gfx);
}

std::filesystem::path qualityFilePath()
{
    std::filesystem::path dir = savesDirectory();  
    if (dir.empty())
        dir = resolveContentPath("user");
    return dir / "warzone_quality.json";
}

bool qualityWriteFile(const wq::Settings& s)
{
    std::error_code ec;
    std::filesystem::create_directories(g_q.file.parent_path(), ec);
    const std::filesystem::path tmp = std::filesystem::path(g_q.file).concat(".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << wq::toJson(s);
        if (!out)
            return false;
    }
    std::filesystem::rename(tmp, g_q.file, ec);
    g_q.fileExists = !ec;
    return !ec;
}

 
void qualityApplyLocked(const wq::Settings& in, bool persist, const char* why)
{
    wq::Settings s = wq::sanitize(in);
    if (s.preset != wq::Preset::Custom)
        s.preset = wq::classify(s);
    g_q.chosen = s;
    g_q.gov.start(s, QUALITY_APPLY_SETTLE_SEC);
    g_q.running = g_q.gov.effective();
    qualityApplyKnobs(g_q.running, true);
    ++g_q.applies;
    g_q.autoPick = false;  
    if (persist && !qualityWriteFile(s))
        SWARNING_LOG("Warzone quality: could not write {}", g_q.file.string());
    SINFO_LOG("Warzone quality: {} (adaptive {}, target {:.0f}, view {:.2f}, buildings {}, units {}, effects {}, shadows {}) from {}", wq::presetName(s.preset),
              s.adaptive, s.targetFps, s.viewDistance, s.buildingDetail, s.unitDetail, s.effects, s.shadows, why);
}

 
const wq::Settings& qualityCurrent() { return g_q.chosen; }

 
void qualityApply(const wq::Settings& s, bool persist)
{
    std::scoped_lock lock(g_qualityMutex);
    qualityApplyLocked(s, persist, "menu");
}

const wq::HardwareInfo& qualityHardware() { return g_q.hw; }

 
std::string qualityStatusLine()
{
    std::scoped_lock lock(g_qualityMutex);
    return wq::statusLine(g_q.chosen, g_q.running, g_q.gov.fps());
}

 
void qualityFinishProbe()
{
    auto& q = g_q;
    q.probing = false;
    q.hw.probeMs = q.probe.medianMs();
    q.hw.suggested = wq::suggestPreset({q.hw.vramBytes >> 20, q.hw.cpuCores, q.hw.probeMs});
    SINFO_LOG("Warzone quality: probe {:.1f} ms median, {} suggested{}", q.hw.probeMs, wq::presetName(q.hw.suggested),
              q.autoPick && q.hw.suggested != q.chosen.preset ? ", switching" : "");
    if (q.autoPick && q.chosen.preset != wq::Preset::Custom && q.hw.suggested != q.chosen.preset)
    {
        qualityApplyLocked(wq::presetSettings(q.hw.suggested), false, "probe");
        q.autoPick = true;  
    }
}

void qualitySystem(SystemContext& ctx)
{
    const f32 dt = ctx.deltaTime;
    if (!(dt > 0.0f))
        return;
    std::scoped_lock lock(g_qualityMutex);
    auto& q = g_q;
    q.vramTimer += dt;
    if (q.vramTimer >= 0.5f)
    {
        q.vramTimer = 0.0f;
        const warzone::GpuMemory m = warzone::queryGpuMemory();
        if (m.valid)
        {
            q.hw.vramUsedBytes = m.usedBytes;
            q.hw.vramBudgetBytes = m.budgetBytes;
            q.vramFrac = m.budgetBytes ? static_cast<f32>(static_cast<double>(m.usedBytes) / static_cast<double>(m.budgetBytes)) : 0.0f;
        }
    }
     
    const bool filled = g_destruction.streamedOnce && g_listenerSet;
    if (!filled)
        q.gov.ignoreFor(1.0f);
    else if (!q.filled)
        q.gov.ignoreFor(QUALITY_WARMUP_SEC);
    q.filled = filled;
    if (g_listenerSet)
    {
        if (q.haveCam && length(g_listener - q.lastCam) > QUALITY_JUMP_METRES)
            q.gov.ignoreFor(QUALITY_JUMP_IGNORE_SEC);
        q.lastCam = g_listener;
        q.haveCam = true;
    }
    if (q.probing && q.gov.skipLeft() <= 0.0f)
    {
        q.probe.add(dt);
        if (q.probe.done())
            qualityFinishProbe();
    }
    if (q.gov.frame(dt, q.vramFrac))
    {
        q.running = q.gov.effective();
        qualityApplyKnobs(q.running, false);
        const wq::Decision& d = q.gov.decisions().back();
        SINFO_LOG("Warzone quality: {} (drop {}, memory drop {}, p90 {:.1f} ms, memory {:.0f}%): view {:.2f}, buildings {}, units {}, effects {}", d.why, d.drop,
                  d.memDrop, d.p90Ms, d.vram * 100.0f, q.running.viewDistance, q.running.buildingDetail, q.running.unitDetail, q.running.effects);
    }
}

void registerQualitySystem(World world)
{
    SystemDescriptor sys;
    sys.moduleName = MODULE;
    sys.name = "WarzoneQuality";
    sys.fn = qualitySystem;
    sys.update = SystemPhaseDescriptor{.reads = {getComponentId<TransformComponent>()}};
    world.registerSystem(sys);
}

 
 
void attachQuality(const ModuleContext& context)
{
    std::scoped_lock lock(g_qualityMutex);
    auto& q = g_q;
    g_qualityControl = &context.control;
    q.filled = q.haveCam = false;
    q.vramTimer = 1.0f;
    q.gov.start(q.chosen, QUALITY_WARMUP_SEC);
    q.running = q.gov.effective();
    q.probing = q.autoPick;
    q.probe = wq::FrameProbe(3.0f);
    q.gfx.clear();  
    qualityApplyKnobs(q.running, true);
}

 
void writeQualityState(IFormatWriter& out)
{
    const auto& q = g_q;
    out.beginObject("hardware");
    out.writeString("gpu", q.hw.gpuName);
    out.writeU32("vramMb", qualityMb(q.hw.vramBytes));
    out.writeU32("vramUsedMb", qualityMb(q.hw.vramUsedBytes));
    out.writeU32("vramBudgetMb", qualityMb(q.hw.vramBudgetBytes));
    out.writeF32("vramFraction", q.vramFrac);
    out.writeString("cpu", q.hw.cpuName);
    out.writeU32("cpuCores", q.hw.cpuCores);
    out.writeU32("ramMb", static_cast<u32>(q.hw.ramMb));
    out.writeF32("probeMs", q.hw.probeMs);
    out.writeString("suggested", wq::presetName(q.hw.suggested));
    out.writeBool("probing", q.probing);
    out.endObject();
    auto settings = [&](const char* name, const wq::Settings& s)
    {
        out.beginObject(name);
        out.writeString("preset", wq::presetName(s.preset));
        out.writeBool("adaptive", s.adaptive);
        out.writeF32("targetFps", s.targetFps);
        out.writeF32("viewDistance", s.viewDistance);
        out.writeI32("buildingDetail", s.buildingDetail);
        out.writeI32("unitDetail", s.unitDetail);
        out.writeI32("effects", s.effects);
        out.writeI32("shadows", s.shadows);
        out.writeBool("ambientOcclusion", s.ambientOcclusion);
        out.writeBool("bloom", s.bloom);
        out.writeBool("antiAliasing", s.antiAliasing);
        out.writeBool("engineLod", s.engineLod);
        out.writeF32("gameLodScale", s.gameLodScale);
        out.writeF32("engineLodBias", s.engineLodBias);
        out.endObject();
    };
    settings("settings", q.chosen);  
    settings("running", q.running);  
    out.writeBool("autoPick", q.autoPick);
    out.beginObject("governor");
    out.writeI32("drop", q.gov.drop());
    out.writeI32("memoryDrop", q.gov.memDrop());
    out.writeI32("maxDrop", q.gov.maxDrop());
    out.writeF32("p90Ms", q.gov.p90Ms());
    out.writeF32("fps", q.gov.fps());
    out.writeI32("calmWindows", q.gov.calm());
    out.writeI32("calmNeeded", q.gov.upNeed());
    out.writeF32("ignoringSeconds", q.gov.skipLeft());
    out.writeF32("clock", q.gov.clock());
    out.endObject();
     
    out.beginObject("applied");
    out.writeF32("streamRadiusIn", g_stream.radiusIn);
    out.writeF32("streamRadiusOut", g_stream.radiusOut);
    out.writeU32("pieceBudget", g_stream.pieceBudget);
    out.writeF32("detailRadiusIn", g_detail.radiusIn);
    out.writeF32("detailRadiusOut", g_detail.radiusOut);
    out.writeU32("detailPieceBudget", g_detail.pieceBudget);
    out.writeF32("coarseFrom", g_tier.lowIn);
    out.writeF32("coarseBack", g_tier.lowOut);
    out.writeU32("streamInPerFrame", g_streamInPerFrame);
    out.writeU32("streamOutPerFrame", g_streamOutPerFrame);
    out.writeFloats("aiActive", std::array<f32, 4>{f32(g_aiFullActive[0]), f32(g_aiFullActive[1]), f32(g_aiFullActive[2]), f32(g_aiFullActive[3])});
    out.writeFloats("aiRange", std::array<f32, 4>{g_aiFullRange[0], g_aiFullRange[1], g_aiFullRange[2], g_aiFullRange[3]});
    out.writeF32("farPlane", g_authoredFar > 0.0f ? g_authoredFar * g_farScale : 0.0f);
    out.writeF32("effectReach", g_effectReach);
    out.writeU32("lights", g_lightLimit);
    out.writeI32("cardBudget", g_destruction.governor.budget);
#ifdef WZ_HAS_PARTICLES_SET_QUALITY
    out.writeU32("particleQualityCalls", q.particleCalls);
#else
    out.writeString("particleQuality", "not wired: particlesSetQuality is not in this build");
#endif
    out.endObject();
    out.beginArray("graphics", static_cast<u32>(q.gfx.size()));
    for (const auto& [path, g] : q.gfx)
    {
        out.beginObject({});
        out.writeString("path", path);
        out.writeString("value", g.value);
        out.writeString("status", g.status);
        out.endObject();
    }
    out.endArray();
    out.beginObject("file");
    out.writeString("path", q.file.string());
    out.writeBool("exists", q.fileExists);
    out.endObject();
    out.writeString("statusLine", wq::statusLine(q.chosen, q.running, q.gov.fps()));
    out.writeU32("applies", q.applies);
    const auto& decisions = q.gov.decisions();
    out.beginArray("decisions", static_cast<u32>(decisions.size()));
    for (const auto& d : decisions)
    {
        out.beginObject({});
        out.writeF32("t", d.t);
        out.writeString("why", d.why);
        out.writeI32("drop", d.drop);
        out.writeI32("memoryDrop", d.memDrop);
        out.writeF32("p90Ms", d.p90Ms);
        out.writeF32("memory", d.vram);
        out.endObject();
    }
    out.endArray();
}

void registerQualityCommands(const ModuleContext& context)
{
    auto& control = context.control;
    const std::string_view owner = MODULE;
    static const std::array<std::string_view, 6> presets = {"low", "medium", "high", "ultra", "custom", "auto"};
    control.registerCommand(
        "warzoneQuality", owner,
        "graphics quality: preset (low medium high ultra custom, or auto = the machine's suggestion) loads that preset's values, then any "
        "field overrides it (a field that no preset matches makes it custom); adaptive turns the governor on or off, target is the fps it "
        "holds (targetFps too), drop forces the governor's step count; persist writes the user file (off by default for scripts). Answers "
        "the state as warzoneQualityState does",
        {optionalArg("preset", ArgKind::String, "low | medium | high | ultra | custom | auto", {}, presets),
         optionalArg("adaptive", ArgKind::Boolean, "the governor moves view, buildings, units and effects with the frame time"),
         optionalArg("target", ArgKind::Number, "fps the governor holds: 30 60 90 120 144"),
         optionalArg("targetFps", ArgKind::Number, "same as target"),
         optionalArg("viewDistance", ArgKind::Number, "0.5 .. 1.5, x streaming radius, stand-in reach and far plane"),
         optionalArg("buildingDetail", ArgKind::Integer, "0..3: piece budget, full-detail range and budget, coarse-tier distance"),
         optionalArg("unitDetail", ArgKind::Integer, "0..3: near full models per type and their range"),
         optionalArg("effects", ArgKind::Integer, "0..3: particle quality, card tiers, lights"),
         optionalArg("shadows", ArgKind::Integer, "0..3: cascade resolution, max distance, filter"),
         optionalArg("ambientOcclusion", ArgKind::Boolean, "GTAO"), optionalArg("bloom", ArgKind::Boolean, "bloom"),
         optionalArg("antiAliasing", ArgKind::Boolean, "TAA"), optionalArg("engineLod", ArgKind::Boolean, "graphics.lod (engine cluster LOD)"),
         optionalArg("gameLodScale", ArgKind::Number, "1..16 x game LOD distances; above 4 extends streaming reach and piece budgets up to the hard cap; governor preserves scale"),
         optionalArg("engineLodBias", ArgKind::Number, "-2..4 stops; +1 doubles allowed cluster error; governor preserves bias"),
         optionalArg("persist", ArgKind::Boolean, "write the user file", "false"),
         optionalArg("drop", ArgKind::Integer, "force the governor to this many steps below the setting")},
        [](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            std::scoped_lock lock(g_qualityMutex);
            wq::Settings s = g_q.chosen;
            bool changed = false;
            if (const auto name = args.string("preset"))
            {
                wq::Preset p;
                if (*name == "auto")
                    s = wq::presetSettings(g_q.hw.suggested);
                else if (wq::presetFromName(*name, p))
                    s = wq::presetSettings(p);
                else
                    return "unknown preset '" + *name + "': low, medium, high, ultra, custom or auto";
                changed = true;
            }
            auto flag = [&](const char* key, bool& dst)
            {
                if (const auto v = args.boolean(key))
                    dst = *v, changed = true;
            };
            auto level = [&](const char* key, int& dst)
            {
                if (const auto v = args.integer(key))
                    dst = static_cast<int>(std::min<u64>(*v, 3)), changed = true;
            };
            auto value = [&](const char* key, f32& dst)
            {
                if (const auto v = number(args, key))
                    dst = *v, changed = true;
            };
            flag("adaptive", s.adaptive);
            value("target", s.targetFps);
            value("targetFps", s.targetFps);
            value("viewDistance", s.viewDistance);
            level("buildingDetail", s.buildingDetail);
            level("unitDetail", s.unitDetail);
            level("effects", s.effects);
            level("shadows", s.shadows);
            flag("ambientOcclusion", s.ambientOcclusion);
            flag("bloom", s.bloom);
            flag("antiAliasing", s.antiAliasing);
            flag("engineLod", s.engineLod);
            value("gameLodScale", s.gameLodScale);
            value("engineLodBias", s.engineLodBias);
            if (changed)
                qualityApplyLocked(s, args.boolean("persist").value_or(false), "command");
            if (const auto d = args.integer("drop"))
            {
                g_q.gov.forceDrop(static_cast<int>(*d));
                g_q.running = g_q.gov.effective();
                qualityApplyKnobs(g_q.running, false);
            }
            writeQualityState(result);
            return {};
        });
    control.registerQuery("warzoneQualityState", owner,
                          "graphics quality: the hardware found (GPU, video memory used / budget, cores, probe frame time, suggested preset), the user's "
                          "settings, what runs after the governor, the governor's state, the knobs in the game's globals, the engine config answers "
                          "(applied / deferred / restartRequired per field), the user file and the governor's last decisions with reasons",
                          {},
                          [](const ControlArgs&, IFormatWriter& result) -> std::string
                          {
                              std::scoped_lock lock(g_qualityMutex);
                              writeQualityState(result);
                              return {};
                          });
}

 
void loadQuality(const ModuleContext& context)
{
    g_qualityControl = &context.control;
    g_frameStart = context.runAtFrameStart;
    if (context.services.contains<GpuMemoryService>())
        warzone::gpuMemory = *context.services.read<GpuMemoryService>();
    qualityCaptureBase();
    qualityDetectHardware();
    g_q.file = qualityFilePath();
    wq::Settings s;
    std::ifstream in(g_q.file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in && wq::fromJson(text, s))
    {
        g_q.chosen = s;
        g_q.fileExists = true;
    }
    else
    {
        g_q.chosen = wq::presetSettings(g_q.hw.suggested);
        g_q.autoPick = true;
    }
    g_q.running = g_q.chosen;
    g_q.gov.start(g_q.chosen, 0.0f);
    registerQualityCommands(context);
    SINFO_LOG("Warzone quality: {} {} MB, {} ({} threads), {} MB RAM; {} {}", g_q.hw.gpuName, qualityMb(g_q.hw.vramBytes), g_q.hw.cpuName, g_q.hw.cpuCores,
              g_q.hw.ramMb, g_q.autoPick ? "suggested" : "from file", wq::presetName(g_q.chosen.preset));
}
