 
 
 
 
 
 
 
 
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "Core.hpp"
#include "Vfx.hpp"

namespace warzone::pt
{
     
     
    enum Atlas : uint8_t
    {
        SMOKE,
        DUST,
        FIRE,
        FLAME,
        FLASH,
        SPARK,
        GLOW,
        DEBRIS,
        SMOKE_B,     
        SMOKE_C,     
        DUST_BURST,  
        FIRE_SMOKE,  
        MUZZLE,
        SHOCK,
        ATLASES
    };
    constexpr const char* ATLAS_NAMES[ATLASES] = {"smoke", "dust", "fire", "flame", "flash", "spark", "glow", "debris",
                                                  "smoke_b", "smoke_c", "dust_burst", "fire_smoke", "muzzle", "shock"};
    enum FrameMode : uint8_t
    {
        PLAY,   
        LOOP,   
        RANDOM  
    };
    struct AtlasInfo
    {
        uint16_t grid = 8, frames = 64;
        FrameMode mode = PLAY;
        float fps = 20.0f;
    };
    constexpr std::array<AtlasInfo, ATLASES> ATLAS_DEFAULTS = {{{8, 64, PLAY, 20}, {8, 64, PLAY, 20}, {8, 64, PLAY, 20}, {8, 64, LOOP, 20},
                                                                {8, 64, RANDOM, 20}, {8, 64, RANDOM, 20}, {8, 64, RANDOM, 20}, {8, 64, RANDOM, 20},
                                                                {8, 64, PLAY, 20},   {8, 64, PLAY, 20},   {8, 64, PLAY, 20},   {8, 64, PLAY, 20}, {8, 64, RANDOM, 20}, {8, 64, PLAY, 20}}};

     
     
    inline bool parseAtlasLine(const std::string& line, std::array<AtlasInfo, ATLASES>& infos)
    {
        std::istringstream in(line);
        std::string name, kv;
        if (!(in >> name) || name[0] == '#')
            return false;
        int index = -1;
        for (int i = 0; i < ATLASES; ++i)
            if (name == ATLAS_NAMES[i])
                index = i;
        if (index < 0)
            return false;
        AtlasInfo info = infos[index];
        while (in >> kv)
        {
            const auto eq = kv.find('=');
            if (eq == std::string::npos)
                return false;
            const std::string key = kv.substr(0, eq), value = kv.substr(eq + 1);
            if (key == "grid" || key == "frames")
            {
                const int v = std::atoi(value.c_str());
                if (v <= 0 || v > 256 || (key == "grid" && v != 4 && v != 8 && v != 16))
                    return false;
                (key == "grid" ? info.grid : info.frames) = static_cast<uint16_t>(v);
            }
            else if (key == "mode")
            {
                if (value == "play")
                    info.mode = PLAY;
                else if (value == "loop")
                    info.mode = LOOP;
                else if (value == "random")
                    info.mode = RANDOM;
                else
                    return false;
            }
            else if (key == "fps")
                info.fps = std::clamp(static_cast<float>(std::atof(value.c_str())), 1.0f, 120.0f);
            else
                return false;
        }
        info.frames = std::min<uint16_t>(info.frames, static_cast<uint16_t>(info.grid * info.grid));
        infos[index] = info;
        return true;
    }

     
    enum StyleFlag : uint8_t
    {
        LIT = 1,      
        STRETCH = 2,  
        SOFT = 4      
    };
     
     
    struct Style
    {
        Atlas atlas = SMOKE;
        uint8_t flags = LIT | SOFT;
        V3 c0{1, 1, 1}, c1{1, 1, 1};
        float cPow = 1.0f;
        float opacity = 1.0f;
        float fadeIn = 0.05f, fadeOut = 0.4f;  
        V3 e0{}, e1{};                          
        float emissive = 0.0f, ePow = 1.0f;     
        float sizePow = 2.0f;                   
        float stretch = 0.0f;                   
        float soft = 0.6f;                      
    };
    enum StyleId : uint16_t
    {
        S_SMOKE,        
        S_SMOKE_HOT,    
        S_SMOKE_LIGHT,  
        S_DUST,         
        S_FIREBALL,     
        S_FLASH,        
        S_GLOW,         
        S_SPARK,        
        S_DEBRIS,       
        S_FLAME,        
        S_TRACER,       
        S_EMBER,        
        S_TRAIL_GLOW,   
        S_PLUME,        
        S_WISP,         
        S_DUST_BURST,   
        S_FIRE_SMOKE,   
        S_MUZZLE,
        S_SHOCK,
        S_BATTLE_HAZE,
        S_JET_CORE, S_JET_PLUME, S_JET_DIAMOND, S_WING_VAPOUR, S_BOMB_MOTOR,
        S_NUKE_FLASH, S_NUKE_CLOUD, S_NUKE_DUST, S_NUKE_FIRE, S_NUKE_CONDENSATION,
        STYLES
    };
    constexpr uint16_t NO_STYLE = 0xFFFF;

