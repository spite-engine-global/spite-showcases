 
 
 
 
 
 
 
 

namespace mn = warzone::menu;

struct MenuUi
{
    std::unique_ptr<UiScope> scope;
    Canvas canvas;
};
std::weak_ptr<MenuUi> g_menuUi;

mn::Menu g_menu = mn::makeMenu();
mn::SceneSettings g_scene;
mn::BattleSettings g_battle;
mn::BattleRamp g_ramp;
wa::Settings g_audioApplied;
std::mutex g_menuMutex;  
std::atomic<bool> g_menuOpenFlag{false};
ControlServer* g_menuControl = nullptr;
ServiceLocator* g_menuServices = nullptr;

bool menuBlocksGame() { return g_menuOpenFlag.load(std::memory_order_relaxed); }

struct MenuRuntime
{
    bool shown = false;            
    bool sceneActive = false;      
    bool sceneApplyTime = false;   
    bool sceneDirty = false;       
    bool battleManaged = false;    
    bool saveDirty = false;
    f32 saveTimer = 0.0f;
    bool qualityDirty = false;
    bool spawnPending = false;
    std::string spawnError;
    f32 fps = 0.0f, infoTimer = 0.0f;
    std::string lastAction;
    u32 changes = 0;
    warzone::quality::Settings applied;  
    std::array<int, 4> allowed{};
};
MenuRuntime g_mr;

 
struct PointerEvent
{
    UiCanvasPointerKind kind = UiCanvasPointerKind::Move;
    Vec2 local{};
    Vec2 wheel{};
    u32 region = UI_CANVAS_NO_REGION;
    u8 button = 0;
};
std::mutex g_pointerMutex;
std::vector<PointerEvent> g_pointerQueue;

enum : u32
{
    REG_TAB = 1,
    REG_CLOSE = 9,
    REG_ROW = 100,
    REG_TRACK = 200,
    REG_LEFT = 300,
    REG_RIGHT = 400
};

struct RowBox
{
    f32 y = 0, h = 0, trackX0 = 0, trackX1 = 0;
};
std::vector<RowBox> g_rowBoxes;
int g_hoverRow = -1, g_dragRow = -1;

void runOutsideSystems(std::function<void()> fn)
{
    if (g_frameStart) g_frameStart(std::move(fn));
}

 
std::filesystem::path menuFile()
{
    std::filesystem::path dir = savesDirectory();
    if (dir.empty())
        dir = resolveContentPath("user");
    return dir / "warzone_scene.json";
}

std::filesystem::path audioFile() { return menuFile().parent_path() / "warzone_audio.json"; }
bool saveAudioFile()
{
    try
    {
        std::filesystem::create_directories(audioFile().parent_path());
        std::ofstream out(audioFile(), std::ios::binary | std::ios::trunc);
        out << wa::toJson(audioSettings());
        out.close();
        return !out.fail();
    }
    catch (const std::exception& e)
    {
        SWARNING_LOG("Warzone: audio settings not saved: {}", e.what());
        return false;
    }
}
void audioEvents(const mn::Event& e)
{
    if (e.id == "audioRevert")
    {
        setAudioSettings(g_audioApplied);
        mn::audioToItems(g_menu, g_audioApplied);
        g_mr.lastAction = "audio reverted";
    }
    else
    {
        setAudioSettings(mn::itemsToAudio(g_menu));
        if (e.id == "audioApply")
        {
            if (saveAudioFile())
            {
                g_audioApplied = audioSettings();
                g_mr.lastAction = "audio applied and saved";
            }
            else g_mr.lastAction = "audio save failed";
        }
    }
}

void loadMenuFile()
{
    std::ifstream in(menuFile(), std::ios::binary);
    if (!in)
        return;
    std::stringstream text;
    text << in.rdbuf();
    if (mn::fromJson(text.str(), g_scene, g_battle))
    {
        g_mr.sceneActive = true;
        g_mr.sceneApplyTime = true;
        g_mr.sceneDirty = true;
    }
    else
        SWARNING_LOG("Warzone: {} is not a settings file, ignored", menuFile().string());
}

void saveMenuFile()
{
    try
    {
        const auto path = menuFile();
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary | std::ios::trunc) << mn::toJson(g_scene, g_battle);
    }
    catch (const std::exception& e)
    {
        SWARNING_LOG("Warzone: settings not saved: {}", e.what());
    }
}

 
Vec3 toVec3(const std::array<f32, 3>& a) { return {a[0], a[1], a[2]}; }

 
void applyScene(SystemContext& ctx, bool setTime)
{
    const mn::SceneSettings& s = g_scene;
    auto skyQuery = ctx.query<Write<WarzoneSky>>();
    for (auto& sky : skyQuery->view<Write<WarzoneSky>>())
    {
        if (setTime)
            sky.timeOfDay = std::fmod(std::fmod(s.timeOfDay, 24.0f) + 24.0f, 24.0f);
        sky.animate = s.animate;
        sky.hoursPerSecond = s.hoursPerSecond();
        sky.sunIntensity = s.sunIntensity;
        break;
    }
    {
        std::scoped_lock lock(g_lookMutex);
        g_look.hazeDensity = s.hazeDensity();
        g_look.hazeMax = s.hazeMax();
        g_look.sunGlow = s.sunGlow();
        g_look.vignette = s.vignette;
        g_look.fixHaze = s.hazeColor != 0;
        g_look.fixTint = s.grade != 0;
        if (g_look.fixHaze)
            g_look.hazeColor = toVec3(mn::HAZE_COLORS[static_cast<size_t>(s.hazeColor)]);
        if (g_look.fixTint)
            g_look.tint = toVec3(mn::GRADE_TINTS[static_cast<size_t>(s.grade)]);
    }
    auto camQuery = ctx.query<Read<WarzoneCameraRig>, Write<CameraComponent>>();
    for (auto [rig, lens] : camQuery->view<Read<WarzoneCameraRig>, Write<CameraComponent>>())
    {
        (void)rig;
        lens.exposure = s.exposure;
        break;
    }
    g_destruction.wind.dirX = std::cos(s.windDir * 0.0174533f);
    g_destruction.wind.dirZ = std::sin(s.windDir * 0.0174533f);
    g_destruction.wind.speed = s.windSpeed;
    g_destruction.wind.gust = s.gust;
}

