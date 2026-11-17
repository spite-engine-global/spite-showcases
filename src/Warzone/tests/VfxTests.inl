 
 
 

namespace vfxt
{
    using namespace warzone::vfx;
    int count(const Plan& p, Card c)
    {
        int n = 0;
        for (int i = 0; i < p.count; ++i)
            n += p.cards[i].card == c;
        return n;
    }
    int count(const Plan& p, Stage s)
    {
        int n = 0;
        for (int i = 0; i < p.count; ++i)
            n += p.cards[i].stage == s;
        return n;
    }
}

void testVfxTiers()
{
    std::printf("vfx tiers\n");
    using namespace warzone::vfx;
    CHECK(tierFor(50, 6) == NEAR && tierFor(250, 6) == MID && tierFor(600, 6) == FAR && tierFor(3000, 6) == DISTANT);
     
    CHECK(tierFor(120, 1.5f) > tierFor(120, 12.0f));
     
    Tier prev = NEAR;
    bool mono = true;
    for (float d = 0; d < 4000; d += 10)
    {
        const Tier t = tierFor(d, 6);
        mono = mono && t >= prev;
        prev = t;
    }
    CHECK(mono);
    CHECK(distanceBoost(10) == 1.0f && distanceBoost(5000) == 2.6f && distanceBoost(520) == 2.0f);
}

void testVfxExplosion()
{
    std::printf("vfx explosion\n");
    using namespace warzone::vfx;
    Rng rng(7);
    int total[TIERS] = {};
    for (float r : {1.5f, 4.0f, 7.0f, 12.0f})
        for (int ti = 0; ti < TIERS; ++ti)
        {
            const Tier t = static_cast<Tier>(ti);
            const Plan p = explosion(r, 100, 0.5f, t, rng);
            total[ti] = std::max(total[ti], p.count);
            CHECK(p.count > 0 && p.count <= CARDS_BY_TIER[ti]);
             
            CHECK(vfxt::count(p, FIRE) >= 1 && vfxt::count(p, SMOKE) >= 1);
            bool sane = true;
            for (int i = 0; i < p.count; ++i)
            {
                const Spec& s = p.cards[i];
                sane = sane && s.delay >= 0 && s.delay < 3.0f && s.life > 0.2f && s.life < 20.0f && s.s0 > 0 && s.s1 >= s.s0 * 0.99f;
            }
            CHECK(sane);
            if (t == NEAR)
            {
                 
                CHECK(vfxt::count(p, ST_SHOCK) == 1 && vfxt::count(p, ST_FIREBALL) == 2 && vfxt::count(p, ST_DEBRIS) == 2);
                CHECK(vfxt::count(p, ST_SKIRT) == 5 && vfxt::count(p, ST_COLUMN) == 5 && vfxt::count(p, ST_SPARKS) == 1);
                float shockLife = 0, columnLife = 1e9f, columnDelay = 0, skirtDelay = 1e9f;
                for (int i = 0; i < p.count; ++i)
                {
                    const Spec& s = p.cards[i];
                    if (s.stage == ST_SHOCK)
                        shockLife = s.life;
                    if (s.stage == ST_COLUMN)
                    {
                        columnLife = std::min(columnLife, s.life);
                        columnDelay = std::max(columnDelay, s.delay);
                    }
                    if (s.stage == ST_SKIRT)
                        skirtDelay = std::min(skirtDelay, s.delay);
                }
                CHECK(shockLife < 0.6f && columnLife > 5.0f && columnDelay > 0.8f && skirtDelay < columnDelay);
                 
                float lastY = -1;
                bool climbs = true;
                for (int i = 0; i < p.count; ++i)
                    if (p.cards[i].stage == ST_COLUMN)
                    {
                        climbs = climbs && p.cards[i].pos.y > lastY;
                        lastY = p.cards[i].pos.y;
                    }
                CHECK(climbs);
                CHECK(p.lightPeak > 0 && p.lightLife > 0);
            }
        }
    {
        Rng sr(4);
        CHECK(smallBlast(1.5f, 50, NEAR, sr).count == 3 && smallBlast(1.5f, 250, MID, sr).count == 2 && smallBlast(1.5f, 500, FAR, sr).count == 1);
    }
    CHECK(total[NEAR] == 16 && total[MID] == 8 && total[FAR] == 4 && total[DISTANT] == 3);
     
    Rng r2(9);
    CHECK(vfxt::count(explosion(5, 60, 30, NEAR, r2), ST_SKIRT) == 0);
    CHECK(vfxt::count(explosion(5, 60, 0.5f, NEAR, r2), ST_SKIRT) == 5);
     
    CHECK(explosion(5, 2000, 0, DISTANT, r2).lightPeak == 0);
    CHECK(explosion(5, 100, 0, NEAR, r2).lightPeak > 0);
     
    Rng a(3), b(3);
    const Plan near = explosion(6, 100, 0, NEAR, a), far = explosion(6, 1500, 0, DISTANT, b);
    float fireNear = 0, fireFar = 0;
    for (int i = 0; i < near.count; ++i)
        if (near.cards[i].card == FIRE)
            fireNear = near.cards[i].s1;
    for (int i = 0; i < far.count; ++i)
        if (far.cards[i].card == FIRE)
            fireFar = far.cards[i].s1;
    CHECK(fireFar > fireNear * 2.0f);
     
    Rng c(11), d(11);
    const Plan p1 = explosion(6, 100, 0, NEAR, c), p2 = explosion(6, 100, 0, NEAR, d);
    bool same = p1.count == p2.count;
    for (int i = 0; same && i < p1.count; ++i)
        same = p1.cards[i].pos.x == p2.cards[i].pos.x && p1.cards[i].life == p2.cards[i].life;
    CHECK(same);
}

