#include "tascend/map_assembler.hh"
#include "tascend/psk_reader.hh"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <iostream>

namespace tascend {

namespace fs = std::filesystem;

static std::string map_name_from_path(const std::string& path) {
    fs::path p(path);
    return p.parent_path().filename().string();
}

MapAssembler::MapAssembler(const std::string& raw_dir, const std::string& output_dir)
    : raw_dir_(raw_dir)
    , output_dir_(output_dir)
{
    fs::create_directories(output_dir_ + "/maps");
}

bool MapAssembler::assemble_map(const std::string& scene_json_path, const std::string& map_name)
{
    MapScene scene;
    if (!parse_scene_json(scene_json_path, scene)) {
        std::cerr << "Failed to parse scene: " << scene_json_path << std::endl;
        return false;
    }

    fs::path out_path = fs::path(output_dir_) / "maps" / (map_name + ".gltf");
    return merge_mesh_gltf(scene, out_path.string());
}

bool MapAssembler::parse_scene_json(const std::string& path, MapScene& out) const
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::stringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();

    size_t mesh_inst_pos = content.find("\"mesh_instances\"");
    if (mesh_inst_pos == std::string::npos) return false;

    size_t pos = mesh_inst_pos;
    while ((pos = content.find("\"mesh_ref\"", pos)) != std::string::npos) {
        MapMeshInstance inst;
        memset(&inst, 0, sizeof(inst));
        inst.rotation[3] = 1.0f;
        inst.scale[0] = inst.scale[1] = inst.scale[2] = 1.0f;

        size_t val_start = content.find('"', pos + 11) + 1;
        size_t val_end = content.find('"', val_start);
        inst.mesh_ref = content.substr(val_start, val_end - val_start);

        size_t name_pos = content.rfind("\"actor_name\"", pos);
        if (name_pos != std::string::npos && name_pos > mesh_inst_pos) {
            size_t ns = content.find('"', name_pos + 13) + 1;
            size_t ne = content.find('"', ns);
            inst.actor_name = content.substr(ns, ne - ns);
        }

        size_t tr_start = content.find("\"translation\"", pos);
        if (tr_start != std::string::npos && tr_start < pos + 500) {
            size_t arr_start = content.find('[', tr_start);
            std::sscanf(content.c_str() + arr_start, "[%f,%f,%f]",
                       &inst.translation[0], &inst.translation[1], &inst.translation[2]);
        }

        size_t rot_start = content.find("\"rotation\"", pos);
        if (rot_start != std::string::npos && rot_start < pos + 500) {
            size_t arr_start = content.find('[', rot_start);
            std::sscanf(content.c_str() + arr_start, "[%f,%f,%f,%f]",
                       &inst.rotation[0], &inst.rotation[1], &inst.rotation[2], &inst.rotation[3]);
        }

        size_t sc_start = content.find("\"scale\"", pos);
        if (sc_start != std::string::npos && sc_start < pos + 500) {
            size_t arr_start = content.find('[', sc_start);
            std::sscanf(content.c_str() + arr_start, "[%f,%f,%f]",
                       &inst.scale[0], &inst.scale[1], &inst.scale[2]);
        }

        out.mesh_instances.push_back(inst);
        pos = val_end;
    }

    out.map_name = map_name_from_path(path);
    return !out.mesh_instances.empty();
}

bool MapAssembler::merge_mesh_gltf(const MapScene& scene, const std::string& output_path) const
{
    if (scene.mesh_instances.empty()) return false;

    std::ostringstream json;
    json << "{\n";
    json << "  \"asset\": {\"version\": \"2.0\", \"generator\": \"tascend_importer\"},\n";
    json << "  \"scene\": 0,\n";
    json << "  \"scenes\": [{\"nodes\": [";
    for (size_t i = 0; i < scene.mesh_instances.size(); i++) {
        if (i > 0) json << ",";
        json << i;
    }
    json << "]}],\n";

    json << "  \"nodes\": [\n";
    for (size_t i = 0; i < scene.mesh_instances.size(); i++) {
        const auto& inst = scene.mesh_instances[i];
        json << "    {";
        json << "\"name\": \"" << inst.actor_name << "\", ";
        json << "\"translation\": [" << inst.translation[0] << "," << inst.translation[1] << "," << inst.translation[2] << "], ";
        json << "\"rotation\": [" << inst.rotation[0] << "," << inst.rotation[1] << "," << inst.rotation[2] << "," << inst.rotation[3] << "], ";
        json << "\"scale\": [" << inst.scale[0] << "," << inst.scale[1] << "," << inst.scale[2] << "]";
        json << "}";
        if (i + 1 < scene.mesh_instances.size()) json << ",";
        json << "\n";
    }
    json << "  ]\n";
    json << "}\n";

    std::ofstream f(output_path);
    f << json.str();

    return true;
}

}