std::array<int, 4> poolCounts()
{
    std::array<int, 4> have{};
    for (const auto& u : g_ai.units)
        ++have[static_cast<size_t>(u.s.type)];
    return have;
}

 
void ensureSpawned()
{
    if (g_mr.spawnPending || !g_ramp.running)
        return;
    std::array<f32, 4> d = mn::poolDeficit(g_ramp.target, {g_battle.mix[0], g_battle.mix[1], g_battle.mix[2], g_battle.mix[3]}, poolCounts());
    const f32 sum = d[0] + d[1] + d[2] + d[3];
    if (sum <= 0.0f)
        return;
    constexpr f32 CHUNK = 250.0f;
    if (sum > CHUNK)
        for (auto& x : d)
            x = std::floor(x * CHUNK / sum);
    const u32 add = static_cast<u32>(d[0] + d[1] + d[2] + d[3]);
    if (add == 0)
        return;
    g_mr.spawnPending = true;
    const u32 total = static_cast<u32>(g_ai.units.size()) + add;
    runOutsideSystems(
        [d, total]
        {
            const World* play = g_menuControl ? g_menuControl->world("play") : nullptr;
            u32 added = 0;
            std::string err = "no Play world";
            if (play && g_menuServices)
                err = spawnAiUnits(g_menuServices->contains<ModelSpawner>() ? &g_menuServices->get<ModelSpawner>() : nullptr, *play, total, d, 0, added);
            g_mr.spawnError = err;
            g_mr.spawnPending = false;
        });
}

void applyBattleGlobals()
{
    g_aiRespawn = g_battle.respawn;
    warzone::engageScale() = g_battle.engage * 0.01f;
}

int activeNow()
{
    int n = 0;
    for (const auto& u : g_ai.units)
        n += u.parked ? 0 : 1;
    return n;
}

void battleStart(bool fresh)
{
    g_mr.battleManaged = true;
    g_ramp.start(static_cast<int>(g_battle.size), fresh ? 0 : activeNow());
    ensureSpawned();
}

void battleStop()
{
    g_mr.battleManaged = true;
    g_ramp.stop();
}

 
void battleStep(f32 dt)
{
    if (!g_mr.battleManaged)
        return;
    ensureSpawned();
    const int active = g_ramp.step(dt, g_battle.rate);
    const auto have = poolCounts();
    g_mr.allowed = mn::splitActive(active, {g_battle.mix[0], g_battle.mix[1], g_battle.mix[2], g_battle.mix[3]}, have);
    for (size_t t = 0; t < 4; ++t)
        g_aiAllow[t] = static_cast<u32>(g_mr.allowed[t]);
}

 
bool isGraphicsId(std::string_view id)
{
    int p = -1;
    return g_menu.find(id, &p) && p == 0;
}

void graphicsEvents(const mn::Event& e)
{
    namespace q = warzone::quality;
    if (e.id == "apply")
    {
        const q::Settings s = mn::itemsToSettings(g_menu, qualityCurrent());
        g_mr.applied = s;
        runOutsideSystems([s] { qualityApply(s, true); });
        g_mr.lastAction = "applied and saved";
        return;
    }
    if (e.id == "revert")
    {
        mn::settingsToItems(g_menu, g_mr.applied);
        g_mr.qualityDirty = true;
        g_mr.lastAction = "reverted";
        return;
    }
    if (e.id == "preset")
    {
        const auto p = static_cast<q::Preset>(g_menu.find("preset")->index());
        if (p != q::Preset::Custom)
            mn::settingsToItems(g_menu, q::presetSettings(p));
    }
    g_mr.qualityDirty = true;
}

void sceneEvents(const mn::Event& e)
{
    if (e.id == "look")
    {
        mn::SceneSettings s = mn::itemsToScene(g_menu);
        mn::applyLookPreset(s, s.look);
        mn::sceneToItems(g_menu, s);
    }
    g_mr.sceneActive = true;
    g_mr.sceneDirty = true;
    g_mr.sceneApplyTime = g_mr.sceneApplyTime || e.id == "timeOfDay";
}

void battleEvents(const mn::Event& e)
{
    if (e.action)
    {
        g_battle = mn::itemsToBattle(g_menu);
        if (e.id == "startBattle")
            battleStart(false);
        else if (e.id == "stopBattle")
            battleStop();
        else if (e.id == "restartBattle")
            battleStart(true);
        else if (e.id == "startMatch")
        {
            std::scoped_lock lock(g_matchMutex);
            g_match.request.start = true;
            g_match.request.duration = g_battle.matchSeconds;
        }
        g_mr.lastAction = e.id;
        return;
    }
    g_battle = mn::itemsToBattle(g_menu);
    applyBattleGlobals();
    if (g_ramp.running)
        g_ramp.retarget(static_cast<int>(g_battle.size));
}

