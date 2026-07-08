#pragma once

#include <string>
#include "tascend/package_scanner.hh"
#include "tascend/export_driver.hh"
#include "tascend/material_resolver.hh"
#include "tascend/gltf_writer.hh"
#include "tascend/map_assembler.hh"
#include "tascend/blender_bridge.hh"

namespace tascend {

struct PipelineConfig {
    std::string game_path;      // e.g. original/TribesGame/CookedPC
    std::string output_root;    // e.g. src/importer/output
    std::string blender_path;   // e.g. /Applications/Blender.app/Contents/MacOS/Blender
    bool skip_extract = false;  // skip raw extraction stage
    bool skip_convert = false;  // skip glTF conversion stage
    bool skip_skeletal = false; // skip skeletal mesh (Blender) stage
    bool verbose = false;
};

class Pipeline {
public:
    Pipeline(const PipelineConfig& config);

    int run_all();
    int run_scan();
    int run_extract();
    int run_convert();
    int run_dedup();
    int run_extract_actors();
    int run_map(const std::string& map_name);

private:
    PipelineConfig config_;
    std::string raw_dir_;
    std::string gltf_dir_;

    PackageManifest scan_packages() const;
    bool extract_stage();
    bool convert_stage();
    bool dedup_stage();
    bool extract_actors_stage();
    bool skeletal_stage();
};

}
