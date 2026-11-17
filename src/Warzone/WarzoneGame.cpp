 
 
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "application/input/InputSystem.hpp"
#include "animation/AnimationComponents.hpp"
#include "src/Giga.hpp"
#include "assetruntime/AssetComponents.hpp"
#include "assetruntime/ModelSpawner.hpp"
#include "audio/AudioComponents.hpp"
#include "audio/AudioWorld.hpp"
#include "base/ContentRoot.hpp"
#include "base/Logging.hpp"
#include "base/Machine.hpp"
#include "control/ControlServer.hpp"
#include "ecs/Query.hpp"
#include "engine/Module.hpp"
#include "physics/PhysicsComponents.hpp"
#include "physics/PhysicsWorld.hpp"
#include "rendering/IRenderer.hpp"
#include "rendering/RenderPassModule.hpp"
#include "rendering/RenderingSingletons.hpp"
#include "renderruntime/LightComponents.hpp"
#include "renderruntime/RenderView.hpp"
#include "renderruntime/RenderingComponents.hpp"
#include "serialization/FormatWalk.hpp"
#include "serialization/JsonFormat.hpp"
#include "ui/UiCanvas.hpp"
#include "ui/UiScope.hpp"
#include "ui/UiWidgets.hpp"
#include "uiview/UiStyleSheet.hpp"
#include "uiview/UiViewContext.hpp"
#include "world/EntityName.hpp"
#include "world/EntityTransformManager.hpp"
#include "world/TransformComponents.hpp"

#include "src/Amp.hpp"
#include "src/BuildingGen.hpp"
#include "src/Decals.hpp"
#include "src/Destruction.hpp"
#include "src/Heli.hpp"
#include "src/Plane.hpp"
#include "src/Match.hpp"
#include "src/Tank.hpp"
#include "src/Terrain.hpp"
#include "src/Weapons.hpp"
#include "src/Ai.hpp"
#include "src/Breakup.hpp"
#include "src/Streaming.hpp"
#include "src/Quality.hpp"
#include "src/GpuMemory.hpp"
#include "assetruntime/AssetRuntime.hpp"
#include "asset/AssetManifest.hpp"
#include "src/Menu.hpp"
#include "src/Particles.hpp"

namespace spite
{
     
    struct WarzoneSky : IComponent
    {
        f32 timeOfDay = 9.0f;        
        f32 hoursPerSecond = 0.0f;   
        f32 maxElevation = 52.0f;    
        f32 sunIntensity = 5.0f;     
        bool animate = false;
        SPITE_SCHEMA(WarzoneSky, timeOfDay, hoursPerSecond, maxElevation, sunIntensity, animate)
    };

     
    struct WarzoneCameraRig : IComponent
    {
        u32 mode = 2;
        Vec3 pivot = {200.0f, 20.0f, -150.0f};
        f32 radius = 700.0f;
        f32 height = 220.0f;
        f32 speed = 1.0f;      
        f32 phase = 0.0f;      
        f32 smoothing = 2.5f;  
        f32 zoom = 1.0f, smoothZoom = 1.0f;
        SPITE_SCHEMA(WarzoneCameraRig, mode, pivot, radius, height, speed, phase, smoothing, zoom)
    };

     
    struct WarzoneTown : IComponent
    {
        u32 seed = 7;
        u32 generator = 1;
        SPITE_SCHEMA(WarzoneTown, seed, generator)
    };

     
    struct WarzoneBuilding : IComponent
    {
        u32 id = 0;
        u32 kind = 0;
        u32 floors = 0;
        SPITE_SCHEMA(WarzoneBuilding, id, kind, floors)
    };

     
    struct WarzonePiece : IComponent
    {
        u32 building = 0;
        u32 type = 0;
        u32 part = 0;
        u32 floor = 0;
        u32 index = 0;
        SPITE_SCHEMA(WarzonePiece, building, type, part, floor, index)
    };

     
    struct WarzoneTank : IComponent
    {
        u32 role = 0;
        f32 hp = 1000.0f;
        SPITE_SCHEMA(WarzoneTank, role, hp)
    };

     
    struct WarzoneAmp : IComponent
    {
        u32 role = 0;
        f32 hp = 700.0f;
        SPITE_SCHEMA(WarzoneAmp, role, hp)
    };

     
    struct WarzoneHeli : IComponent
    {
        u32 role = 0;
        f32 hp = 700.0f;
        SPITE_SCHEMA(WarzoneHeli, role, hp)
    };

     
    struct WarzonePlane : IComponent
    {
        u32 role = 0;
        f32 hp = 600.0f;
        SPITE_SCHEMA(WarzonePlane, role, hp)
    };
}  

namespace
{
    using namespace spite;

    constexpr const char* MODULE = "WarzoneGame";
    constexpr f32 PI = 3.14159265358979f;

     
    warzone::Heightfield g_terrain;

    struct Look
    {
        Vec3 sunDir = {0.3f, 0.6f, -0.7f};  
        Vec3 sunColor = {1.0f, 0.95f, 0.9f};
        Vec3 hazeColor = {0.62f, 0.70f, 0.80f};
        f32 hazeDensity = 0.00012f;   
        f32 hazeFalloff = 160.0f;     
        f32 hazeBase = 15.0f;
        f32 hazeMax = 0.75f;
        f32 sunGlow = 0.55f;
        f32 skyDistance = 9000.0f;
        Vec3 tint = {1.0f, 1.0f, 1.0f};
        f32 vignette = 0.22f;
        bool hazeOn = true;
        bool gradeOn = true;
        bool autoHaze = true;  
        bool fixHaze = false;  
        bool fixTint = false;
    };
    Look g_look;
    std::mutex g_lookMutex;
    u64 g_playLookFrame = 0;  
    u64 g_frame = 0;