void handleEvents()
{
    for (const mn::Event& e : g_menu.drain())
    {
        ++g_mr.changes;
        int page = 0;
        g_menu.find(e.id, &page);
        if (page == 0)
            graphicsEvents(e);
        else if (page == 1)
            sceneEvents(e);
        else if (page == 2)
            battleEvents(e);
        else
            audioEvents(e);
        if (page == 1 || page == 2)
        {
            g_scene = mn::itemsToScene(g_menu);
            g_battle = mn::itemsToBattle(g_menu);
            if (!e.action)
            {
                g_mr.saveDirty = true;
                g_mr.saveTimer = 0.0f;
            }
        }
    }
}

 
void openMenu(SystemContext& ctx)
{
    mn::settingsToItems(g_menu, qualityCurrent());
    g_mr.applied = qualityCurrent();
    auto skyQuery = ctx.query<Read<WarzoneSky>>();
    for (const auto& sky : skyQuery->view<Read<WarzoneSky>>())
    {
        g_menu.set("timeOfDay", sky.timeOfDay, false);
        g_menu.set("animate", sky.animate ? 1.0f : 0.0f, false);
        break;
    }
    g_menuOpenFlag = true;
    if (ctx.services().contains<InputSystem>())
    {
        auto input = ctx.services().write<InputSystem>();
        input->setCursorCaptured(false);
        input->setCursorVisible(true);
    }
    {
        std::scoped_lock lock(g_tankMutex);
        g_tankCtl.gameFocus = false;
        g_tankCtl.captureRequested = false;
    }
}

void closeMenu()
{
    g_menuOpenFlag = false;
    g_hoverRow = g_dragRow = -1;
    if (g_mr.saveDirty)
    {
        saveMenuFile();
        g_mr.saveDirty = false;
    }
}

 
constexpr f32 PANEL_W = 780.0f;

std::string hardwareLine()
{
    const auto& h = qualityHardware();
    constexpr double GIB = 1073741824.0;
    return std::format("{}  |  video memory {:.1f} of {:.1f} GB  |  suggested {}", h.gpuName.empty() ? std::string("GPU") : h.gpuName,
                       static_cast<double>(h.vramUsedBytes) / GIB, static_cast<double>(h.vramBytes) / GIB, warzone::quality::presetName(h.suggested));
}

void refreshInfo()
{
    g_menu.setText("hardware", hardwareLine());
    g_menu.setText("status", qualityStatusLine());
    u32 alive[4] = {}, parked = 0;
    for (const auto& u : g_ai.units)
    {
        if (u.parked)
            ++parked;
        else if (u.slot < g_weapons.vehicles.size() && !g_weapons.vehicles[u.slot].wrecked)
            ++alive[static_cast<int>(u.s.type)];
    }
    const u32 fighting = alive[0] + alive[1] + alive[2] + alive[3];
    g_menu.setText("units", std::format("{} in the pool, {} fighting{}", g_ai.units.size(), fighting,
                                        g_ramp.running ? std::format("  (ramp {}/{})", g_ramp.active(), g_ramp.target) : std::string(g_mr.battleManaged ? "  (stopped)" : "")));
    g_menu.setText("alive", std::format("tanks {}   AMPs {}   helicopters {}   jets {}", alive[0], alive[1], alive[2], alive[3]));
    g_menu.setText("fps", std::format("{:.0f} fps   ({:.1f} ms)", g_mr.fps, g_mr.fps > 0.0f ? 1000.0f / g_mr.fps : 0.0f));
}

