#include "tascend/material_resolver.hh"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace tascend {

namespace fs = std::filesystem;

MaterialResolver::MaterialResolver(const std::string& raw_dir)
    : raw_dir_(raw_dir)
{
}

void MaterialResolver::scan_materials()
{
    materials_.clear();

    std::vector<std::string> mat_dirs = {
        raw_dir_ + "/Material3",
        raw_dir_ + "/MaterialInstanceConstant",
    };

    for (const auto& pkg_dir : {raw_dir_}) {
        if (!fs::exists(pkg_dir)) continue;
        for (const auto& entry : fs::recursive_directory_iterator(pkg_dir)) {
            if (entry.path().extension() == ".mat") {
                MaterialInfo info;
                info.name = entry.path().stem().string();
                parse_mat_file(entry.path().string(), info);

                fs::path parent = entry.path().parent_path();
                info.type = parent.filename().string();

                materials_.push_back(info);
            }
        }
    }
}

void MaterialResolver::scan_textures()
{
    texture_paths_.clear();
    find_textures(raw_dir_);
}

void MaterialResolver::find_textures(const std::string& dir)
{
    if (!fs::exists(dir)) return;

    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".png") {
            std::string name = entry.path().stem().string();
            std::string rel = fs::relative(entry.path(), raw_dir_).string();
            texture_paths_[name] = rel;
        }
    }
}

void MaterialResolver::resolve_material_chain()
{
    for (auto& mat : materials_) {
        if (mat.type == "MaterialInstanceConstant" && !mat.parent.empty()) {
            for (const auto& parent_mat : materials_) {
                if (parent_mat.name == mat.parent) {
                    if (mat.diffuse.empty()) mat.diffuse = parent_mat.diffuse;
                    if (mat.normal.empty()) mat.normal = parent_mat.normal;
                    if (mat.specular.empty()) mat.specular = parent_mat.specular;
                    if (mat.opacity.empty()) mat.opacity = parent_mat.opacity;
                    if (mat.emissive.empty()) mat.emissive = parent_mat.emissive;
                    break;
                }
            }
        }
    }
}

void MaterialResolver::parse_mat_file(const std::string& path, MaterialInfo& out) const
{
    std::ifstream f(path);
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        val.erase(val.find_last_not_of(" \t\r\n") + 1);

        if (key == "Diffuse") out.diffuse = val;
        else if (key == "Normal") out.normal = val;
        else if (key == "Specular") out.specular = val;
        else if (key == "SpecPower") out.spec_power = val;
        else if (key == "Opacity") out.opacity = val;
        else if (key == "Emissive") out.emissive = val;
        else if (key == "Cube") out.cube = val;
        else if (key == "Mask") out.mask = val;
        else if (key.find("Other[") == 0) out.other_textures.push_back(val);
    }

    if (out.diffuse.empty() && !out.other_textures.empty()) {
        out.diffuse = out.other_textures[0];
    }
}

void MaterialResolver::write_manifest(const std::string& output_path) const
{
    fs::path p(output_path);
    fs::create_directories(p.parent_path());

    std::ofstream f(output_path);
    if (!f.is_open()) return;

    f << "{\n  \"materials\": [\n";
    for (size_t i = 0; i < materials_.size(); i++) {
        const auto& m = materials_[i];
        f << "    {\n";
        f << "      \"name\": \"" << m.name << "\",\n";
        f << "      \"type\": \"" << m.type << "\",\n";
        if (!m.parent.empty()) f << "      \"parent\": \"" << m.parent << "\",\n";
        if (!m.diffuse.empty()) f << "      \"diffuse\": \"" << m.diffuse << "\",\n";
        if (!m.normal.empty()) f << "      \"normal\": \"" << m.normal << "\",\n";
        if (!m.specular.empty()) f << "      \"specular\": \"" << m.specular << "\",\n";
        if (!m.opacity.empty()) f << "      \"opacity\": \"" << m.opacity << "\",\n";
        if (!m.emissive.empty()) f << "      \"emissive\": \"" << m.emissive << "\",\n";
        f << "      \"diffuse_png\": \"";
        auto it = texture_paths_.find(m.diffuse);
        if (it != texture_paths_.end()) f << it->second;
        f << "\"\n";
        f << "    }";
        if (i + 1 < materials_.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
}

}
