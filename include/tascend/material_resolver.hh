#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace tascend {

/// UE3 `EBlendMode`. The numeric value is what `BlendMode = BLEND_Masked (1)`
/// carries in the companion `.props.txt`; the enum name is not printed in old
/// packages, so we key off the number.
enum class BlendMode : int {
    Opaque = 0,
    Masked = 1,
    Translucent = 2,
    Additive = 3,
    Modulate = 4,
};

/// What a texture name is, inferred from its suffix (`_DIF`, `_NRM`, `_MSK`, ...).
///
/// This matters because a `.mat` file's `Diffuse=` key is *not* trustworthy: for
/// materials whose base colour comes from a material expression default (or a
/// parent's parameter), the exporter falls back to whatever texture it happened
/// to reference first, which is frequently a normal or mask map. Feeding a
/// `_MSK` into `baseColorTexture` is what produced magenta rocks and periwinkle
/// water in the client.
enum class TexRole {
    Albedo,     // _DIF / _DIFF / _Diffuse
    Normal,     // _NRM / _Norm / _Normal
    Mask,       // _MSK / _Mask
    Spec,       // _SPC / _Spec / _Specular
    Emissive,   // _EMI / _Emissive / _Glow
    Opacity,    // _OPA / _Opacity / _Alpha
    Cube,       // _CUB / _Cube / _Cubemap
    Other,      // no recognised suffix (a plain colour map in this corpus)
    None,       // no name at all
};

/// Classify a texture object name (no file extension, e.g. `T_Rock_01_DIF`).
TexRole classify_texture(const std::string& name);

/// True for the roles that are usable as `baseColorTexture`.
inline bool role_is_albedo_usable(TexRole r) { return r == TexRole::Albedo || r == TexRole::Other; }

struct MaterialInfo {
    std::string name;
    std::string type;          // "Material3" or "MaterialInstanceConstant"
    std::string parent;        // for MIC: parent material object name (from props.txt)
    std::string parent_path;   // full `Package.Group.Object` path of the parent, if known
    std::string diffuse;       // texture name for Diffuse slot
    std::string normal;        // texture name for Normal slot
    std::string specular;      // texture name for Specular slot
    std::string spec_power;    // texture name for SpecPower slot
    std::string opacity;       // texture name for Opacity slot
    std::string emissive;      // texture name for Emissive slot
    std::string cube;          // texture name for Cube slot
    std::string mask;          // texture name for Mask slot
    std::vector<std::string> other_textures;

    /// Provenance of `diffuse` / `normal` / `emissive` after role resolution.
    /// One of: `diffuse`, `parent_diffuse`, `other_dif`, `other`, `normal`,
    /// `other_nrm`, `emissive`, `other_emi`, `none`. Recorded in the manifest so
    /// every heuristic guess is auditable rather than silent.
    std::string albedo_source;
    std::string normal_source;
    std::string emissive_source;

    /// Authoritative material flags from the companion `.props.txt`, which is a
    /// dump of the actual UE3 object. `blend_mode_known` is false when neither
    /// the material nor any ancestor declares one.
    bool blend_mode_known = false;
    BlendMode blend_mode = BlendMode::Opaque;
    bool two_sided = false;
    bool is_masked = false;
    float opacity_mask_clip = 0.333f;

    /// Whether the material *itself* declared the flag. A `false` default is
    /// indistinguishable from "not declared", so inheritance needs to know.
    bool declares_two_sided = false;
    bool declares_is_masked = false;
    bool declares_opacity_clip = false;

    /// The `.mat` did not yield a usable colour map at any point in the parent
    /// chain, so `diffuse` is empty or a non-colour map. The glTF writer must
    /// not publish it as `baseColorTexture`.
    bool albedo_unresolved = false;
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
    /// `name -> index into materials_`, first match in sorted-path order.
    std::unordered_map<std::string, size_t> by_name_;

    void parse_mat_file(const std::string& path, MaterialInfo& out) const;
    void parse_props_file(const std::string& path, MaterialInfo& out) const;
    void find_textures(const std::string& dir);

    /// Walk `parent` links from `start`, yielding `start` first. Cycles and
    /// over-deep chains are cut off.
    std::vector<size_t> parent_chain(size_t start) const;
};

}
