#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include "vpk/vpk.hpp"

int main() {
    std::vector<std::string> vpk_paths = {
        "/home/milo/.steam/steam/steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_dir.vpk",
        "/home/milo/.steam/steam/steamapps/common/Counter-Strike Global Offensive/game/core/pak01_dir.vpk",
        "/home/milo/.steam/steam/steamapps/common/Counter-Strike Global Offensive/game/csgo_core/pak01_dir.vpk",
        "/home/milo/.steam/steam/steamapps/common/Counter-Strike Global Offensive/game/csgo_imported/pak01_dir.vpk",
        "/home/milo/.steam/steam/steamapps/common/Counter-Strike Global Offensive/game/csgo_lv/pak01_dir.vpk"
    };

    std::vector<std::string> models = {
        "models/props/cs_office/paper_towels.vmdl_c",
        "models/props/de_inferno/claypot03.vmdl_c",
        "models/props/de_inferno/hr_i/inferno_ceiling_fan/inferno_ceiling_fan.vmdl_c",
        "models/props/de_mirage/shutter_window_l_breakable.vmdl_c",
        "models/props/de_mirage/shutter_window_l_remainder.vmdl_c",
        "models/props/de_mirage/shutter_window_r_remainder.vmdl_c",
        "models/props/de_mirage/wall_hole_b_cover_sheetmetal.vmdl_c",
        "models/props/de_mirage/wall_hole_cover_sheetmetal.vmdl_c",
        "models/props/de_vostok/hammer01.vmdl_c",
        "models/props/de_vostok/screwdriver01.vmdl_c",
        "models/props/gg_vietnam/cloth03.vmdl_c",
        "models/props_c17/chair_stool01a.vmdl_c",
        "models/props_c17/metalpot001a.vmdl_c",
        "models/props_c17/metalpot002a.vmdl_c",
        "models/props_furniture/cafe_barstool1.vmdl_c",
        "models/props_interiors/tv.vmdl_c",
        "models/props_junk/cinderblock01a.vmdl_c",
        "models/props_junk/garbage_coffeemug001a_fullsheet.vmdl_c",
        "models/props_junk/garbage_glassbottle003a.vmdl_c",
        "models/props_junk/garbage_metalcan001a.vmdl_c",
        "models/props_junk/garbage_plasticbottle001a_fullsheet.vmdl_c",
        "models/props_junk/garbage_plasticbottle002a_fullsheet.vmdl_c",
        "models/props_junk/garbage_sodacup01a.vmdl_c",
        "models/props_junk/garbage_spraypaintcan01a.vmdl_c",
        "models/props_junk/metal_paintcan001a.vmdl_c",
        "models/props_junk/metalbucket01a.vmdl_c",
        "models/props_junk/plasticcrate01a.vmdl_c",
        "models/props_junk/shoe001a.vmdl_c",
        "models/props_urban/plastic_water_jug001.vmdl_c"
    };

    std::vector<vpk::VPKDir> vpks(vpk_paths.size());
    for (size_t i = 0; i < vpk_paths.size(); ++i) {
        vpks[i].open(vpk_paths[i]);
    }

    for (const auto& model : models) {
        std::cout << "Model: " << model << " ->";
        bool found = false;
        for (size_t i = 0; i < vpk_paths.size(); ++i) {
            if (!vpks[i].is_open()) continue;
            auto bytes = vpks[i].read_file(model);
            if (bytes && !bytes->empty()) {
                std::cout << " " << i;
                found = true;
            }
        }
        if (!found) std::cout << " NOT FOUND ANYWHERE";
        std::cout << std::endl;
    }

    return 0;
}
