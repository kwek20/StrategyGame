#include "assets/Texture.hpp"

#include <glad/glad.h>
#include <stdexcept>

namespace strategy {

Texture::Texture(std::shared_ptr<ModelTextureAsset> asset) {
    if (!asset)
        throw std::runtime_error("Cannot upload an empty texture asset");
    upload(*asset);
}

Texture::Texture(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    ModelTextureAsset marker;
    marker.width = marker.height = 2;
    marker.rgba = {red, green, blue, 255, 20, 20, 20, 255,
                   20, 20, 20, 255, red, green, blue, 255};
    upload(marker);
}

void Texture::upload(const ModelTextureAsset& asset) {
    if (asset.width <= 0 || asset.height <= 0 || asset.rgba.empty())
        throw std::runtime_error("Cannot upload invalid texture pixels");
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_SRGB8_ALPHA8,
                 asset.width,
                 asset.height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 asset.rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

Texture::~Texture() {
    if (id_ != 0)
        glDeleteTextures(1, &id_);
}

void Texture::bind(std::uint32_t unit, bool repeat) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, id_);
    const GLint wrapping = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapping);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapping);
}

} // namespace strategy
