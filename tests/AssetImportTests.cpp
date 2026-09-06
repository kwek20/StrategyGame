#include "assets/ModelAsset.hpp"

#include <filesystem>
#include <iostream>

namespace {

bool imports(const std::filesystem::path& path, bool expectAnimations) {
    const auto asset = strategy::importModelAsset(path);
    if (!asset || asset->meshes.empty()) {
        std::cerr << "Failed to import " << path << '\n';
        return false;
    }
    if (expectAnimations && asset->animations.empty()) {
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
