 
 

void testHitTests()
{
    const Box b{{0, 0, 0}, {2, 2, 2}};
    float t;
    V3 n;
    CHECK(segmentBox(b, {-1, 1, 1}, {3, 1, 1}, t, &n) && std::abs(t - 0.25f) < 1e-5f && n.x == -1.0f);
    CHECK(segmentBox(b, {1, 5, 1}, {1, -5, 1}, t, &n) && std::abs(t - 0.3f) < 1e-5f && n.y == 1.0f);
    CHECK(!segmentBox(b, {-1, 3, 1}, {3, 3, 1}, t));           
    CHECK(!segmentBox(b, {-3, 1, 1}, {-1, 1, 1}, t));          
    CHECK(segmentBox(b, {1, 1, 1}, {5, 1, 1}, t) && t == 0.0f);  
     
    Obb o;
    o.centre = {10, 1, 0};
    o.axis[0] = {0, 0, 1};
    o.axis[1] = {0, 1, 0};
    o.axis[2] = {-1, 0, 0};
    o.half = {2, 1, 4};
    CHECK(segmentObb(o, {0, 1, 0}, {20, 1, 0}, t, &n) && std::abs(t * 20 - 6.0f) < 1e-4f && std::abs(n.x + 1.0f) < 1e-5f);
    CHECK(!segmentObb(o, {0, 1, 3}, {20, 1, 3}, t));  
    CHECK(std::abs(distance(o, {10, 1, 6}) - 4.0f) < 1e-4f);
}

void testBallistics()
{
     
    const float v = specOf(ProjectileKind::Shell).speed;
    for (float dx : {150.0f, 600.0f, 1500.0f})
        for (float dy : {-15.0f, 0.0f, 30.0f})
        {
            float a;
            CHECK(solveElevation(v, GRAVITY, dx, dy, a));
            Projectile p;
            p.kind = ProjectileKind::Shell;
            p.vel = {0, std::sin(a) * v, std::cos(a) * v};
            float best = 1e9f;
            const V3 goal{0, dy, dx};
            for (int i = 0; i < 120 * 10 && p.pos.z < dx + 20; ++i)
            {
                const V3 before = p.pos;
                stepProjectile(p, 1.0f / 120.0f);
                const V3 seg = p.pos - before;  
                const float u = std::clamp(dot(goal - before, seg) / std::max(dot(seg, seg), 1e-9f), 0.0f, 1.0f);
                best = std::min(best, len(before + seg * u - goal));
            }
            CHECK(best < 1.0f);
        }
    float a;
    CHECK(!solveElevation(100.0f, GRAVITY, 5000.0f, 0.0f, a));  
     
    Projectile p;
    p.kind = ProjectileKind::Shell;
    p.vel = {0, 50, 100};
    for (int i = 0; i < 240; ++i)
        stepProjectile(p, 1.0f / 120.0f);
    CHECK(std::abs(p.pos.z - 200.0f) < 1e-2f && std::abs(p.pos.y - (100.0f - 0.5f * GRAVITY * 4.0f)) < 0.2f);
}

void testGuidance()
{
     
    Projectile m;
    m.kind = ProjectileKind::Missile;
    m.guided = true;
    m.pos = {0, 100, 0};
    m.vel = {specOf(ProjectileKind::Missile).speed, 0, 0};
    m.target = {0, 20, 800};
    float closest = 1e9f;
    for (int i = 0; i < 120 * 9 && closest > 2.0f; ++i)
    {
        stepProjectile(m, 1.0f / 120.0f);
        closest = std::min(closest, len(m.pos - m.target));
    }
    CHECK(closest < 2.0f);
    CHECK(len(m.vel) <= specOf(ProjectileKind::Missile).maxSpeed + 1e-3f);
     
    const V3 d = turnToward({1, 0, 0}, {0, 0, 1}, 0.1f);
    CHECK(std::abs(std::acos(dot(d, V3{1, 0, 0})) - 0.1f) < 1e-4f);
    CHECK(len(turnToward({1, 0, 0}, {-1, 0, 0}, 0.5f) - V3{1, 0, 0}) > 0.4f);  
     
    Projectile b;
    b.kind = ProjectileKind::Bomb;
    b.guided = true;
    b.pos = {0, 800, 0};
    b.vel = V3{0, 0, 200} * BOMB_SPEED_SCALE;
    b.target = {30, 20, 1000};
    for (int i = 0; i < 120 * 30 && b.pos.y > 20.0f; ++i)
        stepProjectile(b, 1.0f / (120.0f * BOMB_SPEED_SCALE));
    CHECK(len(b.pos - b.target) < specOf(ProjectileKind::Bomb).blastRadius);
}

void testDamageModel()
{
    const Armor a;
    CHECK(armorFactor(a, {0, 0, -1}) == a.front);  
    CHECK(armorFactor(a, {0, 0, 1}) == a.rear);
    CHECK(armorFactor(a, {1, 0, 0}) == a.side);
    CHECK(armorFactor(a, {0, -1, 0}) == a.top);
    CHECK(armorFactor(a, norm({0.3f, 0, -1})) == a.front);
    CHECK(splash(900, 4, 0) == 900.0f && splash(900, 4, 4) == 0.0f && splash(900, 4, 6) == 0.0f);
    CHECK(std::abs(splash(900, 4, 2) - 225.0f) < 1e-3f);
    Health h{100, 100};
    CHECK(!h.apply(60) && h.hp == 40.0f);
    CHECK(h.apply(60) && h.dead && h.hp == 0.0f);
    CHECK(!h.apply(10));  
     
    float lastShake = 2.0f, lastGain = 2.0f, lastDelay = -1.0f;
    for (float d = 0; d < 3000; d += 25)
    {
        const float sh = shakeAt(1.0f, 4.5f, d), g = soundGain(d, 30.0f), dl = soundDelay(d);
        CHECK(sh <= lastShake && g <= lastGain && dl >= lastDelay);
        lastShake = sh, lastGain = g, lastDelay = dl;
    }
    CHECK(shakeAt(1.0f, 4.5f, 0.0f) == 1.0f && shakeAt(1.0f, 4.5f, 2000.0f) == 0.0f);
    CHECK(std::abs(soundDelay(343.0f) - 1.0f) < 1e-5f && soundGain(10, 30) == 1.0f && soundGain(5000, 30) == 0.0f);
}

