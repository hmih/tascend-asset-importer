#pragma once

#include <string>
#include <vector>

namespace tascend {

struct PackageInfo {
    std::string path;       // full path to .u/.upk/.fmap file
    std::string name;       // package name without extension
    std::string type;       // "u", "upk", "fmap"
    std::string map_name;   // for .fmap: the map name (e.g. "Perdition")
};

struct PackageManifest {
    std::vector<PackageInfo> packages;
    std::vector<PackageInfo> maps;
    std::string game_path;  // path to CookedPC/
    std::string tfc_dir;    // directory containing .tfc files

    int total_count() const { return packages.size() + maps.size(); }
};

class PackageScanner {
public:
    PackageScanner(const std::string& game_path);

    PackageManifest scan() const;

private:
    std::string game_path_;

    void scan_directory(const std::string& dir, const std::string& ext,
                        std::vector<PackageInfo>& out, bool is_map = false) const;
    void scan_map_directories(std::vector<PackageInfo>& out) const;
};

}
