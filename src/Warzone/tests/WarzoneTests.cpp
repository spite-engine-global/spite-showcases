 
 
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "src/BuildingGen.hpp"
#include "src/Destruction.hpp"
#include "src/Amp.hpp"
#include "src/Heli.hpp"
#include "src/Plane.hpp"
#include "src/Match.hpp"
#include "src/Tank.hpp"
#include "src/Ai.hpp"
#include "src/Breakup.hpp"
#include "src/Streaming.hpp"
#include "src/Vfx.hpp"
#include "src/Quality.hpp"
#include "src/Particles.hpp"
#include "src/Menu.hpp"
#include "src/Decals.hpp"
#include "src/Giga.hpp"

using namespace warzone;

namespace
{
    int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        ++g_checks;                                                                                                    \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            ++g_failures;                                                                                              \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                             \
        }                                                                                                              \
    } while (0)

    struct Built
    {
        Town town;
        Structure s;
    };

    void buildStructure(const Town& town, Structure& s, float ground)
    {
        std::vector<Box> boxes;
        std::vector<float> hp, gy;
        std::vector<uint16_t> bld;
        for (const auto& p : town.pieces)
        {
            boxes.push_back(pieceBox(p));
            hp.push_back(hitPoints(boxes.back(), PIECES[p.type].mat));
            bld.push_back(p.building);
            gy.push_back(ground);
        }
        s.build(boxes, hp, bld, gy);
    }

     
    Town smallFrame(int floors = 3)
    {
        Town t;
        Rng rng(1);
        TownBuilder tb(t, rng);
        tb.begin(Kind::House, floors);
        tb.frame({0.0f, 0.0f, 10.0f, 2, 2, floors, P_WALL_CONCRETE, 0.0f, -1, 0, false, false, 0.0f});
        tb.end();
        return t;
    }

    std::vector<uint32_t> piecesOf(const Town& t, uint16_t type, int floor)
    {
        std::vector<uint32_t> out;
        for (uint32_t i = 0; i < t.pieces.size(); ++i)
            if (t.pieces[i].type == type && (floor < 0 || t.pieces[i].floor == floor))
                out.push_back(i);
        return out;
    }

    void testSupportGraph()
    {
        std::puts("support graph");
        Town t = smallFrame();
        Structure s;
        buildStructure(t, s, 10.0f);
        CHECK(s.unsupportedAtBuild() == 0);
         
        for (uint32_t i = 0; i < t.pieces.size(); ++i)
            CHECK(s.node(i).grounded == (t.pieces[i].type == P_SLAB && t.pieces[i].floor == 0));
         
        const auto slabs1 = piecesOf(t, P_SLAB, 1);
        CHECK(slabs1.size() == 4);
        for (uint32_t i : slabs1)
        {
            uint32_t cols = 0, walls = 0;
            for (uint32_t sp : s.supporters(i))
            {
                cols += t.pieces[sp].type == P_COLUMN ? 1 : 0;
                walls += t.pieces[sp].type == P_WALL_CONCRETE ? 1 : 0;
                CHECK(t.pieces[sp].floor == 0);
            }
            CHECK(cols == 4);
            CHECK(walls >= 1);
            CHECK(s.node(i).needed == 2);
        }
         
        for (uint32_t i : piecesOf(t, P_COLUMN, 1))
            for (uint32_t sp : s.supporters(i))
                CHECK(t.pieces[sp].type == P_SLAB && t.pieces[sp].floor == 1);

         
        std::vector<Removal> out;
        s.remove(piecesOf(t, P_COLUMN, 0)[0], 0);
        for (uint32_t f = 1; f < 40; ++f)
            s.tick(f, out);
        CHECK(out.empty());

         
         
        for (uint32_t i = 0; i < t.pieces.size(); ++i)
            if (t.pieces[i].floor == 0 && t.pieces[i].type != P_SLAB)
                s.remove(i, 100);
        std::vector<uint32_t> firstFall(8, 1u << 30), lastFall(8, 0);
        for (uint32_t f = 100; f < 400; ++f)
        {
            out.clear();
            s.tick(f, out);
            for (const auto& r : out)
            {
                CHECK(!r.destroyed);
                const auto& p = t.pieces[r.piece];
                const int level = p.type == P_SLAB ? p.floor * 2 - 1 : p.floor * 2;  
                firstFall[level] = std::min(firstFall[level], f);
                lastFall[level] = std::max(lastFall[level], f);
            }
        }
        uint32_t standing = 0;
        for (uint32_t i = 0; i < t.pieces.size(); ++i)
            standing += s.node(i).state == PieceState::Intact ? 1 : 0;
        CHECK(standing == 4);  
        CHECK(firstFall[1] > 100);
        for (int level = 2; level <= 5; ++level)
            CHECK(firstFall[level] > firstFall[level - 1]);
        CHECK(lastFall[5] - firstFall[1] >= 15);  
        CHECK(s.pending() == 0);
    }

    void testFallBudget()
    {
        std::puts("per-frame fall budget");
        Town t = smallFrame(4);
        Structure s;
        s.params.maxFallsPerFrame = 3;
        s.params.waveJitter = 0;
        buildStructure(t, s, 10.0f);
        for (uint32_t i = 0; i < t.pieces.size(); ++i)
            if (t.pieces[i].floor == 0 && t.pieces[i].type != P_SLAB)
                s.remove(i, 0);
        std::vector<Removal> out;
        uint32_t maxPerFrame = 0, total = 0;
        for (uint32_t f = 0; f < 2000; ++f)
        {
            out.clear();
            s.tick(f, out);
            maxPerFrame = std::max<uint32_t>(maxPerFrame, static_cast<uint32_t>(out.size()));
            total += static_cast<uint32_t>(out.size());
        }
        CHECK(maxPerFrame == 3);
        CHECK(total > 40);
    }

    void testDamage()
    {
        std::puts("damage and spreading");
        Town t = smallFrame();
         
        uint32_t col = 0;
        for (uint32_t i : piecesOf(t, P_COLUMN, 0))
            if (t.pieces[i].pos.x == 0.0f && t.pieces[i].pos.z == 0.0f)
                col = i;
        const V3 at = t.pieces[col].pos + V3{0, 1.5f, 0};

        Structure a;
        buildStructure(t, a, 10.0f);
        const float hpCol = a.node(col).maxHp;
        CHECK(hpCol > 30.0f && hpCol < 200.0f);
         
        CHECK(hitPoints({{0, 0, 0}, {4, 3.2f, 0.35f}}, Material::Concrete) > hitPoints({{0, 0, 0}, {0.55f, 3.2f, 0.5f}}, Material::Concrete));
        CHECK(hitPoints({{0, 0, 0}, {4, 3.2f, 0.35f}}, Material::Concrete) > hitPoints({{0, 0, 0}, {4, 3.2f, 0.35f}}, Material::Masonry));

         
        std::vector<Removal> out;
        a.params.looseShare = 10.0f;  
        a.applyBlast({at, 1.0f, hpCol * 1.2f, 10.0f}, 0, out);
        CHECK(out.size() >= 1);
        bool colDestroyed = false;
        for (auto& r : out)
            colDestroyed = colDestroyed || (r.piece == col && r.destroyed);
        CHECK(colDestroyed);
        for (uint32_t i : piecesOf(t, P_SLAB, 3))
            CHECK(a.node(i).hp == a.node(i).maxHp);

         
        auto neighbourDamage = [&](float spread)
        {
            Structure s;
            s.params.spread = spread;
            s.params.looseShare = 10.0f;
            buildStructure(t, s, 10.0f);
            std::vector<Removal> o;
            s.applyBlast({at, 0.5f, hpCol * 6.0f, 10.0f}, 0, o);
            float lost = 0;
            for (uint32_t n : s.dependents(col))
                lost += s.node(n).maxHp - s.node(n).hp;
            for (uint32_t n : s.supporters(col))
                lost += s.node(n).maxHp - s.node(n).hp;
            return lost;
        };
        const float withSpread = neighbourDamage(0.5f), without = neighbourDamage(0.0f);
        CHECK(withSpread > 0.0f);
        CHECK(without == 0.0f);
         
        Structure b;
        buildStructure(t, b, 10.0f);
        b.applyBlast({{-10.0f, 11.0f, -10.0f}, 3.0f, 10000.0f, 10.0f}, 0, out);
        uint32_t damaged = 0;
        for (uint32_t i = 0; i < t.pieces.size(); ++i)
            damaged += b.node(i).hp < b.node(i).maxHp ? 1 : 0;
        CHECK(damaged == 0);
         
        Structure c;
        buildStructure(t, c, 10.0f);
        out.clear();
        const uint32_t wall = piecesOf(t, P_WALL_CONCRETE, 1)[0];
        const float hpWall = c.node(wall).maxHp;
        const V3 wc = c.node(wall).box.centre();
        c.params.spreadDepth = 0;
        c.applyBlast({wc + V3{0, 0, -0.3f}, 0.7f, hpWall * 0.6f, 10.0f}, 0, out);
        bool loose = false;
        for (auto& r : out)
            if (r.piece == wall)
                loose = !r.destroyed && len(r.velocity) > 0.5f;
        CHECK(loose);
    }

    void testDebrisCap()
    {
        std::puts("debris lifecycle and cap");
        DebrisSet d;
        d.params.cap = 5;
        d.params.sleepToStatic = 0.5f;
        d.params.staticSeconds = 3.0f;
        d.params.fadeSeconds = 1.0f;
        d.params.minDynamicSeconds = 0.0f;
        std::vector<DebrisSet::Event> ev;
        for (uint64_t id = 100; id < 108; ++id)
            d.add(id, ev);
        CHECK(ev.size() == 3);
        for (size_t i = 0; i < ev.size(); ++i)
        {
            CHECK(ev[i].id == 100 + i);  
            CHECK(ev[i].to == DebrisState::Fading);
        }
        CHECK(d.live() == 5);
         
        auto awake = [](uint64_t id) { return id >= 105; };
        ev.clear();
        for (int step = 0; step < 6; ++step)  
            d.update(0.1f, awake, ev);
        std::set<uint64_t> toStatic, gone;
        for (auto& e : ev)
        {
            if (e.to == DebrisState::Static)
                toStatic.insert(e.id);
            if (e.to == DebrisState::Gone)
                gone.insert(e.id);
        }
        CHECK(toStatic == (std::set<uint64_t>{103, 104}));
        CHECK(gone.empty());  
        CHECK(d.count(DebrisState::Fading) == 3);
        ev.clear();
        for (int step = 0; step < 5; ++step)  
            d.update(0.1f, awake, ev);
        CHECK(d.count(DebrisState::Fading) == 0);
        CHECK(d.items().size() == 5);
        for (int step = 0; step < 45; ++step)  
            d.update(0.1f, awake, ev);
        CHECK(d.count(DebrisState::Gone) == 0);  
         
        d.params.maxDynamicSeconds = 2.0f;
        ev.clear();
        for (int step = 0; step < 200; ++step)
            d.update(0.1f, awake, ev);
        CHECK(d.items().empty());
         
        DebrisSet m;
        m.params.minDynamicSeconds = 1.5f;
        m.params.sleepToStatic = 0.5f;
        std::vector<DebrisSet::Event> evm;
        m.add(1, evm);
        for (int step = 0; step < 15; ++step)  
            m.update(0.1f, [](uint64_t) { return false; }, evm);
        CHECK(m.count(DebrisState::Dynamic) == 1);
        for (int step = 0; step < 6; ++step)
            m.update(0.1f, [](uint64_t) { return false; }, evm);
        CHECK(m.count(DebrisState::Static) == 1);
         
        DebrisSet e;
        e.params.cap = 50;
        std::vector<DebrisSet::Event> ev2;
        for (uint64_t id = 0; id < 1000; ++id)
        {
            e.add(id, ev2);
            CHECK(e.live() <= 50);
        }
    }

    void testGrid()
    {
        std::puts("spatial grid vs brute force");
        Rng rng(3);
        SpatialGrid g(8.0f);
        std::vector<Box> boxes;
        for (uint32_t i = 0; i < 500; ++i)
        {
            const V3 c{rng.range(-100, 100), rng.range(0, 30), rng.range(-100, 100)};
            const V3 h{rng.range(0.1f, 6), rng.range(0.1f, 3), rng.range(0.1f, 6)};
            boxes.push_back({c - h, c + h});
            g.insert(i, boxes.back());
        }
        for (int q = 0; q < 50; ++q)
        {
            const V3 c{rng.range(-100, 100), rng.range(0, 30), rng.range(-100, 100)};
            const float r = rng.range(1, 20);
            std::vector<uint32_t> got, want;
            g.querySphere(c, r, got);
            for (uint32_t i = 0; i < boxes.size(); ++i)
                if (distance(boxes[i], c) <= r)
                    want.push_back(i);
            CHECK(got == want);
        }
        g.remove(7);
        std::vector<uint32_t> got;
        g.query(boxes[7], got);
        CHECK(std::find(got.begin(), got.end(), 7u) == got.end());
    }

    void testGenerator()
    {
        std::puts("town generator");
        std::vector<Block> blocks;
        for (int i = 0; i < 7; ++i)
            for (int j = 0; j < 6; ++j)
                blocks.push_back({-41.0f + i * 70.0f, -341.0f + j * 56.0f, 11.0f + i * 70.0f, -303.0f + j * 56.0f});
        auto ground = [](float, float) { return 19.92f; };
        const Town a = generateTown(7, blocks, ground), b = generateTown(7, blocks, ground), c = generateTown(8, blocks, ground);
        CHECK(a.pieces.size() == b.pieces.size());
        bool same = a.pieces.size() == b.pieces.size();
        for (size_t i = 0; same && i < a.pieces.size(); ++i)
            same = a.pieces[i].type == b.pieces[i].type && a.pieces[i].pos.x == b.pieces[i].pos.x &&
                   a.pieces[i].pos.y == b.pieces[i].pos.y && a.pieces[i].pos.z == b.pieces[i].pos.z;
        CHECK(same);
        CHECK(a.pieces.size() != c.pieces.size() || a.buildings.size() != c.buildings.size());
        int kinds[KIND_COUNT] = {};
        for (const auto& bl : a.buildings)
        {
            ++kinds[static_cast<int>(bl.kind)];
            CHECK(bl.count > 0);
            CHECK(bl.parts >= 1);
        }
        std::printf("  seed 7: %zu buildings (%d houses, %d apartments, %d towers, %d factories, %d yards), %zu pieces\n",
                    a.buildings.size(), kinds[0], kinds[1], kinds[2], kinds[3], kinds[4], a.pieces.size());
        CHECK(kinds[2] >= 1);
        CHECK(a.pieces.size() < 40000);
         
        for (const auto& p : a.pieces)
            CHECK(p.pos.y >= 19.9f);
         
        Structure s;
        buildStructure(a, s, 19.92f);
        CHECK(s.unsupportedAtBuild() == 0);
        if (s.unsupportedAtBuild())
            for (uint32_t i = 0; i < a.pieces.size(); ++i)
                if (!s.node(i).grounded && s.supporters(i).empty())
                {
                    std::printf("  unsupported: %s in %s at (%.2f %.2f %.2f)\n", std::string(PIECES[a.pieces[i].type].slug).c_str(),
                                a.buildings[a.pieces[i].building].name.c_str(), a.pieces[i].pos.x, a.pieces[i].pos.y, a.pieces[i].pos.z);
                    break;
                }
    }
