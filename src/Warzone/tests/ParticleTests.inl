 
 
 
 

namespace ptt
{
    using namespace warzone::pt;
    const auto FLAT = [](float, float) { return 0.0f; };
    const std::array<uint32_t, ATLASES> ALL_TEX = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    float alphaOf(uint32_t rgba) { return static_cast<float>(rgba >> 24) / 255.0f; }
    Spawn puff(float x = 0, float y = 10, float z = 0)
    {
        Spawn s;
        s.style = S_SMOKE, s.pos = {x, y, z}, s.life = 4.0f, s.s0 = 1.0f, s.s1 = 3.0f;
        return s;
    }
    bool same(const System& a, const System& b)
    {
        if (a.parts.size() != b.parts.size())
            return false;
        return a.parts.empty() || std::memcmp(a.parts.data(), b.parts.data(), a.parts.size() * sizeof(Particle)) == 0;
    }
    int countStyle(const System& s, uint16_t style)
    {
        int n = 0;
        for (const auto& p : s.parts)
            n += p.style == style;
        return n;
    }
}

void testParticleAtlasSpec()
{
    std::printf("particles: atlas spec\n");
    using namespace ptt;
    auto infos = ATLAS_DEFAULTS;
    CHECK(parseAtlasLine("smoke grid=16 frames=256 mode=play", infos));
    CHECK(infos[SMOKE].grid == 16 && infos[SMOKE].frames == 256 && infos[SMOKE].mode == PLAY);
    CHECK(parseAtlasLine("flame mode=loop fps=24", infos) && infos[FLAME].mode == LOOP && infos[FLAME].fps == 24.0f);
    CHECK(parseAtlasLine("dust grid=8 frames=999", infos) == false && infos[DUST].frames == 64);  
    CHECK(parseAtlasLine("glow grid=8 frames=100", infos) && infos[GLOW].frames == 64);             
    CHECK(parseAtlasLine("glow grid=4 frames=32", infos) && infos[GLOW].frames == 16);              
    CHECK(!parseAtlasLine("bogus grid=8", infos) && !parseAtlasLine("smoke grid=7", infos) && !parseAtlasLine("smoke size=3", infos));
    CHECK(!parseAtlasLine("# comment", infos) && !parseAtlasLine("", infos));
}

void testParticleCurves()
{
    std::printf("particles: style curves\n");
    using namespace ptt;
    const auto styles = defaultStyles();
    for (int i = 0; i < STYLES; ++i)
    {
        const Lut l = buildLut(styles[i]);
        bool grows = true;
        for (int k = 1; k < LUT; ++k)
            grows = grows && l.grow[k] >= l.grow[k - 1];
        CHECK(grows && l.grow[0] == 0.0f && std::abs(l.grow[LUT - 1] - 1.0f) < 1e-5f);
        CHECK(alphaOf(l.colour[LUT - 1]) == 0.0f || styles[i].fadeOut == 0.0f);  
        CHECK(l.emissive[LUT - 1] < 0.05f || styles[i].ePow == 0.0f);
    }
    const Lut smoke = buildLut(styles[S_SMOKE]);
    CHECK(alphaOf(smoke.colour[0]) == 0.0f && alphaOf(smoke.colour[LUT / 3]) > 0.8f);  
    const Lut fire = buildLut(styles[S_FIREBALL]);
    CHECK(fire.emissive[1] > fire.emissive[LUT / 2] && fire.emissive[LUT / 2] > fire.emissive[LUT - 4]);  
    CHECK(alphaOf(buildLut(styles[S_SPARK]).colour[3]) == 0.0f);                                           
    const Lut hot = buildLut(styles[S_SMOKE_HOT]);
    CHECK(hot.emissive[2] > 0.3f && hot.emissive[LUT / 2] < 0.05f);  
}

