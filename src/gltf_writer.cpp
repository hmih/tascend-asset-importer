#include "tascend/gltf_writer.hh"
#include "tascend/psk_reader.hh"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace tascend {

namespace fs = std::filesystem;

GltfWriter::GltfWriter(const std::string& output_dir, const MaterialResolver& materials)
    : output_dir_(output_dir)
    , materials_(materials)
{
    fs::create_directories(output_dir_ + "/static-meshes");
    fs::create_directories(output_dir_ + "/textures");
    fs::create_directories(output_dir_ + "/manifests");
}

int GltfWriter::add_image(const std::string& png_path)
{
    for (size_t i = 0; i < images_.size(); i++) {
        if (images_[i].uri == png_path) return static_cast<int>(i);
    }
    GltfImage img;
    img.uri = png_path;
    images_.push_back(img);
    return static_cast<int>(images_.size() - 1);
}

int GltfWriter::add_texture(const std::string& texture_name)
{
    auto it = materials_.texture_paths().find(texture_name);
    if (it == materials_.texture_paths().end()) return -1;

    int img_idx = add_image("../textures/" + it->second);
    if (img_idx < 0) return -1;

    for (size_t i = 0; i < textures_.size(); i++) {
        if (textures_[i].image_index == img_idx) return static_cast<int>(i);
    }

    GltfTexture tex;
    tex.image_index = img_idx;
    textures_.push_back(tex);
    return static_cast<int>(textures_.size() - 1);
}

int GltfWriter::add_material(const PskMaterial& psk_mat)
{
    for (size_t i = 0; i < gltf_materials_.size(); i++) {
        if (gltf_materials_[i].name == psk_mat.name) return static_cast<int>(i);
    }

    GltfMaterial mat;
    mat.name = psk_mat.name;

    for (const auto& m : materials_.materials()) {
        if (m.name == psk_mat.name) {
            mat.base_color_texture = add_texture(m.diffuse);
            mat.normal_texture = add_texture(m.normal);
            mat.metallic_roughness_texture = add_texture(m.specular);
            mat.emissive_texture = add_texture(m.emissive);
            break;
        }
    }

    gltf_materials_.push_back(mat);
    return static_cast<int>(gltf_materials_.size() - 1);
}

int GltfWriter::add_buffer(std::vector<uint8_t> data, const std::string& uri)
{
    GltfBuffer buf;
    buf.data = std::move(data);
    buf.uri = uri;
    buffers_.push_back(std::move(buf));
    return static_cast<int>(buffers_.size() - 1);
}

