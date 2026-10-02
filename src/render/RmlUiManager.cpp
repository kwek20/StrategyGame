#include "render/RmlUiManager.hpp"

#include "localization/Text.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <glad/glad.h>
#include <stb_image.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace strategy {
namespace {

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE)
        return shader;
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string message(static_cast<std::size_t>(std::max(length, 1)), '\0');
    glGetShaderInfoLog(shader, length, nullptr, message.data());
    glDeleteShader(shader);
    throw std::runtime_error("Could not compile RmlUi shader: " + message);
}

GLuint createProgram() {
    constexpr const char* vertex = R"(#version 330 core
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;
uniform vec2 viewport;
uniform vec2 translation;
out vec4 color;
out vec2 texCoord;
void main() {
    vec2 pixel = inPosition + translation;
    gl_Position = vec4(pixel.x / viewport.x * 2.0 - 1.0,
                       1.0 - pixel.y / viewport.y * 2.0, 0.0, 1.0);
    color = inColor;
    texCoord = inTexCoord;
})";
    constexpr const char* fragment = R"(#version 330 core
in vec4 color;
in vec2 texCoord;
uniform sampler2D imageTexture;
uniform bool useTexture;
out vec4 finalColor;
void main() {
    finalColor = useTexture ? texture(imageTexture, texCoord) * color : color;
})";
    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertex);
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragment);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_TRUE)
        return program;
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string message(static_cast<std::size_t>(std::max(length, 1)), '\0');
    glGetProgramInfoLog(program, length, nullptr, message.data());
    glDeleteProgram(program);
    throw std::runtime_error("Could not link RmlUi shader: " + message);
}

class SystemInterface final : public Rml::SystemInterface {
  public:
    double GetElapsedTime() override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

  private:
    std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
};

class RenderInterface final : public Rml::RenderInterface {
  public:
    RenderInterface()
        : program_(createProgram()) {}
    ~RenderInterface() override {
        glDeleteProgram(program_);
    }

