#ifndef __MODEL_H_INCLUDED__
#define __MODEL_H_INCLUDED__

#pragma once

#include "Resources.h"

#include "assimp/scene.h"
#include "assimp/Importer.hpp"	//OO version Header!

#include <string>
#include <iostream>
#include <map>

#include <allegro5\allegro_opengl.h>

class Model : public Resource<const aiScene>
{
public:
	Model(std::string p) : Resource(p){};
	const aiScene *loadFile(const char *fileName);
	
	const aiScene *Import3DFromFile(const std::string& pFile);
	int LoadGLTextures(const aiScene* scene, const std::string path);
	void loadDataFile(int delSize, std::string path);
private:
	std::string getBasePath(const std::string& path);

	// Create an instance of the Importer class
	Assimp::Importer importer;

	// images / texture
	std::map<std::string, GLuint*> textureIdMap;	// map image filenames to textureIds
	GLuint*		textureIds;							// pointer to texture Array
};

#endif