    struct Spot
    {
        const char* name;
        Vec3 eye;
        Vec3 target;
    };
     
     
    const std::array<Spot, 16> SPOTS = {{
        {"overview", {-5200.0f, 2300.0f, 5200.0f}, {0.0f, 0.0f, 0.0f}},
        {"town", {-300.0f, 160.0f, 450.0f}, {200.0f, 30.0f, -150.0f}},
        {"airstrip", {1350.0f, 90.0f, -820.0f}, {2100.0f, 28.0f, -1100.0f}},
        {"fields", {-600.0f, 80.0f, 1300.0f}, {-1100.0f, 25.0f, 900.0f}},
        {"hills", {300.0f, 320.0f, -2000.0f}, {0.0f, 150.0f, -3200.0f}},
        {"lake", {-700.0f, 70.0f, 0.0f}, {-1250.0f, 25.0f, -650.0f}},
        {"highway", {-1000.0f, 60.0f, -400.0f}, {-200.0f, 30.0f, -250.0f}},
        {"sunrise", {-3000.0f, 250.0f, 2500.0f}, {1500.0f, 50.0f, -1000.0f}},
        {"tower", {60.0f, 70.0f, 40.0f}, {200.0f, 35.0f, -150.0f}},
        {"downtown", {150.0f, 90.0f, 60.0f}, {230.0f, 30.0f, -200.0f}},
        {"zavod", {1950.0f, 130.0f, 2100.0f}, {2300.0f, 25.0f, 1700.0f}},
        {"voskhod", {1440.0f, 135.0f, 640.0f}, {1820.0f, 28.0f, 220.0f}},
        {"krasnoe", {30.0f, 100.0f, 3330.0f}, {400.0f, 21.0f, 2970.0f}},
        {"lesnoy", {-1950.0f, 130.0f, -1400.0f}, {-2300.0f, 30.0f, -1800.0f}},
        {"mirny", {-1500.0f, 110.0f, 2500.0f}, {-1900.0f, 25.0f, 2100.0f}},
        {"country", {0.0f, 900.0f, 2800.0f}, {0.0f, 30.0f, 0.0f}},
    }};
     
    const std::array<Vec3, 10> FLYBY = {{
        {-2600.0f, 380.0f, -1300.0f},
        {-1300.0f, 300.0f, -600.0f},
        {100.0f, 260.0f, 150.0f},
        {1500.0f, 300.0f, -900.0f},
        {2700.0f, 380.0f, -2200.0f},
        {3300.0f, 420.0f, 600.0f},
        {2300.0f, 330.0f, 2100.0f},
        {600.0f, 360.0f, 2600.0f},
        {-1800.0f, 330.0f, 2500.0f},
        {-3200.0f, 400.0f, 500.0f},
    }};

    f32 smoothstep(f32 e0, f32 e1, f32 x)
    {
        const f32 t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    }

    Vec3 mix(const Vec3& a, const Vec3& b, f32 t) { return a * (1 - t) + b * t; }

     
    struct SunState
    {
        f32 azimuth;    
        f32 elevation;  
        Quat rotation;  
        Vec3 color;
        f32 intensity;
        Vec3 toSun;
    };

    SunState sunFor(const WarzoneSky& sky)
    {
        SunState s{};
        const f32 h = sky.timeOfDay;
        s.azimuth = 90.0f + (h - 6.0f) * 15.0f;
        s.elevation = sky.maxElevation * std::sin(PI * (h - 6.0f) / 12.0f);
        const f32 yaw = toRadians(180.0f - s.azimuth);
        const f32 pitch = toRadians(-s.elevation);
        s.rotation = normalize(multiply(fromAxisAngle({0, 1, 0}, yaw), fromAxisAngle({1, 0, 0}, pitch)));
        const Vec3 warm = {1.0f, 0.42f, 0.18f}, mid = {1.0f, 0.74f, 0.50f}, day = {1.0f, 0.95f, 0.89f};
        s.color = s.elevation < 12.0f ? mix(warm, mid, smoothstep(-1.0f, 12.0f, s.elevation))
                                      : mix(mid, day, smoothstep(12.0f, 40.0f, s.elevation));
        s.intensity =
            sky.sunIntensity * smoothstep(-3.0f, 4.0f, s.elevation) * (0.35f + 0.65f * smoothstep(0.0f, 35.0f, s.elevation));
        s.toSun = -rotate(s.rotation, {0, 0, -1});
        return s;
    }

    void updateLookFromSun(const SunState& s)
    {
        std::scoped_lock lock(g_lookMutex);
        g_look.sunDir = s.toSun;
        g_look.sunColor = s.color;
        if (g_look.autoHaze)
        {
             
            const Vec3 night = {0.10f, 0.13f, 0.20f}, dawn = {0.86f, 0.66f, 0.52f}, day = {0.66f, 0.73f, 0.82f};
            const f32 e = s.elevation;
            if (!g_look.fixHaze)
                g_look.hazeColor = e < 8.0f ? mix(night, dawn, smoothstep(-6.0f, 3.0f, e)) : mix(dawn, day, smoothstep(8.0f, 30.0f, e));
            if (!g_look.fixTint)
                g_look.tint = e < 15.0f ? mix(Vec3{1.04f, 0.97f, 0.92f}, Vec3{1.0f, 1.0f, 1.0f}, smoothstep(5.0f, 15.0f, e))
                                        : Vec3{1.0f, 0.995f, 0.985f};
        }
    }

     
    void skyReadSystem(SystemContext& context)
    {
        ++g_frame;
        if (g_frame - g_playLookFrame < 4)
            return;  
        auto query = context.query<Read<WarzoneSky>>();
        for (const auto& sky : query->view<Read<WarzoneSky>>())
        {
            updateLookFromSun(sunFor(sky));
            break;
        }
    }

     
    void skyDriveSystem(SystemContext& context)
    {
        auto query = context.query<Write<WarzoneSky>, Write<TransformComponent>, Write<DirectionalLightComponent>>();
        for (auto [sky, transform, light] :
             query->view<Write<WarzoneSky>, Write<TransformComponent>, Write<DirectionalLightComponent>>())
        {
            if (sky.animate)
            {
                sky.timeOfDay += sky.hoursPerSecond * std::min(context.deltaTime, 0.1f);
                if (sky.timeOfDay >= 24.0f)
                    sky.timeOfDay -= 24.0f;
            }
            const SunState s = sunFor(sky);
            transform.rotation = s.rotation;
            light.color = s.color;
            light.intensity = s.intensity;
            updateLookFromSun(s);
            g_playLookFrame = g_frame;
            break;
        }
    }

     
    Quat lookRotation(const Vec3& forward)
    {
        const Vec3 f = normalize(forward);
        const f32 yaw = std::atan2(-f.x, -f.z);
        const f32 pitch = std::asin(std::clamp(f.y, -1.0f, 1.0f));
        return normalize(multiply(fromAxisAngle({0, 1, 0}, yaw), fromAxisAngle({1, 0, 0}, pitch)));
    }

