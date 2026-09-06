#include "render/MaterialManager.hpp"

#include <stdexcept>

namespace strategy {

MaterialHandle MaterialManager::create(RenderMaterial material) {
    if (material.name.empty() || !material.shader)
        throw std::runtime_error("A render material requires a name and shader");
    if (const auto found = handles_.find(material.name); found != handles_.end())
        return found->second;
    const std::uint32_t index = static_cast<std::uint32_t>(slots_.size());
    slots_.push_back({1, std::move(material)});
    const MaterialHandle handle{index, slots_.back().generation};
    handles_.emplace(slots_.back().material.name, handle);
    return handle;
}

MaterialHandle MaterialManager::find(const std::string& name) const {
    const auto found = handles_.find(name);
    return found == handles_.end() ? MaterialHandle{} : found->second;
}

const RenderMaterial& MaterialManager::get(MaterialHandle handle) const {
    if (!handle || handle.index >= slots_.size() ||
        slots_[handle.index].generation != handle.generation)
        throw std::runtime_error("Invalid material handle");
    return slots_[handle.index].material;
}

} // namespace strategy
