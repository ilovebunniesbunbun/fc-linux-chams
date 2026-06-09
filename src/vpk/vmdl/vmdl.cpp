#include "vmdl.hpp"
#include "model.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace VmdlParser {

static std::string s_LastError;

static void SetError(const std::string& msg) { s_LastError = msg; }
static void ClearError() { s_LastError.clear(); }

const std::string& LastError() { return s_LastError; }

static vpk::VPKDir& GetVpk() {
    static vpk::VPKDir dir;
    static bool tried = false;
    if (!tried) {
        tried = true;
        for (const auto& p : vpk::cs2_default_vpk_paths())
            if (dir.open(p)) break;
    }
    return dir;
}

std::vector<Entry> ListAll(const std::string& filter) {
    std::vector<Entry> result;
    const auto paths = GetVpk().list_files("", ".vmdl_c");
    result.reserve(paths.size());
    for (const auto& path : paths) {
        if (!filter.empty() && path.find(filter) == std::string::npos) continue;
        Entry e;
        e.Path = path;
        const auto slash = path.rfind('/');
        e.Name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
        result.push_back(std::move(e));
    }
    return result;
}

AgentParser::AgentMesh Load(const std::string& path) {
    ClearError();
    if (path.empty()) { SetError("Empty path"); return {}; }
    
    AgentParser::AgentMesh mesh;
    if (AgentParser::LoadModel(path, mesh)) {
        return mesh;
    }
    
    SetError("Failed to load model: " + path);
    return {};
}

static std::string FloatToHex(float value) {
    union { float f; uint32_t i; } u;
    u.f = value;
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << u.i;
    return ss.str();
}

