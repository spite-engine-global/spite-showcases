 
 
 
 
 

constexpr f32 PI_F = 3.14159265f;
f32 yawForHeading(f32 deg) { return (180.0f - deg) * PI_F / 180.0f; }

const std::array<warzone::SpawnPoint, 9> SPAWNS = {{
    {"South Field", 230.0f, 330.0f, yawForHeading(0.0f), warzone::Domain::Ground},
    {"West Highway", -700.0f, -620.0f, yawForHeading(90.0f), warzone::Domain::Ground},
    {"Zavod Gate", 1735.0f, 1665.0f, yawForHeading(270.0f), warzone::Domain::Ground},
    {"Airfield Apron", 1680.0f, -1160.0f, yawForHeading(270.0f), warzone::Domain::Ground},
    {"Lesnoy East", -1815.0f, -1772.0f, yawForHeading(270.0f), warzone::Domain::Ground},
    {"Air: South", 200.0f, 1500.0f, yawForHeading(0.0f), warzone::Domain::Air},
    {"Air: Airfield", 2400.0f, -1100.0f, yawForHeading(270.0f), warzone::Domain::Air},
    {"Air: West", -2500.0f, 0.0f, yawForHeading(90.0f), warzone::Domain::Air},
    {"Air: North Hills", 0.0f, -2500.0f, yawForHeading(180.0f), warzone::Domain::Air},
}};
constexpr std::array<const char*, 4> TYPE_WEAPONS = {"125 mm HE", "30 mm autocannon", "AT missiles", "guided bombs"};

struct MatchRequest
{
    bool start = false, stop = false, exit = false;
    f32 duration = 300.0f;
    int spawnType = -1, spawnPoint = -1;
    bool deploy = false;
};

struct MatchState
{
    bool active = false, spawnScreen = false;
    warzone::MatchRules rules;
    warzone::MatchClock clock;
    int choiceType = 0, choicePoint = 0;
    int playerVehicle = -1;
    int lastPoint = -1;  
    warzone::RespawnTimer playerTimer;
    std::vector<warzone::RespawnTimer> timers;  
    bool prevLeft = false, prevRight = false, prevDeploy = false, prevStart = false;
    bool board = false;
    u32 deploys = 0, targetRespawns = 0;
    MatchRequest request;  
    std::vector<std::string> events;  
    struct KillNotice {
        std::string target, weapon;
        f32 at = -100;
        u32 burst = 0, chain = 0;
        int points = 0;
    } killNotice;
};
MatchState g_match;
std::mutex g_matchMutex;

bool matchActive() { return g_match.active; }

int playerSlotOf(warzone::VehicleType t)
{
    switch (t)
    {
    case warzone::VehicleType::Tank:
        return playerTank() ? static_cast<int>(playerTank()->vehicle) : -1;
    case warzone::VehicleType::Amp:
        return playerAmp() ? static_cast<int>(playerAmp()->vehicle) : -1;
    case warzone::VehicleType::Heli:
        return playerHeli() ? static_cast<int>(playerHeli()->vehicle) : -1;
    case warzone::VehicleType::Plane:
        return playerPlane() ? static_cast<int>(playerPlane()->vehicle) : -1;
    default:
        return -1;
    }
}

 
std::vector<warzone::Combatant> combatants(std::vector<int>* slotOf = nullptr)
{
    std::vector<warzone::Combatant> c;
    warzone::Combatant you;
    you.name = "You";
    you.player = true;
    if (g_match.playerVehicle >= 0)
        you.name = std::format("You ({})", warzone::typeName(g_weapons.vehicles[g_match.playerVehicle].type));
    if (slotOf)
        slotOf->push_back(-1);
    for (u32 i = 0; i < g_weapons.vehicles.size(); ++i)
    {
        const auto& v = g_weapons.vehicles[i];
        if (v.player)
        {
            you.kills += static_cast<int>(v.kills);
            you.deaths += static_cast<int>(v.deaths);
            you.suicides += static_cast<int>(v.suicides);
            continue;
        }
        c.push_back({v.name, static_cast<int>(v.kills), static_cast<int>(v.deaths), static_cast<int>(v.suicides), false});
        if (slotOf)
            slotOf->push_back(static_cast<int>(i));
    }
    c.insert(c.begin(), you);
    return c;
}

int playerScore()
{
    const auto c = combatants();
    return c.front().score(g_match.rules);
}

