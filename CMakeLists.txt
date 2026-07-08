cmake_minimum_required(VERSION 3.28)
project(tascend_importer C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#------------------------------------------------
# UModel as a dependency (builds umodel_lib static library)
#------------------------------------------------
set(UMODEL_DIR "${CMAKE_SOURCE_DIR}/vendor/UEViewer")
add_subdirectory(${UMODEL_DIR} ${CMAKE_BINARY_DIR}/umodel-build)

#------------------------------------------------
# tascend_importer executable
#------------------------------------------------
set(IMPORTER_SOURCES
    src/main.cpp
    src/pipeline.cpp
    src/package_scanner.cpp
    src/export_driver.cpp
    src/material_resolver.cpp
    src/psk_reader.cpp
    src/terrain_reader.cpp
    src/gltf_writer.cpp
    src/map_assembler.cpp
    src/blender_bridge.cpp
    src/actor_extractor.cpp
)

add_executable(tascend_importer ${IMPORTER_SOURCES})

target_include_directories(tascend_importer PRIVATE include)

# Link against UModel's static library (exports all headers + compile defs)
target_link_libraries(tascend_importer PRIVATE umodel_lib)

#------------------------------------------------
# macOS hardening: UBSan + fortification in RelWithDebInfo
# ASan hangs due to heavy UModel static init; use Guard Malloc instead:
#   DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib
#------------------------------------------------
if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(HARDEN_FLAGS
        -fsanitize=undefined
        -fno-sanitize-recover=all
        -fno-omit-frame-pointer
        -fstack-protector-strong
        -D_FORTIFY_SOURCE=2
    )
    target_compile_options(tascend_importer PRIVATE ${HARDEN_FLAGS})
    target_link_options(tascend_importer PRIVATE ${HARDEN_FLAGS})
endif()
