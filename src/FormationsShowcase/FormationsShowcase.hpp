#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

#include "base/MathFunctions.hpp"
#include "base/MathTypes.hpp"
#include "base/Schema.hpp"
#include "ecs/EcsTypes.hpp"

 
namespace spite::formations
{
    inline constexpr const char* MODULE_NAME = "FormationsShowcase";
    inline constexpr const char* MOVE_SYSTEM_NAME = "FormationsMove";
    inline constexpr const char* FLY_SYSTEM_NAME =
        "FormationsFly";  
     
     
    inline constexpr const char* LAYOUT_COMMAND = "formationsLayout";
     
    inline constexpr const char* CLEAR_COMMAND = "formationsClear";
     
     
    inline constexpr const char* SET_COMMAND = "formationSet";
     
     
    inline constexpr const char* STATUS_QUERY = "formationsStatus";
     
     
    inline constexpr const char* PREDATOR_COMMAND = "formationsPredator";

    inline constexpr u32 DEFAULT_COUNT = 100000;
     
     
    inline constexpr u32 MAX_COUNT = 1u << 31;
    inline constexpr u32 FORMATION_COUNT = 3;
    inline constexpr u32 FORMATION_CUBE = 0;  
    inline constexpr u32 FORMATION_SPHERE = 1;  
    inline constexpr u32 FORMATION_GALAXY = 2;  
     
    inline u32 latticeWidthOf(const u32 count)
    {
        u32 w = static_cast<u32>(std::cbrt(static_cast<double>(count)));
        while (w * w * w < count)
            ++w;
        return w;
    }
    inline constexpr float LATTICE_SPACING = 1.0f;
    struct LatticePoint
    {
        float x, y, z;
    };
     
    inline LatticePoint latticePosition(const u32 index, const u32 width, const u32 layers)
    {
        const float center = static_cast<float>(width - 1) * 0.5f;
        return {(static_cast<float>(index % width) - center) * LATTICE_SPACING,
                (static_cast<float>(index / (width * width)) + 0.5f - static_cast<float>(layers) * 0.5f) *
                    LATTICE_SPACING,
                (static_cast<float>((index / width) % width) - center) * LATTICE_SPACING};
    }
    inline constexpr u32 STEPS_PER_SECOND = 60;
    inline constexpr u32 TRANSITION_STEPS = 160;  
    inline constexpr u32 HOLD_STEPS = 360;
     
     
    inline constexpr u32 CLAIM_STEPS = 120;
    inline constexpr u32 NO_SLOT = 0xFFFFFFFFu;

    inline constexpr u32 PREDATOR_OFF = 0;
    inline constexpr u32 PREDATOR_WANDER = 1;  
    inline constexpr u32 PREDATOR_AT = 2;  
    inline constexpr u32 PREDATOR_CURSOR =
        3;  

     
     
    inline std::optional<Vec3> cursorPlanePoint(const Vec3& position, const Quat& rotation, const float fov,
                                                const float aspect, const Vec2 ndc, const Vec3& through)
    {
        const float tanHalf = std::tan(fov * 0.5f);
        const Vec3 direction = normalize(rotate(rotation, Vec3(ndc.x * tanHalf * aspect, ndc.y * tanHalf, -1.0f)));
        const Vec3 forward = rotate(rotation, Vec3(0.0f, 0.0f, -1.0f));
        const float distance = dot(through - position, forward) / dot(direction, forward);
        if (distance <= 0.0f)
        {
            return std::nullopt;
        }
        return position + direction * distance;
    }

     
    struct FlyLook
    {
        float yaw = 0.0f;
        float pitch = 0.0f;
    };
    inline constexpr float FLY_MAX_PITCH = 1.55f;
    inline FlyLook flyLookOf(const Quat& rotation)
    {
        const Vec3 forward = rotate(rotation, Vec3(0.0f, 0.0f, -1.0f));
        return {std::atan2(-forward.x, -forward.z), std::asin(std::clamp(forward.y, -1.0f, 1.0f))};
    }
    inline Quat flyRotation(const FlyLook& look)
    {
        return multiply(fromAxisAngle(Vec3(0.0f, 1.0f, 0.0f), look.yaw),
                        fromAxisAngle(Vec3(1.0f, 0.0f, 0.0f), look.pitch));
    }
     
    inline Vec3 flyMove(const Quat& rotation, const Vec3& rightUpForward, const float distance)
    {
        return (rotate(rotation, Vec3(rightUpForward.x, 0.0f, -rightUpForward.z)) +
                Vec3(0.0f, rightUpForward.y, 0.0f)) *
            distance;
    }

    inline constexpr u32 SWARM_SEEKING = 0;  
    inline constexpr u32 SWARM_SETTLED = 1;  
    inline constexpr u32 SWARM_FLEEING = 2;  
    inline constexpr u32 SWARM_REGROUPING = 3;  
    inline constexpr u32 SWARM_STATE_COUNT = 4;
    inline constexpr u32 ROLE_FOLLOWER = 0;
    inline constexpr u32 ROLE_LEADER = 1;  

     
     
     
    struct FormationsStateComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "FormationsState";
        u64 seed = 0;
        u32 count = 0;
        u32 formation = FORMATION_CUBE;
        u32 previous = FORMATION_CUBE;  
        u32 phaseStep = TRANSITION_STEPS;  
        u32 step = 0;  
        u32 predatorMode = PREDATOR_WANDER;
        f32 predatorX = 0.0f;  
        f32 predatorY = 0.0f;
        f32 predatorZ = 0.0f;

        SPITE_SCHEMA(FormationsStateComponent, seed, count, formation, previous, phaseStep, step, predatorMode,
                     predatorX, predatorY, predatorZ)
    };

     
    struct FormationCubeComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "FormationCube";
        u32 index = 0;
        f32 shade = 0.0f;  
        u32 role = ROLE_FOLLOWER;
        f32 radius = 0.0f;  
        f32 angle = 0.0f;  
        f32 lift = 0.0f;  
        f32 sphereX = 0.0f;  
        f32 sphereY = 0.0f;
        f32 sphereZ = 0.0f;

        SPITE_SCHEMA(FormationCubeComponent, index, shade, role, radius, angle, lift, sphereX, sphereY, sphereZ)
    };

     
     
    struct SwarmAgentComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "SwarmAgent";
        Vec3 velocity = {0.0f, 0.0f, 0.0f};
        u32 slot = NO_SLOT;
        u32 slotFormation = FORMATION_CUBE;
        u32 state = SWARM_SEEKING;
        u32 stateSteps = 0;  
        f32 energy = 1.0f;  

        SPITE_SCHEMA(SwarmAgentComponent, velocity, slot, slotFormation, state, stateSteps, energy)
    };

     
    struct FormationsSlotComponent : IComponent
    {
        static constexpr const char* SPITE_COMPONENT_NAME = "FormationsSlot";
        u32 index = 0;

        SPITE_SCHEMA(FormationsSlotComponent, index)
    };
}  