bool GltfWriter::write_static_mesh(const std::string& psk_path, const std::string& mesh_name)
{
    PskReader reader;
    PskMesh mesh;
    if (!reader.read(psk_path, mesh)) return false;

    fs::path out_dir = fs::path(output_dir_) / "static-meshes" / fs::path(mesh_name).parent_path();
    fs::create_directories(out_dir);

    std::string bin_filename = fs::path(mesh_name).filename().string() + ".bin";
    std::string gltf_filename = fs::path(mesh_name).filename().string() + ".gltf";

    std::vector<uint8_t> bin_data;

    auto add_to_buffer = [&](const void* p, size_t size) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        while (bin_data.size() % 4 != 0) bin_data.push_back(0);
        size_t offset = bin_data.size();
        bin_data.insert(bin_data.end(), b, b + size);
        return offset;
    };

    auto write_accessor = [](std::ostringstream& ss, int bv, int off, int ct, const char* type,
                            const float* mn = nullptr, const float* mx = nullptr) {
        ss << "{";
        ss << "\"bufferView\":" << bv << ",";
        ss << "\"byteOffset\":" << off << ",";
        ss << "\"componentType\":5126,";
        ss << "\"count\":" << ct << ",";
        ss << "\"type\":\"" << type << "\"";
        if (mn) {
            ss << ",\"min\":[" << mn[0] << "," << mn[1] << "," << mn[2] << "]";
        }
        if (mx) {
            ss << ",\"max\":[" << mx[0] << "," << mx[1] << "," << mx[2] << "]";
        }
        ss << "}";
    };

    size_t pos_off = add_to_buffer(mesh.vertices.data(), mesh.vertices.size() * sizeof(PskVertex));
    size_t pos_data_off = 0;
    size_t norm_data_off = sizeof(float) * 3;
    size_t uv_data_off = sizeof(float) * 6;

    float mn[3] = {1e30f, 1e30f, 1e30f};
    float mx[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& v : mesh.vertices) {
        for (int i = 0; i < 3; i++) {
            mn[i] = std::min(mn[i], v.pos[i]);
            mx[i] = std::max(mx[i], v.pos[i]);
        }
    }

    size_t idx_off = add_to_buffer(mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));

    int mat_idx = add_material(mesh.materials.empty() ? PskMaterial{mesh_name} : mesh.materials[0]);

    std::ostringstream json;
    json << "{\n";
    json << "  \"asset\": {\"version\": \"2.0\", \"generator\": \"tascend_importer\"},\n";
    json << "  \"scene\": 0,\n";
    json << "  \"scenes\": [{\"nodes\": [0]}],\n";
    json << "  \"nodes\": [{\"mesh\": 0, \"name\": \"" << mesh_name << "\"}],\n";

    json << "  \"buffers\": [{\"uri\": \"" << bin_filename << "\", \"byteLength\": " << bin_data.size() << "}],\n";

    json << "  \"bufferViews\": [\n";
    json << "    {\"buffer\": 0, \"byteOffset\": " << pos_off << ", \"byteLength\": " << (mesh.vertices.size() * sizeof(PskVertex)) << ", \"byteStride\": " << sizeof(PskVertex) << "},\n";
    json << "    {\"buffer\": 0, \"byteOffset\": " << idx_off << ", \"byteLength\": " << (mesh.indices.size() * sizeof(uint32_t)) << "}\n";
    json << "  ],\n";

    json << "  \"accessors\": [\n";
    json << "    "; write_accessor(json, 0, pos_data_off, mesh.vertices.size(), "VEC3", mn, mx); json << ",\n";
    json << "    "; write_accessor(json, 0, norm_data_off, mesh.vertices.size(), "VEC3"); json << ",\n";
    json << "    "; write_accessor(json, 0, uv_data_off, mesh.vertices.size(), "VEC2"); json << ",\n";
    json << "    {\"bufferView\": 1, \"componentType\": 5125, \"count\": " << mesh.indices.size() << ", \"type\": \"SCALAR\"}\n";
    json << "  ],\n";

    json << "  \"images\": [\n";
    for (size_t i = 0; i < images_.size(); i++) {
        json << "    {\"uri\": \"" << images_[i].uri << "\"}";
        if (i + 1 < images_.size()) json << ",";
        json << "\n";
    }
    json << "  ],\n";

    json << "  \"textures\": [\n";
    for (size_t i = 0; i < textures_.size(); i++) {
        json << "    {\"source\": " << textures_[i].image_index << "}";
        if (i + 1 < textures_.size()) json << ",";
        json << "\n";
    }
    json << "  ],\n";

    json << "  \"materials\": [\n";
    for (size_t i = 0; i < gltf_materials_.size(); i++) {
        const auto& m = gltf_materials_[i];
        json << "    {\"name\": \"" << m.name << "\",";
        json << "\"pbrMetallicRoughness\": {";
        json << "\"baseColorFactor\": [" << m.base_color_factor[0] << "," << m.base_color_factor[1] << "," << m.base_color_factor[2] << "," << m.base_color_factor[3] << "],";
        json << "\"metallicFactor\": " << m.metallic_factor << ",";
        json << "\"roughnessFactor\": " << m.roughness_factor;
        if (m.base_color_texture >= 0) json << ",\"baseColorTexture\": {\"index\": " << m.base_color_texture << "}";
        json << "}";
        if (m.normal_texture >= 0) json << ",\"normalTexture\": {\"index\": " << m.normal_texture << "}";
        json << "}";
        if (i + 1 < gltf_materials_.size()) json << ",";
        json << "\n";
    }
    json << "  ],\n";

    json << "  \"meshes\": [{\n";
    json << "    \"name\": \"" << mesh_name << "\",\n";
    json << "    \"primitives\": [{";
    json << "\"attributes\": {\"POSITION\": 0, \"NORMAL\": 1, \"TEXCOORD_0\": 2}, ";
    json << "\"indices\": 3";
    if (mat_idx >= 0) json << ", \"material\": " << mat_idx;
    json << "}]\n";
    json << "  }]\n";
    json << "}\n";

    {
        std::ofstream bf(out_dir / bin_filename, std::ios::binary);
        bf.write(reinterpret_cast<const char*>(bin_data.data()), bin_data.size());
    }
    {
        std::ofstream jf(out_dir / gltf_filename);
        jf << json.str();
    }

    return true;
}

bool GltfWriter::write_map(const std::string& map_name, const std::vector<GltfNode>& nodes,
                           const std::vector<std::string>& mesh_refs)
{
    (void)map_name; (void)nodes; (void)mesh_refs;
    return false;
}

std::string GltfWriter::serialize_json(const std::string& scene_name) const
{
    (void)scene_name;
    return "{}";
}

}