void drawMenu(UiCanvasPainter& p, f32 w, f32 h)
{
    const HudColors col;
    const mn::Page& page = g_menu.page();
    const int rows = static_cast<int>(page.items.size());
    const f32 rowH = std::clamp((h - 250.0f) / static_cast<f32>(std::max(rows, 1)), 18.0f, 30.0f);
    const f32 pw = std::min(PANEL_W, w - 24.0f), ph = 148.0f + rowH * static_cast<f32>(rows);
    const f32 x0 = (w - pw) * 0.5f, y0 = std::max(64.0f, (h - ph) * 0.5f + 26.0f);  
    const f32 cx0 = x0 + (page.id == "audio" ? 330.0f : 270.0f), cx1 = x0 + pw - 20.0f;
    p.rect({0, 0}, {w, h}, rgba(4, 6, 4, 0.30f));
    p.rect({x0, y0}, {pw, ph}, rgba(10, 12, 10, 0.90f), 8.0f);
    p.line({x0 + 8, y0 + 1}, {x0 + pw - 8, y0 + 1}, 2.0f, col.amber);
    p.text({x0 + 20, y0 + 12}, {300, 28}, "SETTINGS", col.white, UiCanvasAlign::Left, 22.0f);
    p.text({x0, y0 + 16}, {pw - 70, 20}, std::format("{:.0f} fps", g_mr.fps), col.dim, UiCanvasAlign::Right, 14.0f);
    p.rect({x0 + pw - 54, y0 + 12}, {40, 24}, rgba(70, 30, 24, 0.85f), 4.0f);
    p.text({x0 + pw - 54, y0 + 15}, {40, 20}, "X", col.white, UiCanvasAlign::Center, 14.0f);
    p.region(REG_CLOSE, {x0 + pw - 54, y0 + 12}, {40, 24});
     
    f32 tx = x0 + 20.0f;
    for (int t = 0; t < static_cast<int>(g_menu.pages.size()); ++t)
    {
        const bool sel = t == g_menu.tab;
        p.rect({tx, y0 + 46}, {130, 28}, sel ? rgba(60, 46, 16, 0.9f) : rgba(24, 26, 22, 0.8f), 4.0f);
        if (sel)
            p.line({tx, y0 + 74}, {tx + 130, y0 + 74}, 2.5f, col.amber);
        p.text({tx, y0 + 50}, {130, 22}, g_menu.pages[static_cast<size_t>(t)].title, sel ? col.amber : col.dim, UiCanvasAlign::Center, 15.0f);
        p.region(REG_TAB + static_cast<u32>(t), {tx, y0 + 46}, {130, 28});
        tx += 138.0f;
    }
    p.text({x0, y0 + 52}, {pw - 20, 18}, "PgUp / PgDn  page", col.dim, UiCanvasAlign::Right, 12.0f);
     
    g_rowBoxes.assign(static_cast<size_t>(rows), {});
    for (int i = 0; i < rows; ++i)
    {
        const mn::Item& it = page.items[static_cast<size_t>(i)];
        const f32 y = y0 + 86.0f + rowH * static_cast<f32>(i);
        const bool sel = i == page.selected, hover = i == g_hoverRow;
        RowBox& box = g_rowBoxes[static_cast<size_t>(i)];
        box.y = y, box.h = rowH;
        const f32 ty = y + (rowH - 18.0f) * 0.5f;
        if (sel)
        {
            p.rect({x0 + 10, y}, {pw - 20, rowH - 2}, rgba(60, 46, 16, 0.70f), 4.0f);
            p.rect({x0 + 10, y}, {3, rowH - 2}, col.amber);
        }
        else if (hover && it.selectable())
            p.rect({x0 + 10, y}, {pw - 20, rowH - 2}, rgba(80, 80, 70, 0.20f), 4.0f);
        p.region(REG_ROW + static_cast<u32>(i), {x0 + 10, y}, {pw - 20, rowH - 2});
        const u32 labelCol = it.kind == mn::Kind::Info ? col.dim : sel ? col.amber : col.white;
        p.text({x0 + 22, ty}, {240, 18}, it.label, labelCol, UiCanvasAlign::Left, 14.0f);
        const std::string value = mn::display(it);
        switch (it.kind)
        {
        case mn::Kind::Slider:
        {
            const f32 t0 = cx0 + 30.0f, t1 = cx1 - 150.0f;
            box.trackX0 = t0, box.trackX1 = t1;
            const f32 frac = it.max > it.min ? (it.value - it.min) / (it.max - it.min) : 0.0f;
            p.rect({t0, y + rowH * 0.5f - 3}, {t1 - t0, 6}, rgba(60, 60, 54, 0.9f), 3.0f);
            p.rect({t0, y + rowH * 0.5f - 3}, {(t1 - t0) * frac, 6}, sel ? col.amber : rgba(200, 150, 70, 0.8f), 3.0f);
            p.rect({t0 + (t1 - t0) * frac - 4, y + rowH * 0.5f - 8}, {8, 16}, col.white, 2.0f);
            p.region(REG_TRACK + static_cast<u32>(i), {t0 - 6, y}, {t1 - t0 + 12, rowH - 2});
            p.text({cx0, ty}, {24, 18}, "<", col.dim, UiCanvasAlign::Center, 15.0f);
            p.region(REG_LEFT + static_cast<u32>(i), {cx0, y}, {26, rowH - 2});
            p.text({t1 + 6, ty}, {24, 18}, ">", col.dim, UiCanvasAlign::Center, 15.0f);
            p.region(REG_RIGHT + static_cast<u32>(i), {t1 + 6, y}, {26, rowH - 2});
            p.text({cx1 - 112, ty}, {112, 18}, value, col.white, UiCanvasAlign::Right, 14.0f);
            break;
        }
        case mn::Kind::Choice:
            p.text({cx0, ty}, {24, 18}, "<", col.dim, UiCanvasAlign::Center, 15.0f);
            p.region(REG_LEFT + static_cast<u32>(i), {cx0, y}, {30, rowH - 2});
            p.text({cx0 + 30, ty}, {cx1 - cx0 - 60, 18}, value, it.preset && value == "Custom" ? col.dim : col.white, UiCanvasAlign::Center, 14.0f);
            p.text({cx1 - 24, ty}, {24, 18}, ">", col.dim, UiCanvasAlign::Center, 15.0f);
            p.region(REG_RIGHT + static_cast<u32>(i), {cx1 - 30, y}, {30, rowH - 2});
            break;
        case mn::Kind::Toggle:
        {
            const bool on = it.value >= 0.5f;
            p.rect({cx0 + 4, y + rowH * 0.5f - 9}, {40, 18}, on ? rgba(60, 150, 70, 0.9f) : rgba(60, 60, 54, 0.9f), 9.0f);
            p.rect({cx0 + (on ? 26.0f : 8.0f), y + rowH * 0.5f - 6}, {12, 12}, col.white, 6.0f);
            p.text({cx0 + 54, ty}, {80, 18}, value, on ? col.green : col.dim, UiCanvasAlign::Left, 14.0f);
            break;
        }
        case mn::Kind::Action:
            p.rect({cx0 + 4, y + 2}, {cx1 - cx0 - 8, rowH - 6}, sel ? rgba(50, 90, 50, 0.95f) : rgba(36, 56, 36, 0.85f), 4.0f);
            p.text({cx0 + 4, ty}, {cx1 - cx0 - 8, 18}, "ENTER", col.green, UiCanvasAlign::Center, 13.0f);
            break;
        default:
            p.text({cx0, ty}, {cx1 - cx0, 18}, value, col.dim, UiCanvasAlign::Left, 12.0f);
            break;
        }
    }
    const f32 fy = y0 + ph - 30.0f;
    p.text({x0 + 20, fy}, {pw - 40, 18}, "Up / Down select    Left / Right change (Shift x10)    Enter toggle / run    Backspace, O, F10 close", col.dim,
           UiCanvasAlign::Left, 12.0f);
    if (!g_mr.lastAction.empty())
        p.text({x0 + 20, fy - 20}, {pw - 40, 18}, g_mr.lastAction, col.green, UiCanvasAlign::Right, 12.0f);
}

 
int rowOfRegion(u32 region, u32 base)
{
    return region >= base && region < base + 100 ? static_cast<int>(region - base) : -1;
}

