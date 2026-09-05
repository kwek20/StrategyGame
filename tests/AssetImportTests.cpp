#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <filesystem>
#include <iostream>

namespace {

bool imports(const std::filesystem::path& path, bool expectAnimations) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path.string(),
                                             aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                                 aiProcess_JoinIdenticalVertices);
    if (scene == nullptr || scene->mRootNode == nullptr || scene->mNumMeshes == 0) {
        std::cerr << "Failed to import " << path << ": " << importer.GetErrorString() << '\n';
        return false;
    }
    if (expectAnimations && scene->mNumAnimations == 0) {
        std::cerr << path << " imported without its expected animations\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    const bool townCenter =
        imports("assets/models/buildings/TownCenter_FirstAge_Level1.gltf", false);
    const bool worker = imports("assets/models/units/Worker_Male.gltf", true);
    return townCenter && worker ? 0 : 1;
}
