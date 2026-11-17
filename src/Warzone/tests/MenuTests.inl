 
 

 

namespace
{
    struct FakeQuality  
    {
        enum class P : uint8_t { Low, Medium, High, Ultra, Custom };
        P preset = P::Ultra;
        bool adaptive = false;
        float targetFps = 90.0f, viewDistance = 1.25f, gameLodScale = 2.5f, engineLodBias = 1.0f;
        int buildingDetail = 3, unitDetail = 1, effects = 0, shadows = 3;
        bool ambientOcclusion = false, bloom = true, antiAliasing = false, engineLod = true;
    };
}

void testMenuItems()
{
    std::printf("menu items\n");
    using namespace warzone::menu;
    Menu m = makeMenu();
     
    CHECK(m.set("viewDistance", 0.83f, false));
    CHECK(std::abs(m.find("viewDistance")->value - 0.85f) < 1e-5f);
    m.set("viewDistance", 9.0f, false);
    CHECK(m.find("viewDistance")->value == 1.5f);
    m.set("viewDistance", -3.0f, false);
    CHECK(m.find("viewDistance")->value == 0.5f);
     
    m.set("shadows", 9.0f, false);
    CHECK(m.find("shadows")->index() == 3);
    m.set("bloom", 0.4f, false);
    CHECK(m.find("bloom")->value == 0.0f);
     
    CHECK(display(*m.find("shadows")) == "Ultra");
    CHECK(display(*m.find("bloom")) == "Off");
    m.set("timeOfDay", 13.25f, false);
    CHECK(display(*m.find("timeOfDay")) == "13.25 h");
     
    CHECK(!m.set("bloom", 0.0f, false));
    CHECK(!m.set("nope", 1.0f));
    CHECK(!m.set("apply", 1.0f) && !m.set("hardware", 1.0f));
    CHECK(m.find("size")->kind == warzone::menu::Kind::Slider && m.find("startBattle")->kind == warzone::menu::Kind::Action);
}

void testMenuNavigation()
{
    std::printf("menu navigation\n");
    using namespace warzone::menu;
    Menu m = makeMenu();
     
    m.setTab(0);
    m.tabStep(-1);
    CHECK(m.tab == 3);
    m.tabStep(1);
    CHECK(m.tab == 0);
     
    m.setTab(0);
    CHECK(m.selectedItem()->id == "preset");
    m.move(-1);
    CHECK(m.selectedItem()->id == "revert");  
    m.move(1);
    CHECK(m.selectedItem()->id == "preset");
    CHECK(m.select("apply"));
    m.move(-1);
    CHECK(m.selectedItem()->id == "engineLod");  
     
    m.select("viewDistance");
    CHECK(m.adjust(1) && std::abs(m.find("viewDistance")->value - 1.05f) < 1e-5f);
    CHECK(m.adjust(-1, 4) && std::abs(m.find("viewDistance")->value - 0.85f) < 1e-5f);
    m.select("shadows");
    m.set("shadows", 3.0f, false);
    CHECK(m.adjust(1) && m.find("shadows")->index() == 0);
    CHECK(m.adjust(-1) && m.find("shadows")->index() == 3);
    m.select("bloom");
    CHECK(m.adjust(-1) && m.find("bloom")->value == 0.0f);
    CHECK(!m.adjust(-1));  
    CHECK(m.activate() && m.find("bloom")->value == 1.0f);  
     
    CHECK(m.select("startBattle") && m.tab == 2);
    m.drain();
    CHECK(m.activate());
    const auto ev = m.drain();
    CHECK(ev.size() == 1 && ev[0].id == "startBattle" && ev[0].action);
    CHECK(m.drain().empty());
    CHECK(!m.select("nope"));
}

