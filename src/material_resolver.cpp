#include "tascend/material_resolver.hh"

#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace tascend {

namespace fs = std::filesystem;

TexRole classify_texture(const std::string& name)
{
    if (name.empty()) return TexRole::None;

    std::string s;
    s.reserve(name.size());
    for (char c : name) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    // Strip a trailing run of digits so `_DIF00` / `_MSK_02` classify like
    // `_DIF` / `_MSK`. Some export names carry an index suffix.
    size_t end = s.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(s[end - 1]))) end--;
    s.resize(end);

    struct Entry { const char* suffix; TexRole role; };
    // Longest suffix first inside each family so `_diffuse` wins over `_dif`.
    static const Entry table[] = {
        { "_diffuse",  TexRole::Albedo },
        { "_diff",     TexRole::Albedo },
        { "_dif",      TexRole::Albedo },
        { "_normal",   TexRole::Normal },
        { "_norm",     TexRole::Normal },
        { "_nrm",      TexRole::Normal },
        { "_emissive", TexRole::Emissive },
        { "_emiss",    TexRole::Emissive },
        { "_emit",     TexRole::Emissive },
        { "_emi",      TexRole::Emissive },
        { "_glow",     TexRole::Emissive },
        { "_specular", TexRole::Spec },
        { "_spec",     TexRole::Spec },
        { "_spc",      TexRole::Spec },
        { "_opacity",  TexRole::Opacity },
        { "_opa",      TexRole::Opacity },
        { "_alpha",    TexRole::Opacity },
        { "_mask",     TexRole::Mask },
        { "_msk",      TexRole::Mask },
        { "_cubemap",  TexRole::Cube },
        { "_cubeext",  TexRole::Cube },
        { "_cubeface", TexRole::Cube },
        { "_cube",     TexRole::Cube },
        { "_cub",      TexRole::Cube },
    };

    for (const auto& e : table) {
        size_t n = std::strlen(e.suffix);
        if (s.size() >= n && s.compare(s.size() - n, n, e.suffix) == 0) return e.role;
    }
    // Cubemaps are exported as six faces named `<name>Face_<axis>`; the faces are
    // not usable as a 2D colour map and must not be published as one.
    if (s.rfind("cubeface", 0) == 0 || s.find("cubeface") != std::string::npos) return TexRole::Cube;
    return TexRole::Other;
}

namespace {

std::string trim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string alnum_lower(const std::string& s)
{
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)))
            o.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return o;
}

void strip_role_suffix(std::string& s)
{
    static const char* suffixes[] = {
        "diffuse", "diff", "dif",
        "normal", "norm", "nrm",
        "emissive", "emiss", "emit", "emi", "glow",
        "specular", "spec", "spc",
        "opacity", "opa", "alpha",
        "mask", "msk",
        "cubemap", "cube", "cub",
    };
    for (const char* suf : suffixes) {
        size_t n = std::strlen(suf);
        if (s.size() > n && s.compare(s.size() - n, n, suf) == 0) {
            s.resize(s.size() - n);
            return;
        }
    }
}

/// Normalised stem of a *material* name: `MAT_BellaOmega_Skybox` -> `bellaomegaskybox`.
std::string material_stem(const std::string& name)
{
    std::string s = alnum_lower(name);
    if (s.rfind("mat", 0) == 0 || s.rfind("mic", 0) == 0) s = s.substr(3);
    strip_role_suffix(s);
    return s;
}

/// Normalised stem of a *texture* name: `T_BellaOmega_Skybox_EMI` -> `bellaomegaskybox`.
std::string texture_stem(const std::string& name)
{
    std::string s = alnum_lower(name);
    strip_role_suffix(s);
    if (s.rfind("texture", 0) == 0) s = s.substr(7);
    else if (s.rfind("tex", 0) == 0) s = s.substr(3);
    else if (s.size() > 1 && s[0] == 't') s = s.substr(1);
    return s;
}

