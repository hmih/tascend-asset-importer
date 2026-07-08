#pragma once

#include <string>
#include <vector>

namespace tascend {

struct TerrainVertex {
    float pos[3];
};

struct TerrainData {
    std::vector<TerrainVertex> vertices;
    std::string terrain_name;
};

class TerrainReader {
public:
    TerrainReader(const std::string& output_dir);

    bool read(const std::string& bin_path, const std::string& json_path,
              TerrainData& out) const;
    bool write_gltf(const TerrainData& data, const std::string& output_path) const;

private:
    std::string output_dir_;
};

}
