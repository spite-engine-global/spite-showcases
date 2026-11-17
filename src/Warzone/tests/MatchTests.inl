 
 
 

void testSpawnRules()
{
    std::printf("spawn rules\n");
    const MatchRules r;
    const std::vector<SpawnPoint> pts = {
        {"G0", 0, 0, 0, Domain::Ground},   {"G1", 500, 0, 0, Domain::Ground}, {"A0", 0, 600, 0, Domain::Air},
        {"G2", 1000, 0, 0, Domain::Ground}, {"A1", 900, 900, 0, Domain::Air},
    };
     
    CHECK(domainOf(VehicleType::Tank) == Domain::Ground && domainOf(VehicleType::Amp) == Domain::Ground);
    CHECK(domainOf(VehicleType::Heli) == Domain::Air && domainOf(VehicleType::Plane) == Domain::Air);
     
    for (int pref = -1; pref < 5; ++pref)
    {
        const int g = pickSpawn(pts, Domain::Ground, std::vector<V3>{}, r.safeRadius, pref);
        const int a = pickSpawn(pts, Domain::Air, std::vector<V3>{}, r.safeRadius, pref);
        CHECK(g >= 0 && pts[g].domain == Domain::Ground);
        CHECK(a >= 0 && pts[a].domain == Domain::Air);
    }
     
    CHECK(pickSpawn(pts, Domain::Ground, std::vector<V3>{{1000, 0, 400}}, r.safeRadius, 1) == 1);
     
    const std::vector<V3> enemyAt1 = {{510, 0, 10}};
    CHECK(pickSpawn(pts, Domain::Ground, enemyAt1, r.safeRadius, 1) == 0);  
    const std::vector<V3> enemies = {{480, 0, 0}, {50, 0, 30}};
    CHECK(pickSpawn(pts, Domain::Ground, enemies, r.safeRadius, 0) == 3);  
     
    CHECK(pickSpawn(pts, Domain::Air, std::vector<V3>{{0, 0, 650}}, r.safeRadius) == 4);
     
    CHECK(pickSpawn(pts, Domain::Ground, std::vector<V3>{}, r.safeRadius) == 0);
     
    const std::vector<SpawnPoint> groundOnly = {{"G0", 0, 0, 0, Domain::Ground}};
    CHECK(pickSpawn(groundOnly, Domain::Air, std::vector<V3>{}, r.safeRadius) == -1);
     
    CHECK(spawnPosition(pts[2], VehicleType::Plane, 20.0f, r).y == 20.0f + r.planeAltitude);
    CHECK(spawnPosition(pts[2], VehicleType::Heli, 20.0f, r).y == 20.0f + r.heliAltitude);
    CHECK(spawnPosition(pts[0], VehicleType::Tank, 20.0f, r).y == 20.0f);
    CHECK(r.heliAltitude >= 50.0f && r.planeAltitude >= 150.0f && r.planeSpeed > 60.0f);
     
    CHECK(cycleSpawn(pts, Domain::Ground, 0, 1) == 1);
    CHECK(cycleSpawn(pts, Domain::Ground, 1, 1) == 3);
    CHECK(cycleSpawn(pts, Domain::Ground, 3, 1) == 0);
    CHECK(cycleSpawn(pts, Domain::Ground, 0, -1) == 3);
    CHECK(cycleSpawn(pts, Domain::Air, 2, 1) == 4);
    CHECK(cycleSpawn(pts, Domain::Air, 4, 1) == 2);
    CHECK(cycleSpawn(groundOnly, Domain::Ground, 0, 1) == 0);
}

void testScoring()
{
    std::printf("scoring and scoreboard\n");
    const MatchRules r;
    std::vector<Combatant> c = {{"You", 0, 0, 0, true}, {"Target Tank 1"}, {"Target AMP 1"}, {"Target Tank 2"}};
    recordKill(c, 1, 0);  
    recordKill(c, 2, 0);
    recordKill(c, 0, 3);  
    recordKill(c, 3, -1);  
    recordKill(c, 0, 0);  
    CHECK(c[0].kills == 2 && c[0].deaths == 2 && c[0].suicides == 1);
    CHECK(c[0].score(r) == 2 * r.killScore - r.suicidePenalty);
    CHECK(c[3].kills == 1 && c[3].deaths == 1 && c[3].suicides == 1 && c[3].score(r) == r.killScore - r.suicidePenalty);
    CHECK(c[1].deaths == 1 && c[1].score(r) == 0);
    recordKill(c, 9, 0);  
    CHECK(c[0].kills == 2);
    const auto order = scoreboard(c, r);
    CHECK(order.size() == 4);
    CHECK(order[0] == 0);  
    CHECK(order[1] == 3);  
     
    CHECK(order[2] == 2 && order[3] == 1);  
    std::vector<Combatant> tie = {{"B", 1, 2}, {"A", 1, 0}, {"C", 0, 0}};
    const auto o2 = scoreboard(tie, r);
    CHECK(o2[0] == 1 && o2[1] == 0 && o2[2] == 2);
}

void testMatchClock()
{
    std::printf("match clock and respawn\n");
    MatchClock m;
    CHECK(!m.step(1.0f));  
    m.start(10.0f);
    int ends = 0;
    for (int i = 0; i < 60 * 12; ++i)
        ends += m.step(1.0f / 60.0f) ? 1 : 0;
    CHECK(ends == 1 && m.over && m.left == 0.0f);
     
    MatchClock slow;
    slow.start(10.0f);
    int frames = 0;
    while (!slow.step(1.0f / 20.0f))
        ++frames;
    CHECK(frames >= 198 && frames <= 200);
    RespawnTimer t;
    CHECK(!t.armed() && !t.step(1.0f));
    t.arm(4.0f);
    int fired = 0, steps = 0;
    for (int i = 0; i < 600; ++i, ++steps)
    {
        if (t.step(1.0f / 60.0f))
        {
            ++fired;
            break;
        }
    }
    CHECK(fired == 1 && steps >= 238 && steps <= 241);
    CHECK(!t.armed() && !t.step(1.0f));
}