#include "WeaponTests.inl"
#include "AmpHeliTests.inl"
#include "PlaneTests.inl"
#include "MatchTests.inl"
#include "AiTests.inl"
#include "BreakupTests.inl"
#include "PlanTests.inl"
#include "StreamingTests.inl"
#include "TownTests.inl"
#include "VfxTests.inl"
#include "QualityTests.inl"
#include "MenuTests.inl"
#include "AudioMixTests.inl"
#include "ParticleTests.inl"
#include "DecalTests.inl"
}  

#include "GigaTests.inl"

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--perf-particles") == 0)  
    {
        perfParticles();
        return 0;
    }
    testSupportGraph();
    testFallBudget();
    testDamage();
    testDebrisCap();
    testGrid();
    testGenerator();
    testHitTests();
    testBallistics();
    testGuidance();
    testDamageModel();
    testTankModel();
    testPieceRaycast();
    testAutocannon();
    testHeliFlight();
    testLock();
    testHeliMissile();
    testPlaneFlight();
    testPlaneTurn();
    testBombTargets();
    testBombPrediction();
    testSpawnRules();
    testScoring();
    testMatchClock();
    testUnitGrid();
    testNearestThree();
    testNavigation();
    testTargetPick();
    testGroundBrain();
    testHeliBrain();
    testPlaneBrain();
    testPieceFlight();
    testWorldZones();
    testBreakupRules();
    testDenseCity();
    testWorldPlan();
    testSyntheticWorlds();
    testDensityLevels();
    testStreamingChoice();
    testBuildingGrid();
    testDetailChoice();
    testTier();
    testStandIns();
    testStreamingReach();
    testVfxTiers();
    testVfxExplosion();
    testVfxGovernor();
    testVfxWind();
    testVfxWreck();
    testVfxBudget();
    testQualityPresets();
    testQualityHardware();
    testQualitySteps();
    testQualityFile();
    testQualityGovernorDown();
    testQualityGovernorUp();
    testQualityGovernorNoise();
    testQualityGovernorMemory();
    const int checksBeforeMenus = g_checks;
    const int beforeAudio = g_checks;
    testAudioDefaults();
    testAudioScaling();
    testAudioBlastMinimum();
    testAudioFalloff();
    testAudioDelay();
    testAudioClamping();
    testAudioPersistence();
    testAudioMenu();
    std::printf("audio: 8 scenarios, %d executed checks (40 distinct CHECK statements)\n", g_checks - beforeAudio);
    testMenuItems();
    testMenuNavigation();
    testMenuPresetCustom();
    testMenuScene();
    testMenuPersistence();
    testBattleRamp();
    testSpawnMix();
    testSplitActive();
    std::printf("menus: %d checks\n", g_checks - checksBeforeMenus);
    testParticleAtlasSpec();
    testParticleCurves();
    testParticleCapBudget();
    testParticleMotion();
    testParticleDeterminism();
    testParticleTrails();
    testParticleEmitters();
    testParticlePacking();
    testParticleGather();
    testParticleQuality();
    testParticleRecipes();
    testNukeParticles();
    testDecals();
    testGiga();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
