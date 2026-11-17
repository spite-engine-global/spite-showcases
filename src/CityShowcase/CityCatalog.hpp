#pragma once

#include "CityShowcase.hpp"

 
 
 
 
namespace spite::city::catalog
{
    struct BuildingModel
    {
        const char* name;
        u32 zone;
        u32 special;  
        u32 width;
        u32 depth;
        float minX, maxX, minZ, maxZ;
        float height;
    };

    inline constexpr BuildingModel BUILDINGS[] = {
        {"city_bld_comhigh_w2d2_f10_v1", 3, SPECIAL_NONE, 2, 2, -7.850f, 7.850f, -7.850f, 8.000f, 41.500f},
        {"city_bld_comhigh_w2d2_f14_v1", 3, SPECIAL_NONE, 2, 2, -7.850f, 7.850f, -7.850f, 8.000f, 53.300f},
        {"city_bld_comhigh_w2d3_f16_v1", 3, SPECIAL_NONE, 2, 3, -7.850f, 7.850f, -11.850f, 12.000f, 60.300f},
        {"city_bld_comhigh_w2d3_f22_v1", 3, SPECIAL_NONE, 2, 3, -7.850f, 7.850f, -11.850f, 12.000f, 83.500f},
        {"city_bld_comhigh_w3d3_f12_v1", 3, SPECIAL_NONE, 3, 3, -11.850f, 11.850f, -11.850f, 12.000f, 46.300f},
        {"city_bld_comhigh_w3d3_f20_v1", 3, SPECIAL_NONE, 3, 3, -11.850f, 11.850f, -11.850f, 12.000f, 76.500f},
        {"city_bld_comhigh_w3d3_f25_v1", 3, SPECIAL_NONE, 3, 3, -11.850f, 11.850f, -11.850f, 12.000f, 100.900f},
        {"city_bld_comhigh_w3d3_f45_v1", 3, SPECIAL_NONE, 3, 3, -11.850f, 11.850f, -11.850f, 12.000f, 177.900f},
        {"city_bld_comhigh_w3d4_f18_v1", 3, SPECIAL_NONE, 3, 4, -11.850f, 11.850f, -15.850f, 16.000f, 67.300f},
        {"city_bld_comhigh_w3d4_f32_v1", 3, SPECIAL_NONE, 3, 4, -11.850f, 11.850f, -15.850f, 16.000f, 127.850f},
        {"city_bld_comhigh_w4d4_f30_v1", 3, SPECIAL_NONE, 4, 4, -15.850f, 15.850f, -15.850f, 16.000f, 120.150f},
        {"city_bld_comhigh_w4d4_f38_v1", 3, SPECIAL_NONE, 4, 4, -15.850f, 15.850f, -15.850f, 16.000f, 150.950f},
        {"city_bld_comhigh_w4d4_f50_v1", 3, SPECIAL_NONE, 4, 4, -15.850f, 15.850f, -15.850f, 16.000f, 197.150f},
        {"city_bld_comhigh_w4d4_f60_v1", 3, SPECIAL_NONE, 4, 4, -15.850f, 15.850f, -15.850f, 16.000f, 235.650f},
        {"city_bld_comlow_w1d2_f1_v1", 2, SPECIAL_NONE, 1, 2, -3.850f, 3.850f, -7.850f, 8.000f, 6.570f},
        {"city_bld_comlow_w1d2_f2_v1", 2, SPECIAL_NONE, 1, 2, -3.850f, 3.850f, -7.850f, 8.000f, 9.414f},
        {"city_bld_comlow_w1d3_f1_v1", 2, SPECIAL_NONE, 1, 3, -3.850f, 3.850f, -11.850f, 12.000f, 6.406f},
        {"city_bld_comlow_w1d3_f3_v1", 2, SPECIAL_NONE, 1, 3, -3.850f, 3.850f, -11.850f, 12.000f, 13.081f},
        {"city_bld_comlow_w2d2_f1_v1", 2, SPECIAL_NONE, 2, 2, -7.850f, 7.850f, -7.850f, 8.000f, 6.083f},
        {"city_bld_comlow_w2d2_f2_v1", 2, SPECIAL_NONE, 2, 2, -7.850f, 7.850f, -7.850f, 8.000f, 9.530f},
        {"city_bld_comlow_w2d2_f3_v1", 2, SPECIAL_NONE, 2, 2, -7.850f, 7.850f, -7.850f, 8.000f, 12.868f},
        {"city_bld_comlow_w2d3_f1_v1", 2, SPECIAL_NONE, 2, 3, -7.850f, 7.850f, -11.850f, 12.000f, 6.439f},
        {"city_bld_comlow_w2d3_f2_v1", 2, SPECIAL_NONE, 2, 3, -7.850f, 7.850f, -11.850f, 12.000f, 9.827f},
        {"city_bld_comlow_w3d2_f1_v1", 2, SPECIAL_NONE, 3, 2, -11.850f, 11.850f, -7.850f, 8.000f, 6.483f},
        {"city_bld_comlow_w3d3_f2_v1", 2, SPECIAL_NONE, 3, 3, -11.850f, 11.850f, -11.850f, 12.000f, 9.768f},
        {"city_bld_comlow_w3d3_f3_v1", 2, SPECIAL_NONE, 3, 3, -11.850f, 11.850f, -11.850f, 12.000f, 12.786f},
        {"city_bld_dronehub", 5, SPECIAL_DRONE_HUB, 4, 4, -15.850f, 15.850f, -15.850f, 15.950f, 15.480f},
        {"city_bld_factory_w3d3_f2_v1", 4, SPECIAL_FACTORY, 3, 3, -11.850f, 11.850f, -11.850f, 11.850f, 26.413f},
        {"city_bld_factory_w3d4_f2_v1", 4, SPECIAL_FACTORY, 3, 4, -11.850f, 11.850f, -15.850f, 15.850f, 33.398f},
        {"city_bld_factory_w4d3_f2_v1", 4, SPECIAL_FACTORY, 4, 3, -15.850f, 15.850f, -11.850f, 11.850f, 30.982f},
        {"city_bld_factory_w4d4_f2_v1", 4, SPECIAL_FACTORY, 4, 4, -15.850f, 15.850f, -15.850f, 15.850f, 30.338f},
        {"city_bld_reshigh_w2d3_f5_v1", 1, SPECIAL_NONE, 2, 3, -7.850f, 7.850f, -12.030f, 12.030f, 23.400f},
        {"city_bld_reshigh_w2d3_f8_v1", 1, SPECIAL_NONE, 2, 3, -7.850f, 7.850f, -11.850f, 12.000f, 33.900f},
        {"city_bld_reshigh_w2d4_f11_v1", 1, SPECIAL_NONE, 2, 4, -7.850f, 7.850f, -16.030f, 16.030f, 44.400f},
        {"city_bld_reshigh_w2d4_f6_v1", 1, SPECIAL_NONE, 2, 4, -7.850f, 7.850f, -16.030f, 16.030f, 26.900f},
        {"city_bld_reshigh_w3d3_f10_v1", 1, SPECIAL_NONE, 3, 3, -11.850f, 11.850f, -11.850f, 12.000f, 40.900f},
        {"city_bld_reshigh_w3d3_f12_v1", 1, SPECIAL_NONE, 3, 3, -11.850f, 11.850f, -11.850f, 12.000f, 47.900f},
        {"city_bld_reshigh_w3d3_f7_v1", 1, SPECIAL_NONE, 3, 3, -11.850f, 11.850f, -12.030f, 12.030f, 30.400f},
        {"city_bld_reshigh_w3d4_f14_v1", 1, SPECIAL_NONE, 3, 4, -11.850f, 11.850f, -16.030f, 16.030f, 54.900f},
        {"city_bld_reshigh_w3d4_f9_v1", 1, SPECIAL_NONE, 3, 4, -11.850f, 11.850f, -16.030f, 16.030f, 37.400f},
        {"city_bld_reshigh_w4d3_f8_v1", 1, SPECIAL_NONE, 4, 3, -15.850f, 15.850f, -12.030f, 12.030f, 33.900f},
        {"city_bld_reshigh_w4d4_f12_v1", 1, SPECIAL_NONE, 4, 4, -15.850f, 15.850f, -15.850f, 16.000f, 47.900f},
        {"city_bld_reshigh_w4d4_f15_v1", 1, SPECIAL_NONE, 4, 4, -15.850f, 15.850f, -16.030f, 16.030f, 58.400f},
        {"city_bld_reslow_w1d2_f1_v1", 0, SPECIAL_NONE, 1, 2, -3.850f, 3.850f, -7.850f, 7.850f, 3.700f},
        {"city_bld_reslow_w1d2_f2_v1", 0, SPECIAL_NONE, 1, 2, -3.850f, 3.850f, -7.850f, 7.850f, 6.700f},
        {"city_bld_reslow_w1d2_f3_v1", 0, SPECIAL_NONE, 1, 2, -3.850f, 3.850f, -7.850f, 7.850f, 11.737f},
        {"city_bld_reslow_w1d3_f1_v1", 0, SPECIAL_NONE, 1, 3, -3.850f, 3.850f, -11.850f, 11.850f, 6.003f},
        {"city_bld_reslow_w1d3_f2_v1", 0, SPECIAL_NONE, 1, 3, -3.850f, 3.850f, -11.850f, 11.850f, 8.546f},
        {"city_bld_reslow_w1d3_f3_v1", 0, SPECIAL_NONE, 1, 3, -3.850f, 3.850f, -11.850f, 11.850f, 12.023f},
        {"city_bld_reslow_w2d2_f1_v1", 0, SPECIAL_NONE, 2, 2, -7.850f, 7.850f, -7.850f, 7.850f, 3.700f},
        {"city_bld_reslow_w2d2_f2_v1", 0, SPECIAL_NONE, 2, 2, -7.850f, 7.850f, -7.850f, 7.850f, 11.150f},
        {"city_bld_reslow_w2d2_f2_v2", 0, SPECIAL_NONE, 2, 2, -7.850f, 7.850f, -7.850f, 7.850f, 10.489f},
        {"city_bld_reslow_w2d2_f3_v1", 0, SPECIAL_NONE, 2, 2, -7.850f, 7.850f, -7.850f, 7.850f, 9.700f},
        {"city_bld_reslow_w2d3_f1_v1", 0, SPECIAL_NONE, 2, 3, -7.850f, 7.850f, -11.850f, 11.850f, 3.700f},
        {"city_bld_reslow_w2d3_f2_v1", 0, SPECIAL_NONE, 2, 3, -7.850f, 7.850f, -11.850f, 11.850f, 11.918f},
        {"city_bld_reslow_w2d3_f2_v2", 0, SPECIAL_NONE, 2, 3, -7.850f, 7.850f, -11.850f, 11.850f, 6.700f},
        {"city_bld_reslow_w2d3_f3_v1", 0, SPECIAL_NONE, 2, 3, -7.850f, 7.850f, -11.850f, 11.850f, 14.076f},
        {"city_bld_truckdepot", 5, SPECIAL_TRUCK_DEPOT, 3, 4, -11.850f, 11.850f, -15.850f, 15.850f, 9.427f},
        {"city_bld_warehouse_w3d3_f1_v1", 4, SPECIAL_WAREHOUSE, 3, 3, -11.850f, 11.850f, -11.850f, 11.950f, 11.280f},
        {"city_bld_warehouse_w3d4_f1_v1", 4, SPECIAL_WAREHOUSE, 3, 4, -11.850f, 11.850f, -15.850f, 15.950f, 11.280f},
        {"city_bld_warehouse_w4d3_f1_v1", 4, SPECIAL_WAREHOUSE, 4, 3, -15.850f, 15.850f, -11.850f, 11.950f, 11.280f},
        {"city_bld_warehouse_w4d4_f1_v1", 4, SPECIAL_WAREHOUSE, 4, 4, -15.850f, 15.850f, -15.850f, 15.950f, 11.600f},
    };

     
    struct VehicleModel
    {
        const char* name;
        float length;
    };

