#include "tascend/actor_extractor.hh"

#include "Core.h"
#include "UnCore.h"
#include "UnObject.h"
#include "UnrealPackage/UnPackage.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdio>

namespace tascend {

struct ActorData {
    std::string objName, className;
    float loc[3]; int rot[3]; float scl[3];
    bool hasLoc, hasRot;
};

static bool is_actor(const char* n) {
    if (!n) return false;
    return strstr(n, "Actor") || strstr(n, "Pawn");
}

static bool extract_actors_from_package(UnPackage* pkg, std::vector<ActorData>& out) {
    int actorCount = 0;

    // Debug: print name table entries for key indices
    printf("  Names[160..169]:");
    for (int i = 160; i < 170 && i < pkg->Summary.NameCount; i++)
        printf(" %d=%s", i, pkg->GetName(i));
    printf("\n  Names[220..229]:");
    for (int i = 220; i < 230 && i < pkg->Summary.NameCount; i++)
        printf(" %d=%s", i, pkg->GetName(i));
    printf("\n");

    for (int idx = 0; idx < pkg->Summary.ExportCount; idx++) {
        const FObjectExport& Exp = pkg->GetExport(idx);
        const char* cn = pkg->GetClassNameFor(Exp);
        if (!is_actor(cn)) continue;

        actorCount++;
        pkg->SetupReader(idx);
        int dataEnd = pkg->GetStopper();
        int dataStart = pkg->Tell();

        // Only debug first StaticMeshActor
        if (!strstr(cn, "StaticMeshActor") || actorCount > 1) continue;

        printf("  [%s:%s] offset=%d size=%d\n", cn, *Exp.ObjectName, dataStart, dataEnd - dataStart);
        printf("  raw ints: ");
        int n = (dataEnd - dataStart) / 4;
        if (n > 48) n = 48;
        for (int i = 0; i < n; i++) { int v; *pkg << v; printf("%d ", v); }
        printf("\n");
        pkg->Seek(dataStart);
        break;
    }

    printf("  %d actors total\n", actorCount);
    return true;
}

bool extract_actors_json(const std::string& fmap_path,
                         const std::string& out_path) {
    UnPackage* pkg = UnPackage::LoadPackage(fmap_path.c_str());
    if (!pkg || !pkg->IsValid()) {
        std::cerr << "  Failed: " << fmap_path << std::endl;
        if (pkg) UnPackage::UnloadPackage(pkg);
        return false;
    }

    std::vector<ActorData> actors;
    extract_actors_from_package(pkg, actors);

    std::ostringstream js;
    js << "{\"map\":\"" << pkg->Name << "\",\"actors\":[";
    for (size_t i = 0; i < actors.size(); i++) {
        auto& a = actors[i];
        if (i) js << ",";
        js << "{\"name\":\"" << a.objName
           << "\",\"class\":\"" << a.className
           << "\",\"loc\":[" << a.loc[0] << "," << a.loc[1] << "," << a.loc[2]
           << "],\"rot\":[" << (a.rot[0]*360.0f/65536.0f) << "," << (a.rot[1]*360.0f/65536.0f) << "," << (a.rot[2]*360.0f/65536.0f) << ",0]"
           << "],\"scale\":[" << a.scl[0] << "," << a.scl[1] << "," << a.scl[2]
           << "]}";
    }
    js << "],\"count\":" << actors.size() << "}\n";

    UnPackage::UnloadPackage(pkg);

    std::ofstream f(out_path);
    if (!f) return false;
    f << js.str();
    return true;
}

} // namespace tascend