std::string matchClockText()
{
    const int s = static_cast<int>(std::ceil(g_match.clock.left));
    return std::format("{:02d}:{:02d}", s / 60, s % 60);
}

 
std::vector<warzone::V3> enemiesOf(int except)
{
    std::vector<warzone::V3> e;
    for (u32 i = 0; i < g_weapons.vehicles.size(); ++i)
    {
        const auto& v = g_weapons.vehicles[i];
        if (static_cast<int>(i) == except || v.benched)
            continue;
        if (v.respawnPending)
            e.push_back(toV3(v.respawnPos));  
        else if (!v.wrecked)
            e.push_back(v.box.centre);
    }
    return e;
}

 
int respawnAt(int vi, int preferred)
{
    auto& v = g_weapons.vehicles[vi];
    const warzone::Domain d = warzone::domainOf(v.type);
    const auto enemies = enemiesOf(vi);
    const int idx = warzone::pickSpawn(SPAWNS, d, enemies, g_match.rules.safeRadius, preferred);
    if (idx < 0)
        return -1;
    const auto& sp = SPAWNS[idx];
    const f32 ground = g_terrain.valid() ? g_terrain.height(sp.x, sp.z) : 0.0f;
    v.respawnPos = toVec(warzone::spawnPosition(sp, v.type, ground, g_match.rules));
    v.respawnYaw = sp.yaw;
    v.respawnSpeed = v.type == warzone::VehicleType::Plane ? g_match.rules.planeSpeed : 0.0f;
    v.benched = false;
    v.respawnPending = true;
    return idx;
}

void setMatchCamera(SystemContext& ctx, u32 mode)
{
    g_cameraCommand.pending = true;
    g_cameraCommand.mode = mode;
    g_cameraCommand.spot = -1;
    g_cameraCommand.target.clear();
    if (mode == 1)
        if (auto* rig = g_cameraRigEntity.isValid() ? ctx.tryGetComponent<WarzoneCameraRig>(g_cameraRigEntity) : nullptr)
        {
            rig->pivot = {200.0f, 20.0f, -150.0f};
            rig->radius = 620.0f;
            rig->height = 300.0f;
            rig->speed = 4.0f;
        }
}

void deploy(SystemContext& ctx, int type, int point)
{
    auto& m = g_match;
    const auto t = static_cast<warzone::VehicleType>(type);
    const int vi = playerSlotOf(t);
    if (vi < 0)
        return;
    if (m.playerVehicle >= 0 && m.playerVehicle != vi)
        g_weapons.vehicles[m.playerVehicle].benched = true;
    m.lastPoint = respawnAt(vi, point);
    m.events.push_back(std::format("{:.0f}s you deployed a {} at {}", m.rules.duration - m.clock.left, warzone::typeName(t), m.lastPoint >= 0 ? SPAWNS[m.lastPoint].name : "?"));
    m.playerVehicle = vi;
    m.spawnScreen = false;
    m.playerTimer = {};
    ++m.deploys;
    {
        std::scoped_lock lock(g_tankMutex);
        setActiveLocked(static_cast<Active>(type));
        g_tankCtl.scripted = false;
        g_tankCtl.hasAim = false;
    }
    setMatchCamera(ctx, 6u + static_cast<u32>(type));
    SINFO_LOG("Warzone: deployed {} at {}", warzone::typeName(t), m.lastPoint >= 0 ? SPAWNS[m.lastPoint].name : "?");
}

void startMatch(SystemContext& ctx, f32 duration)
{
    auto& m = g_match;
    m.active = true;
    m.rules.duration = duration;
    m.clock.start(duration);
    m.spawnScreen = true;
    m.playerVehicle = -1;
    m.playerTimer = {};
    m.deploys = m.targetRespawns = 0;
    m.events.clear();
    m.killNotice = {};
    m.timers.assign(g_weapons.vehicles.size(), {});
    m.choiceType = std::clamp(static_cast<int>(g_active), 0, 3);
    m.choicePoint = warzone::pickSpawn(SPAWNS, warzone::domainOf(static_cast<warzone::VehicleType>(m.choiceType)), {}, 0.0f);
    g_weapons.feed.clear();
    for (u32 i = 0; i < g_weapons.vehicles.size(); ++i)
    {
        auto& v = g_weapons.vehicles[i];
        v.kills = v.deaths = v.suicides = 0;
        if (v.player)
            v.benched = true;  
        else
        {
            v.benched = false;
            v.respawnPos = v.homePos;
            v.respawnYaw = v.homeYaw;
            v.respawnSpeed = 0.0f;
            v.respawnPending = true;
        }
    }
    {
        std::scoped_lock lock(g_tankMutex);
        g_tankCtl.scripted = false;
        g_tankCtl.hasAim = false;
        g_heliScript = HeliScript{};
        g_planeScript = PlaneScript{};
    }
    setMatchCamera(ctx, 1);
    SINFO_LOG("Warzone: deathmatch started, {:.0f} s", duration);
}

