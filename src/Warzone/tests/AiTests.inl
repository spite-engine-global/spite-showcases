 
 
 
 

void testUnitGrid()
{
    std::printf("unit grid\n");
    Rng rng(42);
    std::vector<V3> pos;
    std::vector<uint8_t> live;
    for (int i = 0; i < 1500; ++i)
    {
        pos.push_back({rng.range(-1100.0f, 1100.0f), rng.range(0.0f, 300.0f), rng.range(-1100.0f, 1100.0f)});  
        live.push_back(rng.chance(0.9f) ? 1 : 0);
    }
    UnitGrid g;
    g.build(1024.0f, 64.0f, pos, live);
     
    int agree = 0;
    for (int q = 0; q < 200; ++q)
    {
        const V3 p{rng.range(-1000.0f, 1000.0f), 0.0f, rng.range(-1000.0f, 1000.0f)};
        const float r = rng.range(20.0f, 400.0f);
        int brute = -1;
        float bd = r * r;
        for (int i = 0; i < static_cast<int>(pos.size()); ++i)
        {
            const float dx = pos[i].x - p.x, dz = pos[i].z - p.z, d = dx * dx + dz * dz;
            if (live[i] && d <= bd && (i % 3) != 0)
            {
                bd = d;
                brute = i;
            }
        }
        agree += g.nearest(p, r, [](int u) { return (u % 3) != 0; }) == brute;
    }
    CHECK(agree == 200);
     
    bool allFound = true, noDuplicates = true;
    for (int q = 0; q < 100; ++q)
    {
        const V3 a{rng.range(-1000.0f, 1000.0f), 0, rng.range(-1000.0f, 1000.0f)}, b{rng.range(-1000.0f, 1000.0f), 0, rng.range(-1000.0f, 1000.0f)};
        std::vector<int> seen(pos.size(), 0);
        g.forEachNearSegment(a, b, 10.0f, [&](int u) { ++seen[u]; });
        for (int i = 0; i < static_cast<int>(pos.size()); ++i)
        {
            if (seen[i] > 1)
                noDuplicates = false;
            if (!live[i] || std::abs(pos[i].x) > 1024.0f || std::abs(pos[i].z) > 1024.0f)
                continue;
             
            const float ex = b.x - a.x, ez = b.z - a.z, L2 = ex * ex + ez * ez;
            const float t = std::clamp(((pos[i].x - a.x) * ex + (pos[i].z - a.z) * ez) / L2, 0.0f, 1.0f);
            const float d = std::hypot(a.x + ex * t - pos[i].x, a.z + ez * t - pos[i].z);
            if (d < 10.0f && !seen[i])
                allFound = false;
        }
    }
    CHECK(allFound);
    CHECK(noDuplicates);
     
    std::vector<uint8_t> none(pos.size(), 0);
    g.build(1024.0f, 64.0f, pos, none);
    CHECK(g.nearest({0, 0, 0}, 5000.0f, [](int) { return true; }) < 0);
}

void testNavigation()
{
    std::printf("navigation\n");
    NavGrid nav;
    nav.init(512.0f, 8.0f);
    std::vector<V3> path;
     
    CHECK(nav.findPath({-300, 0, 0}, {300, 0, 0}, path));
    CHECK(path.size() == 1 && flatDist(path[0], {300, 0, 0}) < 0.01f);
     
    nav.blockRect(-10.0f, -400.0f, 10.0f, 300.0f, 0.0f);
    CHECK(!nav.lineClear({-300, 0, 0}, {300, 0, 0}));
    CHECK(nav.findPath({-300, 0, 0}, {300, 0, 0}, path));
    bool clear = true, viaGap = false;
    V3 at{-300, 0, 0};
    for (const auto& w : path)
    {
        clear = clear && nav.lineClear(at, w);
        viaGap = viaGap || w.z > 300.0f;
        at = w;
    }
    CHECK(clear);
    CHECK(viaGap);
    CHECK(flatDist(path.back(), {300, 0, 0}) < 0.01f);
     
    CHECK(path.size() <= 4);
     
    nav.blockRect(200.0f, -40.0f, 240.0f, 40.0f, 0.0f);
    CHECK(nav.findPath({-300, 0, 0}, {220, 0, 0}, path));
    CHECK(!nav.isBlocked(path.back().x, path.back().z));
    CHECK(flatDist(path.back(), {220, 0, 0}) < 40.0f);
     
    NavGrid box;
    box.init(256.0f, 8.0f);
    box.blockRect(-60.0f, -60.0f, 60.0f, -50.0f, 0.0f);
    box.blockRect(-60.0f, 50.0f, 60.0f, 60.0f, 0.0f);
    box.blockRect(-60.0f, -60.0f, -50.0f, 60.0f, 0.0f);
    box.blockRect(50.0f, -60.0f, 60.0f, 60.0f, 0.0f);
    CHECK(!box.findPath({0, 0, 0}, {200, 0, 200}, path, 5000));
    CHECK(!box.findPath({0, 0, 0}, {200, 0, 200}, path, 10));
     
    NavGrid town;
    town.init(256.0f, 8.0f);
    town.blockCentres(-60.0f, -40.0f, -9.0f, 40.0f, 1.0f);
    town.blockCentres(9.0f, -40.0f, 60.0f, 40.0f, 1.0f);
    CHECK(town.isBlocked(-30.0f, 0.0f) && town.isBlocked(30.0f, 0.0f));
    CHECK(town.lineClear({0, 0, -100}, {0, 0, 100}));
    NavGrid closed;
    closed.init(256.0f, 8.0f);
    closed.blockRect(-60.0f, -40.0f, -9.0f, 40.0f, 2.5f);
    closed.blockRect(9.0f, -40.0f, 60.0f, 40.0f, 2.5f);
    CHECK(!closed.lineClear({0, 0, -100}, {0, 0, 100}));  
}

