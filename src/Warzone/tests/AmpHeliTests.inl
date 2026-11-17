 
 
 

void testAutocannon()
{
    std::printf("autocannon\n");
    AutocannonParams p;
    AutocannonState s;
    s.ammo = p.ammo;
     
    int rounds = 0;
    for (int i = 0; i < 120; ++i)
        rounds += stepAutocannon(s, p, true, 1.0f / 60.0f);
    CHECK(rounds >= 15 && rounds <= 17);
     
    AutocannonState slow;
    slow.ammo = p.ammo;
    int slowRounds = 0;
    for (int i = 0; i < 40; ++i)
        slowRounds += stepAutocannon(slow, p, true, 1.0f / 20.0f);
    CHECK(std::abs(slowRounds - rounds) <= 1);
     
    AutocannonState h;
    h.ammo = p.ammo;
    int beforeLock = 0;
    for (int i = 0; i < 600 && !h.overheated; ++i)
        beforeLock += stepAutocannon(h, p, true, 1.0f / 60.0f);
    CHECK(h.overheated);
    CHECK(beforeLock >= 20 && beforeLock <= 24);
    int whileHot = 0;
    for (int i = 0; i < 30; ++i)
        whileHot += stepAutocannon(h, p, true, 1.0f / 60.0f);
    CHECK(whileHot == 0);
     
    int cooled = 0;
    for (int i = 0; i < 600 && h.overheated; ++i, ++cooled)
        stepAutocannon(h, p, false, 1.0f / 60.0f);
    CHECK(!h.overheated && h.heat <= p.recoverAt + 1e-4f);
    CHECK(cooled > 60 && cooled < 300);  
    CHECK(stepAutocannon(h, p, true, 1.0f / 60.0f) == 1);
     
    AutocannonState b;
    b.ammo = p.ammo;
    for (int burst = 0; burst < 10; ++burst)
    {
        for (int i = 0; i < 30; ++i)
            stepAutocannon(b, p, true, 1.0f / 60.0f);  
        for (int i = 0; i < 60; ++i)
            stepAutocannon(b, p, false, 1.0f / 60.0f);  
    }
    CHECK(!b.overheated && b.heat < 0.5f);
     
    AutocannonState cold;
    const float s0 = spreadOf(cold, p, 0.0f);
    cold.heat = 1.0f;
    CHECK(spreadOf(cold, p, 0.0f) > s0 * 3.0f);
    CHECK(spreadOf(AutocannonState{}, p, 1.0f) > s0);
    Rng rng(5);
    const V3 dir = norm(V3{0.3f, 0.1f, 1.0f});
    float worst = 0.0f;
    for (int i = 0; i < 500; ++i)
        worst = std::max(worst, std::acos(std::clamp(dot(scatter(dir, 0.01f, rng.f01(), rng.f01()), dir), -1.0f, 1.0f)));
    CHECK(worst <= 0.0101f && worst > 0.007f);
     
    AutocannonState e;
    e.ammo = 3;
    int fired = 0;
    for (int i = 0; i < 120; ++i)
        fired += stepAutocannon(e, p, true, 1.0f / 60.0f);
    CHECK(fired == 3 && e.ammo == 0);
     
    const TankParams amp = ampDriveParams(), tank;
    CHECK(amp.maxForward > tank.maxForward && amp.turretRate > tank.turretRate * 2.0f && amp.maxElevation > 0.8f);
    TankState ts;
    TankInput in;
    in.throttle = 1.0f;
    for (int i = 0; i < 600; ++i)
        stepDrive(ts, amp, in, 1.0f / 60.0f);
    CHECK(std::abs(ts.speed - amp.maxForward) < 1e-3f);
}

