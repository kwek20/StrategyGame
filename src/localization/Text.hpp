#pragma once
#include <initializer_list>
#include <string>
namespace strategy {
class Text final {
  public:
    [[nodiscard]] static std::string get(const std::string& key);
    [[nodiscard]] static std::string format(const std::string& key,
                                            std::initializer_list<std::string> arguments);
};
} // namespace strategy