void testMenuPresetCustom()
{
    std::printf("menu preset -> Custom\n");
    using namespace warzone::menu;
    Menu m = makeMenu();
    FakeQuality q;
    settingsToItems(m, q);
     
    CHECK(m.find("preset")->index() == 3 && display(*m.find("preset")) == "Ultra");
    CHECK(m.find("targetFps")->index() == 2 && display(*m.find("targetFps")) == "90");
    CHECK(m.find("buildingDetail")->index() == 3 && !m.find("ambientOcclusion")->value && m.find("engineLod")->value == 1.0f);
    CHECK(m.drain().empty());
     
    m.select("effects");
    CHECK(m.adjust(1));
    CHECK(m.find("preset")->index() == 4 && display(*m.find("preset")) == "Custom");
    const auto ev = m.drain();
    CHECK(ev.size() == 1 && ev[0].id == "effects" && !ev[0].action);
     
    m.select("preset");
    m.set("preset", 1.0f);
    CHECK(m.find("preset")->index() == 1);
     
    const FakeQuality back = itemsToSettings(m, q);
    CHECK(back.gameLodScale == 2.5f && back.engineLodBias == 1.0f);
    CHECK(back.effects == 1 && back.shadows == 3 && back.targetFps == 90.0f && !back.adaptive && back.engineLod);
    CHECK(back.preset == FakeQuality::P::Medium);
     
    m.set("look", 4.0f);
    SceneSettings s = itemsToScene(m);
    applyLookPreset(s, s.look);
    sceneToItems(m, s);
    CHECK(display(*m.find("look")) == "Smoky battlefield" && m.find("hazePct")->value == 65.0f && m.find("hazeColor")->index() == 4);
    m.drain();
    m.set("hazePct", 40.0f);
    CHECK(display(*m.find("look")) == "Custom");
     
    m.set("look", 1.0f, false);
    m.set("timeOfDay", 20.0f);
    CHECK(m.find("look")->index() == 1);
}

void testMenuScene()
{
    std::printf("menu scene settings\n");
    using namespace warzone::menu;
     
    Menu m = makeMenu();
    for (int i = 0; i < LOOK_CUSTOM; ++i)
    {
        SceneSettings s;
        applyLookPreset(s, i);
        CHECK(s.look == i);
        sceneToItems(m, s);
        const SceneSettings r = itemsToScene(m);
        CHECK(std::abs(r.hazePct - s.hazePct) < 1e-3f && r.hazeColor == s.hazeColor && r.grade == s.grade);
        CHECK(std::abs(r.sunIntensity - s.sunIntensity) < 1e-3f && std::abs(r.exposure - s.exposure) < 0.051f);
    }
     
    SceneSettings c;
    c.hazePct = 77.0f;
    applyLookPreset(c, LOOK_CUSTOM);
    CHECK(c.look == LOOK_CUSTOM && c.hazePct == 77.0f);
     
    SceneSettings d;
    d.dayMinutes = 24.0f;
    CHECK(std::abs(d.hoursPerSecond() - 1.0f / 60.0f) < 1e-6f);
    d.hazePct = 0.0f;
    const float lo = d.hazeMax();
    d.hazePct = 100.0f;
    CHECK(d.hazeMax() > lo && d.hazeMax() <= 0.92f);
    CHECK(std::abs(SceneSettings{}.hazeDensity() - 0.00012f) < 1e-7f);  
    for (const auto& col : HAZE_COLORS)
        for (float x : col)
            CHECK(x >= 0.0f && x <= 1.0f);
}

void testMenuPersistence()
{
    std::printf("menu persistence\n");
    using namespace warzone::menu;
    SceneSettings s;
    BattleSettings b;
    s.timeOfDay = 17.5f, s.animate = true, s.look = 3, s.hazePct = 41.0f, s.hazeColor = 2, s.grade = 3, s.windDir = 120.0f, s.windSpeed = 9.5f;
    b.size = 1500.0f, b.mix = {10.0f, 20.0f, 30.0f, 40.0f}, b.respawn = 30.0f, b.rate = 75.0f, b.engage = 130.0f, b.matchSeconds = 600.0f;
    const std::string json = toJson(s, b);
    SceneSettings s2;
    BattleSettings b2;
    CHECK(fromJson(json, s2, b2));
    CHECK(s2.timeOfDay == 17.5f && s2.animate && s2.look == 3 && s2.hazePct == 41.0f && s2.hazeColor == 2 && s2.grade == 3);
    CHECK(s2.windDir == 120.0f && s2.windSpeed == 9.5f);
    CHECK(b2.size == 1500.0f && b2.mix[2] == 30.0f && b2.mix[3] == 40.0f && b2.respawn == 30.0f && b2.rate == 75.0f && b2.engage == 130.0f &&
          b2.matchSeconds == 600.0f);
     
    SceneSettings s3;
    BattleSettings b3;
    CHECK(fromJson("{\"size\": 700, \"mystery\": 5}", s3, b3));
    CHECK(b3.size == 700.0f && b3.rate == BattleSettings{}.rate && s3.timeOfDay == SceneSettings{}.timeOfDay);
    CHECK(!fromJson("not json", s3, b3) && !fromJson("{}", s3, b3) && !fromJson("{\"size\": }", s3, b3));
    CHECK(b3.size == 700.0f);
     
    CHECK(fromJson("{\"look\": 99, \"hazeColor\": -4}", s3, b3));
    CHECK(s3.look == LOOK_CUSTOM && s3.hazeColor == 0);
}

