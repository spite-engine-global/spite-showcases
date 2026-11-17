#include "CityTransitions.hpp"

#include <algorithm>
#include <map>
#include <vector>

#include "base/ServiceLocator.hpp"
#include "ecs/CommandBuffer.hpp"
#include "ecs/SystemContext.hpp"
#include "ecs/World.hpp"
#include "engine/Module.hpp"
#include "renderruntime/RenderingComponents.hpp"
#include "world/EntityTransformManager.hpp"

namespace spite::city
{
    namespace
    {
        struct FadeEvent
        {
            Entity entity;
            bool visible;
            TransformComponent before;
            TransformComponent after;
        };
        struct Fade
        {
            bool visible = false;
            float opacity = 1.0f;
            TransformComponent held;
            TransformComponent hidden;
            std::vector<Entity> parts;
        };
        struct CityTransitions
        {
            explicit CityTransitions(const World target) : world(target) {}
            ~CityTransitions()
            {
                if (!world)
                    return;
                for (const auto& [entity, fade] : active)
                {
                    if (!fade.visible && world.isEntityAlive(entity) && world.hasComponent<TransformComponent>(entity))
                        world.getComponent<TransformComponent>(entity) = fade.hidden;
                    for (const Entity part : fade.parts)
                        if (world.isEntityAlive(part) && world.hasComponent<RenderFadeComponent>(part))
                            world.getComponent<RenderFadeComponent>(part).opacity = 1.0f;
                }
                for (const FadeEvent& event : events)
                    if (world.isEntityAlive(event.entity) && world.hasComponent<TransformComponent>(event.entity))
                        world.getComponent<TransformComponent>(event.entity) = event.after;
            }
            World world;
            std::vector<FadeEvent> events;
            std::map<Entity, Fade> active;
        };

        void updateTransitions(SystemContext& context)
        {
            auto transitions = context.services().write<CityTransitions>();
            for (const FadeEvent& event : transitions->events)
            {
                if (!context.isEntityValid(event.entity))
                    continue;
                auto [it, inserted] = transitions->active.try_emplace(event.entity);
                Fade& fade = it->second;
                if (inserted)
                {
                    fade.opacity = event.visible ? 0.0f : 1.0f;
                    std::vector<Entity> subtree;
                    context.services().read<EntityTransformManager>()->collectSubtree(event.entity, subtree);
                    fade.parts = std::move(subtree);
                }
                fade.visible = event.visible;
                fade.held = event.before;
                fade.hidden = event.after;
            }
            transitions->events.clear();
            for (auto it = transitions->active.begin(); it != transitions->active.end();)
            {
                const Entity entity = it->first;
                Fade& fade = it->second;
                if (!context.isEntityValid(entity))
                {
                    it = transitions->active.erase(it);
                    continue;
                }
                fade.opacity = std::clamp(
                    fade.opacity + (fade.visible ? 1.0f : -1.0f) * context.deltaTime / CITY_FADE_SECONDS, 0.0f, 1.0f);
                const bool done = fade.opacity == (fade.visible ? 1.0f : 0.0f);
                if (!fade.visible)
                    context.getComponent<TransformComponent>(entity) = done ? fade.hidden : fade.held;
                const float coverage = fade.opacity * fade.opacity * (3.0f - 2.0f * fade.opacity);
                for (const Entity part : fade.parts)
                {
                    if (!context.isEntityValid(part))
                        continue;
                    const float opacity = done ? 1.0f : coverage;
                    if (context.hasComponent<RenderFadeComponent>(part))
                        context.getComponent<RenderFadeComponent>(part).opacity = opacity;
                    else
                        context.getCommandBuffer().addComponent<RenderFadeComponent>(
                            part, RenderFadeComponent{.opacity = opacity});
                }
                if (done)
                    it = transitions->active.erase(it);
                else
                    ++it;
            }
        }
    }  

    void queueCityFade(SystemContext& context, const Entity entity, const bool visible,
                       const TransformComponent& before, const TransformComponent& after)
    {
        if (context.services().contains<CityTransitions>())
            context.services().write<CityTransitions>()->events.push_back({entity, visible, before, after});
    }

    void attachTransitions(const ModuleContext& context, const World world)
    {
        world.services().registerService(std::make_shared<CityTransitions>(world), context.owner);
        world.registerSystem({.moduleName = context.owner.toView().data(),
                              .name = "CityTransitions",
                              .fn = &updateTransitions,
                              .update = SystemPhaseDescriptor{.writes = {getComponentId<TransformComponent>(),
                                                                         getComponentId<RenderFadeComponent>()}}});
    }

    void prepareCityFades(const World world, const std::span<const Entity> roots)
    {
        auto buffer = world.createCommandBuffer();
        auto& transforms = world.services().get<EntityTransformManager>();
        std::vector<Entity> parts;
        for (const Entity root : roots)
        {
            parts.clear();
            transforms.collectSubtree(root, parts);
            for (const Entity part : parts)
                if (!world.hasComponent<RenderFadeComponent>(part))
                    buffer.addComponent<RenderFadeComponent>(part);
        }
        buffer.commit();
    }
}  
