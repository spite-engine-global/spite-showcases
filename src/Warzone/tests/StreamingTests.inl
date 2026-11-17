 

void testStreamingChoice()
{
    std::puts("streaming choice");
    StreamParams p;
    p.radiusIn = 100, p.radiusOut = 150, p.pieceBudget = 1000, p.budgetSlack = 1.2f;
    std::vector<float> dist = {10, 50, 90, 120, 200, 60};
    std::vector<uint32_t> pieces = {300, 300, 300, 300, 300, 300};
    std::vector<uint8_t> res(dist.size(), 0);
    std::vector<uint32_t> order;
    uint32_t used = chooseResident(dist, pieces, res, p, order);
     
    CHECK(order[0] == 0 && order[1] == 1 && order[2] == 5 && order[3] == 2);
    CHECK(res[0] && res[1] && res[5] && !res[2] && !res[3] && !res[4]);
    CHECK(used == 900);
     
    dist = {120, 500};
    pieces = {100, 100};
    res = {0, 0};
    chooseResident(dist, pieces, res, p, order);
    CHECK(!res[0]);
    res = {1, 1};
    chooseResident(dist, pieces, res, p, order);
    CHECK(res[0] && !res[1]);
    dist = {160};
    pieces = {100};
    res = {1};
    std::vector<uint32_t> o1;
    chooseResident(dist, pieces, res, p, o1);
    CHECK(!res[0]);  
     
    dist = {10, 20};
    pieces = {600, 500};
    res = {1, 1};
    used = chooseResident(dist, pieces, res, p, order);
    CHECK(res[0] && res[1] && used == 1100);
    res = {0, 0};
    chooseResident(dist, pieces, res, p, order);
    CHECK(res[0] && !res[1]);
     
    pieces = {0, 10};
    res = {1, 0};
    chooseResident(dist, pieces, res, p, order);
    CHECK(!res[0] && res[1]);
     
    dist = {5, 5, 5};
    pieces = {1, 1, 1};
    res = {0, 0, 0};
    chooseResident(dist, pieces, res, p, order);
    CHECK(order[0] == 0 && order[1] == 1 && order[2] == 2);
}

void testDetailChoice()
{
    std::puts("detail choice");
    StreamParams p;
    p.radiusIn = 100, p.radiusOut = 130, p.pieceBudget = 500, p.budgetSlack = 1.2f;
    std::vector<float> dist = {20, 60, 90, 110, 300};
    std::vector<uint32_t> pieces = {200, 200, 200, 50, 50};
    std::vector<uint8_t> resident = {1, 1, 1, 1, 1}, detail(5, 0);
    std::vector<uint32_t> order, scratch;
    sortByDistance(dist, order);
    uint32_t used = chooseDetail(order, dist, pieces, resident, detail, scratch, p);
     
    CHECK(detail[0] && detail[1] && !detail[2] && !detail[3] && !detail[4] && used == 400);
     
    dist = {20, 110};
    pieces = {10, 10};
    resident = {1, 1};
    detail = {0, 1};
    order.clear();
    sortByDistance(dist, order);
    chooseDetail(order, dist, pieces, resident, detail, scratch, p);
    CHECK(detail[0] && detail[1]);
    dist = {110, 140};
    detail = {0, 1};
    sortByDistance(dist, order);
    chooseDetail(order, dist, pieces, resident, detail, scratch, p);
    CHECK(!detail[0] && !detail[1]);  
     
    dist = {5, 6};
    resident = {0, 1};
    detail = {1, 0};
    sortByDistance(dist, order);
    chooseDetail(order, dist, pieces, resident, detail, scratch, p);
    CHECK(!detail[0] && detail[1]);
     
    p.pieceBudget = 100;
    dist = {10, 20};
    pieces = {80, 30};
    resident = {1, 1};
    detail = {1, 1};
    sortByDistance(dist, order);
    chooseDetail(order, dist, pieces, resident, detail, scratch, p);
    CHECK(detail[0] && detail[1]);  
    detail = {0, 0};
    chooseDetail(order, dist, pieces, resident, detail, scratch, p);
    CHECK(detail[0] && !detail[1]);  
}

void testTier()
{
    std::puts("mesh tier");
    TierParams t;
    t.lowIn = 450, t.lowOut = 400;
    CHECK(chooseTier(true, 600, 0, t) == 1);  
    CHECK(chooseTier(false, 100, 0, t) == 0);
    CHECK(chooseTier(false, 449, 0, t) == 0);
    CHECK(chooseTier(false, 451, 0, t) == 2);
    CHECK(chooseTier(false, 420, 2, t) == 2);  
    CHECK(chooseTier(false, 399, 2, t) == 0);
    CHECK(chooseTier(false, 420, 1, t) == 0);  
}