void testParticleCapBudget()
{
    std::printf("particles: cap, budget, delays\n");
    using namespace ptt;
    System s;
    s.cap = 100, s.budget = 1000;
    s.begin(0.016f);
    CHECK(s.burst(puff(), 150) == 75 && s.parts.size() == 75 && s.stats.capped == 75);
    Spawn hi = puff(5, 5, 5);
    hi.prio = 2;
    CHECK(s.spawn(hi) && s.parts.size() == 75);  
    bool found = false;
    for (const auto& p : s.parts)
        found = found || (p.pos.x > 4.0f && p.pos.x < 6.0f);
    CHECK(found);
    System b;
    b.budget = 10;
    b.begin(0.016f);
    CHECK(b.burst(puff(), 25) == 8 && b.stats.budgetDropped == 17);
    hi.prio = 2;
    CHECK(b.spawn(hi));  
    b.begin(0.016f);
    CHECK(b.burst(puff(), 5) == 5);  
     
    {
        System e, f;
        e.cap = f.cap = 10000, e.budget = f.budget = 100000;
        e.begin(0.0f), f.begin(0.0f);
        f.burst(puff(), 8500);
        CHECK(e.pressure() == 1.0f && f.pressure() == 0.25f);
        const auto columnOf = [](System& sys, warzone::vfx::Tier tier) { return explosion(sys, {0, 0, 0}, 6.0f, 0.0f, tier, {0.7f, 0.65f, 0.57f}).column; };
        const int farFull = columnOf(f, warzone::vfx::DISTANT), farEmpty = columnOf(e, warzone::vfx::DISTANT);
        CHECK(farFull * 2 < farEmpty);
        System g, h;
        g.cap = h.cap = 10000, g.budget = h.budget = 100000;
        g.begin(0.0f), h.begin(0.0f);
        h.burst(puff(), 8500);
        const int nearFull = columnOf(h, warzone::vfx::NEAR), nearEmpty = columnOf(g, warzone::vfx::NEAR);
        CHECK(std::abs(nearFull - nearEmpty) <= 1);
    }
     
    System d;
    Spawn late = puff();
    late.delay = 0.5f;
    d.begin(0.0f);
    d.spawn(late);
    d.prepare();
    d.pack(0, 1, {0, 0, -50}, ALL_TEX);
    CHECK(d.keys[0] == 0xFFFF);
    for (int i = 0; i < 40; ++i)
        d.step(0.016f, FLAT);
    d.prepare();
    d.pack(0, 1, {0, 0, -50}, ALL_TEX);
    CHECK(d.keys[0] != 0xFFFF && d.parts[0].age > 0.0f);
}

void testParticleMotion()
{
    std::printf("particles: motion\n");
    using namespace ptt;
    System s;
    s.wind.speed = 6.0f, s.wind.gust = 0.0f, s.wind.dirX = 1.0f, s.wind.dirZ = 0.0f;
    Spawn up = puff();
    up.rise = 4.0f, up.life = 10.0f;
    Spawn windy = puff();
    windy.windK = 0.8f, windy.life = 10.0f;
    Spawn rock = puff(0, 20, 0);
    rock.style = S_DEBRIS, rock.flags = P_BALLISTIC | P_BOUNCE, rock.gravity = 9.81f, rock.drag = 0.0f, rock.vel = {3, 0, 0}, rock.life = 10.0f;
    Spawn dust = puff(0, 0.1f, 0);
    dust.style = S_DUST, dust.flags = P_GROUNDED, dust.vel = {0, -5, 0}, dust.life = 10.0f;
    Spawn spark = puff(0, 3, 0);
    spark.style = S_SPARK, spark.flags = P_BALLISTIC | P_DIE_GROUND, spark.gravity = 9.81f, spark.life = 10.0f;
    s.begin(0.0f);
    s.spawn(up), s.spawn(windy), s.spawn(rock), s.spawn(dust), s.spawn(spark);
    const float y0 = s.parts[0].pos.y;
    bool aboveGround = true, sparkGone = false;
    for (int i = 0; i < 120; ++i)
    {
        s.step(1.0f / 60.0f, FLAT);
        for (const auto& p : s.parts)
            if (p.style == S_DEBRIS || p.style == S_DUST)
                aboveGround = aboveGround && p.pos.y >= -1e-4f;
        sparkGone = sparkGone || countStyle(s, S_SPARK) == 0;
    }
    CHECK(s.parts[0].pos.y > y0 + 3.0f);                     
    CHECK(s.parts[1].pos.x > 3.0f && std::abs(s.parts[1].pos.z) < 1.0f);  
    CHECK(aboveGround);                                       
    CHECK(sparkGone);                                         
    const auto* rockP = &s.parts[0];
    for (const auto& p : s.parts)
        if (p.style == S_DEBRIS)
            rockP = &p;
    CHECK(rockP->pos.x > 3.0f && rockP->pos.y < 1.0f);  
     
    System d;
    Spawn thrown = puff();
    thrown.vel = {20, 0, 0}, thrown.drag = 2.0f;
    d.begin(0.0f);
    d.spawn(thrown);
    for (int i = 0; i < 60; ++i)
        d.step(1.0f / 60.0f, FLAT);
    CHECK(std::abs(d.parts[0].vel.x) < 3.0f && d.parts[0].pos.x > 5.0f);
     
    System e;
    Spawn brief = puff();
    brief.life = 0.1f;
    e.begin(0.0f);
    e.burst(brief, 10);
    for (int i = 0; i < 10; ++i)
        e.step(0.02f, FLAT);
    CHECK(e.parts.empty() && e.stats.live == 0);
}