    inline constexpr VehicleModel CARS[] = {
        {"city_veh_sedan", 4.862f},
        {"city_veh_hatch", 4.162f},
        {"city_veh_suv", 4.962f},
        {"city_veh_van", 5.462f},
    };
     
    struct TruckModel
    {
        const char* name;
        float length;
        float width;
        float height;
    };

    inline constexpr TruckModel TRUCKS[] = {
        {"city_veh_van", 5.462f, 2.12f, 2.0f},
        {"city_veh_boxtruck", 6.38f, 2.58f, 3.25f},
        {"city_veh_semi", 16.66f, 2.62f, 4.0f},
    };
    inline constexpr const char* VEHICLE_MATERIAL = "city_mat_vehicle";
    inline constexpr const char* DRONE = "city_drone";
    inline constexpr float DRONE_PACKAGE_ANCHOR = 0.2f;  
    inline constexpr const char* PACKAGE = "city_package";  
    inline constexpr float PACKAGE_SIZE = 0.4f;
     
    inline constexpr const char* DRONE_HUB = "city_bld_dronehub";
    inline constexpr float HUB_PADS[4][2] = {{-7.0f, -7.0f}, {-7.0f, 7.0f}, {7.0f, -7.0f}, {7.0f, 7.0f}};
    inline constexpr float HUB_PAD_HEIGHT = 8.3f;
    inline constexpr const char* STREET_LAMP =
        "city_streetlamp";  
    inline constexpr float LAMP_ARM = 2.5f;  
    inline constexpr float LAMP_HEIGHT = 7.77f;
    inline constexpr const char* TRAFFIC_LIGHT = "city_traffic_light";  
     