    void setViewport(int width, int height) {
        width_ = std::max(width, 1);
        height_ = std::max(height, 1);
    }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override {
        auto* geometry = new Geometry;
        geometry->count = static_cast<GLsizei>(indices.size());
        glGenVertexArrays(1, &geometry->vao);
        glGenBuffers(1, &geometry->vbo);
        glGenBuffers(1, &geometry->ibo);
        glBindVertexArray(geometry->vao);
        glBindBuffer(GL_ARRAY_BUFFER, geometry->vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(Rml::Vertex)),
                     vertices.data(),
                     GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry->ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(indices.size() * sizeof(int)),
                     indices.data(),
                     GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0,
                              2,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(Rml::Vertex),
                              reinterpret_cast<void*>(offsetof(Rml::Vertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1,
                              4,
                              GL_UNSIGNED_BYTE,
                              GL_TRUE,
                              sizeof(Rml::Vertex),
                              reinterpret_cast<void*>(offsetof(Rml::Vertex, colour)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2,
                              2,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(Rml::Vertex),
                              reinterpret_cast<void*>(offsetof(Rml::Vertex, tex_coord)));
        glBindVertexArray(0);
        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
    }

    void RenderGeometry(Rml::CompiledGeometryHandle handle,
                        Rml::Vector2f translation,
                        Rml::TextureHandle texture) override {
        const auto* geometry = reinterpret_cast<const Geometry*>(handle);
        glUseProgram(program_);
        glUniform2f(glGetUniformLocation(program_, "viewport"),
                    static_cast<float>(width_),
                    static_cast<float>(height_));
        glUniform2f(glGetUniformLocation(program_, "translation"), translation.x, translation.y);
        glUniform1i(glGetUniformLocation(program_, "useTexture"), texture ? 1 : 0);
        if (texture) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
            glUniform1i(glGetUniformLocation(program_, "imageTexture"), 0);
        }
        glBindVertexArray(geometry->vao);
        glDrawElements(GL_TRIANGLES, geometry->count, GL_UNSIGNED_INT, nullptr);
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override {
        auto* geometry = reinterpret_cast<Geometry*>(handle);
        if (!geometry)
            return;
        glDeleteBuffers(1, &geometry->ibo);
        glDeleteBuffers(1, &geometry->vbo);
        glDeleteVertexArrays(1, &geometry->vao);
        delete geometry;
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
        int width = 0, height = 0, channels = 0;
        unsigned char* pixels = stbi_load(source.c_str(), &width, &height, &channels, 4);
        if (!pixels)
            return {};
        for (int pixel = 0; pixel < width * height; ++pixel) {
            const int base = pixel * 4;
            const unsigned int alpha = pixels[base + 3];
            pixels[base] = static_cast<unsigned char>(pixels[base] * alpha / 255U);
            pixels[base + 1] = static_cast<unsigned char>(pixels[base + 1] * alpha / 255U);
            pixels[base + 2] = static_cast<unsigned char>(pixels[base + 2] * alpha / 255U);
        }
        const auto handle = uploadTexture(pixels, {width, height});
        stbi_image_free(pixels);
        dimensions = {width, height};
        return handle;
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i dimensions) override {
        return uploadTexture(source.data(), dimensions);
    }

    void ReleaseTexture(Rml::TextureHandle handle) override {
        const GLuint texture = static_cast<GLuint>(handle);
        glDeleteTextures(1, &texture);
    }

    void EnableScissorRegion(bool enable) override {
        if (enable)
            glEnable(GL_SCISSOR_TEST);
        else
            glDisable(GL_SCISSOR_TEST);
    }

    void SetScissorRegion(Rml::Rectanglei region) override {
        glScissor(region.Left(), height_ - region.Bottom(), region.Width(), region.Height());
    }

  private:
    struct Geometry {
        GLuint vao{};
        GLuint vbo{};
        GLuint ibo{};
        GLsizei count{};
    };

    static Rml::TextureHandle uploadTexture(const unsigned char* pixels, Rml::Vector2i dimensions) {
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA8,
                     dimensions.x,
                     dimensions.y,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return static_cast<Rml::TextureHandle>(texture);
    }

    GLuint program_{};
    int width_{1};
    int height_{1};
};

} // namespace

class RmlUiManager::Impl final : public Rml::EventListener {
  public:
    Impl() {
        Rml::SetSystemInterface(&system_);
        Rml::SetRenderInterface(&renderer_);
        if (!Rml::Initialise())
            throw std::runtime_error("Could not initialize RmlUi");
        context_ = Rml::CreateContext("strategy_ui", {1280, 720});
        if (!context_)
            throw std::runtime_error("Could not create RmlUi context");
        const std::array<std::filesystem::path, 4> fonts{"assets/fonts/Inter-Bold.ttf",
                                                         "C:/Windows/Fonts/segoeuib.ttf",
                                                         "assets/fonts/Inter-Regular.ttf",
                                                         "C:/Windows/Fonts/segoeui.ttf"};
        for (const auto& font : fonts) {
            if (std::filesystem::exists(font) && Rml::LoadFontFace(font.string())) {
                fontLoaded_ = true;
                if (font.filename().string().find("Bold") != std::string::npos ||
                    font.filename() == "segoeuib.ttf")
                    break;
            }
        }
        if (!fontLoaded_)
            throw std::runtime_error("Could not load an RmlUi font");
        // Documents such as the gameplay HUD replace dynamic children every frame. Activating on
        // press keeps a valid target even when that child is rebuilt before mouse release.
        context_->AddEventListener("mousedown", this);
    }

    ~Impl() override {
        while (!screens_.empty())
            removeScreen(screens_.back().handle);
        if (context_) {
            context_->RemoveEventListener("mousedown", this);
            Rml::RemoveContext(context_->GetName());
            context_ = nullptr;
        }
        Rml::Shutdown();
    }

    RmlUiScreenHandle pushScreen(RmlUiScreenDefinition definition) {
        pendingAction_.reset();
        if (screens_.empty())
            uiScale_ = std::clamp(definition.uiScale, 0.75F, 1.5F);
        if (!screens_.empty())
            screens_.back().document->Hide();
        Screen screen;
        screen.handle = nextHandle_++;
        screen.definition = std::move(definition);
        screen.document = context_->LoadDocument(screen.definition.documentPath);
        if (!screen.document)
            throw std::runtime_error("Could not load " + screen.definition.documentPath);
        for (const auto& [id, value] : screen.definition.text)
            if (Rml::Element* element = screen.document->GetElementById(id))
                element->SetInnerRML(value);
        screen.document->Show();
        screens_.push_back(std::move(screen));
        applyDensity();
        focusCurrent();
        return screens_.back().handle;
    }

    void setScale(float scale) {
        uiScale_ = std::clamp(scale, 0.75F, 1.5F);
        applyDensity();
        if (context_)
            context_->Update();
    }