void testParticleDeterminism()
{
    std::printf("particles: determinism and ranges\n");
    using namespace ptt;
    auto run = [](System& s, bool split)
    {
        for (int f = 0; f < 90; ++f)
        {
            s.begin(1.0f / 60.0f);
            if (f % 15 == 0)
                explosion(s, {f * 3.0f, 0, 0}, 6.0f, 0.0f, warzone::vfx::NEAR, {0.7f, 0.6f, 0.5f});
            const uint32_t n = static_cast<uint32_t>(s.parts.size());
            if (split)
            {
                s.simulate(0, n / 3, 1.0f / 60.0f, FLAT);
                s.simulate(n / 3, n, 1.0f / 60.0f, FLAT);
            }
            else
                s.simulate(0, n, 1.0f / 60.0f, FLAT);
            s.finish(1.0f / 60.0f);
        }
    };
    System a, b, c;
    run(a, false);
    run(b, false);
    run(c, true);
    CHECK(!a.parts.empty() && same(a, b));  
    CHECK(same(a, c));                      
}

void testParticleTrails()
{
    std::printf("particles: trails\n");
    using namespace ptt;
    System s;
    TrailState t;
    Spawn proto = trailPuff(true, warzone::vfx::NEAR);
    s.begin(0.0f);
    const int a = trail(s, t, {0, 0, 0}, {10, 0, 0}, 2.0f, proto);
    const int b = trail(s, t, {10, 0, 0}, {13, 0, 0}, 2.0f, proto);
    const int c = trail(s, t, {13, 0, 0}, {14, 0, 0}, 2.0f, proto);
    CHECK(a == 5 && a + b + c == 7);  
    bool even = true;
    for (size_t i = 1; i < s.parts.size(); ++i)
        even = even && std::abs(s.parts[i].pos.x - s.parts[i - 1].pos.x - 2.0f) < 0.5f;  
    CHECK(even);
     
    System d;
    Spawn piece = puff(0, 50, 0);
    piece.style = S_DEBRIS, piece.flags = P_BALLISTIC, piece.vel = {30, 0, 0}, piece.drag = 0.0f, piece.trailStyle = S_SMOKE, piece.life = 5.0f;
    d.begin(0.0f);
    d.spawn(piece);
    for (int i = 0; i < 60; ++i)
        d.step(1.0f / 60.0f, FLAT);
    const int puffs = countStyle(d, S_SMOKE);
    CHECK(puffs >= 15 && puffs <= 20);  
}

