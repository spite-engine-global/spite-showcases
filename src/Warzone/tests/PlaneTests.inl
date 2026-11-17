 
 
 

void testPlaneFlight()
{
    std::printf("plane flight\n");
    const PlaneParams p;
    const float dt = 1.0f / 60.0f;
     
    PlaneState s;
    s.pos = {0, 500, 0};
    s.speed = 100.0f;
    s.throttle = 1.0f;
    const V3 north{0, 0, 1};
    for (int i = 0; i < 60 * 60; ++i)
        stepPlane(s, p, north, 0.0f, dt);
    CHECK(s.speed > p.maxSpeed * 0.95f && s.speed < p.maxSpeed * 1.02f);
    CHECK(std::abs(s.pos.y - 500.0f) < 1.0f);
    CHECK(!s.stalled);
     
    PlaneState idle;
    idle.pos = {0, 800, 0};
    idle.speed = 80.0f;
    idle.throttle = 0.0f;
    bool stalled = false;
    for (int i = 0; i < 60 * 40; ++i)
    {
        stepPlane(idle, p, north, -1.0f, dt);
        stalled = stalled || idle.stalled;
    }
    CHECK(stalled);
    CHECK(idle.pos.y < 800.0f - 50.0f);
     
    PlaneState climb;
    climb.pos = {0, 800, 0};
    climb.speed = 45.0f;
    climb.throttle = 0.0f;
    climb.dir = norm(V3{0, 0.5f, 1});
    for (int i = 0; i < 60 * 3; ++i)
        stepPlane(climb, p, norm(V3{0, 1, 1}), 0.0f, dt);
    CHECK(climb.dir.y < 0.0f);
     
    for (int i = 0; i < 60 * 10; ++i)
        stepPlane(climb, p, norm(V3{0, -0.3f, 1}), 1.0f, dt);
    CHECK(!climb.stalled && climb.speed > p.stallSpeed * 1.2f);
     
    PlaneState t;
    t.throttle = 0.5f;
    stepPlane(t, p, north, 1.0f, 0.5f);
    CHECK(std::abs(t.throttle - (0.5f + p.throttleRate * 0.5f)) < 1e-4f);
    for (int i = 0; i < 10; ++i)
        stepPlane(t, p, north, 1.0f, 0.5f);
    CHECK(t.throttle == 1.0f);
}

void testPlaneTurn()
{
    std::printf("plane turn\n");
    const PlaneParams p;
    const float dt = 1.0f / 60.0f;
     
    PlaneState s;
    s.pos = {0, 600, 0};
    s.speed = 120.0f;
    s.throttle = 0.8f;
    const V3 left{1, 0, 0};  
    float maxRoll = 0.0f, maxRate = 0.0f;
    int steps = 0;
    for (; steps < 60 * 20 && dot(s.dir, left) < 0.999f; ++steps)
    {
        stepPlane(s, p, left, 0.0f, dt);
        maxRoll = std::max(maxRoll, s.roll);
        maxRate = std::max(maxRate, std::abs(s.yawRate));
    }
    CHECK(dot(s.dir, left) >= 0.999f);
    CHECK(maxRate <= turnLimit(p, 100.0f) * 1.05f);
    const float seconds = steps * dt;
    CHECK(seconds > 1.5f && seconds < 6.0f);  
    CHECK(maxRoll > 0.6f && maxRoll <= p.maxBank + 1e-4f);  
     
    for (int i = 0; i < 120; ++i)
        stepPlane(s, p, left, 0.0f, dt);
    CHECK(std::abs(s.roll) < 0.05f);
     
    CHECK(turnLimit(p, 180.0f) < turnLimit(p, 90.0f));
    CHECK(turnLimit(p, 20.0f) == p.maxTurnRate);
     
    PlaneState up;
    up.pos = {0, 300, 0};
    up.speed = 150.0f;
    up.throttle = 1.0f;
    for (int i = 0; i < 60 * 4; ++i)
        stepPlane(up, p, V3{0, 1, 0}, 0.0f, dt);
    CHECK(up.dir.y <= p.maxClimb + 1e-3f);
    CHECK(up.pos.y > 300.0f + 100.0f);
     
    PlaneState a, b;
    a.speed = b.speed = 120.0f;
    a.throttle = b.throttle = 0.5f;
    a.pos = b.pos = {0, 800, 0};
    for (int i = 0; i < 60 * 5; ++i)
    {
        stepPlane(a, p, norm(V3{0, 0.5f, 1}), 0.0f, dt);
        stepPlane(b, p, norm(V3{0, -0.5f, 1}), 0.0f, dt);
    }
    CHECK(a.speed < 110.0f && b.speed > 130.0f);
     
    PlaneState d1, d2;
    for (int i = 0; i < 500; ++i)
    {
        stepPlane(d1, p, norm(V3{0.3f, 0.1f, 1}), 0.2f, dt);
        stepPlane(d2, p, norm(V3{0.3f, 0.1f, 1}), 0.2f, dt);
    }
    CHECK(d1.pos.x == d2.pos.x && d1.pos.y == d2.pos.y && d1.roll == d2.roll);
}

