 
 
namespace q = warzone::quality;

     
    float machineMs(float k, const q::Settings& s)
    {
        return k * (4.0f + 6.0f * s.viewDistance * s.viewDistance + 2.5f * s.buildingDetail + 1.5f * s.unitDetail + 0.8f * s.effects);
    }

    struct Sim
    {
        int changes = 0, changesLastMinute = 0, downs = 0, ups = 0;
        float finalMs = 0, seconds = 0;
    };

     
     
    template <class Cost>
    Sim simulate(q::Governor& g, Cost cost, float seconds, float vram = 0.4f, int spikeEvery = 0, uint32_t seed = 7)
    {
        Sim r;
        uint32_t rng = seed;
        q::Settings eff = g.effective();
        int frameNo = 0;
        float t = 0;
        while (t < seconds)
        {
            rng = rng * 1664525u + 1013904223u;
            float ms = cost(eff) * (1.0f + 0.04f * (static_cast<float>((rng >> 16) & 0xff) / 127.5f - 1.0f));
            if (spikeEvery > 0 && ++frameNo % spikeEvery == 0)
                ms = 150.0f;
            r.finalMs = ms;
            t += ms / 1000.0f;
            const int before = g.drop();
            if (g.frame(ms / 1000.0f, vram))
            {
                eff = g.effective();
                ++r.changes;
                r.changesLastMinute += t > seconds - 60.0f ? 1 : 0;
                r.downs += g.drop() > before ? 1 : 0;
                r.ups += g.drop() < before ? 1 : 0;
            }
        }
        r.seconds = t;
        return r;
    }

    q::Governor startGovernor(const q::Settings& ceiling, float warmup = 0.0f)
    {
        q::Governor g;
        g.start(ceiling, warmup);
        return g;
    }

    bool atLeast(const q::Knobs& a, const q::Knobs& b)  
    {
        return b.radius >= a.radius && b.pieceBudget >= a.pieceBudget && b.detailRange >= a.detailRange && b.detailBudget >= a.detailBudget &&
               b.lowRange >= a.lowRange && b.streamRate >= a.streamRate && b.aiCount >= a.aiCount && b.aiRange >= a.aiRange &&
               b.farScale >= a.farScale && b.effectReach >= a.effectReach && b.cardBudget >= a.cardBudget && b.lights >= a.lights &&
               b.particleQuality >= a.particleQuality && b.gfx.shadowResolution >= a.gfx.shadowResolution &&
               b.gfx.shadowDistance >= a.gfx.shadowDistance && b.gfx.pcfRadius >= a.gfx.pcfRadius && b.gfx.lodBias <= a.gfx.lodBias;
    }

    void testQualityPresets()
    {
        std::puts("quality presets");
        const q::Preset all[4] = {q::Preset::Low, q::Preset::Medium, q::Preset::High, q::Preset::Ultra};
        for (int i = 0; i < 3; ++i)
            CHECK(atLeast(q::knobsFor(q::presetSettings(all[i])), q::knobsFor(q::presetSettings(all[i + 1]))));
        for (int i = 0; i < 4; ++i)
        {
            CHECK(q::classify(q::presetSettings(all[i])) == all[i]);
            CHECK(q::presetSettings(all[i]).preset == all[i]);
        }
        q::Settings hybrid = q::presetSettings(q::Preset::High);
        hybrid.gameLodScale = 2.5f, hybrid.engineLodBias = 1.0f, hybrid.engineLod = true, hybrid.preset = q::Preset::Custom;
        const auto hk = q::knobsFor(hybrid);
        CHECK(hk.detailRange == 2.5f && hk.lowRange == 2.5f && hk.detailBudget == 6.25f && hk.gfx.lodBias == 1.0f);
        q::Settings restored;
        CHECK(q::fromJson(q::toJson(hybrid), restored));
        CHECK(restored.gameLodScale == 2.5f && restored.engineLodBias == 1.0f && restored.engineLod);
        CHECK(q::classify(hybrid) == q::Preset::Custom);
        for (int drop = 0; drop <= q::stepsAvailable(hybrid); ++drop)
        {
            const auto effective = q::degrade(hybrid, drop, 4);
            CHECK(effective.gameLodScale == 2.5f && effective.engineLodBias == 1.0f && effective.engineLod);
        }
        hybrid.gameLodScale = 99.0f, hybrid.engineLodBias = -99.0f;
        CHECK(q::sanitize(hybrid).gameLodScale == 16.0f && q::sanitize(hybrid).engineLodBias == -2.0f);
        hybrid.gameLodScale = NAN, hybrid.engineLodBias = NAN;
        CHECK(q::sanitize(hybrid).gameLodScale == 1.0f && q::sanitize(hybrid).engineLodBias == 0.0f);
        CHECK(q::presetSettings(q::Preset::Low).gameLodScale == 1.5f && q::presetSettings(q::Preset::Low).engineLodBias == 3.0f);
        CHECK(q::presetSettings(q::Preset::Medium).gameLodScale == 1.75f && q::presetSettings(q::Preset::Medium).engineLodBias == 2.5f);
        CHECK(q::presetSettings(q::Preset::Ultra).gameLodScale == 2.1f && q::presetSettings(q::Preset::Ultra).engineLodBias == 2.0f);
         
        const q::Knobs high = q::knobsFor(q::presetSettings(q::Preset::High));
        CHECK(high.radius == 1.0f && high.pieceBudget == 1.0f && high.detailRange == 2.0f && high.detailBudget == 4.0f);
        CHECK(high.lowRange == 2.0f && high.streamRate == 1.0f && high.aiCount == 1.0f && high.aiRange == 1.0f);
        CHECK(high.farScale == 1.0f && high.effectReach == 1.0f && high.cardBudget == 1.0f && high.lights == 8);
        CHECK(high.gfx.shadowResolution == 2048 && high.gfx.shadowDistance == 900.0f && high.gfx.pcfRadius == 2);
        CHECK(high.gfx.ao && high.gfx.bloom && high.gfx.taa && high.gfx.lod && high.gfx.lodBias == 2.0f);
         
        const q::Knobs ultra = q::knobsFor(q::presetSettings(q::Preset::Ultra)), low = q::knobsFor(q::presetSettings(q::Preset::Low));
        CHECK(ultra.radius >= 1.15f && ultra.pieceBudget > high.pieceBudget && ultra.detailRange > 1.2f && ultra.detailBudget > 1.5f && ultra.lowRange > 1.2f);
        CHECK(ultra.aiCount > 1.0f && ultra.aiRange > 1.2f && ultra.farScale >= 1.15f && ultra.effectReach > 1.2f && ultra.cardBudget > 1.3f);
        CHECK(ultra.gfx.shadowResolution == 2560 && ultra.gfx.shadowDistance == 1000.0f);
        CHECK(ultra.detailRange > high.detailRange && ultra.detailBudget > high.detailBudget && ultra.lowRange > high.lowRange);
        CHECK(26000.0f * ultra.pieceBudget < 32768.0f && 2500.0f * ultra.detailBudget < 16384.0f);
        CHECK(ultra.gfx.lodBias == high.gfx.lodBias && ultra.particleQuality == 3);
        CHECK(ultra.streamRate <= 1.0f);  
        CHECK(low.radius < 0.7f && low.pieceBudget < 0.5f && low.detailBudget < 0.6f && low.aiCount < 0.3f && low.lights < 4);
        CHECK(!low.gfx.ao && !low.gfx.bloom && !low.gfx.taa && low.gfx.shadowResolution == 1024);
         
        q::Settings s = q::presetSettings(q::Preset::High);
        s.adaptive = false, s.targetFps = 144.0f;
        CHECK(q::classify(s) == q::Preset::High);
        s.viewDistance = 0.85f;
        CHECK(q::classify(s) == q::Preset::Custom);
         
        q::Settings wild;
        wild.viewDistance = 9.0f, wild.buildingDetail = -4, wild.unitDetail = 12, wild.effects = 7, wild.shadows = -1, wild.targetFps = 1.0f;
        wild = q::sanitize(wild);
        CHECK(wild.viewDistance == 1.5f && wild.buildingDetail == 0 && wild.unitDetail == 3 && wild.effects == 3 && wild.shadows == 0 && wild.targetFps == 20.0f);
        q::Preset p = q::Preset::High;
        CHECK(q::presetFromName("ULTRA", p) && p == q::Preset::Ultra && q::presetFromName("low", p) && p == q::Preset::Low);
        CHECK(!q::presetFromName("potato", p) && p == q::Preset::Low);
        CHECK(std::string(q::presetName(q::Preset::Custom)) == "Custom");
    }

    void testQualityHardware()
    {
        std::puts("quality hardware suggestion");
        using P = q::Preset;
         
        CHECK(q::suggestPreset({8192, 16, 0.0f}) == P::High);
        CHECK(q::suggestPreset({8192, 16, 5.0f}) == P::High);
        CHECK(q::suggestPreset({8192, 16, 9.0f}) == P::High);
        CHECK(q::suggestPreset({6144, 12, 7.0f}) == P::High);
        CHECK(q::suggestPreset({4096, 8, 0.0f}) == P::Low);
        CHECK(q::suggestPreset({2048, 16, 5.0f}) == P::Low);
        CHECK(q::suggestPreset({5120, 8, 0.0f}) == P::Medium);
        CHECK(q::suggestPreset({10240, 16, 6.0f}) == P::High);
         
        CHECK(q::suggestPreset({12288, 16, 5.0f}) == P::Ultra);
        CHECK(q::suggestPreset({24576, 32, 3.0f}) == P::Ultra);
        CHECK(q::suggestPreset({12288, 16, 0.0f}) == P::High);
        CHECK(q::suggestPreset({12288, 16, 12.0f}) == P::High);
        CHECK(q::suggestPreset({12288, 6, 5.0f}) == P::High);
         
        CHECK(q::suggestPreset({8192, 2, 5.0f}) == P::Low);
        CHECK(q::suggestPreset({8192, 4, 5.0f}) == P::Medium);
        CHECK(q::suggestPreset({8192, 16, 40.0f}) == P::Medium);
        CHECK(q::suggestPreset({4096, 8, 40.0f}) == P::Low);
        CHECK(q::suggestPreset({0, 0, 0.0f}) == P::Medium);
         
        q::FrameProbe probe(3.0f);
        for (int i = 0; i < 100; ++i)
            probe.add(i % 20 == 0 ? 0.120f : 0.010f);
        CHECK(!probe.done());
        for (int i = 0; i < 200; ++i)
            probe.add(0.010f);
        CHECK(probe.done());
        CHECK(std::abs(probe.medianMs() - 10.0f) < 0.01f);
        probe.add(5.0f);  
        CHECK(std::abs(probe.medianMs() - 10.0f) < 0.01f);
    }

    void testQualitySteps()
    {
        std::puts("quality steps");
        const q::Settings high = q::presetSettings(q::Preset::High), ultra = q::presetSettings(q::Preset::Ultra), low = q::presetSettings(q::Preset::Low);
        CHECK(q::degrade(high, 0).viewDistance == 1.0f && q::degrade(high, 0).effects == 2);
        const int n = q::stepsAvailable(high);
        CHECK(n == 11);
         
        for (int d = 1; d <= n; ++d)
        {
            const q::Settings a = q::degrade(high, d - 1), b = q::degrade(high, d);
            CHECK(b.viewDistance <= a.viewDistance && b.buildingDetail <= a.buildingDetail && b.unitDetail <= a.unitDetail && b.effects <= a.effects);
            CHECK(b.viewDistance != a.viewDistance || b.buildingDetail != a.buildingDetail || b.unitDetail != a.unitDetail || b.effects != a.effects);
        }
        const q::Settings floor = q::degrade(high, 99);
        CHECK(floor.viewDistance == 0.5f && floor.buildingDetail == 0 && floor.unitDetail == 0 && floor.effects == 0);
        CHECK(q::degrade(high, n).viewDistance == q::degrade(high, 99).viewDistance);
         
        CHECK(floor.shadows == high.shadows && floor.ambientOcclusion == high.ambientOcclusion && floor.bloom == high.bloom && floor.antiAliasing);
         
        CHECK(q::stepKind(high, 1) == q::Step::Effects && q::stepKind(high, 2) == q::Step::Units);
        CHECK(q::stepKind(high, 3) == q::Step::View && q::stepKind(high, 4) == q::Step::Buildings);
        CHECK(q::degrade(high, 3).viewDistance < 1.0f && q::degrade(high, 2).viewDistance == 1.0f);
         
        CHECK(q::stepsAvailable(low) == 2);
        CHECK(q::stepsAvailable(ultra) == 14 &&q::stepsAvailable(ultra) > q::stepsAvailable(high));
        CHECK(q::degrade(ultra, 99).viewDistance == 0.5f && q::degrade(ultra, 99).buildingDetail == 0 && q::degrade(low, 99).viewDistance == 0.5f);
         
        const q::Settings braked = q::degrade(high, 0, 2);
        CHECK(braked.viewDistance < 1.0f && braked.buildingDetail == 1 && braked.effects == 2 && braked.unitDetail == 2);
        CHECK(q::degrade(low, 0, 4).buildingDetail == 0 && q::degrade(low, 0, 4).viewDistance == 0.5f);
        CHECK(q::statusLine(q::presetSettings(q::Preset::High), [] { auto s = q::presetSettings(q::Preset::High); s.viewDistance = 0.85f; return s; }(), 63.2f) ==
              "adaptive: view 0.85, buildings 2, fps 63 (target 60)");
        q::Settings fixed = high;
        fixed.adaptive = false;
        CHECK(q::statusLine(fixed, fixed, 144.0f) == "High: view 1.00, buildings 2, fps 144 (target 60)");
    }

    void testQualityFile()
    {
        std::puts("quality user file");
        for (int i = 0; i < 5; ++i)
        {
            q::Settings s = q::presetSettings(static_cast<q::Preset>(i));
            s.adaptive = i % 2 == 0, s.targetFps = i % 2 ? 90.0f : 144.0f;
            q::Settings back;
            CHECK(q::fromJson(q::toJson(s), back));
            CHECK(back.preset == s.preset && back.adaptive == s.adaptive && back.targetFps == s.targetFps);
            CHECK(back.viewDistance == s.viewDistance && back.buildingDetail == s.buildingDetail && back.shadows == s.shadows);
        }
        q::Settings custom = q::presetSettings(q::Preset::Custom);
        custom.viewDistance = 0.85f, custom.buildingDetail = 3, custom.unitDetail = 0, custom.effects = 1, custom.shadows = 3;
        custom.ambientOcclusion = false, custom.bloom = false, custom.antiAliasing = false, custom.engineLod = true;
        q::Settings back;
        CHECK(q::fromJson(q::toJson(custom), back));
        CHECK(back.preset == q::Preset::Custom && std::abs(back.viewDistance - 0.85f) < 1e-6f && back.buildingDetail == 3 && back.unitDetail == 0);
        CHECK(back.effects == 1 && back.shadows == 3 && !back.ambientOcclusion && !back.bloom && !back.antiAliasing && back.engineLod);
         
        CHECK(q::toJson(q::presetSettings(q::Preset::Ultra)).find("viewDistance") == std::string::npos);
        CHECK(q::fromJson("{\"preset\": \"Medium\", \"viewDistance\": 1.4}", back) && back.preset == q::Preset::Medium && back.viewDistance == 0.8f);
         
        CHECK(q::fromJson("{\"other\":1,\"preset\":\"Custom\",\"viewDistance\":9,\"effects\":-3}", back));
        CHECK(back.viewDistance == 1.5f && back.effects == 0 && back.buildingDetail == 2);
         
        q::Settings keep = custom;
        CHECK(!q::fromJson("", keep) && !q::fromJson("{\"preset\": \"Potato\"}", keep) && !q::fromJson("not json at all", keep));
        CHECK(keep.buildingDetail == 3);
    }

    void testQualityGovernorDown()
    {
        std::puts("quality governor: weak machines");
        const q::Settings high = q::presetSettings(q::Preset::High);
         
        {
            q::Governor g = startGovernor(high);
            const Sim r = simulate(g, [](const q::Settings& s) { return machineMs(1.8f, s); }, 240.0f);
            CHECK(g.drop() == 8);
            CHECK(r.finalMs < 1000.0f / 60.0f * 1.08f);
            CHECK(r.downs == r.changes && r.ups == 0);
            CHECK(r.changesLastMinute == 0);
            CHECK(g.effective().viewDistance < 1.0f && g.effective().effects == 0);
            CHECK(g.fps() > 55.0f && g.fps() < 80.0f);
             
            CHECK(!g.decisions().empty());
            for (size_t i = 0; i < g.decisions().size(); ++i)
                CHECK(std::string(g.decisions()[i].why).find("step down") == 0 && (i == 0 || g.decisions()[i].t > g.decisions()[i - 1].t));
        }
         
        {
            q::Governor g = startGovernor(high);
            for (float t = 0; t < 12.0f; t += 0.1f)  
                g.frame(0.1f, 0.4f);
            CHECK(g.drop() >= 2);
            CHECK(std::string(g.decisions()[0].why).find("x2") != std::string::npos);
        }
         
        {
            const auto ultra = q::presetSettings(q::Preset::Ultra);
            q::Governor g = startGovernor(ultra, 3.0f);
            for (int i = 0; i < 500; ++i)
                g.frame(0.040f, 0.4f);
            CHECK(!g.decisions().empty() && g.decisions().front().t >= 5.0f && g.decisions().front().t < 5.2f);
            CHECK(g.drop() >= 2 && std::string(g.decisions().front().why).find("step down x2") == 0);
            CHECK(g.effective().gameLodScale == ultra.gameLodScale && g.effective().engineLodBias == ultra.engineLodBias);
        }
         
        {
            q::Governor g = startGovernor(high);
            const Sim r = simulate(g, [](const q::Settings& s) { return machineMs(6.0f, s); }, 300.0f);
            CHECK(g.drop() == g.maxDrop() && g.maxDrop() == q::stepsAvailable(high));
            CHECK(r.changesLastMinute == 0 && r.ups == 0);
            CHECK(g.effective().viewDistance == 0.5f && g.effective().buildingDetail == 0);
        }
         
        {
            q::Governor g = startGovernor(q::presetSettings(q::Preset::Low));
            const Sim r = simulate(g, [](const q::Settings& s) { return machineMs(8.0f, s); }, 120.0f);
            CHECK(g.maxDrop() == 2 && g.drop() == 2 && r.ups == 0 && r.changesLastMinute == 0);
        }
         
        {
            q::Settings s = high;
            s.targetFps = 144.0f;
            q::Governor g = startGovernor(s);
            simulate(g, [](const q::Settings& v) { return machineMs(0.9f, v); }, 120.0f);
            CHECK(g.drop() > 0);
            s.targetFps = 30.0f;
            q::Governor slow = startGovernor(s);
            simulate(slow, [](const q::Settings& v) { return machineMs(1.5f, v); }, 120.0f);
            CHECK(slow.drop() == 0);
        }
         
        {
            q::Settings fixed = high;
            fixed.adaptive = false;
            q::Governor g = startGovernor(fixed);
            const Sim r = simulate(g, [](const q::Settings& s) { return machineMs(4.0f, s); }, 60.0f);
            CHECK(r.changes == 0 && g.drop() == 0 && g.fps() < 20.0f);
        }
    }

    void testQualityGovernorUp()
    {
        std::puts("quality governor: strong machines and hysteresis");
        const q::Settings high = q::presetSettings(q::Preset::High), ultra = q::presetSettings(q::Preset::Ultra);
         
        {
            q::Governor g = startGovernor(high);
            const Sim r = simulate(g, [](const q::Settings& s) { return machineMs(0.5f, s); }, 180.0f);
            CHECK(r.changes == 0 && g.drop() == 0);
        }
         
        {
            q::Governor g = startGovernor(high);
            g.forceDrop(6);
            const Sim r = simulate(g, [](const q::Settings& s) { return machineMs(0.5f, s); }, 180.0f);
            CHECK(g.drop() == 0 && r.ups == 6 && r.downs == 0 && r.changes == 6);
            CHECK(r.changesLastMinute == 0);
            for (const auto& d : g.decisions())
                CHECK(std::string(d.why).find("step up") == 0);
             
            for (size_t i = 1; i < g.decisions().size(); ++i)
                CHECK(g.decisions()[i].t - g.decisions()[i - 1].t >= 3.0f * 2.0f);
        }
         
        {
            q::Governor g = startGovernor(ultra);
            const Sim r = simulate(g, [](const q::Settings& s) { return machineMs(0.4f, s); }, 120.0f);
            CHECK(r.changes == 0);
            q::Governor mid = startGovernor(ultra);
            const Sim m = simulate(mid, [](const q::Settings& s) { return machineMs(0.9f, s); }, 300.0f);
            CHECK(mid.drop() > 0 && m.changesLastMinute == 0 && m.finalMs < 1000.0f / 60.0f * 1.08f);
        }
         
         
        {
            q::Governor g = startGovernor(high);
            const Sim r = simulate(g, [&](const q::Settings& s) { return s.effects == high.effects ? 19.0f : 11.0f; }, 300.0f);
            CHECK(r.changes == 1 && r.downs == 1 && g.drop() == 1);
        }
         
        {
            q::Governor g = startGovernor(high);
            g.forceDrop(1);
            const int wait = g.upNeed();
            const Sim r = simulate(g, [&](const q::Settings& s) { return s.effects == high.effects ? 19.0f : 11.0f; }, 240.0f);
            CHECK(r.ups == 1 && r.downs == 1 && g.drop() == 1);
            CHECK(g.upNeed() >= wait * 2);
            CHECK(std::string(g.decisions().back().why).find("did not hold") != std::string::npos);
            CHECK(r.changesLastMinute == 0);
        }
    }

    void testQualityGovernorNoise()
    {
        std::puts("quality governor: spikes, jumps, warmup");
        const q::Settings high = q::presetSettings(q::Preset::High);
        auto cost = [](const q::Settings& s) { return machineMs(0.6f, s); };  
         
        {
            q::Governor g = startGovernor(high);
            CHECK(simulate(g, cost, 120.0f, 0.4f, 90).changes == 0);
        }
         
        {
            q::Governor g = startGovernor(high);
            int frames = 0;
            bool moved = false;
            for (float t = 0; t < 120.0f;)
            {
                const float dt = frames++ % 800 >= 795 ? 0.080f : 0.012f;  
                t += dt;
                moved = g.frame(dt, 0.4f) || moved;
            }
            CHECK(!moved && g.drop() == 0);
        }
         
        {
            q::Governor g = startGovernor(high);
            bool moved = false;
            for (int i = 0; i < 1000 && !moved; ++i)
                moved = g.frame(i % 4 == 0 ? 0.040f : 0.012f, 0.4f);
            CHECK(moved && g.drop() == 2);
        }
         
        {
            q::Governor g = startGovernor(high, 4.0f);
            bool moved = false;
            for (int i = 0; i < 38; ++i)  
                moved = g.frame(0.1f, 0.4f) || moved;
            CHECK(!moved && g.skipLeft() > 0.0f && g.decisions().empty());
            for (int i = 0; i < 40 && !moved; ++i)
                moved = g.frame(0.1f, 0.4f);
            CHECK(moved && g.clock() > 6.0f && g.clock() < 7.5f);
        }
         
        {
            q::Governor g = startGovernor(high);
            simulate(g, cost, 10.0f);
            g.ignoreFor(2.0f);
            bool moved = false;
            for (int i = 0; i < 19; ++i)  
                moved = g.frame(0.09f, 0.4f) || moved;
            CHECK(!moved && g.drop() == 0);
            CHECK(!simulate(g, cost, 20.0f).changes);
        }
         
        {
            q::Governor g = startGovernor(high);
            simulate(g, cost, 6.0f);
            bool moved = g.frame(3.0f, 0.4f);
            for (int i = 0; i < 12; ++i)
                moved = g.frame(0.1f, 0.4f) || moved;
            CHECK(!moved && g.drop() == 0);
        }
         
        {
            q::Governor g = startGovernor(high);
            g.forceDrop(5);
            q::Settings low = q::presetSettings(q::Preset::Low);
            g.start(low, 0.0f);
            CHECK(g.drop() == 0 && g.memDrop() == 0 && g.maxDrop() == 2 && g.effective().viewDistance == 0.6f);
        }
    }

    void testQualityGovernorMemory()
    {
        std::puts("quality governor: video memory");
        const q::Settings high = q::presetSettings(q::Preset::High);
        auto strong = [](const q::Settings& s) { return machineMs(0.5f, s); };
         
        {
            q::Governor g = startGovernor(high);
            g.forceDrop(3);  
            const Sim r = simulate(g, strong, 90.0f, 0.90f);
            CHECK(r.changes == 0 && g.drop() == 3);
            CHECK(g.decisions().size() == 1 && std::string(g.decisions()[0].why).find("held") != std::string::npos);
            CHECK(g.effective().viewDistance < 1.0f);
             
            const Sim free = simulate(g, strong, 60.0f, 0.40f);
            CHECK(g.drop() == 0 && free.ups == 3);
        }
        {
            q::Governor g = startGovernor(high);
            g.forceDrop(2);  
            const Sim r = simulate(g, strong, 90.0f, 0.90f);
            CHECK(g.drop() == 0 && r.ups == 2);
        }
         
        {
            q::Governor g = startGovernor(high);
            q::Settings eff = g.effective();
            float lowestView = 9.0f;
            int lowestBuildings = 9;
            bool grew = false;
            for (int i = 0; i < 6000; ++i)
            {
                if (g.frame(0.010f, 0.95f))
                {
                    const q::Settings now = g.effective();
                    grew = grew || now.viewDistance > eff.viewDistance + 1e-4f || now.buildingDetail > eff.buildingDetail;
                    eff = now;
                }
                lowestView = std::min(lowestView, g.effective().viewDistance);
                lowestBuildings = std::min(lowestBuildings, g.effective().buildingDetail);
            }
            CHECK(!grew);
            CHECK(g.memDrop() == 4 && g.effective().buildingDetail == 0 && g.effective().viewDistance < 0.85f);
            CHECK(g.effective().effects == 2 && g.effective().unitDetail == 2);  
            CHECK(std::string(g.decisions().front().why).find("memory brake") != std::string::npos);
             
            for (int i = 0; i < 6000; ++i)
                g.frame(0.010f, 0.50f);
            CHECK(g.memDrop() == 0 && g.effective().viewDistance == 1.0f);
        }
         
        {
            q::Governor g = startGovernor(high);
            const Sim r = simulate(g, [](const q::Settings& s) { return machineMs(1.8f, s); }, 120.0f, 0.97f);
            CHECK(g.memDrop() == 4 && g.drop() > 0 && r.ups == 0);
        }
         
        {
            q::Governor g = startGovernor(high);
            CHECK(simulate(g, strong, 60.0f, 0.0f).changes == 0 && g.memDrop() == 0);
        }
    }
