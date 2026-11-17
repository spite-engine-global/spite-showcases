 
 
 

void testPieceFlight()
{
    std::printf("piece flight\n");
    auto flat = [](float, float) { return 20.0f; };
    Rng rng(9);
     
    int ok = 0, okRest = 0, below = 0;
    float apexMin = 1e9f, apexMax = 0.0f, farMax = 0.0f;
    for (int k = 0; k < 50; ++k)
    {
        ThrownState p;
        p.pos = {0, 22.0f, 0};
        p.radius = 1.3f;
        const Launch l = launchFor(VehicleType::Tank, 0, {}, rng);
        p.vel = l.vel;
        float apex = 0.0f;
        for (int i = 0; i < 60 * 20 && p.phase == ThrownPhase::Flying; ++i)
        {
            stepPiece(p, 1.0f / 60.0f, flat);
            apex = std::max(apex, p.pos.y - 22.0f);
            below += p.pos.y < 20.0f + p.radius - 1e-3f ? 1 : 0;
        }
        apexMin = std::min(apexMin, apex), apexMax = std::max(apexMax, apex);
        farMax = std::max(farMax, std::hypot(p.pos.x, p.pos.z));
        ok += p.phase == ThrownPhase::Resting ? 1 : 0;
        okRest += (p.phase == ThrownPhase::Resting && std::abs(p.pos.y - (20.0f + p.radius)) < 1e-3f && p.bounces >= 1) ? 1 : 0;
    }
    CHECK(ok == 50 && okRest == 50);
    CHECK(below == 0);
    CHECK(apexMin > 8.0f && apexMax < 20.0f);
    CHECK(farMax < 30.0f);
     
    auto slope = [](float x, float) { return 10.0f + 0.3f * x; };
    ThrownState s;
    s.pos = {0, 30, 0};
    s.vel = {6, 12, 0};
    s.radius = 1.0f;
    for (int i = 0; i < 60 * 20 && s.phase == ThrownPhase::Flying; ++i)
        stepPiece(s, 1.0f / 60.0f, slope);
    CHECK(s.phase == ThrownPhase::Resting && std::abs(s.pos.y - (10.0f + 0.3f * s.pos.x + 1.0f)) < 1e-3f);
     
    PieceParams k;
    float t = 0.0f;
    while (s.phase == ThrownPhase::Resting && t < 100.0f)
    {
        stepPiece(s, 0.1f, slope, k);
        t += 0.1f;
    }
    CHECK(std::abs(t - k.restSeconds) < 0.25f && s.phase == ThrownPhase::Sinking);
    const float y0 = s.pos.y;
    while (s.phase == ThrownPhase::Sinking)
        stepPiece(s, 0.1f, slope, k);
    CHECK(s.phase == ThrownPhase::Gone && y0 - s.pos.y > k.sinkDepth * 0.9f);
     
    ThrownState b;
    b.pos = {0, 40, 0};
    b.vel = {0, 0, 0};
    b.radius = 0.5f;
    stepPiece(b, 1.0f / 60.0f, flat);
    CHECK(b.spin == 1.0f);
    for (int i = 0; i < 60 * 10 && b.phase == ThrownPhase::Flying; ++i)
        stepPiece(b, 1.0f / 60.0f, flat);
    CHECK(b.spin == 0.0f);
}

void testBreakupRules()
{
    std::printf("break-up rules\n");
     
    CHECK(breakupDetail(50.0f) == 2 && breakupDetail(349.0f) == 2);
    CHECK(breakupDetail(500.0f) == 1 && breakupDetail(899.0f) == 1);
    CHECK(breakupDetail(900.0f) == 0 && breakupDetail(5000.0f) == 0);
    Rng rng(3);
     
    bool turretUp = true, partsLower = true, bladesOut = true, jetCarries = true;
    for (int k = 0; k < 100; ++k)
    {
        const Launch t = launchFor(VehicleType::Tank, 0, {}, rng), p = launchFor(VehicleType::Amp, 1, {}, rng);
        turretUp = turretUp && t.vel.y >= 14.0f && t.spinRate >= 2.0f;
        partsLower = partsLower && p.vel.y < 14.0f;
        const Launch b = launchFor(VehicleType::Heli, 0, {0, 0, 0}, rng);
        bladesOut = bladesOut && std::hypot(b.vel.x, b.vel.z) >= 10.0f && b.spinRate >= 10.0f;
        const Launch j = launchFor(VehicleType::Plane, k % 5, {140, 0, 0}, rng);
        jetCarries = jetCarries && j.vel.x > 60.0f;
    }
    CHECK(turretUp && partsLower && bladesOut && jetCarries);
     
    Rng a(77), c(77);
    const Launch la = launchFor(VehicleType::Tank, 0, {1, 0, 2}, a), lc = launchFor(VehicleType::Tank, 0, {1, 0, 2}, c);
    CHECK(la.vel.x == lc.vel.x && la.vel.y == lc.vel.y && la.spinRate == lc.spinRate);
     
    CHECK(overCap({1, 2, 3}, 3).empty());
    const auto out = overCap({5.0f, 30.0f, 1.0f, 12.0f, 30.5f}, 3);
    CHECK(out.size() == 2 && out[0] == 4 && out[1] == 1);
}
