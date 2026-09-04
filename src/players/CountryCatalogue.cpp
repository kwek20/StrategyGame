#include "players/CountryCatalogue.hpp"
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <fstream>
#include <stdexcept>
namespace strategy {
CountryCatalogue::CountryCatalogue(const std::filesystem::path& path){std::ifstream stream(path);if(!stream)throw std::runtime_error("Could not open country catalogue: "+path.string());rapidjson::IStreamWrapper input(stream);rapidjson::Document document;document.ParseStream(input);if(document.HasParseError()||!document.IsObject()||!document.HasMember("countries")||!document["countries"].IsArray())throw std::runtime_error("Invalid country catalogue: "+path.string());for(const auto& value:document["countries"].GetArray()){if(!value.IsObject()||!value.HasMember("id")||!value["id"].IsString()||!value.HasMember("nameKey")||!value["nameKey"].IsString())continue;CountryDefinition country;country.id=value["id"].GetString();country.nameKey=value["nameKey"].GetString();if(value.HasMember("specialization")&&value["specialization"].IsString())country.specializationId=value["specialization"].GetString();countries_.push_back(std::move(country));}if(countries_.empty())throw std::runtime_error("Country catalogue contains no countries");}
}
