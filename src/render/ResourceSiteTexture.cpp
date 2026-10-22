#include "render/ResourceSiteTexture.hpp"

#include <glad/glad.h>

namespace strategy {
ResourceSiteTexture::~ResourceSiteTexture() {
    clear();
}
void ResourceSiteTexture::clear() {
    if (texture_)
        glDeleteTextures(1, &texture_);
    texture_ = 0;
    map_.clear();
    uploadedTiles_ = 0;
}
void ResourceSiteTexture::update(std::span<const ResourceSiteSource> sources, float extent) {
    const auto oldSide = map_.side();
    const auto updates = map_.update(sources, extent);
    uploadedTiles_ = updates.size();
    if (updates.empty())
        return;
    if (!texture_)
        glGenTextures(1, &texture_);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, texture_);
    GLint rowLength = 0, alignment = 0;
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (oldSide != map_.side()) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA8,
                     map_.side(),
                     map_.side(),
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     map_.pixels().data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, map_.side());
        for (const auto& tile : updates)
            glTexSubImage2D(GL_TEXTURE_2D,
                            0,
                            tile.x,
                            tile.y,
                            tile.width,
                            tile.height,
                            GL_RGBA,
                            GL_UNSIGNED_BYTE,
                            map_.pixels().data() +
                                (static_cast<std::size_t>(tile.y) * map_.side() + tile.x) * 4);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    glActiveTexture(GL_TEXTURE0);
}
void ResourceSiteTexture::bind() const {
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glActiveTexture(GL_TEXTURE0);
}
} // namespace strategy