    inline std::array<Style, STYLES> defaultStyles()
    {
        std::array<Style, STYLES> s{};
        Style& smoke = s[S_SMOKE];
        smoke.c0 = {0.3f, 0.29f, 0.28f}, smoke.c1 = {0.85f, 0.85f, 0.85f}, smoke.cPow = 0.6f;
        smoke.opacity = 0.85f, smoke.fadeIn = 0.04f, smoke.fadeOut = 0.5f, smoke.sizePow = 1.6f, smoke.soft = 0.7f;
        Style& hot = s[S_SMOKE_HOT];
        hot = smoke;
        hot.c0 = {0.2f, 0.19f, 0.18f}, hot.c1 = {0.6f, 0.59f, 0.58f};
        hot.e0 = {1.0f, 0.42f, 0.12f}, hot.e1 = {0.6f, 0.12f, 0.02f}, hot.emissive = 1.6f, hot.ePow = 16.0f;
        Style& light = s[S_SMOKE_LIGHT];
        light = smoke;
        light.c0 = {1.0f, 1.0f, 0.98f}, light.c1 = {0.95f, 0.95f, 0.95f}, light.opacity = 0.7f, light.fadeOut = 0.6f;
         
        Style& dust = s[S_DUST];
        dust.atlas = DUST;
        dust.c0 = {0.78f, 0.78f, 0.78f}, dust.c1 = {1.0f, 1.0f, 1.0f}, dust.cPow = 0.7f;
        dust.opacity = 0.85f, dust.fadeIn = 0.03f, dust.fadeOut = 0.6f, dust.sizePow = 2.4f, dust.soft = 0.8f;
        Style& fire = s[S_FIREBALL];
        fire.atlas = FIRE, fire.flags = SOFT;
        fire.c0 = {0.25f, 0.18f, 0.12f}, fire.c1 = {0.1f, 0.08f, 0.07f};
        fire.opacity = 0.35f, fire.fadeIn = 0.0f, fire.fadeOut = 0.35f;
        fire.e0 = {1.0f, 0.85f, 0.6f}, fire.e1 = {1.0f, 0.35f, 0.08f}, fire.emissive = 3.0f, fire.ePow = 1.3f, fire.sizePow = 3.0f;
        fire.soft = 0.5f;
        Style& flash = s[S_FLASH];
        flash.atlas = FLASH, flash.flags = SOFT;
        flash.opacity = 0.0f, flash.fadeIn = 0.0f, flash.fadeOut = 0.6f;
        flash.e0 = {1.0f, 0.92f, 0.75f}, flash.e1 = {1.0f, 0.6f, 0.25f}, flash.emissive = 4.0f, flash.ePow = 1.5f, flash.sizePow = 4.0f;
        flash.soft = 0.3f;
        Style& glow = s[S_GLOW];
        glow = flash;
        glow.atlas = GLOW, glow.emissive = 3.0f;
        Style& spark = s[S_SPARK];
        spark.atlas = SPARK, spark.flags = STRETCH;
        spark.opacity = 0.0f, spark.fadeIn = 0.0f, spark.fadeOut = 0.4f;
        spark.e0 = {1.0f, 0.9f, 0.6f}, spark.e1 = {1.0f, 0.4f, 0.1f}, spark.emissive = 4.0f, spark.ePow = 0.8f, spark.sizePow = 1.0f;
        spark.stretch = 0.035f;
        Style& debris = s[S_DEBRIS];
        debris.atlas = DEBRIS, debris.flags = LIT;
        debris.c0 = debris.c1 = {0.75f, 0.72f, 0.68f};
        debris.opacity = 1.0f, debris.fadeIn = 0.0f, debris.fadeOut = 0.15f, debris.sizePow = 1.0f, debris.soft = 0.2f;
        Style& flame = s[S_FLAME];
        flame.atlas = FLAME, flame.flags = SOFT;
        flame.c0 = flame.c1 = {0.1f, 0.06f, 0.04f};
        flame.opacity = 0.25f, flame.fadeIn = 0.15f, flame.fadeOut = 0.45f;
        flame.e0 = {1.0f, 0.75f, 0.4f}, flame.e1 = {1.0f, 0.35f, 0.08f}, flame.emissive = 2.4f, flame.ePow = 0.7f, flame.sizePow = 1.2f;
        flame.soft = 0.4f;
        Style& tracer = s[S_TRACER];
        tracer = spark;
        tracer.e0 = {1.0f, 0.85f, 0.55f}, tracer.e1 = {1.0f, 0.45f, 0.15f}, tracer.emissive = 3.0f, tracer.ePow = 1.2f;
        tracer.fadeOut = 1.0f;
        Style& trailGlow = s[S_TRAIL_GLOW];
        trailGlow = tracer;
        trailGlow.atlas = GLOW, trailGlow.emissive = 1.1f, trailGlow.ePow = 1.5f, trailGlow.fadeOut = 1.0f;
        Style& ember = s[S_EMBER];
        ember = glow;
        ember.fadeIn = 0.1f, ember.fadeOut = 0.5f, ember.emissive = 2.5f, ember.e1 = {0.9f, 0.25f, 0.05f}, ember.sizePow = 1.0f;
        Style& plume = s[S_PLUME];
        plume = smoke;
        plume.atlas = SMOKE_B, plume.c0 = {0.6f, 0.59f, 0.58f}, plume.c1 = {1.0f, 1.0f, 1.0f}, plume.cPow = 0.8f;
        plume.opacity = 1.0f, plume.fadeIn = 0.06f, plume.fadeOut = 0.55f, plume.sizePow = 2.0f, plume.soft = 0.8f;
        Style& wisp = s[S_WISP];
        wisp = smoke;
        wisp.atlas = SMOKE_C, wisp.c0 = {0.62f, 0.62f, 0.62f}, wisp.c1 = {0.85f, 0.85f, 0.85f};
        wisp.opacity = 0.85f, wisp.fadeIn = 0.1f, wisp.fadeOut = 0.6f, wisp.sizePow = 1.2f, wisp.soft = 0.9f;
        Style& burst = s[S_DUST_BURST];
        burst = dust;
        burst.atlas = DUST_BURST, burst.fadeOut = 0.5f, burst.sizePow = 3.0f;
        Style& fsmoke = s[S_FIRE_SMOKE];
        fsmoke = smoke;
        fsmoke.atlas = FIRE_SMOKE, fsmoke.c0 = {0.95f, 0.97f, 1.0f}, fsmoke.c1 = {1.0f, 1.0f, 1.0f};
        fsmoke.opacity = 0.95f, fsmoke.fadeIn = 0.0f, fsmoke.fadeOut = 0.3f, fsmoke.sizePow = 1.8f;
        fsmoke.e0 = {1.0f, 0.45f, 0.15f}, fsmoke.e1 = {0.5f, 0.1f, 0.02f}, fsmoke.emissive = 0.3f, fsmoke.ePow = 8.0f;
        s[S_MUZZLE] = flash;
        s[S_MUZZLE].atlas = MUZZLE;
        s[S_SHOCK] = flash;
        s[S_SHOCK].atlas = SHOCK;
        s[S_SHOCK].flags = 0;
        s[S_SHOCK].emissive = 0.8f;
        s[S_SHOCK].e0 = s[S_SHOCK].e1 = {0.8f, 0.75f, 0.65f};
        s[S_BATTLE_HAZE] = wisp;
        s[S_BATTLE_HAZE].opacity = 0.13f;
        s[S_BATTLE_HAZE].fadeIn = 0.2f;
        s[S_BATTLE_HAZE].fadeOut = 0.4f;
        auto& jet = s[S_JET_CORE];
        jet = glow; jet.atlas = SPARK; jet.flags = STRETCH | SOFT; jet.stretch = 1;
        jet.fadeIn = 0; jet.fadeOut = 0.2f; jet.ePow = 0;
        jet.e0 = jet.e1 = {0.55f, 0.75f, 1}; jet.emissive = 5; jet.soft = 0.12f;
        auto& exhaust = s[S_JET_PLUME];
        exhaust = jet; exhaust.e0 = exhaust.e1 = {0.42f, 0.22f, 1}; exhaust.emissive = 1.2f;
        auto& diamond = s[S_JET_DIAMOND];
        diamond = jet; diamond.atlas = GLOW; diamond.e0 = diamond.e1 = {0.8f, 0.85f, 1}; diamond.emissive = 3;
        auto& vapour = s[S_WING_VAPOUR];
        vapour = light; vapour.opacity = 0.16f; vapour.fadeIn = 0.04f; vapour.fadeOut = 0.8f;
        vapour.c0 = vapour.c1 = {0.85f, 0.9f, 1}; vapour.soft = 0.3f;
        s[S_BOMB_MOTOR] = jet;
        s[S_BOMB_MOTOR].e0 = s[S_BOMB_MOTOR].e1 = {1, 0.65f, 0.25f};
        s[S_BOMB_MOTOR].emissive = 7;
        s[S_NUKE_FLASH] = glow;
        s[S_NUKE_FLASH].emissive = 18;
        s[S_NUKE_FLASH].flags = 0;
        s[S_NUKE_CLOUD] = plume;
        auto& cloud = s[S_NUKE_CLOUD];
        cloud.atlas = SMOKE;
        cloud.c0 = {0.82f, 0.74f, 0.61f}; cloud.c1 = {0.24f, 0.26f, 0.29f}; cloud.cPow = 0.45f;
        cloud.fadeIn = 0.005f; cloud.fadeOut = 0.35f; cloud.sizePow = 1.3f;
        cloud.e0 = {1, 0.32f, 0.06f}; cloud.e1 = {}; cloud.emissive = 0.8f; cloud.ePow = 28;
        s[S_NUKE_DUST] = dust;
        s[S_NUKE_FIRE] = fire;
        s[S_NUKE_FIRE].emissive = 5;
        s[S_NUKE_FIRE].e0 = {1, 0.86f, 0.48f}; s[S_NUKE_FIRE].e1 = {1, 0.22f, 0.035f};
        s[S_NUKE_FIRE].opacity = 0.2f; s[S_NUKE_FIRE].fadeOut = 0.55f;
        s[S_NUKE_CONDENSATION] = light;
        s[S_NUKE_CONDENSATION].atlas = SHOCK;
        s[S_NUKE_CONDENSATION].flags = LIT;
        s[S_NUKE_CONDENSATION].fadeIn = 0;
        s[S_NUKE_CONDENSATION].opacity = 0.28f;
        return s;
    }

     
    constexpr int LUT = 32;
    struct Lut
    {
        std::array<uint32_t, LUT> colour{};    
        std::array<uint32_t, LUT> emission{};  
        std::array<float, LUT> emissive{};     
        std::array<float, LUT> grow{};         
    };
    inline uint8_t u8of(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }
    inline uint32_t rgba8(float r, float g, float b, float a)
    {
        return static_cast<uint32_t>(u8of(r)) | static_cast<uint32_t>(u8of(g)) << 8 | static_cast<uint32_t>(u8of(b)) << 16 |
               static_cast<uint32_t>(u8of(a)) << 24;
    }
    inline Lut buildLut(const Style& st)
    {
        Lut l;
        for (int i = 0; i < LUT; ++i)
        {
            const float u = static_cast<float>(i) / (LUT - 1);
            const float cu = std::pow(u, st.cPow);
            const V3 c = st.c0 * (1.0f - cu) + st.c1 * cu;
            const float in = st.fadeIn > 0.0f ? std::clamp(u / st.fadeIn, 0.0f, 1.0f) : 1.0f;
            const float o = st.fadeOut > 0.0f ? std::clamp((1.0f - u) / st.fadeOut, 0.0f, 1.0f) : 1.0f;
            const float a = in * o * o * (3.0f - 2.0f * o);  
            l.colour[i] = rgba8(c.x, c.y, c.z, st.opacity * a);
            const V3 e = st.e0 * (1.0f - u) + st.e1 * u;
            const float em = std::max({e.x, e.y, e.z, 1e-4f});
            l.emission[i] = rgba8(e.x / em, e.y / em, e.z / em, 0.0f);
            l.emissive[i] = st.emissive * em * std::pow(std::max(0.0f, 1.0f - u), st.ePow) * in * std::min(1.0f, o * 2.0f);
            l.grow[i] = 1.0f - std::pow(1.0f - u, st.sizePow);
        }
        return l;
    }

     
    enum ParticleFlag : uint8_t
    {
        P_GROUNDED = 1,   
        P_BOUNCE = 2,     
        P_BALLISTIC = 4,  
        P_TRAIL = 8,      
        P_DIE_GROUND = 16,  
        P_PLANE = 64,
        P_STATIC = 32       
    };
    constexpr float TRAIL_SPACING = 1.6f;
    struct Particle
    {
        V3 pos;
        float age = 0;  
        V3 vel;
        float life = 1;
        float s0 = 1, s1 = 2, rot = 0, spin = 0;
        float drag = 0.6f, rise = 0, windK = 0, gravity = 0;
        float windX = 0, windZ = 0, trailAcc = 0, heat = 1;
        uint32_t tint = 0xFFFFFFFFu;
        uint16_t style = S_SMOKE, trailStyle = NO_STYLE;
        uint8_t frame = 0, flags = 0, prio = 1, pad = 0;
    };

     
     
    struct Spawn
    {
        uint16_t style = S_SMOKE, trailStyle = NO_STYLE;
        V3 pos{};
        float posR = 0;
        bool flat = false;
        V3 vel{};
        float velR = 0, radial = 0, radialJ = 0;
        float life = 1, lifeJ = 0;
        float s0 = 1, s1 = 2, sizeJ = 0;
        float rise = 0, windK = 0, drag = 0.6f, gravity = 0, spin = 0;
        float riseJ = 0;  
        float delay = 0, delayJ = 0;
        float heat = 1;
        uint32_t tint = 0xFFFFFFFFu;
        float tintJ = 0;  
        uint8_t flags = 0, prio = 1;
    };

     
    struct GpuParticle
    {
        float x, y, z, size;    
        uint32_t colour;        
        uint32_t tex;           
        uint32_t frame;         
        uint32_t emission;      
        float c0, c1, c2;       
        float emissive;         
    };
    static_assert(sizeof(GpuParticle) == 48);
    enum GpuFlag : uint32_t
    {
        GPU_LIT = 1,
        GPU_STRETCH = 2,
        GPU_SOFT = 4,
        GPU_NORMAL = 8,  
        GPU_PLANE = 128,
        GPU_GRID_SHIFT = 4  
    };

    struct Quality
    {
        float count, size;
        uint32_t cap;
        bool soft;
        float maxScreen;  
    };
    constexpr std::array<Quality, 4> QUALITY = {{{0.3f, 1.35f, 8000, false, 0.35f},
                                                 {0.55f, 1.2f, 16000, true, 0.5f},
                                                 {0.8f, 1.08f, 28000, true, 0.75f},
                                                 {1.0f, 1.0f, 40000, true, 1.0f}}};

    struct Stats
    {
        uint32_t live = 0, spawned = 0, capped = 0, budgetDropped = 0, emitters = 0, built = 0;
        uint64_t spawnedTotal = 0, cappedTotal = 0, droppedTotal = 0;
    };

    struct Emitter
    {
        Spawn proto;
        float rate0 = 0, rate1 = 0, duration = 0, age = 0, acc = 0;
        uint32_t gen = 0;
        bool alive = false;
    };
    struct EmitterId
    {
        uint32_t index = ~0u, gen = 0;
        bool valid() const { return index != ~0u; }
    };

    inline float hgen(uint32_t& seed)
    {
        seed = hash32(seed + 0x9E3779B9u);
        return static_cast<float>(seed >> 8) / 16777216.0f;
    }