    void removeScreen(RmlUiScreenHandle handle) {
        pendingAction_.reset();
        const auto found =
            std::find_if(screens_.begin(), screens_.end(), [handle](const Screen& screen) {
                return screen.handle == handle;
            });
        if (found == screens_.end())
            return;
        const bool wasActive = found == screens_.end() - 1;
        context_->UnloadDocument(found->document);
        screens_.erase(found);
        context_->Update();
        if (wasActive && !screens_.empty()) {
            screens_.back().document->Show();
            applyDensity();
            focusCurrent();
        }
    }

    void handleEvent(const SDL_Event& event) {
        if (screens_.empty())
            return;
        const int modifiers = modifierState();
        float density = 1.0F;
        Uint32 windowId = 0;
        if (event.type == SDL_EVENT_MOUSE_MOTION)
            windowId = event.motion.windowID;
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                 event.type == SDL_EVENT_MOUSE_BUTTON_UP)
            windowId = event.button.windowID;
        if (windowId)
            if (SDL_Window* window = SDL_GetWindowFromID(windowId))
                density = SDL_GetWindowPixelDensity(window);
        if (event.type == SDL_EVENT_MOUSE_MOTION)
            context_->ProcessMouseMove(static_cast<int>(event.motion.x * density),
                                       static_cast<int>(event.motion.y * density),
                                       modifiers);
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            // HUD content is regenerated as selection changes. Refresh the pointer from the
            // button event so a click cannot target the element that occupied the cursor during
            // the preceding frame when the physical mouse has not moved.
            context_->ProcessMouseMove(static_cast<int>(event.button.x * density),
                                       static_cast<int>(event.button.y * density),
                                       modifiers);
            context_->ProcessMouseButtonDown(mouseButton(event.button.button), modifiers);
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
            context_->ProcessMouseButtonUp(mouseButton(event.button.button), modifiers);
        else if (event.type == SDL_EVENT_MOUSE_WHEEL)
            context_->ProcessMouseWheel({-event.wheel.x, -event.wheel.y}, modifiers);
    }

    void focus(int direction) {
        if (screens_.empty() || screens_.back().definition.focusOrder.empty())
            return;
        auto& screen = screens_.back();
        const int count = static_cast<int>(screen.definition.focusOrder.size());
        screen.focusIndex = (screen.focusIndex + direction + count) % count;
        focusCurrent();
    }

    void activateFocused() {
        const std::string id = focusedId();
        if (!id.empty())
            pendingAction_ = id;
    }

    std::string focusedId() const {
        if (screens_.empty() || screens_.back().definition.focusOrder.empty())
            return {};
        const auto& screen = screens_.back();
        return screen.definition.focusOrder[static_cast<std::size_t>(screen.focusIndex)];
    }

    std::optional<std::string> takeAction() {
        return std::exchange(pendingAction_, std::nullopt);
    }

    bool pointerOverUi() const {
        return !screens_.empty() && context_->IsMouseInteracting();
    }

    void setControls(std::vector<std::string> focusOrder,
                     std::vector<std::string> actionIds) {
        if (screens_.empty())
            return;
        const std::string previous = focusedId();
        auto& screen = screens_.back();
        screen.definition.focusOrder = std::move(focusOrder);
        screen.definition.actionIds = std::move(actionIds);
        const auto found = std::find(screen.definition.focusOrder.begin(),
                                     screen.definition.focusOrder.end(), previous);
        screen.focusIndex = found == screen.definition.focusOrder.end()
                                ? 0
                                : static_cast<int>(found - screen.definition.focusOrder.begin());
        focusCurrent();
    }

    std::optional<RmlUiRect> bounds(const std::string& id) const {
        Rml::Element* element = elementById(id);
        if (!element)
            return std::nullopt;
        const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
        return RmlUiRect{offset.x, offset.y, offset.x + element->GetClientWidth(),
                         offset.y + element->GetClientHeight()};
    }

    void setText(const std::string& id, const std::string& text) {
        if (Rml::Element* element = elementById(id))
            element->SetInnerRML(text);
    }

