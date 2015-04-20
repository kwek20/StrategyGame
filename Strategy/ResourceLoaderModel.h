#ifndef __RESOURCELOADERMODEL_H_INCLUDED__
#define __RESOURCELOADERMODEL_H_INCLUDED__

#pragma once

#include "ResourceLoader.h"

#include "assimp/Importer.hpp"	//OO version Header!

#include "Model.h"

#include <string>
#include <iostream>
#include <map>

#include <allegro5\allegro_opengl.h>

class ModelLoader : public ResourceLoader<Model>
{
public:
	ModelLoader(std::string p) : ResourceLoader(p){};
	Model *loadFile(const char *fileName);
	
	const aiScene *Import3DFromFile(const std::string& pFile);
	Model *LoadGLTextures(const aiScene* scene, const std::string modelPath);
	void loadDataFile(int delSize, std::string path);
private:
	std::string getBasePath(const std::string& path);

	// Create an instance of the Importer class
	Assimp::Importer importer;
};

#endif