void exitMatch(SystemContext& ctx)
{
    auto& m = g_match;
    m.active = false;
    m.spawnScreen = false;
    for (auto& v : g_weapons.vehicles)
    {
        v.benched = false;
        v.respawnPos = v.homePos;
        v.respawnYaw = v.homeYaw;
        v.respawnSpeed = 120.0f;
        v.respawnPending = true;
    }
    {
        std::scoped_lock lock(g_tankMutex);
        setActiveLocked(Active::Tank);
    }
    setMatchCamera(ctx, 6);
}

void onVehicleKilled(u32 vi, u32 attacker)
{
    auto& m = g_match;
    if (vi >= g_weapons.vehicles.size()) return;
    const auto& victim = g_weapons.vehicles[vi];
    const bool byOther = attacker < g_weapons.vehicles.size() && attacker != vi;
    if (victim.player) m.killNotice = {};
    else if (byOther && g_weapons.vehicles[attacker].player && (!m.active || !m.clock.over)) {
        auto& notice = m.killNotice;
        const f32 gap = g_destruction.time - notice.at;
        notice.chain = gap <= 4 ? notice.chain + 1 : 1;
        if (gap > 1.2f) { notice.burst = 0; notice.points = 0; }
        ++notice.burst;
        notice.points += m.rules.killScore;
        notice.target = victim.name;
        notice.weapon = g_weapons.vehicles[attacker].weapon;
        notice.at = g_destruction.time;
    }
    if (!m.active || m.clock.over) return;
    m.events.push_back(std::format("{:.0f}s {} killed by {} at ({:.0f},{:.0f})", m.rules.duration - m.clock.left, victim.name,
                                   byOther ? g_weapons.vehicles[attacker].name : std::string("itself"), victim.box.centre.x, victim.box.centre.z));
    if (static_cast<int>(vi) == m.playerVehicle)
        m.playerTimer.arm(m.rules.playerRespawn);
    else if (!g_weapons.vehicles[vi].player && !g_weapons.vehicles[vi].ai && vi < m.timers.size())
        m.timers[vi].arm(m.rules.targetRespawn);
}

struct InputState
{
    bool left = false, right = false, deploy = false;
    int pick = -1;
};