    void setValue(const std::string& id, const std::string& value) {
        if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(elementById(id)))
            input->SetValue(value);
        else
            setText(id, value);
    }

    std::string value(const std::string& id) const {
        if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(elementById(id)))
            return input->GetValue();
        return {};
    }

    void setAttribute(const std::string& id, const std::string& name, const std::string& value) {
        if (Rml::Element* element = elementById(id))
            element->SetAttribute(name, value);
    }

    void setProperty(const std::string& id, const std::string& name, const std::string& value) {
        if (Rml::Element* element = elementById(id))
            element->SetProperty(name, value);
    }

    void render(int width, int height) {
        if (screens_.empty())
            return;
        renderer_.setViewport(width, height);
        context_->SetDimensions({width, height});
        applyDensity();
        context_->Update();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        context_->Render();
        glDisable(GL_SCISSOR_TEST);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
    }

    void ProcessEvent(Rml::Event& event) override {
        if (screens_.empty())
            return;
        Rml::Element* element = event.GetTargetElement();
        const auto& actions = screens_.back().definition.actionIds;
        while (element) {
            const std::string id = element->GetId();
            if (std::find(actions.begin(), actions.end(), id) != actions.end()) {
                auto& screen = screens_.back();
                const auto focused = std::find(
                    screen.definition.focusOrder.begin(), screen.definition.focusOrder.end(), id);
                if (focused != screen.definition.focusOrder.end())
                    screen.focusIndex =
                        static_cast<int>(focused - screen.definition.focusOrder.begin());
                pendingAction_ = id;
                return;
            }
            element = element->GetParentNode();
        }
    }

  private:
    struct Screen {
        RmlUiScreenHandle handle{};
        RmlUiScreenDefinition definition;
        Rml::ElementDocument* document{};
        int focusIndex{};
    };

    static int modifierState() {
        const SDL_Keymod modifiers = SDL_GetModState();
        int result = 0;
        if (modifiers & SDL_KMOD_CTRL)
            result |= Rml::Input::KM_CTRL;
        if (modifiers & SDL_KMOD_SHIFT)
            result |= Rml::Input::KM_SHIFT;
        if (modifiers & SDL_KMOD_ALT)
            result |= Rml::Input::KM_ALT;
        return result;
    }

    static int mouseButton(Uint8 button) {
        if (button == SDL_BUTTON_RIGHT)
            return 1;
        if (button == SDL_BUTTON_MIDDLE)
            return 2;
        return 0;
    }

    Rml::Element* elementById(const std::string& id) const {
        return screens_.empty() ? nullptr : screens_.back().document->GetElementById(id);
    }

    void applyDensity() {
        if (context_)
            context_->SetDensityIndependentPixelRatio(uiScale_);
    }

    void focusCurrent() {
        const std::string id = focusedId();
        if (!id.empty())
            if (Rml::Element* element = elementById(id))
                element->Focus(true);
    }

    SystemInterface system_;
    RenderInterface renderer_;
    Rml::Context* context_{};
    bool fontLoaded_{false};
    std::vector<Screen> screens_;
    RmlUiScreenHandle nextHandle_{1};
    float uiScale_{1.0F};
    std::optional<std::string> pendingAction_;
};

RmlUiManager::RmlUiManager()
    : impl_(std::make_unique<Impl>()) {}
RmlUiManager::~RmlUiManager() = default;
RmlUiScreenHandle RmlUiManager::pushScreen(RmlUiScreenDefinition definition) {
    return impl_->pushScreen(std::move(definition));
}
void RmlUiManager::setScale(float scale) {
    impl_->setScale(scale);
}
void RmlUiManager::removeScreen(RmlUiScreenHandle handle) {
    impl_->removeScreen(handle);
}
void RmlUiManager::handleEvent(const SDL_Event& event) {
    impl_->handleEvent(event);
}
void RmlUiManager::focus(int direction) {
    impl_->focus(direction);
}
void RmlUiManager::activateFocused() {
    impl_->activateFocused();
}
std::string RmlUiManager::focusedId() const {
    return impl_->focusedId();
}
std::optional<std::string> RmlUiManager::takeAction() {
    return impl_->takeAction();
}
bool RmlUiManager::pointerOverUi() const {
    return impl_->pointerOverUi();
}
void RmlUiManager::setControls(std::vector<std::string> focusOrder,
                               std::vector<std::string> actionIds) {
    impl_->setControls(std::move(focusOrder), std::move(actionIds));
}
std::optional<RmlUiRect> RmlUiManager::bounds(const std::string& id) const {
    return impl_->bounds(id);
}
void RmlUiManager::setText(const std::string& id, const std::string& text) {
    impl_->setText(id, text);
}
void RmlUiManager::setValue(const std::string& id, const std::string& value) {
    impl_->setValue(id, value);
}
std::string RmlUiManager::value(const std::string& id) const {
    return impl_->value(id);
}
void RmlUiManager::setAttribute(const std::string& id,
                                const std::string& name,
                                const std::string& value) {
    impl_->setAttribute(id, name, value);
}
void RmlUiManager::setProperty(const std::string& id,
                               const std::string& name,
                               const std::string& value) {
    impl_->setProperty(id, name, value);
}
void RmlUiManager::render(int width, int height) {
    impl_->render(width, height);
}

} // namespace strategy
