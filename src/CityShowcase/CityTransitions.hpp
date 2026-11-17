#pragma once

#include <span>

#include "ecs/EcsTypes.hpp"
#include "world/TransformComponents.hpp"

namespace spite
{
    class SystemContext;
    class World;
    struct ModuleContext;
}  

namespace spite::city
{
    inline constexpr float CITY_FADE_SECONDS = 0.45f;
    void queueCityFade(SystemContext& context, Entity entity, bool visible, const TransformComponent& before,
                       const TransformComponent& after);
    void attachTransitions(const ModuleContext& context, World world);
    void prepareCityFades(World world, std::span<const Entity> roots);
}  