/// How well a candidate texture name matches a material name. Used only to break
/// ties between several same-role textures in one material's reference list: e.g.
/// `MAT_BellaOmega_Skybox` references three `_EMI` textures (two cubemap faces and
/// the full panorama) and we must pick the panorama. Exact stem matches score
/// highest; no match scores 0 and leaves the first candidate in chain order.
int name_match_score(const std::string& mat_stem, const std::string& tex_stem)
{
    if (mat_stem.size() < 4 || tex_stem.size() < 4) return 0;
    if (mat_stem.find(tex_stem) != std::string::npos) return static_cast<int>(tex_stem.size());
    return 0;
}

bool parse_bool(const std::string& v)
{
    std::string s = alnum_lower(v);
    return s == "true" || s == "1" || s == "yes";
}

} // namespace

MaterialResolver::MaterialResolver(const std::string& raw_dir)
    : raw_dir_(raw_dir)
{
}

void MaterialResolver::scan_materials()
{
    materials_.clear();
    by_name_.clear();

    // Collect first, then sort. `recursive_directory_iterator` order is not
    // specified, and 75 material names appear in several packages with different
    // contents; sorting makes "first match wins" reproducible across runs.
    std::vector<std::string> paths;
    if (fs::exists(raw_dir_)) {
        for (const auto& entry : fs::recursive_directory_iterator(raw_dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".mat") {
                paths.push_back(entry.path().string());
            }
        }
    }
    std::sort(paths.begin(), paths.end());

    materials_.reserve(paths.size());
    for (const auto& path : paths) {
        MaterialInfo info;
        info.name = fs::path(path).stem().string();
        parse_mat_file(path, info);
        info.type = fs::path(path).parent_path().filename().string();

        // The companion `.props.txt` is a dump of the real UE3 object and is the
        // only source for blend mode / two-sidedness / parent linkage.
        fs::path props = fs::path(path);
        props.replace_extension(".props.txt");
        if (fs::exists(props)) parse_props_file(props.string(), info);

        by_name_.emplace(info.name, materials_.size());
        materials_.push_back(std::move(info));
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

void MaterialResolver::parse_mat_file(const std::string& path, MaterialInfo& out) const
{
    std::ifstream f(path);
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (val.empty()) continue;

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
    // NOTE: no `Other[0]` fallback here. Which slot a texture belongs in is
    // decided by `resolve_material_chain`, which can tell a colour map from a
    // normal/mask map and can consult the parent material.
}

void MaterialResolver::parse_props_file(const std::string& path, MaterialInfo& out) const
{
    std::ifstream f(path);
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line)) {
        // Nested entries are indented; only top-level properties are meaningful
        // here (an indented `ParameterName` must not be mistaken for a property).
        if (line.empty() || line[0] == ' ' || line[0] == '\t') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "Parent") {
            auto q1 = val.find('\'');
            auto q2 = val.rfind('\'');
            if (q1 != std::string::npos && q2 > q1) {
                std::string full = val.substr(q1 + 1, q2 - q1 - 1);
                out.parent_path = full;
                auto dot = full.rfind('.');
                out.parent = (dot == std::string::npos) ? full : full.substr(dot + 1);
            }
        } else if (key == "BlendMode") {
            // `BLEND_Masked (1)` — the enum *name* is absent in older packages,
            // so the number in parentheses is the reliable part.
            auto lp = val.find('(');
            if (lp != std::string::npos) {
                int n = std::atoi(val.c_str() + lp + 1);
                if (n >= 0 && n <= 4) {
                    out.blend_mode = static_cast<BlendMode>(n);
                    out.blend_mode_known = true;
                }
            }
        } else if (key == "TwoSided") {
            out.two_sided = parse_bool(val);
            out.declares_two_sided = true;
        } else if (key == "bIsMasked") {
            out.is_masked = parse_bool(val);
            out.declares_is_masked = true;
        } else if (key == "OpacityMaskClipValue") {
            try {
                out.opacity_mask_clip = std::stof(val);
                out.declares_opacity_clip = true;
            } catch (...) {
                // leave the UE3 default
            }
        }
    }
}

