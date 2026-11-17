#pragma once

#include <memory>
#include <string>

#include "CityDesign.hpp"
#include "CityShowcase.hpp"
#include "ecs/World.hpp"

namespace spite
{
    class ControlServer;
    class ControlArgs;
    struct ModuleContext;
    class SystemContext;
}  

 
 
namespace spite::city
{
     
    const CityStateComponent* findCity(World world);
     
    const World* statusWorld(ControlServer& control, const ControlArgs& args);
     
    std::shared_ptr<const model::CityMap> worldMap(World world, const CityStateComponent& city);
     
    std::string clockText(float t);
    inline constexpr const char* NO_CITY = "the world has no city: run cityLayout";

     
     
    void registerPlay(const ModuleContext& context);
     
    void attachPlay(const ModuleContext& context, World world);
    Entity activeCityCamera(SystemContext& context);
}  
