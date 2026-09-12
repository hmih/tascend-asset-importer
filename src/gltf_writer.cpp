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

static bool material_is_translucent(const std::string& name, const MaterialInfo& info)
{
    static const char* translucency_keywords[] = {
        "water", "Water", "river", "River", "ocean", "Ocean",
        "forcefield", "ForceField", "force_field", "Force_Field",
        "glass", "Glass",
        "hologram", "Hologram", "Holo",
        "smoke", "Smoke", "steam", "Steam", "fog", "Fog", "dust", "Dust",
        "cloud", "Cloud", "sky", "Sky",
        "blend", "Blend", "translucent", "Translucent",
        "additive", "Additive", "modulate", "Modulate",
        "distort", "Distort", "beam", "Beam",
        "fx_", "FX_", "pfx_", "PFX_",
        "splash", "Splash",
        "energy", "Energy", "shield", "Shield",
    };
    const int kw_count = sizeof(translucency_keywords) / sizeof(translucency_keywords[0]);

    std::string name_lower = name;
    for (auto& c : name_lower) c = static_cast<char>(std::tolower(c));

    for (int i = 0; i < kw_count; i++) {
        std::string kw(translucency_keywords[i]);
        for (auto& c : kw) c = static_cast<char>(std::tolower(c));
        if (name_lower.find(kw) != std::string::npos) return true;
    }

    if (!info.opacity.empty()) return true;
    return false;
}

int GltfWriter::add_material(const PskMaterial& psk_mat)
{
    for (size_t i = 0; i < gltf_materials_.size(); i++) {
        if (gltf_materials_[i].name == psk_mat.name) return static_cast<int>(i);
    }

    GltfMaterial mat;
    mat.name = psk_mat.name;

    const MaterialInfo* info = nullptr;
    for (const auto& m : materials_.materials()) {
        if (m.name == psk_mat.name) { info = &m; break; }
    }

    if (info) {
        mat.albedo_source = info->albedo_source;

        // `albedo_unresolved` means every candidate in the parent chain was a
        // normal/mask/emissive map: publishing one as `baseColorTexture` is what
        // produced magenta rocks and periwinkle water. Better to ship no texture
        // and a neutral factor. The same applies when the chosen texture has no
        // exported PNG (cubemap faces and other non-2D textures).
        if (!info->albedo_unresolved) mat.base_color_texture = add_texture(info->diffuse);
        mat.normal_texture = add_texture(info->normal);
        mat.metallic_roughness_texture = add_texture(info->specular);
        mat.emissive_texture = add_texture(info->emissive);

        const bool no_albedo_image = info->albedo_unresolved || mat.base_color_texture < 0;

        if (info->blend_mode_known) {
            switch (info->blend_mode) {
                case BlendMode::Masked:
                    // Cut-out geometry (foliage, ivy, hedges, banners). Bevy's
                    // masked pass discards below `alpha_cutoff` and still writes
                    // depth, which BLEND cannot do.
                    mat.alpha_mode = "MASK";
                    mat.alpha_cutoff = info->opacity_mask_clip;
                    mat.alpha_source = "masked";
                    break;
                case BlendMode::Translucent:
                    mat.alpha_mode = "BLEND";
                    mat.alpha_source = "translucent";
                    break;
                case BlendMode::Additive:
                case BlendMode::Modulate:
                    // glTF has no additive or modulate blend mode; BLEND is the
                    // closest the format offers. The alpha handling below decides
                    // how visible that leaves the surface.
                    mat.alpha_mode = "BLEND";
                    mat.alpha_source = "additive";
                    break;
                case BlendMode::Opaque:
                    mat.alpha_mode = "OPAQUE";
                    mat.alpha_source = "opaque";
                    break;
            }
        } else if (material_is_translucent(psk_mat.name, *info)) {
            // No blend mode anywhere in the chain: keep the old name heuristic.
            mat.alpha_mode = "BLEND";
            mat.alpha_source = "name_heuristic";
        }

        // Restrict two-sidedness to cut-out cards. UE3 marks them `TwoSided`
        // because a leaf card is seen from both sides; enabling it broadly is
        // what broke roof shading, since Bevy's `prepare_world_normal` inverts
        // the normal on the back face of a double-sided material.
        mat.double_sided = info->two_sided && mat.alpha_mode == "MASK";

        if (no_albedo_image) {
            if (mat.emissive_texture >= 0) {
                // Unlit / self-illuminated material (sky dome, city add-on,
                // lights, holograms): the emissive slot carries the image, so the
                // diffuse contribution is blacked out rather than lit.
                mat.base_color_factor[0] = 0.0f;
                mat.base_color_factor[1] = 0.0f;
                mat.base_color_factor[2] = 0.0f;
                mat.alpha_source = "emissive_only";
            } else if (info->blend_mode == BlendMode::Additive
                       || info->blend_mode == BlendMode::Modulate) {
                // An additive surface with no recoverable texture contributes
                // nothing: adding black to the framebuffer is a no-op. Rendering
                // it as opaque geometry instead is how a 81920x35840 `Creativity
                // Wall` boundary blocker turned into a solid grey slab across the
                // middle of the map.
                mat.base_color_factor[3] = 0.0f;
                mat.alpha_source = "additive_noop";
            } else if (mat.alpha_mode == "BLEND") {
                // Translucent, but every colour/opacity candidate was a mask or
                // normal map. Keep the geometry legible as a faint tint rather
                // than an opaque slab. The exact value is a heuristic — recorded
                // in `extras.alpha_source` so it is auditable.
                mat.base_color_factor[0] = 0.5f;
                mat.base_color_factor[1] = 0.5f;
                mat.base_color_factor[2] = 0.5f;
                mat.base_color_factor[3] = 0.35f;
                mat.alpha_source = "translucent_default";
            } else {
                // Genuinely unknown base colour (a material-expression default
                // the `.mat` dump does not contain). Mid grey reads as untextured
                // instead of blowing out to white.
                mat.base_color_factor[0] = 0.5f;
                mat.base_color_factor[1] = 0.5f;
                mat.base_color_factor[2] = 0.5f;
                mat.alpha_source = "opaque_default";
            }
        }
    } else {
        MaterialInfo bare;
        bare.name = psk_mat.name;
        if (material_is_translucent(psk_mat.name, bare)) mat.alpha_mode = "BLEND";
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
        if (m.emissive_texture >= 0) {
            json << ",\"emissiveTexture\": {\"index\": " << m.emissive_texture << "}";
            json << ",\"emissiveFactor\": [" << m.emissive_factor[0] << "," << m.emissive_factor[1] << "," << m.emissive_factor[2] << "]";
        }
        if (!m.alpha_mode.empty() && m.alpha_mode != "OPAQUE") {
            json << ",\"alphaMode\": \"" << m.alpha_mode << "\"";
            if (m.alpha_mode == "MASK") json << ",\"alphaCutoff\": " << m.alpha_cutoff;
        }
        if (m.double_sided) json << ",\"doubleSided\": true";
        json << ",\"extras\": {\"albedo_source\": \"" << m.albedo_source
             << "\", \"alpha_source\": \"" << m.alpha_source << "\"}";
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