void matchSystem(SystemContext& ctx)
{
    if (!g_weapons.ready)
        return;
    auto& m = g_match;
    const f32 dt = std::min(ctx.deltaTime, 0.05f);
    MatchRequest req;
    {
        std::scoped_lock lock(g_matchMutex);
        req = m.request;
        m.request = MatchRequest{};
    }
    bool startKey = false, boardKey = false;
    InputState in{};
    if (ctx.services().contains<InputSystem>())
    {
        const auto input = ctx.services().read<InputSystem>();
        startKey = input->wasActionPressedThisFrame(InternedString("match_start"));
        boardKey = input->isActionDown(InternedString("match_board"));
        in.left = input->getActionValue(InternedString("tank_left")) > 0.5f;
        in.right = input->getActionValue(InternedString("tank_right")) > 0.5f;
        in.deploy = input->isActionDown(InternedString("tank_brake")) || input->isActionDown(InternedString("tank_fire")) ||
                    input->isActionDown(InternedString("tank_fire_key"));
        for (int k = 0; k < 4; ++k)
            if (input->wasActionPressedThisFrame(InternedString(std::array{"veh_tank", "veh_amp", "veh_heli", "veh_plane"}[k])))
                in.pick = k;
    }
    if (menuBlocksGame())
    {
        startKey = boardKey = false;
        in = {};
    }
    if (req.start || (startKey && !m.active))
        startMatch(ctx, req.start ? req.duration : 300.0f);
    else if (req.exit || (startKey && m.active && m.clock.over))
        exitMatch(ctx);
    if (!m.active)
        return;
    m.board = boardKey;
    if (req.stop && !m.clock.over)
        m.clock.left = 0.0f;
    if (m.clock.step(dt) || (req.stop && !m.clock.over))
    {
        m.clock.over = true;
        m.spawnScreen = false;
        setMatchCamera(ctx, 1);
        const auto c = combatants();
        const auto order = warzone::scoreboard(c, m.rules);
        SINFO_LOG("Warzone: deathmatch over, winner {} with {}", c[order[0]].name, c[order[0]].score(m.rules));
    }
    if (m.clock.over)
        return;
     
    if (m.playerTimer.step(dt))
    {
        if (m.playerVehicle >= 0)
            g_weapons.vehicles[m.playerVehicle].benched = true;
        m.playerVehicle = -1;
        m.spawnScreen = true;
        setMatchCamera(ctx, 1);
    }
     
    for (u32 i = 0; i < m.timers.size() && i < g_weapons.vehicles.size(); ++i)
        if (m.timers[i].step(dt))
        {
            const int at = respawnAt(static_cast<int>(i), -1);
            ++m.targetRespawns;
            if (at >= 0)
                m.events.push_back(std::format("{:.0f}s {} respawned at {}", m.rules.duration - m.clock.left, g_weapons.vehicles[i].name, SPAWNS[at].name));
        }
     
    if (m.spawnScreen)
    {
        auto domainNow = [&] { return warzone::domainOf(static_cast<warzone::VehicleType>(m.choiceType)); };
        const int pick = req.spawnType >= 0 ? req.spawnType : in.pick;
        if (pick >= 0 && pick != m.choiceType)
        {
            m.choiceType = pick;
            if (SPAWNS[m.choicePoint].domain != domainNow())
                m.choicePoint = warzone::cycleSpawn(SPAWNS, domainNow(), m.choicePoint, 1);
        }
        if (req.spawnPoint >= 0 && req.spawnPoint < static_cast<int>(SPAWNS.size()) && SPAWNS[req.spawnPoint].domain == domainNow())
            m.choicePoint = req.spawnPoint;
        if (in.left && !m.prevLeft)
            m.choicePoint = warzone::cycleSpawn(SPAWNS, domainNow(), m.choicePoint, -1);
        if (in.right && !m.prevRight)
            m.choicePoint = warzone::cycleSpawn(SPAWNS, domainNow(), m.choicePoint, 1);
        if (req.deploy || (in.deploy && !m.prevDeploy))
            deploy(ctx, m.choiceType, m.choicePoint);
    }
    m.prevLeft = in.left;
    m.prevRight = in.right;
    m.prevDeploy = in.deploy;
}

 
void drawScoreboard(UiCanvasPainter& p, f32 w, f32 h, bool final)
{
    const HudColors col;
    const auto c = combatants();
    const auto order = warzone::scoreboard(c, g_match.rules);
    const f32 pw = std::min(560.0f, w - 40.0f), rowH = 26.0f;
    const f32 ph = 92.0f + rowH * static_cast<f32>(order.size());
    const Vec2 o{(w - pw) * 0.5f, std::max(20.0f, (h - ph) * 0.5f)};
    p.rect(o, {pw, ph}, rgba(8, 10, 8, 0.82f), 6.0f);
    p.text({o.x, o.y + 10}, {pw, 28}, final ? "MATCH OVER" : std::format("SCOREBOARD   {}", matchClockText()), final ? col.amber : col.white,
           UiCanvasAlign::Center, 24.0f);
    if (final)
        p.text({o.x, o.y + 40}, {pw, 20}, std::format("WINNER: {}  ({} pts)    [M] free play", c[order[0]].name, c[order[0]].score(g_match.rules)), col.green,
               UiCanvasAlign::Center, 15.0f);
    const f32 hy = o.y + 66;
    p.text({o.x + 16, hy}, {40, 18}, "#", col.dim, UiCanvasAlign::Left, 12.0f);
    p.text({o.x + 46, hy}, {260, 18}, "NAME", col.dim, UiCanvasAlign::Left, 12.0f);
    p.text({o.x + pw - 250, hy}, {60, 18}, "KILLS", col.dim, UiCanvasAlign::Right, 12.0f);
    p.text({o.x + pw - 180, hy}, {60, 18}, "DEATHS", col.dim, UiCanvasAlign::Right, 12.0f);
    p.text({o.x + pw - 90, hy}, {70, 18}, "SCORE", col.dim, UiCanvasAlign::Right, 12.0f);
    for (size_t r = 0; r < order.size(); ++r)
    {
        const auto& k = c[order[r]];
        const f32 y = hy + 20 + rowH * static_cast<f32>(r);
        if (k.player)
            p.rect({o.x + 8, y - 3}, {pw - 16, rowH - 2}, rgba(110, 170, 240, 0.22f), 3.0f);
        const u32 tc = k.player ? rgba(170, 210, 255, 0.95f) : col.white;
        p.text({o.x + 16, y}, {40, 20}, std::format("{}", r + 1), tc, UiCanvasAlign::Left, 15.0f);
        p.text({o.x + 46, y}, {300, 20}, k.name, tc, UiCanvasAlign::Left, 15.0f);
        p.text({o.x + pw - 250, y}, {60, 20}, std::format("{}", k.kills), tc, UiCanvasAlign::Right, 15.0f);
        p.text({o.x + pw - 180, y}, {60, 20}, std::format("{}", k.deaths), tc, UiCanvasAlign::Right, 15.0f);
        p.text({o.x + pw - 90, y}, {70, 20}, std::format("{}", k.score(g_match.rules)), tc, UiCanvasAlign::Right, 15.0f);
    }
}

