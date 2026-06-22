#include <iostream>
#include <vector>
#include <string>
#include "vpk/vpk.hpp"
#include "vpk/vmdl/model.hpp"

int main() {
    vpk::VPKDir vpk;
    vpk.open("/home/milo/.steam/steam/steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_dir.vpk");

    AgentParser::AgentMesh mesh;
    bool res = AgentParser::LoadModel("agents/models/ctm_sas/ctm_sas.vmdl", mesh, false);
    std::cout << "Res: " << res << " Vertices: " << mesh.vertices.size() << " Indices: " << mesh.indices.size() << std::endl;
    
    // Check if the vertices are just dummy values
    for(size_t i = 0; i < std::min<size_t>(10, mesh.vertices.size()); ++i) {
        std::cout << "v" << i << " pos: " << mesh.vertices[i].px << ", " << mesh.vertices[i].py << ", " << mesh.vertices[i].pz << "\n";
    }
    return 0;
}