    Vec3 catmull(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, f32 t)
    {
        const f32 t2 = t * t, t3 = t2 * t;
        return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) *
               0.5f;
    }

    Vec3 flybyAt(f32 u)  
    {
        const int n = static_cast<int>(FLYBY.size());
        const f32 w = std::fmod(std::fmod(u, static_cast<f32>(n)) + n, static_cast<f32>(n));
        const int i = static_cast<int>(w);
        const f32 t = w - i;
        auto P = [&](int k) { return FLYBY[((k % n) + n) % n]; };
        return catmull(P(i - 1), P(i), P(i + 1), P(i + 2), t);
    }

    struct CameraCommand
    {
        bool pending = false;
        u32 mode = 0;
        int spot = -1;
        std::string target;
    };
    CameraCommand g_cameraCommand;
    Entity g_followTarget = Entity::undefined();
    Entity g_cameraRigEntity = Entity::undefined();
    std::array<Extent2D, 32> g_viewExtent{};  
    f32 g_authoredFov = -1.0f;
    f32 g_authoredFar = -1.0f, g_farScale = 1.0f;  
    f32 g_shakeTime = 0.0f;
    Vec3 g_battleCentre{200.0f, 20.0f, -150.0f};  
    f32 g_battleSpread = 0.0f;

     
    bool vehicleCamera(SystemContext& ctx, u32 mode, TransformComponent& cam, CameraComponent* lens, f32 dt);
    void setCameraPose(const Vec3& pos, const Vec3& dir, f32 fov);
    void addShake(TransformComponent& cam, f32 dt);

    Vec3 keepAboveGround(Vec3 p, f32 clearance)
    {
        p.y = std::max(p.y, g_terrain.height(p.x, p.z) + clearance);
        return p;
    }

    bool gigaHud(UiCanvasPainter&, f32, f32);
    bool gigaCamera(SystemContext&, TransformComponent&, CameraComponent*, f32);
    bool gigaCinematicActive();
    bool menuBlocksGame();
    void cameraSystem(SystemContext& context)
    {
        const f32 dt = std::min(context.deltaTime, 0.1f);
        auto query = context.query<Write<WarzoneCameraRig>, Write<TransformComponent>>();
        for (auto [entity, rig, transform] : query->view<Entity, Write<WarzoneCameraRig>, Write<TransformComponent>>())
        {
            g_cameraRigEntity = entity;
            auto* lens = context.tryGetComponent<CameraComponent>(entity);
            if (lens && g_authoredFov < 0.0f)
                g_authoredFov = lens->fov;
            if (lens && g_authoredFar < 0.0f)
                g_authoredFar = lens->farPlane;
            if (lens && g_authoredFar > 0.0f)
                lens->farPlane = g_authoredFar * g_farScale;
            if (g_cameraCommand.pending)
            {
                g_cameraCommand.pending = false;
                rig.mode = g_cameraCommand.mode;
                if (g_cameraCommand.spot >= 0)
                {
                    transform.position = SPOTS[g_cameraCommand.spot].eye;
                    transform.rotation = lookRotation(SPOTS[g_cameraCommand.spot].target - SPOTS[g_cameraCommand.spot].eye);
                }
                g_followTarget = Entity::undefined();
                if (!g_cameraCommand.target.empty())
                {
                     
                    for (auto [e, name] : context.query<Read<EntityNameComponent>>()->view<Entity, Read<EntityNameComponent>>())
                    {
                        if (name.name.toView() == g_cameraCommand.target)
                        {
                            g_followTarget = e;
                            break;
                        }
                    }
                }
            }
            if (context.services().contains<InputSystem>() && !menuBlocksGame() && !gigaCinematicActive()) {
                const auto input = context.services().read<InputSystem>();
                if (input->hasGameInputFocus())
                    rig.zoom *= std::exp(std::clamp(input->getMouseState().scrollDeltaY, -10.0f, 10.0f) * 0.12f);
            }
            rig.zoom = std::clamp(rig.zoom, 0.5f, 4.0f);
            rig.smoothZoom += (rig.zoom - rig.smoothZoom) * (1 - std::exp(-12 * dt));
            const bool giga = gigaCamera(context, transform, lens, dt);
            if (!giga) switch (rig.mode)
            {
            case 1:  
            {
                rig.phase += rig.speed * dt;
                const f32 a = toRadians(rig.phase);
                Vec3 eye = rig.pivot + Vec3{std::sin(a) * rig.radius, rig.height, std::cos(a) * rig.radius};
                eye = keepAboveGround(eye, 25.0f);
                transform.position = eye;
                transform.rotation = lookRotation(rig.pivot - eye);
                break;
            }
            case 2:  
            {
                const f32 metres = 40.0f * rig.speed * dt;
                 
                const Vec3 a = flybyAt(rig.phase), b = flybyAt(rig.phase + 0.01f);
                const f32 segLen = std::max(length(b - a) / 0.01f, 1.0f);
                rig.phase += metres / segLen;
                Vec3 eye = keepAboveGround(flybyAt(rig.phase), 40.0f);
                const Vec3 ahead = flybyAt(rig.phase + 0.35f);
                Vec3 look = normalize(ahead - eye) * 0.7f + normalize(rig.pivot - eye) * 0.3f;
                look.y = std::min(look.y, -0.18f);
                transform.position = eye;
                transform.rotation = slerp(transform.rotation, lookRotation(look), std::min(1.0f, dt * 1.5f));
                break;
            }
            case 3:  
            case 4:
            {
                auto* targetTransform = g_followTarget.isValid() ? context.tryGetComponent<TransformComponent>(g_followTarget) : nullptr;
                if (!targetTransform)
                    break;
                const Vec3 fwd = rotate(targetTransform->rotation, {0, 0, 1});  
                const Vec3 flat = normalize(Vec3{fwd.x, 0.0f, fwd.z});
                const f32 back = rig.mode == 3 ? 28.0f : 14.0f;
                const f32 up = rig.mode == 3 ? 12.0f : 5.0f;
                const Vec3 want = keepAboveGround(targetTransform->position - flat * back + Vec3{0, up, 0}, 2.0f);
                const f32 k = 1.0f - std::exp(-rig.smoothing * dt);
                transform.position = transform.position + (want - transform.position) * k;
                transform.rotation = slerp(transform.rotation, lookRotation(targetTransform->position + Vec3{0, 2, 0} - transform.position), k);
                break;
            }
            case 10:  
            {
                rig.phase += rig.speed * dt;
                const f32 a = toRadians(rig.phase * 2.5f);
                const f32 r = std::clamp(g_battleSpread * 0.9f, 260.0f, 900.0f) * (0.85f + 0.15f * std::sin(a * 1.7f));
                const Vec3 eye = keepAboveGround(g_battleCentre + Vec3{std::sin(a) * r, 120.0f + 0.25f * r + 30.0f * std::sin(a * 1.3f), std::cos(a) * r}, 40.0f);
                const f32 k = 1.0f - std::exp(-1.5f * dt);
                transform.position = length(transform.position - eye) > 400.0f ? eye : transform.position + (eye - transform.position) * k;
                transform.rotation = slerp(transform.rotation, lookRotation(g_battleCentre - transform.position), std::min(1.0f, dt * 2.0f));
                break;
            }
            case 6:  
            case 7:
            case 8:
            case 9:
                if (!vehicleCamera(context, rig.mode, transform, lens, dt))
                    rig.mode = 0;
                break;
            default:
                break;
            }
            if (!gigaCinematicActive() && (giga || rig.mode < 6 || rig.mode > 9) && lens && g_authoredFov > 0.0f)
                lens->fov = g_authoredFov;
            if (lens && !gigaCinematicActive()) lens->fov = 2 * std::atan(std::tan(lens->fov * 0.5f) / rig.smoothZoom);
            if (!giga) addShake(transform, dt);
            setCameraPose(transform.position, rotate(transform.rotation, {0, 0, -1}), lens ? lens->fov : 1.0f);
            break;
        }
    }

     
    constexpr f32 COLLISION_CELL = 32.0f;  

     
     
     
     
    void buildTerrainCollision(World world, u32 extraBodies, std::span<const std::array<f32, 4>> keep = {})
    {
        if (!g_terrain.valid())
            return;
        const int cells = static_cast<int>((2 * g_terrain.half) / COLLISION_CELL);
        if (world.services().contains<PhysicsWorld>())
            world.services().write<PhysicsWorld>()->reserveBodies(static_cast<u32>(cells * cells) + extraBodies + 4096u);
        const Entity root = world.createEntity();
        world.addComponent<TransformComponent>(root);
        setEntityName(world, root, "Terrain Collision");
        std::vector<std::pair<Entity, Entity>> edges;
        edges.reserve(static_cast<size_t>(cells) * cells);
        for (int j = 0; j < cells; ++j)
        {
            for (int i = 0; i < cells; ++i)
            {
                const f32 cx = -g_terrain.half + (i + 0.5f) * COLLISION_CELL;
                const f32 cz = -g_terrain.half + (j + 0.5f) * COLLISION_CELL;
                if (!keep.empty() && std::none_of(keep.begin(), keep.end(), [&](const auto& r) { return cx >= r[0] && cx <= r[2] && cz >= r[1] && cz <= r[3]; }))
                    continue;
                f32 dx = 0, dz = 0;
                const f32 hx0 = g_terrain.height(cx - COLLISION_CELL * 0.5f, cz), hx1 = g_terrain.height(cx + COLLISION_CELL * 0.5f, cz);
                const f32 hz0 = g_terrain.height(cx, cz - COLLISION_CELL * 0.5f), hz1 = g_terrain.height(cx, cz + COLLISION_CELL * 0.5f);
                dx = (hx1 - hx0) / COLLISION_CELL;
                dz = (hz1 - hz0) / COLLISION_CELL;
                 
                const f32 hc = 0.25f * (hx0 + hx1 + hz0 + hz1) * 0.5f + 0.5f * g_terrain.height(cx, cz);
                const Vec3 n = normalize(Vec3{-dx, 1.0f, -dz});
                const Vec3 axis = cross(Vec3{0, 1, 0}, n);
                const f32 s = length(axis);
                const Quat q = s > 1e-5f ? fromAxisAngle(axis * (1.0f / s), std::acos(std::clamp(n.y, -1.0f, 1.0f))) : Quat::identity();
                const f32 thick = 3.0f;
                const Entity e = world.createEntity();
                TransformComponent t;
                t.position = Vec3{cx, hc, cz} - n * thick;
                t.rotation = q;
                world.addComponent<TransformComponent>(e, t);
                BoxColliderComponent box;
                const f32 stretch = 1.0f / std::max(n.y, 0.5f);
                box.halfExtents = {COLLISION_CELL * 0.5f * stretch + 0.3f, thick, COLLISION_CELL * 0.5f * stretch + 0.3f};
                world.addComponent<BoxColliderComponent>(e, box);
                RigidbodyComponent body;
                body.bodyType = RigidbodyComponent::BodyType::Static;
                body.friction = 0.9f;
                body.restitution = 0.05f;
                body.objectLayer = static_cast<u16>(ObjectLayer::NonMoving);
                world.addComponent<RigidbodyComponent>(e, body);
                edges.emplace_back(e, root);
            }
        }
        if (world.services().contains<EntityTransformManager>())
            world.services().write<EntityTransformManager>()->attachBatch(edges);
        SINFO_LOG("Warzone: terrain collision {} static boxes ({} m cells)", edges.size(), COLLISION_CELL);
    }

     
    constexpr auto FULLSCREEN_VS = "./shaders/bin/wz_fullscreen.vert.spv";
    constexpr auto HAZE_FS = "./shaders/bin/wz_haze.frag.spv";
    constexpr auto GRADE_FS = "./shaders/bin/wz_grade.frag.spv";
    const InternedString DEPTH_NAME("DepthBuffer");
    std::array<bool, 32> g_viewHasDepth{};  

    struct HazePush
    {
        u32 depthIndex, hasDepth, pad0, pad1;
        f32 p22, p32, p11, aspect;
        f32 density, falloff, base, maxOpacity;
        f32 camX, camY, camZ, sunGlow;
        f32 fwdX, fwdY, fwdZ, skyDistance;
        f32 hazeR, hazeG, hazeB, pad2;
        f32 sunX, sunY, sunZ, pad3;
        f32 sunR, sunG, sunB, pad4;
    };
    static_assert(sizeof(HazePush) == 128);

    struct GradePush
    {
        f32 tintR, tintG, tintB, vignette;
        f32 aspect, pad0, pad1, pad2;
    };

    PipelineHandle blendPipeline(RenderPassExecutionContext& context, Format format, const char* fragment, bool multiply)
    {
        GraphicsPipelineSpec spec;
        spec.depthTestEnable = false;
        spec.depthWriteEnable = false;
        spec.cullMode = CullMode::NONE;
        spec.colorAttachmentFormats = {format};
        BlendStateDesc blend;
        blend.blendEnable = true;
        if (multiply)
        {
            blend.srcColorBlendFactor = BlendFactor::DST_COLOR;
            blend.dstColorBlendFactor = BlendFactor::ZERO;
            blend.srcAlphaBlendFactor = BlendFactor::ZERO;
            blend.dstAlphaBlendFactor = BlendFactor::ONE;
        }
        else
        {
            blend.srcColorBlendFactor = BlendFactor::ONE;  
            blend.dstColorBlendFactor = BlendFactor::ONE_MINUS_SRC_ALPHA;
            blend.srcAlphaBlendFactor = BlendFactor::ZERO;
            blend.dstAlphaBlendFactor = BlendFactor::ONE;
        }
        spec.blendStates = {blend};
        spec.shaders = {
            {.path = InternedString(FULLSCREEN_VS), .stage = ShaderStage::VERTEX},
            {.path = InternedString(fragment), .stage = ShaderStage::FRAGMENT},
        };
        return context.services().write<ShaderManager>()->getOrCreatePipeline(spec);
    }

    void addParticlePass(RenderModuleBuilder& module);  

    void registerPasses(const ModuleContext& context)
    {
        auto& services = context.scene.services();
        if (!services.contains<RenderPassSetupService>())
            return;
        for (auto path : {FULLSCREEN_VS, HAZE_FS, GRADE_FS})
            if (!std::filesystem::is_regular_file(resolveContentPath(path)))
                throw std::runtime_error(std::format("WarzoneGame requires shader {}", path));
        auto setup = services.write<RenderPassSetupService>();
        const InternedString name("WarzoneAtmosphere");
        setup->registerModule(
            context.owner,
            createRenderPassModule({
                .passName = name,
                .perView = true,
                .placement = RenderPassPlacement::AfterComposite,
                .setup =
                    [](RenderModuleBuilder& module)
                {
                    module.addFullscreenPass(
                        InternedString("WarzoneHaze"),
                        [](RGBuilder& builder)
                        {
                            builder.asGraphicsPassMatchingAttachment();
                            const bool depth = builder.hasResource(DEPTH_NAME);
                            if (depth)
                                builder.read(DEPTH_NAME, RGUsage::FragmentShaderReadSampled);
                            if (builder.view() < g_viewHasDepth.size())
                                g_viewHasDepth[builder.view()] = depth;
                            builder.writeColorAttachment(viewTargetName(0), 0, RGUsage::ColorAttachmentWrite,
                                                         {.loadOp = AttachmentLoadOp::LOAD});
                        },
                        [pipeline = PipelineHandle{}, format = Format::UNDEFINED](RenderPassExecutionContext& context) mutable
                        {
                            const Format current = context.services().read<IRenderer>()->getSwapchainFormat();
                            if (current != format)
                            {
                                pipeline = blendPipeline(context, current, HAZE_FS, false);
                                format = current;
                            }
                            return pipeline;
                        },
                        [](RenderPassExecutionContext& context)
                        {
                            HazePush p{};
                            Look look;
                            {
                                std::scoped_lock lock(g_lookMutex);
                                look = g_look;
                            }
                            const u32 view = context.view();
                            if (!context.services().contains<CameraMatricesSingleton>())
                            {
                                context.pushConstants(p);  
                                return;
                            }
                            const auto guard = context.services().read<CameraMatricesSingleton>();
                            const auto& cameras = guard.get();
                            const auto& m = cameras.get();
                            const bool depth = view < g_viewHasDepth.size() && g_viewHasDepth[view];
                            p.hasDepth = look.hazeOn && depth ? 1u : 0u;
                            if (p.hasDepth)
                                p.depthIndex = context.textureIndex(DEPTH_NAME);
                            p.p22 = m.projection[2][2];
                            p.p32 = m.projection[3][2];
                            p.p11 = m.projection[1][1];
                            const Extent2D e = context.viewportExtent();
                            if (view < g_viewExtent.size())
                                g_viewExtent[view] = e;
                            p.aspect = e.height ? static_cast<f32>(e.width) / e.height : 1.0f;
                            Vec3 cam = m.pos;
                            Vec3 fwd = {0, 0, -1};
                            if (view < cameras.viewPositions.size())
                                cam = cameras.viewPositions[view];
                            if (view < cameras.viewDirections.size())
                                fwd = cameras.viewDirections[view].forward;
                            p.density = look.hazeDensity;
                            p.falloff = look.hazeFalloff;
                            p.base = look.hazeBase;
                            p.maxOpacity = look.hazeMax;
                            p.camX = cam.x, p.camY = cam.y, p.camZ = cam.z;
                            p.sunGlow = look.sunGlow;
                            p.fwdX = fwd.x, p.fwdY = fwd.y, p.fwdZ = fwd.z;
                            p.skyDistance = look.skyDistance;
                            p.hazeR = look.hazeColor.x, p.hazeG = look.hazeColor.y, p.hazeB = look.hazeColor.z;
                            p.sunX = look.sunDir.x, p.sunY = look.sunDir.y, p.sunZ = look.sunDir.z;
                            p.sunR = look.sunColor.x, p.sunG = look.sunColor.y, p.sunB = look.sunColor.z;
                            context.pushConstants(p);
                        });
                    module.addFullscreenPass(
                        InternedString("WarzoneGrade"),
                        [](RGBuilder& builder)
                        {
                            builder.asGraphicsPassMatchingAttachment();
                            builder.writeColorAttachment(viewTargetName(0), 0, RGUsage::ColorAttachmentWrite,
                                                         {.loadOp = AttachmentLoadOp::LOAD});
                        },
                        [pipeline = PipelineHandle{}, format = Format::UNDEFINED](RenderPassExecutionContext& context) mutable
                        {
                            const Format current = context.services().read<IRenderer>()->getSwapchainFormat();
                            if (current != format)
                            {
                                pipeline = blendPipeline(context, current, GRADE_FS, true);
                                format = current;
                            }
                            return pipeline;
                        },
                        [](RenderPassExecutionContext& context)
                        {
                            GradePush p{};
                            Look look;
                            {
                                std::scoped_lock lock(g_lookMutex);
                                look = g_look;
                            }
                            const Extent2D e = context.viewportExtent();
                            p.aspect = e.height ? static_cast<f32>(e.width) / e.height : 1.0f;
                            if (look.gradeOn)
                            {
                                p.tintR = look.tint.x, p.tintG = look.tint.y, p.tintB = look.tint.z;
                                p.vignette = look.vignette;
                            }
                            else
                            {
                                p.tintR = p.tintG = p.tintB = 1.0f;
                            }
                            context.pushConstants(p);
                        });
                },
            }));
        setup->registerModule(context.owner, createRenderPassModule({
            .passName = InternedString("WarzoneHdrParticles"),
            .perView = true,
            .placement = RenderPassPlacement::AfterTransparent,
            .setup = [](RenderModuleBuilder& module) { addParticlePass(module); },
        }));
        setup->finalize(services);
    }

     
    const std::array<std::string_view, 11> CAMERA_MODES = {"hold", "orbit", "flyby", "follow", "chase", "spot", "tank", "amp", "heli", "plane", "battle"};

    std::optional<f32> number(const ControlArgs& args, std::string_view key)
    {
        if (auto text = args.json(key))
        {
            try
            {
                return std::stof(*text);
            }
            catch (...)
            {
            }
        }
        return std::nullopt;
    }

    Entity findWith(const World& world, ComponentID id)
    {
        auto query = world.createQueryBuilder().buildQuery<Read<WarzoneCameraRig>>();
        (void)id;
        for (auto [e, rig] : query->view<Entity, Read<WarzoneCameraRig>>())
        {
            (void)rig;
            return e;
        }
        return Entity::undefined();
    }

