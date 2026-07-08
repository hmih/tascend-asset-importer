#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace tascend {

struct PskVertex {
    float pos[3];
    float normal[3];
    float uv[2];
};

struct PskMaterial {
    std::string name;
    int face_count = 0;
};

struct PskMesh {
    std::vector<PskVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<PskMaterial> materials;
    std::string mesh_name;
};

class PskReader {
public:
    bool read(const std::string& path, PskMesh& out) const;

private:
    struct ChunkHeader {
        char id[20];
        uint32_t flags;
        uint32_t data_size;
        uint32_t data_count;
    };

    bool read_chunk_header(const uint8_t*& p, const uint8_t* end, ChunkHeader& out) const;
};

}
