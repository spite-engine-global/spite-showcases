void testGiga()
{
    using namespace warzone::giga;
    const int before = g_checks;
    CHECK(damage(true, 1000000) == 0);
    CHECK(damage(false, 250) == 250);
    CHECK(damage(false, -10) == 0);

    CHECK(nukeDamage(0) == NUKE_DAMAGE);
    CHECK(nukeDamage(NUKE_RADIUS) == 0);
    CHECK(nukeDamage(NUKE_RADIUS + 1) == 0);
    CHECK(nukeDamage(20) > nukeDamage(100));
    CHECK(nukeDamage(100) > 0);

    CHECK(ready(0));
    CHECK(!ready(COOLDOWN));
    CHECK(tick(1, 2) == 0);
    CHECK(tick(8, 1) == 7);
    CHECK(tick(1, -1) == 1);

    std::array<Target, 5> targets{{
        {{3, 0, 0}, false, true, true}, {{4, 0, 0}, true, false, true},
        {{6, 0, 0}, true, true, true}, {{5, 0, 0}, true, true, true},
        {{2, 0, 0}, true, true, false}
    }};
    CHECK(pick({}, targets) == 3);
    targets[3].available = false;
    CHECK(pick({}, targets) == 2);
    CHECK(pick({100, 0, 0}, targets) == -1);
    std::array<Target, 1> edge{{{{PICKUP_RANGE, 0, 0}, true, true, true}}};
    CHECK(pick({}, edge) == 0);
    edge[0].pos.x += 0.01f;
    CHECK(pick({}, edge) == -1);

    const V3 velocity = throwVelocity({0, 0, 2});
    CHECK(std::abs(velocity.z - 110) < 0.001f);
    CHECK(velocity.y == 20);
    CHECK(velocity.x == 0);

    const V3 offset = carryOffset(2);
    CHECK(offset.y > 4.7f && offset.y < 4.9f);
    CHECK(offset.x == 0 && offset.z == 0);
    CHECK(RUN_SPEED > WALK_SPEED && WALK_SPEED > 8);

    warzone::menu::SceneSettings scene;
    warzone::menu::BattleSettings battle;
    battle.giga = 1;
    const auto json = warzone::menu::toJson(scene, battle);
    battle.giga = 0;
    CHECK(warzone::menu::fromJson(json, scene, battle));
    CHECK(battle.giga == 1);
    std::printf("GIGA: 7 scenarios, %d checks\n", g_checks - before);
}
