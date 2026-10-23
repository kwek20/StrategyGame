#include "reference/ResourceSiteMapReference.hpp"
#include "world/ResourceSiteMap.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

void compareReference() {
    using namespace strategy;
    for (unsigned seed = 0; seed < 16; ++seed) {
        std::mt19937 random{seed};
        const float extent = seed % 2 ? 127.5F : 64.F;
        const auto unit = [&] { return static_cast<float>(random() % 10001) / 10000.F; };
        std::vector<ResourceSiteSource> sources;
        for (unsigned i = 0; i < 48; ++i) {
            const auto field = i / 12;
            const glm::vec2 center{(static_cast<float>(field % 2) - .5F) * extent * .5F,
                                   (static_cast<float>(field / 2) - .5F) * extent * .5F};
            sources.push_back({100 + i,
                               center + glm::vec2{unit() * 12 - 6, unit() * 12 - 6},
                               .2F + unit() * 4,
                               static_cast<std::uint8_t>((i / 12) % 3),
                               field});
        }
        // Bucket boundaries, exact link limits, coincident points and distinct field identities.
        const std::vector<ResourceSiteSource> boundaries{{200, {-22, 0}, 1, 0, 0},
                                                         {201, {0, 0}, 1, 0, 0},
                                                         {202, {22, 0}, 1, 0, 0},
                                                         {203, {-6, -6}, 1, 1, 99},
                                                         {204, {0, -6}, 1, 1, 99},
                                                         {205, {6.001F, -6}, 1, 1, 99},
                                                         {206, {-.001F, -.001F}, 1, 1, 99},
                                                         {207, {.001F, .001F}, 1, 1, 99},
                                                         {208, {0, 0}, .01F, 2, 100},
                                                         {209, {0, 0}, .01F, 2, 101}};
        sources.insert(sources.end(), boundaries.begin(), boundaries.end());
        sources.push_back({310, {-1e-8F, 12}, .7F, 1, 999});
        sources.push_back({311, {6, 12}, .7F, 1, 999});
        sources.push_back({312, {-1e-8F, -25}, .7F, 2, 0});
        sources.push_back({313, {22, -25}, .7F, 2, 0});

        if (seed == 0)
            sources.push_back({300, {0, 0}, 256, 1, 0});
        std::shuffle(sources.begin(), sources.end(), random);
        ResourceSiteMap optimized;
        strategy::test::ResourceSiteMapReference reference;
        optimized.update(sources, extent);
        reference.update(sources, extent);
        require(optimized.pixels() == reference.pixels(),
                "Optimized initial painting matches the frozen original byte for byte");
        sources.erase(sources.begin() + 5);
        sources.front().fieldId = 500;
        sources.back().position += glm::vec2{7, -3};
        sources.back().radius *= .7F;
        optimized.update(sources, extent);
        reference.update(sources, extent);
        require(optimized.pixels() == reference.pixels(),
                "Removal, movement and field edits retain original incremental pixels");
        optimized.clear();
        optimized.beginUpdate(sources, extent);
        while (!optimized.advanceUpdate()) {
        }
        reference.clear();
        reference.update(sources, extent);
        require(optimized.pixels() == reference.pixels(),
                "Sliced loading uses the same optimized image as the original painter");
    }
}

bool benchmarkRequested() {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    _dupenv_s(&value, &length, "STRATEGY_RESOURCE_PAINT_BENCHMARK");
    const bool enabled = value && *value;
    std::free(value);
    return enabled;
#else
    return std::getenv("STRATEGY_RESOURCE_PAINT_BENCHMARK") != nullptr;
#endif
}

void benchmark() {
    using namespace strategy;
    std::vector<ResourceSiteSource> sources;
    std::mt19937 random{31};
    for (unsigned field = 0; field < 64; ++field)
        for (unsigned node = 0; node < 12; ++node) {
            const glm::vec2 center{(static_cast<float>(field % 8) - 3.5F) * 40,
                                   (static_cast<float>(field / 8) - 3.5F) * 40};
            const glm::vec2 offset{static_cast<float>(random() % 1401) / 100 - 7,
                                   static_cast<float>(random() % 1401) / 100 - 7};
            sources.push_back({field * 12 + node + 1,
                               center + offset,
                               .8F,
                               static_cast<std::uint8_t>(field % 3),
                               field + 1});
        }
    const auto timed = [](auto&& action) {
        const auto start = std::chrono::steady_clock::now();
        action();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
            .count();
    };
    std::vector<double> oldCold, newCold, oldEdit, newEdit;
    for (int run = 0; run < 3; ++run) {
        test::ResourceSiteMapReference reference;
        ResourceSiteMap optimized;
        oldCold.push_back(timed([&] { reference.update(sources, 360); }));
        newCold.push_back(timed([&] { optimized.update(sources, 360); }));
        require(reference.pixels() == optimized.pixels(), "Benchmark initial images match");
        auto edited = sources;
        edited.erase(edited.begin() + 381);
        oldEdit.push_back(timed([&] { reference.update(edited, 360); }));
        newEdit.push_back(timed([&] { optimized.update(edited, 360); }));
        require(reference.pixels() == optimized.pixels(), "Benchmark local edits match");
        require(optimized.update(edited, 360).empty(), "Unchanged benchmark frames stay cached");
    }
    const auto median = [](auto values) {
        std::sort(values.begin(), values.end());
        return values[1];
    };
    std::cout << "PAINT BENCHMARK 768 nodes, 64 fields, 480x480 pixels; median of 3\n"
              << "cold reference_ms=" << median(oldCold) << " optimized_ms=" << median(newCold)
              << '\n'
              << "one-node removal reference_ms=" << median(oldEdit)
              << " optimized_ms=" << median(newEdit) << '\n';
}
} // namespace
int strategyTestMain() {
    using namespace strategy;
    compareReference();
    if (benchmarkRequested())
        benchmark();
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
    {
        ResourceSiteMap sliced;
        sliced.beginUpdate(sources, 480);
        require(sliced.updateProgress() == 0,
                "Starting preparation does not rasterize pixels synchronously");
        std::size_t steps = 0;
        while (!sliced.advanceUpdate())
            ++steps;
        require(steps > 1 && sliced.updateProgress() == 1 && sliced.pixels() == pixels,
                "Tile-bounded preparation reproduces synchronous output exactly");
        require(sliced.update(sources, 480).empty(),
                "Prepared source identity survives entry into gameplay");
        sliced.clear();
        sliced.beginUpdate(sources, 480);
        sliced.advanceUpdate();
        auto replacement = sources;
        replacement.pop_back();
        sliced.beginUpdate(replacement, 240);
        while (!sliced.advanceUpdate()) {
        }
        ResourceSiteMap expected;
        expected.update(replacement, 240);
        require(sliced.pixels() == expected.pixels(),
                "Abandoned preparation cannot contaminate a new map");
        sliced.beginUpdate(sources, 240);
        sliced.advanceUpdate();
        sliced.clear();
        require(sliced.advanceUpdate() && sliced.side() == 0,
                "Cancellation drops pending tile work immediately");
    }

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