void testParticleEmitters()
{
    std::printf("particles: emitters\n");
    using namespace ptt;
    System s;
    const EmitterId id = s.emit(puff(), 10.0f, 10.0f, 2.0f);
    for (int i = 0; i < 150; ++i)
        s.step(1.0f / 60.0f, FLAT);
    CHECK(s.stats.spawnedTotal >= 19 && s.stats.spawnedTotal <= 21);  
    CHECK(s.emitter(id) == nullptr);                                   
    const EmitterId k = s.emit(puff(), 100.0f, 100.0f, 0.0f);          
    s.step(0.1f, FLAT);
    s.stop(k);
    const uint64_t before = s.stats.spawnedTotal;
    s.step(0.1f, FLAT);
    CHECK(s.stats.spawnedTotal == before && s.emitter(k) == nullptr);
    const EmitterId reuse = s.emit(puff(), 1.0f, 1.0f, 1.0f);
    CHECK(reuse.index == k.index && s.emitter(k) == nullptr && s.emitter(reuse) != nullptr);  
}

void testParticlePacking()
{
    std::printf("particles: packing\n");
    using namespace ptt;
    System s;
    s.atlas[FLAME].mode = LOOP;
    Spawn smoke = puff(0, 0, -10);
    smoke.life = 1.0f;
    Spawn flame = puff(0, 0, -20);
    flame.style = S_FLAME, flame.life = 100.0f;
    Spawn spark = puff(0, 0, -30);
    spark.style = S_SPARK, spark.flags = P_BALLISTIC, spark.vel = {0, 0, 40}, spark.drag = 0.0f, spark.life = 10.0f, spark.s0 = spark.s1 = 0.1f;
    Spawn flash = puff(0, 0, -40);
    flash.style = S_FLASH, flash.life = 10.0f;
    s.begin(0.0f);
    s.spawn(smoke), s.spawn(flame), s.spawn(spark), s.spawn(flash);
    std::vector<GpuParticle> out(8);
    uint32_t lastSmokeFrame = 0, flashFrame = 999;
    bool smokeMonotonic = true, loopWrapped = false, flashFixed = true;
    for (int i = 0; i < 220; ++i)
    {
        s.step(0.019f, FLAT);
        s.prepare();
        s.pack(0, static_cast<uint32_t>(s.parts.size()), {0, 0, 0}, ALL_TEX);
        for (uint32_t k = 0; k < s.parts.size(); ++k)
        {
            if (s.keys[k] == 0xFFFF)
                continue;
            const GpuParticle& g = s.scratch[k];
            const uint32_t f0 = g.frame & 255u, f1 = (g.frame >> 8) & 255u;
            if (s.parts[k].style == S_SMOKE)
            {
                smokeMonotonic = smokeMonotonic && f0 >= lastSmokeFrame && f1 >= f0;
                lastSmokeFrame = f0;
            }
            if (s.parts[k].style == S_FLAME)
                loopWrapped = loopWrapped || (f0 == 63 && f1 == 0);
            if (s.parts[k].style == S_FLASH)
            {
                flashFixed = flashFixed && (flashFrame == 999 || flashFrame == f0) && f0 == f1;
                flashFrame = f0;
            }
        }
    }
    CHECK(smokeMonotonic && lastSmokeFrame > 40);  
    CHECK(loopWrapped);                             
    CHECK(flashFixed);                              
    s.prepare();
    s.pack(0, static_cast<uint32_t>(s.parts.size()), {0, 0, 0}, ALL_TEX);
    const uint32_t n = s.gather(out.data(), 8);
    bool streak = false, flags = true;
    for (uint32_t k = 0; k < n; ++k)
    {
        const uint32_t f = out[k].frame >> 24;
        if (f & GPU_STRETCH)
        {
            const float l = std::sqrt(out[k].c0 * out[k].c0 + out[k].c1 * out[k].c1 + out[k].c2 * out[k].c2);
            streak = out[k].c2 < 0.0f && std::abs(l - 40.0f * s.styles[S_SPARK].stretch) < 1e-3f;  
        }
        flags = flags && ((f >> 4) & 7u) == 3u;  
    }
    CHECK(streak && flags);
     
    System t;
    t.begin(0.0f);
    tracer(t, {0, 0, 0}, {-12, 0, 0}, {1, 0, 0}, 18.0f, 0.4f, 0.1f, 1.0f / 60.0f, warzone::vfx::NEAR);
    t.prepare();
    t.pack(0, static_cast<uint32_t>(t.parts.size()), {0, 0, 20}, ALL_TEX);
    CHECK(t.parts.size() == 3 && t.parts[0].style == S_TRACER && std::abs(t.scratch[0].c0 + 18.0f) < 1e-3f);
    CHECK(t.parts[1].style == S_TRAIL_GLOW && std::abs(t.scratch[1].c0 + 12.0f) < 1e-3f);
    t.step(1.0f / 60.0f, FLAT);
    CHECK(countStyle(t, S_TRACER) == 1 && t.parts[0].pos.x == 0.0f);  
    t.step(1.0f / 60.0f, FLAT);
    CHECK(countStyle(t, S_TRACER) == 0 && countStyle(t, S_TRAIL_GLOW) == 1);  
}