std::vector<size_t> MaterialResolver::parent_chain(size_t start) const
{
    std::vector<size_t> chain;
    std::vector<bool> seen(materials_.size(), false);
    size_t cur = start;
    while (cur < materials_.size() && !seen[cur] && chain.size() < 16) {
        seen[cur] = true;
        chain.push_back(cur);
        const std::string& p = materials_[cur].parent;
        if (p.empty()) break;
        auto it = by_name_.find(p);
        if (it == by_name_.end()) break;
        cur = it->second;
    }
    return chain;
}

void MaterialResolver::resolve_material_chain()
{
    for (size_t i = 0; i < materials_.size(); i++) {
        MaterialInfo& mat = materials_[i];
        const std::vector<size_t> chain = parent_chain(i);

        // --- Inherit material flags down the chain (nearest ancestor wins). ---
        if (!mat.blend_mode_known) {
            for (size_t j : chain) {
                if (materials_[j].blend_mode_known) {
                    mat.blend_mode = materials_[j].blend_mode;
                    mat.blend_mode_known = true;
                    break;
                }
            }
        }
        if (!mat.declares_two_sided) {
            for (size_t j : chain) {
                if (materials_[j].declares_two_sided) {
                    mat.two_sided = materials_[j].two_sided;
                    mat.declares_two_sided = true;
                    break;
                }
            }
        }
        if (!mat.declares_is_masked) {
            for (size_t j : chain) {
                if (materials_[j].declares_is_masked) {
                    mat.is_masked = materials_[j].is_masked;
                    mat.declares_is_masked = true;
                    break;
                }
            }
        }
        if (!mat.declares_opacity_clip) {
            for (size_t j : chain) {
                if (materials_[j].declares_opacity_clip) {
                    mat.opacity_mask_clip = materials_[j].opacity_mask_clip;
                    mat.declares_opacity_clip = true;
                    break;
                }
            }
        }

        // `bIsMasked` is redundant with `BlendMode == Masked` but is present on
        // materials whose blend mode lives in an unreadable expression graph.
        if (mat.is_masked) mat.blend_mode = BlendMode::Masked, mat.blend_mode_known = true;

        // --- Resolve remaining texture slots through the chain. ---
        auto first_nonempty = [&](std::string MaterialInfo::*field) -> std::string {
            for (size_t j : chain)
                if (!(materials_[j].*field).empty()) return materials_[j].*field;
            return {};
        };
        if (mat.specular.empty())   mat.specular = first_nonempty(&MaterialInfo::specular);
        if (mat.spec_power.empty()) mat.spec_power = first_nonempty(&MaterialInfo::spec_power);
        if (mat.cube.empty())       mat.cube = first_nonempty(&MaterialInfo::cube);
        if (mat.opacity.empty()) {
            mat.opacity = first_nonempty(&MaterialInfo::opacity);
            if (mat.opacity.empty()) {
                // Fall back to any `_OPA`/`_Alpha` texture referenced anywhere in
                // the chain.
                for (size_t j : chain) {
                    for (const auto& t : materials_[j].other_textures) {
                        if (classify_texture(t) == TexRole::Opacity) { mat.opacity = t; break; }
                    }
                    if (!mat.opacity.empty()) break;
                }
            }
        }
        if (mat.mask.empty()) {
            for (size_t j : chain) {
                for (const auto& t : materials_[j].other_textures) {
                    if (classify_texture(t) == TexRole::Mask) { mat.mask = t; break; }
                }
                if (!mat.mask.empty()) break;
            }
        }

        // --- Albedo. ---
        // Pass 1: the nearest ancestor that actually declares a colour map in
        // `Diffuse`. Pass 2: a `_DIF` referenced anywhere in the chain. Pass 3:
        // an unsuffixed texture, which in this corpus is always a colour map.
        // Anything else means the base colour is a material expression default
        // that the `.mat` dump does not contain, and we must not publish a normal
        // or mask map as the albedo.
        mat.albedo_source.clear();
        mat.albedo_unresolved = false;

        bool found = false;
        for (size_t j : chain) {
            const std::string& d = materials_[j].diffuse;
            if (!d.empty() && role_is_albedo_usable(classify_texture(d))) {
                mat.diffuse = d;
                mat.albedo_source = (j == i) ? "diffuse" : "parent_diffuse";
                found = true;
                break;
            }
        }
        if (!found) {
            for (size_t j : chain) {
                for (const auto& t : materials_[j].other_textures) {
                    if (classify_texture(t) == TexRole::Albedo) {
                        mat.diffuse = t;
                        mat.albedo_source = "other_dif";
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
        }
        if (!found) {
            for (size_t j : chain) {
                for (const auto& t : materials_[j].other_textures) {
                    if (classify_texture(t) == TexRole::Other) {
                        mat.diffuse = t;
                        mat.albedo_source = "other";
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
        }
        if (!found) {
            mat.albedo_source = "none";
            mat.albedo_unresolved = true;
        }

        // --- Normal. ---
        mat.normal_source.clear();
        if (!mat.normal.empty() && classify_texture(mat.normal) == TexRole::Normal) {
            mat.normal_source = "normal";
        } else {
            std::string picked;
            for (size_t j : chain) {
                for (const auto& t : materials_[j].other_textures) {
                    if (classify_texture(t) == TexRole::Normal) { picked = t; break; }
                }
                if (!picked.empty()) break;
            }
            if (!picked.empty()) {
                mat.normal = picked;
                mat.normal_source = "other_nrm";
            } else {
                mat.normal.clear();
                mat.normal_source = "none";
            }
        }

        // --- Emissive. ---
        // Several materials reference more than one `_EMI` (a cubemap's faces plus
        // the panorama, a parent's generic glow map plus the material's own).
        // Prefer the candidate whose name matches the material's.
        mat.emissive_source.clear();
        if (!mat.emissive.empty()) {
            mat.emissive_source = "emissive";
        } else {
            // Some materials put their self-illumination straight in `Diffuse`
            // (`MAT_ArxNovena_Skybox_CityAddon` is `Diffuse=T_..._EMI` with no
            // other reference at all). Without this the material would have no
            // colour source whatsoever.
            bool stole_diffuse = false;
            for (size_t j : chain) {
                const std::string& d = materials_[j].diffuse;
                if (!d.empty() && classify_texture(d) == TexRole::Emissive) {
                    mat.emissive = d;
                    mat.emissive_source = "diffuse_emi";
                    stole_diffuse = true;
                    break;
                }
            }

            if (!stole_diffuse) {
                const std::string mat_stem = material_stem(mat.name);
                std::string best;
                int best_score = 0;
                for (size_t j : chain) {
                    for (const auto& t : materials_[j].other_textures) {
                        if (classify_texture(t) != TexRole::Emissive) continue;
                        int score = name_match_score(mat_stem, texture_stem(t));
                        if (best.empty() || score > best_score) {
                            best = t;
                            best_score = score;
                        }
                    }
                }
                if (!best.empty()) {
                    mat.emissive = best;
                    mat.emissive_source = "other_emi";
                } else {
                    mat.emissive_source = "none";
                }
            }
        }
    }
}

void MaterialResolver::write_manifest(const std::string& output_path) const
{
    fs::path p(output_path);
    if (!p.parent_path().empty()) fs::create_directories(p.parent_path());

    std::ofstream f(output_path);
    if (!f.is_open()) return;

    static const char* blend_names[] = {"OPAQUE", "MASK", "BLEND", "BLEND", "BLEND"};

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
        f << "      \"albedo_source\": \"" << m.albedo_source << "\",\n";
        f << "      \"normal_source\": \"" << m.normal_source << "\",\n";
        f << "      \"emissive_source\": \"" << m.emissive_source << "\",\n";
        f << "      \"albedo_unresolved\": " << (m.albedo_unresolved ? "true" : "false") << ",\n";
        f << "      \"blend_mode\": \"" << (m.blend_mode_known ? blend_names[static_cast<int>(m.blend_mode)] : "UNKNOWN") << "\",\n";
        f << "      \"two_sided\": " << (m.two_sided ? "true" : "false") << ",\n";
        f << "      \"is_masked\": " << (m.is_masked ? "true" : "false") << ",\n";
        f << "      \"opacity_mask_clip\": " << m.opacity_mask_clip << ",\n";
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