    class System
    {
    public:
        std::vector<Particle> parts;
        std::array<Style, STYLES> styles = defaultStyles();
        std::array<Lut, STYLES> luts{};
        std::array<AtlasInfo, ATLASES> atlas = ATLAS_DEFAULTS;
        std::vector<Emitter> emitters;
        struct NukeEmitter { V3 at{}; float age = 0; int tick = 0; bool alive = false; };
        std::array<NukeEmitter, 4> nukes{};
        uint32_t nukeLive = 0;
        static bool isNuke(uint16_t style) { return style >= S_NUKE_FLASH && style < STYLES; }
        struct DensityCell { int x = 0, y = 0, z = 0; float density = 0; };
        std::vector<DensityCell> smokeDensity = std::vector<DensityCell>(8192);
        V3 sunDirection{0, 1, 0};
        struct BattleCell { V3 centre{}; float mass = 0; };
        V3 viewer{};
        float hazeClock = 0;
        std::array<BattleCell, 256> battleHaze{};
        void addBattleSmoke(const V3& at, float mass)
        {
            const int x = int(std::floor((at.x + 4096) / 512)), z = int(std::floor((at.z + 4096) / 512));
            if (x < 0 || x >= 16 || z < 0 || z >= 16) return;
            auto& c = battleHaze[z * 16 + x];
            const float total = c.mass + mass;
            c.centre = total > 0 ? (c.centre * c.mass + at * mass) * (1.0f / total) : at;
            c.mass = std::min(30.0f, total);
        }
        static uint32_t densityKey(int x, int y, int z)
        {
            return hash32(uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ uint32_t(z) * 83492791u) & 8191u;
        }
        float densityAt(const V3& p) const
        {
            const int x = int(std::floor(p.x / 20)), y = int(std::floor(p.y / 20)), z = int(std::floor(p.z / 20));
            const auto& c = smokeDensity[densityKey(x, y, z)];
            return c.x == x && c.y == y && c.z == z ? c.density : 0;
        }
        vfx::Wind wind;
        Rng rng{20261116};
        float time = 0;
        uint32_t frame = 0;
        uint32_t cap = 40000, budget = 2500;  
        float countScale = 1, sizeScale = 1, maxScreen = 1;
        bool soft = true;
        int quality = 3;
        Stats stats;
        uint32_t spawnedThisFrame = 0;
        bool playerEffects = false;
         
        std::vector<GpuParticle> scratch;
        std::vector<uint16_t> keys;
        std::vector<uint32_t> order, order2;

        System() { rebuildLuts(); }
        void rebuildLuts()
        {
            for (int i = 0; i < STYLES; ++i)
                luts[i] = buildLut(styles[i]);
        }
        void setQuality(int level)
        {
            quality = std::clamp(level, 0, 3);
            const Quality& q = QUALITY[quality];
            countScale = q.count;
            sizeScale = q.size;
            cap = q.cap;
            soft = q.soft;
            maxScreen = q.maxScreen;
            nukeLive = 0;
            std::erase_if(parts, [&](const Particle& p) { return isNuke(p.style) && ++nukeLive > cap / 8; });
            nukeLive = std::min(nukeLive, cap / 8);
            if (parts.size() > cap) {
                std::stable_partition(parts.begin(), parts.end(), [](const auto& p) { return p.prio == 3; });
                parts.resize(cap);
            }
            nukeLive = static_cast<uint32_t>(std::count_if(parts.begin(), parts.end(), [](const Particle& p) { return isNuke(p.style); }));
            int haze = 0;
            std::erase_if(parts, [&](const Particle& p) { return p.style == S_BATTLE_HAZE && ++haze > quality * 16; });
        }
        void clear()
        {
            parts.clear();
            emitters.clear();
            nukes.fill({}); nukeLive = 0;
            battleHaze.fill({});
            hazeClock = 0;
        }

         
         
        float pressure() const
        {
            if (playerEffects) return 1;
            const float fill = static_cast<float>(parts.size()) / static_cast<float>(std::max(cap, 1u));
            return std::clamp((0.9f - fill) / 0.4f, 0.25f, 1.0f);
        }

         
        int scaled(float n, int least = 0)
        {
            const float v = n * countScale;
            int k = static_cast<int>(v);
            if (rng.f01() < v - static_cast<float>(k))
                ++k;
            return std::max(k, least);
        }

        bool admit(uint8_t prio)
        {
            const uint32_t limit = prio == 3 ? cap : cap - std::min(1024u, cap / 4);
            if (parts.size() >= limit)
            {
                if (!cap || prio < 2 || parts.empty())
                {
                    ++stats.capped;
                    return false;
                }
                 
                ++stats.capped;
                return true;
            }
            if (spawnedThisFrame >= budget - std::min(128u, budget / 4) && prio < 2)
            {
                ++stats.budgetDropped;
                return false;
            }
            return true;
        }

