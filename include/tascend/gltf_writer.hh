#pragma once

#include <string>
#include <vector>
#include "psk_reader.hh"
#include "material_resolver.hh"

namespace tascend {

struct GltfBuffer {
    std::vector<uint8_t> data;
    std::string uri;  // relative path to .bin file
};

struct GltfImage {
    std::string uri;  // relative path to .png file
};

struct GltfTexture {
    int image_index = -1;
};

struct GltfMaterial {
    std::string name;
    int base_color_texture = -1;     // index into textures_
    int normal_texture = -1;
    int metallic_roughness_texture = -1;
    int emissive_texture = -1;
    float base_color_factor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float metallic_factor = 0.0f;
    float roughness_factor = 0.5f;
};

struct GltfPrimitive {
    int positions_accessor = -1;
    int normals_accessor = -1;
    int texcoords_accessor = -1;
    int indices_accessor = -1;
    int material = -1;
};

struct GltfMesh {
    std::string name;
    std::vector<GltfPrimitive> primitives;
};

struct GltfNode {
    std::string name;
    int mesh = -1;
    float translation[3] = {0, 0, 0};
    float rotation[4] = {0, 0, 0, 1};  // quaternion
    float scale[3] = {1, 1, 1};
};

class GltfWriter {
public:
    GltfWriter(const std::string& output_dir, const MaterialResolver& materials);

    bool write_static_mesh(const std::string& psk_path, const std::string& mesh_name);
    bool write_map(const std::string& map_name, const std::vector<GltfNode>& nodes,
                   const std::vector<std::string>& mesh_refs);

private:
    std::string output_dir_;
    const MaterialResolver& materials_;
    std::vector<GltfBuffer> buffers_;
    std::vector<GltfImage> images_;
    std::vector<GltfTexture> textures_;
    std::vector<GltfMaterial> gltf_materials_;
    std::vector<GltfMesh> meshes_;
    std::vector<GltfNode> nodes_;

    int add_material(const PskMaterial& psk_mat);
    int add_texture(const std::string& texture_name);
    int add_image(const std::string& png_path);
    int add_buffer(std::vector<uint8_t> data, const std::string& uri);

    std::string serialize_json(const std::string& scene_name) const;
};

}
