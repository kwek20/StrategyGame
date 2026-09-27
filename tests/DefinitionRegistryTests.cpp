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
             const std::string& expected,
             const std::filesystem::path& power = "assets/gameplay/power.json") {
    try {
        const strategy::DefinitionRegistry registry{units,
                                                    "assets/gameplay/buildings.json",
                                                    "assets/gameplay/resource_nodes.json",
                                                    "assets/gameplay/resources.json",
                                                    "assets/gameplay/weapons.json",
                                                    power,
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

int strategyTestMain() {
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

    const auto infinitePower = directory / "infinite-power.json";
    writeFixture(infinitePower, R"({"version":1,"devices":{"invalid":{"production":1e100}}})");
    valid = valid && rejects("assets/gameplay/units.json", "assets/gameplay/recipes.json",
                             "assets/gameplay/countries.json", "requires finite values", infinitePower);
    std::ifstream unitsInput("assets/gameplay/units.json");
    const std::string unitsText((std::istreambuf_iterator<char>(unitsInput)), {});
    const auto repairBegin = unitsText.find("\"repair\": {");
    const auto repairEnd = unitsText.find('}', repairBegin);
    for (const std::string repair : {
             R"("repair":{"healthPerTick":0,"batteryPerTick":1})",
             R"("repair":{"healthPerTick":2,"batteryPerTick":-1})",
             R"("repair":{"healthPerTick":2,"batteryPerTick":101})",
             R"("repair":{"healthPerTick":1e100,"batteryPerTick":1})",
             R"("repair":{"healthPerTick":2,"batteryPerTick":1e100})",
             R"("repair":false)"}) {
        auto invalidRepair = unitsText;
        invalidRepair.replace(repairBegin, repairEnd - repairBegin + 1, repair);
        writeFixture(invalidUnits, invalidRepair);
        valid = rejects(invalidUnits, "assets/gameplay/recipes.json", "assets/gameplay/countries.json",
                        "invalid repair values") && valid;
    }
    auto noBattery = unitsText;
    const auto batteryBegin = noBattery.find("\"battery\": {");
    const auto batteryEnd = noBattery.find('}', batteryBegin);
    noBattery.erase(batteryBegin, batteryEnd - batteryBegin + 2);
    writeFixture(invalidUnits, noBattery);
    valid = rejects(invalidUnits, "assets/gameplay/recipes.json", "assets/gameplay/countries.json",
                    "invalid repair values") && valid;
    std::filesystem::remove_all(directory);
    if (!valid) {
        std::cerr << "Definition registry validation failed\n";
        return 1;
    }
    std::cout << "Definition registry validation passed\n";
    return 0;
}