void testHeliFlight()
{
    std::printf("helicopter flight\n");
    HeliParams p;
    const float dt = 1.0f / 60.0f;
     
    HeliState s;
    s.pos = {0, 20, 0};
    s.grounded = true;
    HeliInput up;
    up.climb = 1.0f;
    for (int i = 0; i < 300; ++i)
        stepHeli(s, p, up, 20.0f, dt);
    CHECK(!s.grounded && s.pos.y > 20.0f + 40.0f);
    CHECK(s.vel.y <= p.maxClimb + 1e-3f);
    HeliInput none;
    for (int i = 0; i < 180; ++i)
        stepHeli(s, p, none, 20.0f, dt);
    const float held = s.pos.y;
    for (int i = 0; i < 600; ++i)
        stepHeli(s, p, none, 20.0f, dt);
    CHECK(std::abs(s.pos.y - held) < 0.05f && std::abs(s.vel.y) < 1e-3f);
     
    HeliInput fwd;
    fwd.forward = 1.0f;
    float maxPitch = 0.0f;
    for (int i = 0; i < 900; ++i)
    {
        stepHeli(s, p, fwd, 20.0f, dt);
        maxPitch = std::max(maxPitch, s.pitch);
    }
    CHECK(std::abs(dot(s.vel, heliForward(s.yaw)) - p.maxForward) < 0.01f);
    CHECK(maxPitch > 0.2f && maxPitch <= p.maxPitch + 1e-4f);
    CHECK(s.pitch > 0.05f);  
    CHECK(std::abs(s.pos.y - held) < 0.05f);  
     
    float minPitch = 0.0f;
    int stopFrames = 0;
    for (; stopFrames < 1200 && len(s.vel) > 0.01f; ++stopFrames)
    {
        stepHeli(s, p, none, 20.0f, dt);
        minPitch = std::min(minPitch, s.pitch);
    }
    CHECK(len(s.vel) <= 0.01f);
    CHECK(stopFrames < 60 * 9);
    CHECK(minPitch < -0.15f);
     
    HeliInput side;
    side.strafe = 1.0f;
    float maxRoll = 0.0f;
    for (int i = 0; i < 600; ++i)
    {
        stepHeli(s, p, side, 20.0f, dt);
        maxRoll = std::max(maxRoll, s.roll);
    }
    CHECK(std::abs(dot(s.vel, heliLeft(s.yaw)) - p.maxSide) < 0.01f);
    CHECK(maxRoll > 0.2f);
     
    HeliState y;
    y.pos = {0, 100, 0};
    y.grounded = false;
    y.yaw = 3.0f;
    HeliInput look;
    look.hasYawTarget = true;
    look.yawTarget = -3.0f;  
    float maxRate = 0.0f;
    for (int i = 0; i < 300; ++i)
    {
        stepHeli(y, p, look, 0.0f, dt);
        maxRate = std::max(maxRate, std::abs(y.yawRate));
    }
    CHECK(std::abs(wrapPi(y.yaw - look.yawTarget)) < 0.01f);
    CHECK(maxRate <= p.maxYawRate + 1e-4f);
    HeliState far = y;
    far.yawRate = 0.0f;
    look.yawTarget = wrapPi(far.yaw + 3.0f);
    float rateFar = 0.0f;
    for (int i = 0; i < 120; ++i)
    {
        stepHeli(far, p, look, 0.0f, dt);
        rateFar = std::max(rateFar, std::abs(far.yawRate));
    }
    CHECK(std::abs(rateFar - p.maxYawRate) < 1e-3f);
     
    HeliState d;
    d.pos = {0, 30, 0};
    d.grounded = false;
    HeliInput down;
    down.climb = -1.0f;
    down.forward = 1.0f;
    for (int i = 0; i < 600; ++i)
        stepHeli(d, p, down, 25.0f, dt);
    CHECK(d.grounded && std::abs(d.pos.y - 25.0f) < 1e-4f);
    const V3 rest = d.pos;
    for (int i = 0; i < 120; ++i)
        stepHeli(d, p, down, 25.0f, dt);
    CHECK(len(d.pos - rest) < 2.0f);  
     
    HeliState c;
    c.pos = {0, p.maxAltitude - 5.0f, 0};
    c.grounded = false;
    for (int i = 0; i < 300; ++i)
        stepHeli(c, p, up, 0.0f, dt);
    CHECK(c.pos.y <= p.maxAltitude + 1e-3f);
     
    CHECK(s.rotorSpeed > p.rotorFlight - 0.1f);
    CHECK(d.rotorSpeed < p.rotorIdle + 0.1f);
     
    HeliState a, b;
    a.pos = b.pos = {10, 50, -20};
    a.grounded = b.grounded = false;
    for (int i = 0; i < 400; ++i)
    {
        HeliInput in;
        in.forward = std::sin(i * 0.05f);
        in.strafe = std::cos(i * 0.03f);
        in.climb = std::sin(i * 0.02f) * 0.5f;
        stepHeli(a, p, in, 0.0f, dt);
        stepHeli(b, p, in, 0.0f, dt);
    }
    CHECK(a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z && a.yaw == b.yaw);
}

