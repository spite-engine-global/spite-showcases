 
 
 
 
#pragma once
#include "AudioMix.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace warzone::menu
{
    enum class Kind : uint8_t
    {
        Choice,  
        Slider,
        Toggle,  
        Action,
        Info
    };

    struct Item
    {
        std::string id, label;
        Kind kind = Kind::Info;
        float value = 0.0f, min = 0.0f, max = 1.0f, step = 1.0f;
        std::vector<std::string> choices;
        std::string unit;  
        int decimals = 0;
        std::string text;  
        bool custom = false;  
        bool preset = false;  
        bool selectable() const { return kind != Kind::Info; }
        int index() const { return static_cast<int>(std::lround(value)); }
    };

    struct Page
    {
        std::string id, title;
        std::vector<Item> items;
        int selected = 0;
    };

     
    struct Event
    {
        std::string id;
        bool action = false;
    };

    inline float snap(const Item& it, float v)
    {
        if (it.kind == Kind::Toggle)
            return v >= 0.5f ? 1.0f : 0.0f;
        if (it.kind == Kind::Choice)
            return std::clamp(std::round(v), 0.0f, static_cast<float>(std::max<size_t>(it.choices.size(), 1) - 1));
        v = std::clamp(v, it.min, it.max);
        if (it.step > 0.0f)
            v = std::clamp(it.min + std::round((v - it.min) / it.step) * it.step, it.min, it.max);
        return v;
    }

    inline std::string display(const Item& it)
    {
        char b[64];
        switch (it.kind)
        {
        case Kind::Slider:
            std::snprintf(b, sizeof b, "%.*f%s", it.decimals, it.value, it.unit.c_str());
            return b;
        case Kind::Choice:
            return it.choices.empty() ? std::string() : it.choices[static_cast<size_t>(std::clamp(it.index(), 0, static_cast<int>(it.choices.size()) - 1))];
        case Kind::Toggle:
            return it.value >= 0.5f ? "On" : "Off";
        case Kind::Info:
            return it.text;
        default:
            return {};
        }
    }

    struct Menu
    {
        bool open = false;
        int tab = 0;
        std::vector<Page> pages;
        std::vector<Event> events;  

        Page& page() { return pages[static_cast<size_t>(tab)]; }
        const Page& page() const { return pages[static_cast<size_t>(tab)]; }
        Item* selectedItem() { return page().items.empty() ? nullptr : &page().items[static_cast<size_t>(page().selected)]; }

        Item* find(std::string_view id, int* pageIndex = nullptr)
        {
            for (size_t p = 0; p < pages.size(); ++p)
                for (auto& it : pages[p].items)
                    if (it.id == id)
                    {
                        if (pageIndex)
                            *pageIndex = static_cast<int>(p);
                        return &it;
                    }
            return nullptr;
        }
        const Item* find(std::string_view id) const { return const_cast<Menu*>(this)->find(id); }

        void setTab(int t)
        {
            const int n = static_cast<int>(pages.size());
            tab = n ? ((t % n) + n) % n : 0;
        }
        void tabStep(int d) { setTab(tab + d); }

         
        void move(int d)
        {
            auto& p = page();
            const int n = static_cast<int>(p.items.size());
            for (int k = 0; k < n; ++k)
            {
                p.selected = ((p.selected + d) % n + n) % n;
                if (p.items[static_cast<size_t>(p.selected)].selectable())
                    return;
            }
        }

        bool select(std::string_view id)
        {
            int pi = 0;
            if (!find(id, &pi))
                return false;
            tab = pi;
            auto& p = page();
            for (size_t i = 0; i < p.items.size(); ++i)
                if (p.items[i].id == id)
                    p.selected = static_cast<int>(i);
            return true;
        }

         
         
        bool set(std::string_view id, float v, bool user = true)
        {
            int pi = 0;
            Item* it = find(id, &pi);
            if (!it || it->kind == Kind::Info || it->kind == Kind::Action)
                return false;
            v = snap(*it, v);
            if (v == it->value)
                return false;
            it->value = v;
            if (!user)
                return true;
            events.push_back({it->id, false});
            if (it->custom)
                for (auto& other : pages[static_cast<size_t>(pi)].items)
                    if (other.preset && other.value != static_cast<float>(other.choices.size() - 1))
                        other.value = static_cast<float>(other.choices.size() - 1);
            return true;
        }

         
        bool adjust(int dir, int mult = 1)
        {
            Item* it = selectedItem();
            if (!it)
                return false;
            switch (it->kind)
            {
            case Kind::Slider:
                return set(it->id, it->value + static_cast<float>(dir * mult) * it->step);
            case Kind::Choice:
            {
                const int n = static_cast<int>(it->choices.size());
                return n > 0 && set(it->id, static_cast<float>(((it->index() + dir) % n + n) % n));
            }
            case Kind::Toggle:
                return set(it->id, dir > 0 ? 1.0f : 0.0f);
            default:
                return false;
            }
        }

         
        bool activate()
        {
            Item* it = selectedItem();
            if (!it)
                return false;
            if (it->kind == Kind::Action)
            {
                events.push_back({it->id, true});
                return true;
            }
            if (it->kind == Kind::Toggle)
                return set(it->id, it->value >= 0.5f ? 0.0f : 1.0f);
            return it->kind == Kind::Choice && adjust(1);
        }

        void setText(std::string_view id, std::string text)
        {
            if (Item* it = find(id))
                it->text = std::move(text);
        }

        std::vector<Event> drain()
        {
            std::vector<Event> e;
            e.swap(events);
            return e;
        }
    };

     
    inline Item makeSlider(const char* id, const char* label, float v, float lo, float hi, float step, const char* unit = "", int decimals = 0, bool custom = false)
    {
        Item it;
        it.id = id, it.label = label, it.kind = Kind::Slider, it.value = v, it.min = lo, it.max = hi, it.step = step, it.unit = unit, it.decimals = decimals;
        it.custom = custom;
        return it;
    }
    inline Item makeChoice(const char* id, const char* label, int v, std::vector<std::string> choices, bool custom = false, bool preset = false)
    {
        Item it;
        it.id = id, it.label = label, it.kind = Kind::Choice, it.value = static_cast<float>(v), it.choices = std::move(choices), it.custom = custom, it.preset = preset;
        return it;
    }
    inline Item makeToggle(const char* id, const char* label, bool on, bool custom = false)
    {
        Item it;
        it.id = id, it.label = label, it.kind = Kind::Toggle, it.value = on ? 1.0f : 0.0f, it.custom = custom;
        return it;
    }
    inline Item makeAction(const char* id, const char* label)
    {
        Item it;
        it.id = id, it.label = label, it.kind = Kind::Action;
        return it;
    }
    inline Item makeInfo(const char* id, const char* label)
    {
        Item it;
        it.id = id, it.label = label, it.kind = Kind::Info;
        return it;
    }

    constexpr std::array<int, 5> TARGET_FPS = {30, 60, 90, 120, 144};
    inline int fpsIndex(float fps)
    {
        int best = 0;
        for (int i = 0; i < static_cast<int>(TARGET_FPS.size()); ++i)
            if (std::abs(TARGET_FPS[static_cast<size_t>(i)] - fps) < std::abs(TARGET_FPS[static_cast<size_t>(best)] - fps))
                best = i;
        return best;
    }

     
    inline Page graphicsPage()
    {
        const std::vector<std::string> level = {"Low", "Medium", "High", "Ultra"};
        Page p{"graphics", "GRAPHICS", {}, 0};
        p.items.push_back(makeChoice("preset", "Preset", 2, {"Low", "Medium", "High", "Ultra", "Custom"}, false, true));
        p.items.push_back(makeToggle("adaptive", "Adaptive quality", true, true));
        p.items.push_back(makeChoice("targetFps", "Target frame rate", 1, {"30", "60", "90", "120", "144"}, true));
        p.items.push_back(makeSlider("viewDistance", "View distance", 1.0f, 0.5f, 1.5f, 0.05f, " x", 2, true));
        p.items.push_back(makeChoice("buildingDetail", "Building detail", 2, level, true));
        p.items.push_back(makeChoice("unitDetail", "Unit detail", 2, level, true));
        p.items.push_back(makeChoice("effects", "Effects", 2, level, true));
        p.items.push_back(makeChoice("shadows", "Shadows", 2, level, true));
        p.items.push_back(makeToggle("ambientOcclusion", "Ambient occlusion", true, true));
        p.items.push_back(makeToggle("bloom", "Bloom", true, true));
        p.items.push_back(makeToggle("antiAliasing", "Anti-aliasing (TAA)", true, true));
        p.items.push_back(makeSlider("gameLodScale", "Game LOD distances", 2.0f, 1.0f, 16.0f, 0.25f, " x", 2, true));
        p.items.push_back(makeSlider("engineLodBias", "Engine LOD bias (+ = coarser)", 2.0f, -2.0f, 4.0f, 0.25f, " stops", 2, true));
        p.items.push_back(makeToggle("engineLod", "Engine LOD", true, true));
        p.items.push_back(makeInfo("hardware", "Hardware"));
        p.items.push_back(makeInfo("status", "Governor"));
        p.items.push_back(makeAction("apply", "Apply and save"));
        p.items.push_back(makeAction("revert", "Revert to the applied settings"));
        p.selected = 0;
        return p;
    }

     
    template <class S>
    void settingsToItems(Menu& m, const S& s)
    {
        m.set("preset", static_cast<float>(static_cast<int>(s.preset)), false);
        m.set("adaptive", s.adaptive ? 1.0f : 0.0f, false);
        m.set("targetFps", static_cast<float>(fpsIndex(s.targetFps)), false);
        m.set("viewDistance", s.viewDistance, false);
        m.set("buildingDetail", static_cast<float>(s.buildingDetail), false);
        m.set("unitDetail", static_cast<float>(s.unitDetail), false);
        m.set("effects", static_cast<float>(s.effects), false);
        m.set("shadows", static_cast<float>(s.shadows), false);
        m.set("ambientOcclusion", s.ambientOcclusion ? 1.0f : 0.0f, false);
        m.set("bloom", s.bloom ? 1.0f : 0.0f, false);
        m.set("antiAliasing", s.antiAliasing ? 1.0f : 0.0f, false);
        m.set("engineLod", s.engineLod ? 1.0f : 0.0f, false);
        m.set("gameLodScale", s.gameLodScale, false);
        m.set("engineLodBias", s.engineLodBias, false);
    }

    template <class S>
    S itemsToSettings(const Menu& m, S s)
    {
        auto v = [&](const char* id) { return m.find(id)->value; };
        s.preset = static_cast<decltype(s.preset)>(static_cast<int>(v("preset")));
        s.adaptive = v("adaptive") >= 0.5f;
        s.targetFps = static_cast<float>(TARGET_FPS[static_cast<size_t>(std::clamp(static_cast<int>(v("targetFps")), 0, 4))]);
        s.viewDistance = v("viewDistance");
        s.buildingDetail = static_cast<int>(v("buildingDetail"));
        s.unitDetail = static_cast<int>(v("unitDetail"));
        s.effects = static_cast<int>(v("effects"));
        s.shadows = static_cast<int>(v("shadows"));
        s.ambientOcclusion = v("ambientOcclusion") >= 0.5f;
        s.bloom = v("bloom") >= 0.5f;
        s.antiAliasing = v("antiAliasing") >= 0.5f;
        s.engineLod = v("engineLod") >= 0.5f;
        s.gameLodScale = v("gameLodScale");
        s.engineLodBias = v("engineLodBias");
        return s;
    }

     
    constexpr std::array<std::array<float, 3>, 5> HAZE_COLORS = {{{0.0f, 0.0f, 0.0f}, {0.66f, 0.73f, 0.82f}, {0.86f, 0.62f, 0.48f}, {0.62f, 0.64f, 0.67f}, {0.30f, 0.27f, 0.24f}}};
    constexpr std::array<std::array<float, 3>, 5> GRADE_TINTS = {{{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.93f, 0.84f}, {0.88f, 0.95f, 1.0f}, {0.93f, 0.88f, 0.82f}}};
     

    struct SceneSettings
    {
        float timeOfDay = 9.0f;
        bool animate = false;
        float dayMinutes = 10.0f;  
        int look = 0;              
        float hazePct = 12.0f;     
        int hazeColor = 0;         
        int grade = 0;             
        float vignette = 0.22f;
        float sunIntensity = 5.0f;
        float exposure = 0.0f;  
        float windDir = 36.9f;  
        float windSpeed = 3.5f;
        float gust = 0.45f;

        float hoursPerSecond() const { return 24.0f / (std::max(dayMinutes, 0.1f) * 60.0f); }
        float hazeDensity() const { return hazePct * 1e-5f; }
        float hazeMax() const { return std::clamp(0.6f + 1.25f * hazePct / 100.0f, 0.6f, 0.92f); }
        float sunGlow() const { return 0.55f * std::clamp(sunIntensity / 5.0f, 0.2f, 1.2f); }
    };

    struct LookPreset
    {
        const char* name;
        float hazePct;
        int hazeColor, grade;
        float vignette, sunIntensity, exposure;
    };
    constexpr std::array<LookPreset, 5> LOOKS = {{
        {"Clear", 12.0f, 0, 0, 0.22f, 5.0f, 0.0f},
        {"Hazy", 32.0f, 0, 0, 0.25f, 4.25f, 0.2f},
        {"Overcast", 22.0f, 3, 3, 0.30f, 2.0f, 0.4f},
        {"Dusk", 20.0f, 2, 2, 0.32f, 3.5f, 0.3f},
        {"Smoky battlefield", 65.0f, 4, 4, 0.38f, 3.0f, 0.5f},
    }};
    constexpr int LOOK_CUSTOM = static_cast<int>(LOOKS.size());

    inline void applyLookPreset(SceneSettings& s, int i)
    {
        s.look = std::clamp(i, 0, LOOK_CUSTOM);
        if (s.look == LOOK_CUSTOM)
            return;
        const LookPreset& p = LOOKS[static_cast<size_t>(s.look)];
        s.hazePct = p.hazePct, s.hazeColor = p.hazeColor, s.grade = p.grade, s.vignette = p.vignette, s.sunIntensity = p.sunIntensity, s.exposure = p.exposure;
    }

    inline Page scenePage()
    {
        Page p{"scene", "SCENE", {}, 0};
        const SceneSettings d;
        p.items.push_back(makeSlider("timeOfDay", "Time of day", d.timeOfDay, 0.0f, 24.0f, 0.25f, " h", 2));
        p.items.push_back(makeToggle("animate", "Animate time", d.animate));
        p.items.push_back(makeSlider("dayMinutes", "Day length", d.dayMinutes, 1.0f, 120.0f, 1.0f, " min"));
        p.items.push_back(makeChoice("look", "Weather / look", 0, {"Clear", "Hazy", "Overcast", "Dusk", "Smoky battlefield", "Custom"}, false, true));
        p.items.push_back(makeSlider("hazePct", "Haze density", d.hazePct, 0.0f, 100.0f, 1.0f, " %", 0, true));
        p.items.push_back(makeChoice("hazeColor", "Haze colour", 0, {"Auto (sun)", "Blue-grey", "Warm", "Grey", "Smoke"}, true));
        p.items.push_back(makeChoice("grade", "Colour grade", 0, {"Auto (sun)", "Neutral", "Warm", "Cool", "Smoke"}, true));
        p.items.push_back(makeSlider("vignette", "Vignette", d.vignette, 0.0f, 0.6f, 0.02f, "", 2, true));
        p.items.push_back(makeSlider("sunIntensity", "Sun intensity", d.sunIntensity, 0.0f, 10.0f, 0.25f, "", 2, true));
        p.items.push_back(makeSlider("exposure", "Exposure compensation", d.exposure, -2.0f, 2.0f, 0.1f, " EV", 1, true));
        p.items.push_back(makeSlider("windDir", "Wind direction", d.windDir, 0.0f, 345.0f, 15.0f, " deg"));
        p.items.push_back(makeSlider("windSpeed", "Wind speed", d.windSpeed, 0.0f, 25.0f, 0.5f, " m/s", 1));
        p.items.push_back(makeSlider("gust", "Gusts", d.gust, 0.0f, 1.0f, 0.05f, "", 2));
        return p;
    }

    inline void sceneToItems(Menu& m, const SceneSettings& s)
    {
        m.set("timeOfDay", s.timeOfDay, false);
        m.set("animate", s.animate ? 1.0f : 0.0f, false);
        m.set("dayMinutes", s.dayMinutes, false);
        m.set("look", static_cast<float>(s.look), false);
        m.set("hazePct", s.hazePct, false);
        m.set("hazeColor", static_cast<float>(s.hazeColor), false);
        m.set("grade", static_cast<float>(s.grade), false);
        m.set("vignette", s.vignette, false);
        m.set("sunIntensity", s.sunIntensity, false);
        m.set("exposure", s.exposure, false);
        m.set("windDir", s.windDir, false);
        m.set("windSpeed", s.windSpeed, false);
        m.set("gust", s.gust, false);
    }

    inline SceneSettings itemsToScene(const Menu& m)
    {
        auto v = [&](const char* id) { return m.find(id)->value; };
        SceneSettings s;
        s.timeOfDay = v("timeOfDay"), s.animate = v("animate") >= 0.5f, s.dayMinutes = v("dayMinutes"), s.look = static_cast<int>(v("look"));
        s.hazePct = v("hazePct"), s.hazeColor = static_cast<int>(v("hazeColor")), s.grade = static_cast<int>(v("grade")), s.vignette = v("vignette");
        s.sunIntensity = v("sunIntensity"), s.exposure = v("exposure"), s.windDir = v("windDir"), s.windSpeed = v("windSpeed"), s.gust = v("gust");
        return s;
    }

     
    struct BattleSettings
    {
        float giga = 0;
        float size = 200.0f;  
        std::array<float, 4> mix = {40.0f, 30.0f, 18.0f, 12.0f};  
        float respawn = 12.0f;                                     
        float rate = 20.0f;                                        
        float engage = 100.0f;                                     
        float matchSeconds = 300.0f;                               
    };

    inline Page battlePage()
    {
        Page p{"battle", "BATTLE", {}, 0};
        const BattleSettings d;
        p.items.push_back(makeToggle("giga", "GIGA mode", false));
        p.items.push_back(makeSlider("size", "Battle size (AI units)", d.size, 0.0f, 8000.0f, 10.0f));
        p.items.push_back(makeSlider("mixTank", "Mix: tanks", d.mix[0], 0.0f, 100.0f, 1.0f, " %"));
        p.items.push_back(makeSlider("mixAmp", "Mix: AMPs", d.mix[1], 0.0f, 100.0f, 1.0f, " %"));
        p.items.push_back(makeSlider("mixHeli", "Mix: helicopters", d.mix[2], 0.0f, 100.0f, 1.0f, " %"));
        p.items.push_back(makeSlider("mixJet", "Mix: jets", d.mix[3], 0.0f, 100.0f, 1.0f, " %"));
        p.items.push_back(makeSlider("respawn", "Respawn delay", d.respawn, 2.0f, 120.0f, 1.0f, " s"));
        p.items.push_back(makeSlider("rate", "Spawn rate", d.rate, 1.0f, 500.0f, 1.0f, " units/s"));
        p.items.push_back(makeSlider("engage", "Engagement range", d.engage, 50.0f, 150.0f, 5.0f, " %"));
        p.items.push_back(makeSlider("matchSeconds", "Deathmatch length", d.matchSeconds, 60.0f, 1800.0f, 30.0f, " s"));
        p.items.push_back(makeAction("startBattle", "Start battle"));
        p.items.push_back(makeAction("stopBattle", "Stop battle"));
        p.items.push_back(makeAction("restartBattle", "Restart battle"));
        p.items.push_back(makeAction("startMatch", "Start deathmatch"));
        p.items.push_back(makeInfo("units", "Units"));
        p.items.push_back(makeInfo("alive", "Alive by type"));
        p.items.push_back(makeInfo("fps", "Frame rate"));
        return p;
    }

    inline void battleToItems(Menu& m, const BattleSettings& b)
    {
        m.set("giga", b.giga, false);
        m.set("size", b.size, false);
        m.set("mixTank", b.mix[0], false);
        m.set("mixAmp", b.mix[1], false);
        m.set("mixHeli", b.mix[2], false);
        m.set("mixJet", b.mix[3], false);
        m.set("respawn", b.respawn, false);
        m.set("rate", b.rate, false);
        m.set("engage", b.engage, false);
        m.set("matchSeconds", b.matchSeconds, false);
    }

    inline BattleSettings itemsToBattle(const Menu& m)
    {
        auto v = [&](const char* id) { return m.find(id)->value; };
        BattleSettings b;
        b.giga = v("giga");
        b.size = v("size"), b.mix = {v("mixTank"), v("mixAmp"), v("mixHeli"), v("mixJet")}, b.respawn = v("respawn"), b.rate = v("rate");
        b.engage = v("engage"), b.matchSeconds = v("matchSeconds");
        return b;
    }

    inline Page audioPage()
    {
        Page p{"audio", "AUDIO", {}, 0};
        const audio::Settings defaults;
        for (const auto& f : audio::FIELDS)
            p.items.push_back(f.max == 1 && f.step == 1 ? makeToggle(f.id, f.label, defaults.*(f.member) >= 0.5f)
                : makeSlider(f.id, f.label, defaults.*(f.member), f.min, f.max, f.step, "", f.step < 1 ? 2 : 0));
        p.items.push_back(makeAction("audioApply", "Apply and save"));
        p.items.push_back(makeAction("audioRevert", "Revert to the applied settings"));
        return p;
    }
    inline void audioToItems(Menu& m, const audio::Settings& s)
    {
        for (const auto& f : audio::FIELDS) m.set(f.id, s.*(f.member), false);
    }
    inline audio::Settings itemsToAudio(const Menu& m)
    {
        audio::Settings s;
        for (const auto& f : audio::FIELDS) s.*(f.member) = m.find(f.id)->value;
        return audio::sanitize(s);
    }

    inline Menu makeMenu()
    {
        Menu m;
        m.pages = {graphicsPage(), scenePage(), battlePage(), audioPage()};
        return m;
    }

     
     
    struct BattleRamp
    {
        bool running = false;
        float level = 0.0f;
        int target = 0;
        void start(int to, int from)
        {
            running = true;
            target = std::max(to, 0);
            level = static_cast<float>(std::min(from, target));
        }
        void retarget(int to) { target = std::max(to, 0); }
        void stop()
        {
            running = false;
            level = 0.0f;
        }
        int step(float dt, float rate)
        {
            if (!running)
                return 0;
            level = std::min(level + std::max(rate, 0.0f) * dt, static_cast<float>(target));
            return active();
        }
        int active() const { return running ? static_cast<int>(level + 1e-4f) : 0; }
    };

     
     
    inline std::array<float, 4> spawnWeights(const std::array<float, 4>& have, const std::array<float, 4>& mix, int total)
    {
        const float sum = mix[0] + mix[1] + mix[2] + mix[3];
        std::array<float, 4> w{};
        if (sum <= 0.0f)
            return w;
        float any = 0.0f;
        for (size_t t = 0; t < 4; ++t)
        {
            w[t] = std::max(0.0f, static_cast<float>(total) * mix[t] / sum - have[t]);
            any += w[t];
        }
        return any > 0.0f ? w : mix;
    }

     
     
     
    inline std::array<int, 4> splitActive(int total, const std::array<float, 4>& mix, const std::array<int, 4>& have)
    {
        std::array<int, 4> out{};
        const float sum = mix[0] + mix[1] + mix[2] + mix[3];
        for (int k = 0; k < total && sum > 0.0f; ++k)
        {
            int best = -1;
            float bestKey = 0.0f;
            for (size_t t = 0; t < 4; ++t)
            {
                if (mix[t] <= 0.0f || out[t] >= have[t])
                    continue;
                const float key = mix[t] * static_cast<float>(k + 1) - static_cast<float>(out[t]) * sum;
                if (best < 0 || key > bestKey)
                    best = static_cast<int>(t), bestKey = key;
            }
            if (best < 0)
                break;
            ++out[static_cast<size_t>(best)];
        }
        return out;
    }

     
    inline std::array<float, 4> poolDeficit(int size, const std::array<float, 4>& mix, const std::array<int, 4>& have)
    {
        std::array<float, 4> d{};
        const float sum = mix[0] + mix[1] + mix[2] + mix[3];
        if (sum <= 0.0f)
            return d;
        for (size_t t = 0; t < 4; ++t)
            d[t] = static_cast<float>(std::max(0, static_cast<int>(std::ceil(static_cast<float>(size) * mix[t] / sum - 1e-4f)) - have[t]));
        return d;
    }

     
     
    inline std::vector<std::pair<const char*, float*>> fields(SceneSettings& s, BattleSettings& b, float (&flags)[3])
    {
        return {{"giga", &b.giga}, {"timeOfDay", &s.timeOfDay}, {"dayMinutes", &s.dayMinutes}, {"hazePct", &s.hazePct}, {"vignette", &s.vignette},
                {"sunIntensity", &s.sunIntensity}, {"exposure", &s.exposure}, {"windDir", &s.windDir}, {"windSpeed", &s.windSpeed},
                {"gust", &s.gust}, {"size", &b.size}, {"mixTank", &b.mix[0]}, {"mixAmp", &b.mix[1]}, {"mixHeli", &b.mix[2]},
                {"mixJet", &b.mix[3]}, {"respawn", &b.respawn}, {"rate", &b.rate}, {"engage", &b.engage},
                {"matchSeconds", &b.matchSeconds}, {"animate", &flags[0]}, {"look", &flags[1]}, {"hazeColor", &flags[2]}};
    }

    inline std::string toJson(const SceneSettings& scene, const BattleSettings& battle)
    {
        SceneSettings s = scene;
        BattleSettings b = battle;
        float flags[3] = {s.animate ? 1.0f : 0.0f, static_cast<float>(s.look), static_cast<float>(s.hazeColor)};
        std::string out = "{";
        char buf[64];
        for (const auto& [key, ptr] : fields(s, b, flags))
        {
            std::snprintf(buf, sizeof buf, "%s\"%s\":%.6g", out.size() > 1 ? "," : "", key, *ptr);
            out += buf;
        }
        std::snprintf(buf, sizeof buf, ",\"grade\":%d}", s.grade);
        return out + buf;
    }

     
    inline bool fromJson(std::string_view text, SceneSettings& scene, BattleSettings& battle)
    {
        SceneSettings s = scene;
        BattleSettings b = battle;
        float grade = static_cast<float>(s.grade);
        float flags[3] = {s.animate ? 1.0f : 0.0f, static_cast<float>(s.look), static_cast<float>(s.hazeColor)};
        auto table = fields(s, b, flags);
        table.emplace_back("grade", &grade);
        size_t i = text.find('{');
        if (i == std::string_view::npos)
            return false;
        ++i;
        int pairs = 0;
        while (i < text.size())
        {
            const size_t q0 = text.find('"', i);
            if (q0 == std::string_view::npos)
                break;
            const size_t q1 = text.find('"', q0 + 1);
            if (q1 == std::string_view::npos)
                return false;
            const std::string_view key = text.substr(q0 + 1, q1 - q0 - 1);
            const size_t colon = text.find(':', q1);
            if (colon == std::string_view::npos)
                return false;
            const std::string num(text.substr(colon + 1, 32));
            char* end = nullptr;
            const float v = std::strtof(num.c_str(), &end);
            if (end == num.c_str() || !std::isfinite(v))
                return false;
            for (auto& [k, ptr] : table)
                if (key == k)
                    *ptr = v;
            ++pairs;
            i = colon + 1 + static_cast<size_t>(end - num.c_str());
        }
        if (pairs == 0)
            return false;
        s.animate = flags[0] >= 0.5f, s.look = std::clamp(static_cast<int>(flags[1]), 0, LOOK_CUSTOM);
        s.hazeColor = std::clamp(static_cast<int>(flags[2]), 0, 4), s.grade = std::clamp(static_cast<int>(grade), 0, 4);
        scene = s;
        battle = b;
        return true;
    }
}  
