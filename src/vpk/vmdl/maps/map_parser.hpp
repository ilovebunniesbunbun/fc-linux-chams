#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace MapParser {

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };

struct Triangle {
    Vec3 v0, v1, v2;
};

struct MapMesh {
    std::vector<Triangle> Triangles;
    std::vector<Triangle> VisualTriangles;
    bool Valid = false;
};

struct MapEntry {
    std::string Name;
    std::string Path;
};

std::string GetCurrentMap();
std::string GetLoadStatus();

MapMesh LoadMesh(const std::string& MapName);

const MapMesh* GetLoadedMesh();
void ClearLoadedMesh();

std::vector<Vec2> ProjectTopDown(const MapMesh& Mesh,
                                    float CanvasW, float CanvasH);

std::vector<MapEntry> ListAllMaps();

bool AppendPhysBlockTriangles(const std::vector<uint8_t>& vmdl_blob,
                              std::vector<Triangle>& out,
                              std::vector<Triangle>& out_visual);

}