void drawSpawnScreen(UiCanvasPainter& p, f32 w, f32 h)
{
    const HudColors col;
    const auto& m = g_match;
    p.rect({0, 0}, {w, h}, rgba(6, 8, 6, 0.5f));
    p.text({20, 14}, {w - 40, 28}, "DEATHMATCH  -  CHOOSE YOUR VEHICLE", col.white, UiCanvasAlign::Left, 22.0f);
    p.text({20, 14}, {w - 40, 28}, matchClockText(), col.amber, UiCanvasAlign::Right, 22.0f);
     
    const f32 cw = (w - 40.0f - 30.0f) / 4.0f;
    for (int i = 0; i < 4; ++i)
    {
        const auto t = static_cast<warzone::VehicleType>(i);
        const Vec2 o{20.0f + i * (cw + 10.0f), 52.0f};
        const bool sel = i == m.choiceType;
        p.rect(o, {cw, 66}, sel ? rgba(60, 46, 16, 0.85f) : rgba(14, 16, 14, 0.75f), 5.0f);
        if (sel)
        {
            p.line(o, {o.x + cw, o.y}, 2.5f, col.amber);
            p.line({o.x, o.y + 66}, {o.x + cw, o.y + 66}, 2.5f, col.amber);
        }
        p.text({o.x + 8, o.y + 6}, {cw - 16, 20}, std::format("[{}] {}", i + 1, warzone::typeName(t)), sel ? col.amber : col.white, UiCanvasAlign::Left, 16.0f);
        p.text({o.x + 8, o.y + 28}, {cw - 16, 16}, TYPE_WEAPONS[i], col.dim, UiCanvasAlign::Left, 12.0f);
        p.text({o.x + 8, o.y + 45}, {cw - 16, 16}, warzone::domainOf(t) == warzone::Domain::Air ? "SPAWNS IN THE AIR" : "SPAWNS ON THE GROUND", col.dim,
               UiCanvasAlign::Left, 11.0f);
    }
     
    const f32 ms = std::min(h - 150.0f, w * 0.52f);
    const Vec2 mo{20.0f, 130.0f};
    const f32 span = 2600.0f, cx = 200.0f, cz = -150.0f;
    p.rect(mo, {ms, ms}, rgba(16, 22, 16, 0.85f), 4.0f);
    auto toMap = [&](f32 x, f32 z) { return Vec2{mo.x + ms * (0.5f + (x - cx) / span), mo.y + ms * (0.5f + (z - cz) / span)}; };
    for (const auto& b : g_town.buildings)
    {
        const Vec2 a = toMap(b.bounds.lo.x, b.bounds.lo.z), z = toMap(b.bounds.hi.x, b.bounds.hi.z);
        p.rect(a, {std::max(1.0f, z.x - a.x), std::max(1.0f, z.y - a.y)}, rgba(150, 140, 120, 0.7f));
    }
    {
        const Vec2 a = toMap(540, -222), z = toMap(1000, -178);  
        p.rect(a, {z.x - a.x, z.y - a.y}, rgba(120, 120, 120, 0.7f));
    }
    for (const auto& v : g_weapons.vehicles)
        if (!v.benched && !v.wrecked && !v.player)
        {
            const Vec2 q = toMap(v.box.centre.x, v.box.centre.z);
            p.rect({q.x - 3, q.y - 3}, {6, 6}, col.red, 1.5f);
        }
    const warzone::Domain d = warzone::domainOf(static_cast<warzone::VehicleType>(m.choiceType));
    for (int i = 0; i < static_cast<int>(SPAWNS.size()); ++i)
    {
        const auto& sp = SPAWNS[i];
        const Vec2 q = toMap(sp.x, sp.z);
        const bool usable = sp.domain == d, sel = i == m.choicePoint;
        const u32 c = sel ? col.amber : usable ? col.white : rgba(236, 234, 222, 0.25f);
        ring(p, q, sel ? 9.0f : 6.0f, sel ? 3.0f : 1.5f, c);
        if (sp.domain == warzone::Domain::Air)
            p.line({q.x - 5, q.y}, {q.x + 5, q.y}, 1.5f, c);
        if (usable)
            p.text({q.x + 10, q.y - 8}, {160, 16}, sp.name, c, UiCanvasAlign::Left, 11.0f);
    }
    p.text({mo.x, mo.y + ms + 2}, {ms, 16}, "N  up    red: opponents", col.dim, UiCanvasAlign::Left, 11.0f);
     
    const f32 tx = mo.x + ms + 20.0f, tw = w - tx - 20.0f;
    p.text({tx, 132}, {tw, 18}, "SPAWN POINT", col.dim, UiCanvasAlign::Left, 12.0f);
    p.text({tx, 150}, {tw, 24}, SPAWNS[m.choicePoint].name, col.amber, UiCanvasAlign::Left, 20.0f);
    p.text({tx, 180}, {tw, 18}, "A / D   change spawn point", col.white, UiCanvasAlign::Left, 13.0f);
    p.text({tx, 200}, {tw, 18}, "1 - 4   change vehicle", col.white, UiCanvasAlign::Left, 13.0f);
    p.text({tx, 220}, {tw, 18}, "SPACE / F   DEPLOY", col.green, UiCanvasAlign::Left, 15.0f);
    p.text({tx, 244}, {tw, 16}, "enemy within 120 m: the safest point instead", col.dim, UiCanvasAlign::Left, 10.0f);
    p.rect({tx, 258}, {std::min(tw, 210.0f), 18}, rgba(40, 56, 36, 0.8f), 3.0f);
    p.text({tx + 8, 259}, {std::min(tw, 210.0f) - 16, 16}, "O / F10   SETTINGS", col.green, UiCanvasAlign::Left, 12.0f);
     
    const auto c = combatants();
    const auto order = warzone::scoreboard(c, m.rules);
    f32 y = 276.0f;
    p.text({tx, y}, {tw, 18}, std::format("STANDINGS   (your score {})", c.front().score(m.rules)), col.dim, UiCanvasAlign::Left, 12.0f);
    for (size_t r = 0; r < order.size() && r < 6; ++r)
    {
        y += 19.0f;
        const auto& k = c[order[r]];
        p.text({tx, y}, {tw, 18}, std::format("{}. {}   {} / {}   {}", r + 1, k.name, k.kills, k.deaths, k.score(m.rules)),
               k.player ? rgba(170, 210, 255, 0.95f) : col.white, UiCanvasAlign::Left, 13.0f);
    }
     
    f32 fy = y + 30.0f;
    for (const auto& line : g_weapons.feed)
    {
        p.text({tx, fy}, {tw, 16}, line.text, rgba(255, 220, 160, std::min(1.0f, (20.0f - line.age) / 3.0f)), UiCanvasAlign::Left, 12.0f);
        fy += 17.0f;
    }
}