void testTargetPick()
{
    std::printf("target pick\n");
    CHECK(canEngage(VehicleType::Tank, VehicleType::Amp) && !canEngage(VehicleType::Tank, VehicleType::Heli));
    CHECK(canEngage(VehicleType::Amp, VehicleType::Heli) && !canEngage(VehicleType::Amp, VehicleType::Plane));
    CHECK(canEngage(VehicleType::Heli, VehicleType::Tank) && canEngage(VehicleType::Heli, VehicleType::Heli));
    CHECK(canEngage(VehicleType::Plane, VehicleType::Tank) && !canEngage(VehicleType::Plane, VehicleType::Heli));
    const V3 me{0, 0, 0};
    std::vector<Candidate> c = {
        {1, {100, 0, 0}, VehicleType::Tank, false},  
        {2, {300, 0, 0}, VehicleType::Amp, true},
        {3, {200, 80, 0}, VehicleType::Heli, true},  
        {4, {2000, 0, 0}, VehicleType::Tank, true},  
    };
    CHECK(pickTarget(VehicleType::Tank, me, c) == 2);  
    CHECK(pickTarget(VehicleType::Amp, me, c) == 3);   
    CHECK(pickTarget(VehicleType::Plane, me, c) == 2);
    c[0].visible = true;
    CHECK(pickTarget(VehicleType::Tank, me, c) == 1);  
    std::vector<Candidate> far = {{4, {2000, 0, 0}, VehicleType::Tank, true}};
    CHECK(pickTarget(VehicleType::Tank, me, far) == -1);
    CHECK(pickTarget(VehicleType::Plane, me, far) == -1 && engageRange(VehicleType::Plane) < 2000.0f);
}

void testGroundBrain()
{
    std::printf("ground brain\n");
    AiState s;
    s.type = VehicleType::Tank;
    s.pos = {0, 0, 0};
    s.yaw = 0.0f;
    s.path = {{0, 0, 100}, {100, 0, 100}};
     
    float top = 0.0f;
    for (int i = 0; i < 60 * 40 && s.pathIdx < s.path.size(); ++i)
    {
        stepGround(s, 1.0f / 60.0f, false);
        top = std::max(top, s.speed);
    }
    CHECK(s.pathIdx == s.path.size());
    CHECK(flatDist(s.pos, {100, 0, 100}) < 8.0f);
    CHECK(top <= aiParams(VehicleType::Tank).maxSpeed + 1e-3f);
     
    s.path = {{0, 0, 1000}};
    s.pathIdx = 0;
    s.target = 7;
    s.targetPos = s.pos + V3{300, 0, 0};
    int shots = 0;
    for (int i = 0; i < 60 * 30; ++i)
        shots += stepGround(s, 1.0f / 60.0f, true).fire ? 1 : 0;
    CHECK(s.speed < 0.1f);
    CHECK(std::abs(wrapAngle(yawTo(s.pos, s.targetPos) - s.yaw)) < 0.02f);
    CHECK(shots >= 4 && shots <= 5);  
     
    int blind = 0;
    for (int i = 0; i < 60 * 10; ++i)
        blind += stepGround(s, 1.0f / 60.0f, false).fire ? 1 : 0;
    CHECK(blind == 0 && s.speed > 1.0f);
     
    AiState a;
    a.type = VehicleType::Amp;
    a.target = 1;
    a.targetPos = {200, 0, 0};
    int rounds = 0;
    for (int i = 0; i < 60 * 2; ++i)
        rounds += stepGround(a, 1.0f / 60.0f, true).fire ? 1 : 0;
    CHECK(rounds == 6);
     
    AiState x, y;
    x.type = y.type = VehicleType::Amp;
    x.path = y.path = {{50, 0, 80}, {-40, 0, 200}};
    for (int i = 0; i < 900; ++i)
    {
        stepGround(x, 1.0f / 60.0f, false);
        stepGround(y, 1.0f / 60.0f, false);
    }
    CHECK(x.pos.x == y.pos.x && x.pos.z == y.pos.z && x.yaw == y.yaw);
}

