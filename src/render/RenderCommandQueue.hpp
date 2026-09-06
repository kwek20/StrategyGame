#pragma once

#include "assets/ResourceHandle.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <vector>

namespace strategy {

struct ModelRenderCommand {
    ModelHandle model;
    MaterialHandle material;
    glm::mat4 transform{1.0F};
    std::string animation;
    double animationSeconds{0.0};
    glm::vec3 tint{1.0F};
};

class RenderCommandQueue final {
  public:
    void submit(ModelRenderCommand command);
    void sort();
    void clear();
    [[nodiscard]] const std::vector<ModelRenderCommand>& commands() const { return commands_; }

  private:
    std::vector<ModelRenderCommand> commands_;
};

} // namespace strategy
