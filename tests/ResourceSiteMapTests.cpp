#include "world/ResourceSiteMap.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int strategyTestMain() {
    using namespace strategy;
    ResourceSiteMap map;
    {
        ResourceSiteMap oil;
        oil.update(std::vector<ResourceSiteSource>{{1, {0, 0}, 2, 1, 1}}, 64);
        std::size_t dryArea = 0, wetArea = 0;
        for (std::size_t i = 0; i < oil.pixels().size(); i += 4) {
            require(oil.pixels()[i] == 0 && oil.pixels()[i + 2] == 0,
                    "Oil cannot paint other resource families");
            dryArea += oil.pixels()[i + 1] > 0;
            wetArea += oil.pixels()[i + 3] > 0;
        }
        require(wetArea > 0 && wetArea < dryArea,
                "Wet oil cores are localized inside broader cracked-earth sites");
        oil.update({}, 64);
        require(
            std::all_of(oil.pixels().begin(), oil.pixels().end(), [](auto p) { return p == 0; }),
            "Removing oil clears its wet cores as well as its ground scar");
    }
    {
        ResourceSiteMap separated, joined;
        std::vector<ResourceSiteSource> nodes{{1, {-1.7F, 0}, .7F, 0, 1},
                                              {2, {1.7F, 0}, .7F, 0, 2}};
        separated.update(nodes, 64);
        nodes[1].fieldId = 1;
        joined.update(nodes, 64);
        require(separated.pixels() != joined.pixels(),
                "Nearby fields retain separate ground treatment identities");
        separated.update(nodes, 64);
        require(separated.pixels() == joined.pixels(),
                "Changing field membership invalidates the affected cached links");
    }
    std::vector<ResourceSiteSource> sources{
        {1, {-12, 0}, 3.8F, 0}, {2, {1, 3}, 5, 0}, {3, {155, 160}, 3.8F, 1}};
    const auto initial = map.update(sources, 480);
    const auto pixels = map.pixels();
    const auto revision = map.revision();
    require(!initial.empty() &&
                std::any_of(pixels.begin(), pixels.end(), [](auto p) { return p != 0; }),
            "Sites rasterize a nonempty mask");
    require(map.update(sources, 480).empty() && map.revision() == revision,
            "Unchanged frames reuse all raster data");
    std::reverse(sources.begin(), sources.end());
    require(map.update(sources, 480).empty(), "Entity iteration order cannot change presentation");
    ResourceSiteMap restored;
    restored.update(sources, 480);
    require(restored.pixels() == pixels,
            "Restored entity state reconstructs identical ground treatment");
    sources.erase(
        std::remove_if(sources.begin(), sources.end(), [](const auto& s) { return s.id == 1; }),
        sources.end());
    const auto changed = map.update(sources, 480);
    require(!changed.empty() && changed.size() < initial.size() / 2,
            "Removal dirties only nearby tiles");
    restored.clear();
    restored.update(sources, 480);
    require(restored.pixels() == map.pixels(),
            "Local removal clears the old source and its links exactly");
    sources[0].position = {-170, -165};
    map.update(sources, 480);
    restored.clear();
    restored.update(sources, 480);
    require(restored.pixels() == map.pixels(), "Movement clears old tiles and paints new tiles");
    map.update({}, 480);
    require(std::all_of(map.pixels().begin(), map.pixels().end(), [](auto p) { return p == 0; }),
            "World replacement cannot leave stale site stains");
    sources = {{4, {0, 0}, 3.8F, 2}};
    map.update(sources, 240);
    bool mineral = false;
    for (std::size_t i = 0; i < map.pixels().size(); i += 4) {
        require(map.pixels()[i] == 0 && map.pixels()[i + 1] == 0,
                "Resource families keep independent channels");
        mineral |= map.pixels()[i + 2] != 0;
    }
    require(mineral, "Mineral sites populate their channel");
    require(map.densityAt({120, 0}) == 0 &&
                map.densityAt({std::numeric_limits<float>::quiet_NaN(), 0}) == 0,
            "Out-of-map and invalid clearance probes cannot read beyond the mask");
    sources = {{4, {210, 0}, 35, 0}};
    map.update(sources, 480);
    require(std::isfinite(map.densityAt({239.99998F, 0})),
            "Near-boundary clearance probes remain safe");
    map.update({}, 480);
    restored.clear();
    restored.update({}, 480);
    require(map.pixels() == restored.pixels(),
            "Larger authored deposits clear their complete irregular envelope");
    bool rejected = false;
    try {
        sources[0].family = 3;
        map.update(sources, 240);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "Invalid inputs are rejected before corrupting a valid mask");
    rejected = false;
    try {
        sources = {{4, {0, 0}, 3.8F, 0}, {4, {10, 0}, 3.8F, 0}};
        map.update(sources, 480);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "Duplicate identities cannot make cache updates order dependent");
    std::cout << "Resource site cache, restoration and local update checks passed\n";
    return 0;
}