bool ExportToGLTF(const AgentParser::AgentMesh& mesh, const std::string& outputPath) {
    ClearError();
    if (!mesh.valid) {
        SetError("Invalid mesh");
        return false;
    }
    
    // Prepare vertex data
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    
    positions.reserve(mesh.vertices.size() * 3);
    normals.reserve(mesh.vertices.size() * 3);
    uvs.reserve(mesh.vertices.size() * 2);
    
    for (const auto& v : mesh.vertices) {
        positions.push_back(v.px);
        positions.push_back(v.py);
        positions.push_back(v.pz);
        
        normals.push_back(v.nx);
        normals.push_back(v.ny);
        normals.push_back(v.nz);
        
        uvs.push_back(v.u);
        uvs.push_back(v.v);
    }
    
    // Prepare binary buffer
    std::vector<uint8_t> bufferData;
    
    // Position data (vec3, float)
    size_t posOffset = bufferData.size();
    bufferData.insert(bufferData.end(), (uint8_t*)positions.data(), (uint8_t*)positions.data() + positions.size() * sizeof(float));
    
    // Normal data (vec3, float)
    size_t normOffset = bufferData.size();
    bufferData.insert(bufferData.end(), (uint8_t*)normals.data(), (uint8_t*)normals.data() + normals.size() * sizeof(float));
    
    // UV data (vec2, float)
    size_t uvOffset = bufferData.size();
    bufferData.insert(bufferData.end(), (uint8_t*)uvs.data(), (uint8_t*)uvs.data() + uvs.size() * sizeof(float));
    
    // Index data (uint32_t)
    size_t idxOffset = bufferData.size();
    bufferData.insert(bufferData.end(), (uint8_t*)mesh.indices.data(), (uint8_t*)mesh.indices.data() + mesh.indices.size() * sizeof(uint32_t));
    
    // Write binary buffer
    std::string bufferPath = outputPath;
    size_t dotPos = bufferPath.rfind('.');
    if (dotPos != std::string::npos) {
        bufferPath = bufferPath.substr(0, dotPos) + ".bin";
    } else {
        bufferPath += ".bin";
    }
    
    std::ofstream binFile(bufferPath, std::ios::binary);
    if (!binFile) {
        SetError("Failed to create binary buffer file");
        return false;
    }
    binFile.write((const char*)bufferData.data(), bufferData.size());
    binFile.close();
    
    // Build GLTF JSON
    std::stringstream json;
    json << "{\n";
    json << "  \"asset\": {\n";
    json << "    \"version\": \"2.0\"\n";
    json << "  },\n";
    
    // Buffers
    json << "  \"buffers\": [\n";
    json << "    {\n";
    json << "      \"uri\": \"" << bufferPath << "\",\n";
    json << "      \"byteLength\": " << bufferData.size() << "\n";
    json << "    }\n";
    json << "  ],\n";
    
    // BufferViews
    json << "  \"bufferViews\": [\n";
    json << "    {\n";
    json << "      \"buffer\": 0,\n";
    json << "      \"byteOffset\": " << posOffset << ",\n";
    json << "      \"byteLength\": " << (positions.size() * sizeof(float)) << "\n";
    json << "    },\n";
    json << "    {\n";
    json << "      \"buffer\": 0,\n";
    json << "      \"byteOffset\": " << normOffset << ",\n";
    json << "      \"byteLength\": " << (normals.size() * sizeof(float)) << "\n";
    json << "    },\n";
    json << "    {\n";
    json << "      \"buffer\": 0,\n";
    json << "      \"byteOffset\": " << uvOffset << ",\n";
    json << "      \"byteLength\": " << (uvs.size() * sizeof(float)) << "\n";
    json << "    },\n";
    json << "    {\n";
    json << "      \"buffer\": 0,\n";
    json << "      \"byteOffset\": " << idxOffset << ",\n";
    json << "      \"byteLength\": " << (mesh.indices.size() * sizeof(uint32_t)) << ",\n";
    json << "      \"target\": 34963\n"; // ELEMENT_ARRAY_BUFFER
    json << "    }\n";
    json << "  ],\n";
    
    // Accessors
    json << "  \"accessors\": [\n";
    // Position accessor
    json << "    {\n";
    json << "      \"bufferView\": 0,\n";
    json << "      \"componentType\": 5126,\n"; // FLOAT
    json << "      \"count\": " << mesh.vertices.size() << ",\n";
    json << "      \"type\": \"VEC3\"\n";
    json << "    },\n";
    // Normal accessor
    json << "    {\n";
    json << "      \"bufferView\": 1,\n";
    json << "      \"componentType\": 5126,\n"; // FLOAT
    json << "      \"count\": " << mesh.vertices.size() << ",\n";
    json << "      \"type\": \"VEC3\"\n";
    json << "    },\n";
    // UV accessor
    json << "    {\n";
    json << "      \"bufferView\": 2,\n";
    json << "      \"componentType\": 5126,\n"; // FLOAT
    json << "      \"count\": " << mesh.vertices.size() << ",\n";
    json << "      \"type\": \"VEC2\"\n";
    json << "    },\n";
    // Index accessor
    json << "    {\n";
    json << "      \"bufferView\": 3,\n";
    json << "      \"componentType\": 5125,\n"; // UNSIGNED_INT
    json << "      \"count\": " << mesh.indices.size() << ",\n";
    json << "      \"type\": \"SCALAR\"\n";
    json << "    }\n";
    json << "  ],\n";
    
    // Mesh
    json << "  \"meshes\": [\n";
    json << "    {\n";
    json << "      \"primitives\": [\n";
    json << "        {\n";
    json << "          \"attributes\": {\n";
    json << "            \"POSITION\": 0,\n";
    json << "            \"NORMAL\": 1,\n";
    json << "            \"TEXCOORD_0\": 2\n";
    json << "          },\n";
    json << "          \"indices\": 3\n";
    json << "        }\n";
    json << "      ]\n";
    json << "    }\n";
    json << "  ],\n";
    
    // Node
    json << "  \"nodes\": [\n";
    json << "    {\n";
    json << "      \"mesh\": 0\n";
    json << "    }\n";
    json << "  ],\n";
    
    // Scene
    json << "  \"scenes\": [\n";
    json << "    {\n";
    json << "      \"nodes\": [0]\n";
    json << "    }\n";
    json << "  ],\n";
    
    json << "  \"scene\": 0\n";
    json << "}\n";
    
    // Write JSON file
    std::ofstream jsonFile(outputPath);
    if (!jsonFile) {
        SetError("Failed to create JSON file");
        return false;
    }
    jsonFile << json.str();
    jsonFile.close();
    
    return true;
}

