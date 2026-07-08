#pragma once

#include <string>
#include <vector>

namespace tascend {

struct MapMeshInstance {
    std::string actor_name;
    std::string mesh_ref;
    float translation[3];
    float rotation[4];  // quaternion
    float scale[3];
};

struct MapScene {
    std::string map_name;
    std::vector<MapMeshInstance> mesh_instances;
    std::string terrain_path;  // path to terrain.gltf if any
};

class MapAssembler {
public:
    MapAssembler(const std::string& raw_dir, const std::string& output_dir);

    bool assemble_map(const std::string& scene_json_path, const std::string& map_name);

private:
    std::string raw_dir_;
    std::string output_dir_;

    bool parse_scene_json(const std::string& path, MapScene& out) const;
    bool merge_mesh_gltf(const MapScene& scene, const std::string& output_path) const;
};

}
