#include "tascend/terrain_reader.hh"

#include <fstream>
#include <filesystem>
#include <iostream>

namespace tascend {

namespace fs = std::filesystem;

TerrainReader::TerrainReader(const std::string& output_dir)
    : output_dir_(output_dir)
{
    fs::create_directories(output_dir_);
}

bool TerrainReader::read(const std::string& bin_path, const std::string& json_path,
                         TerrainData& out) const
{
    std::ifstream bf(bin_path, std::ios::binary);
    if (!bf.is_open()) {
        std::cerr << "Cannot open terrain binary: " << bin_path << std::endl;
        return false;
    }

    while (bf.good()) {
        float x, y, z;
        bf.read(reinterpret_cast<char*>(&x), sizeof(float));
        if (!bf.good()) break;
        bf.read(reinterpret_cast<char*>(&y), sizeof(float));
        if (!bf.good()) break;
        bf.read(reinterpret_cast<char*>(&z), sizeof(float));
        if (!bf.good()) break;

        TerrainVertex v;
        v.pos[0] = x;
        v.pos[1] = z;  // UE3 Z-up → glTF Y-up
        v.pos[2] = -y;
        out.vertices.push_back(v);
    }

    fs::path p(bin_path);
    out.terrain_name = p.stem().string();

    return !out.vertices.empty();
}

bool TerrainReader::write_gltf(const TerrainData& data, const std::string& output_path) const
{
    (void)data; (void)output_path;
    return false;
}

}
