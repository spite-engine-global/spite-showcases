void testAudioDefaults()
{
    using namespace warzone::audio;
    const Settings s;
    for (float distance : {0.0f, 40.0f, 80.0f, 100.0f, 4000.0f, 4001.0f, 8000.0f})
    {
        CHECK(mix(s, Category::Explosions, distance, 80) == warzone::soundGain(distance, 80));
        CHECK(mix(s, Category::Vehicles, distance, 80, 0.3f) == warzone::soundGain(distance, 80) * 0.3f);
        CHECK(delay(s, distance) == warzone::soundDelay(distance));
    }
    CHECK(mix(s, Category::Weapons, 0, 1) == 1);
}
void testAudioScaling()
{
    using namespace warzone::audio;
    Settings s;
    s.master = 50;
    s.weapons = 40; s.explosions = 60; s.vehicles = 80; s.impacts = 20;
    CHECK(std::abs(mix(s, Category::Weapons, 100, 50) - 0.1f) < 1e-6f);
    CHECK(std::abs(mix(s, Category::Explosions, 100, 50) - 0.15f) < 1e-6f);
    CHECK(std::abs(mix(s, Category::Vehicles, 100, 50) - 0.2f) < 1e-6f);
    CHECK(std::abs(mix(s, Category::Impacts, 100, 50) - 0.05f) < 1e-6f);
    s.mute = 1;
    CHECK(mix(s, Category::Vehicles, 0, 50) == 0);
    s.mute = 0; s.master = 0;
    CHECK(mix(s, Category::Explosions, 0, 50) == 0);
}
void testAudioBlastMinimum()
{
    using namespace warzone::audio;
    Settings s;
    s.blastMinimum = 30;
    CHECK(mix(s, Category::Explosions, 10000, 50, 1, 11) == 0.3f);
    CHECK(mix(s, Category::Explosions, 10000, 50, 1, 10) == 0);
    CHECK(mix(s, Category::Weapons, 10000, 50, 1, 20) == 0);
    CHECK(mix(s, Category::Explosions, 0, 50, 1, 20) == 1);
    s.master = 50; s.explosions = 50;
    CHECK(mix(s, Category::Explosions, 10000, 50, 1, 20) == 0.075f);
    s.explosions = 0;
    CHECK(mix(s, Category::Explosions, 10000, 50, 1, 20) == 0);
    s.explosions = 100; s.mute = 1;
    CHECK(mix(s, Category::Explosions, 10000, 50, 1, 20) == 0);
}
void testAudioFalloff()
{
    using namespace warzone::audio;
    Settings s;
    s.falloff = 2;
    CHECK(mix(s, Category::Weapons, 100, 50) == 0.25f);
    s.falloff = 0.5f;
    CHECK(mix(s, Category::Weapons, 200, 50) == 0.5f);
    s.falloff = 0;
    CHECK(mix(s, Category::Vehicles, 10000, 50) == 1);
}
void testAudioDelay()
{
    using namespace warzone::audio;
    Settings s;
    CHECK(delay(s, 343) == 1);
    CHECK(delay(s, -4) == 0);
    s.delay = 0;
    CHECK(delay(s, 10000) == 0);
}
void testAudioClamping()
{
    using namespace warzone::audio;
    Settings s;
    s.master = 400; s.weapons = -50;
    CHECK(mix(s, Category::Weapons, 0, 1) == 0);
    CHECK(mix(s, Category::Explosions, 0, 1, 4) == 1);
    CHECK(mix(s, Category::Explosions, 0, 1, -4) == 0);
    s.falloff = -10; s.blastMinimum = 500; s.blastThreshold = -1;
    auto clamped = sanitize(s);
    CHECK(clamped.falloff == 0 && clamped.blastMinimum == 100 && clamped.blastThreshold == 1);
}
void testAudioPersistence()
{
    using namespace warzone::audio;
    Settings s;
    s.master = 43; s.mute = 1; s.weapons = 25; s.explosions = 30; s.vehicles = 35; s.impacts = 40;
    s.blastMinimum = 15; s.blastThreshold = 22; s.falloff = 0.65f; s.delay = 0;
    Settings restored;
    CHECK(fromJson(toJson(s), restored));
    for (const auto& f : FIELDS) CHECK(restored.*(f.member) == s.*(f.member));
    CHECK(!fromJson("{\"audioMaster\":nan}", restored));
    CHECK(!fromJson("{\"audioMaster\":42,}", restored));
    CHECK(!fromJson("{\"audioMaster\":42", restored));
    CHECK(restored.master == 43);
    CHECK(fromJson("{\"audioMaster\":500}", restored) && restored.master == 100);
}
void testAudioMenu()
{
    using namespace warzone::menu;
    auto m = makeMenu();
    CHECK(m.select("audioMaster") && m.page().id == "audio");
    m.set("audioMaster", 48);
    CHECK(itemsToAudio(m).master == 48);
    const warzone::audio::Settings defaults;
    audioToItems(m, defaults);
    CHECK(itemsToAudio(m).master == 100);
    CHECK(m.select("audioApply") && m.activate());
    CHECK(m.drain().back().id == "audioApply");
    CHECK(m.select("audioRevert") && m.activate());
}
