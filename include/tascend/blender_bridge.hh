#pragma once

#include <string>

namespace tascend {

class BlenderBridge {
public:
    BlenderBridge(const std::string& blender_path);

    bool convert_skeletal_meshes(const std::string& raw_dir,
                                 const std::string& output_dir) const;

private:
    std::string blender_path_;
    std::string convert_script_path_;
    std::string match_script_path_;
};

}