void testParticleGather()
{
    std::printf("particles: back-to-front gather\n");
    using namespace ptt;
    System s;
    s.begin(0.0f);
    for (int i = 0; i < 50; ++i)
    {
        Spawn p = puff(static_cast<float>((i * 37) % 50) * 10.0f, 0, 0);
        p.style = (i % 5 == 0) ? S_FIREBALL : S_SMOKE;
        s.spawn(p);
    }
    s.step(0.2f, FLAT);  
    s.prepare();
    s.pack(0, 50, {-5, 0, 0}, ALL_TEX);
    std::vector<GpuParticle> out(64);
    const uint32_t n = s.gather(out.data(), 64);
    bool backToFront = n == 50;
    for (uint32_t k = 1; k < n; ++k)
        backToFront = backToFront && out[k].x <= out[k - 1].x + 0.3f;  
    CHECK(backToFront);
     
    const uint32_t m = s.gather(out.data(), 10);
    CHECK(m == 10 && out[0].x < 110.0f && out[9].x < 10.0f);
     
    auto tex = ALL_TEX;
    tex[FIRE] = 0xFFFFFFFFu;
    s.prepare();
    s.pack(0, 50, {-5, 0, 0}, tex);
    CHECK(s.gather(out.data(), 64) == 40);
}

void testParticleQuality()
{
    std::printf("particles: quality\n");
    using namespace ptt;
    int counts[4];
    float size[4];
    for (int q = 0; q < 4; ++q)
    {
        System s;
        s.setQuality(q);
        s.begin(0.0f);
        counts[q] = explosion(s, {0, 0, 0}, 6.0f, 0.0f, warzone::vfx::NEAR, {0.7f, 0.6f, 0.5f}).total();
        float total = 0;
        for (const auto& p : s.parts)
            total += p.s1;
        size[q] = total / static_cast<float>(s.parts.size());
        CHECK(countStyle(s, S_FIREBALL) >= 2 && countStyle(s, S_GLOW) == 1);  
        CHECK(s.cap == QUALITY[q].cap);
    }
    CHECK(counts[0] < counts[1] && counts[1] < counts[2] && counts[2] < counts[3]);
    CHECK(size[0] > size[3]);  
}

