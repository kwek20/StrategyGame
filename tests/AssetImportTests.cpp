#include "assets/ModelAsset.hpp"

#include <assimp/Importer.hpp>
#include <assimp/importerdesc.h>
#include <filesystem>
#include <iostream>
#include <string_view>

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

bool usesOnlyModernGltfImporter() {
    Assimp::Importer importer;
    bool hasGltf2 = false;
    bool hasLegacyGltf = false;
    for (std::size_t index = 0; index < importer.GetImporterCount(); ++index) {
        const aiImporterDesc* description = importer.GetImporterInfo(index);
        if (description == nullptr || description->mName == nullptr)
            continue;
        const std::string_view name{description->mName};
        hasGltf2 = hasGltf2 || name == "glTF2 Importer";
        hasLegacyGltf = hasLegacyGltf || name == "glTF Importer";
    }
    if (!hasGltf2 || hasLegacyGltf) {
        std::cerr << "Expected glTF2 importer without obsolete glTF1 importer\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    const bool importerConfiguration = usesOnlyModernGltfImporter();
    const bool townCenter =
        imports("assets/models/buildings/TownCenter_FirstAge_Level1.gltf", false);
    const bool electricityPole = imports("assets/models/gen/electricity_pole.glb", false);
    const bool worker = imports("assets/models/units/Worker_Male.gltf", true);
    const bool resourceVariants =
        imports("assets/models/gen/resources/scrap_pile_small.glb", false) &&
        imports("assets/models/gen/resources/scrap_pile_medium.glb", false) &&
        imports("assets/models/gen/resources/oil_seep_small.glb", false) &&
        imports("assets/models/gen/resources/oil_barrel_medium.glb", false) &&
        imports("assets/models/gen/resources/uranium_crystal_small.glb", false) &&
        imports("assets/models/gen/resources/uranium_crystal_large.glb", false);
    return importerConfiguration && townCenter && electricityPole && worker && resourceVariants
               ? 0
               : 1;
}
