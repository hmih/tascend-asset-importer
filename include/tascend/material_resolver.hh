#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace tascend {

struct MaterialInfo {
    std::string name;
    std::string type;          // "Material3" or "MaterialInstanceConstant"
    std::string parent;        // for MIC: parent material name
    std::string diffuse;       // texture name for Diffuse slot
    std::string normal;        // texture name for Normal slot
    std::string specular;      // texture name for Specular slot
    std::string spec_power;    // texture name for SpecPower slot
    std::string opacity;       // texture name for Opacity slot
    std::string emissive;      // texture name for Emissive slot
    std::string cube;          // texture name for Cube slot
    std::string mask;          // texture name for Mask slot
    std::vector<std::string> other_textures;
};

struct TextureMap {
    std::string texture_name;
    std::string png_path;      // relative path to PNG file
};

class MaterialResolver {
public:
    MaterialResolver(const std::string& raw_dir);

    void scan_materials();
    void scan_textures();
    void resolve_material_chain();

    const std::vector<MaterialInfo>& materials() const { return materials_; }
    const std::unordered_map<std::string, std::string>& texture_paths() const { return texture_paths_; }

    void write_manifest(const std::string& output_path) const;

private:
    std::string raw_dir_;
    std::vector<MaterialInfo> materials_;
    std::unordered_map<std::string, std::string> texture_paths_;

    void parse_mat_file(const std::string& path, MaterialInfo& out) const;
    void find_textures(const std::string& dir);
};

}
