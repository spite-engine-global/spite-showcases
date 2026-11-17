#include "../CityDesign.hpp"
#include <iostream>

int main()
{
    namespace city = spite::city::model;
    const auto a = city::generateDesign(7, 160);
    const auto b = city::generateDesign(7, 160);
    const auto c = city::generateDesign(8, 160);
    if (a.edges.empty() || a.lots.empty() || !(a == b) || (a.edges == c.edges && a.lots == c.lots))
    {
        std::cerr << "City design determinism failed\n";
        return 1;
    }
    const auto design = city::generateDesign(11, 256);
    if (city::edgesOf(city::cellsOf(256, design.edges)) != design.edges)
    {
        std::cerr << "City road cell round trip failed\n";
        return 1;
    }
    return 0;
}
