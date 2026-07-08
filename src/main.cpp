#include "tascend/pipeline.hh"

#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>

static void print_usage()
{
    std::cout <<
        "tascend_importer — Tribes: Ascend asset import pipeline\n"
        "\n"
        "Usage: tascend_importer [options] <command>\n"
        "\n"
        "Commands:\n"
        "  scan        List all packages found in the game directory\n"
        "  extract     Run UModel raw extraction stage\n"
        "  convert     Run glTF conversion stage\n"
        "  dedup       Deduplicate identical raw files via hardlinks\n"
        "  extract-actors  Extract actor placements from .fmap files\n"
        "  all         Full pipeline: extract + convert + skeletal\n"
        "  map <name>  Process a single map end-to-end\n"
        "\n"
        "Options:\n"
        "  --game-path=<dir>     Path to CookedPC/ (default: original/TribesGame/CookedPC)\n"
        "  --output=<dir>        Output root (default: src/importer/output)\n"
        "  --blender=<path>      Blender binary (default: /Applications/Blender.app/Contents/MacOS/Blender)\n"
        "  --skip-extract        Skip raw extraction stage\n"
        "  --skip-convert        Skip glTF conversion stage\n"
        "  --skip-skeletal       Skip skeletal mesh (Blender) stage\n"
        "  --verbose             Verbose output\n"
        "  --help                Show this help\n";
}

int main(int argc, char** argv)
{
    tascend::PipelineConfig config;
    config.game_path = "original/TribesGame/CookedPC";
    config.output_root = "src/importer/output";
    config.blender_path = "/Applications/Blender.app/Contents/MacOS/Blender";

    std::string command;
    std::string map_name;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg.rfind("--game-path=", 0) == 0) {
            config.game_path = arg.substr(12);
        } else if (arg.rfind("--output=", 0) == 0) {
            config.output_root = arg.substr(9);
        } else if (arg.rfind("--blender=", 0) == 0) {
            config.blender_path = arg.substr(10);
        } else if (arg == "--skip-extract") {
            config.skip_extract = true;
        } else if (arg == "--skip-convert") {
            config.skip_convert = true;
        } else if (arg == "--skip-skeletal") {
            config.skip_skeletal = true;
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg[0] != '-' && command.empty()) {
            command = arg;
        } else if (arg[0] != '-' && command == "map" && map_name.empty()) {
            map_name = arg;
        }
    }

    if (command.empty()) {
        print_usage();
        return 1;
    }

    // Resolve relative paths to absolute for UModel compatibility
    config.game_path = std::filesystem::absolute(config.game_path).string();
    config.output_root = std::filesystem::absolute(config.output_root).string();

    tascend::Pipeline pipeline(config);

    if (command == "scan") {
        return pipeline.run_scan();
    } else if (command == "extract") {
        return pipeline.run_extract();
    } else if (command == "convert") {
        return pipeline.run_convert();
    } else if (command == "dedup") {
        return pipeline.run_dedup();
    } else if (command == "extract-actors") {
        return pipeline.run_extract_actors();
    } else if (command == "all") {
        return pipeline.run_all();
    } else if (command == "map") {
        if (map_name.empty()) {
            std::cerr << "Error: map command requires a map name" << std::endl;
            return 1;
        }
        return pipeline.run_map(map_name);
    } else {
        std::cerr << "Unknown command: " << command << std::endl;
        print_usage();
        return 1;
    }
}
