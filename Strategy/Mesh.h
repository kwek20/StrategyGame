#ifndef __MESH_H_INCLUDED__
#define __MESH_H_INCLUDED__

#pragma once

#include <allegro5\allegro.h>
#include "allegro5\allegro_opengl.h"

#include "assimp\scene.h"
#include "assimp\mesh.h"

#include <vector>

/**
 * Mesh code from http://www.nexcius.net/2014/04/13/loading-meshes-using-assimp-in-opengl/
 */
class Mesh
{
public:
	struct MeshEntry {
		static enum BUFFERS {
			VERTEX_BUFFER, TEXCOORD_BUFFER, NORMAL_BUFFER, INDEX_BUFFER
		};
		GLuint vao;
		GLuint vbo[4];

		unsigned int elementCount;

		MeshEntry(aiMesh *mesh);
		~MeshEntry();

		void load(aiMesh *mesh);
		void render();
	};

	std::vector<MeshEntry*> meshEntries;

public:
	Mesh(const char *filename);
	~Mesh(void);

	void render();
};
#endif