void testBattleRamp()
{
    std::printf("battle ramp\n");
    using namespace warzone::menu;
    BattleRamp r;
    CHECK(r.step(1.0f, 10.0f) == 0 && !r.running);
    r.start(100, 0);
    CHECK(r.step(0.5f, 20.0f) == 10);  
    CHECK(r.step(1.0f, 20.0f) == 30);
     
    r.start(100, 40);
    CHECK(r.active() == 40);
    for (int i = 0; i < 100; ++i)
        r.step(0.1f, 50.0f);
    CHECK(r.active() == 100);
     
    r.retarget(150);
    CHECK(r.step(0.1f, 50.0f) == 105);
    r.retarget(20);
    CHECK(r.step(0.1f, 50.0f) == 20);
    r.stop();
    CHECK(r.active() == 0 && !r.running);
     
    r.start(50, 10);
    CHECK(r.step(1.0f, 0.0f) == 10 && r.step(1.0f, -5.0f) == 10);
}

void testSpawnMix()
{
    std::printf("battle mix\n");
    using namespace warzone::menu;
     
    auto w = spawnWeights({10.0f, 30.0f, 0.0f, 0.0f}, {50.0f, 50.0f, 0.0f, 0.0f}, 100);
    CHECK(w[0] == 40.0f && w[1] == 20.0f && w[2] == 0.0f);
    w = spawnWeights({60.0f, 0.0f, 0.0f, 0.0f}, {50.0f, 50.0f, 0.0f, 0.0f}, 100);
    CHECK(w[0] == 0.0f && w[1] == 50.0f);
     
    w = spawnWeights({50.0f, 50.0f, 0.0f, 0.0f}, {50.0f, 50.0f, 0.0f, 0.0f}, 100);
    CHECK(w[0] == 50.0f && w[1] == 50.0f);
    w = spawnWeights({1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, 100);
    CHECK(w[0] + w[1] + w[2] + w[3] == 0.0f);
     
    CHECK(fpsIndex(60.0f) == 1 && fpsIndex(100.0f) == 2 && fpsIndex(500.0f) == 4 && fpsIndex(1.0f) == 0);
}

void testSplitActive()
{
    std::printf("battle split\n");
    using namespace warzone::menu;
    const std::array<float, 4> mix = {40.0f, 30.0f, 18.0f, 12.0f};
    const std::array<int, 4> plenty = {1000, 1000, 1000, 1000};
     
    for (int total : {0, 1, 7, 100, 333, 1000})
    {
        const auto a = splitActive(total, mix, plenty);
        CHECK(a[0] + a[1] + a[2] + a[3] == total);
        CHECK(std::abs(a[0] - total * 0.4f) <= 1.0f && std::abs(a[3] - total * 0.12f) <= 1.0f);
    }
     
    auto a = splitActive(100, mix, {1000, 1000, 1000, 2});
    CHECK(a[3] == 2 && a[0] + a[1] + a[2] + a[3] == 100 && a[0] > a[1] && a[1] > a[2]);
     
    a = splitActive(50, {50.0f, 50.0f, 0.0f, 0.0f}, {10, 10, 100, 100});
    CHECK(a[0] == 10 && a[1] == 10 && a[2] == 0 && a[3] == 0);
    a = splitActive(10, {0.0f, 0.0f, 0.0f, 0.0f}, plenty);
    CHECK(a[0] + a[1] + a[2] + a[3] == 0);
     
    std::array<int, 4> prev{};
    bool monotone = true;
    for (int total = 0; total <= 200; ++total)
    {
        const auto cur = splitActive(total, mix, {70, 1000, 1000, 1000});
        for (size_t t = 0; t < 4; ++t)
            monotone = monotone && cur[t] >= prev[t];
        prev = cur;
    }
    CHECK(monotone);
     
    const auto d = poolDeficit(100, mix, {10, 0, 0, 50});
    CHECK(d[0] == 30.0f && d[1] == 30.0f && d[2] == 18.0f && d[3] == 0.0f);
    CHECK(poolDeficit(100, {0, 0, 0, 0}, {0, 0, 0, 0})[0] == 0.0f);
}