void setSliderFromX(int row, f32 x)
{
    if (row < 0 || row >= static_cast<int>(g_rowBoxes.size()))
        return;
    const RowBox& b = g_rowBoxes[static_cast<size_t>(row)];
    const mn::Item& it = g_menu.page().items[static_cast<size_t>(row)];
    if (it.kind != mn::Kind::Slider || b.trackX1 <= b.trackX0)
        return;
    const f32 frac = std::clamp((x - b.trackX0) / (b.trackX1 - b.trackX0), 0.0f, 1.0f);
    g_menu.set(it.id, it.min + frac * (it.max - it.min));
}

void selectRow(int row)
{
    auto& p = g_menu.page();
    if (row >= 0 && row < static_cast<int>(p.items.size()) && p.items[static_cast<size_t>(row)].selectable())
        p.selected = row;
}

void handlePointer(const PointerEvent& e)
{
    using K = UiCanvasPointerKind;
    const u32 r = e.region;
    if (e.kind == K::Move || e.kind == K::DragMove || e.kind == K::Enter)
    {
        if (g_dragRow >= 0)
        {
            setSliderFromX(g_dragRow, e.local.x);
            return;
        }
        int row = rowOfRegion(r, REG_ROW);
        for (u32 base : {REG_TRACK, REG_LEFT, REG_RIGHT})
            if (row < 0)
                row = rowOfRegion(r, base);
        g_hoverRow = row;
        selectRow(row);
        return;
    }
    if (e.kind == K::Up || e.kind == K::DragEnd || e.kind == K::Leave)
    {
        g_dragRow = -1;
        return;
    }
    if (e.kind == K::Wheel)
    {
        int row = g_hoverRow;
        if (row >= 0 && e.wheel.y != 0.0f)
        {
            selectRow(row);
            g_menu.adjust(e.wheel.y > 0.0f ? 1 : -1);
        }
        return;
    }
    if (e.kind != K::Down || e.button != 0)
        return;
    if (r >= REG_TAB && r < REG_TAB + 8)
    {
        g_menu.setTab(static_cast<int>(r - REG_TAB));
        g_hoverRow = -1;
    }
    else if (r == REG_CLOSE)
        g_menu.open = false;
    else if (const int row = rowOfRegion(r, REG_TRACK); row >= 0)
    {
        selectRow(row);
        g_dragRow = row;
        setSliderFromX(row, e.local.x);
    }
    else if (const int row = rowOfRegion(r, REG_LEFT); row >= 0)
    {
        selectRow(row);
        g_menu.adjust(-1);
    }
    else if (const int row = rowOfRegion(r, REG_RIGHT); row >= 0)
    {
        selectRow(row);
        g_menu.adjust(1);
    }
    else if (const int row = rowOfRegion(r, REG_ROW); row >= 0)
    {
        selectRow(row);
        g_menu.activate();
    }
}

 
void menuSystem(SystemContext& ctx)
{
    const f32 dt = std::min(ctx.deltaTime, 0.1f);
    std::scoped_lock lock(g_menuMutex);
    if (dt > 0.0f)
        g_mr.fps = g_mr.fps <= 0.0f ? 1.0f / dt : g_mr.fps + (1.0f / dt - g_mr.fps) * std::min(1.0f, dt * 2.0f);

     
    bool toggle = false;
    if (ctx.services().contains<InputSystem>())
    {
        const auto input = ctx.services().read<InputSystem>();
        auto pressed = [&](const char* a) { return input->wasActionPressedThisFrame(InternedString(a)); };
        toggle = pressed("menu_toggle") || pressed("menu_toggle_o");
        if (g_menu.open)
        {
            const int mult = input->getKeyState(KeyCode::LSHIFT).isPressed || input->getKeyState(KeyCode::RSHIFT).isPressed ? 10 : 1;
            if (pressed("menu_up"))
                g_menu.move(-1);
            if (pressed("menu_down"))
                g_menu.move(1);
            if (pressed("menu_left"))
                g_menu.adjust(-1, mult);
            if (pressed("menu_right"))
                g_menu.adjust(1, mult);
            if (pressed("menu_enter"))
                g_menu.activate();
            if (pressed("menu_tab_prev"))
                g_menu.tabStep(-1);
            if (pressed("menu_tab_next"))
                g_menu.tabStep(1);
            if (pressed("menu_close"))
                toggle = true;
        }
    }
    if (toggle)
        g_menu.open = !g_menu.open;
    {
        std::vector<PointerEvent> events;
        {
            std::scoped_lock pl(g_pointerMutex);
            events.swap(g_pointerQueue);
        }
        if (g_menu.open)
            for (const auto& e : events)
                handlePointer(e);
    }
    if (g_menu.open != g_mr.shown)
    {
        g_mr.shown = g_menu.open;
        if (g_menu.open)
            openMenu(ctx);
        else
            closeMenu();
    }
    if (g_menu.open)
    {
         
        auto skyQuery = ctx.query<Read<WarzoneSky>>();
        for (const auto& sky : skyQuery->view<Read<WarzoneSky>>())
        {
            if (sky.animate)
                g_menu.set("timeOfDay", sky.timeOfDay, false);
            break;
        }
        g_mr.infoTimer -= dt;
        if (g_mr.infoTimer <= 0.0f)
        {
            g_mr.infoTimer = 0.25f;
            refreshInfo();
        }
    }
    handleEvents();
    if (g_mr.qualityDirty)
    {
        g_mr.qualityDirty = false;
        const auto s = mn::itemsToSettings(g_menu, qualityCurrent());
        runOutsideSystems([s] { qualityApply(s, false); });
    }
    if (g_mr.sceneActive && g_mr.sceneDirty)
    {
        applyScene(ctx, g_mr.sceneApplyTime);
        g_mr.sceneDirty = g_mr.sceneApplyTime = false;
    }
    battleStep(dt);
    if (g_mr.saveDirty && (g_mr.saveTimer += dt) > 1.0f)
    {
        saveMenuFile();
        g_mr.saveDirty = false;
    }

    auto ui = g_menuUi.lock();
    if (!ui || !ui->canvas.isValid())
        return;
    ui->canvas.setVisible(g_menu.open);
    ui->canvas.setInteractable(g_menu.open);
    if (!g_menu.open)
        return;
    if (ctx.services().contains<InputSystem>())
    {
         
        auto input = ctx.services().write<InputSystem>();
        if (input->isCursorCaptured() || !input->isCursorVisible())
        {
            input->setCursorCaptured(false);
            input->setCursorVisible(true);
        }
    }
    const Extent2D e = g_viewExtent[1];
    const f32 w = e.width ? static_cast<f32>(e.width) : 1280.0f, h = e.height ? static_cast<f32>(e.height) : 720.0f;
    ui->canvas.setSize({w, h});
    ui->canvas.update([&](UiCanvasPainter& p) { drawMenu(p, w, h); });
}

