#include "Core.h"
#include "UnCore.h"

#include "tascend/pipeline.hh"
#include "tascend/package_scanner.hh"
#include "tascend/export_driver.hh"
#include "tascend/material_resolver.hh"
#include "tascend/gltf_writer.hh"
#include "tascend/map_assembler.hh"
#include "tascend/blender_bridge.hh"
#include "tascend/actor_extractor.hh"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <map>
#include <cstring>
#include <sys/stat.h>

namespace tascend {

namespace fs = std::filesystem;

Pipeline::Pipeline(const PipelineConfig& config)
    : config_(config)
    , raw_dir_(config.output_root + "/raw")
    , gltf_dir_(config.output_root + "/gltf")
{
    fs::create_directories(raw_dir_);
    fs::create_directories(gltf_dir_);
}

int Pipeline::run_all()
{
    if (!config_.skip_extract) {
        if (!extract_stage()) return 1;
    }
    if (!config_.skip_convert) {
        if (!convert_stage()) return 1;
    }
    if (!config_.skip_skeletal) {
        if (!skeletal_stage()) return 1;
    }
    std::cout << "Pipeline complete." << std::endl;
    return 0;
}

int Pipeline::run_scan()
{
    auto manifest = scan_packages();
    std::cout << "Found " << manifest.packages.size() << " packages and "
              << manifest.maps.size() << " map files." << std::endl;
    for (const auto& p : manifest.packages) {
        std::cout << "  [pkg] " << p.name << "." << p.type << std::endl;
    }
    for (const auto& m : manifest.maps) {
        std::cout << "  [map] " << m.map_name << std::endl;
    }
    return 0;
}

int Pipeline::run_extract()
{
    return extract_stage() ? 0 : 1;
}

int Pipeline::run_convert()
{
    return convert_stage() ? 0 : 1;
}

int Pipeline::run_dedup()
{
    return dedup_stage() ? 0 : 1;
}

int Pipeline::run_extract_actors()
{
    return extract_actors_stage() ? 0 : 1;
}

int Pipeline::run_map(const std::string& map_name)
{
    ExportConfig exp_config;
    exp_config.game_path = config_.game_path;
    exp_config.output_dir = raw_dir_;

    ExportDriver driver(exp_config);

    fs::path maps_dir = fs::path(config_.game_path) / "Maps" / map_name;
    if (!fs::exists(maps_dir)) {
        std::cerr << "Map directory not found: " << maps_dir << std::endl;
        return 1;
    }

    for (const auto& entry : fs::directory_iterator(maps_dir)) {
        if (entry.path().extension() == ".fmap") {
            std::cout << "Exporting map: " << entry.path().filename() << std::endl;
            driver.export_package(entry.path().string());
        }
    }

    MapAssembler assembler(raw_dir_, gltf_dir_);
    fs::path scene_json = fs::path(raw_dir_) / "maps" / map_name / (map_name + ".scene.json");
    assembler.assemble_map(scene_json.string(), map_name);

    return 0;
}

static bool files_match(const std::string& a, const std::string& b)
{
    std::ifstream fa(a, std::ios::binary);
    std::ifstream fb(b, std::ios::binary);
    if (!fa || !fb) return false;

    const size_t buf_size = 65536;
    std::vector<char> bufa(buf_size), bufb(buf_size);
    while (fa && fb) {
        fa.read(bufa.data(), buf_size);
        fb.read(bufb.data(), buf_size);
        if (fa.gcount() != fb.gcount()) return false;
        if (std::memcmp(bufa.data(), bufb.data(), fa.gcount()) != 0) return false;
    }
    return true;
}

bool Pipeline::dedup_stage()
{
    std::cout << "=== Deduplication stage ===" << std::endl;

    // Group files by size
    std::map<uintmax_t, std::vector<std::string>> by_size;
    size_t total_files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(raw_dir_)) {
        if (entry.is_regular_file()) {
            auto size = entry.file_size();
            by_size[size].push_back(entry.path().string());
            total_files++;
        }
    }

    std::cout << "  Scanned " << total_files << " files in " << raw_dir_ << std::endl;

    size_t candidates = 0;
    for (const auto& [sz, paths] : by_size) {
        if (paths.size() > 1) candidates += paths.size();
    }
    std::cout << "  " << candidates << " files in same-size groups" << std::endl;

    size_t saved_bytes = 0;
    size_t deduped_files = 0;

