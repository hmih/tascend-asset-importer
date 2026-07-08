#include "tascend/package_scanner.hh"

#include <filesystem>
#include <algorithm>

namespace tascend {

namespace fs = std::filesystem;

PackageScanner::PackageScanner(const std::string& game_path)
    : game_path_(game_path)
{
}

PackageManifest PackageScanner::scan() const
{
    PackageManifest manifest;
    manifest.game_path = game_path_;
    manifest.tfc_dir = game_path_;

    scan_directory(game_path_, ".u", manifest.packages);
    scan_directory(game_path_, ".upk", manifest.packages);

    fs::path maps_root = fs::path(game_path_) / "Maps";
    if (fs::exists(maps_root)) {
        scan_map_directories(manifest.maps);
    }

    return manifest;
}

void PackageScanner::scan_directory(const std::string& dir, const std::string& ext,
                                     std::vector<PackageInfo>& out, bool is_map) const
{
    if (!fs::exists(dir)) return;

    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ext) continue;

        PackageInfo info;
        info.path = entry.path().string();
        info.name = entry.path().stem().string();
        info.type = ext.substr(1);
        info.map_name = is_map ? info.name : "";

        out.push_back(info);
    }
}

void PackageScanner::scan_map_directories(std::vector<PackageInfo>& out) const
{
    fs::path maps_root = fs::path(game_path_) / "Maps";

    for (const auto& entry : fs::directory_iterator(maps_root)) {
        if (!entry.is_directory()) continue;

        std::string dir_name = entry.path().filename().string();
        std::string map_name = dir_name;

        scan_directory(entry.path().string(), ".fmap", out, true);

        for (auto& m : out) {
            if (m.path.find(dir_name) != std::string::npos && m.map_name.empty()) {
                m.map_name = map_name;
            }
        }
    }

    fs::path root_maps = maps_root;
    for (const auto& entry : fs::directory_iterator(root_maps)) {
        if (entry.is_regular_file() && entry.path().extension() == ".fmap") {
            PackageInfo info;
            info.path = entry.path().string();
            info.name = entry.path().stem().string();
            info.type = "fmap";
            info.map_name = info.name;
            out.push_back(info);
        }
    }
}

}