void drawKillNotice(UiCanvasPainter& p, f32 w, f32 h)
{
    const auto& notice = g_match.killNotice;
    const f32 age = g_destruction.time - notice.at;
    if (!notice.burst || age < 0 || age >= 2.6f || menuBlocksGame()) return;
    const f32 enter = std::clamp(age / 0.14f, 0.0f, 1.0f);
    const f32 ease = 1 - std::pow(1 - enter, 3);
    const f32 alpha = ease * std::clamp((2.6f - age) / 0.5f, 0.0f, 1.0f);
    const f32 width = std::min(440.0f, w - 24);
    const f32 x = (w - width) * 0.5f, y = std::max(84.0f, h * 0.24f) - 12 * (1 - ease);
    const u32 gold = rgba(255, 186, 75, alpha), white = rgba(255, 246, 225, alpha);
    p.rect({x, y}, {width, 78}, rgba(10, 15, 18, alpha * 0.86f), 5);
    p.line({x, y}, {x + width * ease, y}, 3, gold);
    p.line({x + 1, y + 78}, {x + width - 1, y + 78}, 1, rgba(255, 186, 75, alpha * 0.3f));
    const Vec2 badge{x + 27, y + 33};
    const Vec2 diamond[] = {{badge.x, badge.y - 15}, {badge.x + 15, badge.y}, {badge.x, badge.y + 15},
        {badge.x - 15, badge.y}, {badge.x, badge.y - 15}};
    p.polyline(diamond, 5, 2, gold);
    p.line({badge.x - 5, badge.y - 5}, {badge.x + 5, badge.y + 5}, 2, white);
    p.line({badge.x + 5, badge.y - 5}, {badge.x - 5, badge.y + 5}, 2, white);
    const std::string title = notice.burst > 1 ? std::format("{} TARGETS DESTROYED", notice.burst) : "TARGET DESTROYED";
    p.text({x + 54, y + 12}, {width - 176, 22}, title, gold, UiCanvasAlign::Left, 15);
    p.text({x + width - 122, y + 6}, {108, 32}, std::format("+{}", notice.points), white, UiCanvasAlign::Right,
        27 + 5 * (1 - ease));
    p.text({x + 54, y + 35}, {width - 68, 18}, std::format("{:.26}  /  {:.22}", notice.target, notice.weapon),
        rgba(219, 228, 231, alpha * 0.85f), UiCanvasAlign::Left, 11);
    p.text({x + 54, y + 55}, {width - 68, 16}, notice.chain > 1 ? std::format("CHAIN x{}", notice.chain) : "ELIMINATION CONFIRMED",
        notice.chain > 1 ? rgba(104, 234, 207, alpha) : rgba(255, 186, 75, alpha * 0.65f), UiCanvasAlign::Left, 11);
    p.rect({x + 54, y + 75}, {(width - 68) * (1 - age / 2.6f), 2}, gold);
    if (age < 0.3f) {
        const f32 spread = 10 + 8 * age / 0.3f;
        for (int sx : {-1, 1}) for (int sy : {-1, 1})
            p.line({w * 0.5f + sx * spread, h * 0.5f + sy * spread},
                {w * 0.5f + sx * (spread + 7), h * 0.5f + sy * (spread + 7)}, 2,
                rgba(255, 202, 100, 1 - age / 0.3f));
    }
}

 
bool drawMatchOverlay(UiCanvasPainter& p, f32 w, f32 h)
{
    if (!g_match.board && !(g_match.active && (g_match.spawnScreen || g_match.clock.over))) drawKillNotice(p, w, h);
    if (!g_match.active)
        return false;
    if (g_match.clock.over)
    {
        drawScoreboard(p, w, h, true);
        return true;
    }
    if (g_match.spawnScreen)
    {
        drawSpawnScreen(p, w, h);
        return true;
    }
    if (g_match.board)
        drawScoreboard(p, w, h, false);
    return false;
}