void testVfxGovernor()
{
    std::printf("vfx governor, queue, lights\n");
    using namespace warzone::vfx;
    Governor g;
    g.budget = 40;
    Tier t[8];
    for (auto& x : t)
        x = g.admit(NEAR);
     
    CHECK(t[0] == NEAR && t[1] == NEAR && t[2] > NEAR);
    CHECK(g.spent <= 36 + CARDS_BY_TIER[FAR] + 6 * CARDS_BY_TIER[DISTANT]);  
    for (auto x : t)
        CHECK(x <= DISTANT);
    g.newFrame();
    CHECK(g.spent == 0 && g.admit(NEAR) == NEAR);
     
    CHECK(g.admit(FAR) == FAR);

     
    DelayQueue<4> q;
    Spec col, shock;
    col.stage = ST_COLUMN;
    shock.stage = ST_SHOCK;
    for (int i = 0; i < 4; ++i)
        CHECK(q.push(col, {}, 1.0f + i));
    CHECK(!q.push(col, {}, 9.0f) && q.dropped == 1);    
    CHECK(q.push(shock, {}, 9.0f) && q.n == 4);        
    int seen = 0;
    CHECK(q.pop(0.5f, 10, [&](const Pending&) { ++seen; }) == 0);
    CHECK(q.pop(2.5f, 1, [&](const Pending&) { ++seen; }) == 1 && q.n == 3);   
    CHECK(q.pop(100.0f, 10, [&](const Pending&) { ++seen; }) == 3 && q.n == 0 && seen == 4);

     
    std::array<LightState, 3> l;
    CHECK(pickLight(l, 100.0f, 50.0f) == 0);
    l[0] = {0.1f, 0.4f, 7000, 30};
    l[1] = {0.3f, 0.4f, 7000, 30};
    l[2] = {0.0f, 0.4f, 1000, 400};
    CHECK(pickLight(l, 7000.0f, 40.0f) == 2);                       
    CHECK(pickLight(l, 500.0f, 600.0f) == -1);                      
    l[1].age = -1;
    CHECK(pickLight(l, 10.0f, 900.0f) == 1);
}