    for (auto& [sz, paths] : by_size) {
        if (paths.size() <= 1) continue;
        std::sort(paths.begin(), paths.end());

        // Quick fingerprint: first 4096 bytes
        std::map<std::string, std::vector<std::string>> by_fp;
        std::vector<char> buf(4096);
        for (const auto& p : paths) {
            std::ifstream f(p, std::ios::binary);
            if (!f) continue;
            f.read(buf.data(), 4096);
            by_fp[std::string(buf.data(), f.gcount())].push_back(p);
        }

        // Full comparison within each fingerprint group
        for (auto& [fp, group] : by_fp) {
            if (group.size() <= 1) continue;

            for (size_t i = 1; i < group.size(); i++) {
                if (fs::equivalent(group[0], group[i])) continue;

                if (files_match(group[0], group[i])) {
                    // Replace duplicate with hardlink
                    std::string tmp = group[i] + ".dedup_tmp";
                    fs::remove(tmp);
                    fs::create_hard_link(group[0], tmp);
                    fs::rename(tmp, group[i]);
                    saved_bytes += sz;
                    deduped_files++;
                }
            }
        }
    }

    std::cout << "  Deduplicated " << deduped_files << " files, saved "
              << (saved_bytes / (1024.0 * 1024.0)) << " MB" << std::endl;

    return true;
}

PackageManifest Pipeline::scan_packages() const
{
    PackageScanner scanner(config_.game_path);
    return scanner.scan();
}

bool Pipeline::extract_stage()
{
    auto manifest = scan_packages();

    ExportConfig exp_config;
    exp_config.game_path = config_.game_path;
    exp_config.output_dir = raw_dir_;

    ExportDriver driver(exp_config);

    for (const auto& pkg : manifest.packages) {
        std::cout << "Extracting package: " << pkg.name << std::endl;
        driver.export_package(pkg.path);
    }

    for (const auto& map : manifest.maps) {
        std::cout << "Extracting map: " << map.map_name << std::endl;
        driver.export_package(map.path);
    }

    return true;
}

bool Pipeline::convert_stage()
{
    MaterialResolver resolver(raw_dir_);
    resolver.scan_materials();
    resolver.scan_textures();
    resolver.resolve_material_chain();
    resolver.write_manifest(gltf_dir_ + "/manifests/materials.json");

    GltfWriter writer(gltf_dir_, resolver);

    int mesh_count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(raw_dir_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".pskx") {
            std::string rel = fs::relative(entry.path(), raw_dir_).string();
            std::string mesh_name = rel.substr(0, rel.size() - 5); // strip ".pskx"
            writer.write_static_mesh(entry.path().string(), mesh_name);
            if (++mesh_count % 50 == 0)
                std::cout << "  Converted " << mesh_count << " meshes..." << std::endl;
        }
    }
    std::cout << "  Converted " << mesh_count << " static meshes total." << std::endl;

    fs::path maps_dir = fs::path(raw_dir_) / "maps";
    if (fs::exists(maps_dir)) {
        MapAssembler assembler(raw_dir_, gltf_dir_);
        for (const auto& entry : fs::directory_iterator(maps_dir)) {
            if (entry.is_directory()) {
                std::string map_name = entry.path().filename().string();
                fs::path scene_json = entry.path() / (map_name + ".scene.json");
                if (fs::exists(scene_json)) {
                    assembler.assemble_map(scene_json.string(), map_name);
                }
            }
        }
    }

    return true;
}

bool Pipeline::skeletal_stage()
{
    BlenderBridge blender(config_.blender_path);
    return blender.convert_skeletal_meshes(raw_dir_, gltf_dir_ + "/skeletal-meshes");
}

bool Pipeline::extract_actors_stage()
{
    std::cout << "=== Actor extraction stage (UELib) ===" << std::endl;

    fs::path actors_dir = gltf_dir_ + "/actors";
    fs::create_directories(actors_dir);

    auto manifest = scan_packages();

    // Build argument list: "maps" subcommand, then alternating map path + output path
    std::vector<std::string> map_args;
    for (const auto& map : manifest.maps) {
        fs::path out_json = actors_dir / (map.map_name + ".actors.json");
        map_args.push_back(map.path);
        map_args.push_back(out_json.string());
    }

    if (map_args.empty()) {
        std::cout << "  No maps found." << std::endl;
        return false;
    }

    // Path to the .NET decompiler tool
    fs::path slicer_dir = fs::current_path() / "src" / "importer" / "slicer";
    std::string cmd = "dotnet run --project " + slicer_dir.string() + "/Decompiler.csproj -- maps";
    for (const auto& a : map_args)
        cmd += " \"" + a + "\"";

    std::cout << "  Running .NET MapExtractor for " << (map_args.size()/2) << " maps..." << std::endl;
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "  MapExtractor failed with code " << ret << std::endl;
        return false;
    }

    // Count how many output files were produced
    int count = 0;
    for (const auto& entry : fs::directory_iterator(actors_dir)) {
        if (entry.path().extension() == ".json") count++;
    }
    std::cout << "  Extracted " << count << " map actor sets." << std::endl;
    return count > 0;
}

}
