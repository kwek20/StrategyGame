#include "render/ShaderManager.hpp"

#include <glad/glad.h>
#include <fstream>
#include <sstream>
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

std::string readText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Could not open shader file: " + path.string());
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

std::uint32_t buildProgram(std::string_view vertexSource,
                           std::string_view fragmentSource,
                           const std::string& name) {
    GLuint vertex = 0;
    GLuint fragment = 0;
    GLuint program = 0;
    try {
        vertex = compile(GL_VERTEX_SHADER, vertexSource, name + ":vertex");
        fragment = compile(GL_FRAGMENT_SHADER, fragmentSource, name + ":fragment");
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        vertex = fragment = 0;
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            GLint length = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(length), '\0');
            glGetProgramInfoLog(program, length, nullptr, log.data());
            throw std::runtime_error("Shader '" + name + "' linking failed: " + log);
        }
        return program;
    } catch (...) {
        if (vertex != 0)
            glDeleteShader(vertex);
        if (fragment != 0)
            glDeleteShader(fragment);
        if (program != 0)
            glDeleteProgram(program);
        throw;
    }
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
    try {
        slot.program = buildProgram(vertexSource, fragmentSource, slot.name);
        slot.state = ResourceState::ready;
    } catch (const std::exception& exception) {
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

ShaderHandle ShaderManager::loadFiles(std::string name,
                                      const std::filesystem::path& vertexPath,
                                      const std::filesystem::path& fragmentPath) {
    const std::string vertex = readText(vertexPath);
    const std::string fragment = readText(fragmentPath);
    const ShaderHandle handle = load(std::move(name), vertex, fragment);
    Slot& slot = slots_[handle.index];
    slot.vertexPath = vertexPath;
    slot.fragmentPath = fragmentPath;
    slot.vertexWriteTime = std::filesystem::last_write_time(vertexPath);
    slot.fragmentWriteTime = std::filesystem::last_write_time(fragmentPath);
    return handle;
}

std::vector<ShaderReloadResult> ShaderManager::reloadChanged() {
    std::vector<ShaderReloadResult> results;
#ifdef STRATEGY_SHADER_HOT_RELOAD
    const auto now = std::chrono::steady_clock::now();
    if (now < nextReloadScan_)
        return results;
    nextReloadScan_ = now + std::chrono::milliseconds(250);
    for (Slot& slot : slots_) {
        if (slot.vertexPath.empty() || slot.fragmentPath.empty())
            continue;
        try {
            const auto vertexTime = std::filesystem::last_write_time(slot.vertexPath);
            const auto fragmentTime = std::filesystem::last_write_time(slot.fragmentPath);
            if (vertexTime == slot.vertexWriteTime && fragmentTime == slot.fragmentWriteTime)
                continue;
            slot.vertexWriteTime = vertexTime;
            slot.fragmentWriteTime = fragmentTime;
            const GLuint replacement = buildProgram(
                readText(slot.vertexPath), readText(slot.fragmentPath), slot.name);
            glDeleteProgram(slot.program);
            slot.program = replacement;
            slot.uniforms.clear();
            slot.error.clear();
            slot.state = ResourceState::ready;
            results.push_back({slot.name, true, "reloaded"});
        } catch (const std::exception& exception) {
            const std::string message = exception.what();
            if (message != slot.error)
                results.push_back({slot.name, false, message});
            slot.error = message;
        }
    }
#endif
    return results;
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