void testVfxWind()
{
    std::printf("vfx wind and motion\n");
    using namespace warzone::vfx;
    Wind w;
     
    const V3 a = w.at(12.5f, 100, -40, 5), b = w.at(12.5f, 100, -40, 5);
    CHECK(a.x == b.x && a.z == b.z && a.y == 0.0f);
    const V3 hi = w.at(12.5f, 100, -40, 100);
    CHECK(len(hi) > len(a) * 1.3f);
    CHECK(std::abs(hi.x * a.z - hi.z * a.x) > 1e-3f);  
    bool bounded = true;
    for (float t = 0; t < 120; t += 0.7f)
        bounded = bounded && len(w.at(t, t * 3, -t, 60)) < 3.5f * 1.5f * 1.8f + 0.5f && len(w.at(t, 0, 0, 0)) > 0.0f;
    CHECK(bounded);

     
    Motion m;
    m.pos = {0, 10, 0};
    m.vel = {0, 3, 0};
    m.life = 12;
    m.rise = 4;
    m.windK = 0.5f;
    float time = 0;
    float maxY = 0;
    for (int i = 0; i < 600; ++i)
    {
        step(m, 0.02f, w, time, 0.0f);
        time += 0.02f;
        maxY = std::max(maxY, m.pos.y);
    }
    const V3 ww = w.at(time, m.pos.x, m.pos.z, m.pos.y);
    CHECK(maxY > 25.0f);                                                  
    CHECK(m.pos.x * w.dirX + m.pos.z * w.dirZ > 15.0f);                   
    CHECK(std::abs(m.vel.x - ww.x) < 1.0f && std::abs(m.vel.z - ww.z) < 1.0f);
     
    Motion n;
    n.pos = {0, 10, 0};
    n.vel = {0, 3, 0};
    n.life = 12;
    n.rise = 4;
    for (int i = 0; i < 600; ++i)
        step(n, 0.02f, w, i * 0.02f, 0.0f);
    CHECK(std::abs(n.pos.x) < 0.01f && std::abs(n.pos.z) < 0.01f);
     
    Motion d;
    d.pos = {0, 1, 0};
    d.vel = {-w.dirX * 8, 0, -w.dirZ * 8};  
    d.life = 5;
    d.windK = 0.5f;
    d.drag = 1.0f;
    float minAlong = 1e9f, t2 = 0;
    for (int i = 0; i < 250; ++i)
    {
        step(d, 0.02f, w, t2, 0.0f);
        t2 += 0.02f;
        minAlong = std::min(minAlong, d.vel.x * w.dirX + d.vel.z * w.dirZ);
    }
    CHECK(minAlong < -3.0f && d.vel.x * w.dirX + d.vel.z * w.dirZ > 0.0f);
     
    CHECK(fadeAlpha(0, 0.1f, 0.8f) == 0.0f && fadeAlpha(1, 0.1f, 0.8f) == 0.0f && fadeAlpha(0.5f, 0.1f, 0.8f) == 1.0f);
    CHECK(fadeAlpha(0.05f, 0.1f, 0.8f) > 0.0f && fadeAlpha(0.05f, 0.1f, 0.8f) < 1.0f);
}

