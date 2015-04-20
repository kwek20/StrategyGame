#include <iostream>

#include "Texture.h"

Texture::Texture(GLenum TextureTarget, const std::string fileName){
	m_textureTarget = TextureTarget;
	m_bitmap = al_load_bitmap(fileName.c_str());
}

bool Texture::Load(){
	m_textureObj = al_get_opengl_texture(m_bitmap);
	if (m_textureObj != 0)
		glBindTexture(m_textureTarget, m_textureObj);

	return true;
}

void Texture::Bind(GLenum TextureUnit){
	glActiveTexture(TextureUnit);
	glBindTexture(m_textureTarget, m_textureObj);
}
