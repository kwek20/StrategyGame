#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace strategy {

// Stable values are persisted instead of UI indices. New match options belong in this profile;
// the JSON store preserves fields it does not yet understand so future biome settings survive
// older builds opening and rewriting the profile.
struct MatchSetupProfile {
    std::uint32_t terrainSeed{0x5EED1234U};
    std::string playerOneCountry{"spain"};
    std::string playerTwoCountry{"japan"};
    std::uint32_t mapChunksPerSide{15};
    float startingResourcesScale{1.0F};
    float resourceAbundanceScale{1.0F};
    std::string terrainLayout{"continental"};
};

class MatchSetupProfileStore final {
  public:
    [[nodiscard]] static MatchSetupProfile load(const std::filesystem::path& path);
    static void write(const std::filesystem::path& path, const MatchSetupProfile& profile);
};

} // namespace strategy