void testStandIns()
{
    std::puts("stand-ins");
     
    std::vector<Block> blocks;
    std::vector<CityInfo> cities;
    if (!loadWorldFile("assets_baked/warzone/world_meta.txt", blocks, cities))
    {
        std::puts("  (no world_meta.txt here: skipped)");
        return;
    }
    auto flat = [](float, float) { return 20.0f; };
    const Town t = generateTown(7, blocks, flat, {}, cities);
     
    int missingModel = 0;
    for (const auto& b : t.buildings)
        missingModel += std::ifstream("assets_baked/" + standInModel(b.kind, b.floors, b.bounds, b.id) + ".spiteasset").good() ? 0 : 1;
    CHECK(missingModel == 0);
    if (const char* dump = std::getenv("WZ_PRINT_KEYS"))  
    {
        std::set<std::string> keys;
        for (const auto& b : t.buildings)
            if (!isPatch(b.kind))
            {
                const std::string m = standInModel(b.kind, b.floors, b.bounds, b.id);  
                keys.insert(m.substr(7, m.size() - 9));
            }
        std::ofstream out(dump);
        for (const auto& k : keys)
        {
            const size_t u = k.find('_');
            int nx = 0, nz = 0, f = 0;
            std::sscanf(k.c_str() + u + 1, "%dx%dx%d", &nx, &nz, &f);
            out << k.substr(0, u) << ' ' << nx << ' ' << nz << ' ' << f << '\n';
        }
        std::printf("  stand-in shapes: %zu -> %s\n", keys.size(), dump);
    }
    CHECK(standInModel(Kind::House, 2, {{0, 0, 0}, {12.55f, 5, 8.5f}}, 0) != standInModel(Kind::House, 2, {{0, 0, 0}, {12.55f, 5, 8.5f}}, 1));
    CHECK(standInModel(Kind::House, 2, {{0, 0, 0}, {12.55f, 5, 8.5f}}, 0).rfind("wz_lod_house_3x2x2_", 0) == 0);
    int framed = 0, topMismatch = 0;
    float worstGap = 0.0f;
    for (const auto& b : t.buildings)
    {
        const StandIn s = standIn(b.kind, b.floors, b.bounds, b.bounds.hi.y, b.count);
        CHECK(s.shown);
        CHECK(s.size.x <= b.bounds.hi.x - b.bounds.lo.x + 1e-3f && s.size.z <= b.bounds.hi.z - b.bounds.lo.z + 1e-3f);
        CHECK(s.base.y + s.size.y <= b.bounds.hi.y + 1e-3f);
        if (isPatch(b.kind))
            continue;
        ++framed;
         
        const float gap = b.bounds.hi.y - (s.base.y + s.size.y);
        worstGap = std::max(worstGap, gap);
        topMismatch += gap > 7.0f;
        CHECK(s.size.x > 0.9f * (b.bounds.hi.x - b.bounds.lo.x));
    }
    std::printf("  %d framed buildings, worst top gap %.2f m\n", framed, worstGap);
    CHECK(framed > 500);
    CHECK(topMismatch == 0);
     
    const auto& tower = *std::find_if(t.buildings.begin(), t.buildings.end(), [](const BuildingSpec& b) { return b.kind == Kind::Tower; });
    const StandIn half = standIn(tower.kind, tower.floors, tower.bounds, tower.bounds.lo.y + 20.0f, 100);
    CHECK(std::abs(half.size.y - 20.0f) < 1e-3f);
    CHECK(!standIn(tower.kind, tower.floors, tower.bounds, tower.bounds.lo.y, 0).shown);
    CHECK(standIn(tower.kind, tower.floors, tower.bounds, tower.bounds.lo.y - 5.0f, 3).size.y >= 0.5f);
}

void testBuildingGrid()
{
    std::puts("building grid: exact residency through camera moves, teleports and damage");
    Rng rng(719);
    std::vector<Box> boxes;
    std::vector<uint32_t> pieces;
    for (uint32_t i = 0; i < 8343; ++i)
    {
        float x = rng.range(-4200, 4200), z = rng.range(-4200, 4200);
        boxes.push_back({{x, 0, z}, {x + rng.range(4, 400), 30, z + rng.range(4, 80)}});
        pieces.push_back(i % 13 == 0 ? 0 : i % 900 + 1);
    }
    BuildingGrid grid; grid.build(boxes);
    std::vector<uint8_t> full(boxes.size()), local(full), detailFull(full), detailLocal(full);
    std::vector<float> dist(boxes.size());
    std::vector<uint32_t> order, subset, scratch;
    StreamParams p, detail; detail.radiusIn = 280; detail.radiusOut = 360; detail.pieceBudget = 10000;
    bool same = true, deterministic = true, reduced = false;
    for (int frame = 0; frame < 120; ++frame)
    {
        float x = frame < 60 ? -800 + frame * 12.0f : 3200 - frame * 8.0f;
        float z = frame < 60 ? 100.0f : -3200.0f;
        for (size_t i = 0; i < boxes.size(); ++i) dist[i] = groundDistance(boxes[i], x, z);
        grid.candidates(x, z, p.radiusOut, subset);
        auto again = subset; grid.candidates(x, z, p.radiusOut, subset);
        deterministic &= again == subset;
        reduced |= subset.size() < boxes.size()/4;
        for (uint32_t i = 0; i < local.size(); ++i) if (local[i]) subset.push_back(i);
        std::sort(subset.begin(), subset.end()); subset.erase(std::unique(subset.begin(), subset.end()), subset.end());
        std::sort(subset.begin(), subset.end(), [&](uint32_t a, uint32_t b) { return dist[a] != dist[b] ? dist[a] < dist[b] : a < b; });
        auto used = chooseResident(dist, pieces, full, p, order);
        same &= used == chooseByOrder(subset, dist, pieces, local, p) && full == local;
        chooseDetail(order, dist, pieces, full, detailFull, scratch, detail);
        for (size_t i = 0; i < local.size(); ++i) if (!local[i]) detailLocal[i] = 0;
        chooseDetail(subset, dist, pieces, local, detailLocal, scratch, detail);
        same &= detailFull == detailLocal;
        if (frame == 45) for (size_t i = 0; i < pieces.size(); i += 3) pieces[i] = 0;
    }
    CHECK(same);
    CHECK(deterministic);
    CHECK(reduced);
}