void createMenuCanvas(const ModuleContext& context, World world)
{
    auto& services = world.services();
    if (!services.contains<UiContext>() || !services.contains<UiViewContext>())
        return;
    auto ui = std::make_shared<MenuUi>();
    ui->scope = std::make_unique<UiScope>(services.get<UiContext>());
    UiCanvasDesc desc;
    desc.rect.anchor = {0, 0};
    desc.rect.pivot = {0, 0};
    desc.rect.offset = {0, 0};
    desc.rect.size = {1280, 720};
    desc.maxPrimitives = 3000;
    ui->canvas = ui->scope->createCanvas(InternedString("warzone_menu"), desc);
    ui->canvas.setVisible(false);
    ui->canvas.setInteractable(false);
    ui->canvas.onPointer(
        [](const UiCanvasPointer& p)
        {
            std::scoped_lock lock(g_pointerMutex);
            if (g_pointerQueue.size() < 256)
                g_pointerQueue.push_back({p.kind, p.local, p.wheel, p.region, p.button});
        });
    ui->scope->commit();
    services.registerService(ui, context.owner);
    g_menuUi = ui;
}

void registerMenuSystem(World world)
{
    SystemDescriptor sys;
    sys.moduleName = MODULE;
    sys.name = "WarzoneMenu";
    sys.fn = menuSystem;
    sys.update = SystemPhaseDescriptor{.reads = {getComponentId<WarzoneCameraRig>()},
                                       .writes = {getComponentId<WarzoneSky>(), getComponentId<CameraComponent>()}};
    world.registerSystem(sys);
}

