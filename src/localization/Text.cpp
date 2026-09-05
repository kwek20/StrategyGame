#include "localization/Text.hpp"

#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <stdexcept>
#include <unordered_map>
namespace strategy {
namespace {
const std::unordered_map<std::string, std::string>& catalogue() {
    static const auto values = [] {
        std::ifstream stream("assets/text/en_us.json");
        if (!stream)
            throw std::runtime_error("Could not open language file: assets/text/en_us.json");
        rapidjson::IStreamWrapper input(stream);
        rapidjson::Document document;
        document.ParseStream(input);
        if (document.HasParseError() || !document.IsObject())
            throw std::runtime_error("Invalid language file: assets/text/en_us.json");
        std::unordered_map<std::string, std::string> result;
        for (const auto& member : document.GetObject())
            if (member.value.IsString())
                result.emplace(member.name.GetString(), member.value.GetString());
        return result;
    }();
    return values;
}
} // namespace
std::string Text::get(const std::string& key) {
    const auto found = catalogue().find(key);
    return found == catalogue().end() ? "[" + key + "]" : found->second;
}
std::string Text::format(const std::string& key, std::initializer_list<std::string> arguments) {
    std::string result = get(key);
    std::size_t index = 0;
    for (const std::string& argument : arguments) {
        const std::string marker = "{" + std::to_string(index++) + "}";
        std::size_t position = 0;
        while ((position = result.find(marker, position)) != std::string::npos) {
            result.replace(position, marker.size(), argument);
            position += argument.size();
        }
    }
    return result;
}
} // namespace strategy