void testParticleRecipes()
{
    {
        using namespace ptt;
        System s;
        s.setQuality(3);
        s.addBattleSmoke({0,0,0}, 10);
        s.begin(8);
        CHECK(countStyle(s, S_BATTLE_HAZE) == 1);
        CHECK(s.battleHaze[8 * 16 + 8].mass < 10);
        s.setQuality(0);
        CHECK(countStyle(s, S_BATTLE_HAZE) == 0);
        s.clear();
        CHECK(s.battleHaze[8 * 16 + 8].mass == 0);
        s.setQuality(0);
        s.addBattleSmoke({0,0,0}, 10);
        s.begin(8);
        CHECK(countStyle(s, S_BATTLE_HAZE) == 0);
        s.clear();
        s.setQuality(1);
        s.viewer = {1280,0,1280};
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x)
                s.addBattleSmoke({-3840.0f + x*512,0,-3840.0f + z*512}, 100);
        CHECK(s.battleHaze[255].mass == 30);
        s.begin(8);
        CHECK(countStyle(s, S_BATTLE_HAZE) == 16);
        CHECK(warzone::len(s.parts[0].pos - s.viewer) < 200);
    }
    {
        using namespace ptt;
        System s;
        Spawn p = puff(0, 10, 0);
        p.s0 = p.s1 = 20;
        s.spawn(p);
        p.pos.y = 30;
        s.burst(p, 8);
        for (auto& part : s.parts) part.age = 0.5f;
        s.sunDirection = {0,1,0};
        s.prepare();
        s.pack(0, 1, {0,10,100}, ALL_TEX);
        const float shadow = s.scratch[0].c1;
        s.sunDirection = {1,0,0};
        s.pack(0, 1, {0,10,100}, ALL_TEX);
        CHECK(shadow < s.scratch[0].c1);
        CHECK(shadow > 0 && shadow < 1);
    }
    {
        using namespace ptt;
        System s;
        muzzle(s, {0,10,0}, {1,0,0}, 2, 10, warzone::vfx::NEAR, {1,1,1});
        CHECK(s.parts[0].style == S_MUZZLE);
        s.prepare();
        s.pack(0, 1, {100,10,0}, ALL_TEX);
        CHECK((s.scratch[0].frame & 255u) < 32u);
        s.pack(0, 1, {0,10,100}, ALL_TEX);
        CHECK((s.scratch[0].frame & 255u) >= 32u);
        s.clear();
        explosion(s, {0,1,0}, 6, 1, warzone::vfx::NEAR, {1,1,1});
        CHECK(countStyle(s, S_SHOCK) == 1);
        bool plane = false, varied = false;
        int frame = -1;
        for (const auto& p : s.parts)
        {
            if (p.style == S_SHOCK) plane = (p.flags & P_PLANE) != 0;
            if (p.style == S_FIREBALL)
            {
                varied |= frame >= 0 && frame != p.frame;
                frame = p.frame;
                CHECK(p.s0 < 6 * 0.72f);
            }
        }
        CHECK(plane && varied);
        CHECK(countStyle(s, S_FIREBALL) > 20);
        s.clear();
        explosion(s, {0,100,0}, 6, 100, warzone::vfx::NEAR, {1,1,1});
        CHECK(countStyle(s, S_SHOCK) == 0);
    }

    std::printf("particles: recipes by tier\n");
    using namespace ptt;
    using warzone::vfx::Tier;
    const V3 dust{0.7f, 0.6f, 0.5f};
    int prev = 1 << 30;
    for (int t = 0; t < warzone::vfx::TIERS; ++t)
    {
        System s;
        s.begin(0.0f);
        const ExplosionParts n = explosion(s, {0, 0, 0}, 6.0f, 0.0f, static_cast<Tier>(t), dust);
        CHECK(n.fire >= 2 && n.column >= 1 && n.flash >= 1);       
        CHECK(n.total() <= prev && n.total() < 260);               
        CHECK((t <= 1) == (n.debris > 0 && n.sparks > 0));         
        CHECK((t <= 1) == (n.ring > 0));
        prev = n.total();
    }
    {
        System s;
        s.begin(0.0f);
        const ExplosionParts air = explosion(s, {0, 60, 0}, 6.0f, 60.0f, warzone::vfx::NEAR, dust);
        CHECK(air.skirt == 0 && air.ring == 0 && air.fire >= 2);  
         
        float columnLife = 0, fireLife = 100, maxDelay = 0;
        for (const auto& p : s.parts)
        {
            if (p.style == S_SMOKE)
                columnLife = std::max(columnLife, p.life), maxDelay = std::max(maxDelay, -p.age);
            if (p.style == S_FIREBALL)
                fireLife = std::min(fireLife, p.life);
        }
        CHECK(columnLife >= 10.0f && columnLife <= 40.0f && maxDelay > 0.5f && fireLife < 2.5f);
    }
    {
         
        System s;
        s.begin(0.0f);
        const int cannon = muzzle(s, {0, 2, 0}, {1, 0, 0}, 2, 2.0f, warzone::vfx::NEAR, dust);
        const int mg = muzzle(s, {0, 2, 0}, {1, 0, 0}, 0, 2.0f, warzone::vfx::NEAR, dust, 1);
        CHECK(cannon > mg * 4 && countStyle(s, S_SMOKE_LIGHT) >= 5 && countStyle(s, S_DUST) >= 1);
    }
    {
         
        System s;
        s.begin(0.0f);
        impact(s, {0, 0, 0}, {0, 1, 0}, S_ARMOUR, 1.0f, warzone::vfx::NEAR, dust);
        CHECK(countStyle(s, S_SPARK) >= 6 && countStyle(s, S_DEBRIS) == 0);
        System w;
        w.begin(0.0f);
        impact(w, {0, 0, 0}, {1, 0, 0}, S_WALL, 1.0f, warzone::vfx::NEAR, dust);
        CHECK(countStyle(w, S_DEBRIS) >= 1 && countStyle(w, S_DUST) >= 1);
    }
    {
         
        System s;
        float acc[3] = {};
        int early = 0, late = 0;
        for (int i = 0; i < 60; ++i)
            early += wreck(s, {0, 0, 0}, 1.0f, 70.0f, 70.0f, warzone::vfx::NEAR, 1.0f / 60.0f, acc);
        uint32_t freshTint = 0;
        for (const auto& p : s.parts)
            if (p.style == S_PLUME)
                freshTint = p.tint;
        System l;
        float acc2[3] = {};
        for (int i = 0; i < 60; ++i)
            late += wreck(l, {0, 0, 0}, 1.0f, 5.0f, 70.0f, warzone::vfx::NEAR, 1.0f / 60.0f, acc2);
        CHECK(early > late * 2 && countStyle(s, S_EMBER) >= 1);
        uint32_t lateSmoke = 0;
        for (const auto& p : l.parts)
            if (p.style == S_PLUME)
                lateSmoke = p.tint;
        CHECK(countStyle(s, S_PLUME) >= 1 && (lateSmoke & 255u) > (freshTint & 255u) + 40u);  
        System far;
        float acc3[3] = {};
        int distant = 0;
        for (int i = 0; i < 60; ++i)
            distant += wreck(far, {0, 0, 0}, 1.0f, 70.0f, 70.0f, warzone::vfx::DISTANT, 1.0f / 60.0f, acc3);
        CHECK(distant * 4 < early && countStyle(far, S_EMBER) == 0);
    }
    {
         
        System s;
        float acc = 0;
        int n = 0;
        for (int i = 0; i < 60; ++i)
            n += rotorWash(s, {0, 0, 0}, 7.0f, 1.0f, warzone::vfx::NEAR, 1.0f / 60.0f, acc, dust);
        CHECK(n >= 30 && rotorWash(s, {0, 0, 0}, 7.0f, 0.0f, warzone::vfx::NEAR, 1.0f, acc, dust) == 0);
        bool outward = true;
        for (const auto& p : s.parts)
            outward = outward && (p.pos.x * p.vel.x + p.pos.z * p.vel.z) > 0.0f;
        CHECK(outward);
    }
    {
         
        System s;
        s.begin(0.0f);
        card(s, warzone::vfx::SPARK, {0, 1, 0}, 2.0f, 3.0f, 0.4f, {0, 2, 0}, dust, 1.0f);
        card(s, warzone::vfx::PUFF, {0, 1, 0}, 2.0f, 3.0f, 2.0f, {0, 1, 0}, dust, 1.0f);
        CHECK(countStyle(s, S_SPARK) >= 4 && countStyle(s, S_SMOKE) >= 1);
    }
}

 
void perfParticles()
{
    using namespace ptt;
    for (uint32_t target : {20000u, 40000u})
    {
        System s;
        s.cap = target;
        s.budget = target;
        s.begin(0.0f);
        Rng r(7);
        while (s.parts.size() < target)
        {
            Spawn p = puff(r.range(-500, 500), r.range(0, 80), r.range(-500, 500));
            p.style = static_cast<uint16_t>(r.irange(0, STYLES - 1));
            p.life = 1000.0f, p.windK = 0.4f, p.rise = 1.0f;
            p.flags = p.style == S_DUST ? P_GROUNDED : 0;
            s.spawn(p);
        }
        std::vector<GpuParticle> out(target);
        double sim = 0, pack = 0, gather = 0;
        const int frames = 60;
        for (int f = 0; f < frames; ++f)
        {
            const auto t0 = std::chrono::steady_clock::now();
            s.step(1.0f / 60.0f, FLAT);
            const auto t1 = std::chrono::steady_clock::now();
            s.prepare();
            s.pack(0, static_cast<uint32_t>(s.parts.size()), {0, 50, 0}, ALL_TEX);
            const auto t2 = std::chrono::steady_clock::now();
            s.gather(out.data(), target);
            const auto t3 = std::chrono::steady_clock::now();
            sim += std::chrono::duration<double, std::milli>(t1 - t0).count();
            pack += std::chrono::duration<double, std::milli>(t2 - t1).count();
            gather += std::chrono::duration<double, std::milli>(t3 - t2).count();
        }
        std::printf("perf particles %u live (one thread): sim %.3f ms, pack %.3f ms, sort+gather %.3f ms a frame\n", target, sim / frames,
                    pack / frames, gather / frames);
    }
}