void attachMenu()
{
    std::scoped_lock lock(g_menuMutex);
    g_menu = mn::makeMenu();
    g_scene = {};
    g_battle = {};
    g_ramp = {};
    g_mr = {};
    g_menuOpenFlag = false;
    g_hoverRow = g_dragRow = -1;
    {
        std::scoped_lock pl(g_pointerMutex);
        g_pointerQueue.clear();
    }
    for (u32& a : g_aiAllow)
        a = ~0u;
    g_aiRespawn = AI_RESPAWN;
    warzone::engageScale() = 1.0f;
    setAudioSettings({});
    {
        std::ifstream in(audioFile(), std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        wa::Settings loaded;
        if (in && wa::fromJson(text.str(), loaded)) setAudioSettings(loaded);
    }
    g_audioApplied = audioSettings();
    mn::audioToItems(g_menu, g_audioApplied);
    loadMenuFile();
    mn::sceneToItems(g_menu, g_scene);
    mn::battleToItems(g_menu, g_battle);
    applyBattleGlobals();
    mn::settingsToItems(g_menu, qualityCurrent());
    g_mr.applied = qualityCurrent();
}

 
void mapMenuInput(const ModuleContext& context)
{
    g_menuControl = &context.control;
    g_menuServices = &context.services;
    if (!context.services.contains<InputSystem>())
        return;
    auto input = context.services.write<InputSystem>();
    auto press = [&](const char* a, KeyCode k) { input->mapActionToKey(InternedString(a), k, InputActionType::Press); };
    press("menu_toggle", KeyCode::F10);
    press("menu_toggle_o", KeyCode::O);
    press("menu_up", KeyCode::UP);
    press("menu_down", KeyCode::DOWN);
    press("menu_left", KeyCode::LEFT);
    press("menu_right", KeyCode::RIGHT);
    press("menu_enter", KeyCode::RETURN);
    press("menu_tab_prev", KeyCode::PAGEUP);
    press("menu_tab_next", KeyCode::PAGEDOWN);
    press("menu_close", KeyCode::BACKSPACE);
}

void unmapMenuInput(const ModuleContext& context)
{
    if (!context.services.contains<InputSystem>())
        return;
    auto input = context.services.write<InputSystem>();
    for (auto name : {"menu_toggle", "menu_toggle_o", "menu_up", "menu_down", "menu_left", "menu_right", "menu_enter", "menu_tab_prev", "menu_tab_next", "menu_close"})
        input->unmapAction(InternedString(name));
    g_menuControl = nullptr;
    g_menuServices = nullptr;
}

 
std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

 
bool parseValue(const mn::Item& it, const std::string& text, f32& out)
{
    const std::string t = lower(text);
    if (it.kind == mn::Kind::Choice)
        for (size_t i = 0; i < it.choices.size(); ++i)
            if (lower(it.choices[i]) == t)
            {
                out = static_cast<f32>(i);
                return true;
            }
    if (it.kind == mn::Kind::Toggle && (t == "on" || t == "true" || t == "off" || t == "false"))
    {
        out = t == "on" || t == "true" ? 1.0f : 0.0f;
        return true;
    }
    try
    {
        size_t used = 0;
        out = std::stof(t, &used);
        return used == t.size() && std::isfinite(out);
    }
    catch (...)
    {
        return false;
    }
}

void writeItem(IFormatWriter& result, const mn::Item& it)
{
    result.beginObject({});
    result.writeString("id", it.id);
    result.writeString("label", it.label);
    static constexpr const char* KINDS[] = {"choice", "slider", "toggle", "action", "info"};
    result.writeString("kind", KINDS[static_cast<int>(it.kind)]);
    result.writeString("value", mn::display(it));
    if (it.kind == mn::Kind::Slider || it.kind == mn::Kind::Choice || it.kind == mn::Kind::Toggle)
    {
        result.writeF32("number", it.value);
        if (it.kind == mn::Kind::Slider)
        {
            result.writeF32("min", it.min);
            result.writeF32("max", it.max);
        }
    }
    result.endObject();
}

void registerMenuCommands(const ModuleContext& context)
{
    auto& control = context.control;
    const std::string_view owner = MODULE;
    control.registerCommand(
        "warzoneMenu", owner,
        "the in-game settings menu (Play): open=true|false, tab=graphics|scene|battle|audio, select=<item id> (also switches to its tab), "
        "value=<number | choice name | on/off> sets the selected item as a user change (an option after a preset makes it Custom), "
        "press=up|down|left|right|enter|tabNext|tabPrev|close (the keys; steps= repeats, shift=true is the x10 step), apply=true saves "
        "the current Audio or Graphics page (the Apply row). Order: open, tab, select, value, press, apply. The effects land one frame later; "
        "warzoneMenuState lists the items. Keys: O / F10 open, Up/Down/Left/Right/Enter/PgUp/PgDn/Backspace",
        {optionalArg("open", ArgKind::Boolean, "show or hide the menu"), optionalArg("tab", ArgKind::String, "graphics | scene | battle | audio"),
         optionalArg("select", ArgKind::String, "item id (see warzoneMenuState)"), optionalArg("value", ArgKind::String, "value for the selected item"),
         optionalArg("press", ArgKind::String, "a key: up down left right enter tabNext tabPrev close"),
         optionalArg("steps", ArgKind::Integer, "repeat the key", "1"), optionalArg("shift", ArgKind::Boolean, "press with Shift held (x10)"),
         optionalArg("apply", ArgKind::Boolean, "run the Audio or Graphics Apply row")},
        [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
        {
            if (!control.world("play"))
                return "warzoneMenu needs Play running";
            std::scoped_lock lock(g_menuMutex);
            auto& m = g_menu;
            if (auto o = args.boolean("open"))
                m.open = *o;
            if (auto tab = args.string("tab"))
            {
                int found = -1;
                for (size_t i = 0; i < m.pages.size(); ++i)
                    if (lower(m.pages[i].id) == lower(*tab))
                        found = static_cast<int>(i);
                if (found < 0)
                    return std::format("no tab '{}' (graphics, scene, battle, audio)", *tab);
                m.setTab(found);
            }
            if (auto id = args.string("select"))
                if (!m.select(*id))
                    return std::format("no item '{}'", *id);
            if (auto text = args.string("value"))
            {
                mn::Item* it = m.selectedItem();
                f32 v = 0.0f;
                if (!it || it->kind == mn::Kind::Info || it->kind == mn::Kind::Action)
                    return "the selected item takes no value";
                if (!parseValue(*it, *text, v))
                    return std::format("'{}' is not a value of {}", *text, it->id);
                m.set(it->id, v);
            }
            if (auto key = args.string("press"))
            {
                const int steps = static_cast<int>(std::clamp<long long>(static_cast<long long>(args.integer("steps").value_or(1)), 1, 1000));
                const int mult = args.boolean("shift").value_or(false) ? 10 : 1;
                const std::string k = lower(*key);
                for (int i = 0; i < steps; ++i)
                {
                    if (k == "up")
                        m.move(-1);
                    else if (k == "down")
                        m.move(1);
                    else if (k == "left")
                        m.adjust(-1, mult);
                    else if (k == "right")
                        m.adjust(1, mult);
                    else if (k == "enter")
                        m.activate();
                    else if (k == "tabnext")
                        m.tabStep(1);
                    else if (k == "tabprev")
                        m.tabStep(-1);
                    else if (k == "close")
                        m.open = false;
                    else
                        return std::format("no key '{}'", *key);
                }
            }
            if (args.boolean("apply").value_or(false))
                m.events.push_back({m.page().id == "audio" ? "audioApply" : "apply", true});
            result.writeBool("open", m.open);
            result.writeString("tab", m.page().id);
            if (const mn::Item* it = m.selectedItem())
            {
                result.writeString("selected", it->id);
                result.writeString("value", mn::display(*it));
            }
            return {};
        });

    control.registerQuery(
        "warzoneMenuState", owner,
        "the in-game settings menu: open, page, the selected item, every page's items (id, label, kind, shown value, number, range), "
        "the graphics hardware and governor lines, the battle ramp (running, level, target, allowed per type, pool), the saved "
        "settings file, and the input state (game input focus, cursor)",
        {},
        [&control](const ControlArgs&, IFormatWriter& result) -> std::string
        {
            if (!control.world("play"))
                return "no Play world";
            std::scoped_lock lock(g_menuMutex);
            const auto& m = g_menu;
            result.writeBool("open", m.open);
            result.writeBool("blocksGame", g_menuOpenFlag.load());
            result.writeString("tab", m.page().id);
            if (!m.page().items.empty())
                result.writeString("selected", m.page().items[static_cast<size_t>(m.page().selected)].id);
            result.writeF32("fps", g_mr.fps);
            result.writeU32("changes", g_mr.changes);
            result.writeString("lastAction", g_mr.lastAction);
            result.writeString("file", menuFile().string());
            result.writeBool("fileExists", std::filesystem::exists(menuFile()));
            result.writeString("audioFile", audioFile().string());
            result.writeBool("audioFileExists", std::filesystem::exists(audioFile()));
            result.writeBool("sceneActive", g_mr.sceneActive);
            result.writeString("hardware", hardwareLine());
            result.writeString("status", qualityStatusLine());
            result.writeBool("rampRunning", g_ramp.running);
            result.writeF32("rampLevel", g_ramp.level);
            result.writeI32("rampTarget", g_ramp.target);
            result.writeBool("battleManaged", g_mr.battleManaged);
            result.writeBool("spawnPending", g_mr.spawnPending);
            result.writeString("spawnError", g_mr.spawnError);
            const auto have = poolCounts();
            result.writeFloats("pool", std::array<f32, 4>{f32(have[0]), f32(have[1]), f32(have[2]), f32(have[3])});
            result.writeFloats("allowed", std::array<f32, 4>{f32(g_mr.allowed[0]), f32(g_mr.allowed[1]), f32(g_mr.allowed[2]), f32(g_mr.allowed[3])});
            result.writeF32("respawn", g_aiRespawn);
            result.writeF32("engage", warzone::engageScale());
            {
                const auto& sc = g_look;
                std::scoped_lock ll(g_lookMutex);
                result.writeF32("hazeDensity", sc.hazeDensity);
                result.writeF32("hazeMax", sc.hazeMax);
                result.writeF32("vignette", sc.vignette);
                result.writeBool("fixHaze", sc.fixHaze);
                result.writeBool("fixTint", sc.fixTint);
                result.writeFloats("tint", std::array<f32, 3>{sc.tint.x, sc.tint.y, sc.tint.z});
            }
            result.writeF32("windDir", std::atan2(g_destruction.wind.dirZ, g_destruction.wind.dirX) * 57.2958f);
            result.writeF32("windSpeed", g_destruction.wind.speed);
            result.writeF32("gust", g_destruction.wind.gust);
            if (auto* play = control.world("play"))
            {
                auto sky = play->createQueryBuilder().buildQuery<Read<WarzoneSky>>();
                for (const auto& s : sky->view<Read<WarzoneSky>>())
                {
                    result.writeF32("skyTime", s.timeOfDay);
                    result.writeBool("skyAnimate", s.animate);
                    result.writeF32("skySpeed", s.hoursPerSecond);
                    result.writeF32("sunIntensity", s.sunIntensity);
                    break;
                }
                auto cam = play->createQueryBuilder().buildQuery<Read<CameraComponent>, Read<WarzoneCameraRig>>();
                for (auto [lens, rig] : cam->view<Read<CameraComponent>, Read<WarzoneCameraRig>>())
                {
                    (void)rig;
                    result.writeF32("exposureEv", lens.exposure);
                    result.writeBool("autoExposure", lens.autoExposure);
                    break;
                }
            }
            if (g_menuServices && g_menuServices->contains<InputSystem>())
            {
                const auto input = g_menuServices->read<InputSystem>();
                result.writeBool("inputFocus", input->hasGameInputFocus());
                result.writeBool("cursorCaptured", input->isCursorCaptured());
                result.writeBool("cursorVisible", input->isCursorVisible());
            }
            result.beginArray("pages", static_cast<u32>(m.pages.size()));
            for (const auto& pg : m.pages)
            {
                result.beginObject({});
                result.writeString("id", pg.id);
                result.beginArray("items", static_cast<u32>(pg.items.size()));
                for (const auto& it : pg.items)
                    writeItem(result, it);
                result.endArray();
                result.endObject();
            }
            result.endArray();
            return {};
        });
}
