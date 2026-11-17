 
 
 
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace warzone::quality
{
    enum class Preset : uint8_t { Low, Medium, High, Ultra, Custom };

    inline const char* presetName(Preset p)
    {
        static constexpr const char* NAMES[] = {"Low", "Medium", "High", "Ultra", "Custom"};
        return NAMES[std::min<int>(static_cast<int>(p), 4)];
    }

    inline bool presetFromName(std::string_view s, Preset& out)
    {
        for (int i = 0; i < 5; ++i)
        {
            const std::string_view n = presetName(static_cast<Preset>(i));
            if (n.size() == s.size() && std::equal(n.begin(), n.end(), s.begin(), [](char a, char b) { return std::tolower(a) == std::tolower(b); }))
            {
                out = static_cast<Preset>(i);
                return true;
            }
        }
        return false;
    }

    struct Settings
    {
        Preset preset = Preset::High;
        bool adaptive = true;           
        float targetFps = 60.0f;        
         
        float viewDistance = 1.0f;      
        int buildingDetail = 2;         
        int unitDetail = 2;             
        int effects = 2;                
         
        int shadows = 2;                
        bool ambientOcclusion = true;   
        bool bloom = true;
        bool antiAliasing = true;       
        float gameLodScale = 2.0f;       
        float engineLodBias = 2.0f;      
        bool engineLod = true;          
    };

    inline Settings presetSettings(Preset p)
    {
        Settings s;
        s.preset = p;
        switch (p)
        {
        case Preset::Low:
            s.gameLodScale = 1.5f, s.engineLodBias = 3.0f;
            s.targetFps = 30.0f, s.viewDistance = 0.6f, s.buildingDetail = 0, s.unitDetail = 0, s.effects = 0, s.shadows = 0;
            s.ambientOcclusion = false, s.bloom = false, s.antiAliasing = false;
            break;
        case Preset::Medium:
            s.gameLodScale = 1.75f, s.engineLodBias = 2.5f;
            s.viewDistance = 0.8f, s.buildingDetail = 1, s.unitDetail = 1, s.effects = 1, s.shadows = 1;
            break;
        case Preset::Ultra:
            s.gameLodScale = 2.1f, s.engineLodBias = 2.0f;
            s.viewDistance = 1.15f, s.buildingDetail = 3, s.unitDetail = 3, s.effects = 3, s.shadows = 3;
            break;
        default:  
            break;
        }
        return s;
    }

    inline Settings sanitize(Settings s)
    {
        s.targetFps = std::clamp(std::isfinite(s.targetFps) ? s.targetFps : 60.0f, 20.0f, 240.0f);
        s.viewDistance = std::clamp(std::isfinite(s.viewDistance) ? s.viewDistance : 1.0f, 0.5f, 1.5f);
        s.gameLodScale = std::clamp(std::isfinite(s.gameLodScale) ? s.gameLodScale : 1.0f, 1.0f, 16.0f);
        s.engineLodBias = std::clamp(std::isfinite(s.engineLodBias) ? s.engineLodBias : 0.0f, -2.0f, 4.0f);
        s.buildingDetail = std::clamp(s.buildingDetail, 0, 3);
        s.unitDetail = std::clamp(s.unitDetail, 0, 3);
        s.effects = std::clamp(s.effects, 0, 3);
        s.shadows = std::clamp(s.shadows, 0, 3);
        s.preset = std::min(s.preset, Preset::Custom);
        return s;
    }

     
    inline Preset classify(const Settings& s)
    {
        for (int i = 0; i < 4; ++i)
        {
            const Settings p = presetSettings(static_cast<Preset>(i));
            if (std::abs(p.viewDistance - s.viewDistance) < 1e-3f && p.buildingDetail == s.buildingDetail && p.unitDetail == s.unitDetail &&
                p.effects == s.effects && p.shadows == s.shadows && p.ambientOcclusion == s.ambientOcclusion && p.bloom == s.bloom &&
                p.antiAliasing == s.antiAliasing && p.engineLod == s.engineLod &&
                std::abs(p.gameLodScale - s.gameLodScale) < 1e-3f && std::abs(p.engineLodBias - s.engineLodBias) < 1e-3f)
                return static_cast<Preset>(i);
        }
        return Preset::Custom;
    }

     
     
     
    struct Knobs
    {
        float radius = 1;         
        float pieceBudget = 1;    
        float detailRange = 1;    
        float detailBudget = 1;   
        float lowRange = 1;       
        float streamRate = 1;     
        float aiCount = 1;        
        float aiRange = 1;        
        float farScale = 1;       
        float effectReach = 1;    
        float cardBudget = 1;     
        uint32_t lights = 8;      
        int particleQuality = 2;  
        struct Gfx
        {
            uint32_t shadowResolution = 2048;
            float shadowDistance = 900.0f;
            uint32_t pcfRadius = 2;
            bool ao = true, bloom = true, taa = true, lod = false;
            float lodBias = 0.0f;
        } gfx;
    };

    inline Knobs knobsFor(const Settings& in)
    {
        const Settings s = sanitize(in);
        static constexpr float PIECES[4] = {0.46f, 0.73f, 1.0f, 1.1f};       
        static constexpr float DETAIL_RANGE[4] = {0.57f, 0.79f, 1.0f, 1.1f};
        static constexpr float DETAIL_BUDGET[4] = {0.24f, 0.56f, 1.0f, 1.0f};
        static constexpr float LOW_RANGE[4] = {0.6f, 0.8f, 1.0f, 1.1f};
        static constexpr float STREAM_RATE[4] = {0.67f, 0.85f, 1.0f, 1.0f};  
        static constexpr float AI_COUNT[4] = {0.2f, 0.5f, 1.0f, 1.2f};
        static constexpr float AI_RANGE[4] = {0.55f, 0.75f, 1.0f, 1.3f};
        static constexpr float EFFECT_REACH[4] = {0.55f, 0.8f, 1.0f, 1.35f};
        static constexpr float CARDS[4] = {0.45f, 0.7f, 1.0f, 1.6f};
        static constexpr uint32_t LIGHTS[4] = {2, 5, 8, 8};
        static constexpr uint32_t SHADOW_RES[4] = {1024, 2048, 2048, 2560};
        static constexpr float SHADOW_DIST[4] = {450.0f, 650.0f, 900.0f, 1000.0f};
        static constexpr uint32_t PCF[4] = {1, 1, 2, 2};
        Knobs k;
        k.radius = k.farScale = s.viewDistance;
        const float extended = std::max(1.0f, s.gameLodScale / 4.0f);
        k.radius *= extended;
        k.pieceBudget = PIECES[s.buildingDetail] * extended * extended;
        k.detailRange = DETAIL_RANGE[s.buildingDetail] * s.gameLodScale;
        k.detailBudget = std::min(k.pieceBudget * 10.4f, DETAIL_BUDGET[s.buildingDetail] * s.gameLodScale * s.gameLodScale);
        k.lowRange = LOW_RANGE[s.buildingDetail] * s.gameLodScale;
        k.streamRate = STREAM_RATE[s.buildingDetail];
        k.aiCount = AI_COUNT[s.unitDetail];
        k.aiRange = AI_RANGE[s.unitDetail];
        k.effectReach = EFFECT_REACH[s.effects];
        k.cardBudget = CARDS[s.effects];
        k.lights = LIGHTS[s.effects];
        k.particleQuality = s.effects;
        k.gfx = {SHADOW_RES[s.shadows], SHADOW_DIST[s.shadows], PCF[s.shadows], s.ambientOcclusion, s.bloom, s.antiAliasing, s.engineLod, s.engineLodBias};
        return k;
    }

     
    struct HardwareInfo
    {
        std::string gpuName;            
        uint64_t vramBytes = 0;         
        uint64_t vramUsedBytes = 0;
        Preset suggested = Preset::High;
         
        uint64_t vramBudgetBytes = 0;   
        uint32_t cpuCores = 0;          
        uint64_t ramMb = 0;
        std::string cpuName;
        float probeMs = 0;              
    };

    struct HardwareFacts
    {
        uint64_t vramMb = 0;   
        uint32_t cores = 0;    
        float probeMs = 0;     
    };

     
    inline Preset suggestPreset(const HardwareFacts& h)
    {
        int level = h.vramMb == 0 ? 1 : h.vramMb < 5000 ? 0 : h.vramMb < 6000 ? 1 : h.vramMb < 11000 ? 2 : 3;
        if (level == 3 && !(h.probeMs > 0.0f && h.probeMs < 8.0f && h.cores >= 8))
            level = 2;
        if (h.cores > 0 && h.cores < 4)
            level = 0;
        else if (h.cores > 0 && h.cores < 6)
            level = std::min(level, 1);
        if (h.probeMs > 33.0f)
            level = std::max(0, level - 1);
        return static_cast<Preset>(level);
    }

     
    class FrameProbe
    {
    public:
        explicit FrameProbe(float seconds = 3.0f) : m_seconds(seconds) {}
        void add(float dtSec)
        {
            if (done())
                return;
            m_t += dtSec;
            m_ms.push_back(dtSec * 1000.0f);
        }
        bool done() const { return m_t >= m_seconds && m_ms.size() >= 30; }
        float medianMs() const
        {
            if (m_ms.empty())
                return 0.0f;
            std::vector<float> v = m_ms;
            std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
            return v[v.size() / 2];
        }

    private:
        float m_seconds, m_t = 0;
        std::vector<float> m_ms;
    };

     
     
     
     
    enum class Step : uint8_t { Effects, Units, View, Buildings };
    inline constexpr Step STEP_ORDER[] = {Step::Effects, Step::Units,     Step::View, Step::Buildings, Step::Effects, Step::View,   Step::Units,
                                          Step::Buildings, Step::View,    Step::Effects, Step::View,   Step::Buildings, Step::View,
                                          Step::Units};

     
    inline bool applyStep(Settings& s, Step k, float ceilingView)
    {
        switch (k)
        {
        case Step::Effects:
            return s.effects > 0 && (--s.effects, true);
        case Step::Units:
            return s.unitDetail > 0 && (--s.unitDetail, true);
        case Step::Buildings:
            return s.buildingDetail > 0 && (--s.buildingDetail, true);
        case Step::View:
            if (s.viewDistance <= 0.5f + 1e-4f)
                return false;
            s.viewDistance = std::max(0.5f, s.viewDistance - std::max(0.05f, (ceilingView - 0.5f) / 5.0f));
            if (s.viewDistance < 0.5f + 1e-3f)
                s.viewDistance = 0.5f;
            return true;
        }
        return false;
    }

    inline int stepsAvailable(const Settings& ceiling)
    {
        Settings s = ceiling;
        int n = 0;
        for (Step k : STEP_ORDER)
            n += applyStep(s, k, ceiling.viewDistance) ? 1 : 0;
        return n;
    }

     
    inline Step stepKind(const Settings& ceiling, int drop)
    {
        Settings s = ceiling;
        for (Step k : STEP_ORDER)
            if (applyStep(s, k, ceiling.viewDistance) && --drop <= 0)
                return k;
        return Step::Effects;
    }

     
     
    inline Settings degrade(const Settings& ceiling, int drop, int memDrop = 0)
    {
        Settings s = ceiling;
        const float view = ceiling.viewDistance;
        for (int i = 0; i < memDrop; ++i)
            if (!applyStep(s, i % 2 == 0 ? Step::View : Step::Buildings, view))
                applyStep(s, i % 2 == 0 ? Step::Buildings : Step::View, view);
        for (Step k : STEP_ORDER)
        {
            if (drop <= 0)
                break;
            if (applyStep(s, k, view))
                --drop;
        }
        return s;
    }

    struct GovernorParams
    {
        float windowSec = 2.0f;       
        float settleSec = 1.5f;       
        float pauseSec = 1.0f;        
        float downFactor = 1.08f;     
        float severeFactor = 1.6f;    
        float upFactor = 0.72f;       
        float headroom = 0.92f;       
        int upWindows = 3;            
        int upWindowsMax = 24;        
        float brakeHold = 0.85f;      
        float brakeCut = 0.93f;       
        float brakeRelease = 0.75f;   
        int maxMemDrop = 4;
    };

    struct Decision
    {
        float t = 0;           
        int drop = 0, memDrop = 0;
        float p90Ms = 0, vram = 0;
        const char* why = "";
    };

    class Governor
    {
    public:
        GovernorParams p;

         
        void start(const Settings& ceiling, float warmupSec)
        {
            m_ceiling = sanitize(ceiling);
            m_maxDrop = stepsAvailable(m_ceiling);
            m_drop = m_memDrop = 0;
            m_calm = m_stable = 0;
            m_upNeed = p.upWindows;
            m_sinceClimb = 1000;
            m_skip = warmupSec;
            m_pending = {};
            m_gain.fill(-1.0f);
            restartWindow();
        }
        void ignoreFor(float sec) { m_skip = std::max(m_skip, sec); }
        void forceDrop(int drop) { m_drop = std::clamp(drop, 0, m_maxDrop); }  

         
        bool frame(float dtSec, float vramFrac)
        {
            m_clock += dtSec;
            if (dtSec > p.pauseSec)
            {
                m_skip = std::max(m_skip, p.settleSec);
                restartWindow();
                return false;
            }
            if (m_skip > 0.0f)
            {
                m_skip -= dtSec;
                restartWindow();
                return false;
            }
            m_ms.push_back(dtSec * 1000.0f);
            m_windowT += dtSec;
            if (m_windowT < p.windowSec || (m_ms.size() < 8 && m_windowT < 3.0f * p.windowSec))
                return false;
            m_p90 = percentile(0.9f);
            m_fps = 1000.0f / std::max(percentile(0.5f), 0.01f);
            restartWindow();
            ++m_sinceClimb;
            return m_ceiling.adaptive && judge(vramFrac);
        }

        Settings effective() const { return degrade(m_ceiling, m_drop, m_memDrop); }
        const Settings& ceiling() const { return m_ceiling; }
        int drop() const { return m_drop; }
        int memDrop() const { return m_memDrop; }
        int maxDrop() const { return m_maxDrop; }
        int upNeed() const { return m_upNeed; }
        int calm() const { return m_calm; }
        float p90Ms() const { return m_p90; }
        float fps() const { return m_fps; }
        float clock() const { return m_clock; }
        float skipLeft() const { return std::max(0.0f, m_skip); }
        const std::vector<Decision>& decisions() const { return m_decisions; }

    private:
        struct Pending
        {
            bool valid = false;
            int from = 0, to = 0;
            float before = 0;
        };

        Settings m_ceiling;
        int m_maxDrop = 0, m_drop = 0, m_memDrop = 0, m_calm = 0, m_stable = 0, m_upNeed = 3, m_sinceClimb = 1000;
        float m_clock = 0, m_skip = 0, m_windowT = 0, m_p90 = 0, m_fps = 0;
        bool m_holdLogged = false;
        std::vector<float> m_ms;
        Pending m_pending;
        std::array<float, 16> m_gain{};  
        std::vector<Decision> m_decisions;

        void restartWindow()
        {
            m_ms.clear();
            m_windowT = 0;
        }
        float percentile(float q) const
        {
            if (m_ms.empty())
                return 0.0f;
            std::vector<float> v = m_ms;
            const size_t i = std::min(v.size() - 1, static_cast<size_t>(std::max(0.0f, std::ceil(q * static_cast<float>(v.size())) - 1.0f)));
            std::nth_element(v.begin(), v.begin() + i, v.end());
            return v[i];
        }
         
        void note(const char* why, float vram, bool changed = true)
        {
            if (m_decisions.size() >= 32)
                m_decisions.erase(m_decisions.begin());
            m_decisions.push_back({m_clock, m_drop, m_memDrop, m_p90, vram, why});
            if (changed)
            {
                m_skip = p.settleSec;
                m_calm = m_stable = 0;
                m_holdLogged = false;
            }
        }

        bool judge(float vram)
        {
            const float target = 1000.0f / m_ceiling.targetFps;
            if (m_pending.valid)  
            {
                const float each = std::max(0.0f, m_pending.before - m_p90) / static_cast<float>(m_pending.to - m_pending.from);
                for (int i = m_pending.from + 1; i <= m_pending.to; ++i)
                    m_gain[std::min(i, 15)] = each;
                m_pending.valid = false;
            }
            if (vram >= p.brakeCut && m_memDrop < p.maxMemDrop)
            {
                ++m_memDrop;
                note("video memory brake: view and buildings down", vram);
                return true;
            }
            if (m_p90 > target * p.downFactor && m_drop < m_maxDrop)
            {
                const int steps = std::min(m_maxDrop - m_drop, m_p90 > target * p.severeFactor ? 2 : 1);
                const bool failedClimb = m_sinceClimb <= 2;
                if (failedClimb)
                    m_upNeed = std::min(p.upWindowsMax, m_upNeed * 2);
                m_pending = {true, m_drop, m_drop + steps, m_p90};
                m_drop += steps;
                note(failedClimb ? "step down: the climb did not hold" : steps > 1 ? "step down x2: far over target" : "step down: over target", vram);
                return true;
            }
            if (m_p90 < target * p.upFactor && (m_drop > 0 || m_memDrop > 0))
            {
                if (++m_calm >= m_upNeed)
                {
                    if (m_drop == 0)
                    {
                        if (vram < p.brakeRelease)
                        {
                            --m_memDrop;
                            note("video memory back: view and buildings up", vram);
                            return true;
                        }
                        return false;
                    }
                    const Step kind = stepKind(m_ceiling, m_drop);
                    const bool growsMemory = kind == Step::View || kind == Step::Buildings;
                    if (growsMemory && vram >= p.brakeHold)
                    {
                        if (!std::exchange(m_holdLogged, true))
                            note("climb held: video memory is near the budget", vram, false);
                        return false;
                    }
                    const float cost = m_gain[std::min(m_drop, 15)] >= 0.0f ? m_gain[std::min(m_drop, 15)] : 0.12f * m_p90;
                    if (m_p90 + cost <= target * p.headroom)
                    {
                        --m_drop;
                        m_sinceClimb = 0;
                        note("step up: calm with headroom", vram);
                        return true;
                    }
                }
                return false;
            }
            m_calm = 0;
            if (++m_stable >= 8 && m_upNeed > p.upWindows)  
            {
                m_upNeed = std::max(p.upWindows, m_upNeed / 2);
                m_stable = 0;
            }
            return false;
        }
    };

     
    inline std::string statusLine(const Settings& chosen, const Settings& running, float fps)
    {
        return std::format("{}: view {:.2f}, buildings {}, fps {:.0f} (target {:.0f})", chosen.adaptive ? "adaptive" : presetName(chosen.preset),
                           running.viewDistance, running.buildingDetail, fps, chosen.targetFps);
    }

     
     
     
    inline std::string toJson(const Settings& in)
    {
        const Settings s = sanitize(in);
        std::string j = std::format("{{\n    \"preset\": \"{}\",\n    \"adaptive\": {},\n    \"targetFps\": {}", presetName(s.preset), s.adaptive, s.targetFps);
        if (s.preset == Preset::Custom)
            j += std::format(",\n    \"viewDistance\": {},\n    \"buildingDetail\": {},\n    \"unitDetail\": {},\n    \"effects\": {},\n    \"shadows\": {},\n"
                             "    \"ambientOcclusion\": {},\n    \"bloom\": {},\n    \"antiAliasing\": {},\n    \"engineLod\": {}",
                             s.viewDistance, s.buildingDetail, s.unitDetail, s.effects, s.shadows, s.ambientOcclusion, s.bloom, s.antiAliasing, s.engineLod);
        j += std::format(",\n    \"gameLodScale\": {},\n    \"engineLodBias\": {}", s.gameLodScale, s.engineLodBias);
        return j + "\n}\n";
    }

    inline std::optional<std::string_view> jsonValue(std::string_view text, std::string_view key)
    {
        const std::string needle = "\"" + std::string(key) + "\"";
        size_t at = text.find(needle);
        if (at == std::string_view::npos)
            return std::nullopt;
        at = text.find(':', at + needle.size());
        if (at == std::string_view::npos)
            return std::nullopt;
        ++at;
        while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])))
            ++at;
        size_t end = at;
        if (end < text.size() && text[end] == '"')
            end = text.find('"', end + 1) == std::string_view::npos ? text.size() : text.find('"', end + 1) + 1;
        else
            while (end < text.size() && text[end] != ',' && text[end] != '}' && text[end] != '\n')
                ++end;
        std::string_view v = text.substr(at, end - at);
        while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back())))
            v.remove_suffix(1);
        if (v.size() >= 2 && v.front() == '"')
            v = v.substr(1, v.size() - 2);
        return v;
    }

     
    inline bool fromJson(std::string_view text, Settings& out)
    {
        Preset preset;
        const auto name = jsonValue(text, "preset");
        if (!name || !presetFromName(*name, preset))
            return false;
        Settings s = presetSettings(preset);
        auto number = [&](std::string_view key, auto& dst)
        {
            if (const auto v = jsonValue(text, key); v && !v->empty())
            {
                const std::string t(*v);
                char* end = nullptr;
                const double d = std::strtod(t.c_str(), &end);
                if (end != t.c_str())
                    dst = static_cast<std::remove_reference_t<decltype(dst)>>(d);
            }
        };
        auto flag = [&](std::string_view key, bool& dst)
        {
            if (const auto v = jsonValue(text, key); v && (*v == "true" || *v == "false"))
                dst = *v == "true";
        };
        flag("adaptive", s.adaptive);
        number("targetFps", s.targetFps);
        if (preset == Preset::Custom)
        {
            number("viewDistance", s.viewDistance);
            number("buildingDetail", s.buildingDetail);
            number("unitDetail", s.unitDetail);
            number("effects", s.effects);
            number("shadows", s.shadows);
            flag("ambientOcclusion", s.ambientOcclusion);
            flag("bloom", s.bloom);
            flag("antiAliasing", s.antiAliasing);
            flag("engineLod", s.engineLod);
        }
        number("gameLodScale", s.gameLodScale);
        number("engineLodBias", s.engineLodBias);
        out = sanitize(s);
        return true;
    }
}  
