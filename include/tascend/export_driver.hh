#pragma once

#include <string>

namespace tascend {

struct ExportConfig {
    std::string game_path;      // path to CookedPC/
    std::string output_dir;     // raw output root
    bool export_png = true;     // textures as PNG
    bool export_dds = false;    // textures as DDS
    bool export_sounds = true;  // export OGG/WAV
    bool export_3rdparty = true;// allow 3rd-party formats
    bool export_materials = true;
    bool use_groups = true;     // preserve UE3 group structure in output
    bool dont_overwrite = true; // idempotent re-runs
};

class ExportDriver {
public:
    ExportDriver(const ExportConfig& config);
    ~ExportDriver();

    void export_package(const std::string& package_path);
    void export_all_packages(const std::string& game_path);
    void export_all_maps(const std::string& maps_dir);

private:
    ExportConfig config_;
    bool initialized_ = false;

    void init_umodel();
    void set_export_globals();
};

}