void testHeliBrain()
{
    std::printf("heli brain\n");
    AiState h;
    h.type = VehicleType::Heli;
    h.pos = {0, 30, 0};
    h.goal = {600, 0, 0};
    h.hasGoal = true;
    for (int i = 0; i < 60 * 20; ++i)
        stepHeli(h, 1.0f / 60.0f, false, 20.0f);
     
    CHECK(std::abs(h.pos.y - (20.0f + aiParams(VehicleType::Heli).cruiseAgl)) < 5.0f);
    CHECK(flatDist(h.pos, h.goal) < 30.0f);
     
    h.target = 3;
    h.targetPos = {600, 20, 800};
    int shots = 0;
    float dmin = 1e9f, dmax = 0.0f;
    for (int i = 0; i < 60 * 60; ++i)
    {
        shots += stepHeli(h, 1.0f / 60.0f, true, 20.0f).fire ? 1 : 0;
        if (i > 60 * 30)
        {
            const float d = flatDist(h.pos, h.targetPos);
            dmin = std::min(dmin, d), dmax = std::max(dmax, d);
        }
    }
    const float so = aiParams(VehicleType::Heli).standoff;
    if (!(dmin > so - 120.0f && dmax < so + 120.0f))
        std::printf("  heli standoff %.0f..%.0f\n", dmin, dmax);
    CHECK(dmin > so - 120.0f && dmax < so + 120.0f);
    CHECK(std::abs(wrapAngle(yawTo(h.pos, h.targetPos) - h.yaw)) < 0.25f);
    CHECK(shots >= 8 && shots <= 18);
    CHECK(h.speed <= aiParams(VehicleType::Heli).maxSpeed + 1e-2f);
}

void testPlaneBrain()
{
    std::printf("plane brain\n");
    AiState p;
    p.type = VehicleType::Plane;
    p.pos = {-3000, 250, 0};
    p.yaw = AI_PI / 2;  
    p.speed = 140.0f;
    p.target = 5;
    p.targetPos = {0, 20, 300};
    int drops = 0;
    V3 dropAt{};
    float dropSpeed = 0.0f;
    for (int i = 0; i < 60 * 40 && drops == 0; ++i)
    {
        const AiFire f = stepPlane(p, 1.0f / 60.0f, true, 20.0f);
        if (f.fire)
        {
            ++drops;
            dropAt = p.pos;
            dropSpeed = p.speed;
        }
    }
    CHECK(drops == 1);
     
    const float release = bombReleaseDistance(dropSpeed, dropAt.y - 20.0f);
    const float d = flatDist(dropAt, p.targetPos);
    CHECK(d < release + 60.0f && d > release - 250.0f);
    CHECK(std::abs(wrapAngle(yawTo(dropAt, p.targetPos) - p.yaw)) < 0.2f);
    CHECK(p.phase == PlanePhase::Egress);
     
    int drops2 = 0;
    for (int i = 0; i < 60 * 120; ++i)
        drops2 += stepPlane(p, 1.0f / 60.0f, true, 20.0f).fire ? 1 : 0;
    CHECK(drops2 >= 1);
    CHECK(p.pos.y > 20.0f + 40.0f && std::abs(p.speed - 140.0f) < 1.0f);
     
    CHECK(bombReleaseDistance(150.0f, 300.0f) > bombReleaseDistance(120.0f, 300.0f));
    CHECK(bombReleaseDistance(140.0f, 400.0f) > bombReleaseDistance(140.0f, 200.0f));
     
    AiState c;
    c.type = VehicleType::Plane;
    c.pos = {0, 250, 0};
    c.speed = 140.0f;
    c.goal = {0, 0, 2000};
    c.hasGoal = true;
    int none = 0;
    for (int i = 0; i < 60 * 10; ++i)
        none += stepPlane(c, 1.0f / 60.0f, false, 20.0f).fire ? 1 : 0;
    CHECK(none == 0 && c.pos.z > 1000.0f);
     
    AiState q;
    q.type = VehicleType::Plane;
    q.pos = {0, 250, 0};
    q.speed = 140.0f;
    q.yaw = 0.0f;
    q.target = 2;
    q.targetPos = {0, 20, 400};
    int qd = 0;
    float farthest = 0.0f;
    for (int i = 0; i < 60 * 60 && qd == 0; ++i)
    {
        if (stepPlane(q, 1.0f / 60.0f, true, 20.0f).fire)
        {
            ++qd;
            const float rel = bombReleaseDistance(q.speed, q.pos.y - 20.0f), dd = flatDist(q.pos, q.targetPos);
            CHECK(dd < rel + 60.0f && dd > rel - 250.0f);
        }
        farthest = std::max(farthest, flatDist(q.pos, q.targetPos));
    }
    CHECK(qd == 1);
    CHECK(farthest > bombReleaseDistance(140.0f, 230.0f) + 400.0f);
}

