#pragma once

#include <string>
#include <string_view>

namespace strategy {

template <typename Tag> struct DefinitionId {
    std::string value;
    DefinitionId() = default;
    explicit DefinitionId(std::string_view identifier)
        : value(identifier) {}
    [[nodiscard]] bool empty() const { return value.empty(); }
    friend bool operator==(const DefinitionId&, const DefinitionId&) = default;
};

} // namespace strategy