void testBombTargets()
{
    for (Kind kind : {Kind::Yard, Kind::Shed, Kind::Kiosk, Kind::Station, Kind::Garage, Kind::Barn})
        CHECK(isBombProp(kind));
    for (Kind kind : {Kind::House, Kind::Apartment, Kind::Tower, Kind::Factory, Kind::Row, Kind::Shop, Kind::Warehouse, Kind::Office, Kind::Hall, Kind::School})
        CHECK(!isBombProp(kind));
    std::printf("bomb targets\n");
    const V3 aim{100, 20, 100};
     
    std::vector<BombCandidate> c = {
        {BUILDING_LOCK + 4, {100, 30, 101}, TargetClass::Building},
        {BUILDING_LOCK + 9, {103, 22, 100}, TargetClass::Prop},
        {2, {125, 21, 115}, TargetClass::Unit},
    };
    CHECK(pickBombTarget(aim, c, 45.0f) == 2);
     
    c[2].pos = {170, 21, 100};
    CHECK(pickBombTarget(aim, c, 45.0f) == 0);
    c[0].pos = {110, 30, 100};
    CHECK(pickBombTarget(aim, c, 45.0f) == 0);
     
    std::vector<BombCandidate> props = {{BUILDING_LOCK + 9, {103, 22, 100}, TargetClass::Prop}};
    CHECK(pickBombTarget(aim, props, 45.0f) == 0);
     
    std::vector<BombCandidate> units = {{0, {130, 20, 100}, TargetClass::Unit}, {1, {110, 200, 100}, TargetClass::Unit}, {3, {95, 20, 95}, TargetClass::Unit}};
    CHECK(pickBombTarget(aim, units, 45.0f) == 2);
     
    CHECK(pickBombTarget(V3{131, 20, 100}, units, 45.0f) == 0);
     
    CHECK(pickBombTarget(V3{900, 20, 900}, units, 45.0f) == -1);
    CHECK(pickBombTarget(aim, std::vector<BombCandidate>{}, 45.0f) == -1);
}

void testBombPrediction()
{
    std::printf("bomb prediction\n");
    const float r = specOf(ProjectileKind::Bomb).blastRadius;
     
    const V3 from{0, 320, 0}, vel{0, -3, 120}, target{0, 20, 700};
    const BombPrediction pr = predictBomb(from, vel, target);
    CHECK(pr.inRange && pr.miss <= r);
    CHECK(pr.timeToImpact > 3.0f / BOMB_SPEED_SCALE && pr.timeToImpact < 12.0f / BOMB_SPEED_SCALE);
    Projectile b;
    b.kind = ProjectileKind::Bomb;
    b.pos = from;
    b.vel = vel * BOMB_SPEED_SCALE;
    b.guided = true;
    b.target = target;
    float t = 0.0f;
    while (b.pos.y > target.y && t < 30.0f)
    {
        stepProjectile(b, 0.05f / BOMB_SPEED_SCALE);
        t += 0.05f / BOMB_SPEED_SCALE;
    }
    CHECK(std::abs(t - pr.timeToImpact) < 0.1f);
    CHECK(len(b.pos - target) < r);
     
    const BombPrediction far = predictBomb(from, vel, V3{0, 20, 4000});
    CHECK(!far.inRange && far.miss > 500.0f);
     
    const BombPrediction side = predictBomb(from, vel, V3{150, 20, 600});
    CHECK(side.inRange);
     
    const BombPrediction fast = predictBomb(from, V3{0, -3, 180}, V3{0, 20, 1100});
    const BombPrediction slow = predictBomb(from, vel, V3{0, 20, 1100});
    CHECK(fast.miss <= slow.miss + 0.5f);
     
     
    Projectile g;
    g.kind = ProjectileKind::Bomb;
    g.pos = from;
    g.vel = vel;
    g.guided = true;
    g.target = target;
    float worstSag = 0.0f;
    for (int i = 0; i < 600 && g.pos.y > target.y; ++i)
    {
        stepProjectile(g, 0.05f);
        const float k = std::clamp(g.pos.z / target.z, 0.0f, 1.0f);
        const float lineY = from.y + (target.y - from.y) * k;
        worstSag = std::max(worstSag, lineY - g.pos.y);
    }
    CHECK(worstSag < 12.0f);
}

