 
 
 

void testDecalPool()
{
    std::printf("decal pool\n");
    using namespace warzone::decal;
    Pool p;
    p.init(3);
    CHECK(p.acquire(0.0f, 10.0f, 5.0f, 1) == 0 && p.acquire(1.0f, 10.0f, 5.0f, 2) == 1 && p.acquire(2.0f, 10.0f, 5.0f, 3) == 2);
    CHECK(p.live(2.5f) == 3 && p.recycled == 0);
     
    CHECK(p.acquire(3.0f, 10.0f, 5.0f, 4) == 0 && p.recycled == 1);
    CHECK(p.acquire(4.0f, 10.0f, 5.0f, 5) == 1 && p.recycled == 2);
    CHECK(p.slots[0].frame == 4 && p.slots[1].frame == 5 && p.slots[2].frame == 3);
     
    Pool q;
    q.init(2);
    q.acquire(0.0f, 5.0f, 1.0f, 0);
    q.acquire(1.0f, 50.0f, 1.0f, 0);
    CHECK(q.acquire(6.0f, 5.0f, 1.0f, 0) == 0 && q.recycled == 0);
     
    Pool f;
    f.init(2);
    f.acquire(10.0f, 100.0f, 4.0f, 0);
    CHECK(f.alpha(0, 10.0f) == 1.0f && f.alpha(0, 84.0f) == 1.0f);
    CHECK(std::abs(f.alpha(0, 97.5f) - 0.5f) < 1e-4f);
    CHECK(f.alpha(0, 110.0f) == 0.0f && f.alpha(0, 500.0f) == 0.0f && f.alpha(1, 10.0f) == 0.0f);
    CHECK(f.live(50.0f) == 1 && f.live(120.0f) == 0);
    Pool none;
    CHECK(none.acquire(0.0f, 1.0f, 1.0f, 0) == -1);
     
    for (int k = 0; k < KINDS; ++k)
        for (uint32_t seed = 0; seed < 200; ++seed)
        {
            const int fr = frameFor(static_cast<warzone::decal::Kind>(k), seed);
            CHECK(fr >= FIRST[k] && fr < FIRST[k] + COUNT[k] && fr == frameFor(static_cast<warzone::decal::Kind>(k), seed));
        }
    bool seen[16] = {};
    for (uint32_t seed = 0; seed < 400; ++seed)
        seen[frameFor(SCORCH, seed)] = true;
    CHECK(seen[0] && seen[1] && seen[2] && seen[3] && seen[4]);  
}

void testDecalSizing()
{
    std::printf("decal sizing\n");
    using namespace warzone::decal;
    CHECK(overlapsSidewalk(-1, 10, 0, 0, 0, 20, 20));
    CHECK(!overlapsSidewalk(-4, 10, 0, 0, 0, 20, 20));
    CHECK(!overlapsSidewalk(10, 10, 0, 0, 0, 20, 20));
    CHECK(overlapsSidewalk(10, 10, 12, 0, 0, 20, 20));
     
    CHECK(craterSize(1.5f) == 0.0f && craterSize(2.9f) == 0.0f);
    const float shell = craterSize(4.5f), cook = craterSize(7.0f), bomb = craterSize(9.0f);
    CHECK(shell >= 3.2f && shell < cook && cook < bomb && bomb <= 14.0f);
    CHECK(craterSize(100.0f) == 14.0f);
    CHECK(craterSize(bomb) <= 14.0f && bomb < scorchSize(9.0f));  
    CHECK(shell < scorchSize(4.5f));
    CHECK(scorchSize(0.1f) == 1.5f && scorchSize(1000.0f) == 26.0f);
    CHECK(sootSize(0.0f, 0.0f) < sootSize(0.0f, 1.0f) && sootSize(0.0f, 1.0f) < 1.6f);
    CHECK(sootSize(4.5f, 0.5f) > sootSize(0.0f, 1.0f) && sootSize(100.0f, 0.5f) == 9.0f);
    CHECK(bulletMarkSize(0.0f) < bulletMarkSize(1.0f) && bulletMarkSize(5.0f) == bulletMarkSize(1.0f));
     
    CHECK(wreckPatchSize(4.2f, 1.7f) > 2.0f * 4.2f && wreckPatchSize(1.0f, 1.0f) == 4.5f);
    CHECK(wreckPatchSize(50.0f, 50.0f) == 24.0f);
     
    CHECK(!wantsBulletMark(300.0f, 0.0f) && wantsBulletMark(10.0f, 0.2f) && !wantsBulletMark(10.0f, 0.5f) && !wantsBulletMark(250.0f, 0.3f));
     
    CHECK(groundLift(4.0f, 0.0f, 1) > groundLift(4.0f, 0.0f, 0) && groundLift(4.0f, 0.5f, 0) > groundLift(4.0f, 0.0f, 0));
    CHECK(groundLift(30.0f, 5.0f, 7) <= GROUND_LIFT + 0.0035f * 7 + 0.5f + 1e-5f && groundLift(4.0f, 0.0f, 0) > 0.082f);  
}