void testWorldZones()
{
    std::printf("world zones\n");
     
    std::vector<Block> blocks;
    std::vector<CityInfo> cities = {{"Core", 0, 0, 120, 6}, {"Suburbia", 1000, 0, 120, 0}, {"Works", 2000, 0, 120, 0}, {"Hamlet", 3000, 0, 120, 0}};
    const Zone zones[4] = {Zone::City, Zone::Suburb, Zone::Industrial, Zone::Village};
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
            {
                Block b;
                b.x0 = cities[c].cx - 105 + i * 70 + 9, b.x1 = b.x0 + 52;
                b.z0 = -84 + j * 56 + 9, b.z1 = b.z0 + 38;
                b.zone = zones[c];
                b.city = static_cast<uint8_t>(c);
                blocks.push_back(b);
            }
    auto flat = [](float, float) { return 20.0f; };
    const Town t = generateTown(7, blocks, flat, {}, cities);
    int kinds[4][KIND_COUNT] = {};
    bool prefixed = true;
    for (uint32_t seed = 7; seed <= 14; ++seed)  
        for (const auto& b : generateTown(seed, blocks, flat, {}, cities).buildings)
        {
            ++kinds[b.city][static_cast<int>(b.kind)];
            prefixed = prefixed && b.name.rfind(cities[b.city].name + " ", 0) == 0;
        }
    CHECK(prefixed);
    CHECK(kinds[0][static_cast<int>(Kind::Tower)] >= 1);                                                   
    CHECK(kinds[1][static_cast<int>(Kind::Tower)] == 0 && kinds[1][static_cast<int>(Kind::House)] >= 4);   
    CHECK(kinds[2][static_cast<int>(Kind::Factory)] >= 1 && kinds[2][static_cast<int>(Kind::Tower)] == 0);  
    CHECK(kinds[3][static_cast<int>(Kind::Tower)] == 0 && kinds[3][static_cast<int>(Kind::Apartment)] == 0 && kinds[3][static_cast<int>(Kind::Factory)] == 0);
    const Town t2 = generateTown(7, blocks, flat, {}, cities);
    CHECK(t2.pieces.size() == t.pieces.size() && t2.buildings.size() == t.buildings.size());
    Structure st;
    buildStructure(t, st, 20.0f);
    CHECK(st.unsupportedAtBuild() == 0);
}

void testNearestThree()
{
    std::puts("nearest three: full-sort parity and stable equal-distance choices");
    Rng rng(51);
    bool same = true;
    for (int trial = 0; trial < 100; ++trial)
    {
        NearestThree nearest;
        std::vector<NearestThree::Entry> all;
        for (int i = 0; i < trial * 17; ++i)
        {
            float d = static_cast<float>(static_cast<int>(rng.range(0, 100)));
            all.push_back({i, d}); nearest.add(i, d);
        }
        std::stable_sort(all.begin(), all.end(), [](auto a, auto b) { return a.d < b.d; });
        same &= nearest.count == std::min<size_t>(3, all.size());
        for (size_t k = 0; k < nearest.count; ++k) same &= nearest.entries[k].i == all[k].i;
    }
    CHECK(same);
}