void testNukeParticles()
{
    using namespace ptt;
    const int before = g_checks;
    for (int q = 0; q < 4; ++q) {
        System a, b; a.setQuality(q); b.setQuality(q);
        CHECK(a.nuke({0, 0, 0}) && b.nuke({0, 0, 0}));
        CHECK(countStyle(a, S_NUKE_FLASH) == 1);
        const auto initial = a.parts.size();
        for (int k = 0; k < 240; ++k) { a.step(0.025f, FLAT); b.step(0.025f, FLAT); }
        CHECK(same(a, b));
        CHECK(a.parts.size() > initial && a.nukeLive <= a.cap / 8);
        CHECK(std::none_of(a.nukes.begin(), a.nukes.end(), [](const auto& e) { return e.alive; }));
        CHECK(countStyle(a, S_NUKE_FLASH) == 0 && countStyle(a, S_NUKE_FIRE) == 0);
        float top = 0;
        for (const auto& p : a.parts) {
            if (p.style == S_NUKE_CLOUD) top = std::max(top, p.pos.y);
        }
        CHECK(top > 180);
        a.step(34, FLAT);
        CHECK(countStyle(a, S_NUKE_CLOUD) > 0);
        a.step(22, FLAT);
        CHECK(a.nukeLive == 0);
        a.clear();
        Spawn battle = puff(); battle.life = 1000;
        a.budget = a.cap; a.burst(battle, int(a.cap));
        const int battleBefore = countStyle(a, S_SMOKE);
        for (int k = 0; k < 12; ++k) { a.nuke({0,0,0}); a.step(0.5f, FLAT); }
        CHECK(a.parts.size() <= a.cap && a.nukeLive <= a.cap / 8);
        CHECK(countStyle(a, S_SMOKE) >= battleBefore - int(a.cap / 8));
        Spawn important = battle; important.prio = 2;
        const auto protectedCount = a.nukeLive;
        a.burst(important, 100);
        CHECK(a.nukeLive == protectedCount && countStyle(a, S_NUKE_CLOUD) > 0);
        a.setQuality(0);
        CHECK(a.parts.size() <= a.cap && a.nukeLive <= a.cap / 8);
        a.clear(); CHECK(a.nukeLive == 0 && a.nuke({0,0,0}));
    }
    System timingA, timingB;
    timingA.nuke({0,0,0}); timingB.nuke({0,0,0});
    timingA.feedNukes(6);
    for (int i = 0; i < 48; ++i) timingB.feedNukes(0.125f);
    CHECK(same(timingA, timingB));
    CHECK(timingA.nukes[0].tick == 24);
    System ring; ring.nuke({0,0,0}); ring.step(4, FLAT);
    float reach = 0;
    for (const auto& p : ring.parts) if (p.style == S_NUKE_DUST) reach = std::max(reach, std::hypot(p.pos.x, p.pos.z));
    CHECK(reach > 420);
    const auto cloud = buildLut(defaultStyles()[S_NUKE_CLOUD]);
    CHECK((cloud.colour[LUT/2] & 255) < (cloud.colour[1] & 255));
    CHECK(cloud.emissive[LUT/4] < 0.01f);
    std::printf("nuke: 5 scenarios, %d checks\n", g_checks - before);
}