void testDecalBasis()
{
    std::printf("decal basis\n");
    using namespace warzone;
    using namespace warzone::decal;
    auto check = [](const V3& n, float yaw, bool wall)
    {
        const Basis b = surfaceBasis(n, yaw);
        const V3 nn = norm(n);
        CHECK(std::abs(len(b.x) - 1) < 1e-4f && std::abs(len(b.y) - 1) < 1e-4f && std::abs(len(b.z) - 1) < 1e-4f);
        CHECK(std::abs(dot(b.x, b.y)) < 1e-4f && std::abs(dot(b.x, b.z)) < 1e-4f && std::abs(dot(b.y, b.z)) < 1e-4f);
        CHECK(dot(b.z, nn) > 0.9999f);
        const V3 xy{b.x.y * b.y.z - b.x.z * b.y.y, b.x.z * b.y.x - b.x.x * b.y.z, b.x.x * b.y.y - b.x.y * b.y.x};
        CHECK(dot(xy, b.z) > 0.9999f);  
        if (wall)
            CHECK(b.y.y > 0.95f);  
    };
    for (float yaw : {0.0f, 1.0f, 3.0f, 6.0f})
    {
        check({0, 1, 0}, yaw, false);
        check({0.3f, 0.9f, -0.2f}, yaw, false);
        check({1, 0, 0}, 0.2f, true);
        check({0, 0, -1}, 0.2f, true);
        check({0.7f, 0.1f, 0.7f}, 0.1f, true);
    }
     
    CHECK(dot(surfaceBasis({0, 1, 0}, 0.0f).x, surfaceBasis({0, 1, 0}, 1.5f).x) < 0.2f);
}

void testWreckStages()
{
    std::printf("wreck stages\n");
    using namespace warzone::decal;
    CHECK(stageAt(0.0f) == Stage::Burning && stageAt(69.9f) == Stage::Burning);
    CHECK(stageAt(70.0f) == Stage::Smouldering && stageAt(219.0f) == Stage::Smouldering);
    CHECK(stageAt(220.0f) == Stage::Cold && stageAt(1e6f) == Stage::Cold);
    CHECK(stageAt(10.0f, 5.0f, 10.0f) == Stage::Smouldering && stageAt(16.0f, 5.0f, 10.0f) == Stage::Cold);
     
    CHECK(smoulderAt(10.0f).interval == 0.0f && smoulderAt(300.0f).interval == 0.0f && smoulderAt(300.0f).ember == 0.0f);
    const Smoulder early = smoulderAt(71.0f), late = smoulderAt(210.0f);
    CHECK(early.interval > 0.0f && late.interval > early.interval);         
    CHECK(early.alpha > late.alpha && early.size > late.size);              
    CHECK(late.grey > early.grey);                                          
    CHECK(early.ember > late.ember && early.ember <= 1.0f && late.ember >= 0.0f);
     
    float peak = 0.0f;
    for (float a = 71.0f; a < 220.0f; a += 0.25f)
    {
        const Smoulder s = smoulderAt(a);
        CHECK(s.ember >= 0.0f && s.ember <= 1.0f && s.interval >= 1.1f);
        if (a > 190.0f)
            peak = std::max(peak, s.ember);
    }
    CHECK(peak < 0.12f);
}

