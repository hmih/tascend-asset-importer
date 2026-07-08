#include "tascend/export_driver.hh"

#include "Core.h"
#include "UnCore.h"
#include "UnObject.h"
#include "UnrealPackage/UnPackage.h"
#include "UmodelTool/UmodelApp.h"
#include "UmodelTool/UmodelCommands.h"
#include "UmodelTool/UmodelSettings.h"
#include "Exporters/Exporters.h"

#include <iostream>
#include <filesystem>

namespace tascend {

namespace fs = std::filesystem;

ExportDriver::ExportDriver(const ExportConfig& config)
    : config_(config)
{
    init_umodel();
}

ExportDriver::~ExportDriver()
{
}

void ExportDriver::init_umodel()
{
    if (initialized_) return;

    appSetRootDirectory(config_.game_path.c_str());

    GSettings.Export.ExportPath = config_.output_dir.c_str();
    GSettings.Export.TextureFormat = config_.export_png ? ETextureExportFormat::png : ETextureExportFormat::tga;
    GSettings.Export.ExportDdsTexture = config_.export_dds;
    GSettings.Export.ExportMeshLods = false;
    GSettings.Export.SaveUncooked = false;
    GSettings.Export.SaveGroups = config_.use_groups;
    GSettings.Export.DontOverwriteFiles = config_.dont_overwrite;
    GSettings.Export.Apply();

    GExportLods = false;
    GUncook = false;

    GSettings.Startup.UseSound = true;

    GSettings.Export.SkeletalMeshFormat = EExportMeshFormat::psk;
    GSettings.Export.StaticMeshFormat = EExportMeshFormat::psk;

    initialized_ = true;
}

void ExportDriver::export_package(const std::string& package_path)
{
    if (!initialized_) {
        init_umodel();
    }

    UnPackage* pkg = UnPackage::LoadPackage(package_path.c_str());
    if (!pkg) {
        std::cerr << "Failed to load package: " << package_path << std::endl;
        return;
    }

    TArray<UnPackage*> packages;
    packages.Add(pkg);

    ExportPackages(packages);
}

void ExportDriver::export_all_packages(const std::string& game_path)
{
    appSetRootDirectory(game_path.c_str());

    TArray<UnPackage*> packages;

    TStaticArray<const CGameFileInfo*, 256> files;
    appFindGameFiles("*", files);

    for (int i = 0; i < files.Num(); i++) {
        UnPackage* pkg = UnPackage::LoadPackage(files[i]);
        if (pkg) {
            packages.Add(pkg);
        }
    }

    if (packages.Num() == 0) {
        std::cerr << "No packages found in: " << game_path << std::endl;
        return;
    }

    ExportPackages(packages);
}

void ExportDriver::export_all_maps(const std::string& maps_dir)
{
    if (!fs::exists(maps_dir)) return;

    for (const auto& entry : fs::recursive_directory_iterator(maps_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".fmap") {
            std::cout << "Exporting map: " << entry.path().filename() << std::endl;
            export_package(entry.path().string());
        }
    }
}

}
