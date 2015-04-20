#ifndef __MESH_H_INCLUDED__
#define __MESH_H_INCLUDED__

#pragma once

#include <windows.h>

#include <iostream>

// assimp include files. These three are usually needed.
#include "assimp/Importer.hpp"	//OO version Header!
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "assimp/DefaultLogger.hpp"
#include "assimp/LogStream.hpp"

#include <GL/gl.h>
#include <GL/glu.h>

#include <allegro5\allegro.h>
#include <allegro5\allegro_opengl.h>

#include "Vec.hpp"
#include "Texture.h"

struct Vertex {
	Vec3<float> m_pos;
	Vec2<float> m_tex;
	Vec3<float> m_normal;

	Vertex() {}

	Vertex(const Vec3<float>& pos, const Vec2<float>& tex, const Vec3<float>& normal)
	{
		m_pos = pos;
		m_tex = tex;
		m_normal = normal;
	}
};


class Mesh {
public:
	Mesh();

	~Mesh();

	bool LoadMesh(const std::string& Filename);

	void Render();

private:
	bool InitFromScene(const aiScene* pScene, const std::string& Filename);
	void InitMesh(unsigned int Index, const aiMesh* paiMesh);
	bool InitMaterials(const aiScene* pScene, const std::string& Filename);
	void Clear();

	#define INVALID_OGL_VALUE 0xFFFFFFFF
	#define INVALID_MATERIAL 0xFFFFFFFF
	#define SAFE_DELETE(p) if (p) { delete p; p = NULL; }

	struct MeshEntry {
		MeshEntry();

		~MeshEntry();

		void Init(const std::vector<Vertex>& Vertices,
			const std::vector<unsigned int>& Indices);

		GLuint VB;
		GLuint IB;
		unsigned int NumIndices;
		unsigned int MaterialIndex;
	};

	std::vector<MeshEntry> m_Entries;
	std::vector<Texture*> m_Textures;
};

#endif