#include "src/GameTown.inl"
#include "src/GameParticles.inl"
#include "src/GameDestruction.inl"
#include "src/GameWeapons.inl"
#include "src/GameTank.inl"
#include "src/GameHeli.inl"
#include "src/GamePlane.inl"
#include "src/GameMatch.inl"
#include "src/GameAI.inl"
#include "src/GameBreakup.inl"
#define WZ_HAS_PARTICLES_SET_QUALITY  
#include "src/GameQuality.inl"
#include "src/GameMenu.inl"
#include "src/GameGiga.inl"

    void registerCommands(const ModuleContext& context)
    {
        auto& control = context.control;
        const std::string_view owner = MODULE;
        static const std::array<std::string_view, 11> modes = CAMERA_MODES;
        static std::vector<std::string_view> spotNames = [] {
            std::vector<std::string_view> v;
            for (const auto& s : SPOTS)
                v.push_back(s.name);
            return v;
        }();
        control.registerCommand(
            "warzoneCamera", owner,
            "drives the Play world's Warzone filming camera: mode orbit (around pivot), flyby (slow loop over the map), "
            "follow/chase (behind the named target), spot (jump to a saved place, then hold), hold, tank (third person "
            "behind the player tank: mouse / Q E arrows aim, the HUD shows), amp, heli, plane (the same for the AMP / helicopter / airplane; "
            "each makes that vehicle the one the keyboard drives)",
            {requiredArg("mode", ArgKind::String, "camera mode", modes),
             optionalArg("target", ArgKind::String, "follow/chase: entity name in the play world"),
             optionalArg("spot", ArgKind::String, "spot mode: a saved place", "overview", spotNames),
             optionalArg("speed", ArgKind::Number, "orbit deg/s or flyby speed scale (1 = 40 m/s)"),
             optionalArg("radius", ArgKind::Number, "orbit radius m"),
             optionalArg("height", ArgKind::Number, "orbit height m"),
             optionalArg("pivot", ArgKind::Json, "orbit pivot [x,y,z]")},
            [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
            {
                const World* play = control.world("play");
                if (!play)
                    return "warzoneCamera needs Play running (the filming camera is driven in the play world)";
                const std::string mode = args.string("mode").value_or("flyby");
                u32 index = 0;
                for (u32 i = 0; i < CAMERA_MODES.size(); ++i)
                    if (CAMERA_MODES[i] == mode)
                        index = i;
                const Entity cam = findWith(*play, 0);
                if (!cam.isValid())
                    return "no entity with WarzoneCameraRig in the play world";
                auto& rig = play->getComponent<WarzoneCameraRig>(cam);
                if (auto v = number(args, "speed"))
                    rig.speed = *v;
                if (auto v = number(args, "radius"))
                    rig.radius = *v;
                if (auto v = number(args, "height"))
                    rig.height = *v;
                if (auto pivot = args.json("pivot"))
                {
                    f32 x = 0, y = 0, z = 0;
                    if (std::sscanf(pivot->c_str(), "[%f,%f,%f]", &x, &y, &z) == 3)
                        rig.pivot = {x, y, z};
                }
                g_cameraCommand.pending = true;
                g_cameraCommand.mode = index == 5 ? 0u : index;
                g_cameraCommand.spot = -1;
                g_cameraCommand.target = args.string("target").value_or("");
                if (index >= 6)
                {
                    std::scoped_lock lock(g_tankMutex);
                    setActiveLocked(static_cast<Active>(index - 6));
                }
                if (index == 5)
                {
                    const std::string spot = args.string("spot").value_or("overview");
                    for (int i = 0; i < static_cast<int>(SPOTS.size()); ++i)
                        if (spot == SPOTS[i].name)
                            g_cameraCommand.spot = i;
                }
                result.writeString("mode", mode);
                result.writeU64("camera", cam.id());
                return {};
            });

        control.registerCommand(
            "warzoneSky", owner, "sets the Play world's time of day: time (hours), speed (hours per second), animate",
            {optionalArg("time", ArgKind::Number, "hours, 0-24"), optionalArg("speed", ArgKind::Number, "hours per second"),
             optionalArg("animate", ArgKind::Boolean, "advance time")},
            [&control](const ControlArgs& args, IFormatWriter& result) -> std::string
            {
                const World* play = control.world("play");
                if (!play)
                    return "warzoneSky needs Play running; author the scene's WarzoneSky.timeOfDay with set instead";
                auto query = play->createQueryBuilder().buildQuery<Write<WarzoneSky>>();
                for (auto& sky : query->view<Write<WarzoneSky>>())
                {
                    if (auto v = number(args, "time"))
                        sky.timeOfDay = std::fmod(std::fmod(*v, 24.0f) + 24.0f, 24.0f);
                    if (auto v = number(args, "speed"))
                        sky.hoursPerSecond = *v;
                    if (auto v = args.boolean("animate"))
                        sky.animate = *v;
                    result.writeF32("time", sky.timeOfDay);
                    result.writeF32("speed", sky.hoursPerSecond);
                    result.writeBool("animate", sky.animate);
                    return {};
                }
                return "no WarzoneSky in the play world";
            });

        control.registerCommand(
            "warzoneLook", owner,
            "tunes the Warzone haze and grade passes live (not saved): haze, grade, density, falloff, base, maxOpacity, "
            "sunGlow, vignette, autoHaze, hazeColor [r,g,b], tint [r,g,b]",
            {optionalArg("haze", ArgKind::Boolean, "distance/height haze pass on"),
             optionalArg("grade", ArgKind::Boolean, "tint + vignette pass on"),
             optionalArg("density", ArgKind::Number, "haze density per metre at the base height"),
             optionalArg("falloff", ArgKind::Number, "metres per e-fold of haze density with height"),
             optionalArg("base", ArgKind::Number, "haze base height m"),
             optionalArg("maxOpacity", ArgKind::Number, "haze opacity cap 0-1"),
             optionalArg("sunGlow", ArgKind::Number, "forward-scatter glow toward the sun"),
             optionalArg("vignette", ArgKind::Number, "vignette strength"),
             optionalArg("autoHaze", ArgKind::Boolean, "haze colour and tint follow the sun"),
             optionalArg("hazeColor", ArgKind::Json, "[r,g,b] display-space haze colour"),
             optionalArg("tint", ArgKind::Json, "[r,g,b] multiply tint")},
            [](const ControlArgs& args, IFormatWriter& result) -> std::string
            {
                std::scoped_lock lock(g_lookMutex);
                auto& l = g_look;
                if (auto v = args.boolean("haze"))
                    l.hazeOn = *v;
                if (auto v = args.boolean("grade"))
                    l.gradeOn = *v;
                if (auto v = args.boolean("autoHaze"))
                    l.autoHaze = *v;
                if (auto v = number(args, "density"))
                    l.hazeDensity = *v;
                if (auto v = number(args, "falloff"))
                    l.hazeFalloff = std::max(1.0f, *v);
                if (auto v = number(args, "base"))
                    l.hazeBase = *v;
                if (auto v = number(args, "maxOpacity"))
                    l.hazeMax = std::clamp(*v, 0.0f, 1.0f);
                if (auto v = number(args, "sunGlow"))
                    l.sunGlow = *v;
                if (auto v = number(args, "vignette"))
                    l.vignette = std::clamp(*v, 0.0f, 1.0f);
                auto vec = [&](std::string_view key, Vec3& out)
                {
                    if (auto text = args.json(key))
                    {
                        f32 x, y, z;
                        if (std::sscanf(text->c_str(), "[%f,%f,%f]", &x, &y, &z) == 3)
                            out = {x, y, z};
                    }
                };
                vec("hazeColor", l.hazeColor);
                vec("tint", l.tint);
                result.writeBool("haze", l.hazeOn);
                result.writeBool("grade", l.gradeOn);
                result.writeF32("density", l.hazeDensity);
                result.writeF32("falloff", l.hazeFalloff);
                result.writeF32("base", l.hazeBase);
                result.writeF32("maxOpacity", l.hazeMax);
                result.writeF32("vignette", l.vignette);
                const f32 haze[3] = {l.hazeColor.x, l.hazeColor.y, l.hazeColor.z};
                result.writeFloats("hazeColor", haze);
                return {};
            });

        control.registerQuery(
            "warzoneTerrain", owner, "the game-side heightfield: height and slope at x,z, the map extent and grid",
            {optionalArg("x", ArgKind::Number, "world x", "0"), optionalArg("z", ArgKind::Number, "world z", "0")},
            [](const ControlArgs& args, IFormatWriter& result) -> std::string
            {
                if (!g_terrain.valid())
                    return "terrain heightfield not loaded";
                const f32 x = number(args, "x").value_or(0.0f), z = number(args, "z").value_or(0.0f);
                f32 dx, dz;
                g_terrain.gradient(x, z, dx, dz);
                result.writeF32("height", g_terrain.height(x, z));
                result.writeF32("slope", std::sqrt(dx * dx + dz * dz));
                result.writeF32("half", g_terrain.half);
                result.writeF32("gridM", g_terrain.gridM);
                result.writeU32("n", static_cast<u32>(g_terrain.n));
                return {};
            });

        control.registerQuery("warzoneSpots", owner, "the saved filming camera places", {},
                              [](const ControlArgs&, IFormatWriter& result) -> std::string
                              {
                                  result.beginArray("spots", static_cast<u32>(SPOTS.size()));
                                  for (const auto& s : SPOTS)
                                  {
                                      result.beginObject({});
                                      result.writeString("name", s.name);
                                      const f32 eye[3] = {s.eye.x, s.eye.y, s.eye.z};
                                      const f32 target[3] = {s.target.x, s.target.y, s.target.z};
                                      result.writeFloats("eye", eye);
                                      result.writeFloats("target", target);
                                      result.endObject();
                                  }
                                  result.endArray();
                                  return {};
                              });
    }

     
    bool load(const ModuleContext& context)
    {
        registerComponent<WarzoneSky>(context.owner.id());
        registerComponent<WarzoneCameraRig>(context.owner.id());
        registerComponent<WarzoneTown>(context.owner.id());
        registerComponent<WarzoneBuilding>(context.owner.id());
        registerComponent<WarzonePiece>(context.owner.id());
        registerComponent<WarzoneTank>(context.owner.id());
        registerComponent<WarzoneAmp>(context.owner.id());
        registerComponent<WarzoneHeli>(context.owner.id());
        registerComponent<WarzonePlane>(context.owner.id());

        std::string error;
        if (!g_terrain.load(resolveContentPath("assets_baked/warzone/terrain_meta.txt"),
                            resolveContentPath("assets_baked/warzone/terrain_height.f32"), error))
            SWARNING_LOG("Warzone: {}", error);

        SystemDescriptor read;
        read.moduleName = MODULE;
        read.name = "WarzoneSkyRead";
        read.fn = skyReadSystem;
        read.update = SystemPhaseDescriptor{.reads = {getComponentId<WarzoneSky>()}};
        context.scene.registerSystem(read);

        ptCheckShaders();
        registerPasses(context);
        ptReadAtlasMetadata();
        registerCommands(context);
        registerTownCommands(context);
        registerDestructionCommands(context);
        registerWeaponsCommands(context);
        registerTankCommands(context);
        registerHeliCommands(context);
        registerPlaneCommands(context);
        registerMatchCommands(context);
        registerAiCommands(context);
        registerBreakupCommands(context);
        loadQuality(context);
        registerMenuCommands(context);
        registerGigaCommands(context);
        mapTankInput(context);
        mapMenuInput(context);
        loadBlocks();
        return true;
    }

    void unload(const ModuleContext& context)
    {
        restoreNukeBattleTime();
        unmapTankInput(context);
        unmapMenuInput(context);
        if (context.services.contains<InputSystem>()) {
            auto input = context.services.write<InputSystem>();
            input->unmapAction(InternedString("giga_jump"));
        }
        ptUnload(context);
    }

    void attachWorld(const ModuleContext& context, World world)
    {
        SystemDescriptor sky;
        sky.moduleName = MODULE;
        sky.name = "WarzoneSky";
        sky.fn = skyDriveSystem;
        sky.update = SystemPhaseDescriptor{
            .writes = {getComponentId<WarzoneSky>(), getComponentId<TransformComponent>(), getComponentId<DirectionalLightComponent>()}};
        world.registerSystem(sky);

        SystemDescriptor cam;
        cam.moduleName = MODULE;
        cam.name = "WarzoneCamera";
        cam.fn = cameraSystem;
        cam.update = SystemPhaseDescriptor{.reads = {getComponentId<EntityNameComponent>()},
                                           .writes = {getComponentId<WarzoneCameraRig>(), getComponentId<TransformComponent>(),
                                                      getComponentId<CameraComponent>()},
                                           .stage = CoreExecutionStages::POST_UPDATE};
        world.registerSystem(cam);

        registerDestructionSystem(world);
        registerWeaponsSystem(world);
        registerTankSystems(world);
        registerHeliSystems(world);
        registerPlaneSystems(world);
        registerMatchSystem(world);
        registerAiSystem(world);
        registerBreakupSystem(world);
        registerQualitySystem(world);
        registerGigaSystem(world);
        registerParticlePublishSystem(world);
        ptAttach(context, world);
        if (!sceneLoading(world))
        {
            u32 seed = 7;
            findTownRoot(world, &seed);
            townPlan(seed);
            std::vector<std::array<f32, 4>> keep;
            for (const auto& c : g_cities)
                keep.push_back({c.cx - c.hx - 200.0f, c.cz - c.hz - 200.0f, c.cx + c.hx + 200.0f, c.cz + c.hz + 200.0f});
             
            buildTerrainCollision(world, static_cast<u32>(STREAM_BODY_CAP * g_stream.budgetSlack) + 3000u, keep);
            attachDestruction(context, world);
            attachWeapons(context, world);
            attachTanks(context, world);
            attachHelis(context, world);
            attachPlanes(context, world);
            attachMatch();
            attachAi();
            attachGiga(context, world);
            attachBreakup();
             
            std::vector<Entity> groundMeshes;
            {
                auto ground = world.createQueryBuilder().buildQuery<Read<EntityNameComponent>, Read<MeshComponent>>();
                for (auto [e, name] : ground->view<Entity, Read<EntityNameComponent>>())
                {
                    const auto n = name.name.toView();
                    if (n.starts_with("tile_") || n.starts_with("rd_") || n.starts_with("terrain_far"))
                        groundMeshes.push_back(e);
                }
            }
            for (Entity e : groundMeshes)
                if (!world.hasComponent<RenderLodPolicyComponent>(e))
                    world.addComponent<RenderLodPolicyComponent>(e);
            SINFO_LOG("Warzone: full-detail LOD policy on {} ground/road renderables", groundMeshes.size());
            attachQuality(context);
            createHud(context, world);
            attachMenu();
            createMenuCanvas(context, world);
            registerMenuSystem(world);
        }
    }
}  

SPITE_MODULE_EXPORT const spite::ModuleDescriptor* SPITE_MODULE_ENTRY()
{
    static const spite::ModuleDescriptor descriptor{.name = "WarzoneGame", .load = &load, .unload = &unload, .attachWorld = &attachWorld};
    return &descriptor;
}
