 

 
void testDensityLevels()
{
    std::puts("density levels");
    std::vector<Block> blocks;
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j)
        {
            Block b;
            b.x0 = i * 70.0f, b.x1 = b.x0 + 52.0f, b.z0 = j * 56.0f, b.z1 = b.z0 + 38.0f;
            blocks.push_back(b);
        }
    auto flat = [](float, float) { return 20.0f; };
    size_t count[4] = {}, pieces[4] = {};
    for (int density = 0; density <= 3; ++density)
    {
        const std::vector<CityInfo> city = {{"Test", 120, 100, 250, 6, density}};
        for (uint32_t seed = 1; seed <= 4; ++seed)
        {
            const Town t = generateTown(seed, blocks, flat, {}, city);
            count[density] += t.buildings.size(), pieces[density] += t.pieces.size();
            checkTown(t, blocks, 20.0f, "density");
        }
        std::printf("  density %d: %zu buildings, %zu pieces over 4 seeds of 36 blocks\n", density, count[density], pieces[density]);
    }
    CHECK(count[3] > count[2] && pieces[3] > pieces[2]);  
    CHECK(pieces[3] > pieces[0]);
}

 
 
void testStreamingReach()
{
    std::puts("streaming reach");
    std::vector<Block> blocks;
    std::vector<CityInfo> cities;
    if (!loadWorldFile("assets_baked/warzone/world_meta.txt", blocks, cities))
    {
        std::puts("  (no world_meta.txt here: skipped)");
        return;
    }
    auto flat = [](float, float) { return 20.0f; };
    const Town t = generateTown(7, blocks, flat, {}, cities);
    const StreamParams params;
    std::vector<uint32_t> pieces(t.buildings.size()), order;
    for (size_t i = 0; i < pieces.size(); ++i)
        pieces[i] = t.buildings[i].count;
    std::vector<float> dist(pieces.size());
    std::vector<uint8_t> resident(pieces.size());
    for (const CityInfo& c : cities)
    {
        for (size_t i = 0; i < pieces.size(); ++i)
            dist[i] = groundDistance(t.buildings[i].bounds, c.cx, c.cz);
        std::fill(resident.begin(), resident.end(), 0);
        const uint32_t used = chooseResident(dist, pieces, resident, params, order);
        float reach = 0.0f;
        uint32_t buildings = 0;
        for (size_t i = 0; i < resident.size(); ++i)
            if (resident[i])
                reach = std::max(reach, dist[i]), ++buildings;
        std::printf("  %-10s %4u buildings, %5u pieces resident, reach %.0f m\n", c.name.c_str(), buildings, used, reach);
        CHECK(used <= params.pieceBudget);
        CHECK(reach >= 100.0f);
    }
}
