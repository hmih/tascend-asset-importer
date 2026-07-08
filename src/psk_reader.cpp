#include "tascend/psk_reader.hh"

#include <fstream>
#include <cstring>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <algorithm>

namespace tascend {

namespace fs = std::filesystem;

namespace {

struct PskPoint { float x, y, z; };
struct PskWedge { int32_t point_idx; float u, v; uint8_t mat_idx; uint8_t reserved; int16_t pad; };
struct PskFace { uint16_t wedge[3]; uint8_t mat_idx; uint8_t aux_mat; uint32_t smoothing; };
struct PskMatRaw { char name[64]; int32_t texture_idx; uint32_t poly_flags; int32_t aux_material; uint32_t aux_flags; int32_t lod_bias; int32_t lod_style; };

}

bool PskReader::read(const std::string& path, PskMesh& out) const
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    std::vector<PskPoint> points;
    std::vector<PskWedge> wedges;
    std::vector<PskFace> faces;
    std::vector<PskMatRaw> materials;

    while (f.good()) {
        ChunkHeader hdr;
        f.read(reinterpret_cast<char*>(&hdr), sizeof(ChunkHeader));
        if (!f.good() || f.gcount() < sizeof(ChunkHeader)) break;

        std::string id(hdr.id, 20);
        id = id.substr(0, id.find('\0'));

        size_t chunk_bytes = static_cast<size_t>(hdr.data_size) * hdr.data_count;
        std::vector<uint8_t> data(chunk_bytes);
        f.read(reinterpret_cast<char*>(data.data()), chunk_bytes);
        if (!f.good()) break;

        const uint8_t* p = data.data();

        if (id == "PNTS0000" && hdr.data_size == 12) {
            points.resize(hdr.data_count);
            std::memcpy(points.data(), p, chunk_bytes);
        } else if (id == "VTXW0000" && hdr.data_size == 16) {
            wedges.resize(hdr.data_count);
            std::memcpy(wedges.data(), p, chunk_bytes);
        } else if (id == "FACE0000" && hdr.data_size == 12) {
            faces.resize(hdr.data_count);
            std::memcpy(faces.data(), p, chunk_bytes);
        } else if (id == "MATT0000") {
            materials.resize(hdr.data_count);
            std::memcpy(materials.data(), p, std::min(chunk_bytes, sizeof(PskMatRaw) * hdr.data_count));
        }
    }

    if (points.empty() || faces.empty()) return false;

    out.materials.clear();
    for (const auto& m : materials) {
        PskMaterial pm;
        std::string name(m.name, 64);
        name = name.substr(0, name.find('\0'));
        pm.name = name;
        out.materials.push_back(pm);
    }
    if (out.materials.empty()) {
        out.materials.push_back({"default"});
    }

    out.vertices.clear();
    out.indices.clear();

    std::vector<uint32_t> wedge_to_vertex(wedges.size(), 0xFFFFFFFF);

    for (const auto& face : faces) {
        for (int i = 0; i < 3; i++) {
            uint16_t wi = face.wedge[i];
            if (wi >= wedges.size()) continue;

            if (wedge_to_vertex[wi] == 0xFFFFFFFF) {
                uint32_t pi = wedges[wi].point_idx;
                PskVertex v;
                if (pi < points.size()) {
                    v.pos[0] = points[pi].x;
                    v.pos[1] = points[pi].y;
                    v.pos[2] = points[pi].z;
                }
                v.normal[0] = 0; v.normal[1] = 0; v.normal[2] = 1;
                v.uv[0] = wedges[wi].u;
                v.uv[1] = wedges[wi].v;
                wedge_to_vertex[wi] = static_cast<uint32_t>(out.vertices.size());
                out.vertices.push_back(v);
            }
            out.indices.push_back(wedge_to_vertex[wi]);
        }
    }

    for (size_t i = 0; i < faces.size(); i++) {
        uint16_t w0 = faces[i].wedge[0];
        uint16_t w1 = faces[i].wedge[1];
        uint16_t w2 = faces[i].wedge[2];

        if (w0 >= wedges.size() || w1 >= wedges.size() || w2 >= wedges.size()) continue;

        uint32_t i0 = wedge_to_vertex[w0];
        uint32_t i1 = wedge_to_vertex[w1];
        uint32_t i2 = wedge_to_vertex[w2];
        if (i0 == 0xFFFFFFFF || i1 == 0xFFFFFFFF || i2 == 0xFFFFFFFF) continue;

        PskVertex& v0 = out.vertices[i0];
        PskVertex& v1 = out.vertices[i1];
        PskVertex& v2 = out.vertices[i2];

        float ux = v1.pos[0] - v0.pos[0];
        float uy = v1.pos[1] - v0.pos[1];
        float uz = v1.pos[2] - v0.pos[2];
        float vx = v2.pos[0] - v0.pos[0];
        float vy = v2.pos[1] - v0.pos[1];
        float vz = v2.pos[2] - v0.pos[2];

        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;

        v0.normal[0] += nx; v0.normal[1] += ny; v0.normal[2] += nz;
        v1.normal[0] += nx; v1.normal[1] += ny; v1.normal[2] += nz;
        v2.normal[0] += nx; v2.normal[1] += ny; v2.normal[2] += nz;
    }

    for (auto& v : out.vertices) {
        float len = std::sqrt(v.normal[0]*v.normal[0] + v.normal[1]*v.normal[1] + v.normal[2]*v.normal[2]);
        if (len > 0) {
            v.normal[0] /= len;
            v.normal[1] /= len;
            v.normal[2] /= len;
        } else {
            v.normal[0] = 0; v.normal[1] = 0; v.normal[2] = 1;
        }
    }

    fs::path p(path);
    out.mesh_name = p.stem().string();

    return !out.vertices.empty();
}

}
