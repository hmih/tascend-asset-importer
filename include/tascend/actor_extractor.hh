#pragma once

#include <string>
#include <filesystem>

namespace tascend {

struct ActorTransform {
    float location[3];
    float rotation[3];   // pitch, yaw, roll
    float scale[3];
};

bool extract_actors_json(const std::string& fmap_path,
                         const std::string& output_json_path);

} // namespace tascend