bool ExportToGLB(const AgentParser::AgentMesh& mesh, const std::string& outputPath) {
    ClearError();
    if (!mesh.valid) {
        SetError("Invalid mesh");
        return false;
    }
    
    // Prepare vertex data
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    
    positions.reserve(mesh.vertices.size() * 3);
    normals.reserve(mesh.vertices.size() * 3);
    uvs.reserve(mesh.vertices.size() * 2);
    
    for (const auto& v : mesh.vertices) {
        positions.push_back(v.px);
        positions.push_back(v.py);
        positions.push_back(v.pz);
        
        normals.push_back(v.nx);
        normals.push_back(v.ny);
        normals.push_back(v.nz);
        
        uvs.push_back(v.u);
        uvs.push_back(v.v);
    }
    
    // Prepare binary buffer
    std::vector<uint8_t> bufferData;
    
    // Position data (vec3, float)
    size_t posOffset = bufferData.size();
    bufferData.insert(bufferData.end(), (uint8_t*)positions.data(), (uint8_t*)positions.data() + positions.size() * sizeof(float));
    
    // Normal data (vec3, float)
    size_t normOffset = bufferData.size();
    bufferData.insert(bufferData.end(), (uint8_t*)normals.data(), (uint8_t*)normals.data() + normals.size() * sizeof(float));
    
    // UV data (vec2, float)
    size_t uvOffset = bufferData.size();
    bufferData.insert(bufferData.end(), (uint8_t*)uvs.data(), (uint8_t*)uvs.data() + uvs.size() * sizeof(float));
    
    // Index data (uint32_t)
    size_t idxOffset = bufferData.size();
    bufferData.insert(bufferData.end(), (uint8_t*)mesh.indices.data(), (uint8_t*)mesh.indices.data() + mesh.indices.size() * sizeof(uint32_t));
    
    // Build GLTF JSON (without buffer URI since it's embedded)
    std::stringstream json;
    json << "{\n";
    json << "  \"asset\": {\n";
    json << "    \"version\": \"2.0\"\n";
    json << "  },\n";
    
    // Buffers (embedded, no URI)
    json << "  \"buffers\": [\n";
    json << "    {\n";
    json << "      \"byteLength\": " << bufferData.size() << "\n";
    json << "    }\n";
    json << "  ],\n";
    
    // BufferViews
    json << "  \"bufferViews\": [\n";
    json << "    {\n";
    json << "      \"buffer\": 0,\n";
    json << "      \"byteOffset\": " << posOffset << ",\n";
    json << "      \"byteLength\": " << (positions.size() * sizeof(float)) << "\n";
    json << "    },\n";
    json << "    {\n";
    json << "      \"buffer\": 0,\n";
    json << "      \"byteOffset\": " << normOffset << ",\n";
    json << "      \"byteLength\": " << (normals.size() * sizeof(float)) << "\n";
    json << "    },\n";
    json << "    {\n";
    json << "      \"buffer\": 0,\n";
    json << "      \"byteOffset\": " << uvOffset << ",\n";
    json << "      \"byteLength\": " << (uvs.size() * sizeof(float)) << "\n";
    json << "    },\n";
    json << "    {\n";
    json << "      \"buffer\": 0,\n";
    json << "      \"byteOffset\": " << idxOffset << ",\n";
    json << "      \"byteLength\": " << (mesh.indices.size() * sizeof(uint32_t)) << ",\n";
    json << "      \"target\": 34963\n"; // ELEMENT_ARRAY_BUFFER
    json << "    }\n";
    json << "  ],\n";
    
    // Accessors
    json << "  \"accessors\": [\n";
    // Position accessor
    json << "    {\n";
    json << "      \"bufferView\": 0,\n";
    json << "      \"componentType\": 5126,\n"; // FLOAT
    json << "      \"count\": " << mesh.vertices.size() << ",\n";
    json << "      \"type\": \"VEC3\"\n";
    json << "    },\n";
    // Normal accessor
    json << "    {\n";
    json << "      \"bufferView\": 1,\n";
    json << "      \"componentType\": 5126,\n"; // FLOAT
    json << "      \"count\": " << mesh.vertices.size() << ",\n";
    json << "      \"type\": \"VEC3\"\n";
    json << "    },\n";
    // UV accessor
    json << "    {\n";
    json << "      \"bufferView\": 2,\n";
    json << "      \"componentType\": 5126,\n"; // FLOAT
    json << "      \"count\": " << mesh.vertices.size() << ",\n";
    json << "      \"type\": \"VEC2\"\n";
    json << "    },\n";
    // Index accessor
    json << "    {\n";
    json << "      \"bufferView\": 3,\n";
    json << "      \"componentType\": 5125,\n"; // UNSIGNED_INT
    json << "      \"count\": " << mesh.indices.size() << ",\n";
    json << "      \"type\": \"SCALAR\"\n";
    json << "    }\n";
    json << "  ],\n";
    
    // Mesh
    json << "  \"meshes\": [\n";
    json << "    {\n";
    json << "      \"primitives\": [\n";
    json << "        {\n";
    json << "          \"attributes\": {\n";
    json << "            \"POSITION\": 0,\n";
    json << "            \"NORMAL\": 1,\n";
    json << "            \"TEXCOORD_0\": 2\n";
    json << "          },\n";
    json << "          \"indices\": 3\n";
    json << "        }\n";
    json << "      ]\n";
    json << "    }\n";
    json << "  ],\n";
    
    // Node
    json << "  \"nodes\": [\n";
    json << "    {\n";
    json << "      \"mesh\": 0\n";
    json << "    }\n";
    json << "  ],\n";
    
    // Scene
    json << "  \"scenes\": [\n";
    json << "    {\n";
    json << "      \"nodes\": [0]\n";
    json << "    }\n";
    json << "  ],\n";
    
    json << "  \"scene\": 0\n";
    json << "}\n";
    
    std::string jsonStr = json.str();
    
    // Build GLB binary file
    std::vector<uint8_t> glbData;
    
    // Header (12 bytes)
    // magic: "glTF"
    glbData.push_back('g');
    glbData.push_back('l');
    glbData.push_back('T');
    glbData.push_back('F');
    // version: 2 (uint32)
    uint32_t version = 2;
    glbData.insert(glbData.end(), (uint8_t*)&version, (uint8_t*)&version + 4);
    // total length (uint32) - will fill later
    size_t totalLengthOffset = glbData.size();
    uint32_t totalLength = 0;
    glbData.insert(glbData.end(), (uint8_t*)&totalLength, (uint8_t*)&totalLength + 4);
    
    // JSON chunk
    // chunk length (uint32)
    uint32_t jsonLength = (uint32_t)jsonStr.size();
    // Pad to 4-byte boundary
    uint32_t jsonPadding = (4 - (jsonLength % 4)) % 4;
    uint32_t jsonChunkLength = jsonLength + jsonPadding;
    glbData.insert(glbData.end(), (uint8_t*)&jsonChunkLength, (uint8_t*)&jsonChunkLength + 4);
    // chunk type: "JSON"
    glbData.push_back('J');
    glbData.push_back('S');
    glbData.push_back('O');
    glbData.push_back('N');
    // JSON data
    glbData.insert(glbData.end(), jsonStr.begin(), jsonStr.end());
    // JSON padding (spaces)
    for (uint32_t i = 0; i < jsonPadding; i++) {
        glbData.push_back(' ');
    }
    
    // Binary chunk
    // chunk length (uint32)
    uint32_t binLength = (uint32_t)bufferData.size();
    // Pad to 4-byte boundary
    uint32_t binPadding = (4 - (binLength % 4)) % 4;
    uint32_t binChunkLength = binLength + binPadding;
    glbData.insert(glbData.end(), (uint8_t*)&binChunkLength, (uint8_t*)&binChunkLength + 4);
    // chunk type: "BIN"
    glbData.push_back('B');
    glbData.push_back('I');
    glbData.push_back('N');
    glbData.push_back(0);
    // Binary data
    glbData.insert(glbData.end(), bufferData.begin(), bufferData.end());
    // Binary padding (zeros)
    for (uint32_t i = 0; i < binPadding; i++) {
        glbData.push_back(0);
    }
    
    // Update total length
    totalLength = (uint32_t)glbData.size();
    memcpy(glbData.data() + totalLengthOffset, &totalLength, 4);
    
    // Write GLB file
    std::ofstream glbFile(outputPath, std::ios::binary);
    if (!glbFile) {
        SetError("Failed to create GLB file");
        return false;
    }
    glbFile.write((const char*)glbData.data(), glbData.size());
    glbFile.close();
    
    return true;
}

}
