/*

Copyright 2011 Etay Meiri

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __TEXTURE_H_INCLUDED__
#define	__TEXTURE_H_INCLUDED__

#include <string>

#include <windows.h>

#include <GL/gl.h>
#include <GL/glu.h>

#include <allegro5\allegro.h>
#include <allegro5\allegro_opengl.h>

class Texture
{
public:
	Texture(GLenum TextureTarget, const std::string fileName);

	bool Load();

	void Bind(GLenum TextureUnit);

private:
	GLenum m_textureTarget;
	GLuint m_textureObj;

	ALLEGRO_BITMAP *m_bitmap;
};


#endif	/* TEXTURE_H */