void testWreckTint()
{
    std::printf("wreck tint\n");
    using namespace warzone::decal;
     
    bool differs = false;
    int rust = 0, dark = 0;
    for (uint32_t seed = 0; seed < 40; ++seed)
        for (uint32_t mesh = 0; mesh < 24; ++mesh)
        {
            const Rgb a = charTint(seed, mesh), b = charTint(seed, mesh);
            CHECK(a.r == b.r && a.g == b.g && a.b == b.b);
            CHECK(a.r > 0.04f && a.r < 0.40f && a.g > 0.04f && a.g < 0.30f && a.b > 0.03f && a.b < 0.30f);
            const Rgb c = charTint(seed + 1, mesh);
            differs = differs || std::abs(a.r - c.r) > 1e-3f;
            rust += a.r > 1.4f * a.b;
            dark += a.r < 0.09f;
        }
    CHECK(differs && rust > 40 && dark > 40);
     
    const Rgb c = charTint(3, 5);
    CHECK(tintAt(c, 0.0f).r == 1.0f && tintAt(c, 0.0f).b == 1.0f);
    CHECK(std::abs(tintAt(c, 5.0f).g - c.g) < 1e-5f && std::abs(tintAt(c, 1.8f).r - c.r) < 1e-5f);
    float last = 2.0f;
    for (float a = 0.0f; a <= 2.0f; a += 0.1f)
    {
        const float r = tintAt(c, a).r;
        CHECK(r <= last + 1e-6f);
        last = r;
    }
}

void testScrap()
{
    std::printf("wreck scrap\n");
    using namespace warzone;
    using namespace warzone::decal;
    const auto plan = scrapPlan(7, {10, 3, -4}, 1.0f, 6);
    CHECK(plan.size() == 6 && scrapPlan(7, {10, 3, -4}, 1.0f, 6)[3].vel.x == plan[3].vel.x);
    CHECK(scrapPlan(8, {10, 3, -4}, 1.0f, 6)[3].vel.x != plan[3].vel.x);
    for (auto s : plan)
    {
        CHECK(s.vel.y > 3.0f && len(V3{s.vel.x, 0, s.vel.z}) >= 3.5f && s.scale > 0.3f);
         
        const float ground = 2.0f;
        bool rested = false;
        float flight = 0.0f;
        for (int i = 0; i < 120 * 20 && !rested; ++i)
        {
            rested = stepScrap(s, 1.0f / 120.0f, ground);
            flight += 1.0f / 120.0f;
            CHECK(s.pos.y >= ground + 0.15f * s.scale - 1e-4f || s.pos.y > ground);
        }
        CHECK(rested && s.landed && flight < 15.0f && std::abs(s.pos.y - (ground + 0.15f * s.scale)) < 1e-4f);
        const V3 at = s.pos;
        stepScrap(s, 0.5f, ground);
        CHECK(s.pos.x == at.x && s.pos.y == at.y && s.pos.z == at.z && s.age > flight);
    }
     
    const auto many = scrapPlan(3, {0, 0, 0}, 1.0f, 40);
    float sx = 0, sz = 0;
    for (const auto& s : many)
    {
        sx += s.vel.x;
        sz += s.vel.z;
    }
    CHECK(std::abs(sx) < 40 * 3.0f && std::abs(sz) < 40 * 3.0f);
}

void testDecals()
{
    testDecalPool();
    testDecalSizing();
    testDecalBasis();
    testWreckStages();
    testWreckTint();
    testScrap();
}
