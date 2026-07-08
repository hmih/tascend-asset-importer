#include "tascend/blender_bridge.hh"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <thread>
#include <vector>
#include <algorithm>

namespace tascend {

namespace fs = std::filesystem;

BlenderBridge::BlenderBridge(const std::string& blender_path)
    : blender_path_(blender_path)
    , convert_script_path_(fs::current_path() / "src" / "importer" / "scripts" / "convert_skeletal.py")
    , match_script_path_(fs::current_path() / "src" / "importer" / "scripts" / "match_psa.py")
{
}

static int run_cmd(const std::string& cmd)
{
    return std::system(cmd.c_str());
}

bool BlenderBridge::convert_skeletal_meshes(const std::string& raw_dir,
                                             const std::string& output_dir) const
{
    std::cout << "=== Skeletal mesh stage ===" << std::endl;

    fs::create_directories(output_dir);

    // Collect all PSK files
    std::vector<std::string> psk_files;
    for (const auto& entry : fs::recursive_directory_iterator(raw_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".psk")
            psk_files.push_back(entry.path().string());
    }
    std::sort(psk_files.begin(), psk_files.end());

    if (psk_files.empty()) {
        std::cout << "  No PSK files found." << std::endl;
        return true;
    }

    std::cout << "  Found " << psk_files.size() << " PSK files" << std::endl;

    // Step 1: Run match_psa.py to find matching animations
    std::string matching_json = (fs::temp_directory_path() / "psa_matching.json").string();
    std::cout << "  Matching PSA animations..." << std::endl;
    {
        std::string cmd = "python3 " + match_script_path_ + " " + raw_dir + " " + matching_json;
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "  match_psa.py failed" << std::endl;
            return false;
        }
    }

    // Step 2: Split PSK files into batches and run Blender instances in parallel
    unsigned int num_workers = std::thread::hardware_concurrency();
    if (num_workers > 12) num_workers = 12;  // cap for memory
    if (num_workers < 2) num_workers = 2;
    size_t batch_size = (psk_files.size() + num_workers - 1) / num_workers;

    std::cout << "  Converting with " << num_workers << " parallel Blender workers"
              << " (" << batch_size << " files each)..." << std::endl;

    std::vector<std::string> batch_files;
    std::vector<std::thread> workers;

    for (unsigned int w = 0; w < num_workers; w++) {
        size_t start = w * batch_size;
        size_t end = std::min(start + batch_size, psk_files.size());
        if (start >= end) break;

        std::string batch_path = (fs::temp_directory_path() / ("psk_batch_" + std::to_string(w) + ".txt")).string();
        batch_files.push_back(batch_path);

        std::ofstream bf(batch_path);
        for (size_t i = start; i < end; i++)
            bf << psk_files[i] << "\n";
        bf.close();

        std::string cmd = blender_path_
            + " --background"
            + " --python " + convert_script_path_
            + " -- " + raw_dir + " " + output_dir
            + " --psa " + matching_json
            + " --list " + batch_path;

        workers.emplace_back([cmd, w]() {
            std::cout << "  [worker " << w << "] starting Blender..." << std::endl;
            int ret = run_cmd(cmd);
            if (ret != 0)
                std::cerr << "  [worker " << w << "] Blender failed with code " << ret << std::endl;
            else
                std::cout << "  [worker " << w << "] done." << std::endl;
        });
    }

    for (auto& t : workers)
        t.join();

    // Cleanup temp files
    for (const auto& bf : batch_files)
        fs::remove(bf);
    fs::remove(matching_json);

    return true;
}

}