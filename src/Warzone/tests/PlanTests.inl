 
 

bool loadWorldFile(const char* path, std::vector<Block>& blocks, std::vector<CityInfo>& cities)
{
    std::ifstream in(path);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line))
    {
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "city")
        {
            CityInfo c;
            float hx = 0, hz = 0;
            int zone = 0;
            ls >> c.name >> c.cx >> c.cz >> hx >> hz >> zone >> c.maxTowers >> c.density;
            c.radius = 0.81f * std::hypot(hx, hz);
            cities.push_back(c);
        }
        else if (tag == "block")
        {
            Block b;
            int zone = 0, city = 0;
            ls >> b.x0 >> b.z0 >> b.x1 >> b.z1 >> zone >> city;
            b.zone = static_cast<Zone>(zone);
            b.city = static_cast<uint8_t>(city);
            blocks.push_back(b);
        }
    }
    return !blocks.empty();
}

 
 
void checkTown(const Town& t, std::span<const Block> blocks, float ground, const char* what)
{
    Structure st;
    buildStructure(t, st, ground);
    CHECK(st.unsupportedAtBuild() == 0);
    if (st.unsupportedAtBuild())
        for (uint32_t i = 0; i < t.pieces.size(); ++i)
            if (!st.node(i).grounded && st.supporters(i).empty())
            {
                std::printf("  %s: unsupported %s in %s at (%.2f %.2f %.2f)\n", what, std::string(PIECES[t.pieces[i].type].slug).c_str(),
                            t.buildings[t.pieces[i].building].name.c_str(), t.pieces[i].pos.x, t.pieces[i].pos.y, t.pieces[i].pos.z);
                break;
            }
    int below = 0, outside = 0, clash = 0;
    for (const auto& p : t.pieces)
        below += p.pos.y < ground - 0.01f;
    for (const auto& b : t.buildings)
    {
        const V3 c = b.bounds.centre();
        const Block* home = nullptr;
        for (const Block& k : blocks)
            if (c.x >= k.x0 && c.x <= k.x1 && c.z >= k.z0 && c.z <= k.z1)
                home = &k;
        if (!home || b.bounds.lo.x < home->x0 + 1.5f || b.bounds.hi.x > home->x1 - 1.5f || b.bounds.lo.z < home->z0 + 1.5f ||
            b.bounds.hi.z > home->z1 - 1.5f)
        {
            if (!outside)
                std::printf("  %s: %s outside its block: x %.1f..%.1f z %.1f..%.1f\n", what, b.name.c_str(), b.bounds.lo.x, b.bounds.hi.x, b.bounds.lo.z, b.bounds.hi.z);
            ++outside;
        }
    }
    SpatialGrid grid(8.0f);
    std::vector<Box> boxes;
    boxes.reserve(t.pieces.size());
    for (uint32_t i = 0; i < t.pieces.size(); ++i)
    {
        boxes.push_back(pieceBox(t.pieces[i]));
        grid.insert(i, boxes.back());
    }
    std::vector<uint32_t> near;
    for (uint32_t i = 0; i < t.pieces.size(); ++i)
    {
        const uint16_t bi = t.pieces[i].building;
        if (t.buildings[bi].kind == Kind::Yard)
            continue;
        grid.query(boxes[i], near);
        for (uint32_t j : near)
            if (t.pieces[j].building != bi && overlaps(boxes[i], boxes[j], -0.02f))
            {
                if (!clash)
                    std::printf("  %s: %s and %s overlap at (%.1f %.1f)\n", what, t.buildings[bi].name.c_str(),
                                t.buildings[t.pieces[j].building].name.c_str(), t.pieces[i].pos.x, t.pieces[i].pos.z);
                ++clash;
            }
    }
    CHECK(below == 0);
    CHECK(outside == 0);
    CHECK(clash == 0);
}

 
struct PlanStats
{
    size_t buildings = 0, pieces = 0, yards = 0;
};
PlanStats printPlan(const Town& t, std::span<const Block> blocks, std::span<const CityInfo> cities)
{
    const size_t nc = cities.size();
    std::vector<size_t> nBlocks(nc), nBuildings(nc), nPieces(nc);
    std::vector<int> maxFloors(nc);
    std::vector<std::array<size_t, 2 * KIND_COUNT>> byKind(nc);
    std::array<size_t, 2 * KIND_COUNT> total{};
    for (const auto& b : blocks)
        ++nBlocks[b.city];
    for (const auto& b : t.buildings)
    {
        ++nBuildings[b.city];
        nPieces[b.city] += b.count;
        maxFloors[b.city] = std::max<int>(maxFloors[b.city], b.floors);
        const int k = static_cast<int>(b.kind);
        ++byKind[b.city][k], byKind[b.city][KIND_COUNT + k] += b.count;
        ++total[k], total[KIND_COUNT + k] += b.count;
    }
    for (size_t c = 0; c < nc; ++c)
        std::printf("  %-10s %4zu blocks %5zu buildings (%zu without yards) %7zu pieces, tallest %d storeys\n", cities[c].name.c_str(), nBlocks[c],
                    nBuildings[c], nBuildings[c] - byKind[c][static_cast<int>(Kind::Yard)], nPieces[c], maxFloors[c]);
    for (size_t c = 0; c < nc; ++c)
    {
        std::printf("  %-10s", cities[c].name.c_str());
        for (int k = 0; k < KIND_COUNT; ++k)
            if (byKind[c][k])
                std::printf("  %s %zu/%zu", kindName(static_cast<Kind>(k)), byKind[c][k], byKind[c][KIND_COUNT + k]);
        std::puts("");
    }
    std::printf("  all       ");
    for (int k = 0; k < KIND_COUNT; ++k)
        if (total[k])
            std::printf("  %s %zu/%zu", kindName(static_cast<Kind>(k)), total[k], total[KIND_COUNT + k]);
    std::puts("");
    std::printf("  total %zu blocks %zu buildings %zu pieces\n", blocks.size(), t.buildings.size(), t.pieces.size());
    return {t.buildings.size(), t.pieces.size(), total[static_cast<int>(Kind::Yard)]};
}

 
void testWorldPlan()
{
    std::puts("world plan");
    std::vector<Block> blocks;
    std::vector<CityInfo> cities;
    if (!loadWorldFile("assets_baked/warzone/world_meta.txt", blocks, cities))
    {
        std::puts("  (no world_meta.txt here: skipped)");
        return;
    }
    CHECK(cities.size() <= 64);
    auto flat = [](float, float) { return 20.0f; };
    const Town t = generateTown(7, blocks, flat, {}, cities);
    const PlanStats s = printPlan(t, blocks, cities);
     
    CHECK(s.buildings >= 3285);
    CHECK(s.buildings - s.yards >= 2900);
    CHECK(s.pieces < 520000);  
     
    int blockOverlaps = 0;
    for (size_t i = 0; i < blocks.size(); ++i)
        for (size_t j = i + 1; j < blocks.size(); ++j)
            blockOverlaps += blocks[i].x0 < blocks[j].x1 && blocks[j].x0 < blocks[i].x1 && blocks[i].z0 < blocks[j].z1 && blocks[j].z0 < blocks[i].z1;
    CHECK(blockOverlaps == 0);
    for (size_t c = 0; c < cities.size(); ++c)
        for (size_t d = c + 1; d < cities.size(); ++d)
            CHECK(cities[c].name != cities[d].name);
    {
        std::vector<std::string> names;
        for (const auto& b : t.buildings)
            names.push_back(b.name);
        std::sort(names.begin(), names.end());
        CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
    }
    CHECK(t.buildings.size() < 65000 && t.pieces.size() < 4000000);  
    checkTown(t, blocks, 20.0f, "world");
     
    std::array<int, KIND_COUNT> kinds{};
    for (const auto& b : t.buildings)
        ++kinds[static_cast<int>(b.kind)];
    for (int k = 0; k < KIND_COUNT; ++k)
        CHECK(kinds[k] > 0);
     
    const Town again = generateTown(7, blocks, flat, {}, cities);
    CHECK(again.pieces.size() == t.pieces.size() && again.buildings.size() == t.buildings.size());
    bool same = again.pieces.size() == t.pieces.size();
    for (size_t i = 0; same && i < t.pieces.size(); i += 17)
        same = again.pieces[i].type == t.pieces[i].type && again.pieces[i].pos.x == t.pieces[i].pos.x && again.pieces[i].pos.z == t.pieces[i].pos.z;
    CHECK(same);
}

 
void testSyntheticWorlds()
{
    std::puts("synthetic worlds");
    auto flat = [](float, float) { return 35.0f; };
    const float sizes[][2] = {{4, 4}, {9, 9}, {14, 30}, {22, 22}, {30, 18}, {38, 26}, {52, 38}, {64, 48}, {72, 52}, {100, 60}, {140, 140}, {26, 90}};
    size_t buildings = 0, pieces = 0;
    for (int density = 0; density <= 3; ++density)
    {
        std::vector<Block> blocks;
        std::vector<CityInfo> cities;
        for (int zone = 0; zone < 4; ++zone)
            cities.push_back({"Test" + std::to_string(zone), 0.0f, 0.0f, 260.0f, 3, density});
        float cursor = 0.0f;
        for (int zone = 0; zone < 4; ++zone)
            for (const auto& sz : sizes)
            {
                Block b;
                b.x0 = cursor, b.x1 = cursor + sz[0], b.z0 = -400.0f + 160.0f * zone, b.z1 = b.z0 + sz[1];
                b.zone = static_cast<Zone>(zone);
                b.city = static_cast<uint8_t>(zone);
                blocks.push_back(b);
                cursor += sz[0] + 20.0f;
            }
         
        for (uint32_t seed = 1; seed <= 3; ++seed)
        {
            const Town t = generateTown(seed, blocks, flat, {}, cities);
            checkTown(t, blocks, 35.0f, "synthetic");
            buildings += t.buildings.size(), pieces += t.pieces.size();
            for (const auto& b : t.buildings)
                CHECK(b.count > 0 && b.parts >= 1);
        }
    }
    std::printf("  12 sizes x 4 zones x 4 densities x 3 seeds: %zu buildings, %zu pieces\n", buildings, pieces);
     
    std::vector<Block> plain = {{0, 0, 52, 38}, {70, 0, 122, 38}, {140, 0, 192, 38}};
    const Town t = generateTown(5, plain, flat);
    checkTown(t, plain, 35.0f, "plain");
    CHECK(t.buildings.size() > 10);
     
    auto slope = [](float x, float) { return 30.0f + 0.02f * x; };
    std::vector<Block> hill = {{0, 0, 64, 48, Zone::Suburb, 0}, {80, 0, 144, 48, Zone::Village, 0}};
    const Town th = generateTown(9, hill, slope);
    Structure st;
    {
        std::vector<Box> boxes;
        std::vector<float> hp, gy;
        std::vector<uint16_t> bld;
        for (const auto& p : th.pieces)
        {
            boxes.push_back(pieceBox(p));
            hp.push_back(hitPoints(boxes.back(), PIECES[p.type].mat));
            bld.push_back(p.building);
            gy.push_back(slope(p.pos.x, p.pos.z));
        }
        st.build(boxes, hp, bld, gy);
    }
    std::printf("  sloped ground: %zu buildings, %u floating pieces\n", th.buildings.size(), st.unsupportedAtBuild());
}