void testTankModel()
{
    TankParams p;
    TankState s;
    TankInput in;
    in.throttle = 1.0f;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60 * 10; ++i)
        stepDrive(s, p, in, dt);
    CHECK(std::abs(s.speed - p.maxForward) < 1e-3f);
     
    TankState s2;
    for (int i = 0; i < static_cast<int>(60 * p.maxForward / p.accel / 2); ++i)
        stepDrive(s2, p, in, dt);
    CHECK(std::abs(s2.speed - p.maxForward / 2) < 0.2f);
     
    in.throttle = 0.0f;
    in.brake = true;
    int frames = 0;
    while (s.speed > 0.0f && frames < 1000)
        stepDrive(s, p, in, dt), ++frames;
    CHECK(frames <= static_cast<int>(60 * p.maxForward / p.brake) + 2);
     
    in.brake = false;
    in.throttle = -1.0f;
    for (int i = 0; i < 60 * 10; ++i)
        stepDrive(s, p, in, dt);
    CHECK(std::abs(s.speed + p.maxReverse) < 1e-3f);
    TankState piv;
    TankInput turn;
    turn.steer = 1.0f;
    for (int i = 0; i < 60 * 3; ++i)
        stepDrive(piv, p, turn, dt);
    CHECK(std::abs(piv.yawRate - p.turnRate) < 1e-3f && trackSpeed(piv, p, true) < 0 && trackSpeed(piv, p, false) > 0);
     
    TankState g;
    TankInput aim;
    aim.hasAim = true;
    aim.aimYaw = -3.0f;  
    aim.aimElevation = 1.0f;
    stepGun(g, p, aim, 1.0f);
    CHECK(std::abs(g.turretYaw + p.turretRate) < 1e-4f && std::abs(g.elevation - p.gunRate) < 1e-4f);
    g.turretYaw = 3.0f;
    aim.aimYaw = -3.0f;  
    stepGun(g, p, aim, 1.0f);
    CHECK(std::abs(g.turretYaw + 3.0f) < 1e-3f);
    for (int i = 0; i < 20; ++i)
        stepGun(g, p, aim, 1.0f);
    CHECK(std::abs(g.elevation - p.maxElevation) < 1e-5f);
     
    TankState f;
    TankInput fire;
    fire.fire = true;
    int shots = 0;
    for (int i = 0; i < static_cast<int>(60 * p.reload * 2.5f); ++i)
    {
        stepGun(f, p, fire, dt);
        shots += f.firedThisStep ? 1 : 0;
    }
    CHECK(shots == 3 && f.shots == 3u);
    TankState r;
    stepGun(r, p, fire, dt);
    CHECK(r.recoil == 1.0f && r.firedThisStep);
    for (int i = 0; i < static_cast<int>(60 * (p.reload + 0.5f)); ++i)
        stepGun(r, p, TankInput{}, dt);
    CHECK(r.recoil == 0.0f && r.reload == 0.0f);
     
    TankState h;
    stepGun(h, p, fire, dt);
    CHECK(h.pitchVel > 0.0f);  
    for (int i = 0; i < 60 * 4; ++i)
        stepSuspension(h, p, dt);
    CHECK(std::abs(h.pitch) < 0.002f && std::abs(h.roll) < 0.002f);
     
    float yaw, el;
    CHECK(aimAngles({0, 0, 800}, p.muzzleSpeed, yaw, el) && std::abs(yaw) < 1e-5f && el > 0.0f && el < 0.05f);
    CHECK(aimAngles({100, 0, 0}, p.muzzleSpeed, yaw, el) && std::abs(yaw - 1.5707963f) < 1e-4f);
}

void testPieceRaycast()
{
    Town t = smallFrame(3);
    Structure s;
    buildStructure(t, s, 10.0f);
    Rng rng(5);
    int agree = 0, hits = 0;
    for (int k = 0; k < 400; ++k)
    {
        const V3 a{rng.range(-30, 30), rng.range(9, 25), rng.range(-30, 30)};
        const V3 b{rng.range(-30, 30), rng.range(9, 25), rng.range(-30, 30)};
        uint32_t piece = ~0u;
        float t0 = 9;
        const bool hit = s.raycast(a, b, piece, t0);
        float best = 2;  
        for (uint32_t i = 0; i < s.size(); ++i)
        {
            float th;
            if (segmentBox(s.node(i).box, a, b, th) && th < best)
                best = th;
        }
        agree += (hit == (best <= 1.0f) && (!hit || std::abs(best - t0) < 1e-5f)) ? 1 : 0;
        hits += hit ? 1 : 0;
    }
    CHECK(agree == 400);
    CHECK(hits > 40);
     
    uint32_t piece;
    float th;
    const V3 a{-30, 11, 4}, b{30, 11, 4};
    CHECK(s.raycast(a, b, piece, th));
    s.remove(piece, 1);
    uint32_t again = piece;
    float th2;
    CHECK(!s.raycast(a, b, again, th2) || again != piece);
    std::printf("  piece raycast: %d of 400 random segments hit\n", hits);
}
