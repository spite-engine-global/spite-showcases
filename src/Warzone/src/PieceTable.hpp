 
#pragma once
#include <array>
#include <string_view>

namespace warzone
{
    enum class Material : unsigned char { Concrete, Masonry, Steel, Wood };

    struct PieceType
    {
        std::string_view slug;
        std::string_view model;
        std::string_view material;
        float sx, sy, sz;  
        Material mat;
    };

    enum Piece : unsigned short
    {
        P_COLUMN = 0,
        P_BEAM = 1,
        P_WALL_MODULAR = 2,
        P_WALL_CONCRETE = 3,
        P_DOOR = 4,
        P_METAL_CRATE = 5,
        P_SHUTTER = 6,
        P_STAIRCASE = 7,
        P_STAIR_LANDING = 8,
        P_PARAPET = 9,
        P_PARAPET_CORNER = 10,
        P_ROOF = 11,
        P_VENT = 12,
        P_GABLE_WALL = 13,
        P_WATER_TANK = 14,
        P_ANTENNA = 15,
        P_HANGAR_WALL = 16,
        P_HANGAR_DOOR = 17,
        P_CATWALK = 18,
        P_TRESTLE = 19,
        P_PIPE_RACK = 20,
        P_STORAGE_TANK = 21,
        P_GENERATOR = 22,
        P_CONTAINER = 23,
        P_ROAD_BARRIER = 24,
        P_SECURITY_BLOCK = 25,
        P_FENCE = 26,
        P_GATE = 27,
        P_CARGO_CRATE = 28,
        P_DRUM = 29,
        P_SLAB = 30,
        P_CURB = 31,
        P_STREET_LIGHT = 32,
        PIECE_COUNT = 33
    };

    inline constexpr std::array<PieceType, 33> PIECES = {{
        {"column", "wz_p_column", "wz_p_column_mat", 0.55f, 3.2f, 0.5f, Material::Concrete},
        {"beam", "wz_p_beam", "wz_p_beam_mat", 4.0f, 0.45f, 0.35f, Material::Concrete},
        {"wall_modular", "wz_p_wall_modular", "wz_p_wall_modular_mat", 4.0f, 3.2f, 0.25f, Material::Masonry},
        {"wall_concrete", "wz_p_wall_concrete", "wz_p_wall_concrete_mat", 4.0f, 3.2f, 0.35f, Material::Concrete},
        {"door", "wz_p_door", "wz_p_door_mat", 1.4f, 2.4f, 0.2f, Material::Steel},
        {"metal_crate", "wz_p_metal_crate", "wz_p_metal_crate_mat", 2.0f, 1.447f, 1.281f, Material::Steel},
        {"shutter", "wz_p_shutter", "wz_p_shutter_mat", 4.0f, 4.0f, 0.6f, Material::Steel},
        {"staircase", "wz_p_staircase", "wz_p_staircase_mat", 1.6f, 3.2f, 3.6f, Material::Concrete},
        {"stair_landing", "wz_p_stair_landing", "wz_p_stair_landing_mat", 2.0f, 0.3f, 1.8f, Material::Concrete},
        {"parapet", "wz_p_parapet", "wz_p_parapet_mat", 4.0f, 0.9f, 0.3f, Material::Concrete},
        {"parapet_corner", "wz_p_parapet_corner", "wz_p_parapet_corner_mat", 0.3f, 0.9f, 1.2f, Material::Concrete},
        {"roof", "wz_p_roof", "wz_p_roof_mat", 4.0f, 1.4f, 6.0f, Material::Steel},
        {"vent", "wz_p_vent", "wz_p_vent_mat", 1.6f, 1.43f, 2.358f, Material::Steel},
        {"gable_wall", "wz_p_gable_wall", "wz_p_gable_wall_mat", 6.0f, 4.0f, 6.0f, Material::Steel},
        {"water_tank", "wz_p_water_tank", "wz_p_water_tank_mat", 2.2f, 3.108f, 2.221f, Material::Steel},
        {"antenna", "wz_p_antenna", "wz_p_antenna_mat", 3.596f, 6.0f, 2.846f, Material::Steel},
        {"hangar_wall", "wz_p_hangar_wall", "wz_p_hangar_wall_mat", 6.0f, 4.0f, 0.4f, Material::Steel},
        {"hangar_door", "wz_p_hangar_door", "wz_p_hangar_door_mat", 5.0f, 5.5f, 0.4f, Material::Steel},
        {"catwalk", "wz_p_catwalk", "wz_p_catwalk_mat", 0.873f, 0.643f, 4.0f, Material::Steel},
        {"trestle", "wz_p_trestle", "wz_p_trestle_mat", 1.37f, 3.0f, 3.954f, Material::Steel},
        {"pipe_rack", "wz_p_pipe_rack", "wz_p_pipe_rack_mat", 1.01f, 1.573f, 4.0f, Material::Steel},
        {"storage_tank", "wz_p_storage_tank", "wz_p_storage_tank_mat", 4.13f, 6.0f, 4.265f, Material::Steel},
        {"generator", "wz_p_generator", "wz_p_generator_mat", 1.742f, 2.081f, 3.5f, Material::Steel},
        {"container", "wz_p_container", "wz_p_container_mat", 2.454f, 2.507f, 6.06f, Material::Steel},
        {"road_barrier", "wz_p_road_barrier", "wz_p_road_barrier_mat", 1.341f, 1.485f, 3.0f, Material::Concrete},
        {"security_block", "wz_p_security_block", "wz_p_security_block_mat", 1.8f, 1.223f, 1.038f, Material::Concrete},
        {"fence", "wz_p_fence", "wz_p_fence_mat", 3.0f, 2.0f, 0.15f, Material::Steel},
        {"gate", "wz_p_gate", "wz_p_gate_mat", 4.0f, 2.4f, 0.4f, Material::Steel},
        {"cargo_crate", "wz_p_cargo_crate", "wz_p_cargo_crate_mat", 1.6f, 0.711f, 0.965f, Material::Wood},
        {"drum", "wz_p_drum", "wz_p_drum_mat", 0.648f, 0.9f, 0.648f, Material::Steel},
        {"slab", "wz_p_slab", "wz_p_slab_mat", 4.0f, 0.3f, 4.0f, Material::Concrete},
        {"curb", "wz_p_curb", "wz_p_curb_mat", 0.499f, 0.163f, 2.0f, Material::Concrete},
        {"street_light", "wz_p_street_light", "wz_p_street_light_mat", 4.777f, 7.0f, 1.556f, Material::Steel},
    }};
}  
