#include "render/ShaderManager.hpp"

#include <glad/glad.h>
#include <stdexcept>

namespace strategy {
namespace {
std::uint32_t compile(GLenum type, std::string_view source, const std::string& name) {
    const GLuint shader = glCreateShader(type);
    const char* text = source.data();
    const GLint length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE)
        return shader;
    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(logLength), '\0');
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error("Shader '" + name + "' compilation failed: " + log);
}
} // namespace

ShaderManager::~ShaderManager() {
    for (const Slot& slot : slots_)
        if (slot.program != 0)
            glDeleteProgram(slot.program);
}

ShaderHandle ShaderManager::load(std::string name,
                                 std::string_view vertexSource,
                                 std::string_view fragmentSource) {
    if (const auto found = handles_.find(name); found != handles_.end()) {
        if (state(found->second) == ResourceState::failed)
            throw std::runtime_error(error(found->second));
        return found->second;
    }
    Slot slot;
    slot.name = std::move(name);
    slot.state = ResourceState::importing;
    GLuint vertex = 0;
    GLuint fragment = 0;
    try {
        vertex = compile(GL_VERTEX_SHADER, vertexSource, slot.name + ":vertex");
        fragment = compile(GL_FRAGMENT_SHADER, fragmentSource, slot.name + ":fragment");
        slot.program = glCreateProgram();
        glAttachShader(slot.program, vertex);
        glAttachShader(slot.program, fragment);
        glLinkProgram(slot.program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        vertex = 0;
        fragment = 0;
        GLint linked = GL_FALSE;
        glGetProgramiv(slot.program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            GLint length = 0;
            glGetProgramiv(slot.program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(length), '\0');
            glGetProgramInfoLog(slot.program, length, nullptr, log.data());
            throw std::runtime_error("Shader '" + slot.name + "' linking failed: " + log);
        }
        slot.state = ResourceState::ready;
    } catch (const std::exception& exception) {
        if (vertex != 0)
            glDeleteShader(vertex);
        if (fragment != 0)
            glDeleteShader(fragment);
        if (slot.program != 0) {
            glDeleteProgram(slot.program);
            slot.program = 0;
        }
        slot.state = ResourceState::failed;
        slot.error = exception.what();
    }
    const std::uint32_t index = static_cast<std::uint32_t>(slots_.size());
    slots_.push_back(std::move(slot));
    const ShaderHandle handle{index, slots_.back().generation};
    handles_.emplace(slots_.back().name, handle);
    if (slots_.back().state == ResourceState::failed)
        throw std::runtime_error(slots_.back().error);
    return handle;
}

ResourceState ShaderManager::state(ShaderHandle handle) const {
    if (!handle || handle.index >= slots_.size() ||
        slots_[handle.index].generation != handle.generation)
        return ResourceState::invalid;
    return slots_[handle.index].state;
}

std::uint32_t ShaderManager::program(ShaderHandle handle) const {
    if (state(handle) != ResourceState::ready)
        throw std::runtime_error("Invalid or unavailable shader handle");
    return slots_[handle.index].program;
}

void ShaderManager::use(ShaderHandle handle) const { glUseProgram(program(handle)); }

std::int32_t ShaderManager::uniform(ShaderHandle handle, std::string_view name) const {
    if (state(handle) != ResourceState::ready)
        throw std::runtime_error("Cannot query a uniform from an unavailable shader");
    Slot& slot = const_cast<Slot&>(slots_.at(handle.index));
    const std::string key{name};
    if (const auto found = slot.uniforms.find(key); found != slot.uniforms.end())
        return found->second;
    const GLint location = glGetUniformLocation(program(handle), key.c_str());
    slot.uniforms.emplace(key, location);
    return location;
}

const std::string& ShaderManager::error(ShaderHandle handle) const {
    static const std::string invalid{"Invalid shader handle"};
    return state(handle) == ResourceState::invalid ? invalid : slots_[handle.index].error;
}

} // namespace strategy
