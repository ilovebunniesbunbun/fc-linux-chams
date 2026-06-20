#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

typedef unsigned int GLuint;

struct SvgTexture {
    GLuint id = 0;
    float width = 0;
    float height = 0;
};

class SvgCache {
public:
    SvgCache();
    ~SvgCache();

    void initialize(const std::string& assets_dir);
    SvgTexture get_texture(uint8_t grenade_type) const;

private:
    SvgTexture load_svg_to_texture(const std::string& path);
    std::unordered_map<uint8_t, SvgTexture> textures;
};