bool matchOverlayOnly() { return g_match.active && (g_match.clock.over || g_match.spawnScreen); }

 
void registerMatchSystem(World world)
{
    SystemDescriptor sys;
    sys.moduleName = MODULE;
    sys.name = "WarzoneMatch";
    sys.fn = matchSystem;
    sys.update = SystemPhaseDescriptor{.reads = {getComponentId<TransformComponent>()}, .writes = {getComponentId<WarzoneCameraRig>()}};
    world.registerSystem(sys);
}

void attachMatch()
{
    std::scoped_lock lock(g_matchMutex);
    g_match = MatchState{};
}

int spawnIndex(std::string_view name)
{
    for (int i = 0; i < static_cast<int>(SPAWNS.size()); ++i)
        if (name == SPAWNS[i].name)
            return i;
    if (!name.empty() && std::all_of(name.begin(), name.end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
        return std::atoi(std::string(name).c_str());
    return -1;
}

void registerMatchCommands(const ModuleContext& context)
{
    auto& control = context.control;
    const std::string_view owner = MODULE;
    static const std::array<std::string_view, 4> types = {"tank", "amp", "heli", "plane"};
    control.registerCommand(
        "warzoneMatch", owner,
        "the deathmatch (Play only): start=true [seconds] begins a timed free-for-all at the spawn screen (the player's vehicles are "
        "benched until one is deployed, the targets come back whole at home); stop=true ends it now (scoreboard); exit=true returns to free "
        "play. Keys: M starts / leaves, Tab holds the scoreboard",
        {optionalArg("start", ArgKind::Boolean, "start a match"), optionalArg("seconds", ArgKind::Number, "match length", "300"),
         optionalArg("stop", ArgKind::Boolean, "end the match now"), optionalArg("exit", ArgKind::Boolean, "back to free play")},
        [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            if (!control.world("play") || !g_weapons.ready)
                return "warzoneMatch needs Play running";
            std::scoped_lock lock(g_matchMutex);
            if (args.boolean("start").value_or(false))
            {
                g_match.request.start = true;
                g_match.request.duration = std::clamp(number(args, "seconds").value_or(300.0f), 10.0f, 3600.0f);
            }
            g_match.request.stop = args.boolean("stop").value_or(false);
            g_match.request.exit = args.boolean("exit").value_or(false);
            result.writeBool("active", g_match.active);
            return {};
        });
    control.registerCommand(
        "warzoneSpawn", owner,
        "the spawn screen by script (Play, during a match): vehicle tank|amp|heli|plane and point (a spawn point name or index) choose, "
        "deploy=true deploys (the same rules as the keys: ground units on the ground, aircraft in the air, an unsafe point moves to the "
        "safest one)",
        {optionalArg("vehicle", ArgKind::String, "vehicle type", "tank", types), optionalArg("point", ArgKind::String, "spawn point name or index"),
         optionalArg("deploy", ArgKind::Boolean, "deploy now", "true")},
        [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            if (!control.world("play") || !g_match.active)
                return "warzoneSpawn needs a running match (warzoneMatch start=true)";
            std::scoped_lock lock(g_matchMutex);
            if (auto t = args.string("vehicle"))
                for (int i = 0; i < 4; ++i)
                    if (*t == types[i])
                        g_match.request.spawnType = i;
            if (auto pt = args.string("point"))
            {
                g_match.request.spawnPoint = spawnIndex(*pt);
                if (g_match.request.spawnPoint < 0)
                    return std::format("no spawn point '{}'", *pt);
            }
            g_match.request.deploy = args.boolean("deploy").value_or(true);
            result.writeBool("spawnScreen", g_match.spawnScreen);
            return {};
        });
    control.registerQuery(
        "warzoneMatchState", owner,
        "the deathmatch: active, over, time left, spawn screen and its choice, the player's vehicle and last spawn point, deploys, target "
        "respawns, the scoreboard (name, kills, deaths, suicides, score in order), the spawn points",
        {}, [&control](const ControlArgs&, IFormatWriter& result) -> std::string
        {
            if (!control.world("play"))
                return "no Play world";
            const auto& m = g_match;
            result.writeBool("active", m.active);
            result.writeBool("over", m.clock.over);
            result.writeF32("timeLeft", m.clock.left);
            result.writeBool("spawnScreen", m.spawnScreen);
            result.writeString("choiceVehicle", warzone::typeName(static_cast<warzone::VehicleType>(m.choiceType)));
            result.writeString("choicePoint", SPAWNS[std::clamp(m.choicePoint, 0, static_cast<int>(SPAWNS.size()) - 1)].name);
            result.writeString("playerVehicle", m.playerVehicle >= 0 ? g_weapons.vehicles[m.playerVehicle].name : std::string());
            result.writeString("lastSpawnPoint", m.lastPoint >= 0 ? SPAWNS[m.lastPoint].name : "");
            result.writeBool("playerRespawning", m.playerTimer.armed());
            result.writeU32("deploys", m.deploys);
            result.writeU32("targetRespawns", m.targetRespawns);
            result.writeU32("killNoticeBurst", m.killNotice.burst);
            result.writeU32("killNoticeChain", m.killNotice.chain);
            result.writeI32("killNoticePoints", m.killNotice.points);
            result.writeString("killNoticeTarget", m.killNotice.target);
            const auto c = combatants();
            const auto order = warzone::scoreboard(c, m.rules);
            result.beginArray("scoreboard", static_cast<u32>(order.size()));
            for (int i : order)
            {
                result.beginObject({});
                result.writeString("name", c[i].name);
                result.writeU32("kills", static_cast<u32>(c[i].kills));
                result.writeU32("deaths", static_cast<u32>(c[i].deaths));
                result.writeU32("suicides", static_cast<u32>(c[i].suicides));
                result.writeF32("score", static_cast<f32>(c[i].score(m.rules)));
                result.endObject();
            }
            result.endArray();
            u32 benched = 0;
            for (const auto& v : g_weapons.vehicles)
                benched += v.benched ? 1u : 0u;
            result.writeU32("benchedCount", benched);
            result.beginArray("events", static_cast<u32>(m.events.size()));
            for (const auto& e : m.events)
                result.writeString({}, e);
            result.endArray();
            return {};
        });
}