void testDenseCity()
{
    std::puts("dense city rules");
     
    std::vector<Block> blocks;
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 7; ++j)
        {
            Block b;
            b.x0 = -245 + i * 70 + 9, b.x1 = b.x0 + 52;
            b.z0 = -196 + j * 56 + 9, b.z1 = b.z0 + 38;
            blocks.push_back(b);
        }
    auto flat = [](float, float) { return 20.0f; };
    const std::vector<CityInfo> dense = {{"Core", 0, 0, 250, 25, 2}}, old = {{"Core", 0, 0, 250, 25, 0}};
    int tallest = 0, big = 0, rows = 0, v1Tallest = 0;
    for (uint32_t seed = 1; seed <= 3; ++seed)
    {
        const Town a = generateTown(seed, blocks, flat, {}, dense), b = generateTown(seed, blocks, flat, {}, dense), v1 = generateTown(seed, blocks, flat, {}, old);
        CHECK(a.pieces.size() == b.pieces.size() && a.buildings.size() == b.buildings.size());
        for (const auto& bl : a.buildings)
        {
            tallest = std::max<int>(tallest, bl.kind == Kind::Tower ? bl.floors : 0);
            big += bl.kind == Kind::Tower && bl.floors >= 13;
            rows += bl.kind == Kind::Apartment && bl.bounds.hi.x - bl.bounds.lo.x > 6 * BAY + 1.0f;
        }
        for (const auto& bl : v1.buildings)
            v1Tallest = std::max<int>(v1Tallest, bl.kind == Kind::Tower ? bl.floors : 0);
        checkTown(a, blocks, 20.0f, "dense");
        checkTown(v1, blocks, 20.0f, "v1");
    }
    std::printf("  dense: tallest tower %d, towers >= 13 storeys: %d, apartment rows over 6 bays: %d | v1 tallest tower %d\n", tallest, big, rows, v1Tallest);
    CHECK(tallest >= 13 && tallest <= 16 && big >= 1 && rows >= 1);
    CHECK(v1Tallest <= 12);
}
