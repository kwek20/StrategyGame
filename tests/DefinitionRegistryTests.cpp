#include "gameplay/DefinitionRegistry.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void writeFixture(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream stream(path, std::ios::trunc);
    stream << contents;
}

bool rejects(const std::filesystem::path& units,
             const std::filesystem::path& recipes,
             const std::filesystem::path& countries,
             const std::string& expected) {
    try {
        const strategy::DefinitionRegistry registry{units,
                                                    "assets/gameplay/buildings.json",
                                                    "assets/gameplay/resource_nodes.json",
                                                    "assets/gameplay/resources.json",
                                                    "assets/gameplay/weapons.json",
                                                    "assets/gameplay/power.json",
                                                    recipes,
                                                    countries,
                                                    "assets/gameplay/specializations.json"};
        (void)registry;
    } catch (const std::runtime_error& error) {
        return std::string{error.what()}.find(expected) != std::string::npos;
    }
    return false;
}

} // namespace

int main() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "strategy_definition_validation";
    std::filesystem::create_directories(directory);
    const auto invalidUnits = directory / "invalid-units.json";
    const auto duplicateRecipes = directory / "duplicate-recipes.json";
    const auto invalidCountries = directory / "invalid-countries.json";

    writeFixture(invalidUnits,
                 R"({"version":1,"units":{"broken":{"collisionRadius":1,"components":["warp-drive"],"tags":[],"stats":{}}}})");
    writeFixture(duplicateRecipes,
                 R"({"version":1,"recipes":[{"id":"one","producer":"town_center","product":{"unit":"worker"},"cost":{},"durationTicks":30},{"id":"two","producer":"town_center","product":{"unit":"worker"},"cost":{},"durationTicks":60}]})");
    writeFixture(invalidCountries,
                 R"({"version":1,"countries":[{"id":"invalid","nameKey":"country.spain","specialization":"missing","modifiers":[]}]})");

    bool valid = true;
    valid = valid && rejects(invalidUnits,
                             "assets/gameplay/recipes.json",
                             "assets/gameplay/countries.json",
                             "broken' has unknown component 'warp-drive");
    valid = valid && rejects("assets/gameplay/units.json",
                             duplicateRecipes,
                             "assets/gameplay/countries.json",
                             "Duplicate producer/product recipe");
    valid = valid && rejects("assets/gameplay/units.json",
                             "assets/gameplay/recipes.json",
                             invalidCountries,
                             "references unknown specialization 'missing'");

    std::filesystem::remove_all(directory);
    if (!valid) {
        std::cerr << "Definition registry validation failed\n";
        return 1;
    }
    std::cout << "Definition registry validation passed\n";
    return 0;
}