    inline constexpr const char* MARKER_MATERIAL = "city_mat_marker";
    inline constexpr const char* TREES[] = {"city_tree_a", "city_tree_b", "city_tree_c"};
     
     
    inline constexpr const char* ROAD_SEGMENTS[] = {"city_road_street_seg", "city_road_avenue_seg",
                                                    "city_road_boulevard_seg", "city_road_highway_seg"};
    inline constexpr const char* ROAD_ENDS[] = {"city_road_street_end", "city_road_avenue_end",
                                                "city_road_boulevard_seg", "city_road_highway_seg"};
    inline constexpr const char* ROAD_JUNCTIONS[] = {"city_road_street_junction", "city_road_avenue_junction",
                                                     "city_road_boulevard_junction", "city_road_highway_junction"};
    inline constexpr float ROAD_TILE_LENGTH = 6.0f;
     
    inline constexpr const char* PEOPLE[] = {
        "city_ped_female_01", "city_ped_female_02", "city_ped_female_03", "city_ped_female_04",
        "city_ped_female_05", "city_ped_female_06", "city_ped_female_07", "city_ped_female_08",
        "city_ped_male_01",   "city_ped_male_02",   "city_ped_male_03",   "city_ped_male_04",
        "city_ped_male_05",   "city_ped_male_06",   "city_ped_male_07",   "city_ped_male_08"};
}  