void testVfxWreck()
{
    std::printf("vfx wreck, muzzle, impact, tracer, wash\n");
    using namespace warzone::vfx;
     
    const Plume n = wreckPlume(NEAR, 70, 70, 1.0f), f = wreckPlume(FAR, 70, 70, 1.0f), d = wreckPlume(DISTANT, 70, 70, 1.0f);
    CHECK(n.interval < f.interval && f.interval < d.interval);
    CHECK(n.s1 < f.s1 && f.s1 < d.s1);
    const Plume old = wreckPlume(NEAR, 5, 70, 1.0f);
    CHECK(old.tint.r > n.tint.r && old.tint.a < n.tint.a && old.flameScale < n.flameScale && n.flameScale == 1.0f);
     
    CHECK(1.0f / d.interval < 1.0f / f.interval && 1.0f / f.interval < 1.0f / n.interval);
    CHECK(partPuffInterval(true, NEAR) < partPuffInterval(true, FAR) && partPuffInterval(false, NEAR) > partPuffInterval(true, NEAR));

     
    const Muzzle mg = muzzle(0, 50, NEAR), ac = muzzle(1, 50, NEAR), gun = muzzle(2, 50, NEAR), gunFar = muzzle(2, 900, FAR);
    CHECK(mg.flashS0 < ac.flashS0 && ac.flashS0 < gun.flashS0);
    CHECK(gun.smokePuffs == 3 && gun.groundDust == 4 && gun.shock && gunFar.smokePuffs == 1 && gunFar.groundDust == 0 && !gunFar.shock);
    CHECK(gunFar.flashS0 > gun.flashS0 * 2.0f && gunFar.lightPeak == 0 && gun.lightPeak > 0);

     
    Rng rng(5);
    int veh = 0, vehFar = 0, ground = 0;
    for (int i = 0; i < 200; ++i)
    {
        veh += impact(VEHICLE, 1, 100, NEAR, rng).sparks;
        vehFar += impact(VEHICLE, 1, 900, FAR, rng).sparks;
        ground += impact(GROUND, 1, 100, NEAR, rng).sparks;
    }
    CHECK(veh == 200 && vehFar == 0 && ground > 20 && ground < 90);
    CHECK(impact(GROUND, 1, 900, FAR, rng).dust && !impact(VEHICLE, 1, 900, FAR, rng).dust);
    CHECK(impact(PIECE, 1, 900, FAR, rng).debris == false);

     
    float L, W;
    tracerShape(10, 18, 0.45f, L, W);
    CHECK(L == 18 && W == 0.45f);
    tracerShape(1000, 18, 0.45f, L, W);
    CHECK(W >= 1.7f && W <= 4.0f && L >= 24.0f && L <= 60.0f);
    tracerShape(100000, 18, 0.45f, L, W);
    CHECK(W == 4.0f && L == 60.0f);

     
    CHECK(!rotorWash(35, 1, NEAR).active && !rotorWash(5, 0.3f, NEAR).active);
    const Wash low = rotorWash(3, 1, NEAR), high = rotorWash(25, 1, NEAR), far = rotorWash(3, 1, FAR);
    CHECK(low.active && high.active && low.strength > high.strength && low.s1 > high.s1 && low.interval < high.interval);
    CHECK(far.interval > low.interval * 2.0f && far.s1 > low.s1 && low.cards == 1 && far.cards == 1);
    CHECK(low.life / low.interval < POOL[WASH]);  
     
    CHECK(trailInterval(true, NEAR) < trailInterval(true, DISTANT) && trailInterval(false, NEAR) > trailInterval(true, NEAR));
}

void testVfxBudget()
{
    std::printf("vfx budget\n");
    using namespace warzone::vfx;
     
    uint32_t total = 0;
    for (auto n : POOL)
        total += n;
    CHECK(total == 348 && POOL[SMOKE] >= 40 && POOL[FLAME] >= 20);
     
    Governor g;
    Rng rng(21);
    int cards = 0, nearCount = 0;
    for (int i = 0; i < 12; ++i)
    {
        const float dist = 60.0f + 130.0f * i;
        const Tier t = g.admit(tierFor(dist, 6.0f));
        nearCount += t == NEAR;
        cards += explosion(6.0f, dist, 0.5f, t, rng).count;
    }
    CHECK(cards <= 64 && nearCount <= 2);  
     
    CHECK(POOL[SMOKE] + POOL[PUFF] < 200);
}
