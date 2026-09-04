#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace strategy {
struct CountryDefinition { std::string id,nameKey,specializationId{"unassigned"}; };
class CountryCatalogue final {
public:
    explicit CountryCatalogue(const std::filesystem::path& path="assets/gameplay/countries.json");
    [[nodiscard]] const std::vector<CountryDefinition>& countries()const{return countries_;}
private: std::vector<CountryDefinition> countries_;
};
}