        Particle make(const Spawn& s)
        {
            Particle p;
            V3 off{};
            if (s.posR > 0.0f)
            {
                 
                for (int i = 0; i < 8; ++i)
                {
                    off = {rng.range(-1.0f, 1.0f), s.flat ? 0.0f : rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f)};
                    if (dot(off, off) <= 1.0f)
                        break;
                }
                off = off * s.posR;
            }
            p.pos = s.pos + off;
            p.vel = s.vel;
            if (s.velR > 0.0f)
            {
                V3 d{};
                for (int i = 0; i < 8; ++i)
                {
                    d = {rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f)};
                    if (dot(d, d) <= 1.0f)
                        break;
                }
                p.vel += d * s.velR;
            }
            if (s.radial != 0.0f)
            {
                const V3 out = dot(off, off) > 1e-6f ? norm(off) : norm(V3{rng.range(-1.0f, 1.0f), s.flat ? 0.0f : rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f)});
                p.vel += out * (s.radial * (1.0f + rng.range(-s.radialJ, s.radialJ)));
            }
            p.life = std::max(0.02f, s.life * (1.0f + rng.range(-s.lifeJ, s.lifeJ)));
            const float k = (1.0f + rng.range(-s.sizeJ, s.sizeJ)) * sizeScale;
            p.s0 = s.s0 * k;
            p.s1 = s.s1 * k;
            p.rot = rng.range(0.0f, 6.2831853f);
            p.spin = s.spin * rng.range(-1.0f, 1.0f);
            p.drag = s.drag;
            p.rise = s.riseJ > 0.0f ? s.rise * (1.0f + rng.range(-s.riseJ, s.riseJ)) : s.rise;
            p.windK = s.windK;
            p.gravity = s.gravity;
            p.heat = s.heat;
            p.age = -std::max(0.0f, s.delay + rng.range(0.0f, s.delayJ));
            p.style = s.style;
            p.trailStyle = s.trailStyle;
            p.flags = s.flags | (s.trailStyle != NO_STYLE ? P_TRAIL : 0);
            p.prio = s.prio;
            p.frame = static_cast<uint8_t>(rng.irange(0, 255));
            p.tint = s.tint;
            if (s.tintJ > 0.0f)
            {
                const float b = 1.0f + rng.range(-s.tintJ, s.tintJ);
                const auto ch = [&](int sh) { return static_cast<uint32_t>(u8of(static_cast<float>((s.tint >> sh) & 255u) / 255.0f * b)) << sh; };
                p.tint = ch(0) | ch(8) | ch(16) | (s.tint & 0xFF000000u);
            }
            const V3 w = wind.at(time, p.pos.x, p.pos.z, 30.0f);
            p.windX = w.x;
            p.windZ = w.z;
            return p;
        }

        bool spawn(const Spawn& s)
        {
            const uint8_t priority = playerEffects ? 3 : s.prio;
            if (!admit(priority))
                return false;
            ++spawnedThisFrame;
            ++stats.spawned;
            ++stats.spawnedTotal;
            const uint32_t limit = priority == 3 ? cap : cap - std::min(1024u, cap / 4);
            if (parts.size() < limit) {
                parts.push_back(make(s));
                parts.back().prio = priority;
            }
            else {
                size_t i = (frame * 7919u + spawnedThisFrame * 104729u) % parts.size();
                for (size_t k = 0; k < std::min<size_t>(parts.size(), 64) && (parts[i].prio == 3 || isNuke(parts[i].style)); ++k)
                    i = (i + 1) % parts.size();
                if (priority != 3 && (parts[i].prio == 3 || isNuke(parts[i].style))) return false;
                parts[i] = make(s);
                parts[i].prio = priority;
            }
            return true;
        }
        int burst(const Spawn& s, int count)
        {
            int n = 0;
            for (int i = 0; i < count; ++i)
                n += spawn(s) ? 1 : 0;
            return n;
        }

        bool nukeSpawn(Spawn s)
        {
            if (nukeLive >= cap / 8) return false;
            s.prio = 2;
            if (!spawn(s)) return false;
            ++nukeLive;
            return true;
        }
        bool nuke(const V3& at)
        {
            for (auto& e : nukes) if (!e.alive) {
                e = {at, 0, 0, true};
                Spawn s; s.style = S_NUKE_FLASH; s.pos = at + V3{0, 20, 0};
                s.life = 0.24f; s.s0 = 120; s.s1 = 700; s.flags = P_STATIC;
                nukeSpawn(s);
                const int fireCount = std::max(10, int(36 * QUALITY[quality].count));
                for (int i = 0; i < fireCount; ++i) {
                    const float a = rng.range(0, 6.2831853f), radial = std::sqrt(rng.f01());
                    const V3 out{std::cos(a), 0, std::sin(a)};
                    Spawn fire;
                    fire.style = S_NUKE_FIRE;
                    fire.pos = at + out * (radial * 45) + V3{0, 8 + (1 - radial) * 40, 0};
                    fire.vel = out * (20 + radial * 45) + V3{0, 25 + 25 * (1 - radial), 0};
                    fire.life = 1.8f; fire.lifeJ = 0.2f; fire.delay = rng.range(0, 0.18f);
                    fire.s0 = 18 + radial * 10; fire.s1 = 75 + rng.range(0, 45);
                    fire.drag = 0.65f; fire.rise = 15; fire.heat = 1.2f; fire.spin = 0.15f;
                    nukeSpawn(fire);
                }
                const int count = std::max(24, int(96 * QUALITY[quality].count));
                for (int i = 0; i < count; ++i) {
                    const float a = 6.2831853f * i / count;
                    const V3 out{std::cos(a), 0, std::sin(a)};
                    s = {}; s.style = S_NUKE_DUST; s.pos = at + out * 18 + V3{0, 2, 0};
                    s.vel = out * 260; s.drag = 0.025f; s.life = 7; s.s0 = 28; s.s1 = 75;
                    s.flags = P_GROUNDED; s.tint = rgba8(0.68f, 0.61f, 0.48f, 0.8f);
                    nukeSpawn(s);
                    if (i % 2 == 0) {
                        s.vel = out * 16; s.drag = 0.4f; s.life = 18; s.s0 = 18; s.s1 = 65;
                        nukeSpawn(s);
                    }
                }
                return true;
            }
            return false;
        }
        void feedNukes(float dt)
        {
            for (auto& e : nukes) {
                if (!e.alive) continue;
                e.age += std::max(0.0f, dt);
                while (e.tick < 24 && e.tick * 0.25f <= e.age) {
                    const float t = e.tick++ * 0.25f;
                    const int count = std::max(4, int(28 * QUALITY[quality].count));
                    for (int i = 0; i < count; ++i) {
                        const float a = 6.2831853f * (i + t * 0.6f) / count;
                        const V3 out{std::cos(a), 0, std::sin(a)};
                        Spawn s; s.style = S_NUKE_CLOUD; s.life = 55; s.posR = 8;
                        s.sizeJ = 0.2f; s.spin = 0.12f; s.windK = 0.035f; s.drag = 0.055f;
                        if (i % 4 == 0) {
                            s.pos = e.at + out * 12 + V3{0, 20 + t * 52, 0};
                            s.vel = {0, 18, 0}; s.rise = 3 + t * 3; s.s0 = 40; s.s1 = 95;
                        } else {
                            const float radial = i % 4 == 1 ? rng.range(0.8f, 1.0f) : std::sqrt(rng.range(0.08f, 1.0f));
                            s.pos = e.at + out * ((48 + t * 30) * radial) + V3{0, 75 + t * 52 + 15 * (1 - radial) + rng.range(-15, 15), 0};
                            s.vel = out * (5 + 12 * radial) + V3{0, 38 - 22 * radial, 0};
                            s.rise = 18 + 18 * (1 - radial); s.s0 = 65 + radial * 15; s.s1 = 160 + radial * 45;
                        }
                        nukeSpawn(s);
                        if (t < 2 && i == 1) {
                            s.style = S_NUKE_FIRE; s.life = 2.2f; s.s0 = 65; s.s1 = 120; s.heat = 2;
                            nukeSpawn(s);
                        }
                    }
                }
                if (e.tick == 24) e.alive = false;
            }
        }

        EmitterId emit(const Spawn& proto, float rate0, float rate1, float duration)
        {
            uint32_t index = 0;
            while (index < emitters.size() && emitters[index].alive)
                ++index;
            if (index == emitters.size())
                emitters.emplace_back();
            Emitter& e = emitters[index];
            e.proto = proto;
            if (playerEffects) e.proto.prio = 3;
            e.rate0 = rate0 * countScale;
            e.rate1 = rate1 * countScale;
            e.duration = duration;
            e.age = 0;
            e.acc = rng.f01();
            e.alive = true;
            ++e.gen;
            return {index, e.gen};
        }
        Emitter* emitter(EmitterId id)
        {
            if (id.index >= emitters.size() || emitters[id.index].gen != id.gen || !emitters[id.index].alive)
                return nullptr;
            return &emitters[id.index];
        }
        void stop(EmitterId id)
        {
            if (Emitter* e = emitter(id))
                e->alive = false;
        }

         
        void begin(float dt)
        {
            time += dt;
            ++frame;
            spawnedThisFrame = 0;
            stats.spawned = stats.capped = stats.budgetDropped = 0;
            stats.emitters = 0;

            feedNukes(dt);
            const float decay = std::exp(-dt / 150.0f);
            for (auto& c : battleHaze) c.mass *= decay;
            hazeClock += dt;
            if (quality > 0 && hazeClock >= 8)
            {
                hazeClock = std::fmod(hazeClock, 8.0f);
                int hazeLive = 0;
                for (const auto& p : parts) hazeLive += p.style == S_BATTLE_HAZE;
                std::array<uint16_t, 256> order;
                for (uint16_t i = 0; i < 256; ++i) order[i] = i;
                const auto distance = [&](uint16_t i) { const auto d = battleHaze[i].centre - viewer; return dot(d,d); };
                std::sort(order.begin(), order.end(), [&](uint16_t a, uint16_t b) { return distance(a) < distance(b); });
                for (auto i : order)
                {
                    const auto& c = battleHaze[i];
                    if (hazeLive >= quality * 16) break;
                    const float reach = 1500.0f + quality * 500.0f;
                    if (c.mass < 0.5f || distance(i) > reach * reach) continue;
                    Spawn h;
                    h.style = S_BATTLE_HAZE, h.pos = c.centre + V3{0, 45, 0};
                    h.posR = 45, h.flat = true, h.life = 50, h.s0 = 100, h.s1 = 340;
                    h.rise = 0.4f, h.windK = 0.25f, h.spin = 0.015f;
                    h.tint = rgba8(0.5f, 0.52f, 0.55f, std::min(1.0f, c.mass / 6));
                    h.prio = 0;
                    hazeLive += spawn(h) ? 1 : 0;
                }
            }
            for (auto& e : emitters)
            {
                if (!e.alive)
                    continue;
                ++stats.emitters;
                const float u = e.duration > 0.0f ? std::min(1.0f, e.age / e.duration) : 0.0f;
                e.acc += (e.rate0 + (e.rate1 - e.rate0) * u) * dt;
                e.age += dt;
                for (; e.acc >= 1.0f; e.acc -= 1.0f)
                    spawn(e.proto);
                if (e.duration > 0.0f && e.age >= e.duration)
                    e.alive = false;
            }
        }

        float sizeOf(const Particle& p) const
        {
            const float u = std::clamp(p.age / p.life, 0.0f, 1.0f);
            return p.s0 + (p.s1 - p.s0) * luts[p.style].grow[static_cast<int>(u * (LUT - 1))];
        }

         
        template <class Ground>
        void simulate(uint32_t b, uint32_t e, float dt, Ground&& ground)
        {
            for (uint32_t i = b; i < e; ++i)
            {
                Particle& p = parts[i];
                p.age += dt;
                if (p.age < 0.0f || p.age >= p.life || (p.flags & P_STATIC))
                    continue;
                if (((i + frame) & 15u) == 0u && p.windK > 0.0f)
                {
                    const V3 w = wind.at(time, p.pos.x, p.pos.z, p.pos.y - (p.flags & P_GROUNDED ? ground(p.pos.x, p.pos.z) : 0.0f));
                    p.windX = w.x;
                    p.windZ = w.z;
                }
                if (p.flags & P_BALLISTIC)
                {
                    const float k = std::max(0.0f, 1.0f - p.drag * dt);
                    p.vel = p.vel * k;
                    p.vel.y -= p.gravity * dt;
                    if (p.windK > 0.0f)
                    {
                        const float kw = std::min(1.0f, p.windK * dt);
                        p.vel.x += (p.windX - p.vel.x) * kw;
                        p.vel.z += (p.windZ - p.vel.z) * kw;
                    }
                }
                else
                {
                    const float k = std::min(1.0f, (p.drag + p.windK) * dt);
                    const float tx = p.windK > 0.0f ? p.windX : 0.0f, tz = p.windK > 0.0f ? p.windZ : 0.0f;
                    p.vel.x += (tx - p.vel.x) * k;
                    p.vel.z += (tz - p.vel.z) * k;
                    const float want = p.rise * (p.style == S_NUKE_CLOUD ? std::exp(-p.age / 6.0f) : std::max(0.0f, 1.0f - p.age / p.life));
                    p.vel.y += (want - p.vel.y) * std::min(1.0f, (1.0f + p.drag) * dt);
                }
                p.pos += p.vel * dt;
                p.rot += p.spin * dt;
                if (p.flags & (P_GROUNDED | P_BOUNCE | P_DIE_GROUND))
                {
                    const float g = ground(p.pos.x, p.pos.z);
                    if (p.flags & P_GROUNDED)
                        p.pos.y = std::max(p.pos.y, g + sizeOf(p) * 0.22f);
                    else if (p.pos.y < g)
                    {
                        if (p.flags & P_DIE_GROUND)
                            p.age = p.life;
                        else
                        {
                            p.pos.y = g;
                            p.vel = {p.vel.x * 0.45f, std::abs(p.vel.y) * 0.3f, p.vel.z * 0.45f};
                            p.spin *= 0.5f;
                            if (std::abs(p.vel.y) < 0.8f)
                                p.flags &= static_cast<uint8_t>(~P_TRAIL);  
                        }
                    }
                }
            }
        }

         
        void finish(float dt)
        {
            const size_t n0 = parts.size();
            for (size_t i = 0; i < n0; ++i)
            {
                Particle& p = parts[i];
                if (!(p.flags & P_TRAIL) || p.age < 0.0f || p.age >= p.life)
                    continue;
                p.trailAcc += len(p.vel) * dt;
                if (p.trailAcc < TRAIL_SPACING * sizeScale)
                    continue;
                p.trailAcc = 0.0f;
                Spawn t;
                t.style = p.trailStyle;
                t.pos = p.pos;
                t.posR = 0.15f;
                t.vel = p.vel * 0.05f + V3{0, 0.4f, 0};
                t.life = 1.6f, t.lifeJ = 0.3f;
                t.s0 = 0.5f, t.s1 = 2.2f, t.sizeJ = 0.25f;
                t.rise = 0.6f, t.windK = 0.5f, t.drag = 1.2f;
                t.prio = 0;
                spawn(t);  
            }
            size_t n = parts.size();
            nukeLive = 0;
            for (size_t i = 0; i < n;)
            {
                if (parts[i].age >= parts[i].life)
                    parts[i] = parts[--n];
                else {
                    nukeLive += isNuke(parts[i].style);
                    ++i;
                }
            }
            parts.resize(n);
            stats.live = static_cast<uint32_t>(n);
            stats.cappedTotal += stats.capped;
            stats.droppedTotal += stats.budgetDropped;
        }

         
        template <class Ground>
        void step(float dt, Ground&& ground)
        {
            begin(dt);
            simulate(0, static_cast<uint32_t>(parts.size()), dt, ground);
            finish(dt);
        }

         
        void prepare()
        {
            std::fill(smokeDensity.begin(), smokeDensity.end(), DensityCell{});
            if (quality > 0)
                for (const auto& p : parts)
                {
                    if (p.age < 0 || p.age >= p.life || !(styles[p.style].flags & LIT) || styles[p.style].atlas == DEBRIS)
                        continue;
                    const int x = int(std::floor(p.pos.x / 20)), y = int(std::floor(p.pos.y / 20)), z = int(std::floor(p.pos.z / 20));
                    auto& c = smokeDensity[densityKey(x, y, z)];
                    if (c.x != x || c.y != y || c.z != z)
                        c = {x, y, z, 0};
                    const float u = std::clamp(p.age / p.life, 0.0f, 1.0f);
                    c.density = std::min(3.0f, c.density + std::min(0.3f, sizeOf(p) * 0.008f) * (1-u) * float(p.tint >> 24) / 255.0f);
                }
            scratch.resize(parts.size());
            keys.resize(parts.size());
        }
         
        void pack(uint32_t b, uint32_t e, const V3& eye, const std::array<uint32_t, ATLASES>& tex)
        {
            for (uint32_t i = b; i < e; ++i)
            {
                const Particle& p = parts[i];
                const Style& st = styles[p.style];
                const uint32_t t = tex[st.atlas];
                if (p.age < 0.0f || t == 0xFFFFFFFFu)
                {
                    keys[i] = 0xFFFF;  
                    continue;
                }
                const Lut& l = luts[p.style];
                const float u = std::clamp(p.age / p.life, 0.0f, 1.0f);
                const float fi = u * (LUT - 1);
                const int i0 = std::min(static_cast<int>(fi), LUT - 2);
                const uint32_t c = l.colour[i0];
                 
                const auto mul = [&](int sh) { return ((c >> sh & 255u) * (p.tint >> sh & 255u) + 127u) / 255u << sh; };
                const uint32_t colour = mul(0) | mul(8) | mul(16) | mul(24);
                const float em = l.emissive[i0] * p.heat;
                if ((colour >> 24) == 0u && em < 0.01f)
                {
                    keys[i] = 0xFFFF;
                    continue;
                }
                GpuParticle& g = scratch[i];
                const float size = p.s0 + (p.s1 - p.s0) * l.grow[i0];
                g.x = p.pos.x, g.y = p.pos.y, g.z = p.pos.z;
                g.size = size * 0.5f;
                g.colour = colour;
                g.tex = t;
                const AtlasInfo& a = atlas[st.atlas];
                uint32_t f0 = 0, f1 = 0, blend = 0;
                const uint32_t frames = std::max<uint32_t>(1u, a.frames);
                if (a.mode == RANDOM)
                    f0 = f1 = p.frame % frames;
                else
                {
                    const float ff = a.mode == PLAY ? std::min(1.0f, u + (p.style == S_FIREBALL ? float(p.frame % 6u) / 64.0f : 0.0f)) * static_cast<float>(frames - 1)
                                                    : std::fmod(p.age * a.fps + static_cast<float>(p.frame), static_cast<float>(frames));
                    f0 = std::min(static_cast<uint32_t>(ff), frames - 1);
                    f1 = a.mode == PLAY ? std::min(f0 + 1, frames - 1) : (f0 + 1) % frames;
                    blend = u8of(ff - static_cast<float>(f0));
                }
                uint32_t flags = 0;
                flags |= (st.flags & LIT) ? GPU_LIT : 0u;
                flags |= (st.flags & STRETCH) ? GPU_STRETCH : 0u;
                flags |= (st.flags & SOFT) && soft ? GPU_SOFT : 0u;
                flags |= (t >> 16) != 0xFFFFu ? GPU_NORMAL : 0u;
                flags |= static_cast<uint32_t>(std::countr_zero(static_cast<uint32_t>(a.grid))) << GPU_GRID_SHIFT;
                if (st.atlas == MUZZLE)
                {
                    flags |= GPU_NORMAL;
                    const float angle = std::abs(dot(norm(p.vel), norm(eye - p.pos)));
                    f0 = f1 = (angle > 0.7f ? 0u : 32u) + p.frame % 32u;
                    blend = 0;
                }
                flags |= (p.flags & P_PLANE) ? GPU_PLANE : 0u;
                g.frame = f0 | f1 << 8 | blend << 16 | flags << 24;
                g.emission = l.emission[i0] | static_cast<uint32_t>(std::min(255.0f, st.soft * 50.0f)) << 24;
                g.emissive = em;
                if (st.flags & STRETCH)
                {
                    const float speed = len(p.vel);
                    const float length = std::max(speed * st.stretch, size);
                    const V3 back = speed > 1e-3f ? p.vel * (-length / speed) : V3{0, -size, 0};
                    g.c0 = back.x, g.c1 = back.y, g.c2 = back.z;
                }
                else
                {
                    g.c0 = p.rot, g.c1 = 1.0f, g.c2 = 0.0f;
                    if ((st.flags & LIT) && quality > 0)
                    {
                        float optical = 0;
                        for (int tap = 1; tap <= quality; ++tap)
                            optical += densityAt(p.pos + sunDirection * (20.0f * tap));
                        g.c1 = std::exp(-optical * 0.6f);
                    }
                    if (st.atlas == MUZZLE)
                        g.c0 = p.vel.x, g.c1 = p.vel.y, g.c2 = p.vel.z;
                }
                const V3 d = p.pos - eye;
                const float dist = std::sqrt(dot(d, d));
                keys[i] = static_cast<uint16_t>(65534u - std::min(65534u, static_cast<uint32_t>(dist * 4.0f)));
            }
        }
         
        uint32_t gather(GpuParticle* out, uint32_t max)
        {
            const uint32_t n = static_cast<uint32_t>(parts.size());
            order.clear();
            order.reserve(n);
            for (uint32_t i = 0; i < n; ++i)
                if (keys[i] != 0xFFFF)
                    order.push_back(i);
            order2.resize(order.size());
            for (int pass = 0; pass < 2; ++pass)
            {
                uint32_t count[257] = {};
                const int sh = pass * 8;
                for (uint32_t i : order)
                    ++count[((keys[i] >> sh) & 255u) + 1];
                for (int k = 0; k < 256; ++k)
                    count[k + 1] += count[k];
                for (uint32_t i : order)
                    order2[count[(keys[i] >> sh) & 255u]++] = i;
                order.swap(order2);
            }
            const uint32_t m = std::min<uint32_t>(max, static_cast<uint32_t>(order.size()));
             
            const uint32_t first = static_cast<uint32_t>(order.size()) - m;
            for (uint32_t k = 0; k < m; ++k)
                out[k] = scratch[order[first + k]];
            stats.built = m;
            return m;
        }
    };

    struct PlayerEffects
    {
        System& system;
        bool previous;
        float count, size;
        PlayerEffects(System& s, bool player) : system(s), previous(s.playerEffects), count(s.countScale), size(s.sizeScale)
        {
            s.playerEffects |= player;
            if (s.playerEffects) { s.countScale = 1; s.sizeScale = 1; }
        }
        ~PlayerEffects() { system.playerEffects = previous; system.countScale = count; system.sizeScale = size; }
    };

     
     
    inline float tierSize(vfx::Tier t) { return t == vfx::NEAR ? 1.0f : t == vfx::MID ? 1.25f : t == vfx::FAR ? 1.7f : 2.4f; }
    inline float tierCount(vfx::Tier t) { return t == vfx::NEAR ? 1.0f : t == vfx::MID ? 0.45f : t == vfx::FAR ? 0.18f : 0.08f; }
    inline uint32_t grey(float v, float a = 1.0f) { return rgba8(v, v, v, a); }
    inline uint32_t tintOf(const V3& c, float a = 1.0f) { return rgba8(c.x, c.y, c.z, a); }

    enum Surface : uint8_t
    {
        S_GROUND_DIRT,
        S_WALL,
        S_ARMOUR
    };

    struct ExplosionParts
    {
        int flash = 0, fire = 0, hotSmoke = 0, column = 0, skirt = 0, ring = 0, debris = 0, sparks = 0;
        int total() const { return flash + fire + hotSmoke + column + skirt + ring + debris + sparks; }
    };

     
     
     
    inline ExplosionParts explosion(System& sys, const V3& at, float r, float agl, vfx::Tier tier, const V3& dustColour)
    {
        ExplosionParts n;
        const float rr = std::max(r, 0.8f);
        const float ts = tierSize(tier), tc = tierCount(tier);
        const bool air = agl > rr * 2.0f;
        sys.addBattleSmoke(at, rr * 0.12f);
        const float k = std::sqrt(rr / 5.0f);  
         
        {
            Spawn s;
            s.style = S_GLOW, s.pos = at + V3{0, rr * 0.3f, 0}, s.life = 0.16f, s.s0 = rr * 2.0f * ts, s.s1 = rr * 3.2f * ts;
            s.heat = 1.6f, s.prio = 2;
            n.flash += sys.burst(s, 1);
            s.style = S_FLASH, s.life = 0.11f, s.s0 = rr * 2.2f * ts, s.s1 = rr * 3.6f * ts, s.heat = 1.3f;
            n.flash += sys.burst(s, tier <= vfx::MID ? 2 : 1);
        }
         
        {
            Spawn s;
            s.style = S_FIREBALL, s.pos = at + V3{0, rr * 0.35f, 0}, s.posR = rr * 0.55f;
            s.radial = rr * 2.6f, s.radialJ = 0.5f, s.vel = {0, rr * 0.6f, 0}, s.drag = 3.5f, s.rise = rr * 0.5f;
            s.life = 0.9f + 0.08f * rr, s.lifeJ = 0.3f, s.s0 = rr * 0.55f * ts, s.s1 = rr * 1.6f * ts, s.sizeJ = 0.3f;
            s.delayJ = 0.035f, s.spin = 1.4f, s.prio = 2;
            n.fire = sys.burst(s, sys.scaled(28.0f * k * std::max(tc, 0.15f), 2));
        }
         
        {
            Spawn s;
            s.style = S_SMOKE_HOT, s.pos = at + V3{0, rr * 0.6f, 0}, s.posR = rr * 0.6f;
            s.radial = rr * 1.2f, s.radialJ = 0.5f, s.drag = 1.8f, s.rise = 2.5f + rr * 0.4f, s.windK = 0.25f;
            s.life = 7.0f + rr * 0.6f, s.lifeJ = 0.3f, s.s0 = rr * 1.1f * ts, s.s1 = rr * 3.6f * ts, s.sizeJ = 0.3f;
            s.delay = 0.12f, s.delayJ = 0.35f, s.tint = grey(0.75f), s.tintJ = 0.15f, s.spin = 0.25f, s.prio = 1;
            n.hotSmoke = sys.burst(s, sys.scaled(12.0f * k * std::max(tc, 0.12f), 1));
        }
         
        {
            Spawn s;
            s.style = S_FIRE_SMOKE, s.pos = at + V3{0, rr * 0.45f, 0}, s.posR = rr * 0.4f;
            s.radial = rr * 1.6f, s.radialJ = 0.5f, s.vel = {0, rr * 0.5f, 0}, s.drag = 2.5f, s.rise = 1.5f + rr * 0.35f, s.windK = 0.2f;
            s.life = 2.2f + 0.2f * rr, s.lifeJ = 0.3f, s.s0 = rr * 0.8f * ts, s.s1 = rr * 2.4f * ts, s.sizeJ = 0.3f;
            s.delayJ = 0.2f, s.tint = grey(0.8f), s.tintJ = 0.15f, s.spin = 0.4f, s.prio = 1;
            n.hotSmoke += sys.burst(s, sys.scaled(6.0f * k * std::max(tc, 0.12f), 1));
        }
         
         
        {
            const float far = tier >= vfx::FAR ? sys.pressure() : 1.0f;  
            const float life = std::clamp(14.0f + rr * 2.6f, 14.0f, 36.0f) * (tier >= vfx::FAR ? 0.85f : 1.0f) * (0.6f + 0.4f * far);
            Spawn s;
            s.style = S_SMOKE, s.pos = at + V3{0, rr * 0.9f, 0}, s.posR = rr * 0.55f;
            s.vel = {0, 3.0f + rr * 0.6f, 0}, s.velR = 1.0f, s.drag = 0.5f, s.rise = 2.0f + rr * 0.35f, s.riseJ = 0.25f, s.windK = 0.3f;
            s.life = life, s.lifeJ = 0.2f, s.s0 = rr * 1.5f * ts, s.s1 = rr * 6.0f * ts, s.sizeJ = 0.3f;
            s.delay = 0.3f, s.delayJ = 0.8f, s.tint = grey(0.85f), s.tintJ = 0.2f, s.spin = 0.12f, s.prio = 1;
            n.column = sys.burst(s, sys.scaled(10.0f * k * std::max(tc, 0.4f * far), 1));
            if (!air)
            {
                const float feed = std::clamp(2.0f + rr * 0.7f, 3.0f, 12.0f);
                Spawn st = s;
                st.style = S_PLUME, st.pos = at + V3{0, rr * 0.4f, 0}, st.posR = rr * 0.35f, st.vel = {0, 2.0f + rr * 0.5f, 0};
                st.rise = 1.2f + rr * 0.28f, st.riseJ = 0.6f, st.s0 = rr * 1.0f * ts, st.s1 = rr * 3.4f * ts, st.delay = 0.6f, st.delayJ = 0.4f;
                st.life = life * 0.85f, st.tint = grey(0.6f), st.vel = {0, 1.5f + rr * 0.3f, 0};
                const float total = 26.0f * k * std::max(tc, 0.3f * far);
                sys.emit(st, 2.0f * total / feed, 0.0f, feed);
                n.column += static_cast<int>(total);
            }
        }
        if (!air && rr >= 3 && tier <= vfx::MID && sys.quality > 0)
        {
            Spawn ring;
            ring.style = S_SHOCK, ring.pos = {at.x, at.y - agl + 0.24f, at.z};
            ring.flags = P_STATIC | P_PLANE, ring.life = 0.65f;
            ring.s0 = rr * 2, ring.s1 = rr * 9, ring.heat = 0.7f;
            n.ring += sys.burst(ring, 1);
        }
        if (!air)
        {
             
            {
                Spawn d;
                d.style = S_DUST, d.pos = at + V3{0, rr * 0.3f, 0}, d.posR = rr * 0.3f;
                d.vel = {0, rr * 2.2f, 0}, d.velR = rr * 1.0f, d.drag = 1.4f, d.rise = 0.5f, d.windK = 0.25f;
                d.life = 7.0f + rr * 0.8f, d.lifeJ = 0.3f, d.s0 = rr * 0.9f * ts, d.s1 = rr * 2.8f * ts, d.sizeJ = 0.3f;
                d.tint = tintOf(dustColour), d.tintJ = 0.12f, d.spin = 0.2f, d.delayJ = 0.1f, d.prio = 1;
                n.skirt += sys.burst(d, sys.scaled(10.0f * k * std::max(tc, 0.12f), 1));
            }
             
            Spawn s;
            s.style = S_DUST_BURST, s.pos = at + V3{0, 0.3f, 0}, s.posR = rr * 0.6f, s.flat = true;
            s.radial = rr * 2.2f, s.radialJ = 0.4f, s.vel = {0, 0.6f, 0}, s.drag = 1.6f, s.rise = 0.6f, s.windK = 0.25f;
            s.life = 6.0f + rr * 0.8f, s.lifeJ = 0.35f, s.s0 = rr * 0.8f * ts, s.s1 = rr * 3.0f * ts, s.sizeJ = 0.3f;
            s.tint = tintOf(dustColour), s.tintJ = 0.12f, s.flags = P_GROUNDED, s.spin = 0.3f, s.delayJ = 0.15f, s.prio = 1;
            n.skirt = sys.burst(s, sys.scaled(16.0f * k * std::max(tc, 0.12f), 1));
            if (tier <= vfx::MID)
            {
                 
                s.radial = rr * 6.0f, s.radialJ = 0.15f, s.drag = 4.0f, s.rise = 0.2f, s.life = 1.2f, s.lifeJ = 0.2f;
                s.s0 = rr * 0.35f * ts, s.s1 = rr * 1.1f * ts, s.posR = rr * 0.3f, s.delayJ = 0.04f, s.prio = 0;
                s.tint = tintOf(dustColour, 0.7f);
                n.ring += sys.burst(s, sys.scaled(22.0f * k * tc));
            }
        }
        if (tier <= vfx::MID)
        {
             
            Spawn s;
            s.style = S_DEBRIS, s.pos = at + V3{0, rr * 0.2f, 0}, s.posR = rr * 0.25f;
            s.vel = {0, rr * 2.4f, 0}, s.velR = rr * 3.0f, s.drag = 0.15f, s.gravity = 9.81f;
            s.life = 2.6f, s.lifeJ = 0.4f, s.s0 = s.s1 = 0.35f + rr * 0.04f, s.sizeJ = 0.5f, s.spin = 9.0f;
            s.flags = P_BALLISTIC | P_BOUNCE, s.tint = tintOf(dustColour * 0.9f), s.tintJ = 0.2f, s.prio = 0;
            n.debris = sys.burst(s, sys.scaled(16.0f * k * tc));
            s.trailStyle = air ? S_SMOKE : S_DUST, s.s0 = s.s1 = 0.6f + rr * 0.05f, s.vel = {0, rr * 3.0f, 0}, s.velR = rr * 3.2f;
            s.tint = grey(0.35f);
            n.debris += sys.burst(s, sys.scaled(5.0f * k * tc));
            Spawn sp;
            sp.style = S_SPARK, sp.pos = at + V3{0, rr * 0.3f, 0}, sp.posR = rr * 0.2f;
            sp.vel = {0, rr * 3.0f, 0}, sp.velR = rr * 7.0f, sp.drag = 0.9f, sp.gravity = 9.81f;
            sp.life = 0.8f, sp.lifeJ = 0.5f, sp.s0 = sp.s1 = 0.12f + rr * 0.01f, sp.sizeJ = 0.4f;
            sp.flags = P_BALLISTIC | P_DIE_GROUND, sp.prio = 0;
            n.sparks = sys.burst(sp, sys.scaled(30.0f * k * tc));
        }
        return n;
    }

     
    inline int smallBlast(System& sys, const V3& at, float r, vfx::Tier tier, const V3& dustColour)
    {
        const float rr = std::max(r, 0.5f), ts = tierSize(tier);
        int n = 0;
        Spawn s;
        s.style = S_FLASH, s.pos = at, s.life = 0.08f, s.s0 = rr * 2.5f * ts, s.s1 = rr * 3.5f * ts, s.prio = 1;
        n += sys.burst(s, 1);
        s.style = S_FIREBALL, s.posR = rr * 0.3f, s.radial = rr * 2.0f, s.drag = 4.0f, s.rise = 1.0f, s.life = 0.45f, s.lifeJ = 0.3f;
        s.s0 = rr * 0.8f * ts, s.s1 = rr * 1.8f * ts, s.sizeJ = 0.3f;
        n += sys.burst(s, tier <= vfx::MID ? 2 : 1);
        s = Spawn{};
        s.style = tier <= vfx::MID ? S_DUST : S_SMOKE, s.pos = at, s.posR = rr * 0.4f, s.radial = rr * 1.2f, s.drag = 2.0f, s.rise = 1.2f;
        s.windK = 0.4f, s.life = 2.6f, s.lifeJ = 0.3f, s.s0 = rr * 1.0f * ts, s.s1 = rr * 3.0f * ts, s.sizeJ = 0.3f, s.delayJ = 0.05f;
        s.tint = tier <= vfx::MID ? tintOf(dustColour) : grey(0.7f), s.flags = P_GROUNDED, s.prio = 0;
        n += sys.burst(s, sys.scaled(tier <= vfx::MID ? 4.0f : 1.5f, 1));
        if (tier <= vfx::MID)
        {
            Spawn sp;
            sp.style = S_SPARK, sp.pos = at, sp.vel = {0, 4.0f, 0}, sp.velR = 16.0f, sp.drag = 1.0f, sp.gravity = 9.81f;
            sp.life = 0.45f, sp.lifeJ = 0.4f, sp.s0 = sp.s1 = 0.08f, sp.flags = P_BALLISTIC | P_DIE_GROUND, sp.prio = 0;
            n += sys.burst(sp, sys.scaled(10.0f * tierCount(tier)));
        }
        return n;
    }

     
    inline int muzzle(System& sys, const V3& at, const V3& dir, int kind, float agl, vfx::Tier tier, const V3& dustColour, uint32_t round = 0)
    {
        const float ts = tierSize(tier);
        const float big = kind == 2 ? 1.0f : kind == 1 ? 0.45f : 0.22f;
        int n = 0;
        Spawn s;
         
        s.style = S_MUZZLE, s.flags = P_STATIC, s.vel = dir, s.pos = at + dir * (0.5f * big), s.life = kind == 2 ? 0.09f : 0.05f, s.s0 = 2.4f * big * ts, s.s1 = 3.2f * big * ts;
        s.heat = 1.2f, s.prio = 1;
        n += sys.burst(s, 1);
        s.style = S_GLOW, s.flags = 0, s.vel = {}, s.life = kind == 2 ? 0.12f : 0.05f, s.s0 = 3.0f * big * ts, s.s1 = 4.5f * big * ts, s.heat = 1.0f;
        n += sys.burst(s, 1);
        if (tier <= vfx::MID)
        {
            s.style = S_FIREBALL, s.pos = at + dir * (1.2f * big), s.vel = dir * (30.0f * big), s.velR = 2.0f * big, s.drag = 9.0f;
            s.life = kind == 2 ? 0.22f : 0.07f, s.s0 = 0.9f * big, s.s1 = 2.2f * big, s.sizeJ = 0.3f, s.heat = 1.3f;
            n += sys.burst(s, kind == 2 ? 3 : 1);
        }
         
        const bool smoke = kind == 2 || (kind == 1 && round % 3 == 0) || (kind == 0 && round % 6 == 0 && tier == vfx::NEAR);
        if (smoke)
        {
            Spawn p;
            p.style = S_SMOKE_LIGHT, p.pos = at + dir * (1.5f * big), p.posR = 0.4f * big;
            p.vel = dir * (14.0f * big) + V3{0, 0.6f, 0}, p.velR = 1.5f * big, p.drag = 2.4f, p.rise = 0.8f, p.windK = 0.5f;
            p.life = kind == 2 ? 5.0f : 2.2f, p.lifeJ = 0.3f, p.s0 = 1.0f * big * ts, p.s1 = 4.5f * big * ts, p.sizeJ = 0.3f;
            p.tint = grey(1.0f, kind == 2 ? 0.9f : 0.6f), p.tintJ = 0.06f, p.spin = 0.4f, p.prio = 0;
            n += sys.burst(p, sys.scaled(kind == 2 ? 10.0f * tierCount(tier) : 1.0f, 1));
        }
         
        if (kind == 2 && agl < 4.0f && tier <= vfx::FAR)
        {
            Spawn d;
            d.style = S_DUST, d.pos = at + V3{0, -agl + 0.3f, 0} + dir * 2.0f, d.posR = 1.5f, d.flat = true;
            d.radial = 9.0f, d.radialJ = 0.4f, d.vel = dir * 4.0f + V3{0, 0.5f, 0}, d.drag = 2.2f, d.rise = 0.5f, d.windK = 0.3f;
            d.life = 3.5f, d.lifeJ = 0.3f, d.s0 = 1.2f * ts, d.s1 = 5.0f * ts, d.sizeJ = 0.3f;
            d.tint = tintOf(dustColour, 0.85f), d.flags = P_GROUNDED, d.prio = 0;
            n += sys.burst(d, sys.scaled(12.0f * tierCount(tier), 1));
        }
        return n;
    }

     
    inline int impact(System& sys, const V3& at, const V3& normal, Surface surface, float size, vfx::Tier tier, const V3& dustColour)
    {
        const float ts = tierSize(tier);
        int n = 0;
        if (surface == S_ARMOUR || (surface == S_WALL && sys.rng.chance(0.5f)) || sys.rng.chance(0.2f))
        {
            Spawn sp;
            sp.style = S_SPARK, sp.pos = at + normal * 0.1f, sp.vel = normal * 8.0f, sp.velR = 9.0f, sp.drag = 1.2f, sp.gravity = 9.81f;
            sp.life = 0.35f, sp.lifeJ = 0.5f, sp.s0 = sp.s1 = 0.06f * size * std::sqrt(ts), sp.flags = P_BALLISTIC | P_DIE_GROUND, sp.prio = 0;
            n += sys.burst(sp, sys.scaled((surface == S_ARMOUR ? 12.0f : 6.0f) * std::max(tierCount(tier), 0.15f), 1));
            Spawn g;
            g.style = S_GLOW, g.pos = at + normal * 0.15f, g.life = 0.06f, g.s0 = 0.5f * size * ts, g.s1 = 0.8f * size * ts, g.prio = 0;
            n += sys.burst(g, 1);
        }
        if (surface != S_ARMOUR || tier <= vfx::MID)
        {
            Spawn d;
            d.style = surface == S_ARMOUR ? S_SMOKE_LIGHT : S_DUST, d.pos = at + normal * 0.3f, d.posR = 0.2f * size;
            d.vel = normal * 3.0f, d.velR = 1.0f, d.drag = 2.0f, d.rise = 0.5f, d.windK = 0.4f;
            d.life = surface == S_ARMOUR ? 0.9f : 2.2f, d.lifeJ = 0.3f, d.s0 = 0.4f * size * ts, d.s1 = 1.8f * size * ts, d.sizeJ = 0.3f;
            d.tint = surface == S_ARMOUR ? grey(0.7f, 0.5f) : tintOf(dustColour), d.prio = 0;
            n += sys.burst(d, sys.scaled(surface == S_ARMOUR ? 1.0f : 3.0f, 1));
        }
        if (surface == S_WALL && tier <= vfx::MID)
        {
            Spawn c;
            c.style = S_DEBRIS, c.pos = at + normal * 0.2f, c.vel = normal * 5.0f + V3{0, 2.0f, 0}, c.velR = 4.0f, c.gravity = 9.81f;
            c.drag = 0.2f, c.life = 1.4f, c.lifeJ = 0.3f, c.s0 = c.s1 = 0.12f * size, c.sizeJ = 0.5f, c.spin = 12.0f;
            c.flags = P_BALLISTIC | P_BOUNCE, c.tint = tintOf(dustColour * 0.95f), c.tintJ = 0.2f, c.prio = 0;
            n += sys.burst(c, sys.scaled(5.0f * tierCount(tier), 1));
        }
        return n;
    }

     
     
     
     
    inline int tracer(System& sys, const V3& head, const V3& prev, const V3& dir, float length, float width, float persist, float dt, vfx::Tier tier)
    {
        const float stretch = sys.styles[S_TRACER].stretch;
        const float frame = std::max(dt, 1e-3f) * 1.5f;
        int n = 0;
        Spawn s;
        s.style = S_TRACER, s.pos = head, s.vel = dir * (length / stretch), s.flags = P_STATIC, s.prio = 0;
        s.life = frame, s.s0 = s.s1 = width;
        n += sys.spawn(s) ? 1 : 0;
        if (persist > frame)
        {
            Spawn t = s;
            t.style = S_TRAIL_GLOW, t.vel = (head - prev) * (1.0f / sys.styles[S_TRAIL_GLOW].stretch), t.life = persist, t.s0 = t.s1 = width * 1.6f;
            n += sys.spawn(t) ? 1 : 0;
        }
        if (tier <= vfx::MID)
        {
            Spawn h;
            h.style = S_GLOW, h.pos = head, h.flags = P_STATIC, h.life = frame, h.s0 = h.s1 = width * 3.0f, h.heat = 0.8f, h.prio = 0;
            n += sys.spawn(h) ? 1 : 0;
        }
        return n;
    }

     
    struct TrailState
    {
        float acc = 0;
    };
    inline int trail(System& sys, TrailState& ts, const V3& from, const V3& to, float spacing, const Spawn& proto)
    {
        const V3 d = to - from;
        const float l = len(d);
        if (l <= 1e-4f || spacing <= 0.0f)
            return 0;
        int n = 0;
        float t = spacing - ts.acc;
        for (; t <= l && n < 64; t += spacing)
        {
            Spawn s = proto;
            s.pos = from + d * (t / l);
            n += sys.spawn(s) ? 1 : 0;
        }
        ts.acc = std::fmod(ts.acc + l, spacing);
        return n;
    }
    inline void jetExhaust(System& sys, const V3& left, const V3& right, const V3& forward,
                           float throttle, float time, float dt, vfx::Tier tier)
    {
        if (tier > vfx::MID || dt <= 0) return;
        const float heat = std::clamp((throttle - 0.65f) / 0.35f, 0.0f, 1.0f);
        const float pulse = 1 + 0.04f * std::sin(time * 71) + 0.025f * std::sin(time * 113);
        const float length = 3 * (0.35f + 4.5f * heat) * pulse;
        for (const V3& nozzle : {left, right}) {
            Spawn s;
            s.style = S_JET_CORE; s.pos = nozzle; s.vel = forward * length; s.flags = P_STATIC;
            s.life = std::max(dt, 0.001f) * 1.5f; s.s0 = s.s1 = 9 * (0.45f + 0.12f * heat);
            s.heat = 0.25f + 0.75f * std::clamp(throttle, 0.0f, 1.0f);
            sys.spawn(s);
            if (heat < 0.05f) continue;
            s.style = S_JET_PLUME; s.pos = nozzle - forward * 0.2f;
            s.vel = forward * (length * 1.2f); s.s0 = s.s1 = 9 * 0.85f * pulse; s.heat = heat;
            sys.spawn(s);
            if (tier == vfx::NEAR && sys.quality > 0) {
                const int count = sys.quality > 1 ? 4 : 2;
                for (int i = 0; i < count; ++i) {
                    const float u = static_cast<float>(i + 1) / (count + 1);
                    s.style = S_JET_DIAMOND; s.pos = nozzle - forward * (length * u);
                    s.vel = forward * (3 * (0.35f + 0.15f * heat)); s.s0 = s.s1 = 9 * 0.35f * (1 - 0.55f * u);
                    s.heat = heat * (1 - 0.6f * u);
                    sys.spawn(s);
                }
            }
        }
    }

     
    inline Spawn trailPuff(bool missile, vfx::Tier tier)
    {
        Spawn s;
        s.style = S_SMOKE_LIGHT, s.posR = 0.15f, s.vel = {0, 0.3f, 0}, s.velR = 0.4f, s.drag = 1.5f, s.rise = 0.3f, s.windK = 0.6f;
        s.life = missile ? 4.5f : 1.6f, s.lifeJ = 0.25f;
        s.s0 = (missile ? 1.3f : 0.5f) * tierSize(tier), s.s1 = (missile ? 4.0f : 1.8f) * tierSize(tier), s.sizeJ = 0.2f;
        s.tint = grey(0.95f, missile ? 0.85f : 0.55f), s.tintJ = 0.05f, s.spin = 0.3f, s.prio = 0;
        return s;
    }
    inline float trailSpacing(bool missile, vfx::Tier tier) { return (missile ? 1.1f : 2.0f) * tierSize(tier) * tierSize(tier); }

    inline void bombRelease(System& sys, const V3& at, const V3& forward)
    {
        Spawn flash;
        flash.style = S_FLASH; flash.pos = at; flash.life = 0.18f;
        flash.s0 = 2.5f; flash.s1 = 4.5f; flash.heat = 0.8f;
        sys.spawn(flash);
        Spawn gas;
        gas.style = S_SMOKE_LIGHT; gas.pos = at; gas.vel = forward * -8; gas.velR = 1;
        gas.life = 0.6f; gas.s0 = 0.6f; gas.s1 = 2; gas.drag = 1; gas.windK = 0.2f;
        sys.burst(gas, 3);
    }

    inline void bombFlight(System& sys, TrailState& state, const V3& from, const V3& to,
                           const V3& forward, float dt, vfx::Tier tier)
    {
        const float boost = std::clamp(vfx::distanceBoost(len(to - sys.viewer)), 1.0f, 2.5f);
        Spawn motor;
        motor.style = S_BOMB_MOTOR; motor.pos = to - forward * 1.4f;
        motor.vel = forward * 5; motor.flags = P_STATIC;
        motor.life = std::max(dt, 0.001f) * 1.5f; motor.s0 = motor.s1 = 0.8f * boost;
        sys.spawn(motor);
        motor.style = S_GLOW; motor.vel = {}; motor.s0 = motor.s1 = 2.2f * boost; motor.heat = 1.5f;
        sys.spawn(motor);
        Spawn smoke;
        smoke.style = S_SMOKE_LIGHT; smoke.life = 1.8f; smoke.lifeJ = 0.1f;
        smoke.s0 = 0.55f * boost; smoke.s1 = 2.0f * boost; smoke.sizeJ = 0.15f;
        smoke.vel = {0, 0.15f, 0}; smoke.windK = 0.2f; smoke.drag = 0.6f; smoke.prio = 0;
        trail(sys, state, from - forward * 1.6f, to - forward * 1.6f, 3 * tierSize(tier), smoke);
    }

     
     
    inline int wreck(System& sys, const V3& base, float size, float left, float total, vfx::Tier tier, float dt, float (&acc)[3])
    {
        const float ts = tierSize(tier), tc = tierCount(tier);
        sys.addBattleSmoke(base, dt * size * 0.07f);
        const float burn = std::clamp(left / std::max(total, 1.0f), 0.0f, 1.0f);
        const float strength = std::clamp(burn * 1.6f, 0.0f, 1.0f);  
        int n = 0;
        static constexpr float FLAME_RATE[vfx::TIERS] = {22.0f, 9.0f, 3.0f, 1.0f}, SMOKE_RATE[vfx::TIERS] = {6.0f, 2.4f, 0.8f, 0.25f};
        (void)tc;
        acc[0] += dt * FLAME_RATE[tier] * strength * sys.countScale;
        for (; acc[0] >= 1.0f; acc[0] -= 1.0f)
        {
            Spawn f;
            f.style = S_FLAME, f.pos = base + V3{0, 0.4f * size, 0}, f.posR = 0.9f * size, f.flat = true;
            f.vel = {0, 1.6f, 0}, f.velR = 0.4f, f.drag = 1.0f, f.rise = 2.4f * size, f.windK = 0.3f;
            f.life = 0.8f, f.lifeJ = 0.3f, f.s0 = 1.6f * size * ts, f.s1 = 2.6f * size * ts * (0.6f + 0.4f * strength), f.sizeJ = 0.25f;
            f.heat = 0.6f + 0.4f * strength, f.prio = 1;
            n += sys.spawn(f) ? 1 : 0;
        }
         
        acc[1] += dt * SMOKE_RATE[tier] * (0.4f + 0.6f * strength) * sys.countScale * (tier >= vfx::FAR ? sys.pressure() : 1.0f);
        for (; acc[1] >= 1.0f; acc[1] -= 1.0f)
        {
            Spawn s;
            s.style = S_PLUME, s.pos = base + V3{0, 1.4f * size, 0}, s.posR = 0.9f * size;
            s.vel = {0, 2.5f + 1.5f * strength, 0}, s.velR = 0.6f, s.drag = 0.5f, s.rise = 3.0f + 3.0f * strength, s.windK = 0.3f;
            s.life = 16.0f + 10.0f * strength, s.lifeJ = 0.25f, s.s0 = 5.0f * size * ts, s.s1 = 24.0f * size * ts, s.sizeJ = 0.3f;
            s.tint = grey(0.2f + 0.6f * (1.0f - burn) * (1.0f - burn)), s.tintJ = 0.12f, s.spin = 0.2f, s.prio = 0;
            n += sys.spawn(s) ? 1 : 0;
        }
        if (tier <= vfx::MID)
        {
            acc[2] += dt * 6.0f * strength * sys.countScale;
            for (; acc[2] >= 1.0f; acc[2] -= 1.0f)
            {
                Spawn e;
                e.style = S_EMBER, e.pos = base + V3{0, 0.8f * size, 0}, e.posR = 0.8f * size, e.vel = {0, 3.0f, 0}, e.velR = 1.5f;
                e.drag = 0.8f, e.rise = 3.5f, e.windK = 0.8f, e.life = 1.8f, e.lifeJ = 0.4f, e.s0 = e.s1 = 0.12f * size, e.prio = 0;
                n += sys.spawn(e) ? 1 : 0;
            }
        }
        return n;
    }

     
    inline int collapseDust(System& sys, const V3& at, const V3& outward, float size, vfx::Tier tier, const V3& dustColour)
    {
        const float ts = tierSize(tier);
        Spawn s;
        s.style = S_DUST, s.pos = at + V3{0, size * 0.2f, 0}, s.posR = size * 0.6f, s.flat = true;
        s.vel = outward * (size * 0.9f) + V3{0, 0.8f, 0}, s.radial = size * 0.7f, s.radialJ = 0.5f, s.drag = 0.7f, s.rise = 0.9f;
        s.windK = 0.25f, s.life = 9.0f + size * 0.3f, s.lifeJ = 0.35f, s.s0 = size * 0.9f * ts, s.s1 = size * 3.2f * ts, s.sizeJ = 0.3f;
        s.delayJ = 0.6f, s.tint = tintOf(dustColour), s.tintJ = 0.1f, s.flags = P_GROUNDED, s.spin = 0.2f, s.prio = 1;
        return sys.burst(s, sys.scaled(10.0f * std::max(tierCount(tier), 0.15f), 1));
    }

     
    inline int rotorWash(System& sys, const V3& ground, float rotorRadius, float strength, vfx::Tier tier, float dt, float& acc, const V3& dustColour)
    {
        if (strength <= 0.01f)
            return 0;
        const float ts = tierSize(tier);
        acc += dt * (40.0f * tierCount(tier) + 1.0f) * strength * sys.countScale;
        int n = 0;
        for (; acc >= 1.0f; acc -= 1.0f)
        {
            const float a = sys.rng.range(0.0f, 6.2831853f);
            const V3 out{std::cos(a), 0.0f, std::sin(a)};
            Spawn s;
            s.style = S_DUST, s.pos = ground + out * (rotorRadius * sys.rng.range(0.3f, 0.9f)) + V3{0, 0.4f, 0};
            s.vel = out * (10.0f + 14.0f * strength) + V3{0, 0.8f, 0}, s.drag = 1.1f, s.rise = 0.7f, s.windK = 0.3f;
            s.life = 2.2f, s.lifeJ = 0.3f, s.s0 = 1.2f * ts, s.s1 = 4.5f * ts, s.sizeJ = 0.3f;
            s.tint = tintOf(dustColour, 0.75f * strength + 0.15f), s.flags = P_GROUNDED, s.spin = 0.5f, s.prio = 0;
            n += sys.spawn(s) ? 1 : 0;
        }
        return n;
    }

     
    inline int card(System& sys, vfx::Card kind, const V3& pos, float s0, float s1, float life, const V3& vel, const V3& colour, float alpha)
    {
        Spawn s;
        s.pos = pos, s.vel = vel, s.life = life, s.lifeJ = 0.2f, s.s0 = s0 * 0.6f, s.s1 = s1 * 0.6f, s.sizeJ = 0.25f, s.posR = s0 * 0.2f;
        s.tint = tintOf(colour, alpha), s.prio = 0;
        int count = 3;
        switch (kind)
        {
        case vfx::DUST:
        case vfx::WASH:
            s.style = S_DUST, s.flags = P_GROUNDED, s.rise = std::max(0.0f, vel.y), s.windK = 0.4f, s.drag = 1.0f;
            break;
        case vfx::SMOKE:
        case vfx::PUFF:
            s.style = S_SMOKE, s.rise = std::max(0.5f, vel.y), s.windK = 0.5f, s.drag = 0.8f, count = 2;
            break;
        case vfx::FIRE:
        case vfx::FLAME:
            s.style = kind == vfx::FIRE ? S_FIREBALL : S_FLAME, s.rise = 1.5f, s.drag = 2.0f, s.tint = 0xFFFFFFFFu;
            break;
        case vfx::SPARK:
            s.style = S_SPARK, s.flags = P_BALLISTIC | P_DIE_GROUND, s.gravity = 9.81f, s.velR = 8.0f, s.s0 = s.s1 = 0.08f * s0;
            s.tint = 0xFFFFFFFFu, count = 8;
            break;
        case vfx::DEBRIS:
            s.style = S_DEBRIS, s.flags = P_BALLISTIC | P_BOUNCE, s.gravity = 9.81f, s.velR = 4.0f, s.vel = vel + V3{0, 3.0f, 0};
            s.s0 = s.s1 = 0.15f * s0, s.spin = 10.0f, count = 5;
            break;
        default:  
            s.style = kind == vfx::SHOCK ? S_GLOW : S_FLASH, s.tint = 0xFFFFFFFFu, s.posR = 0.0f, count = 1;
            break;
        }
        return sys.burst(s, sys.scaled(static_cast<float>(count), 1));
    }
}  
