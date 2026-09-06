#include "render/RenderPass.hpp"

#include <glad/glad.h>

namespace strategy {
namespace {
void enabled(GLenum capability, bool value) {
    value ? glEnable(capability) : glDisable(capability);
}
} // namespace

RenderPass::RenderPass(RenderPassKind kind) {
    glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray_);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer_);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture_);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture2d_);
    glActiveTexture(static_cast<GLenum>(activeTexture_));
    depthTest_ = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
    cullFace_ = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
    blend_ = glIsEnabled(GL_BLEND) == GL_TRUE;

    const bool threeDimensional = kind != RenderPassKind::userInterface;
    enabled(GL_DEPTH_TEST, threeDimensional);
    enabled(GL_CULL_FACE, threeDimensional);
    enabled(GL_BLEND, kind != RenderPassKind::terrain);
}

RenderPass::~RenderPass() {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture2d_));
    glActiveTexture(static_cast<GLenum>(activeTexture_));
    glUseProgram(static_cast<GLuint>(program_));
    glBindVertexArray(static_cast<GLuint>(vertexArray_));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuffer_));
    enabled(GL_DEPTH_TEST, depthTest_);
    enabled(GL_CULL_FACE, cullFace_);
    enabled(GL_BLEND, blend_);
}

} // namespace strategy