void testLock()
{
    std::printf("missile lock\n");
    const V3 eye{0, 100, 0}, dir{0, 0, 1};
     
    std::vector<LockCandidate> c = {{BUILDING_LOCK + 7, {0, 100, 300}, false}, {3, {12, 100, 300}, true}};
    CHECK(pickLockCandidate(eye, dir, c, 0.06f, 1500.0f) == 1);
     
    c[1].pos = {60, 100, 300};
    CHECK(pickLockCandidate(eye, dir, c, 0.06f, 1500.0f) == 0);
     
    c = {{1, {15, 100, 300}, true}, {2, {10, 100, 600}, true}};
    CHECK(pickLockCandidate(eye, dir, c, 0.06f, 1500.0f) == 1);
     
    c = {{1, {0, 100, 2000}, true}, {2, {0, 100, -300}, true}};
    CHECK(pickLockCandidate(eye, dir, c, 0.06f, 1500.0f) == -1);
     
    LockParams lp;
    LockState s;
    const float dt = 1.0f / 60.0f;
    int frames = 0;
    while (!s.locked && frames < 600)
    {
        stepLock(s, lp, 4, dt);
        ++frames;
    }
    CHECK(s.locked && s.id == 4);
    CHECK(std::abs(frames * dt - lp.lockTime) < 2.5f * dt);
     
    for (int i = 0; i < 15; ++i)
        stepLock(s, lp, -1, dt);
    CHECK(s.locked && s.id == 4);
    stepLock(s, lp, 4, dt);
    for (int i = 0; i < 30; ++i)
        stepLock(s, lp, -1, dt);
    CHECK(!s.locked && s.id == -1);
     
    for (int i = 0; i < 50; ++i)
        stepLock(s, lp, 4, dt);
    CHECK(!s.locked && s.progress > 0.6f);
    for (int i = 0; i < 25; ++i)
        stepLock(s, lp, 9, dt);  
    CHECK(s.id == 9 && s.progress < 0.2f && !s.locked);
}

void testHeliMissile()
{
    std::printf("missile from a moving helicopter\n");
     
     
    Projectile m;
    m.kind = ProjectileKind::Missile;
    m.guided = true;
    V3 tank{150, 20, 700};
    const V3 tankVel{-10, 0, 0};
    m.pos = {0, 140, 0};
    m.vel = norm(tank - m.pos) * specOf(ProjectileKind::Missile).speed + V3{0, 0, 40};
    float best = 1e9f;
    for (int i = 0; i < 60 * 10 && m.alive; ++i)
    {
        tank = tank + tankVel * (1.0f / 60.0f);
        m.target = tank;
        stepProjectile(m, 1.0f / 60.0f);
        best = std::min(best, len(m.pos - tank));
        if (len(m.pos - tank) < 2.0f)
            break;
    }
    CHECK(best < specOf(ProjectileKind::Missile).blastRadius);
     
    const float v = specOf(ProjectileKind::Autocannon).speed;
    CHECK(v > 900.0f);
    float el;
    CHECK(solveElevation(v, GRAVITY, 400.0f, 0.0f, el));
    CHECK(std::tan(el) * 400.0f < 1.0f);
    CHECK(specOf(ProjectileKind::Autocannon).blastRadius > 0.5f && specOf(ProjectileKind::Autocannon).blastRadius < 2.0f);
}
